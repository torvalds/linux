/*
 * Greybus audio driver
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/module.h>

#include "audio.h"

static int gbcodec_event_spk(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *k, int event)
{
	/* Ensure GB speaker is connected */

	return 0;
}

static int gbcodec_event_hp(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *k, int event)
{
	/* Ensure GB module supports jack slot */

	return 0;
}

static int gbcodec_event_int_mic(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *k, int event)
{
	/* Ensure GB module supports jack slot */

	return 0;
}

static const struct snd_kcontrol_new gbcodec_snd_controls[] = {
	SOC_DOUBLE("Playback Mute", GBCODEC_MUTE_REG, 0, 1, 1, 1),
	SOC_DOUBLE("Capture Mute", GBCODEC_MUTE_REG, 4, 5, 1, 1),
	SOC_DOUBLE_R("Playback Volume", GBCODEC_PB_LVOL_REG,
		     GBCODEC_PB_RVOL_REG, 0, 127, 0),
	SOC_DOUBLE_R("Capture Volume", GBCODEC_CAP_LVOL_REG,
		     GBCODEC_CAP_RVOL_REG, 0, 127, 0),
};

static const struct snd_kcontrol_new spk_amp_ctl =
	SOC_DAPM_SINGLE("Switch", GBCODEC_CTL_REG, 0, 1, 0);

static const struct snd_kcontrol_new hp_amp_ctl =
	SOC_DAPM_SINGLE("Switch", GBCODEC_CTL_REG, 1, 1, 0);

static const struct snd_kcontrol_new mic_adc_ctl =
	SOC_DAPM_SINGLE("Switch", GBCODEC_CTL_REG, 4, 1, 0);

/* APB1-GBSPK source */
static const char * const gbcodec_apb1_src[] = {"Stereo", "Left", "Right"};

static const SOC_ENUM_SINGLE_DECL(
	gbcodec_apb1_rx_enum, GBCODEC_APB1_MUX_REG, 0, gbcodec_apb1_src);

static const struct snd_kcontrol_new gbcodec_apb1_rx_mux =
	SOC_DAPM_ENUM("APB1 source", gbcodec_apb1_rx_enum);

static const SOC_ENUM_SINGLE_DECL(
	gbcodec_mic_enum, GBCODEC_APB1_MUX_REG, 4, gbcodec_apb1_src);

static const struct snd_kcontrol_new gbcodec_mic_mux =
	SOC_DAPM_ENUM("MIC source", gbcodec_mic_enum);

static const struct snd_soc_dapm_widget gbcodec_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Spk", gbcodec_event_spk),
	SND_SOC_DAPM_SPK("HP", gbcodec_event_hp),
	SND_SOC_DAPM_MIC("Int Mic", gbcodec_event_int_mic),

	SND_SOC_DAPM_OUTPUT("SPKOUT"),
	SND_SOC_DAPM_OUTPUT("HPOUT"),

	SND_SOC_DAPM_INPUT("MIC"),
	SND_SOC_DAPM_INPUT("HSMIC"),

	SND_SOC_DAPM_SWITCH("SPK Amp", SND_SOC_NOPM, 0, 0, &spk_amp_ctl),
	SND_SOC_DAPM_SWITCH("HP Amp", SND_SOC_NOPM, 0, 0, &hp_amp_ctl),
	SND_SOC_DAPM_SWITCH("MIC ADC", SND_SOC_NOPM, 0, 0, &mic_adc_ctl),

	SND_SOC_DAPM_PGA("SPK DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HP DAC", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("SPK Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("HP Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("APB1_TX Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("APB1_RX Mux", SND_SOC_NOPM, 0, 0,
			 &gbcodec_apb1_rx_mux),
	SND_SOC_DAPM_MUX("MIC Mux", SND_SOC_NOPM, 0, 0, &gbcodec_mic_mux),

	SND_SOC_DAPM_AIF_IN("APB1RX", "APBridgeA1 Playback", 0, SND_SOC_NOPM, 0,
			    0),
	SND_SOC_DAPM_AIF_OUT("APB1TX", "APBridgeA1 Capture", 0, SND_SOC_NOPM, 0,
			     0),
};

static const struct snd_soc_dapm_route gbcodec_dapm_routes[] = {
	/* Playback path */
	{"Spk", NULL, "SPKOUT"},
	{"SPKOUT", NULL, "SPK Amp"},
	{"SPK Amp", "Switch", "SPK DAC"},
	{"SPK DAC", NULL, "SPK Mixer"},

	{"HP", NULL, "HPOUT"},
	{"HPOUT", NULL, "HP Amp"},
	{"HP Amp", "Switch", "HP DAC"},
	{"HP DAC", NULL, "HP Mixer"},

	{"SPK Mixer", NULL, "APB1_RX Mux"},
	{"HP Mixer", NULL, "APB1_RX Mux"},

	{"APB1_RX Mux", "Left", "APB1RX"},
	{"APB1_RX Mux", "Right", "APB1RX"},
	{"APB1_RX Mux", "Stereo", "APB1RX"},

	/* Capture path */
	{"MIC", NULL, "Int Mic"},
	{"MIC", NULL, "MIC Mux"},
	{"MIC Mux", "Left", "MIC ADC"},
	{"MIC Mux", "Right", "MIC ADC"},
	{"MIC Mux", "Stereo", "MIC ADC"},
	{"MIC ADC", "Switch", "APB1_TX Mixer"},
	{"APB1_TX Mixer", NULL, "APB1TX"}
};

static int gbcodec_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	return 0;
}

static void gbcodec_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
}

static int gbcodec_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hwparams,
			     struct snd_soc_dai *dai)
{
	return 0;
}

static int gbcodec_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	return 0;
}

static int gbcodec_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	return 0;
}

static int gbcodec_digital_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static struct snd_soc_dai_ops gbcodec_dai_ops = {
	.startup = gbcodec_startup,
	.shutdown = gbcodec_shutdown,
	.hw_params = gbcodec_hw_params,
	.prepare = gbcodec_prepare,
	.set_fmt = gbcodec_set_dai_fmt,
	.digital_mute = gbcodec_digital_mute,
};

static struct snd_soc_dai_driver gbcodec_dai = {
		.playback = {
			.stream_name = "APBridgeA1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_44100,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "APBridgeA1 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_44100,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &gbcodec_dai_ops,
};

static int gbcodec_probe(struct snd_soc_codec *codec)
{
	struct gbaudio_codec_info *gbcodec = snd_soc_codec_get_drvdata(codec);

	gbcodec->codec = codec;

	return 0;
}

static int gbcodec_remove(struct snd_soc_codec *codec)
{
	/* Empty function for now */
	return 0;
}

static int gbcodec_write(struct snd_soc_codec *codec, unsigned int reg,
			 unsigned int value)
{
	int ret = 0;
	struct gbaudio_codec_info *gbcodec = snd_soc_codec_get_drvdata(codec);
	u8 *gbcodec_reg = gbcodec->reg;

	if (reg == SND_SOC_NOPM)
		return 0;

	if (reg >= GBCODEC_REG_COUNT)
		return 0;

	gbcodec_reg[reg] = value;
	dev_dbg(codec->dev, "reg[%d] = 0x%x\n", reg, value);

	return ret;
}

static unsigned int gbcodec_read(struct snd_soc_codec *codec,
				 unsigned int reg)
{
	unsigned int val = 0;

	struct gbaudio_codec_info *gbcodec = snd_soc_codec_get_drvdata(codec);
	u8 *gbcodec_reg = gbcodec->reg;

	if (reg == SND_SOC_NOPM)
		return 0;

	if (reg >= GBCODEC_REG_COUNT)
		return 0;

	val = gbcodec_reg[reg];
	dev_dbg(codec->dev, "reg[%d] = 0x%x\n", reg, val);

	return val;
}

static struct snd_soc_codec_driver soc_codec_dev_gbcodec = {
	.probe = gbcodec_probe,
	.remove = gbcodec_remove,

	.read = gbcodec_read,
	.write = gbcodec_write,

	.reg_cache_size = GBCODEC_REG_COUNT,
	.reg_cache_default = gbcodec_reg_defaults,
	.reg_word_size = 1,

	.idle_bias_off = true,

	.controls = gbcodec_snd_controls,
	.num_controls = ARRAY_SIZE(gbcodec_snd_controls),
	.dapm_widgets = gbcodec_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(gbcodec_dapm_widgets),
	.dapm_routes = gbcodec_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(gbcodec_dapm_routes),
};

static int gbaudio_codec_probe(struct platform_device *pdev)
{
	int ret;
	struct gbaudio_codec_info *gbcodec;
	char dai_name[NAME_SIZE];

	gbcodec = devm_kzalloc(&pdev->dev, sizeof(struct gbaudio_codec_info),
			       GFP_KERNEL);
	if (!gbcodec)
		return -ENOMEM;
	platform_set_drvdata(pdev, gbcodec);

	snprintf(dai_name, NAME_SIZE, "%s.%d", "gbcodec_pcm", pdev->id);
	gbcodec_dai.name = dai_name;

	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_gbcodec,
				     &gbcodec_dai, 1);
	if (!ret)
		gbcodec->registered = 1;

	return ret;
}

static const struct of_device_id gbcodec_of_match[] = {
	{ .compatible = "greybus,codec", },
	{},
};

static int gbaudio_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static struct platform_driver gbaudio_codec_driver = {
	.driver = {
		.name = "gbaudio-codec",
		.owner = THIS_MODULE,
		.of_match_table = gbcodec_of_match,
	},
	.probe = gbaudio_codec_probe,
	.remove   = gbaudio_codec_remove,
};
module_platform_driver(gbaudio_codec_driver);

MODULE_DESCRIPTION("Greybus Audio virtual codec driver");
MODULE_AUTHOR("Vaibhav Agarwal <vaibhav.agarwal@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gbaudio-codec");
