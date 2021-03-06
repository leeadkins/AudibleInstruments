#include <string.h>
#include "AudibleInstruments.hpp"
#include "dsp/samplerate.hpp"
#include "dsp/ringbuffer.hpp"
#include "elements/dsp/part.h"


struct Elements : Module {
	enum ParamIds {
		CONTOUR_PARAM,
		BOW_PARAM,
		BLOW_PARAM,
		STRIKE_PARAM,
		COARSE_PARAM,
		FINE_PARAM,
		FM_PARAM,

		FLOW_PARAM,
		MALLET_PARAM,
		GEOMETRY_PARAM,
		BRIGHTNESS_PARAM,

		BOW_TIMBRE_PARAM,
		BLOW_TIMBRE_PARAM,
		STRIKE_TIMBRE_PARAM,
		DAMPING_PARAM,
		POSITION_PARAM,
		SPACE_PARAM,

		BOW_TIMBRE_MOD_PARAM,
		FLOW_MOD_PARAM,
		BLOW_TIMBRE_MOD_PARAM,
		MALLET_MOD_PARAM,
		STRIKE_TIMBRE_MOD_PARAM,
		DAMPING_MOD_PARAM,
		GEOMETRY_MOD_PARAM,
		POSITION_MOD_PARAM,
		BRIGHTNESS_MOD_PARAM,
		SPACE_MOD_PARAM,

		PLAY_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		NOTE_INPUT,
		FM_INPUT,
		GATE_INPUT,
		STRENGTH_INPUT,
		BLOW_INPUT,
		STRIKE_INPUT,

		BOW_TIMBRE_MOD_INPUT,
		FLOW_MOD_INPUT,
		BLOW_TIMBRE_MOD_INPUT,
		MALLET_MOD_INPUT,
		STRIKE_TIMBRE_MOD_INPUT,
		DAMPING_MOD_INPUT,
		GEOMETRY_MOD_INPUT,
		POSITION_MOD_INPUT,
		BRIGHTNESS_MOD_INPUT,
		SPACE_MOD_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		AUX_OUTPUT,
		MAIN_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		GATE_LIGHT,
		EXCITER_LIGHT,
		RESONATOR_LIGHT,
		NUM_LIGHTS
	};

	SampleRateConverter<2> inputSrc;
	SampleRateConverter<2> outputSrc;
	DoubleRingBuffer<Frame<2>, 256> inputBuffer;
	DoubleRingBuffer<Frame<2>, 256> outputBuffer;

	uint16_t reverb_buffer[32768] = {};
	elements::Part *part;

	Elements();
	~Elements();
	void step() override;

	json_t *toJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "model", json_integer(getModel()));
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		json_t *modelJ = json_object_get(rootJ, "model");
		if (modelJ) {
			setModel(json_integer_value(modelJ));
		}
	}

	int getModel() {
		return (int)part->resonator_model();
	}

	void setModel(int model) {
		part->set_resonator_model((elements::ResonatorModel)model);
	}
};


Elements::Elements() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
	part = new elements::Part();
	// In the Mutable Instruments code, Part doesn't initialize itself, so zero it here.
	memset(part, 0, sizeof(*part));
	part->Init(reverb_buffer);
	// Just some random numbers
	uint32_t seed[3] = {1, 2, 3};
	part->Seed(seed, 3);
}

Elements::~Elements() {
	delete part;
}

void Elements::step() {
	// Get input
	if (!inputBuffer.full()) {
		Frame<2> inputFrame;
		inputFrame.samples[0] = inputs[BLOW_INPUT].value / 5.0;
		inputFrame.samples[1] = inputs[STRIKE_INPUT].value / 5.0;
		inputBuffer.push(inputFrame);
	}

	// Render frames
	if (outputBuffer.empty()) {
		float blow[16] = {};
		float strike[16] = {};
		float main[16];
		float aux[16];

		// Convert input buffer
		{
			inputSrc.setRates(engineGetSampleRate(), 32000);
			Frame<2> inputFrames[16];
			int inLen = inputBuffer.size();
			int outLen = 16;
			inputSrc.process(inputBuffer.startData(), &inLen, inputFrames, &outLen);
			inputBuffer.startIncr(inLen);

			for (int i = 0; i < outLen; i++) {
				blow[i] = inputFrames[i].samples[0];
				strike[i] = inputFrames[i].samples[1];
			}
		}

		// Set patch from parameters
		elements::Patch* p = part->mutable_patch();
		p->exciter_envelope_shape = params[CONTOUR_PARAM].value;
		p->exciter_bow_level = params[BOW_PARAM].value;
		p->exciter_blow_level = params[BLOW_PARAM].value;
		p->exciter_strike_level = params[STRIKE_PARAM].value;

#define BIND(_p, _m, _i) clampf(params[_p].value + 3.3*quadraticBipolar(params[_m].value)*inputs[_i].value/5.0, 0.0, 0.9995)

		p->exciter_bow_timbre = BIND(BOW_TIMBRE_PARAM, BOW_TIMBRE_MOD_PARAM, BOW_TIMBRE_MOD_INPUT);
		p->exciter_blow_meta = BIND(FLOW_PARAM, FLOW_MOD_PARAM, FLOW_MOD_INPUT);
		p->exciter_blow_timbre = BIND(BLOW_TIMBRE_PARAM, BLOW_TIMBRE_MOD_PARAM, BLOW_TIMBRE_MOD_INPUT);
		p->exciter_strike_meta = BIND(MALLET_PARAM, MALLET_MOD_PARAM, MALLET_MOD_INPUT);
		p->exciter_strike_timbre = BIND(STRIKE_TIMBRE_PARAM, STRIKE_TIMBRE_MOD_PARAM, STRIKE_TIMBRE_MOD_INPUT);
		p->resonator_geometry = BIND(GEOMETRY_PARAM, GEOMETRY_MOD_PARAM, GEOMETRY_MOD_INPUT);
		p->resonator_brightness = BIND(BRIGHTNESS_PARAM, BRIGHTNESS_MOD_PARAM, BRIGHTNESS_MOD_INPUT);
		p->resonator_damping = BIND(DAMPING_PARAM, DAMPING_MOD_PARAM, DAMPING_MOD_INPUT);
		p->resonator_position = BIND(POSITION_PARAM, POSITION_MOD_PARAM, POSITION_MOD_INPUT);
		p->space = clampf(params[SPACE_PARAM].value + params[SPACE_MOD_PARAM].value*inputs[SPACE_MOD_INPUT].value/5.0, 0.0, 2.0);

		// Get performance inputs
		elements::PerformanceState performance;
		performance.note = 12.0*inputs[NOTE_INPUT].value + roundf(params[COARSE_PARAM].value) + params[FINE_PARAM].value + 69.0;
		performance.modulation = 3.3*quarticBipolar(params[FM_PARAM].value) * 49.5 * inputs[FM_INPUT].value/5.0;
		performance.gate = params[PLAY_PARAM].value >= 1.0 || inputs[GATE_INPUT].value >= 1.0;
		performance.strength = clampf(1.0 - inputs[STRENGTH_INPUT].value/5.0, 0.0, 1.0);

		// Generate audio
		part->Process(performance, blow, strike, main, aux, 16);

		// Convert output buffer
		{
			Frame<2> outputFrames[16];
			for (int i = 0; i < 16; i++) {
				outputFrames[i].samples[0] = main[i];
				outputFrames[i].samples[1] = aux[i];
			}

			outputSrc.setRates(32000, engineGetSampleRate());
			int inLen = 16;
			int outLen = outputBuffer.capacity();
			outputSrc.process(outputFrames, &inLen, outputBuffer.endData(), &outLen);
			outputBuffer.endIncr(outLen);
		}

		// Set lights
		lights[GATE_LIGHT].setBrightness(performance.gate ? 0.75 : 0.0);
		lights[EXCITER_LIGHT].setBrightness(part->exciter_level());
		lights[RESONATOR_LIGHT].setBrightness(part->resonator_level());
	}

	// Set output
	if (!outputBuffer.empty()) {
		Frame<2> outputFrame = outputBuffer.shift();
		outputs[AUX_OUTPUT].value = 5.0 * outputFrame.samples[0];
		outputs[MAIN_OUTPUT].value = 5.0 * outputFrame.samples[1];
	}
}


ElementsWidget::ElementsWidget() {
	Elements *module = new Elements();
	setModule(module);
	box.size = Vec(15*34, 380);

	{
		Panel *panel = new LightPanel();
		panel->backgroundImage = Image::load(assetPlugin(plugin, "res/Elements.png"));
		panel->box.size = box.size;
		addChild(panel);
	}

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(480, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(480, 365)));

	addParam(createParam<Rogan1PSWhite>(Vec(28, 42), module, Elements::CONTOUR_PARAM, 0.0, 1.0, 1.0));
	addParam(createParam<Rogan1PSWhite>(Vec(99, 42), module, Elements::BOW_PARAM, 0.0, 1.0, 0.0));
	addParam(createParam<Rogan1PSRed>(Vec(169, 42), module, Elements::BLOW_PARAM, 0.0, 1.0, 0.0));
	addParam(createParam<Rogan1PSGreen>(Vec(239, 42), module, Elements::STRIKE_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<Rogan1PSWhite>(Vec(310, 42), module, Elements::COARSE_PARAM, -30.0, 30.0, 0.0));
	addParam(createParam<Rogan1PSWhite>(Vec(381, 42), module, Elements::FINE_PARAM, -2.0, 2.0, 0.0));
	addParam(createParam<Rogan1PSWhite>(Vec(451, 42), module, Elements::FM_PARAM, -1.0, 1.0, 0.0));

	addParam(createParam<Rogan3PSRed>(Vec(115, 116), module, Elements::FLOW_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<Rogan3PSGreen>(Vec(212, 116), module, Elements::MALLET_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<Rogan3PSWhite>(Vec(326, 116), module, Elements::GEOMETRY_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<Rogan3PSWhite>(Vec(423, 116), module, Elements::BRIGHTNESS_PARAM, 0.0, 1.0, 0.5));

	addParam(createParam<Rogan1PSWhite>(Vec(99, 202), module, Elements::BOW_TIMBRE_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<Rogan1PSRed>(Vec(170, 202), module, Elements::BLOW_TIMBRE_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<Rogan1PSGreen>(Vec(239, 202), module, Elements::STRIKE_TIMBRE_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<Rogan1PSWhite>(Vec(310, 202), module, Elements::DAMPING_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<Rogan1PSWhite>(Vec(380, 202), module, Elements::POSITION_PARAM, 0.0, 1.0, 0.5));
	addParam(createParam<Rogan1PSWhite>(Vec(451, 202), module, Elements::SPACE_PARAM, 0.0, 2.0, 0.0));

	addParam(createParam<Trimpot>(Vec(104.5, 273), module, Elements::BOW_TIMBRE_MOD_PARAM, -1.0, 1.0, 0.0));
	addParam(createParam<Trimpot>(Vec(142.5, 273), module, Elements::FLOW_MOD_PARAM, -1.0, 1.0, 0.0));
	addParam(createParam<Trimpot>(Vec(181.5, 273), module, Elements::BLOW_TIMBRE_MOD_PARAM, -1.0, 1.0, 0.0));
	addParam(createParam<Trimpot>(Vec(219.5, 273), module, Elements::MALLET_MOD_PARAM, -1.0, 1.0, 0.0));
	addParam(createParam<Trimpot>(Vec(257.5, 273), module, Elements::STRIKE_TIMBRE_MOD_PARAM, -1.0, 1.0, 0.0));
	addParam(createParam<Trimpot>(Vec(315.5, 273), module, Elements::DAMPING_MOD_PARAM, -1.0, 1.0, 0.0));
	addParam(createParam<Trimpot>(Vec(354.5, 273), module, Elements::GEOMETRY_MOD_PARAM, -1.0, 1.0, 0.0));
	addParam(createParam<Trimpot>(Vec(392.5, 273), module, Elements::POSITION_MOD_PARAM, -1.0, 1.0, 0.0));
	addParam(createParam<Trimpot>(Vec(430.5, 273), module, Elements::BRIGHTNESS_MOD_PARAM, -1.0, 1.0, 0.0));
	addParam(createParam<Trimpot>(Vec(469.5, 273), module, Elements::SPACE_MOD_PARAM, -2.0, 2.0, 0.0));

	addInput(createInput<PJ301MPort>(Vec(20, 178), module, Elements::NOTE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(55, 178), module, Elements::FM_INPUT));

	addInput(createInput<PJ301MPort>(Vec(20, 224), module, Elements::GATE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(55, 224), module, Elements::STRENGTH_INPUT));

	addInput(createInput<PJ301MPort>(Vec(20, 270), module, Elements::BLOW_INPUT));
	addInput(createInput<PJ301MPort>(Vec(55, 270), module, Elements::STRIKE_INPUT));

	addOutput(createOutput<PJ301MPort>(Vec(20, 316), module, Elements::AUX_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(55, 316), module, Elements::MAIN_OUTPUT));

	addInput(createInput<PJ301MPort>(Vec(101, 316), module, Elements::BOW_TIMBRE_MOD_INPUT));
	addInput(createInput<PJ301MPort>(Vec(139, 316), module, Elements::FLOW_MOD_INPUT));
	addInput(createInput<PJ301MPort>(Vec(178, 316), module, Elements::BLOW_TIMBRE_MOD_INPUT));
	addInput(createInput<PJ301MPort>(Vec(216, 316), module, Elements::MALLET_MOD_INPUT));
	addInput(createInput<PJ301MPort>(Vec(254, 316), module, Elements::STRIKE_TIMBRE_MOD_INPUT));
	addInput(createInput<PJ301MPort>(Vec(312, 316), module, Elements::DAMPING_MOD_INPUT));
	addInput(createInput<PJ301MPort>(Vec(350, 316), module, Elements::GEOMETRY_MOD_INPUT));
	addInput(createInput<PJ301MPort>(Vec(389, 316), module, Elements::POSITION_MOD_INPUT));
	addInput(createInput<PJ301MPort>(Vec(427, 316), module, Elements::BRIGHTNESS_MOD_INPUT));
	addInput(createInput<PJ301MPort>(Vec(466, 316), module, Elements::SPACE_MOD_INPUT));

	addParam(createParam<CKD6>(Vec(36, 116), module, Elements::PLAY_PARAM, 0.0, 1.0, 0.0));

	struct GateLight : YellowLight {
		GateLight() {
			box.size = Vec(28-6, 28-6);
			bgColor = COLOR_BLACK_TRANSPARENT;
		}
	};

	addChild(createLight<GateLight>(Vec(36+3, 116+3), module, Elements::GATE_LIGHT));
	addChild(createLight<MediumLight<GreenLight>>(Vec(184, 165), module, Elements::EXCITER_LIGHT));
	addChild(createLight<MediumLight<RedLight>>(Vec(395, 165), module, Elements::RESONATOR_LIGHT));
}

struct ElementsModalItem : MenuItem {
	Elements *elements;
	int model;
	void onAction(EventAction &e) override {
		elements->setModel(model);
	}
	void step() override {
		rightText = (elements->getModel() == model) ? "✔" : "";
		MenuItem::step();
	}
};

Menu *ElementsWidget::createContextMenu() {
	Menu *menu = ModuleWidget::createContextMenu();

	Elements *elements = dynamic_cast<Elements*>(module);
	assert(elements);

	menu->addChild(construct<MenuEntry>());
	menu->addChild(construct<MenuLabel>(&MenuEntry::text, "Alternative models"));
	menu->addChild(construct<ElementsModalItem>(&MenuEntry::text, "Original", &ElementsModalItem::elements, elements, &ElementsModalItem::model, 0));
	menu->addChild(construct<ElementsModalItem>(&MenuEntry::text, "Non-linear string", &ElementsModalItem::elements, elements, &ElementsModalItem::model, 1));
	menu->addChild(construct<ElementsModalItem>(&MenuEntry::text, "Chords", &ElementsModalItem::elements, elements, &ElementsModalItem::model, 2));

	return menu;
}
