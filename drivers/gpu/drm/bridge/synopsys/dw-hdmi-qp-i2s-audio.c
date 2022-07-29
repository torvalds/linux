// SPDX-License-Identifier: GPL-2.0
/*
 * dw-hdmi-qp-i2s-audio.c
 *
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_crtc.h>

#include <sound/hdmi-codec.h>

#include "dw-hdmi-qp.h"
#include "dw-hdmi-qp-audio.h"

#define DRIVER_NAME "dw-hdmi-qp-i2s-audio"

static inline void hdmi_write(struct dw_hdmi_qp_i2s_audio_data *audio,
			      u32 val, int offset)
{
	struct dw_hdmi_qp *hdmi = audio->hdmi;

	audio->write(hdmi, val, offset);
}

static inline u32 hdmi_read(struct dw_hdmi_qp_i2s_audio_data *audio, int offset)
{
	struct dw_hdmi_qp *hdmi = audio->hdmi;

	return audio->read(hdmi, offset);
}

static inline void hdmi_mod(struct dw_hdmi_qp_i2s_audio_data *audio,
			    u32 data, u32 mask, u32 reg)
{
	struct dw_hdmi_qp *hdmi = audio->hdmi;

	return audio->mod(hdmi, data, mask, reg);
}

static inline bool is_dw_hdmi_qp_clk_off(struct dw_hdmi_qp_i2s_audio_data *audio)
{
	u32 sta = hdmi_read(audio, CMU_STATUS);

	return (sta & (AUDCLK_OFF | LINKQPCLK_OFF | VIDQPCLK_OFF));
}

static int dw_hdmi_qp_i2s_hw_params(struct device *dev, void *data,
				    struct hdmi_codec_daifmt *fmt,
				    struct hdmi_codec_params *hparms)
{
	struct dw_hdmi_qp_i2s_audio_data *audio = data;
	struct dw_hdmi_qp *hdmi = audio->hdmi;
	u32 conf0 = 0;
	bool ref2stream = false;

	if (is_dw_hdmi_qp_clk_off(audio))
		return 0;

	if (fmt->bit_clk_master | fmt->frame_clk_master) {
		dev_err(dev, "unsupported clock settings\n");
		return -EINVAL;
	}

	/* Reset the audio data path of the AVP */
	hdmi_write(audio, AVP_DATAPATH_PACKET_AUDIO_SWINIT_P, GLOBAL_SWRESET_REQUEST);

	/* Disable AUDS, ACR, AUDI */
	hdmi_mod(audio, 0,
		 PKTSCHED_ACR_TX_EN | PKTSCHED_AUDS_TX_EN | PKTSCHED_AUDI_TX_EN,
		 PKTSCHED_PKT_EN);

	/* Clear the audio FIFO */
	hdmi_write(audio, AUDIO_FIFO_CLR_P, AUDIO_INTERFACE_CONTROL0);

	/* Select I2S interface as the audio source */
	hdmi_mod(audio, AUD_IF_I2S, AUD_IF_SEL_MSK, AUDIO_INTERFACE_CONFIG0);

	/* Enable the active i2s lanes */
	switch (hparms->channels) {
	case 7 ... 8:
		conf0 |= I2S_LINES_EN(3);
		fallthrough;
	case 5 ... 6:
		conf0 |= I2S_LINES_EN(2);
		fallthrough;
	case 3 ... 4:
		conf0 |= I2S_LINES_EN(1);
		fallthrough;
	default:
		conf0 |= I2S_LINES_EN(0);
		break;
	}

	hdmi_mod(audio, conf0, I2S_LINES_EN_MSK, AUDIO_INTERFACE_CONFIG0);

	/*
	 * Enable bpcuv generated internally for L-PCM, or received
	 * from stream for NLPCM/HBR.
	 */
	switch (fmt->bit_fmt) {
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
		conf0 = (hparms->channels == 8) ? AUD_HBR : AUD_ASP;
		conf0 |= I2S_BPCUV_RCV_EN;
		ref2stream = true;
		break;
	default:
		conf0 = AUD_ASP | I2S_BPCUV_RCV_DIS;
		ref2stream = false;
		break;
	}

	hdmi_mod(audio, conf0, I2S_BPCUV_RCV_MSK | AUD_FORMAT_MSK,
		 AUDIO_INTERFACE_CONFIG0);

	/* Enable audio FIFO auto clear when overflow */
	hdmi_mod(audio, AUD_FIFO_INIT_ON_OVF_EN, AUD_FIFO_INIT_ON_OVF_MSK,
		 AUDIO_INTERFACE_CONFIG0);

	dw_hdmi_qp_set_sample_rate(hdmi, hparms->sample_rate);
	dw_hdmi_qp_set_channel_status(hdmi, hparms->iec.status, ref2stream);
	dw_hdmi_qp_set_channel_count(hdmi, hparms->channels);
	dw_hdmi_qp_set_channel_allocation(hdmi, hparms->cea.channel_allocation);
	dw_hdmi_qp_set_audio_infoframe(hdmi, hparms);

	return 0;
}

static int dw_hdmi_qp_i2s_audio_startup(struct device *dev, void *data)
{
	struct dw_hdmi_qp_i2s_audio_data *audio = data;
	struct dw_hdmi_qp *hdmi = audio->hdmi;

	if (is_dw_hdmi_qp_clk_off(audio))
		return 0;

	dw_hdmi_qp_audio_enable(hdmi);

	return 0;
}

static void dw_hdmi_qp_i2s_audio_shutdown(struct device *dev, void *data)
{
	struct dw_hdmi_qp_i2s_audio_data *audio = data;

	if (is_dw_hdmi_qp_clk_off(audio))
		return;

	/*
	 * Keep ACR, AUDI, AUDS packet always on to make SINK device
	 * active for better compatibility and user experience.
	 *
	 * This also fix POP sound on some SINK devices which wakeup
	 * from suspend to active.
	 */
	hdmi_mod(audio, I2S_BPCUV_RCV_DIS, I2S_BPCUV_RCV_MSK,
		 AUDIO_INTERFACE_CONFIG0);
	hdmi_mod(audio, AUDPKT_PBIT_FORCE_EN | AUDPKT_CHSTATUS_OVR_EN,
		 AUDPKT_PBIT_FORCE_EN_MASK | AUDPKT_CHSTATUS_OVR_EN_MASK,
		 AUDPKT_CONTROL0);
}

static int dw_hdmi_qp_i2s_get_eld(struct device *dev, void *data, uint8_t *buf,
				  size_t len)
{
	struct dw_hdmi_qp_i2s_audio_data *audio = data;

	memcpy(buf, audio->eld, min_t(size_t, MAX_ELD_BYTES, len));

	return 0;
}

static int dw_hdmi_qp_i2s_get_dai_id(struct snd_soc_component *component,
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

static int dw_hdmi_qp_i2s_hook_plugged_cb(struct device *dev, void *data,
					  hdmi_codec_plugged_cb fn,
					  struct device *codec_dev)
{
	struct dw_hdmi_qp_i2s_audio_data *audio = data;
	struct dw_hdmi_qp *hdmi = audio->hdmi;

	return dw_hdmi_qp_set_plugged_cb(hdmi, fn, codec_dev);
}

static struct hdmi_codec_ops dw_hdmi_qp_i2s_ops = {
	.hw_params	= dw_hdmi_qp_i2s_hw_params,
	.audio_startup  = dw_hdmi_qp_i2s_audio_startup,
	.audio_shutdown	= dw_hdmi_qp_i2s_audio_shutdown,
	.get_eld	= dw_hdmi_qp_i2s_get_eld,
	.get_dai_id	= dw_hdmi_qp_i2s_get_dai_id,
	.hook_plugged_cb = dw_hdmi_qp_i2s_hook_plugged_cb,
};

static int snd_dw_hdmi_qp_probe(struct platform_device *pdev)
{
	struct dw_hdmi_qp_i2s_audio_data *audio = pdev->dev.platform_data;
	struct platform_device_info pdevinfo;
	struct hdmi_codec_pdata pdata;
	struct platform_device *platform;

	pdata.ops		= &dw_hdmi_qp_i2s_ops;
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

static int snd_dw_hdmi_qp_remove(struct platform_device *pdev)
{
	struct platform_device *platform = dev_get_drvdata(&pdev->dev);

	platform_device_unregister(platform);

	return 0;
}

static struct platform_driver snd_dw_hdmi_qp_driver = {
	.probe	= snd_dw_hdmi_qp_probe,
	.remove	= snd_dw_hdmi_qp_remove,
	.driver	= {
		.name = DRIVER_NAME,
	},
};
module_platform_driver(snd_dw_hdmi_qp_driver);

MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Synopsis Designware HDMI QP I2S ALSA SoC interface");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
