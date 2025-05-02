// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP DisplayPort Subsystem Driver - Audio support
 *
 * Copyright (C) 2015 - 2024 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "zynqmp_disp_regs.h"
#include "zynqmp_dp.h"
#include "zynqmp_dpsub.h"

#define ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK 512
#define ZYNQMP_NUM_PCMS 2

struct zynqmp_dpsub_audio {
	void __iomem *base;

	struct snd_soc_card card;

	const char *dai_name;
	const char *link_names[ZYNQMP_NUM_PCMS];
	const char *pcm_names[ZYNQMP_NUM_PCMS];

	struct snd_soc_dai_driver dai_driver;
	struct snd_dmaengine_pcm_config pcm_configs[2];

	struct snd_soc_dai_link links[ZYNQMP_NUM_PCMS];

	struct {
		struct snd_soc_dai_link_component cpu;
		struct snd_soc_dai_link_component codec;
		struct snd_soc_dai_link_component platform;
	} components[ZYNQMP_NUM_PCMS];

	/*
	 * Protects:
	 * - enabled_streams
	 * - volumes
	 * - current_rate
	 */
	struct mutex enable_lock;

	u32 enabled_streams;
	u32 current_rate;

	u16 volumes[2];
};

static const struct snd_pcm_hardware zynqmp_dp_pcm_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,

	.buffer_bytes_max       = 128 * 1024,
	.period_bytes_min       = 256,
	.period_bytes_max       = 1024 * 1024,
	.periods_min            = 2,
	.periods_max            = 256,
};

static int zynqmp_dp_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   256);

	return 0;
}

static const struct snd_soc_ops zynqmp_dp_ops = {
	.startup = zynqmp_dp_startup,
};

static void zynqmp_dp_audio_write(struct zynqmp_dpsub_audio *audio, int reg,
				  u32 val)
{
	writel(val, audio->base + reg);
}

static int dp_dai_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *socdai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct zynqmp_dpsub *dpsub =
		snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	struct zynqmp_dpsub_audio *audio = dpsub->audio;
	int ret;
	u32 sample_rate;
	struct snd_aes_iec958 iec = { 0 };
	unsigned long rate;

	sample_rate = params_rate(params);

	if (sample_rate != 48000 && sample_rate != 44100)
		return -EINVAL;

	guard(mutex)(&audio->enable_lock);

	if (audio->enabled_streams && audio->current_rate != sample_rate) {
		dev_err(dpsub->dev,
			"Can't change rate while playback enabled\n");
		return -EINVAL;
	}

	if (audio->enabled_streams > 0) {
		/* Nothing to do */
		audio->enabled_streams++;
		return 0;
	}

	audio->current_rate = sample_rate;

	/* Note: clock rate can only be changed if the clock is disabled */
	ret = clk_set_rate(dpsub->aud_clk,
			   sample_rate * ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK);
	if (ret) {
		dev_err(dpsub->dev, "can't set aud_clk to %u err:%d\n",
			sample_rate * ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK, ret);
		return ret;
	}

	clk_prepare_enable(dpsub->aud_clk);

	rate = clk_get_rate(dpsub->aud_clk);

	/* Ignore some offset +- 10 */
	if (abs(sample_rate * ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK - rate) > 10) {
		dev_err(dpsub->dev, "aud_clk offset is higher: %ld\n",
			sample_rate * ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK - rate);
		clk_disable_unprepare(dpsub->aud_clk);
		return -EINVAL;
	}

	pm_runtime_get_sync(dpsub->dev);

	zynqmp_dp_audio_write(audio, ZYNQMP_DISP_AUD_MIXER_VOLUME,
			      audio->volumes[0] | (audio->volumes[1] << 16));

	/* Clear the audio soft reset register as it's an non-reset flop. */
	zynqmp_dp_audio_write(audio, ZYNQMP_DISP_AUD_SOFT_RESET, 0);

	/* Only 2 channel audio is supported now */
	zynqmp_dp_audio_set_channels(dpsub->dp, 2);

	zynqmp_dp_audio_write_n_m(dpsub->dp);

	/* Channel status */

	if (sample_rate == 48000)
		iec.status[3] = IEC958_AES3_CON_FS_48000;
	else
		iec.status[3] = IEC958_AES3_CON_FS_44100;

	for (unsigned int i = 0; i < AES_IEC958_STATUS_SIZE / 4; ++i) {
		u32 v;

		v = (iec.status[(i * 4) + 0] << 0) |
		    (iec.status[(i * 4) + 1] << 8) |
		    (iec.status[(i * 4) + 2] << 16) |
		    (iec.status[(i * 4) + 3] << 24);

		zynqmp_dp_audio_write(audio, ZYNQMP_DISP_AUD_CH_STATUS(i), v);
	}

	zynqmp_dp_audio_enable(dpsub->dp);

	audio->enabled_streams++;

	return 0;
}

static int dp_dai_hw_free(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *socdai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct zynqmp_dpsub *dpsub =
		snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	struct zynqmp_dpsub_audio *audio = dpsub->audio;

	guard(mutex)(&audio->enable_lock);

	/* Nothing to do */
	if (audio->enabled_streams > 1) {
		audio->enabled_streams--;
		return 0;
	}

	pm_runtime_put(dpsub->dev);

	zynqmp_dp_audio_disable(dpsub->dp);

	/*
	 * Reset doesn't work. If we assert reset between audio stop and start,
	 * the audio won't start anymore. Probably we are missing writing
	 * some audio related registers. A/B buf?
	 */
	/*
	zynqmp_disp_audio_write(audio, ZYNQMP_DISP_AUD_SOFT_RESET,
				ZYNQMP_DISP_AUD_SOFT_RESET_AUD_SRST);
	*/

	clk_disable_unprepare(dpsub->aud_clk);

	audio->current_rate = 0;
	audio->enabled_streams--;

	return 0;
}

static const struct snd_soc_dai_ops zynqmp_dp_dai_ops = {
	.hw_params	= dp_dai_hw_params,
	.hw_free	= dp_dai_hw_free,
};

/*
 * Min = 10 * log10(0x1 / 0x2000) = -39.13
 * Max = 10 * log10(0xffffff / 0x2000) = 9.03
 */
static const DECLARE_TLV_DB_RANGE(zynqmp_dp_tlv,
	0x0, 0x0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, -3913, 1),
	0x1, 0x2000, TLV_DB_LINEAR_ITEM(-3913, 0),
	0x2000, 0xffff, TLV_DB_LINEAR_ITEM(0, 903),
);

static const struct snd_kcontrol_new zynqmp_dp_snd_controls[] = {
	SOC_SINGLE_TLV("Input0 Playback Volume", 0,
		       0, 0xffff, 0, zynqmp_dp_tlv),
	SOC_SINGLE_TLV("Input1 Playback Volume", 1,
		       0, 0xffff, 0, zynqmp_dp_tlv),
};

/*
 * Note: these read & write functions only support two "registers", 0 and 1,
 * for volume 0 and 1. In other words, these are not real register read/write
 * functions.
 *
 * This is done to support caching the volume value for the case where the
 * hardware is not enabled, and also to support locking as volumes 0 and 1
 * are in the same register.
 */
static unsigned int zynqmp_dp_dai_read(struct snd_soc_component *component,
				       unsigned int reg)
{
	struct zynqmp_dpsub *dpsub = dev_get_drvdata(component->dev);
	struct zynqmp_dpsub_audio *audio = dpsub->audio;

	return audio->volumes[reg];
}

static int zynqmp_dp_dai_write(struct snd_soc_component *component,
			       unsigned int reg, unsigned int val)
{
	struct zynqmp_dpsub *dpsub = dev_get_drvdata(component->dev);
	struct zynqmp_dpsub_audio *audio = dpsub->audio;

	guard(mutex)(&audio->enable_lock);

	audio->volumes[reg] = val;

	if (audio->enabled_streams)
		zynqmp_dp_audio_write(audio, ZYNQMP_DISP_AUD_MIXER_VOLUME,
				      audio->volumes[0] |
				      (audio->volumes[1] << 16));

	return 0;
}

static const struct snd_soc_component_driver zynqmp_dp_component_driver = {
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.controls		= zynqmp_dp_snd_controls,
	.num_controls		= ARRAY_SIZE(zynqmp_dp_snd_controls),
	.read			= zynqmp_dp_dai_read,
	.write			= zynqmp_dp_dai_write,
};

int zynqmp_audio_init(struct zynqmp_dpsub *dpsub)
{
	struct platform_device *pdev = to_platform_device(dpsub->dev);
	struct device *dev = dpsub->dev;
	struct zynqmp_dpsub_audio *audio;
	struct snd_soc_card *card;
	void *dev_data;
	int ret;

	if (!dpsub->aud_clk)
		return 0;

	audio = devm_kzalloc(dev, sizeof(*audio), GFP_KERNEL);
	if (!audio)
		return -ENOMEM;

	dpsub->audio = audio;

	mutex_init(&audio->enable_lock);

	/* 0x2000 is the zero level, no change */
	audio->volumes[0] = 0x2000;
	audio->volumes[1] = 0x2000;

	audio->dai_name = devm_kasprintf(dev, GFP_KERNEL,
					 "%s-dai", dev_name(dev));
	if (!audio->dai_name)
		return -ENOMEM;

	for (unsigned int i = 0; i < ZYNQMP_NUM_PCMS; ++i) {
		audio->link_names[i] = devm_kasprintf(dev, GFP_KERNEL,
						      "%s-dp-%u", dev_name(dev), i);
		audio->pcm_names[i] = devm_kasprintf(dev, GFP_KERNEL,
						     "%s-pcm-%u", dev_name(dev), i);
		if (!audio->link_names[i] || !audio->pcm_names[i])
			return -ENOMEM;
	}

	audio->base = devm_platform_ioremap_resource_byname(pdev, "aud");
	if (IS_ERR(audio->base))
		return PTR_ERR(audio->base);

	/* Create CPU DAI */

	audio->dai_driver = (struct snd_soc_dai_driver) {
		.name		= audio->dai_name,
		.ops		= &zynqmp_dp_dai_ops,
		.playback	= {
			.channels_min	= 2,
			.channels_max	= 2,
			.rates		= SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
			.formats	= SNDRV_PCM_FMTBIT_S16_LE,
		},
	};

	ret = devm_snd_soc_register_component(dev, &zynqmp_dp_component_driver,
					      &audio->dai_driver, 1);
	if (ret) {
		dev_err(dev, "Failed to register CPU DAI\n");
		return ret;
	}

	/* Create PCMs */

	for (unsigned int i = 0; i < ZYNQMP_NUM_PCMS; ++i) {
		struct snd_dmaengine_pcm_config *pcm_config =
			&audio->pcm_configs[i];

		*pcm_config = (struct snd_dmaengine_pcm_config){
			.name = audio->pcm_names[i],
			.pcm_hardware = &zynqmp_dp_pcm_hw,
			.prealloc_buffer_size = 64 * 1024,
			.chan_names[SNDRV_PCM_STREAM_PLAYBACK] =
				i == 0 ? "aud0" : "aud1",
		};

		ret = devm_snd_dmaengine_pcm_register(dev, pcm_config, 0);
		if (ret) {
			dev_err(dev, "Failed to register PCM %u\n", i);
			return ret;
		}
	}

	/* Create card */

	card = &audio->card;
	card->name = "DisplayPort";
	card->long_name = "DisplayPort Monitor";
	card->driver_name = "zynqmp_dpsub";
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->num_links = ZYNQMP_NUM_PCMS;
	card->dai_link = audio->links;

	for (unsigned int i = 0; i < ZYNQMP_NUM_PCMS; ++i) {
		struct snd_soc_dai_link *link = &card->dai_link[i];

		link->ops = &zynqmp_dp_ops;

		link->name = audio->link_names[i];
		link->stream_name = audio->link_names[i];

		link->cpus = &audio->components[i].cpu;
		link->num_cpus = 1;
		link->cpus[0].dai_name = audio->dai_name;

		link->codecs = &audio->components[i].codec;
		link->num_codecs = 1;
		link->codecs[0].name = "snd-soc-dummy";
		link->codecs[0].dai_name = "snd-soc-dummy-dai";

		link->platforms = &audio->components[i].platform;
		link->num_platforms = 1;
		link->platforms[0].name = audio->pcm_names[i];
	}

	/*
	 * HACK: devm_snd_soc_register_card() overwrites current drvdata
	 * so we need to hack it back.
	 */
	dev_data = dev_get_drvdata(dev);
	ret = devm_snd_soc_register_card(dev, card);
	dev_set_drvdata(dev, dev_data);
	if (ret) {
		/*
		 * As older dtbs may not have the audio channel dmas defined,
		 * instead of returning an error here we'll continue and just
		 * mark the audio as disabled.
		 */
		dev_err(dev, "Failed to register sound card, disabling audio support\n");

		devm_kfree(dev, audio);
		dpsub->audio = NULL;

		return 0;
	}

	return 0;
}

void zynqmp_audio_uninit(struct zynqmp_dpsub *dpsub)
{
	struct zynqmp_dpsub_audio *audio = dpsub->audio;

	if (!audio)
		return;

	if (!dpsub->aud_clk)
		return;

	mutex_destroy(&audio->enable_lock);
}
