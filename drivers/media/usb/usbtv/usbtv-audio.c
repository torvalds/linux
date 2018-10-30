/*
 * Copyright (c) 2013 Federico Simoncelli
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Fushicai USBTV007 Audio-Video Grabber Driver
 *
 * Product web site:
 * http://www.fushicai.com/products_detail/&productId=d05449ee-b690-42f9-a661-aa7353894bed.html
 *
 * No physical hardware was harmed running Windows during the
 * reverse-engineering activity
 */

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/ac97_codec.h>
#include <sound/pcm_params.h>

#include "usbtv.h"

static const struct snd_pcm_hardware snd_usbtv_digital_hw = {
	.info = SNDRV_PCM_INFO_BATCH |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.period_bytes_min = 11059,
	.period_bytes_max = 13516,
	.periods_min = 2,
	.periods_max = 98,
	.buffer_bytes_max = 62720 * 8, /* value in usbaudio.c */
};

static int snd_usbtv_pcm_open(struct snd_pcm_substream *substream)
{
	struct usbtv *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	chip->snd_substream = substream;
	runtime->hw = snd_usbtv_digital_hw;

	return 0;
}

static int snd_usbtv_pcm_close(struct snd_pcm_substream *substream)
{
	struct usbtv *chip = snd_pcm_substream_chip(substream);

	if (atomic_read(&chip->snd_stream)) {
		atomic_set(&chip->snd_stream, 0);
		schedule_work(&chip->snd_trigger);
	}

	return 0;
}

static int snd_usbtv_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *hw_params)
{
	int rv;
	struct usbtv *chip = snd_pcm_substream_chip(substream);

	rv = snd_pcm_lib_malloc_pages(substream,
		params_buffer_bytes(hw_params));

	if (rv < 0) {
		dev_warn(chip->dev, "pcm audio buffer allocation failure %i\n",
			rv);
		return rv;
	}

	return 0;
}

static int snd_usbtv_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int snd_usbtv_prepare(struct snd_pcm_substream *substream)
{
	struct usbtv *chip = snd_pcm_substream_chip(substream);

	chip->snd_buffer_pos = 0;
	chip->snd_period_pos = 0;

	return 0;
}

static void usbtv_audio_urb_received(struct urb *urb)
{
	struct usbtv *chip = urb->context;
	struct snd_pcm_substream *substream = chip->snd_substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	size_t i, frame_bytes, chunk_length, buffer_pos, period_pos;
	int period_elapsed;
	unsigned long flags;
	void *urb_current;

	switch (urb->status) {
	case 0:
	case -ETIMEDOUT:
		break;
	case -ENOENT:
	case -EPROTO:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return;
	default:
		dev_warn(chip->dev, "unknown audio urb status %i\n",
			urb->status);
	}

	if (!atomic_read(&chip->snd_stream))
		return;

	frame_bytes = runtime->frame_bits >> 3;
	chunk_length = USBTV_CHUNK / frame_bytes;

	buffer_pos = chip->snd_buffer_pos;
	period_pos = chip->snd_period_pos;
	period_elapsed = 0;

	for (i = 0; i < urb->actual_length; i += USBTV_CHUNK_SIZE) {
		urb_current = urb->transfer_buffer + i + USBTV_AUDIO_HDRSIZE;

		if (buffer_pos + chunk_length >= runtime->buffer_size) {
			size_t cnt = (runtime->buffer_size - buffer_pos) *
				frame_bytes;
			memcpy(runtime->dma_area + buffer_pos * frame_bytes,
				urb_current, cnt);
			memcpy(runtime->dma_area, urb_current + cnt,
				chunk_length * frame_bytes - cnt);
		} else {
			memcpy(runtime->dma_area + buffer_pos * frame_bytes,
				urb_current, chunk_length * frame_bytes);
		}

		buffer_pos += chunk_length;
		period_pos += chunk_length;

		if (buffer_pos >= runtime->buffer_size)
			buffer_pos -= runtime->buffer_size;

		if (period_pos >= runtime->period_size) {
			period_pos -= runtime->period_size;
			period_elapsed = 1;
		}
	}

	snd_pcm_stream_lock_irqsave(substream, flags);

	chip->snd_buffer_pos = buffer_pos;
	chip->snd_period_pos = period_pos;

	snd_pcm_stream_unlock_irqrestore(substream, flags);

	if (period_elapsed)
		snd_pcm_period_elapsed(substream);

	usb_submit_urb(urb, GFP_ATOMIC);
}

static int usbtv_audio_start(struct usbtv *chip)
{
	unsigned int pipe;
	static const u16 setup[][2] = {
		/* These seem to enable the device. */
		{ USBTV_BASE + 0x0008, 0x0001 },
		{ USBTV_BASE + 0x01d0, 0x00ff },
		{ USBTV_BASE + 0x01d9, 0x0002 },

		{ USBTV_BASE + 0x01da, 0x0013 },
		{ USBTV_BASE + 0x01db, 0x0012 },
		{ USBTV_BASE + 0x01e9, 0x0002 },
		{ USBTV_BASE + 0x01ec, 0x006c },
		{ USBTV_BASE + 0x0294, 0x0020 },
		{ USBTV_BASE + 0x0255, 0x00cf },
		{ USBTV_BASE + 0x0256, 0x0020 },
		{ USBTV_BASE + 0x01eb, 0x0030 },
		{ USBTV_BASE + 0x027d, 0x00a6 },
		{ USBTV_BASE + 0x0280, 0x0011 },
		{ USBTV_BASE + 0x0281, 0x0040 },
		{ USBTV_BASE + 0x0282, 0x0011 },
		{ USBTV_BASE + 0x0283, 0x0040 },
		{ 0xf891, 0x0010 },

		/* this sets the input from composite */
		{ USBTV_BASE + 0x0284, 0x00aa },
	};

	chip->snd_bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (chip->snd_bulk_urb == NULL)
		goto err_alloc_urb;

	pipe = usb_rcvbulkpipe(chip->udev, USBTV_AUDIO_ENDP);

	chip->snd_bulk_urb->transfer_buffer = kzalloc(
		USBTV_AUDIO_URBSIZE, GFP_KERNEL);
	if (chip->snd_bulk_urb->transfer_buffer == NULL)
		goto err_transfer_buffer;

	usb_fill_bulk_urb(chip->snd_bulk_urb, chip->udev, pipe,
		chip->snd_bulk_urb->transfer_buffer, USBTV_AUDIO_URBSIZE,
		usbtv_audio_urb_received, chip);

	/* starting the stream */
	usbtv_set_regs(chip, setup, ARRAY_SIZE(setup));

	usb_clear_halt(chip->udev, pipe);
	usb_submit_urb(chip->snd_bulk_urb, GFP_ATOMIC);

	return 0;

err_transfer_buffer:
	usb_free_urb(chip->snd_bulk_urb);
	chip->snd_bulk_urb = NULL;

err_alloc_urb:
	return -ENOMEM;
}

static int usbtv_audio_stop(struct usbtv *chip)
{
	static const u16 setup[][2] = {
	/* The original windows driver sometimes sends also:
	 *   { USBTV_BASE + 0x00a2, 0x0013 }
	 * but it seems useless and its real effects are untested at
	 * the moment.
	 */
		{ USBTV_BASE + 0x027d, 0x0000 },
		{ USBTV_BASE + 0x0280, 0x0010 },
		{ USBTV_BASE + 0x0282, 0x0010 },
	};

	if (chip->snd_bulk_urb) {
		usb_kill_urb(chip->snd_bulk_urb);
		kfree(chip->snd_bulk_urb->transfer_buffer);
		usb_free_urb(chip->snd_bulk_urb);
		chip->snd_bulk_urb = NULL;
	}

	usbtv_set_regs(chip, setup, ARRAY_SIZE(setup));

	return 0;
}

void usbtv_audio_suspend(struct usbtv *usbtv)
{
	if (atomic_read(&usbtv->snd_stream) && usbtv->snd_bulk_urb)
		usb_kill_urb(usbtv->snd_bulk_urb);
}

void usbtv_audio_resume(struct usbtv *usbtv)
{
	if (atomic_read(&usbtv->snd_stream) && usbtv->snd_bulk_urb)
		usb_submit_urb(usbtv->snd_bulk_urb, GFP_ATOMIC);
}

static void snd_usbtv_trigger(struct work_struct *work)
{
	struct usbtv *chip = container_of(work, struct usbtv, snd_trigger);

	if (!chip->snd)
		return;

	if (atomic_read(&chip->snd_stream))
		usbtv_audio_start(chip);
	else
		usbtv_audio_stop(chip);
}

static int snd_usbtv_card_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct usbtv *chip = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		atomic_set(&chip->snd_stream, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		atomic_set(&chip->snd_stream, 0);
		break;
	default:
		return -EINVAL;
	}

	schedule_work(&chip->snd_trigger);

	return 0;
}

static snd_pcm_uframes_t snd_usbtv_pointer(struct snd_pcm_substream *substream)
{
	struct usbtv *chip = snd_pcm_substream_chip(substream);

	return chip->snd_buffer_pos;
}

static const struct snd_pcm_ops snd_usbtv_pcm_ops = {
	.open = snd_usbtv_pcm_open,
	.close = snd_usbtv_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_usbtv_hw_params,
	.hw_free = snd_usbtv_hw_free,
	.prepare = snd_usbtv_prepare,
	.trigger = snd_usbtv_card_trigger,
	.pointer = snd_usbtv_pointer,
};

int usbtv_audio_init(struct usbtv *usbtv)
{
	int rv;
	struct snd_card *card;
	struct snd_pcm *pcm;

	INIT_WORK(&usbtv->snd_trigger, snd_usbtv_trigger);
	atomic_set(&usbtv->snd_stream, 0);

	rv = snd_card_new(&usbtv->udev->dev, SNDRV_DEFAULT_IDX1, "usbtv",
		THIS_MODULE, 0, &card);
	if (rv < 0)
		return rv;

	strscpy(card->driver, usbtv->dev->driver->name, sizeof(card->driver));
	strscpy(card->shortname, "usbtv", sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		"USBTV Audio at bus %d device %d", usbtv->udev->bus->busnum,
		usbtv->udev->devnum);

	snd_card_set_dev(card, usbtv->dev);

	usbtv->snd = card;

	rv = snd_pcm_new(card, "USBTV Audio", 0, 0, 1, &pcm);
	if (rv < 0)
		goto err;

	strscpy(pcm->name, "USBTV Audio Input", sizeof(pcm->name));
	pcm->info_flags = 0;
	pcm->private_data = usbtv;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_usbtv_pcm_ops);
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL), USBTV_AUDIO_BUFFER,
		USBTV_AUDIO_BUFFER);

	rv = snd_card_register(card);
	if (rv)
		goto err;

	return 0;

err:
	usbtv->snd = NULL;
	snd_card_free(card);

	return rv;
}

void usbtv_audio_free(struct usbtv *usbtv)
{
	cancel_work_sync(&usbtv->snd_trigger);

	if (usbtv->snd && usbtv->udev) {
		snd_card_free(usbtv->snd);
		usbtv->snd = NULL;
	}
}
