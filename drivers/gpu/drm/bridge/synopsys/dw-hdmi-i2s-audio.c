/*
 * dw-hdmi-i2s-audio.c
 *
 * Copyright (c) 2016 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <drm/bridge/dw_hdmi.h>

#include <sound/hdmi-codec.h>

#include "dw-hdmi.h"
#include "dw-hdmi-audio.h"

#define DRIVER_NAME "dw-hdmi-i2s-audio"

static inline void hdmi_write(struct dw_hdmi_i2s_audio_data *audio, u8 val, int offset)
{
	struct dw_hdmi *hdmi = audio->hdmi;

	audio->write(hdmi, val, offset);
}

static inline u8 hdmi_read(struct dw_hdmi_i2s_audio_data *audio, int offset)
{
	struct dw_hdmi *hdmi = audio->hdmi;

	return audio->read(hdmi, offset);
}

static inline void hdmi_update_bits(struct dw_hdmi_i2s_audio_data *audio,
				    u8 data, u8 mask, unsigned int reg)
{
	struct dw_hdmi *hdmi = audio->hdmi;

	audio->mod(hdmi, data, mask, reg);
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
	u8 val;

	/* it cares I2S only */
	if ((fmt->fmt != HDMI_I2S) ||
	    (fmt->bit_clk_master | fmt->frame_clk_master)) {
		dev_err(dev, "unsupported format/settings\n");
		return -EINVAL;
	}

	inputclkfs = HDMI_AUD_INPUTCLKFS_128FS;

	switch (hparms->sample_width) {
	case 16:
		conf1 = HDMI_AUD_CONF1_WIDTH_16;
		break;
	case 24:
	case 32:
		conf1 = HDMI_AUD_CONF1_WIDTH_24;
		break;
	default:
		dev_err(dev, "unsupported sample width [%d]\n", hparms->sample_width);
		return -EINVAL;
	}

	switch (hparms->channels) {
	case 2:
		conf0 = HDMI_AUD_CONF0_I2S_2CHANNEL_ENABLE;
		break;
	case 4:
		conf0 = HDMI_AUD_CONF0_I2S_4CHANNEL_ENABLE;
		break;
	case 6:
		conf0 = HDMI_AUD_CONF0_I2S_6CHANNEL_ENABLE;
		break;
	case 8:
		conf0 = HDMI_AUD_CONF0_I2S_8CHANNEL_ENABLE;
		break;
	default:
		dev_err(dev, "unsupported channels [%d]\n", hparms->channels);
		return -EINVAL;
	}

	/*
	 * dw-hdmi introduced insert_pcuv bit in version 2.10a.
	 * When set (1'b1), this bit enables the insertion of the PCUV
	 * (Parity, Channel Status, User bit and Validity) bits on the
	 * incoming audio stream (support limited to Linear PCM audio)
	 */
	val = 0;
	if (hdmi_read(audio, HDMI_DESIGN_ID) >= 0x21)
		val = HDMI_AUD_CONF2_INSERT_PCUV;

	/*Mask fifo empty and full int and reset fifo*/
	hdmi_update_bits(audio,
			 HDMI_AUD_INT_FIFO_EMPTY_MSK |
			 HDMI_AUD_INT_FIFO_FULL_MSK,
			 HDMI_AUD_INT_FIFO_EMPTY_MSK |
			 HDMI_AUD_INT_FIFO_FULL_MSK, HDMI_AUD_INT);
	hdmi_update_bits(audio, HDMI_AUD_CONF0_SW_RESET,
			 HDMI_AUD_CONF0_SW_RESET, HDMI_AUD_CONF0);
	hdmi_update_bits(audio, HDMI_MC_SWRSTZ_I2S_RESET_MSK,
			 HDMI_MC_SWRSTZ_I2S_RESET_MSK, HDMI_MC_SWRSTZ);

	switch (hparms->mode) {
	case NLPCM:
		hdmi_write(audio, HDMI_AUD_CONF2_NLPCM, HDMI_AUD_CONF2);
		conf1 = HDMI_AUD_CONF1_WIDTH_21;
		break;
	case HBR:
		hdmi_write(audio, HDMI_AUD_CONF2_HBR, HDMI_AUD_CONF2);
		conf1 = HDMI_AUD_CONF1_WIDTH_21;
		break;
	default:
		hdmi_write(audio, val, HDMI_AUD_CONF2);
		break;
	}

	dw_hdmi_set_sample_rate(hdmi, hparms->sample_rate);

	hdmi_write(audio, inputclkfs, HDMI_AUD_INPUTCLKFS);
	hdmi_write(audio, conf0, HDMI_AUD_CONF0);
	hdmi_write(audio, conf1, HDMI_AUD_CONF1);

	val = HDMI_FC_AUDSCONF_AUD_PACKET_LAYOUT_LAYOUT0;
	if (hparms->channels > 2)
		val = HDMI_FC_AUDSCONF_AUD_PACKET_LAYOUT_LAYOUT1;
	hdmi_update_bits(audio, val, HDMI_FC_AUDSCONF_AUD_PACKET_LAYOUT_MASK,
			 HDMI_FC_AUDSCONF);

	switch (hparms->sample_rate) {
	case 32000:
		val = HDMI_FC_AUDSCHNLS_32K;
		break;
	case 44100:
		val = HDMI_FC_AUDSCHNLS_441K;
		break;
	case 48000:
		val = HDMI_FC_AUDSCHNLS_48K;
		break;
	case 88200:
		val = HDMI_FC_AUDSCHNLS_882K;
		break;
	case 96000:
		val = HDMI_FC_AUDSCHNLS_96K;
		break;
	case 176400:
		val = HDMI_FC_AUDSCHNLS_1764K;
		break;
	case 192000:
		val = HDMI_FC_AUDSCHNLS_192K;
		break;
	default:
		val = HDMI_FC_AUDSCHNLS_441K;
		break;
	}

	/* set channel status register */
	hdmi_update_bits(audio, val,
			 HDMI_FC_AUDSCHNLS7_SAMPFREQ_MASK,
			 HDMI_FC_AUDSCHNLS7);
	hdmi_write(audio,
		   ((~val) << HDMI_FC_AUDSCHNLS8_ORIGSAMPFREQ_OFFSET),
		   HDMI_FC_AUDSCHNLS8);

	/* Refer to CEA861-E Audio infoFrame
	 * Set both Audio Channel Count and Audio Coding
	 * Type Refer to Stream Head for HDMI
	 */
	hdmi_update_bits(audio,
			 (hparms->channels - 1) << HDMI_FC_AUDICONF0_CC_OFFSET,
			 HDMI_FC_AUDICONF0_CC_MASK, HDMI_FC_AUDICONF0);

	/* Set both Audio Sample Size and Sample Frequency
	 * Refer to Stream Head for HDMI
	 */
	hdmi_write(audio, 0x00, HDMI_FC_AUDICONF1);

	/* Set Channel Allocation */
	hdmi_write(audio, 0x00, HDMI_FC_AUDICONF2);

	/* Set LFEPBLDOWN-MIX INH and LSV */
	hdmi_write(audio, 0x00, HDMI_FC_AUDICONF3);

	hdmi_update_bits(audio, HDMI_AUD_CONF0_SW_RESET,
			 HDMI_AUD_CONF0_SW_RESET, HDMI_AUD_CONF0);
	hdmi_update_bits(audio, HDMI_MC_SWRSTZ_I2S_RESET_MSK,
			 HDMI_MC_SWRSTZ_I2S_RESET_MSK, HDMI_MC_SWRSTZ);

	dw_hdmi_audio_enable(hdmi);

	return 0;
}

static void dw_hdmi_i2s_audio_shutdown(struct device *dev, void *data)
{
	struct dw_hdmi_i2s_audio_data *audio = data;
	struct dw_hdmi *hdmi = audio->hdmi;

	dw_hdmi_audio_disable(hdmi);

	hdmi_write(audio, HDMI_AUD_CONF0_SW_RESET, HDMI_AUD_CONF0);
}

static struct hdmi_codec_ops dw_hdmi_i2s_ops = {
	.hw_params	= dw_hdmi_i2s_hw_params,
	.audio_shutdown	= dw_hdmi_i2s_audio_shutdown,
};

static int snd_dw_hdmi_probe(struct platform_device *pdev)
{
	struct dw_hdmi_i2s_audio_data *audio = pdev->dev.platform_data;
	struct platform_device_info pdevinfo;
	struct hdmi_codec_pdata pdata;

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

	audio->pdev = platform_device_register_full(&pdevinfo);
	return IS_ERR_OR_NULL(audio->pdev);
}

static int snd_dw_hdmi_remove(struct platform_device *pdev)
{
	struct dw_hdmi_i2s_audio_data *audio = pdev->dev.platform_data;

	if (!IS_ERR_OR_NULL(audio->pdev))
		platform_device_unregister(audio->pdev);

	return 0;
}

static struct platform_driver snd_dw_hdmi_driver = {
	.probe	= snd_dw_hdmi_probe,
	.remove = snd_dw_hdmi_remove,
	.driver	= {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};
module_platform_driver(snd_dw_hdmi_driver);

MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_DESCRIPTION("Synopsis Designware HDMI I2S ALSA SoC interface");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
