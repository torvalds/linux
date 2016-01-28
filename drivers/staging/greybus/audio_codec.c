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

static DEFINE_MUTEX(gb_codec_list_lock);
static LIST_HEAD(gb_codec_list);

struct gbaudio_dai *gbaudio_find_dai(struct gbaudio_codec_info *gbcodec,
				     int data_cport, const char *name)
{
	struct gbaudio_dai *dai;

	list_for_each_entry(dai, &gbcodec->dai_list, list) {
		if (name && !strncmp(dai->name, name, NAME_SIZE))
			return dai;
		if ((data_cport != -1) && (dai->data_cport == data_cport))
			return dai;
	}
	return NULL;
}

/*
 * codec DAI ops
 */
static int gbcodec_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	int ret;
	__u16 i2s_port, cportid;

	struct gbaudio_dai *gb_dai;
	struct gbaudio_codec_info *gb = dev_get_drvdata(dai->dev);

	/* find the dai */
	mutex_lock(&gb->lock);
	gb_dai = gbaudio_find_dai(gb, -1, dai->name);
	if (!gb_dai) {
		dev_err(dai->dev, "%s: DAI not registered\n", dai->name);
		mutex_unlock(&gb->lock);
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
		atomic_inc(&gb_dai->users);
	mutex_unlock(&gb->lock);

	return ret;
}

static void gbcodec_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	int ret;
	__u16 i2s_port, cportid;

	struct gbaudio_dai *gb_dai;
	struct gbaudio_codec_info *gb = dev_get_drvdata(dai->dev);

	/* find the dai */
	mutex_lock(&gb->lock);
	gb_dai = gbaudio_find_dai(gb, -1, dai->name);
	if (!gb_dai) {
		dev_err(dai->dev, "%s: DAI not registered\n", dai->name);
		goto func_exit;
	}

	atomic_dec(&gb_dai->users);

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
		goto func_exit;
	}

	if (ret)
		dev_err(dai->dev, "%d:Error during deactivate\n", ret);

	/* un register cport */
	i2s_port = 0;	/* fixed for now */
	ret = gb_audio_apbridgea_unregister_cport(gb_dai->connection, i2s_port,
					gb_dai->connection->hd_cport_id);

	dev_dbg(dai->dev, "Unregister %s:%d DAI, ret:%d\n", dai->name,
		gb_dai->connection->hd_cport_id, ret);
func_exit:
	mutex_unlock(&gb->lock);

	return;
}

static int gbcodec_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *hwparams,
			     struct snd_soc_dai *dai)
{
	int ret;
	uint8_t sig_bits, channels;
	uint32_t format, rate;
	uint16_t data_cport;
	struct gbaudio_dai *gb_dai;
	struct gbaudio_codec_info *gb = dev_get_drvdata(dai->dev);

	/* find the dai */
	mutex_lock(&gb->lock);
	gb_dai = gbaudio_find_dai(gb, -1, dai->name);
	if (!gb_dai) {
		dev_err(dai->dev, "%s: DAI not registered\n", dai->name);
		ret = -EINVAL;
		goto func_exit;
	}

	/*
	 * assuming, currently only 48000 Hz, 16BIT_LE, stereo
	 * is supported, validate params before configuring codec
	 */
	if (params_channels(hwparams) != 2) {
		dev_err(dai->dev, "Invalid channel count:%d\n",
			params_channels(hwparams));
		ret = -EINVAL;
		goto func_exit;
	}
	channels = params_channels(hwparams);

	if (params_rate(hwparams) != 48000) {
		dev_err(dai->dev, "Invalid sampling rate:%d\n",
			params_rate(hwparams));
		ret = -EINVAL;
		goto func_exit;
	}
	rate = GB_AUDIO_PCM_RATE_48000;

	if (params_format(hwparams) != SNDRV_PCM_FORMAT_S16_LE) {
		dev_err(dai->dev, "Invalid format:%d\n",
			params_format(hwparams));
		ret = -EINVAL;
		goto func_exit;
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
		goto func_exit;
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
func_exit:
	mutex_unlock(&gb->lock);
	return ret;
}

static int gbcodec_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	int ret;
	uint16_t data_cport;
	struct gbaudio_dai *gb_dai;
	struct gbaudio_codec_info *gb = dev_get_drvdata(dai->dev);

	/* find the dai */
	mutex_lock(&gb->lock);
	gb_dai = gbaudio_find_dai(gb, -1, dai->name);
	if (!gb_dai) {
		dev_err(dai->dev, "%s: DAI not registered\n", dai->name);
		ret = -EINVAL;
		goto func_exit;
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
			goto func_exit;
		}
		ret = gb_audio_apbridgea_set_rx_data_size(gb_dai->connection, 0,
							  192);
		if (ret) {
			dev_err(dai->dev,
				"%d:Error during apbridgea_set_rx_data_size\n",
				ret);
			goto func_exit;
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
			goto func_exit;
		}
		ret = gb_audio_apbridgea_set_tx_data_size(gb_dai->connection, 0,
							  192);
		if (ret) {
			dev_err(dai->dev,
				"%d:Error during apbridgea set_tx_data_size, cport\n",
				ret);
			goto func_exit;
		}
		ret = gb_audio_gb_activate_tx(gb->mgmt_connection, data_cport);
		break;
	default:
		dev_err(dai->dev, "Invalid stream type %d during prepare\n",
			substream->stream);
		ret = -EINVAL;
		goto func_exit;
	}

	if (ret)
		dev_err(dai->dev, "%d: Error during activate stream\n", ret);

func_exit:
	mutex_unlock(&gb->lock);
	return ret;
}

static int gbcodec_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *dai)
{
	int ret;
	int tx, rx, start, stop;
	struct gbaudio_dai *gb_dai;
	struct gbaudio_codec_info *gb = dev_get_drvdata(dai->dev);

	/* find the dai */
	mutex_lock(&gb->lock);
	gb_dai = gbaudio_find_dai(gb, -1, dai->name);
	if (!gb_dai) {
		dev_err(dai->dev, "%s: DAI not registered\n", dai->name);
		ret = -EINVAL;
		goto func_exit;
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
		ret = -EINVAL;
		goto func_exit;
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
		ret = -EINVAL;
		goto func_exit;
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

func_exit:
	mutex_unlock(&gb->lock);
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

static int gbaudio_add_dailinks(struct gbaudio_codec_info *gbcodec)
{
	int ret, i;
	char *dai_link_name;
	struct snd_soc_dai_link *dailink;
	struct device *dev = gbcodec->dev;

	dailink = &gbaudio_dailink;
	dailink->codec_name = gbcodec->name;

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
		dailink->name = dai_link_name;
		dailink->codec_dai_name = gbcodec->dais[i].name;
	}

	ret = msm8994_add_dailink("msm8994-tomtom-mtp-snd-card", dailink, 1);
	if (ret)
		dev_err(dev, "%d:Error while adding DAI link\n", ret);

	return ret;
}

/*
 * gb_snd management functions
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
		if (atomic_read(&gb_dai->users)) {
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
			atomic_dec(&gb_dai->users);
		}
	}
}

static int gbaudio_register_codec(struct gbaudio_codec_info *gbcodec)
{
	int ret, i;
	struct device *dev = gbcodec->dev;
	struct gb_connection *connection = gbcodec->mgmt_connection;
	/*
	 * FIXME: malloc for topology happens via audio_gb driver
	 * should be done within codec driver itself
	 */
	struct gb_audio_topology *topology;

	ret = gb_connection_enable(connection);
	if (ret) {
		dev_err(dev, "%d: Error while enabling mgmt connection\n", ret);
		return ret;
	}

	gbcodec->dev_id = connection->intf->interface_id;
	/* fetch topology data */
	ret = gb_audio_gb_get_topology(connection, &topology);
	if (ret) {
		dev_err(dev, "%d:Error while fetching topology\n", ret);
		goto tplg_fetch_err;
	}

	/* process topology data */
	ret = gbaudio_tplg_parse_data(gbcodec, topology);
	if (ret) {
		dev_err(dev, "%d:Error while parsing topology data\n",
			  ret);
		goto tplg_parse_err;
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
		goto codec_reg_err;
	}

	/* update DAI links in response to this codec */
	ret = gbaudio_add_dailinks(gbcodec);
	if (ret) {
		dev_err(dev, "%d: Failed to add DAI links\n", ret);
		goto add_dailink_err;
	}

	return 0;

add_dailink_err:
	snd_soc_unregister_codec(dev);
codec_reg_err:
	gbaudio_tplg_release(gbcodec);
	gbcodec->topology = NULL;
tplg_parse_err:
	kfree(topology);
tplg_fetch_err:
	gb_connection_disable(gbcodec->mgmt_connection);
	return ret;
}

static void gbaudio_unregister_codec(struct gbaudio_codec_info *gbcodec)
{
	gb_audio_cleanup(gbcodec);
	msm8994_remove_dailink("msm8994-tomtom-mtp-snd-card", &gbaudio_dailink,
			       1);
	snd_soc_unregister_codec(gbcodec->dev);
	gbaudio_tplg_release(gbcodec);
	kfree(gbcodec->topology);
	gb_connection_disable(gbcodec->mgmt_connection);
}

static int gbaudio_codec_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_audio_streaming_event_request *req = op->request->payload;

	dev_warn(&connection->bundle->dev,
		 "Audio Event received: cport: %u, event: %u\n",
		 req->data_cport, req->event);

	return 0;
}

static int gbaudio_dai_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;

	dev_warn(&connection->bundle->dev, "Audio Event received\n");

	return 0;
}

static int gb_audio_add_mgmt_connection(struct gbaudio_codec_info *gbcodec,
				struct greybus_descriptor_cport *cport_desc,
				struct gb_bundle *bundle)
{
	struct gb_connection *connection;

	/* Management Cport */
	if (gbcodec->mgmt_connection) {
		dev_err(&bundle->dev,
			"Can't have multiple Management connections\n");
		return -ENODEV;
	}

	connection = gb_connection_create(bundle, le16_to_cpu(cport_desc->id),
					  gbaudio_codec_request_handler);
	if (IS_ERR(connection))
		return PTR_ERR(connection);

	connection->private = gbcodec;
	gbcodec->mgmt_connection = connection;

	return 0;
}

static int gb_audio_add_data_connection(struct gbaudio_codec_info *gbcodec,
				struct greybus_descriptor_cport *cport_desc,
				struct gb_bundle *bundle)
{
	struct gb_connection *connection;
	struct gbaudio_dai *dai;

	dai = devm_kzalloc(gbcodec->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai) {
		dev_err(gbcodec->dev, "DAI Malloc failure\n");
		return -ENOMEM;
	}

	connection = gb_connection_create(bundle, le16_to_cpu(cport_desc->id),
					  gbaudio_dai_request_handler);
	if (IS_ERR(connection)) {
		devm_kfree(gbcodec->dev, dai);
		return PTR_ERR(connection);
	}

	connection->private = gbcodec;
	atomic_set(&dai->users, 0);
	dai->data_cport = connection->intf_cport_id;
	dai->connection = connection;
	list_add(&dai->list, &gbcodec->dai_list);

	return 0;
}
/*
 * This is the basic hook get things initialized and registered w/ gb
 */

static int gb_audio_probe(struct gb_bundle *bundle,
			  const struct greybus_bundle_id *id)
{
	struct device *dev = &bundle->dev;
	struct gbaudio_codec_info *gbcodec;
	struct greybus_descriptor_cport *cport_desc;
	struct gb_audio_manager_module_descriptor desc;
	struct gbaudio_dai *dai, *_dai;
	int ret, i;

	/* There should be at least one Management and one Data cport */
	if (bundle->num_cports < 2)
		return -ENODEV;

	mutex_lock(&gb_codec_list_lock);
	/*
	 * There can be only one Management connection and any number of data
	 * connections.
	 */
	gbcodec = devm_kzalloc(dev, sizeof(*gbcodec), GFP_KERNEL);
	if (!gbcodec) {
		mutex_unlock(&gb_codec_list_lock);
		return -ENOMEM;
	}

	gbcodec->num_data_connections = bundle->num_cports - 1;
	mutex_init(&gbcodec->lock);
	INIT_LIST_HEAD(&gbcodec->dai_list);
	INIT_LIST_HEAD(&gbcodec->widget_list);
	INIT_LIST_HEAD(&gbcodec->codec_ctl_list);
	INIT_LIST_HEAD(&gbcodec->widget_ctl_list);
	gbcodec->dev = dev;
	snprintf(gbcodec->name, NAME_SIZE, "%s.%s", dev->driver->name,
		 dev_name(dev));
	greybus_set_drvdata(bundle, gbcodec);

	/* Create all connections */
	for (i = 0; i < bundle->num_cports; i++) {
		cport_desc = &bundle->cport_desc[i];

		switch (cport_desc->protocol_id) {
		case GREYBUS_PROTOCOL_AUDIO_MGMT:
			ret = gb_audio_add_mgmt_connection(gbcodec, cport_desc,
							   bundle);
			if (ret)
				goto destroy_connections;
			break;
		case GREYBUS_PROTOCOL_AUDIO_DATA:
			ret = gb_audio_add_data_connection(gbcodec, cport_desc,
							   bundle);
			if (ret)
				goto destroy_connections;
			break;
		default:
			dev_err(dev, "Unsupported protocol: 0x%02x\n",
				cport_desc->protocol_id);
			ret = -ENODEV;
			goto destroy_connections;
		}
	}

	/* There must be a management cport */
	if (!gbcodec->mgmt_connection) {
		ret = -EINVAL;
		dev_err(dev, "Missing management connection\n");
		goto destroy_connections;
	}

	/* Initialize management connection */
	ret = gbaudio_register_codec(gbcodec);
	if (ret)
		goto destroy_connections;

	/* Initialize data connections */
	list_for_each_entry(dai, &gbcodec->dai_list, list) {
		ret = gb_connection_enable(dai->connection);
		if (ret)
			goto remove_dai;
	}

	/* inform above layer for uevent */
	dev_dbg(dev, "Inform set_event:%d to above layer\n", 1);
	/* prepare for the audio manager */
	strlcpy(desc.name, gbcodec->name, GB_AUDIO_MANAGER_MODULE_NAME_LEN);
	desc.slot = 1; /* todo */
	desc.vid = 2; /* todo */
	desc.pid = 3; /* todo */
	desc.cport = gbcodec->dev_id;
	desc.devices = 0x2; /* todo */
	gbcodec->manager_id = gb_audio_manager_add(&desc);

	list_add(&gbcodec->list, &gb_codec_list);
	dev_dbg(dev, "Add GB Audio device:%s\n", gbcodec->name);
	mutex_unlock(&gb_codec_list_lock);

	return 0;

remove_dai:
	list_for_each_entry_safe(dai, _dai, &gbcodec->dai_list, list)
		gb_connection_disable(dai->connection);

	gbaudio_unregister_codec(gbcodec);
destroy_connections:
	list_for_each_entry_safe(dai, _dai, &gbcodec->dai_list, list) {
		gb_connection_destroy(dai->connection);
		list_del(&dai->list);
		devm_kfree(dev, dai);
	}

	if (gbcodec->mgmt_connection)
		gb_connection_destroy(gbcodec->mgmt_connection);

	devm_kfree(dev, gbcodec);
	mutex_unlock(&gb_codec_list_lock);

	return ret;
}

static void gb_audio_disconnect(struct gb_bundle *bundle)
{
	struct gbaudio_codec_info *gbcodec = greybus_get_drvdata(bundle);
	struct gbaudio_dai *dai, *_dai;

	mutex_lock(&gb_codec_list_lock);
	list_del(&gbcodec->list);
	/* inform uevent to above layers */
	gb_audio_manager_remove(gbcodec->manager_id);

	mutex_lock(&gbcodec->lock);
	list_for_each_entry_safe(dai, _dai, &gbcodec->dai_list, list)
		gb_connection_disable(dai->connection);
	gbaudio_unregister_codec(gbcodec);

	list_for_each_entry_safe(dai, _dai, &gbcodec->dai_list, list) {
		gb_connection_destroy(dai->connection);
		list_del(&dai->list);
		devm_kfree(gbcodec->dev, dai);
	}
	gb_connection_destroy(gbcodec->mgmt_connection);
	gbcodec->mgmt_connection = NULL;
	mutex_unlock(&gbcodec->lock);

	devm_kfree(&bundle->dev, gbcodec);
	mutex_unlock(&gb_codec_list_lock);
}

static const struct greybus_bundle_id gb_audio_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_AUDIO) },
	{ }
};
MODULE_DEVICE_TABLE(greybus, gb_audio_id_table);

static struct greybus_driver gb_audio_driver = {
	.name		= "gb-audio",
	.probe		= gb_audio_probe,
	.disconnect	= gb_audio_disconnect,
	.id_table	= gb_audio_id_table,
};
module_greybus_driver(gb_audio_driver);

MODULE_DESCRIPTION("Greybus Audio codec driver");
MODULE_AUTHOR("Vaibhav Agarwal <vaibhav.agarwal@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gbaudio-codec");
