/*
 * Greybus audio Digital Audio Interface (DAI) driver
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <sound/simple_card.h>

#include "greybus.h"
#include "audio.h"

/*
 * This is the greybus cpu dai logic. It really doesn't do much
 * other then provide the TRIGGER_START/STOP hooks that start
 * and stop the timer sending audio data in the pcm logic.
 */


static int gb_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct gb_snd *snd_dev;


	snd_dev = snd_soc_dai_get_drvdata(rtd->cpu_dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		gb_pcm_hrtimer_start(snd_dev);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		gb_pcm_hrtimer_stop(snd_dev);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * XXX This is annoying, if we don't have a set_fmt function
 * the subsystem returns -ENOTSUPP, which causes applications
 * to fail, so add a dummy function here.
 */
static int gb_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	return 0;
}

static const struct snd_soc_dai_ops gb_dai_ops = {
	.trigger	= gb_dai_trigger,
	.set_fmt	= gb_dai_set_fmt,
};

static struct snd_soc_dai_driver gb_cpu_dai = {
	.name			= "gb-cpu-dai",
	.playback = {
		.rates		= GB_RATES,
		.formats	= GB_FMTS,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops = &gb_dai_ops,
};

static const struct snd_soc_component_driver gb_soc_component = {
	.name		= "gb-component",
};

static int gb_plat_probe(struct platform_device *pdev)
{
	struct gb_snd *snd_dev;
	int ret;

	snd_dev = (struct gb_snd *)pdev->dev.platform_data;
	dev_set_drvdata(&pdev->dev, snd_dev);

	ret = snd_soc_register_component(&pdev->dev, &gb_soc_component,
							&gb_cpu_dai, 1);
	return ret;
}

static int gb_plat_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

struct platform_driver gb_audio_plat_driver = {
	.driver		= {
		.name	= "gb-dai-audio",
	},
	.probe		= gb_plat_probe,
	.remove		= gb_plat_remove,
};
