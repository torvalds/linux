// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices ADV7511 HDMI transmitter driver
 *
 * Copyright 2012 Analog Devices Inc.
 * Copyright (c) 2016, Linaro Limited
 */

#include <sound/core.h>
#include <sound/hdmi-codec.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/of_graph.h>

#include "adv7511.h"

static void adv7511_calc_cts_n(unsigned int f_tmds, unsigned int fs,
			       unsigned int *cts, unsigned int *n)
{
	switch (fs) {
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		*n = fs * 128 / 1000;
		break;
	case 44100:
	case 88200:
	case 176400:
		*n = fs * 128 / 900;
		break;
	}

	*cts = ((f_tmds * *n) / (128 * fs)) * 1000;
}

static int adv7511_update_cts_n(struct adv7511 *adv7511)
{
	unsigned int cts = 0;
	unsigned int n = 0;

	adv7511_calc_cts_n(adv7511->f_tmds, adv7511->f_audio, &cts, &n);

	regmap_write(adv7511->regmap, ADV7511_REG_N0, (n >> 16) & 0xf);
	regmap_write(adv7511->regmap, ADV7511_REG_N1, (n >> 8) & 0xff);
	regmap_write(adv7511->regmap, ADV7511_REG_N2, n & 0xff);

	regmap_write(adv7511->regmap, ADV7511_REG_CTS_MANUAL0,
		     (cts >> 16) & 0xf);
	regmap_write(adv7511->regmap, ADV7511_REG_CTS_MANUAL1,
		     (cts >> 8) & 0xff);
	regmap_write(adv7511->regmap, ADV7511_REG_CTS_MANUAL2,
		     cts & 0xff);

	return 0;
}

static int adv7511_hdmi_hw_params(struct device *dev, void *data,
				  struct hdmi_codec_daifmt *fmt,
				  struct hdmi_codec_params *hparms)
{
	struct adv7511 *adv7511 = dev_get_drvdata(dev);
	unsigned int audio_source, i2s_format = 0;
	unsigned int invert_clock;
	unsigned int rate;
	unsigned int len;

	switch (hparms->sample_rate) {
	case 32000:
		rate = ADV7511_SAMPLE_FREQ_32000;
		break;
	case 44100:
		rate = ADV7511_SAMPLE_FREQ_44100;
		break;
	case 48000:
		rate = ADV7511_SAMPLE_FREQ_48000;
		break;
	case 88200:
		rate = ADV7511_SAMPLE_FREQ_88200;
		break;
	case 96000:
		rate = ADV7511_SAMPLE_FREQ_96000;
		break;
	case 176400:
		rate = ADV7511_SAMPLE_FREQ_176400;
		break;
	case 192000:
		rate = ADV7511_SAMPLE_FREQ_192000;
		break;
	default:
		return -EINVAL;
	}

	switch (hparms->sample_width) {
	case 16:
		len = ADV7511_I2S_SAMPLE_LEN_16;
		break;
	case 18:
		len = ADV7511_I2S_SAMPLE_LEN_18;
		break;
	case 20:
		len = ADV7511_I2S_SAMPLE_LEN_20;
		break;
	case 24:
		len = ADV7511_I2S_SAMPLE_LEN_24;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt->fmt) {
	case HDMI_I2S:
		audio_source = ADV7511_AUDIO_SOURCE_I2S;
		i2s_format = ADV7511_I2S_FORMAT_I2S;
		break;
	case HDMI_RIGHT_J:
		audio_source = ADV7511_AUDIO_SOURCE_I2S;
		i2s_format = ADV7511_I2S_FORMAT_RIGHT_J;
		break;
	case HDMI_LEFT_J:
		audio_source = ADV7511_AUDIO_SOURCE_I2S;
		i2s_format = ADV7511_I2S_FORMAT_LEFT_J;
		break;
	case HDMI_SPDIF:
		audio_source = ADV7511_AUDIO_SOURCE_SPDIF;
		break;
	default:
		return -EINVAL;
	}

	invert_clock = fmt->bit_clk_inv;

	regmap_update_bits(adv7511->regmap, ADV7511_REG_AUDIO_SOURCE, 0x70,
			   audio_source << 4);
	regmap_update_bits(adv7511->regmap, ADV7511_REG_AUDIO_CONFIG, BIT(6),
			   invert_clock << 6);
	regmap_update_bits(adv7511->regmap, ADV7511_REG_I2S_CONFIG, 0x03,
			   i2s_format);

	adv7511->audio_source = audio_source;

	adv7511->f_audio = hparms->sample_rate;

	adv7511_update_cts_n(adv7511);

	regmap_update_bits(adv7511->regmap, ADV7511_REG_AUDIO_CFG3,
			   ADV7511_AUDIO_CFG3_LEN_MASK, len);
	regmap_update_bits(adv7511->regmap, ADV7511_REG_I2C_FREQ_ID_CFG,
			   ADV7511_I2C_FREQ_ID_CFG_RATE_MASK, rate << 4);
	regmap_write(adv7511->regmap, 0x73, 0x1);

	return 0;
}

static int audio_startup(struct device *dev, void *data)
{
	struct adv7511 *adv7511 = dev_get_drvdata(dev);

	regmap_update_bits(adv7511->regmap, ADV7511_REG_AUDIO_CONFIG,
				BIT(7), 0);

	/* hide Audio infoframe updates */
	regmap_update_bits(adv7511->regmap, ADV7511_REG_INFOFRAME_UPDATE,
				BIT(5), BIT(5));
	/* enable N/CTS, enable Audio sample packets */
	regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE1,
				BIT(5), BIT(5));
	/* enable N/CTS */
	regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE1,
				BIT(6), BIT(6));
	/* not copyrighted */
	regmap_update_bits(adv7511->regmap, ADV7511_REG_AUDIO_CFG1,
				BIT(5), BIT(5));
	/* enable audio infoframes */
	regmap_update_bits(adv7511->regmap, ADV7511_REG_PACKET_ENABLE1,
				BIT(3), BIT(3));
	/* AV mute disable */
	regmap_update_bits(adv7511->regmap, ADV7511_REG_GC(0),
				BIT(7) | BIT(6), BIT(7));
	/* use Audio infoframe updated info */
	regmap_update_bits(adv7511->regmap, ADV7511_REG_GC(1),
				BIT(5), 0);
	/* enable SPDIF receiver */
	if (adv7511->audio_source == ADV7511_AUDIO_SOURCE_SPDIF)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_AUDIO_CONFIG,
				   BIT(7), BIT(7));

	return 0;
}

static void audio_shutdown(struct device *dev, void *data)
{
	struct adv7511 *adv7511 = dev_get_drvdata(dev);

	if (adv7511->audio_source == ADV7511_AUDIO_SOURCE_SPDIF)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_AUDIO_CONFIG,
				   BIT(7), 0);
}

static int adv7511_hdmi_i2s_get_dai_id(struct snd_soc_component *component,
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

static const struct hdmi_codec_ops adv7511_codec_ops = {
	.hw_params	= adv7511_hdmi_hw_params,
	.audio_shutdown = audio_shutdown,
	.audio_startup	= audio_startup,
	.get_dai_id	= adv7511_hdmi_i2s_get_dai_id,
};

static const struct hdmi_codec_pdata codec_data = {
	.ops = &adv7511_codec_ops,
	.max_i2s_channels = 2,
	.i2s = 1,
	.spdif = 1,
};

int adv7511_audio_init(struct device *dev, struct adv7511 *adv7511)
{
	adv7511->audio_pdev = platform_device_register_data(dev,
					HDMI_CODEC_DRV_NAME,
					PLATFORM_DEVID_AUTO,
					&codec_data,
					sizeof(codec_data));
	return PTR_ERR_OR_ZERO(adv7511->audio_pdev);
}

void adv7511_audio_exit(struct adv7511 *adv7511)
{
	if (adv7511->audio_pdev) {
		platform_device_unregister(adv7511->audio_pdev);
		adv7511->audio_pdev = NULL;
	}
}
