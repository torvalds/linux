/*
 * APBridge ALSA SoC dummy codec driver
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <uapi/linux/input.h>

#include "audio_codec.h"
#include "audio_apbridgea.h"
#include "audio_manager.h"

static struct gbaudio_codec_info *gbcodec;

static struct gbaudio_data_connection *
find_data(struct gbaudio_module_info *module, const char *name)
{
	struct gbaudio_data_connection *data;

	list_for_each_entry(data, &module->data_list, list) {
		if (name && !strncmp(data->name, name, NAME_SIZE))
			return data;
	}
	return NULL;
}

static int find_stream(const char *name)
{
	int stream = 0;

	if (strnstr(name, "SPK Amp", NAME_SIZE))
		stream |= GB_PLAYBACK;

	return stream;
}

static int gbaudio_module_disable(struct gbaudio_codec_info *codec,
				  struct gbaudio_module_info *module,
				  int dir)
{
	int ret = 0;
	uint16_t data_cport, cportid, i2s_port;
	int codec_state, module_state;
	struct gbaudio_data_connection *data;
	const char *dai_name;

	mutex_lock(&codec->lock);

	codec_state = codec->stream[dir].state;
	if (codec_state == GBAUDIO_CODEC_SHUTDOWN) {
		mutex_unlock(&codec->lock);
		return 0;
	}

	dai_name = codec->stream[dir].dai_name;

	module_state = module->ctrlstate[dir];
	if (module_state == GBAUDIO_CODEC_SHUTDOWN) {
		dev_dbg(codec->dev, "%s: module already configured\n",
			module->name);
		goto func_exit;
	}

	/* find the dai */
	data = find_data(module, dai_name);
	if (!data) {
		dev_err(codec->dev, "%s:%s DATA connection missing\n",
			dai_name, module->name);
		ret = -ENODEV;
		goto func_exit;
	}
	if (codec_state > GBAUDIO_CODEC_HWPARAMS) {
		data_cport = data->connection->intf_cport_id;
		switch(dir) {
		case SNDRV_PCM_STREAM_CAPTURE:
			ret = gb_audio_gb_deactivate_rx(
						module->mgmt_connection,
						data_cport);
			break;
		case SNDRV_PCM_STREAM_PLAYBACK:
			ret = gb_audio_gb_deactivate_tx(
						module->mgmt_connection,
						data_cport);
			break;
		default:
			ret = -EINVAL;
		}
		if (ret) {
			dev_err_ratelimited(codec->dev, "deactivate for %s failed:%d\n",
				module->name, ret);
			goto func_exit;
		}
		dev_dbg(codec->dev, "Dynamic deactivate %s:%d DAI\n", dai_name,
			data_cport);
	}
	if (codec_state > GBAUDIO_CODEC_SHUTDOWN) {
		cportid = data->connection->hd_cport_id;
		switch(dir) {
		case SNDRV_PCM_STREAM_CAPTURE:
			ret = gb_audio_apbridgea_unregister_cport(
						data->connection,
						i2s_port, cportid,
						AUDIO_APBRIDGEA_DIRECTION_RX);
			break;
		case SNDRV_PCM_STREAM_PLAYBACK:
			ret = gb_audio_apbridgea_unregister_cport(
						data->connection,
						i2s_port, cportid,
						AUDIO_APBRIDGEA_DIRECTION_TX);
			break;
		default:
			ret = -EINVAL;
		}
		if (ret) {
			dev_err_ratelimited(codec->dev, "unregister_cport for %s failed:%d\n",
				module->name, ret);
			goto func_exit;
		}
		dev_dbg(codec->dev, "Dynamic Unregister %s:%d DAI\n", dai_name,
			cportid);
	}
	module->ctrlstate[dir] = GBAUDIO_CODEC_SHUTDOWN;

func_exit:
	mutex_unlock(&codec->lock);
	return ret;
}

static int gbaudio_module_enable(struct gbaudio_codec_info *codec,
				 struct gbaudio_module_info *module, int dir)
{
	int ret = 0;
	__u16 i2s_port, cportid;
	int codec_state, module_state;
	uint16_t data_cport;
	uint8_t sig_bits, channels;
	uint32_t format, rate;
	struct gbaudio_data_connection *data;
	const char *dai_name;

	mutex_lock(&codec->lock);

	codec_state = codec->stream[dir].state;
	if (codec_state == GBAUDIO_CODEC_SHUTDOWN) {
		mutex_unlock(&codec->lock);
		return 0;
	}

	dai_name = codec->stream[dir].dai_name;
	format = codec->stream[dir].format;
	channels = codec->stream[dir].channels;
	rate = codec->stream[dir].rate;
	sig_bits = codec->stream[dir].sig_bits;

	module_state = module->ctrlstate[dir];
	if (module_state == codec_state) {
		dev_dbg(codec->dev, "%s: module already configured\n",
			module->name);
		goto func_exit;
	}

	/* find the dai */
	data = find_data(module, dai_name);
	if (!data) {
		dev_err(codec->dev, "%s:%s DATA connection missing\n",
			dai_name, module->name);
		ret = -ENODEV;
		goto func_exit;
	}

	/* register cport */
	if (module_state < codec_state) {
		i2s_port = 0;	/* fixed for now */
		cportid = data->connection->hd_cport_id;
		switch(dir) {
		case SNDRV_PCM_STREAM_CAPTURE:
				ret = gb_audio_apbridgea_register_cport(
						data->connection,
						i2s_port, cportid,
						AUDIO_APBRIDGEA_DIRECTION_RX);
				break;
		case SNDRV_PCM_STREAM_PLAYBACK:
				ret = gb_audio_apbridgea_register_cport(
						data->connection,
						i2s_port, cportid,
						AUDIO_APBRIDGEA_DIRECTION_TX);
				break;
		default:
				ret = -EINVAL;
		}
		if (ret) {
			dev_err_ratelimited(codec->dev, "reg_cport for %s\n", module->name);
			goto func_exit;
		}
		module_state = GBAUDIO_CODEC_STARTUP;
		dev_dbg(codec->dev, "Dynamic Register %s:%d DAI\n", dai_name,
			cportid);
	}

	/* hw_params */
	if (module_state < codec_state) {
		data_cport = data->connection->intf_cport_id;
		ret = gb_audio_gb_set_pcm(module->mgmt_connection, data_cport,
					  format, rate, channels, sig_bits);
		if (ret) {
			dev_err_ratelimited(codec->dev, "set_pcm for %s\n", module->name);
			goto func_exit;
		}
		module_state = GBAUDIO_CODEC_HWPARAMS;
		dev_dbg(codec->dev, "Dynamic hw_params %s:%d DAI\n", dai_name,
			data_cport);
	}

	/* prepare */
	if (module_state < codec_state) {
		data_cport = data->connection->intf_cport_id;
		switch(dir) {
		case SNDRV_PCM_STREAM_CAPTURE:
			ret = gb_audio_gb_set_rx_data_size(
						module->mgmt_connection,
						data_cport, 192);
			if (ret) {
				dev_err_ratelimited(codec->dev,
					"set_rx_data_size for %s\n",
					module->name);
				goto func_exit;
			}
			ret = gb_audio_gb_activate_rx(module->mgmt_connection,
						      data_cport);
			if (ret) {
				dev_err_ratelimited(codec->dev, "activate_rx for %s\n",
					module->name);
				goto func_exit;
			}
			break;
		case SNDRV_PCM_STREAM_PLAYBACK:
			ret = gb_audio_gb_set_tx_data_size(
						module->mgmt_connection,
						data_cport, 192);
			if (ret) {
				dev_err_ratelimited(codec->dev,
					"set_tx_data_size for %s\n",
					module->name);
				goto func_exit;
			}
			ret = gb_audio_gb_activate_tx(module->mgmt_connection,
						      data_cport);
			if (ret) {
				dev_err_ratelimited(codec->dev, "activate_tx for %s\n",
					module->name);
				goto func_exit;
			}
			break;
		default:
			dev_err(codec->dev, "Inavlid stream direction\n");
			ret = -EINVAL;
			goto func_exit;
		}
		module_state = GBAUDIO_CODEC_PREPARE;
		dev_dbg(codec->dev, "Dynamic prepare %s:%d DAI\n", dai_name,
			data_cport);
	}

func_exit:
	module->ctrlstate[dir] = module_state;
	mutex_unlock(&codec->lock);
	return ret;
}

int gbaudio_module_update(struct gbaudio_codec_info *codec,
				 const char *w_name,
				 struct gbaudio_module_info *module, int enable)
{
	int stream, ret = 0;
	int pb_state, cap_state;

	dev_dbg(module->dev, "Module update %s sequence\n",
		enable ? "Enable":"Disable");

	stream = find_stream(w_name);
	if (!stream) {
		dev_dbg(codec->dev, "No action required for %s\n", w_name);
		return 0;
	}

	/* check if playback active */
	pb_state = codec->stream[SNDRV_PCM_STREAM_PLAYBACK].state;
	if ((stream & GB_PLAYBACK) && (pb_state > GBAUDIO_CODEC_SHUTDOWN)) {
		if (enable)
			ret = gbaudio_module_enable(codec, module,
						    SNDRV_PCM_STREAM_PLAYBACK);
		else
			ret = gbaudio_module_disable(codec, module,
						     SNDRV_PCM_STREAM_PLAYBACK);
	}

	/* check if capture active */
	cap_state = codec->stream[SNDRV_PCM_STREAM_CAPTURE].state;
	if ((stream & GB_CAPTURE) && (cap_state > GBAUDIO_CODEC_SHUTDOWN)) {
		if (enable)
			ret = gbaudio_module_enable(codec, module,
						    SNDRV_PCM_STREAM_CAPTURE);
		else
			ret = gbaudio_module_disable(codec, module,
						     SNDRV_PCM_STREAM_CAPTURE);
	}

	return ret;
}
EXPORT_SYMBOL(gbaudio_module_update);

/*
 * codec DAI ops
 */
static int gbcodec_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	int ret = 0;
	__u16 i2s_port, cportid;
	int state;
	struct gbaudio_data_connection *data;
	struct gbaudio_module_info *module;
	struct gbaudio_codec_info *codec = dev_get_drvdata(dai->dev);

	mutex_lock(&codec->lock);

	if (list_empty(&codec->module_list)) {
		dev_err(codec->dev, "No codec module available\n");
		mutex_unlock(&codec->lock);
		return -ENODEV;
	}

	state = codec->stream[substream->stream].state;
	list_for_each_entry(module, &codec->module_list, list) {
		/* find the dai */
		data = find_data(module, dai->name);
		if (!data) {
			dev_err(dai->dev, "%s:%s DATA connection missing\n",
				dai->name, module->name);
			continue;
		}

		/* register cport */
		i2s_port = 0;	/* fixed for now */
		cportid = data->connection->hd_cport_id;
		switch (substream->stream) {
		case SNDRV_PCM_STREAM_CAPTURE:
			ret = gb_audio_apbridgea_register_cport(
						data->connection,
						i2s_port, cportid,
						AUDIO_APBRIDGEA_DIRECTION_RX);
			break;
		case SNDRV_PCM_STREAM_PLAYBACK:
			ret = gb_audio_apbridgea_register_cport(
						data->connection,
						i2s_port, cportid,
						AUDIO_APBRIDGEA_DIRECTION_TX);
			break;
		default:
			dev_err(dai->dev, "Inavlid stream\n");
			mutex_unlock(&codec->lock);
			return -EINVAL;
		}
		dev_dbg(dai->dev, "Register %s:%d DAI, ret:%d\n", dai->name,
			cportid, ret);
		state = GBAUDIO_CODEC_STARTUP;
		module->ctrlstate[substream->stream] = state;
		dev_dbg(dai->dev, "%s: state:%d\n", module->name, state);
	}
	codec->stream[substream->stream].state = state;
	codec->stream[substream->stream].dai_name = dai->name;
	mutex_unlock(&codec->lock);
	/* to prevent suspend in case of active audio */
	pm_stay_awake(dai->dev);

	return ret;
}

static int gbmodule_shutdown_tx(struct gbaudio_module_info *module,
				struct gbaudio_data_connection *data,
				int codec_state, struct device *dev)
{
	int ret, module_state;
	__u16 i2s_port, cportid;

	module_state = module->ctrlstate[0];
	if (module_state == GBAUDIO_CODEC_SHUTDOWN) {
		dev_dbg(dev, "%s: module already configured\n",
			module->name);
		return 0;
	}

	/* deactivate */
	cportid = data->connection->intf_cport_id;
	if (module_state >= GBAUDIO_CODEC_PREPARE) {
		ret = gb_audio_gb_deactivate_tx(module->mgmt_connection,
						cportid);
		if (ret)
			return ret;
	}

	/* unregister cport */
	i2s_port = 0;	/* fixed for now */
	cportid = data->connection->hd_cport_id;
	ret = gb_audio_apbridgea_unregister_cport(data->connection, i2s_port,
						  cportid,
						  AUDIO_APBRIDGEA_DIRECTION_TX);

	return ret;
}

static int gbmodule_shutdown_rx(struct gbaudio_module_info *module,
				struct gbaudio_data_connection *data,
				int codec_state, struct device *dev)
{
	int ret, module_state;
	__u16 i2s_port, cportid;

	module_state = module->ctrlstate[1];
	if (module_state == GBAUDIO_CODEC_SHUTDOWN) {
		dev_dbg(dev, "%s: module already configured\n",
			module->name);
		return 0;
	}

	/* deactivate */
	cportid = data->connection->intf_cport_id;
	if (module_state >= GBAUDIO_CODEC_PREPARE) {
		ret = gb_audio_gb_deactivate_rx(module->mgmt_connection,
						cportid);
		if (ret)
			return ret;
	}

	/* unregister cport */
	i2s_port = 0;	/* fixed for now */
	cportid = data->connection->hd_cport_id;
	ret = gb_audio_apbridgea_unregister_cport(data->connection, i2s_port,
						  cportid,
						  AUDIO_APBRIDGEA_DIRECTION_RX);

	return ret;
}

static void gbcodec_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	int ret, state;
	struct gbaudio_module_info *module;
	struct gbaudio_data_connection *data;
	struct gbaudio_codec_info *codec = dev_get_drvdata(dai->dev);

	mutex_lock(&codec->lock);

	if (list_empty(&codec->module_list)) {
		dev_err(codec->dev, "No codec module available\n");
		codec->stream[substream->stream].state = GBAUDIO_CODEC_SHUTDOWN;
		codec->stream[substream->stream].dai_name = NULL;
		mutex_unlock(&codec->lock);
		pm_relax(dai->dev);
		return;
	}

	state = codec->stream[substream->stream].state;
	list_for_each_entry(module, &codec->module_list, list) {
		/* find the dai */
		data = find_data(module, dai->name);
		if (!data) {
			dev_err(dai->dev, "%s:%s DATA connection missing\n",
				dai->name, module->name);
			continue;
		}

		switch (substream->stream) {
		case SNDRV_PCM_STREAM_PLAYBACK:
			ret = gbmodule_shutdown_tx(module, data, state,
						   dai->dev);
			break;
		case SNDRV_PCM_STREAM_CAPTURE:
			ret = gbmodule_shutdown_rx(module, data, state,
						   dai->dev);
			break;
		}
		dev_dbg(dai->dev, "Unregister %s DAI, ret:%d\n", dai->name,
			ret);
		state = GBAUDIO_CODEC_SHUTDOWN;
		module->ctrlstate[substream->stream] = state;
		dev_dbg(dai->dev, "%s: state:%d\n", module->name, state);
	}
	codec->stream[substream->stream].state = state;
	codec->stream[substream->stream].dai_name = NULL;
	mutex_unlock(&codec->lock);
	pm_relax(dai->dev);
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
	struct gbaudio_module_info *module;
	struct gbaudio_data_connection *data;
	int state;
	struct gbaudio_codec_info *codec = dev_get_drvdata(dai->dev);

	mutex_lock(&codec->lock);

	if (list_empty(&codec->module_list)) {
		dev_err(codec->dev, "No codec module available\n");
		mutex_unlock(&codec->lock);
		return -ENODEV;
	}

	/*
	 * assuming, currently only 48000 Hz, 16BIT_LE, stereo
	 * is supported, validate params before configuring codec
	 */
	if (params_channels(hwparams) != 2) {
		dev_err(dai->dev, "Invalid channel count:%d\n",
			params_channels(hwparams));
		mutex_unlock(&codec->lock);
		return -EINVAL;
	}
	channels = params_channels(hwparams);

	if (params_rate(hwparams) != 48000) {
		dev_err(dai->dev, "Invalid sampling rate:%d\n",
			params_rate(hwparams));
		mutex_unlock(&codec->lock);
		return -EINVAL;
	}
	rate = GB_AUDIO_PCM_RATE_48000;

	if (params_format(hwparams) != SNDRV_PCM_FORMAT_S16_LE) {
		dev_err(dai->dev, "Invalid format:%d\n",
			params_format(hwparams));
		mutex_unlock(&codec->lock);
		return -EINVAL;
	}
	format = GB_AUDIO_PCM_FMT_S16_LE;

	state = codec->stream[substream->stream].state;
	list_for_each_entry(module, &codec->module_list, list) {
		/* find the data connection */
		data = find_data(module, dai->name);
		if (!data) {
			dev_err(dai->dev, "%s:%s DATA connection missing\n",
				dai->name, module->name);
			continue;
		}

		data_cport = data->connection->intf_cport_id;
		/* XXX check impact of sig_bit
		 * it should not change ideally
		 */
		dev_dbg(dai->dev,
			"cport:%d, rate:%d, channel %d, format %d, sig_bits:%d\n",
			data_cport, rate, channels, format, sig_bits);
		ret = gb_audio_gb_set_pcm(module->mgmt_connection, data_cport,
					  format, rate, channels, sig_bits);
		if (ret) {
			dev_err_ratelimited(dai->dev, "%d: Error during set_pcm\n", ret);
			goto func_exit;
		}
		if (state < GBAUDIO_CODEC_HWPARAMS) {
			ret = gb_audio_apbridgea_set_config(data->connection, 0,
						AUDIO_APBRIDGEA_PCM_FMT_16,
						AUDIO_APBRIDGEA_PCM_RATE_48000,
						6144000);
			if (ret) {
				dev_err_ratelimited(dai->dev,
					"%d: Error during set_config\n", ret);
				goto func_exit;
			}
		}
		state = GBAUDIO_CODEC_HWPARAMS;
		module->ctrlstate[substream->stream] = state;
		dev_dbg(dai->dev, "%s: state:%d\n", module->name, state);
	}
	codec->stream[substream->stream].state = state;
	codec->stream[substream->stream].format = format;
	codec->stream[substream->stream].rate = rate;
	codec->stream[substream->stream].channels = channels;
	codec->stream[substream->stream].sig_bits = sig_bits;

func_exit:
	mutex_unlock(&codec->lock);
	return ret;
}

static int gbmodule_prepare_tx(struct gbaudio_module_info *module,
			       struct gbaudio_data_connection *data,
			       int codec_state, struct device *dev)
{
	int ret;
	uint16_t data_cport;

	data_cport = data->connection->intf_cport_id;
	ret = gb_audio_gb_set_tx_data_size(module->mgmt_connection, data_cport,
					   192);
	if (ret) {
		dev_err_ratelimited(dev, "%d:Error during set_tx_data_size, cport:%d\n",
			ret, data_cport);
		return ret;
	}
	if (codec_state < GBAUDIO_CODEC_PREPARE) {
		ret = gb_audio_apbridgea_set_tx_data_size(data->connection, 0,
							  192);
		if (ret) {
			dev_err_ratelimited(dev,
				"%d:Error during apbridgea set_tx_data_size, cport\n",
				ret);
			return ret;
		}
	}
	ret = gb_audio_gb_activate_tx(module->mgmt_connection,
				      data_cport);
	if (ret)
		dev_err_ratelimited(dev, "%s:Error during activate stream,%d\n",
			module->name, ret);

	return ret;
}

static int gbmodule_prepare_rx(struct gbaudio_module_info *module,
			       struct gbaudio_data_connection *data,
			       int codec_state, struct device *dev)
{
	int ret;
	uint16_t data_cport;

	data_cport = data->connection->intf_cport_id;

	ret = gb_audio_gb_set_rx_data_size(module->mgmt_connection, data_cport,
					   192);
	if (ret) {
		dev_err_ratelimited(dev, "%d:Error during set_rx_data_size, cport:%d\n",
			ret, data_cport);
		return ret;
	}
	if (codec_state < GBAUDIO_CODEC_PREPARE) {
		ret = gb_audio_apbridgea_set_rx_data_size(data->connection, 0,
							  192);
		if (ret) {
			dev_err_ratelimited(dev,
				"%d:Error during apbridgea_set_rx_data_size\n",
				ret);
			return ret;
		}
	}
	ret = gb_audio_gb_activate_rx(module->mgmt_connection,
				      data_cport);
	if (ret)
		dev_err_ratelimited(dev, "%s:Error during activate stream,%d\n",
			module->name, ret);

	return ret;
}

static int gbcodec_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	int ret;
	struct gbaudio_module_info *module;
	int state;
	struct gbaudio_data_connection *data;
	struct gbaudio_codec_info *codec = dev_get_drvdata(dai->dev);

	mutex_lock(&codec->lock);

	if (list_empty(&codec->module_list)) {
		dev_err(codec->dev, "No codec module available\n");
		mutex_unlock(&codec->lock);
		return -ENODEV;
	}

	state = codec->stream[substream->stream].state;
	list_for_each_entry(module, &codec->module_list, list) {
		/* find the dai */
		data = find_data(module, dai->name);
		if (!data) {
			dev_err(dai->dev, "%s:%s DATA connection missing\n",
				dai->name, module->name);
			continue;
		}

		switch (substream->stream) {
		case SNDRV_PCM_STREAM_PLAYBACK:
			ret = gbmodule_prepare_tx(module, data, state,
						  dai->dev);
			break;
		case SNDRV_PCM_STREAM_CAPTURE:
			ret = gbmodule_prepare_rx(module, data, state,
						  dai->dev);
			break;
		}
		if (ret == -ENODEV)
			continue;
		if (ret) {
			goto func_exit;
		}

		state = GBAUDIO_CODEC_PREPARE;
		module->ctrlstate[substream->stream] = state;
		dev_dbg(dai->dev, "%s: state:%d\n", module->name, state);
	}
	codec->stream[substream->stream].state = state;

func_exit:
	mutex_unlock(&codec->lock);
	return ret;
}

static int gbcodec_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	int ret;
	struct gbaudio_data_connection *data;
	struct gbaudio_module_info *module;
	struct gbaudio_codec_info *codec = dev_get_drvdata(dai->dev);


	dev_dbg(dai->dev, "Mute:%d, Direction:%s\n", mute,
		stream ? "CAPTURE":"PLAYBACK");

	mutex_lock(&codec->lock);
	if (list_empty(&codec->module_list)) {
		dev_err(codec->dev, "No codec module available\n");
		if (mute) {
			codec->stream[stream].state = GBAUDIO_CODEC_STOP;
			ret = 0;
		} else {
			ret = -ENODEV;
		}
		mutex_unlock(&codec->lock);
		return ret;
	}

	list_for_each_entry(module, &codec->module_list, list) {
		/* find the dai */
		data = find_data(module, dai->name);
		if (data)
			break;
	}
	if (!data) {
		dev_err(dai->dev, "%s:%s DATA connection missing\n",
			dai->name, module->name);
		ret = -ENODEV;
		goto func_exit;
	}

	if (!mute && !stream) {/* start playback */
		ret = gb_audio_apbridgea_prepare_tx(data->connection,
						    0);
		if (!ret)
			ret = gb_audio_apbridgea_start_tx(data->connection,
							  0, 0);
		codec->stream[stream].state = GBAUDIO_CODEC_START;
	} else if (!mute && stream) {/* start capture */
		ret = gb_audio_apbridgea_prepare_rx(data->connection,
						    0);
		if (!ret)
			ret = gb_audio_apbridgea_start_rx(data->connection,
							  0);
		codec->stream[stream].state = GBAUDIO_CODEC_START;
	} else if (mute && !stream) {/* stop playback */
		ret = gb_audio_apbridgea_stop_tx(data->connection, 0);
		if (!ret)
			ret = gb_audio_apbridgea_shutdown_tx(data->connection,
							     0);
		codec->stream[stream].state = GBAUDIO_CODEC_STOP;
	} else if (mute && stream) {/* stop capture */
		ret = gb_audio_apbridgea_stop_rx(data->connection, 0);
		if (!ret)
			ret = gb_audio_apbridgea_shutdown_rx(data->connection,
							     0);
		codec->stream[stream].state = GBAUDIO_CODEC_STOP;
	} else
		ret = -EINVAL;
	if (ret)
		dev_err_ratelimited(dai->dev,
				    "%s:Error during %s %s stream:%d\n",
				    module->name, mute ? "Mute" : "Unmute",
				    stream ? "Capture" : "Playback", ret);

func_exit:
	mutex_unlock(&codec->lock);
	return 0;
}

static struct snd_soc_dai_ops gbcodec_dai_ops = {
	.startup = gbcodec_startup,
	.shutdown = gbcodec_shutdown,
	.hw_params = gbcodec_hw_params,
	.prepare = gbcodec_prepare,
	.mute_stream = gbcodec_mute_stream,
};

static int gbaudio_init_jack(struct gbaudio_module_info *module,
			     struct snd_soc_codec *codec)
{
	int ret;

	if (!module->num_jacks)
		return 0;

	/* register jack(s) in case any */
	if (module->num_jacks > 1) {
		dev_err(module->dev, "Currently supports max=1 jack\n");
		return -EINVAL;
	}

	snprintf(module->jack_name, NAME_SIZE, "GB %d Headset Jack",
		 module->dev_id);
	ret = snd_soc_jack_new(codec, module->jack_name, GBCODEC_JACK_MASK,
			       &module->headset_jack);
	if (ret) {
		dev_err(module->dev, "Failed to create new jack\n");
		return ret;
	}

	snprintf(module->button_name, NAME_SIZE, "GB %d Button Jack",
		 module->dev_id);
	ret = snd_soc_jack_new(codec, module->button_name,
			       GBCODEC_JACK_BUTTON_MASK, &module->button_jack);
	if (ret) {
		dev_err(module->dev, "Failed to create button jack\n");
		return ret;
	}

	ret = snd_jack_set_key(module->button_jack.jack, SND_JACK_BTN_0,
			       KEY_MEDIA);
	if (ret) {
		dev_err(module->dev, "Failed to set BTN_0\n");
		return ret;
	}

	ret = snd_jack_set_key(module->button_jack.jack, SND_JACK_BTN_1,
			       KEY_VOICECOMMAND);
	if (ret) {
		dev_err(module->dev, "Failed to set BTN_1\n");
		return ret;
	}

	ret = snd_jack_set_key(module->button_jack.jack, SND_JACK_BTN_2,
			       KEY_VOLUMEUP);
	if (ret) {
		dev_err(module->dev, "Failed to set BTN_2\n");
		return ret;
	}

	ret = snd_jack_set_key(module->button_jack.jack, SND_JACK_BTN_3,
			       KEY_VOLUMEDOWN);
	if (ret) {
		dev_err(module->dev, "Failed to set BTN_0\n");
		return ret;
	}

	/* FIXME
	 * verify if this is really required
	set_bit(INPUT_PROP_NO_DUMMY_RELEASE,
		module->button_jack.jack->input_dev->propbit);
	*/

	return 0;
}

int gbaudio_register_module(struct gbaudio_module_info *module)
{
	int ret;
	struct snd_soc_codec *codec;
	struct snd_card *card;
	struct snd_soc_jack *jack = NULL;

	if (!gbcodec) {
		dev_err(module->dev, "GB Codec not yet probed\n");
		return -EAGAIN;
	}

	codec = gbcodec->codec;
	card = codec->card->snd_card;

	down_write(&card->controls_rwsem);

	if (module->num_dais) {
		dev_err(gbcodec->dev,
			"%d:DAIs not supported via gbcodec driver\n",
			module->num_dais);
		up_write(&card->controls_rwsem);
		return -EINVAL;
	}

	ret = gbaudio_init_jack(module, codec);
	if (ret) {
		up_write(&card->controls_rwsem);
		return ret;
	}

	if (module->dapm_widgets)
		snd_soc_dapm_new_controls(&codec->dapm, module->dapm_widgets,
					  module->num_dapm_widgets);
	if (module->controls)
		snd_soc_add_codec_controls(codec, module->controls,
				     module->num_controls);
	if (module->dapm_routes)
		snd_soc_dapm_add_routes(&codec->dapm, module->dapm_routes,
					module->num_dapm_routes);

	/* card already instantiated, create widgets here only */
	if (codec->card->instantiated) {
		snd_soc_dapm_link_component_dai_widgets(codec->card,
							&codec->dapm);
#ifdef CONFIG_SND_JACK
		/* register jack devices for this module from codec->jack_list */
		list_for_each_entry(jack, &codec->jack_list, list) {
			if ((jack == &module->headset_jack)
			    || (jack == &module->button_jack))
				snd_device_register(codec->card->snd_card,
						    jack->jack);
		}
#endif
	}

	mutex_lock(&gbcodec->lock);
	list_add(&module->list, &gbcodec->module_list);
	mutex_unlock(&gbcodec->lock);

	if (codec->card->instantiated)
		ret = snd_soc_dapm_new_widgets(&codec->dapm);
	dev_dbg(codec->dev, "Registered %s module\n", module->name);

	up_write(&card->controls_rwsem);
	return ret;
}
EXPORT_SYMBOL(gbaudio_register_module);

static void gbaudio_codec_cleanup(struct gbaudio_module_info *module)
{
	struct gbaudio_data_connection *data;
	int pb_state = gbcodec->stream[0].state;
	int cap_state = gbcodec->stream[1].state;
	int ret;
	uint16_t i2s_port, cportid;

	/* locks already acquired */
	if (!pb_state && !cap_state)
		return;

	dev_dbg(gbcodec->dev, "%s: removed, cleanup APBridge\n", module->name);
	if (pb_state == GBAUDIO_CODEC_START) {
		/* cleanup PB path, only APBridge specific */
		data = find_data(module, gbcodec->stream[0].dai_name);
		if (!data) {
			dev_err(gbcodec->dev, "%s: Missing data pointer\n",
				__func__);
			return;
		}

		if (list_is_singular(&gbcodec->module_list)) {
			ret = gb_audio_apbridgea_stop_tx(data->connection, 0);
			if (ret)
				return;
			ret = gb_audio_apbridgea_shutdown_tx(data->connection,
							     0);
			if (ret)
				return;
		}
		i2s_port = 0;	/* fixed for now */
		cportid = data->connection->hd_cport_id;
		ret = gb_audio_apbridgea_unregister_cport(data->connection,
						i2s_port, cportid,
						AUDIO_APBRIDGEA_DIRECTION_TX);
		module->ctrlstate[0] = GBAUDIO_CODEC_SHUTDOWN;
	}

	if (cap_state == GBAUDIO_CODEC_START) {
		/* cleanup CAP path, only APBridge specific */
		data = find_data(module, gbcodec->stream[1].dai_name);
		if (!data) {
			dev_err(gbcodec->dev, "%s: Missing data pointer\n",
				__func__);
			return;
		}
		if (list_is_singular(&gbcodec->module_list)) {
			ret = gb_audio_apbridgea_stop_rx(data->connection, 0);
			if (ret)
				return;
			ret = gb_audio_apbridgea_shutdown_rx(data->connection,
							     0);
			if (ret)
				return;
		}
		i2s_port = 0;	/* fixed for now */
		cportid = data->connection->hd_cport_id;
		ret = gb_audio_apbridgea_unregister_cport(data->connection,
						i2s_port, cportid,
						AUDIO_APBRIDGEA_DIRECTION_RX);
		module->ctrlstate[1] = GBAUDIO_CODEC_SHUTDOWN;
	}
}

void gbaudio_unregister_module(struct gbaudio_module_info *module)
{
	struct snd_soc_codec *codec = gbcodec->codec;
	struct snd_card *card = codec->card->snd_card;
	struct snd_soc_jack *jack, *next_j;

	dev_dbg(codec->dev, "Unregister %s module\n", module->name);

	down_write(&card->controls_rwsem);
	mutex_lock(&gbcodec->lock);
	gbaudio_codec_cleanup(module);
	list_del(&module->list);
	dev_dbg(codec->dev, "Process Unregister %s module\n", module->name);
	mutex_unlock(&gbcodec->lock);

#ifdef CONFIG_SND_JACK
	/* free jack devices for this module from codec->jack_list */
	list_for_each_entry_safe(jack, next_j, &codec->jack_list, list) {
		if ((jack == &module->headset_jack)
		    || (jack == &module->button_jack)) {
			snd_device_free(codec->card->snd_card, jack->jack);
			list_del(&jack->list);
		}
	}
#endif

	if (module->dapm_routes) {
		dev_dbg(codec->dev, "Removing %d routes\n",
			module->num_dapm_routes);
		snd_soc_dapm_del_routes(&codec->dapm, module->dapm_routes,
					module->num_dapm_routes);
	}
	if (module->controls) {
		dev_dbg(codec->dev, "Removing %d controls\n",
			module->num_controls);
		snd_soc_remove_codec_controls(codec, module->controls,
					  module->num_controls);
	}
	if (module->dapm_widgets) {
		dev_dbg(codec->dev, "Removing %d widgets\n",
			module->num_dapm_widgets);
		snd_soc_dapm_free_controls(&codec->dapm, module->dapm_widgets,
					   module->num_dapm_widgets);
	}

	dev_dbg(codec->dev, "Unregistered %s module\n", module->name);

	up_write(&card->controls_rwsem);
}
EXPORT_SYMBOL(gbaudio_unregister_module);

/*
 * codec driver ops
 */
static int gbcodec_probe(struct snd_soc_codec *codec)
{
	struct gbaudio_codec_info *info;

	info = devm_kzalloc(codec->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = codec->dev;
	INIT_LIST_HEAD(&info->module_list);
	mutex_init(&info->lock);
	info->codec = codec;
	snd_soc_codec_set_drvdata(codec, info);
	gbcodec = info;

        device_init_wakeup(codec->dev, 1);
	return 0;
}

static int gbcodec_remove(struct snd_soc_codec *codec)
{
	/* Empty function for now */
	return 0;
}

static u8 gbcodec_reg[GBCODEC_REG_COUNT] = {
	[GBCODEC_CTL_REG] = GBCODEC_CTL_REG_DEFAULT,
	[GBCODEC_MUTE_REG] = GBCODEC_MUTE_REG_DEFAULT,
	[GBCODEC_PB_LVOL_REG] = GBCODEC_PB_VOL_REG_DEFAULT,
	[GBCODEC_PB_RVOL_REG] = GBCODEC_PB_VOL_REG_DEFAULT,
	[GBCODEC_CAP_LVOL_REG] = GBCODEC_CAP_VOL_REG_DEFAULT,
	[GBCODEC_CAP_RVOL_REG] = GBCODEC_CAP_VOL_REG_DEFAULT,
	[GBCODEC_APB1_MUX_REG] = GBCODEC_APB1_MUX_REG_DEFAULT,
	[GBCODEC_APB2_MUX_REG] = GBCODEC_APB2_MUX_REG_DEFAULT,
};

static int gbcodec_write(struct snd_soc_codec *codec, unsigned int reg,
			 unsigned int value)
{
	int ret = 0;

	if (reg == SND_SOC_NOPM)
		return 0;

	BUG_ON(reg >= GBCODEC_REG_COUNT);

	gbcodec_reg[reg] = value;
	dev_dbg(codec->dev, "reg[%d] = 0x%x\n", reg, value);

	return ret;
}

static unsigned int gbcodec_read(struct snd_soc_codec *codec,
				 unsigned int reg)
{
	unsigned int val = 0;

	if (reg == SND_SOC_NOPM)
		return 0;

	BUG_ON(reg >= GBCODEC_REG_COUNT);

	val = gbcodec_reg[reg];
	dev_dbg(codec->dev, "reg[%d] = 0x%x\n", reg, val);

	return val;
}

static struct snd_soc_dai_driver gbaudio_dai[] = {
	{
		.name = "greybus-apb1",
		.id = 0,
		.playback = {
			.stream_name = "GB Audio Playback",
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FORMAT_S16_LE,
			.rate_max = 48000,
			.rate_min = 48000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.capture = {
			.stream_name = "GB Audio Capture",
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FORMAT_S16_LE,
			.rate_max = 48000,
			.rate_min = 48000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &gbcodec_dai_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_gbaudio = {
	.probe	= gbcodec_probe,
	.remove	= gbcodec_remove,

	.read = gbcodec_read,
	.write = gbcodec_write,

	.reg_cache_size = GBCODEC_REG_COUNT,
	.reg_cache_default = gbcodec_reg_defaults,
	.reg_word_size = 1,

	.idle_bias_off = true,
	.ignore_pmdown_time = 1,
};

#ifdef CONFIG_PM
static int gbaudio_codec_suspend(struct device *dev)
{
	dev_dbg(dev, "%s: suspend\n", __func__);
	return 0;
}

static int gbaudio_codec_resume(struct device *dev)
{
	dev_dbg(dev, "%s: resume\n", __func__);
	return 0;
}

static const struct dev_pm_ops gbaudio_codec_pm_ops = {
	.suspend	= gbaudio_codec_suspend,
	.resume		= gbaudio_codec_resume,
};
#endif

static int gbaudio_codec_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_gbaudio,
			gbaudio_dai, ARRAY_SIZE(gbaudio_dai));
}

static int gbaudio_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static const struct of_device_id greybus_asoc_machine_of_match[]  = {
	{ .compatible = "toshiba,apb-dummy-codec", },
	{},
};

static struct platform_driver gbaudio_codec_driver = {
	.driver = {
		.name = "apb-dummy-codec",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &gbaudio_codec_pm_ops,
#endif
		.of_match_table = greybus_asoc_machine_of_match,
	},
	.probe = gbaudio_codec_probe,
	.remove = gbaudio_codec_remove,
};
module_platform_driver(gbaudio_codec_driver);

MODULE_DESCRIPTION("APBridge ALSA SoC dummy codec driver");
MODULE_AUTHOR("Vaibhav Agarwal <vaibhav.agarwal@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:apb-dummy-codec");
