/*
 * Copyright (C) 2010-2013 Bluecherry, LLC <http://www.bluecherrydvr.com>
 *
 * Original author:
 * Ben Collins <bcollins@ubuntu.com>
 *
 * Additional work by:
 * John Brooks <john.brooks@bluecherry.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/mempool.h>
#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/control.h>

#include "solo6x10.h"
#include "solo6x10-tw28.h"

#define G723_FDMA_PAGES		32
#define G723_PERIOD_BYTES	48
#define G723_PERIOD_BLOCK	1024
#define G723_FRAMES_PER_PAGE	48

/* Sets up channels 16-19 for decoding and 0-15 for encoding */
#define OUTMODE_MASK		0x300

#define SAMPLERATE		8000
#define BITRATE			25

/* The solo writes to 1k byte pages, 32 pages, in the dma. Each 1k page
 * is broken down to 20 * 48 byte regions (one for each channel possible)
 * with the rest of the page being dummy data. */
#define PERIODS			G723_FDMA_PAGES
#define G723_INTR_ORDER		4 /* 0 - 4 */

struct solo_snd_pcm {
	int				on;
	spinlock_t			lock;
	struct solo_dev			*solo_dev;
	u8				*g723_buf;
	dma_addr_t			g723_dma;
};

static void solo_g723_config(struct solo_dev *solo_dev)
{
	int clk_div;

	clk_div = (solo_dev->clock_mhz * 1000000)
		/ (SAMPLERATE * (BITRATE * 2) * 2);

	solo_reg_write(solo_dev, SOLO_AUDIO_SAMPLE,
		       SOLO_AUDIO_BITRATE(BITRATE)
		       | SOLO_AUDIO_CLK_DIV(clk_div));

	solo_reg_write(solo_dev, SOLO_AUDIO_FDMA_INTR,
		       SOLO_AUDIO_FDMA_INTERVAL(1)
		       | SOLO_AUDIO_INTR_ORDER(G723_INTR_ORDER)
		       | SOLO_AUDIO_FDMA_BASE(SOLO_G723_EXT_ADDR(solo_dev) >> 16));

	solo_reg_write(solo_dev, SOLO_AUDIO_CONTROL,
		       SOLO_AUDIO_ENABLE
		       | SOLO_AUDIO_I2S_MODE
		       | SOLO_AUDIO_I2S_MULTI(3)
		       | SOLO_AUDIO_MODE(OUTMODE_MASK));
}

void solo_g723_isr(struct solo_dev *solo_dev)
{
	struct snd_pcm_str *pstr =
		&solo_dev->snd_pcm->streams[SNDRV_PCM_STREAM_CAPTURE];
	struct snd_pcm_substream *ss;
	struct solo_snd_pcm *solo_pcm;

	for (ss = pstr->substream; ss != NULL; ss = ss->next) {
		if (snd_pcm_substream_chip(ss) == NULL)
			continue;

		/* This means open() hasn't been called on this one */
		if (snd_pcm_substream_chip(ss) == solo_dev)
			continue;

		/* Haven't triggered a start yet */
		solo_pcm = snd_pcm_substream_chip(ss);
		if (!solo_pcm->on)
			continue;

		snd_pcm_period_elapsed(ss);
	}
}

static int snd_solo_hw_params(struct snd_pcm_substream *ss,
			      struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(ss, params_buffer_bytes(hw_params));
}

static int snd_solo_hw_free(struct snd_pcm_substream *ss)
{
	return snd_pcm_lib_free_pages(ss);
}

static const struct snd_pcm_hardware snd_solo_pcm_hw = {
	.info			= (SNDRV_PCM_INFO_MMAP |
				   SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP_VALID),
	.formats		= SNDRV_PCM_FMTBIT_U8,
	.rates			= SNDRV_PCM_RATE_8000,
	.rate_min		= SAMPLERATE,
	.rate_max		= SAMPLERATE,
	.channels_min		= 1,
	.channels_max		= 1,
	.buffer_bytes_max	= G723_PERIOD_BYTES * PERIODS,
	.period_bytes_min	= G723_PERIOD_BYTES,
	.period_bytes_max	= G723_PERIOD_BYTES,
	.periods_min		= PERIODS,
	.periods_max		= PERIODS,
};

static int snd_solo_pcm_open(struct snd_pcm_substream *ss)
{
	struct solo_dev *solo_dev = snd_pcm_substream_chip(ss);
	struct solo_snd_pcm *solo_pcm;

	solo_pcm = kzalloc(sizeof(*solo_pcm), GFP_KERNEL);
	if (solo_pcm == NULL)
		goto oom;

	solo_pcm->g723_buf = pci_alloc_consistent(solo_dev->pdev,
						  G723_PERIOD_BYTES,
						  &solo_pcm->g723_dma);
	if (solo_pcm->g723_buf == NULL)
		goto oom;

	spin_lock_init(&solo_pcm->lock);
	solo_pcm->solo_dev = solo_dev;
	ss->runtime->hw = snd_solo_pcm_hw;

	snd_pcm_substream_chip(ss) = solo_pcm;

	return 0;

oom:
	kfree(solo_pcm);
	return -ENOMEM;
}

static int snd_solo_pcm_close(struct snd_pcm_substream *ss)
{
	struct solo_snd_pcm *solo_pcm = snd_pcm_substream_chip(ss);

	snd_pcm_substream_chip(ss) = solo_pcm->solo_dev;
	pci_free_consistent(solo_pcm->solo_dev->pdev, G723_PERIOD_BYTES,
			    solo_pcm->g723_buf, solo_pcm->g723_dma);
	kfree(solo_pcm);

	return 0;
}

static int snd_solo_pcm_trigger(struct snd_pcm_substream *ss, int cmd)
{
	struct solo_snd_pcm *solo_pcm = snd_pcm_substream_chip(ss);
	struct solo_dev *solo_dev = solo_pcm->solo_dev;
	int ret = 0;

	spin_lock(&solo_pcm->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (solo_pcm->on == 0) {
			/* If this is the first user, switch on interrupts */
			if (atomic_inc_return(&solo_dev->snd_users) == 1)
				solo_irq_on(solo_dev, SOLO_IRQ_G723);
			solo_pcm->on = 1;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (solo_pcm->on) {
			/* If this was our last user, switch them off */
			if (atomic_dec_return(&solo_dev->snd_users) == 0)
				solo_irq_off(solo_dev, SOLO_IRQ_G723);
			solo_pcm->on = 0;
		}
		break;
	default:
		ret = -EINVAL;
	}

	spin_unlock(&solo_pcm->lock);

	return ret;
}

static int snd_solo_pcm_prepare(struct snd_pcm_substream *ss)
{
	return 0;
}

static snd_pcm_uframes_t snd_solo_pcm_pointer(struct snd_pcm_substream *ss)
{
	struct solo_snd_pcm *solo_pcm = snd_pcm_substream_chip(ss);
	struct solo_dev *solo_dev = solo_pcm->solo_dev;
	snd_pcm_uframes_t idx = solo_reg_read(solo_dev, SOLO_AUDIO_STA) & 0x1f;

	return idx * G723_FRAMES_PER_PAGE;
}

static int snd_solo_pcm_copy(struct snd_pcm_substream *ss, int channel,
			     snd_pcm_uframes_t pos, void __user *dst,
			     snd_pcm_uframes_t count)
{
	struct solo_snd_pcm *solo_pcm = snd_pcm_substream_chip(ss);
	struct solo_dev *solo_dev = solo_pcm->solo_dev;
	int err, i;

	for (i = 0; i < (count / G723_FRAMES_PER_PAGE); i++) {
		int page = (pos / G723_FRAMES_PER_PAGE) + i;

		err = solo_p2m_dma_t(solo_dev, 0, solo_pcm->g723_dma,
				     SOLO_G723_EXT_ADDR(solo_dev) +
				     (page * G723_PERIOD_BLOCK) +
				     (ss->number * G723_PERIOD_BYTES),
				     G723_PERIOD_BYTES, 0, 0);
		if (err)
			return err;

		err = copy_to_user(dst + (i * G723_PERIOD_BYTES),
				   solo_pcm->g723_buf, G723_PERIOD_BYTES);

		if (err)
			return -EFAULT;
	}

	return 0;
}

static const struct snd_pcm_ops snd_solo_pcm_ops = {
	.open = snd_solo_pcm_open,
	.close = snd_solo_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_solo_hw_params,
	.hw_free = snd_solo_hw_free,
	.prepare = snd_solo_pcm_prepare,
	.trigger = snd_solo_pcm_trigger,
	.pointer = snd_solo_pcm_pointer,
	.copy = snd_solo_pcm_copy,
};

static int snd_solo_capture_volume_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 1;
	info->value.integer.min = 0;
	info->value.integer.max = 15;
	info->value.integer.step = 1;

	return 0;
}

static int snd_solo_capture_volume_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *value)
{
	struct solo_dev *solo_dev = snd_kcontrol_chip(kcontrol);
	u8 ch = value->id.numid - 1;

	value->value.integer.value[0] = tw28_get_audio_gain(solo_dev, ch);

	return 0;
}

static int snd_solo_capture_volume_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *value)
{
	struct solo_dev *solo_dev = snd_kcontrol_chip(kcontrol);
	u8 ch = value->id.numid - 1;
	u8 old_val;

	old_val = tw28_get_audio_gain(solo_dev, ch);
	if (old_val == value->value.integer.value[0])
		return 0;

	tw28_set_audio_gain(solo_dev, ch, value->value.integer.value[0]);

	return 1;
}

static struct snd_kcontrol_new snd_solo_capture_volume = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Volume",
	.info = snd_solo_capture_volume_info,
	.get = snd_solo_capture_volume_get,
	.put = snd_solo_capture_volume_put,
};

static int solo_snd_pcm_init(struct solo_dev *solo_dev)
{
	struct snd_card *card = solo_dev->snd_card;
	struct snd_pcm *pcm;
	struct snd_pcm_substream *ss;
	int ret;
	int i;

	ret = snd_pcm_new(card, card->driver, 0, 0, solo_dev->nr_chans,
			  &pcm);
	if (ret < 0)
		return ret;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_solo_pcm_ops);

	snd_pcm_chip(pcm) = solo_dev;
	pcm->info_flags = 0;
	strcpy(pcm->name, card->shortname);

	for (i = 0, ss = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	     ss; ss = ss->next, i++)
		sprintf(ss->name, "Camera #%d Audio", i);

	ret = snd_pcm_lib_preallocate_pages_for_all(pcm,
					SNDRV_DMA_TYPE_CONTINUOUS,
					snd_dma_continuous_data(GFP_KERNEL),
					G723_PERIOD_BYTES * PERIODS,
					G723_PERIOD_BYTES * PERIODS);
	if (ret < 0)
		return ret;

	solo_dev->snd_pcm = pcm;

	return 0;
}

int solo_g723_init(struct solo_dev *solo_dev)
{
	static struct snd_device_ops ops = { };
	struct snd_card *card;
	struct snd_kcontrol_new kctl;
	char name[32];
	int ret;

	atomic_set(&solo_dev->snd_users, 0);

	/* Allows for easier mapping between video and audio */
	sprintf(name, "Softlogic%d", solo_dev->vfd->num);

	ret = snd_card_new(&solo_dev->pdev->dev,
			   SNDRV_DEFAULT_IDX1, name, THIS_MODULE, 0,
			   &solo_dev->snd_card);
	if (ret < 0)
		return ret;

	card = solo_dev->snd_card;

	strcpy(card->driver, SOLO6X10_NAME);
	strcpy(card->shortname, "SOLO-6x10 Audio");
	sprintf(card->longname, "%s on %s IRQ %d", card->shortname,
		pci_name(solo_dev->pdev), solo_dev->pdev->irq);

	ret = snd_device_new(card, SNDRV_DEV_LOWLEVEL, solo_dev, &ops);
	if (ret < 0)
		goto snd_error;

	/* Mixer controls */
	strcpy(card->mixername, "SOLO-6x10");
	kctl = snd_solo_capture_volume;
	kctl.count = solo_dev->nr_chans;

	ret = snd_ctl_add(card, snd_ctl_new1(&kctl, solo_dev));
	if (ret < 0)
		return ret;

	ret = solo_snd_pcm_init(solo_dev);
	if (ret < 0)
		goto snd_error;

	ret = snd_card_register(card);
	if (ret < 0)
		goto snd_error;

	solo_g723_config(solo_dev);

	dev_info(&solo_dev->pdev->dev, "Alsa sound card as %s\n", name);

	return 0;

snd_error:
	snd_card_free(card);
	return ret;
}

void solo_g723_exit(struct solo_dev *solo_dev)
{
	if (!solo_dev->snd_card)
		return;

	solo_reg_write(solo_dev, SOLO_AUDIO_CONTROL, 0);
	solo_irq_off(solo_dev, SOLO_IRQ_G723);

	snd_card_free(solo_dev->snd_card);
	solo_dev->snd_card = NULL;
}
