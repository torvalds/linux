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

#include <drm/display/drm_hdmi_state_helper.h>

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

int adv7511_hdmi_audio_prepare(struct drm_bridge *bridge,
			       struct drm_connector *connector,
			       struct hdmi_codec_daifmt *fmt,
			       struct hdmi_codec_params *hparms)
{
	struct adv7511 *adv7511 = bridge_to_adv7511(bridge);
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
	case 32:
		if (fmt->bit_fmt != SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE)
			return -EINVAL;
		fallthrough;
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
		if (fmt->bit_fmt == SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE)
			i2s_format = ADV7511_I2S_IEC958_DIRECT;
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

	return drm_atomic_helper_connector_hdmi_update_audio_infoframe(connector,
								       &hparms->cea);
}

int adv7511_hdmi_audio_startup(struct drm_bridge *bridge,
			       struct drm_connector *connector)
{
	struct adv7511 *adv7511 = bridge_to_adv7511(bridge);

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
	/* AV mute disable */
	regmap_update_bits(adv7511->regmap, ADV7511_REG_GC(0),
				BIT(7) | BIT(6), BIT(7));

	/* enable SPDIF receiver */
	if (adv7511->audio_source == ADV7511_AUDIO_SOURCE_SPDIF)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_AUDIO_CONFIG,
				   BIT(7), BIT(7));

	return 0;
}

void adv7511_hdmi_audio_shutdown(struct drm_bridge *bridge,
				 struct drm_connector *connector)
{
	struct adv7511 *adv7511 = bridge_to_adv7511(bridge);

	if (adv7511->audio_source == ADV7511_AUDIO_SOURCE_SPDIF)
		regmap_update_bits(adv7511->regmap, ADV7511_REG_AUDIO_CONFIG,
				   BIT(7), 0);

	drm_atomic_helper_connector_hdmi_clear_audio_infoframe(connector);
}
