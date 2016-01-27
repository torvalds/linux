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
#include "audio_apbridgea.h"
#include "audio_manager.h"

#define GB_AUDIO_MGMT_DRIVER_NAME	"gb_audio_mgmt"
#define GB_AUDIO_DATA_DRIVER_NAME	"gb_audio_data"

static DEFINE_MUTEX(gb_codec_list_lock);
static LIST_HEAD(gb_codec_list);

/*
 * codec DAI ops
 */
static int gbcodec_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	int ret, found;
	__u16 i2s_port, cportid;

	struct gbaudio_dai *gb_dai;
	struct gbaudio_codec_info *gb =
		(struct gbaudio_codec_info *)dev_get_drvdata(dai->dev);

	/* find the dai */
	found = 0;
	list_for_each_entry(gb_dai, &gb->dai_list, list) {
		if (!strncmp(gb_dai->name, dai->name, NAME_SIZE)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		dev_err(dai->dev, "%s: DAI not registered\n", dai->name);
		return -EINVAL;
	}

	/* register cport */
	i2s_port = 0;	/* fixed for now */
	cportid = gb_dai->connection->hd_cport_id;
	ret = gb_audio_apbridgea_register_cport(gb_dai->connection, i2s_port,
						cportid);
	dev_dbg(dai->dev, "Register %s:%d DAI, ret:%d\n", dai->name, cportid,
		ret);

	if (!ret)
		atomic_inc(&gb->users);

	return ret;
}

static void gbcodec_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	int ret, found;
	__u16 i2s_port, cportid;

	struct gbaudio_dai *gb_dai;
	struct gbaudio_codec_info *gb =
		(struct gbaudio_codec_info *)dev_get_drvdata(dai->dev);

	/* find the dai */
	found = 0;
	list_for_each_entry(gb_dai, &gb->dai_list, list) {
		if (!strncmp(gb_dai->name, dai->name, NAME_SIZE)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		dev_err(dai->dev, "%s: DAI not registered\n", dai->name);
		return;
	}

	atomic_dec(&gb->users);

	/* deactivate rx/tx */
	cportid = gb_dai->connection->intf_cport_id;

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_CAPTURE:
		ret = gb_audio_gb_deactivate_rx(gb->mgmt_connection, cportid);
		break;
	case SNDRV_PCM_STREAM_PLAYBACK:
		ret = gb_audio_gb_deactivate_tx(gb->mgmt_connection, cportid);
		break;
	default:
		dev_err(dai->dev, "Invalid stream type during shutdown\n");
		return;
	}

	if (ret)
		dev_err(dai->dev, "%d:Error during deactivate\n", ret);

	/* un register cport */
	i2s_port = 0;	/* fixed for now */
	ret = gb_audio_apbridgea_unregister_cport(gb_dai->connection, i2s_port,
					gb_dai->connection->hd_cport_id);

	dev_dbg(dai->dev, "Unregister %s:%d DAI, ret:%d\n", dai->name,
		gb_dai->connection->hd_cport_id, ret);

	return;
}

static int gbcodec_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hwparams,
			     struct snd_soc_dai *dai)
{
	int ret, found;
	uint8_t sig_bits, channels;
	uint32_t format, rate;
	uint16_t data_cport;
	struct gbaudio_dai *gb_dai;
	struct gbaudio_codec_info *gb =
		(struct gbaudio_codec_info *)dev_get_drvdata(dai->dev);

	/* find the dai */
	found = 0;
	list_for_each_entry(gb_dai, &gb->dai_list, list) {
		if (!strncmp(gb_dai->name, dai->name, NAME_SIZE)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		dev_err(dai->dev, "%s: DAI not registered\n", dai->name);
		return -EINVAL;
	}

	/*
	 * assuming, currently only 48000 Hz, 16BIT_LE, stereo
	 * is supported, validate params before configuring codec
	 */
	if (params_channels(hwparams) != 2) {
		dev_err(dai->dev, "Invalid channel count:%d\n",
			params_channels(hwparams));
		return -EINVAL;
	}
	channels = params_channels(hwparams);

	if (params_rate(hwparams) != 48000) {
		dev_err(dai->dev, "Invalid sampling rate:%d\n",
			params_rate(hwparams));
		return -EINVAL;
	}
	rate = GB_AUDIO_PCM_RATE_48000;

	if (params_format(hwparams) != SNDRV_PCM_FORMAT_S16_LE) {
		dev_err(dai->dev, "Invalid format:%d\n",
			params_format(hwparams));
		return -EINVAL;
	}
	format = GB_AUDIO_PCM_FMT_S16_LE;

	data_cport = gb_dai->connection->intf_cport_id;
	/* XXX check impact of sig_bit
	 * it should not change ideally
	 */

	dev_dbg(dai->dev, "cport:%d, rate:%d, channel %d, format %d, sig_bits:%d\n",
		data_cport, rate, channels, format, sig_bits);
	ret = gb_audio_gb_set_pcm(gb->mgmt_connection, data_cport, format,
				  rate, channels, sig_bits);
	if (ret) {
		dev_err(dai->dev, "%d: Error during set_pcm\n", ret);
		return ret;
	}

	/*
	 * XXX need to check if
	 * set config is always required
	 * check for mclk_freq as well
	 */
	ret = gb_audio_apbridgea_set_config(gb_dai->connection, 0,
					    AUDIO_APBRIDGEA_PCM_FMT_16,
					    AUDIO_APBRIDGEA_PCM_RATE_48000,
					    6144000);
	if (ret)
		dev_err(dai->dev, "%d: Error during set_config\n", ret);

	return ret;
}

static int gbcodec_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	int ret, found;
	uint16_t data_cport;
	struct gbaudio_dai *gb_dai;
	struct gbaudio_codec_info *gb =
		(struct gbaudio_codec_info *)dev_get_drvdata(dai->dev);

	/* find the dai */
	found = 0;
	list_for_each_entry(gb_dai, &gb->dai_list, list) {
		if (!strncmp(gb_dai->name, dai->name, NAME_SIZE)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		dev_err(dai->dev, "%s: DAI not registered\n", dai->name);
		return -EINVAL;
	}

	/* deactivate rx/tx */
	data_cport = gb_dai->connection->intf_cport_id;

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_CAPTURE:
		ret = gb_audio_gb_set_rx_data_size(gb->mgmt_connection,
						   data_cport, 192);
		if (ret) {
			dev_err(dai->dev,
				"%d:Error during set_rx_data_size, cport:%d\n",
				ret, data_cport);
			return ret;
		}
		ret = gb_audio_apbridgea_set_rx_data_size(gb_dai->connection, 0,
							  192);
		if (ret) {
			dev_err(dai->dev,
				"%d:Error during apbridgea_set_rx_data_size\n",
				ret);
			return ret;
		}
		ret = gb_audio_gb_activate_rx(gb->mgmt_connection, data_cport);
		break;
	case SNDRV_PCM_STREAM_PLAYBACK:
		ret = gb_audio_gb_set_tx_data_size(gb->mgmt_connection,
						   data_cport, 192);
		if (ret) {
			dev_err(dai->dev,
				"%d:Error during module set_tx_data_size, cport:%d\n",
				ret, data_cport);
			return ret;
		}
		ret = gb_audio_apbridgea_set_tx_data_size(gb_dai->connection, 0,
							  192);
		if (ret) {
			dev_err(dai->dev,
				"%d:Error during apbridgea set_tx_data_size, cport\n",
				ret);
			return ret;
		}
		ret = gb_audio_gb_activate_tx(gb->mgmt_connection, data_cport);
		break;
	default:
		dev_err(dai->dev, "Invalid stream type %d during prepare\n",
			substream->stream);
		return -EINVAL;
	}

	if (ret)
		dev_err(dai->dev, "%d: Error during activate stream\n", ret);

	return ret;
}

static int gbcodec_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *dai)
{
	int ret, found;
	int tx, rx, start, stop;
	struct gbaudio_dai *gb_dai;
	struct gbaudio_codec_info *gb =
		(struct gbaudio_codec_info *)dev_get_drvdata(dai->dev);

	/* find the dai */
	found = 0;
	list_for_each_entry(gb_dai, &gb->dai_list, list) {
		if (!strncmp(gb_dai->name, dai->name, NAME_SIZE)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		dev_err(dai->dev, "%s: DAI not registered\n", dai->name);
		return -EINVAL;
	}

	tx = rx = start = stop = 0;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		start = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		stop = 1;
		break;
	default:
		dev_err(dai->dev, "Invalid tigger cmd:%d\n", cmd);
		return -EINVAL;
	}

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_CAPTURE:
		rx = 1;
		break;
	case SNDRV_PCM_STREAM_PLAYBACK:
		tx = 1;
		break;
	default:
		dev_err(dai->dev, "Invalid stream type:%d\n",
			substream->stream);
		return -EINVAL;
	}

	if (start && tx)
		ret = gb_audio_apbridgea_start_tx(gb_dai->connection, 0, 0);

	else if (start && rx)
		ret = gb_audio_apbridgea_start_rx(gb_dai->connection, 0);

	else if (stop && tx)
		ret = gb_audio_apbridgea_stop_tx(gb_dai->connection, 0);

	else if (stop && rx)
		ret = gb_audio_apbridgea_stop_rx(gb_dai->connection, 0);

	else
		ret = -EINVAL;

	if (ret)
		dev_err(dai->dev, "%d:Error during %s stream\n", ret,
			start ? "Start" : "Stop");

	return ret;
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
	.trigger = gbcodec_trigger,
	.prepare = gbcodec_prepare,
	.set_fmt = gbcodec_set_dai_fmt,
	.digital_mute = gbcodec_digital_mute,
};

/*
 * codec driver ops
 */
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
	.ignore_pmdown_time = 1,
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
	INIT_LIST_HEAD(&gbcodec->widget_list);
	INIT_LIST_HEAD(&gbcodec->codec_ctl_list);
	INIT_LIST_HEAD(&gbcodec->widget_ctl_list);
	gbcodec->dev_id = dev_id;
	dev_set_drvdata(dev, gbcodec);
	gbcodec->dev = dev;
	snprintf(gbcodec->name, NAME_SIZE, "%s.%s", dev->driver->name,
		 dev_name(dev));

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

/* XXX
 * since BE DAI path is not yet properly closed from above layer,
 * dsp dai.mi2s_dai_data.status_mask is still set to STATUS_PORT_STARTED
 * this causes immediate playback/capture to fail in case relevant mixer
 * control is not turned OFF
 * user need to try once again after failure to recover DSP state.
 */
static void gb_audio_cleanup(struct gbaudio_codec_info *gb)
{
	int cportid, ret;
	struct gbaudio_dai *gb_dai;
	struct gb_connection *connection;
	struct device *dev = gb->dev;

	list_for_each_entry(gb_dai, &gb->dai_list, list) {
		/*
		 * In case of BE dailink, need to deactivate APBridge
		 * manually
		 */
		if (gbaudio_dailink.no_pcm && atomic_read(&gb->users)) {
			connection = gb_dai->connection;
			/* PB active */
			ret = gb_audio_apbridgea_stop_tx(connection, 0);
			if (ret)
				dev_info(dev, "%d:Failed during APBridge stop_tx\n",
					 ret);
			cportid = connection->intf_cport_id;
			ret = gb_audio_gb_deactivate_tx(gb->mgmt_connection,
							cportid);
			if (ret)
				dev_info(dev,
					 "%d:Failed during deactivate_tx\n",
					 ret);
			cportid = connection->hd_cport_id;
			ret = gb_audio_apbridgea_unregister_cport(connection, 0,
								  cportid);
			if (ret)
				dev_info(dev, "%d:Failed during unregister cport\n",
					 ret);
			atomic_dec(&gb->users);
		}
	}
}

static int gbaudio_codec_probe(struct gb_connection *connection)
{
	int ret, i;
	struct gbaudio_codec_info *gbcodec;
	struct gb_audio_topology *topology;
	struct gb_audio_manager_module_descriptor desc;
	struct device *dev = &connection->bundle->dev;
	int dev_id = connection->intf->interface_id;

	dev_dbg(dev, "Add device:%d:%s\n", dev_id, dev_name(dev));
	/* get gbcodec data */
	gbcodec = gbaudio_get_codec(dev, dev_id);
	if (!gbcodec)
		return -ENOMEM;

	gbcodec->mgmt_connection = connection;

	/* fetch topology data */
	ret = gb_audio_gb_get_topology(connection, &topology);
	if (ret) {
		dev_err(gbcodec->dev,
			"%d:Error while fetching topology\n", ret);
		goto base_error;
	}

	/* process topology data */
	ret = gbaudio_tplg_parse_data(gbcodec, topology);
	if (ret) {
		dev_err(dev, "%d:Error while parsing topology data\n",
			  ret);
		goto topology_error;
	}
	gbcodec->topology = topology;

	/* update codec info */
	soc_codec_dev_gbcodec.controls = gbcodec->kctls;
	soc_codec_dev_gbcodec.num_controls = gbcodec->num_kcontrols;
	soc_codec_dev_gbcodec.dapm_widgets = gbcodec->widgets;
	soc_codec_dev_gbcodec.num_dapm_widgets = gbcodec->num_dapm_widgets;
	soc_codec_dev_gbcodec.dapm_routes = gbcodec->routes;
	soc_codec_dev_gbcodec.num_dapm_routes = gbcodec->num_dapm_routes;

	/* update DAI info */
	for (i = 0; i < gbcodec->num_dais; i++)
		gbcodec->dais[i].ops = &gbcodec_dai_ops;

	/* register codec */
	ret = snd_soc_register_codec(dev, &soc_codec_dev_gbcodec,
				     gbcodec->dais, 1);
	if (ret) {
		dev_err(dev, "%d:Failed to register codec\n", ret);
		goto parse_error;
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

	/* codec cleanup related */
	atomic_set(&gbcodec->users, 0);

	/* inform above layer for uevent */
	if (!gbcodec->set_uevent &&
	    (gbcodec->dai_added == gbcodec->num_dais)) {
		dev_dbg(dev, "Inform set_event:%d to above layer\n", 1);
		/* prepare for the audio manager */
		strlcpy(desc.name, gbcodec->name,
			GB_AUDIO_MANAGER_MODULE_NAME_LEN); /* todo */
		desc.slot = 1; /* todo */
		desc.vid = 2; /* todo */
		desc.pid = 3; /* todo */
		desc.cport = gbcodec->dev_id;
		desc.devices = 0x2; /* todo */
		gbcodec->manager_id = gb_audio_manager_add(&desc);
		gbcodec->set_uevent = 1;
	}
	mutex_unlock(&gbcodec->lock);

	return ret;

codec_reg_error:
	snd_soc_unregister_codec(dev);
	dev->driver = NULL;
parse_error:
	gbaudio_tplg_release(gbcodec);
	gbcodec->topology = NULL;
topology_error:
	kfree(topology);
base_error:
	gbcodec->mgmt_connection = NULL;
	gbaudio_free_codec(dev, gbcodec);
	return ret;
}

static void gbaudio_codec_remove(struct gb_connection *connection)
{
	struct gbaudio_codec_info *gbcodec;
	struct device *dev = &connection->bundle->dev;
	int dev_id = connection->intf->interface_id;

	dev_dbg(dev, "Remove device:%d:%s\n", dev_id, dev_name(dev));

	/* get gbcodec data */
	gbcodec = gbaudio_find_codec(dev, dev_id);
	if (!gbcodec)
		return;

	/* inform uevent to above layers */
	mutex_lock(&gbcodec->lock);
	if (gbcodec->set_uevent) {
		/* notify the audio manager */
		dev_dbg(dev, "Inform set_event:%d to above layer\n", 0);
		gb_audio_manager_remove(gbcodec->manager_id);
		gbcodec->set_uevent = 0;
	}
	mutex_unlock(&gbcodec->lock);

	if (atomic_read(&gbcodec->users)) {
		dev_err(dev, "Cleanup Error: BE stream not yet closed\n");
		gb_audio_cleanup(gbcodec);
	}

	msm8994_remove_dailink("msm8994-tomtom-mtp-snd-card", &gbaudio_dailink,
			       1);
	gbaudio_remove_dailinks(gbcodec);

	snd_soc_unregister_codec(dev);
	dev->driver = NULL;
	gbaudio_tplg_release(gbcodec);
	kfree(gbcodec->topology);
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

static int gbaudio_dai_probe(struct gb_connection *connection)
{
	struct gbaudio_dai *dai;
	struct device *dev = &connection->bundle->dev;
	int dev_id = connection->intf->interface_id;
	struct gbaudio_codec_info *gbcodec = dev_get_drvdata(dev);
	struct gb_audio_manager_module_descriptor desc;

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

	/* inform above layer for uevent */
	if (!gbcodec->set_uevent && gbcodec->codec_registered &&
	    (gbcodec->dai_added == gbcodec->num_dais)) {
		/* prepare for the audio manager */
		dev_dbg(dev, "Inform set_event:%d to above layer\n", 1);
		strlcpy(desc.name, gbcodec->name,
			GB_AUDIO_MANAGER_MODULE_NAME_LEN); /* todo */
		desc.slot = 1; /* todo */
		desc.vid = 2; /* todo */
		desc.pid = 3; /* todo */
		desc.cport = gbcodec->dev_id;
		desc.devices = 0x2; /* todo */
		gbcodec->manager_id = gb_audio_manager_add(&desc);
		gbcodec->set_uevent = 1;
	}
	mutex_unlock(&gbcodec->lock);

	return 0;
}

static void gbaudio_dai_remove(struct gb_connection *connection)
{
	struct device *dev = &connection->bundle->dev;
	int dev_id = connection->intf->interface_id;
	struct gbaudio_codec_info *gbcodec;

	dev_dbg(dev, "Remove DAI device:%d:%s\n", dev_id, dev_name(dev));

	/* get gbcodec data */
	gbcodec = gbaudio_find_codec(dev, dev_id);
	if (!gbcodec)
		return;

	/* inform uevent to above layers */
	mutex_lock(&gbcodec->lock);
	if (gbcodec->set_uevent) {
		/* notify the audio manager */
		dev_dbg(dev, "Inform set_event:%d to above layer\n", 0);
		gb_audio_manager_remove(gbcodec->manager_id);
		gbcodec->set_uevent = 0;
	}
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
