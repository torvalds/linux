/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include "go7007-priv.h"

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
module_param_array(id, charp, NULL, 0444);
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the go7007 audio driver");
MODULE_PARM_DESC(id, "ID string for the go7007 audio driver");
MODULE_PARM_DESC(enable, "Enable for the go7007 audio driver");

struct go7007_snd {
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	spinlock_t lock;
	int w_idx;
	int hw_ptr;
	int avail;
	int capturing;
};

static const struct snd_pcm_hardware go7007_snd_capture_hw = {
	.info			= (SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_BLOCK_TRANSFER |
					SNDRV_PCM_INFO_MMAP_VALID),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= SNDRV_PCM_RATE_48000,
	.rate_min		= 48000,
	.rate_max		= 48000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= (128*1024),
	.period_bytes_min	= 4096,
	.period_bytes_max	= (128*1024),
	.periods_min		= 1,
	.periods_max		= 32,
};

static void parse_audio_stream_data(struct go7007 *go, u8 *buf, int length)
{
	struct go7007_snd *gosnd = go->snd_context;
	struct snd_pcm_runtime *runtime = gosnd->substream->runtime;
	int frames = bytes_to_frames(runtime, length);

	spin_lock(&gosnd->lock);
	gosnd->hw_ptr += frames;
	if (gosnd->hw_ptr >= runtime->buffer_size)
		gosnd->hw_ptr -= runtime->buffer_size;
	gosnd->avail += frames;
	spin_unlock(&gosnd->lock);
	if (gosnd->w_idx + length > runtime->dma_bytes) {
		int cpy = runtime->dma_bytes - gosnd->w_idx;

		memcpy(runtime->dma_area + gosnd->w_idx, buf, cpy);
		length -= cpy;
		buf += cpy;
		gosnd->w_idx = 0;
	}
	memcpy(runtime->dma_area + gosnd->w_idx, buf, length);
	gosnd->w_idx += length;
	spin_lock(&gosnd->lock);
	if (gosnd->avail < runtime->period_size) {
		spin_unlock(&gosnd->lock);
		return;
	}
	gosnd->avail -= runtime->period_size;
	spin_unlock(&gosnd->lock);
	if (gosnd->capturing)
		snd_pcm_period_elapsed(gosnd->substream);
}

static int go7007_snd_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params)
{
	struct go7007 *go = snd_pcm_substream_chip(substream);
	unsigned int bytes;

	bytes = params_buffer_bytes(hw_params);
	if (substream->runtime->dma_bytes > 0)
		vfree(substream->runtime->dma_area);
	substream->runtime->dma_bytes = 0;
	substream->runtime->dma_area = vmalloc(bytes);
	if (substream->runtime->dma_area == NULL)
		return -ENOMEM;
	substream->runtime->dma_bytes = bytes;
	go->audio_deliver = parse_audio_stream_data;
	return 0;
}

static int go7007_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct go7007 *go = snd_pcm_substream_chip(substream);

	go->audio_deliver = NULL;
	if (substream->runtime->dma_bytes > 0)
		vfree(substream->runtime->dma_area);
	substream->runtime->dma_bytes = 0;
	return 0;
}

static int go7007_snd_capture_open(struct snd_pcm_substream *substream)
{
	struct go7007 *go = snd_pcm_substream_chip(substream);
	struct go7007_snd *gosnd = go->snd_context;
	unsigned long flags;
	int r;

	spin_lock_irqsave(&gosnd->lock, flags);
	if (gosnd->substream == NULL) {
		gosnd->substream = substream;
		substream->runtime->hw = go7007_snd_capture_hw;
		r = 0;
	} else
		r = -EBUSY;
	spin_unlock_irqrestore(&gosnd->lock, flags);
	return r;
}

static int go7007_snd_capture_close(struct snd_pcm_substream *substream)
{
	struct go7007 *go = snd_pcm_substream_chip(substream);
	struct go7007_snd *gosnd = go->snd_context;

	gosnd->substream = NULL;
	return 0;
}

static int go7007_snd_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

static int go7007_snd_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct go7007 *go = snd_pcm_substream_chip(substream);
	struct go7007_snd *gosnd = go->snd_context;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* Just set a flag to indicate we should signal ALSA when
		 * sound comes in */
		gosnd->capturing = 1;
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		gosnd->hw_ptr = gosnd->w_idx = gosnd->avail = 0;
		gosnd->capturing = 0;
		return 0;
	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t go7007_snd_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct go7007 *go = snd_pcm_substream_chip(substream);
	struct go7007_snd *gosnd = go->snd_context;

	return gosnd->hw_ptr;
}

static struct page *go7007_snd_pcm_page(struct snd_pcm_substream *substream,
					unsigned long offset)
{
	return vmalloc_to_page(substream->runtime->dma_area + offset);
}

static const struct snd_pcm_ops go7007_snd_capture_ops = {
	.open		= go7007_snd_capture_open,
	.close		= go7007_snd_capture_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= go7007_snd_hw_params,
	.hw_free	= go7007_snd_hw_free,
	.prepare	= go7007_snd_pcm_prepare,
	.trigger	= go7007_snd_pcm_trigger,
	.pointer	= go7007_snd_pcm_pointer,
	.page		= go7007_snd_pcm_page,
};

static int go7007_snd_free(struct snd_device *device)
{
	struct go7007 *go = device->device_data;

	kfree(go->snd_context);
	go->snd_context = NULL;
	return 0;
}

static struct snd_device_ops go7007_snd_device_ops = {
	.dev_free	= go7007_snd_free,
};

int go7007_snd_init(struct go7007 *go)
{
	static int dev;
	struct go7007_snd *gosnd;
	int ret;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}
	gosnd = kmalloc(sizeof(struct go7007_snd), GFP_KERNEL);
	if (gosnd == NULL)
		return -ENOMEM;
	spin_lock_init(&gosnd->lock);
	gosnd->hw_ptr = gosnd->w_idx = gosnd->avail = 0;
	gosnd->capturing = 0;
	ret = snd_card_new(go->dev, index[dev], id[dev], THIS_MODULE, 0,
			   &gosnd->card);
	if (ret < 0) {
		kfree(gosnd);
		return ret;
	}
	ret = snd_device_new(gosnd->card, SNDRV_DEV_LOWLEVEL, go,
			&go7007_snd_device_ops);
	if (ret < 0) {
		kfree(gosnd);
		return ret;
	}
	ret = snd_pcm_new(gosnd->card, "go7007", 0, 0, 1, &gosnd->pcm);
	if (ret < 0) {
		snd_card_free(gosnd->card);
		kfree(gosnd);
		return ret;
	}
	strlcpy(gosnd->card->driver, "go7007", sizeof(gosnd->card->driver));
	strlcpy(gosnd->card->shortname, go->name, sizeof(gosnd->card->driver));
	strlcpy(gosnd->card->longname, gosnd->card->shortname,
			sizeof(gosnd->card->longname));

	gosnd->pcm->private_data = go;
	snd_pcm_set_ops(gosnd->pcm, SNDRV_PCM_STREAM_CAPTURE,
			&go7007_snd_capture_ops);

	ret = snd_card_register(gosnd->card);
	if (ret < 0) {
		snd_card_free(gosnd->card);
		kfree(gosnd);
		return ret;
	}

	gosnd->substream = NULL;
	go->snd_context = gosnd;
	v4l2_device_get(&go->v4l2_dev);
	++dev;

	return 0;
}
EXPORT_SYMBOL(go7007_snd_init);

int go7007_snd_remove(struct go7007 *go)
{
	struct go7007_snd *gosnd = go->snd_context;

	snd_card_disconnect(gosnd->card);
	snd_card_free_when_closed(gosnd->card);
	v4l2_device_put(&go->v4l2_dev);
	return 0;
}
EXPORT_SYMBOL(go7007_snd_remove);

MODULE_LICENSE("GPL v2");
