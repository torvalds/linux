// SPDX-License-Identifier: GPL-2.0
/*
 * dw-hdmi-i2s-audio.c
 *
 * Copyright (c) 2017 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_crtc.h>

#include <sound/hdmi-codec.h>

#include "dw-hdmi.h"
#include "dw-hdmi-audio.h"

#define DRIVER_NAME "dw-hdmi-i2s-audio"

static inline void hdmi_write(struct dw_hdmi_i2s_audio_data *audio,
			      u8 val, int offset)
{
	struct dw_hdmi *hdmi = audio->hdmi;

	audio->write(hdmi, val, offset);
}

static inline u8 hdmi_read(struct dw_hdmi_i2s_audio_data *audio, int offset)
{
	struct dw_hdmi *hdmi = audio->hdmi;

	return audio->read(hdmi, offset);
}

static int dw_hdmi_i2s_hw_params(struct device *dev, void *data,
				 struct hdmi_codec_daifmt *fmt,
				 struct hdmi_codec_params *hparms)
{
	struct dw_hdmi_i2s_audio_data *audio = data;
	struct dw_hdmi *hdmi = audio->hdmi;
	u8 conf0 = 0;
	u8 conf1 = 0;
	u8 inputclkfs = 0;

	/* it cares I2S only */
	if (fmt->bit_clk_master | fmt->frame_clk_master) {
		dev_err(dev, "unsupported clock settings\n");
		return -EINVAL;
	}

	/* Reset the FIFOs before applying new params */
	hdmi_write(audio, HDMI_AUD_CONF0_SW_RESET, HDMI_AUD_CONF0);
	hdmi_write(audio, (u8)~HDMI_MC_SWRSTZ_I2SSWRST_REQ, HDMI_MC_SWRSTZ);

	inputclkfs	= HDMI_AUD_INPUTCLKFS_64FS;
	conf0		= (HDMI_AUD_CONF0_I2S_SELECT | HDMI_AUD_CONF0_I2S_EN0);

	/* Enable the required i2s lanes */
	switch (hparms->channels) {
	case 7 ... 8:
		conf0 |= HDMI_AUD_CONF0_I2S_EN3;
		/* Fall-thru */
	case 5 ... 6:
		conf0 |= HDMI_AUD_CONF0_I2S_EN2;
		/* Fall-thru */
	case 3 ... 4:
		conf0 |= HDMI_AUD_CONF0_I2S_EN1;
		/* Fall-thru */
	}

	switch (hparms->sample_width) {
	case 16:
		conf1 = HDMI_AUD_CONF1_WIDTH_16;
		break;
	case 24:
	case 32:
		conf1 = HDMI_AUD_CONF1_WIDTH_24;
		break;
	}

	switch (fmt->fmt) {
	case HDMI_I2S:
		conf1 |= HDMI_AUD_CONF1_MODE_I2S;
		break;
	case HDMI_RIGHT_J:
		conf1 |= HDMI_AUD_CONF1_MODE_RIGHT_J;
		break;
	case HDMI_LEFT_J:
		conf1 |= HDMI_AUD_CONF1_MODE_LEFT_J;
		break;
	case HDMI_DSP_A:
		conf1 |= HDMI_AUD_CONF1_MODE_BURST_1;
		break;
	case HDMI_DSP_B:
		conf1 |= HDMI_AUD_CONF1_MODE_BURST_2;
		break;
	default:
		dev_err(dev, "unsupported format\n");
		return -EINVAL;
	}

	dw_hdmi_set_sample_rate(hdmi, hparms->sample_rate);
	dw_hdmi_set_channel_status(hdmi, hparms->iec.status);
	dw_hdmi_set_channel_count(hdmi, hparms->channels);
	dw_hdmi_set_channel_allocation(hdmi, hparms->cea.channel_allocation);

	hdmi_write(audio, inputclkfs, HDMI_AUD_INPUTCLKFS);
	hdmi_write(audio, conf0, HDMI_AUD_CONF0);
	hdmi_write(audio, conf1, HDMI_AUD_CONF1);

	return 0;
}

static int dw_hdmi_i2s_audio_startup(struct device *dev, void *data)
{
	struct dw_hdmi_i2s_audio_data *audio = data;
	struct dw_hdmi *hdmi = audio->hdmi;

	dw_hdmi_audio_enable(hdmi);

	return 0;
}

static void dw_hdmi_i2s_audio_shutdown(struct device *dev, void *data)
{
	struct dw_hdmi_i2s_audio_data *audio = data;
	struct dw_hdmi *hdmi = audio->hdmi;

	dw_hdmi_audio_disable(hdmi);
}

static int dw_hdmi_i2s_get_eld(struct device *dev, void *data, uint8_t *buf,
			       size_t len)
{
	struct dw_hdmi_i2s_audio_data *audio = data;

	memcpy(buf, audio->eld, min_t(size_t, MAX_ELD_BYTES, len));
	return 0;
}

static int dw_hdmi_i2s_get_dai_id(struct snd_soc_component *component,
				  struct device_node *endpoint)
{
	struct of_endpoint of_ep;
	int ret;

	ret = of_graph_parse_endpoint(endpoint, &of_ep);
	if (ret < 0)
		return ret;

	/*
	 * HDMI sound should be located as reg = <2>
	 * Then, it is sound port 0
	 */
	if (of_ep.port == 2)
		return 0;

	return -EINVAL;
}

static int dw_hdmi_i2s_hook_plugged_cb(struct device *dev, void *data,
				       hdmi_codec_plugged_cb fn,
				       struct device *codec_dev)
{
	struct dw_hdmi_i2s_audio_data *audio = data;
	struct dw_hdmi *hdmi = audio->hdmi;

	return dw_hdmi_set_plugged_cb(hdmi, fn, codec_dev);
}

static struct hdmi_codec_ops dw_hdmi_i2s_ops = {
	.hw_params	= dw_hdmi_i2s_hw_params,
	.audio_startup  = dw_hdmi_i2s_audio_startup,
	.audio_shutdown	= dw_hdmi_i2s_audio_shutdown,
	.get_eld	= dw_hdmi_i2s_get_eld,
	.get_dai_id	= dw_hdmi_i2s_get_dai_id,
	.hook_plugged_cb = dw_hdmi_i2s_hook_plugged_cb,
};

static int snd_dw_hdmi_probe(struct platform_device *pdev)
{
	struct dw_hdmi_i2s_audio_data *audio = pdev->dev.platform_data;
	struct platform_device_info pdevinfo;
	struct hdmi_codec_pdata pdata;
	struct platform_device *platform;

	pdata.ops		= &dw_hdmi_i2s_ops;
	pdata.i2s		= 1;
	pdata.max_i2s_channels	= 8;
	pdata.data		= audio;

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.parent		= pdev->dev.parent;
	pdevinfo.id		= PLATFORM_DEVID_AUTO;
	pdevinfo.name		= HDMI_CODEC_DRV_NAME;
	pdevinfo.data		= &pdata;
	pdevinfo.size_data	= sizeof(pdata);
	pdevinfo.dma_mask	= DMA_BIT_MASK(32);

	platform = platform_device_register_full(&pdevinfo);
	if (IS_ERR(platform))
		return PTR_ERR(platform);

	dev_set_drvdata(&pdev->dev, platform);

	return 0;
}

static int snd_dw_hdmi_remove(struct platform_device *pdev)
{
	struct platform_device *platform = dev_get_drvdata(&pdev->dev);

	platform_device_unregister(platform);

	return 0;
}

static struct platform_driver snd_dw_hdmi_driver = {
	.probe	= snd_dw_hdmi_probe,
	.remove	= snd_dw_hdmi_remove,
	.driver	= {
		.name = DRIVER_NAME,
	},
};
module_platform_driver(snd_dw_hdmi_driver);

MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_DESCRIPTION("Synopsis Designware HDMI I2S ALSA SoC interface");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
