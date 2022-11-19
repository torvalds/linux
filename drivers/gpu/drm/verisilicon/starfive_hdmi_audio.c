// SPDX-License-Identifier: GPL-2.0
/*
 * HDMI Audio driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 * Author: Xingyu Wu <xingyu.wu@starfivetech.com>
 */
#include <linux/device.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/pcm_params.h>
#include <linux/of_platform.h>
#include <sound/core.h>
#include <sound/pcm.h>

#include "inno_hdmi.h"

#define SF_PCM_RATE_32000_192000  (SNDRV_PCM_RATE_32000 | \
				   SNDRV_PCM_RATE_44100 | \
				   SNDRV_PCM_RATE_48000 | \
				   SNDRV_PCM_RATE_88200 | \
				   SNDRV_PCM_RATE_96000 | \
				   SNDRV_PCM_RATE_176400 | \
				   SNDRV_PCM_RATE_192000)

#define DEV_MUTE		0x005
#define AUDIO_CFG		0x035
#define SAMPLE_FRE		0x037
#define PINS_ENA		0x038
#define CHANNEL_INPUT		0x039
#define N_VALUE1		0x03F
#define N_VALUE2		0x040
#define N_VALUE3		0x041
#define CTS_VALUE1		0x045
#define CTS_VALUE2		0x046
#define CTS_VALUE3		0x047

/* DEV_MUTE */
#define AUDIO_MUTE_MASK		BIT(1)
#define AUDIO_MUTE		BIT(1)
#define AUDIO_NO_MUTE		0x0

/* AUDIO_CFG */
#define MCLK_RATIO_MASK		GENMASK(1, 0)
#define MCLK_128FS		0x0
#define MCLK_256FS		0x1
#define MCLK_384FS		0x2
#define MCLK_512FS		0x3
#define AUDIO_TYPE_SEL_MASK		GENMASK(4, 3)
#define AUDIO_SEL_I2S		0x0
#define AUDIO_SEL_SPDIF		BIT(3)
#define CTS_SOURCE_SEL_MASK		BIT(7)
#define CTS_INTER		0x0
#define CTS_EXTER		BIT(7)

/* SAMPLE_FRE */
#define I2S_SAMP_FREQ_MASK		GENMASK(3, 0)
#define FREQ_32K		0x3
#define FREQ_44K		0x0
#define FREQ_48K		0x2
#define FREQ_88K		0x8
#define FREQ_96K		0xa
#define FREQ_176K		0xc
#define FREQ_192K		0xe

/* PINS_ENA */
#define I2S_FORMAT_MASK		GENMASK(1, 0)
#define STANDARD_MODE		0x0
#define RIGHT_JUSTIFIED_MODE		0x1
#define LEFT_JUSTIFIED_MODE		0x2
#define I2S_PIN_ENA_MASK		GENMASK(5, 2)
#define I2S0_ENA		BIT(2)
#define I2S1_ENA		BIT(3)
#define I2S2_ENA		BIT(4)
#define I2S3_ENA		BIT(5)

/* CHANNEL_INPUT */
#define CHANNEL0_INPUT_MASK		GENMASK(1, 0)
#define CHANNEL0_I2S0		(0x0 << 0)
#define CHANNEL0_I2S3		(0x1 << 0)
#define CHANNEL0_I2S2		(0x2 << 0)
#define CHANNEL0_I2S1		(0x3 << 0)
#define CHANNEL1_INPUT_MASK		GENMASK(3, 2)
#define CHANNEL1_I2S1		(0x0 << 2)
#define CHANNEL1_I2S0		(0x1 << 2)
#define CHANNEL1_I2S3		(0x2 << 2)
#define CHANNEL1_I2S2		(0x3 << 2)
#define CHANNEL2_INPUT_MASK		GENMASK(5, 4)
#define CHANNEL2_I2S2		(0x0 << 4)
#define CHANNEL2_I2S1		(0x1 << 4)
#define CHANNEL2_I2S0		(0x2 << 4)
#define CHANNEL2_I2S3		(0x3 << 4)
#define CHANNEL3_INPUT_MASK		GENMASK(7, 6)
#define CHANNEL3_I2S3		(0x0 << 6)
#define CHANNEL3_I2S2		(0x1 << 6)
#define CHANNEL3_I2S1		(0x2 << 6)
#define CHANNEL3_I2S0		(0x3 << 6)

static int starfive_hdmi_audio_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct inno_hdmi *priv = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* Audio no mute */
		hdmi_modb(priv, DEV_MUTE, AUDIO_MUTE_MASK, AUDIO_NO_MUTE);
		return 0;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		/* Audio mute */
		hdmi_modb(priv, DEV_MUTE, AUDIO_MUTE_MASK, AUDIO_MUTE);
		return 0;

	default:
		return -EINVAL;
	}
}

static int starfive_hdmi_audio_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct inno_hdmi *priv = snd_soc_dai_get_drvdata(dai);
	unsigned int sample_rate;
	unsigned int channels;
	unsigned int rate_reg;
	unsigned int channels_reg;
	unsigned int Nvalue;
	unsigned int CTSvalue;
	unsigned int TMDS = priv->tmds_rate;

	dev_dbg(priv->dev, "HDMI&AUDIO: tmds rate:%d\n", TMDS);
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return 0;

	sample_rate = params_rate(params);
	switch (sample_rate) {
	case 32000:
		rate_reg = FREQ_32K;
		break;
	case 44100:
		rate_reg = FREQ_44K;
		break;
	case 48000:
		rate_reg = FREQ_48K;
		break;
	case 88200:
		rate_reg = FREQ_88K;
		break;
	case 96000:
		rate_reg = FREQ_96K;
		break;
	case 176400:
		rate_reg = FREQ_176K;
		break;
	case 192000:
		rate_reg = FREQ_192K;
		break;
	default:
		dev_err(priv->dev, "HDMI&AUDIO: not support sample rate:%d\n",
			sample_rate);
		return -EINVAL;
	}

	Nvalue = 128 * sample_rate / 1000;
	CTSvalue = TMDS / 1000;

	channels = params_channels(params);
	switch (channels) {
	case 2:
		channels_reg = I2S0_ENA;
		break;
	case 4:
		channels_reg = I2S0_ENA | I2S1_ENA;
		break;
	case 6:
		channels_reg = I2S0_ENA | I2S1_ENA | I2S2_ENA;
		break;
	case 8:
		channels_reg = I2S0_ENA | I2S1_ENA | I2S2_ENA | I2S3_ENA;
		break;
	default:
		dev_err(priv->dev, "HDMI&AUDIO: not support channels:%d\n",
			channels);
		return -EINVAL;
	}

	hdmi_modb(priv, AUDIO_CFG, CTS_SOURCE_SEL_MASK, CTS_EXTER);

	hdmi_writeb(priv, SAMPLE_FRE, rate_reg);

	hdmi_modb(priv, PINS_ENA, I2S_PIN_ENA_MASK, channels_reg);

	/* N{reg3f[3:0],reg40[7:0],reg41[7:0]} */
	hdmi_writeb(priv, N_VALUE1, ((Nvalue >> 16) & 0xf));
	hdmi_writeb(priv, N_VALUE2, ((Nvalue >> 8) & 0xff));
	hdmi_writeb(priv, N_VALUE3, ((Nvalue >> 0) & 0xff));

	/* CTS{reg45[3:0],reg46[7:0],reg47[7:0]} */
	hdmi_writeb(priv, CTS_VALUE1, ((CTSvalue >> 16) & 0xf));
	hdmi_writeb(priv, CTS_VALUE2, ((CTSvalue >> 8) & 0xff));
	hdmi_writeb(priv, CTS_VALUE3, ((CTSvalue >> 0) & 0xff));

	dev_dbg(priv->dev, "HDMI&AUDIO: AUDIO_CFG :0x%x\n",
			hdmi_readb(priv, AUDIO_CFG));
	dev_dbg(priv->dev, "HDMI&AUDIO: CHANNEL_INPUT :0x%x\n",
			hdmi_readb(priv, CHANNEL_INPUT));
	dev_dbg(priv->dev, "HDMI&AUDIO: SAMPLE_FRE :0x%x\n",
			hdmi_readb(priv, SAMPLE_FRE));
	dev_dbg(priv->dev, "HDMI&AUDIO: PINS_ENA :0x%x\n",
			hdmi_readb(priv, PINS_ENA));
	dev_dbg(priv->dev, "HDMI&AUDIO: N_VALUE :0x%x, 0x%x, 0x%x\n",
			hdmi_readb(priv, N_VALUE1),
			hdmi_readb(priv, N_VALUE2),
			hdmi_readb(priv, N_VALUE3));
	dev_dbg(priv->dev, "HDMI&AUDIO: CTS_VALUE :0x%x,0x%x,0x%x\n",
			hdmi_readb(priv, CTS_VALUE1),
			hdmi_readb(priv, CTS_VALUE2),
			hdmi_readb(priv, CTS_VALUE3));

	return 0;
}

static int starfive_hdmi_audio_probe(struct snd_soc_component *component)
{
	/* In this time, HDMI has suspend and cannot read and write register. */
	return 0;
}

static const struct snd_soc_dai_ops starfive_hdmi_audio_dai_ops = {
	.trigger = starfive_hdmi_audio_trigger,
	.hw_params = starfive_hdmi_audio_hw_params,
};

static struct snd_soc_dai_driver starfive_hdmi_audio_dai = {
	.name = "starfive-hdmi-audio",
	.id = 0,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SF_PCM_RATE_32000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &starfive_hdmi_audio_dai_ops,
	.symmetric_rate = 1,
};

static const struct snd_soc_component_driver starfive_hdmi_audio_component = {
	.name = "starfive-hdmi-audio",
	.probe = starfive_hdmi_audio_probe,
};

int starfive_hdmi_audio_init(struct inno_hdmi *hdmi)
{
	int ret;

	ret = devm_snd_soc_register_component(hdmi->dev, &starfive_hdmi_audio_component,
					       &starfive_hdmi_audio_dai, 1);
	if (ret) {
		dev_err(hdmi->dev, "HDMI&AUDIO: not able to register dai\n");
		return ret;
	}

	/* Use external CTS source */
	hdmi_modb(hdmi, AUDIO_CFG, CTS_SOURCE_SEL_MASK, CTS_EXTER);

	/* select I2S type */
	hdmi_modb(hdmi, AUDIO_CFG, AUDIO_TYPE_SEL_MASK, AUDIO_SEL_I2S);

	/* MCLK ratio 0:128fs, 1:256fs, 2:384fs, 3:512fs */
	hdmi_modb(hdmi, AUDIO_CFG, MCLK_RATIO_MASK, MCLK_256FS);

	/* I2S format 0:standard, 1:right-justified, 2:left-justified */
	hdmi_modb(hdmi, PINS_ENA, I2S_FORMAT_MASK, STANDARD_MODE);

	/* Audio channel input */
	hdmi_writeb(hdmi, CHANNEL_INPUT, CHANNEL0_I2S0 | CHANNEL1_I2S1 |
				CHANNEL2_I2S2 | CHANNEL3_I2S3);

	/* Audio mute */
	hdmi_modb(hdmi, DEV_MUTE, AUDIO_MUTE_MASK, AUDIO_MUTE);

	dev_info(hdmi->dev, "HDMI&AUDIO register done.\n");

	return 0;
}
