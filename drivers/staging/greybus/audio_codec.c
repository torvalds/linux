/*
 * Greybus audio driver
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/msm-dynamic-dailink.h>

#include "audio_codec.h"

#define GB_AUDIO_MGMT_DRIVER_NAME	"gb_audio_mgmt"
#define GB_AUDIO_DATA_DRIVER_NAME	"gb_audio_data"

static DEFINE_MUTEX(gb_codec_list_lock);
static LIST_HEAD(gb_codec_list);

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
	/* Empty function for now */
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

/*
 * GB codec DAI link related
 */
static struct snd_soc_dai_link gbaudio_dailink = {
	.name = "PRI_MI2S_RX",
	.stream_name = "Primary MI2S Playback",
	.platform_name = "msm-pcm-routing",
	.cpu_dai_name = "msm-dai-q6-mi2s.0",
	.no_pcm = 1,
	.be_id = 34,
};

static void gbaudio_remove_dailinks(struct gbaudio_codec_info *gbcodec)
{
	int i;

	for (i = 0; i < gbcodec->num_dai_links; i++) {
		dev_dbg(gbcodec->dev, "Remove %s: DAI link\n",
			gbcodec->dailink_name[i]);
		devm_kfree(gbcodec->dev, gbcodec->dailink_name[i]);
		gbcodec->dailink_name[i] = NULL;
	}
	gbcodec->num_dai_links = 0;
}

static int gbaudio_add_dailinks(struct gbaudio_codec_info *gbcodec)
{
	int ret, i;
	char *dai_link_name;
	struct snd_soc_dai_link *dai;
	struct device *dev = gbcodec->dev;

	dai = &gbaudio_dailink;
	dai->codec_name = gbcodec->name;

	/* FIXME
	 * allocate memory for DAI links based on count.
	 * currently num_dai_links=1, so using static struct
	 */
	gbcodec->num_dai_links = 1;

	for (i = 0; i < gbcodec->num_dai_links; i++) {
		gbcodec->dailink_name[i] = dai_link_name =
			devm_kzalloc(dev, NAME_SIZE, GFP_KERNEL);
		snprintf(dai_link_name, NAME_SIZE, "GB %d.%d PRI_MI2S_RX",
			 gbcodec->dev_id, i);
		dai->name = dai_link_name;
		dai->codec_dai_name = gbcodec->dais[i].name;
	}

	ret = msm8994_add_dailink("msm8994-tomtom-mtp-snd-card", dai, 1);
	if (ret) {
		dev_err(dev, "%d:Error while adding DAI link\n", ret);
		goto err_dai_link;
	}

	return ret;

err_dai_link:
	gbcodec->num_dai_links = i;
	gbaudio_remove_dailinks(gbcodec);
	return ret;
}

/*
 * gb_snd management functions
 */
static struct gbaudio_codec_info *gbaudio_find_codec(struct device *dev,
						     int dev_id)
{
	struct gbaudio_codec_info *tmp, *ret;

	mutex_lock(&gb_codec_list_lock);
	list_for_each_entry_safe(ret, tmp, &gb_codec_list, list) {
		dev_dbg(dev, "%d:device found\n", ret->dev_id);
		if (ret->dev_id == dev_id) {
			mutex_unlock(&gb_codec_list_lock);
			return ret;
		}
	}
	mutex_unlock(&gb_codec_list_lock);
	return NULL;
}

static struct gbaudio_codec_info *gbaudio_get_codec(struct device *dev,
						    int dev_id)
{
	struct gbaudio_codec_info *gbcodec;

	gbcodec = gbaudio_find_codec(dev, dev_id);
	if (gbcodec)
		return gbcodec;

	gbcodec = devm_kzalloc(dev, sizeof(*gbcodec), GFP_KERNEL);
	if (!gbcodec)
		return NULL;

	mutex_init(&gbcodec->lock);
	INIT_LIST_HEAD(&gbcodec->dai_list);
	gbcodec->dev_id = dev_id;
	dev_set_drvdata(dev, gbcodec);
	gbcodec->dev = dev;
	strlcpy(gbcodec->name, dev_name(dev), NAME_SIZE);

	mutex_lock(&gb_codec_list_lock);
	list_add(&gbcodec->list, &gb_codec_list);
	mutex_unlock(&gb_codec_list_lock);
	dev_dbg(dev, "%d:%s Added to codec list\n", gbcodec->dev_id,
		gbcodec->name);

	return gbcodec;
}

static void gbaudio_free_codec(struct device *dev,
			       struct gbaudio_codec_info *gbcodec)
{
	mutex_lock(&gb_codec_list_lock);
	if (!gbcodec->mgmt_connection &&
			list_empty(&gbcodec->dai_list)) {
		list_del(&gbcodec->list);
		mutex_unlock(&gb_codec_list_lock);
		dev_set_drvdata(dev, NULL);
		devm_kfree(dev, gbcodec);
	} else {
		mutex_unlock(&gb_codec_list_lock);
	}
}

/*
 * This is the basic hook get things initialized and registered w/ gb
 */

/*
 * GB codec module driver ops
 */
struct device_driver gb_codec_driver = {
	.name = "1-8",
	.owner = THIS_MODULE,
};

static int gbaudio_codec_probe(struct gb_connection *connection)
{
	int ret;
	struct gbaudio_codec_info *gbcodec;
	struct device *dev = &connection->bundle->dev;
	int dev_id = connection->bundle->id;

	dev_dbg(dev, "Add device:%d:%s\n", dev_id, dev_name(dev));
	/* get gbcodec data */
	gbcodec = gbaudio_get_codec(dev, dev_id);
	if (!gbcodec)
		return -ENOMEM;

	gbcodec->mgmt_connection = connection;

	/* update DAI info */
	gbcodec->dais = &gbcodec_dai;
	/* FIXME */
	dev->driver = &gb_codec_driver;

	/* register codec */
	ret = snd_soc_register_codec(dev, &soc_codec_dev_gbcodec,
				     gbcodec->dais, 1);
	if (ret) {
		dev_err(dev, "%d:Failed to register codec\n", ret);
		goto base_error;
	}

	/* update DAI links in response to this codec */
	ret = gbaudio_add_dailinks(gbcodec);
	if (ret) {
		dev_err(dev, "%d: Failed to add DAI links\n", ret);
		goto codec_reg_error;
	}

	/* set registered flag */
	mutex_lock(&gbcodec->lock);
	gbcodec->codec_registered = 1;

	mutex_unlock(&gbcodec->lock);

	return ret;

codec_reg_error:
	snd_soc_unregister_codec(dev);
base_error:
	dev->driver = NULL;
	gbcodec->mgmt_connection = NULL;
	return ret;
}

static void gbaudio_codec_remove(struct gb_connection *connection)
{
	struct gbaudio_codec_info *gbcodec;
	struct device *dev = &connection->bundle->dev;
	int dev_id = connection->bundle->id;

	dev_dbg(dev, "Remove device:%d:%s\n", dev_id, dev_name(dev));

	/* get gbcodec data */
	gbcodec = gbaudio_find_codec(dev, dev_id);
	if (!gbcodec)
		return;

	msm8994_remove_dailink("msm8994-tomtom-mtp-snd-card", &gbaudio_dailink,
			       1);
	gbaudio_remove_dailinks(gbcodec);

	snd_soc_unregister_codec(dev);
	dev->driver = NULL;
	gbcodec->mgmt_connection = NULL;
	mutex_lock(&gbcodec->lock);
	gbcodec->codec_registered = 0;
	mutex_unlock(&gbcodec->lock);
	gbaudio_free_codec(dev, gbcodec);
}

static int gbaudio_codec_report_event_recv(u8 type, struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_audio_streaming_event_request *req = op->request->payload;

	dev_warn(&connection->bundle->dev,
		 "Audio Event received: cport: %u, event: %u\n",
		 req->data_cport, req->event);

	return 0;
}

static struct gb_protocol gb_audio_mgmt_protocol = {
	.name			= GB_AUDIO_MGMT_DRIVER_NAME,
	.id			= GREYBUS_PROTOCOL_AUDIO_MGMT,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gbaudio_codec_probe,
	.connection_exit	= gbaudio_codec_remove,
	.request_recv		= gbaudio_codec_report_event_recv,
};

static struct gbaudio_dai *gbaudio_allocate_dai(struct gbaudio_codec_info *gb,
					 int data_cport,
					 struct gb_connection *connection,
					 const char *name)
{
	struct gbaudio_dai *dai;

	mutex_lock(&gb->lock);
	dai = devm_kzalloc(gb->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai) {
		dev_err(gb->dev, "%s:DAI Malloc failure\n", name);
		mutex_unlock(&gb->lock);
		return NULL;
	}

	dai->data_cport = data_cport;
	dai->connection = connection;

	/* update name */
	if (name)
		strlcpy(dai->name, name, NAME_SIZE);
	list_add(&dai->list, &gb->dai_list);
	dev_dbg(gb->dev, "%d:%s: DAI added\n", data_cport, dai->name);
	mutex_unlock(&gb->lock);

	return dai;
}

struct gbaudio_dai *gbaudio_add_dai(struct gbaudio_codec_info *gbcodec,
				    int data_cport,
				    struct gb_connection *connection,
				    const char *name)
{
	struct gbaudio_dai *dai, *_dai;

	/* FIXME need to take care for multiple DAIs */
	mutex_lock(&gbcodec->lock);
	if (list_empty(&gbcodec->dai_list)) {
		mutex_unlock(&gbcodec->lock);
		return gbaudio_allocate_dai(gbcodec, data_cport, connection,
					    name);
	}

	list_for_each_entry_safe(dai, _dai, &gbcodec->dai_list, list) {
		if (dai->data_cport == data_cport) {
			if (connection)
				dai->connection = connection;

			if (name)
				strlcpy(dai->name, name, NAME_SIZE);
			dev_dbg(gbcodec->dev, "%d:%s: DAI updated\n",
				data_cport, dai->name);
			mutex_unlock(&gbcodec->lock);
			return dai;
		}
	}

	dev_err(gbcodec->dev, "%s:DAI not found\n", name);
	mutex_unlock(&gbcodec->lock);
	return NULL;
}

static int gbaudio_dai_probe(struct gb_connection *connection)
{
	struct gbaudio_dai *dai;
	struct device *dev = &connection->bundle->dev;
	int dev_id = connection->bundle->id;
	struct gbaudio_codec_info *gbcodec = dev_get_drvdata(dev);

	dev_dbg(dev, "Add DAI device:%d:%s\n", dev_id, dev_name(dev));

	/* get gbcodec data */
	gbcodec = gbaudio_get_codec(dev, dev_id);
	if (!gbcodec)
		return -ENOMEM;

	/* add/update dai_list*/
	dai = gbaudio_add_dai(gbcodec, connection->intf_cport_id, connection,
			       NULL);
	if (!dai)
		return -ENOMEM;

	/* update dai_added count */
	mutex_lock(&gbcodec->lock);
	gbcodec->dai_added++;
	mutex_unlock(&gbcodec->lock);

	return 0;
}

static void gbaudio_dai_remove(struct gb_connection *connection)
{
	struct device *dev = &connection->bundle->dev;
	int dev_id = connection->bundle->id;
	struct gbaudio_codec_info *gbcodec;

	dev_dbg(dev, "Remove DAI device:%d:%s\n", dev_id, dev_name(dev));

	/* get gbcodec data */
	gbcodec = gbaudio_find_codec(dev, dev_id);
	if (!gbcodec)
		return;

	/* inform uevent to above layers */
	mutex_lock(&gbcodec->lock);
	/* update dai_added count */
	gbcodec->dai_added--;
	mutex_unlock(&gbcodec->lock);

	gbaudio_free_codec(dev, gbcodec);
}

static int gbaudio_dai_report_event_recv(u8 type, struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;

	dev_warn(&connection->bundle->dev, "Audio Event received\n");

	return 0;
}

static struct gb_protocol gb_audio_data_protocol = {
	.name			= GB_AUDIO_DATA_DRIVER_NAME,
	.id			= GREYBUS_PROTOCOL_AUDIO_DATA,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gbaudio_dai_probe,
	.connection_exit	= gbaudio_dai_remove,
	.request_recv		= gbaudio_dai_report_event_recv,
};

/*
 * This is the basic hook get things initialized and registered w/ gb
 */

static int __init gb_audio_protocol_init(void)
{
	int err;

	err = gb_protocol_register(&gb_audio_mgmt_protocol);
	if (err) {
		pr_err("Can't register i2s mgmt protocol driver: %d\n", -err);
		return err;
	}

	err = gb_protocol_register(&gb_audio_data_protocol);
	if (err) {
		pr_err("Can't register Audio protocol driver: %d\n", -err);
		goto err_unregister_audio_mgmt;
	}

	return 0;

err_unregister_audio_mgmt:
	gb_protocol_deregister(&gb_audio_mgmt_protocol);
	return err;
}
module_init(gb_audio_protocol_init);

static void __exit gb_audio_protocol_exit(void)
{
	gb_protocol_deregister(&gb_audio_data_protocol);
	gb_protocol_deregister(&gb_audio_mgmt_protocol);
}
module_exit(gb_audio_protocol_exit);

MODULE_DESCRIPTION("Greybus Audio codec driver");
MODULE_AUTHOR("Vaibhav Agarwal <vaibhav.agarwal@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gbaudio-codec");
