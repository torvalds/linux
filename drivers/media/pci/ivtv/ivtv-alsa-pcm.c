// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ALSA PCM device for the
 *  ALSA interface to ivtv PCM capture streams
 *
 *  Copyright (C) 2009,2012  Andy Walls <awalls@md.metrocast.net>
 *  Copyright (C) 2009  Devin Heitmueller <dheitmueller@kernellabs.com>
 *
 *  Portions of this work were sponsored by ONELAN Limited for the cx18 driver
 */

#include "ivtv-driver.h"
#include "ivtv-queue.h"
#include "ivtv-streams.h"
#include "ivtv-fileops.h"
#include "ivtv-alsa.h"
#include "ivtv-alsa-pcm.h"

#include <sound/core.h>
#include <sound/pcm.h>


static unsigned int pcm_debug;
module_param(pcm_debug, int, 0644);
MODULE_PARM_DESC(pcm_debug, "enable debug messages for pcm");

#define dprintk(fmt, arg...) \
	do { \
		if (pcm_debug) \
			pr_info("ivtv-alsa-pcm %s: " fmt, __func__, ##arg); \
	} while (0)

static const struct snd_pcm_hardware snd_ivtv_hw_capture = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP           |
		SNDRV_PCM_INFO_INTERLEAVED    |
		SNDRV_PCM_INFO_MMAP_VALID,

	.formats = SNDRV_PCM_FMTBIT_S16_LE,

	.rates = SNDRV_PCM_RATE_48000,

	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = 62720 * 8,	/* just about the value in usbaudio.c */
	.period_bytes_min = 64,		/* 12544/2, */
	.period_bytes_max = 12544,
	.periods_min = 2,
	.periods_max = 98,		/* 12544, */
};

static void ivtv_alsa_announce_pcm_data(struct snd_ivtv_card *itvsc,
					u8 *pcm_data,
					size_t num_bytes)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	unsigned int oldptr;
	unsigned int stride;
	int period_elapsed = 0;
	int length;

	dprintk("ivtv alsa announce ptr=%p data=%p num_bytes=%zu\n", itvsc,
		pcm_data, num_bytes);

	substream = itvsc->capture_pcm_substream;
	if (substream == NULL) {
		dprintk("substream was NULL\n");
		return;
	}

	runtime = substream->runtime;
	if (runtime == NULL) {
		dprintk("runtime was NULL\n");
		return;
	}

	stride = runtime->frame_bits >> 3;
	if (stride == 0) {
		dprintk("stride is zero\n");
		return;
	}

	length = num_bytes / stride;
	if (length == 0) {
		dprintk("%s: length was zero\n", __func__);
		return;
	}

	if (runtime->dma_area == NULL) {
		dprintk("dma area was NULL - ignoring\n");
		return;
	}

	oldptr = itvsc->hwptr_done_capture;
	if (oldptr + length >= runtime->buffer_size) {
		unsigned int cnt =
			runtime->buffer_size - oldptr;
		memcpy(runtime->dma_area + oldptr * stride, pcm_data,
		       cnt * stride);
		memcpy(runtime->dma_area, pcm_data + cnt * stride,
		       length * stride - cnt * stride);
	} else {
		memcpy(runtime->dma_area + oldptr * stride, pcm_data,
		       length * stride);
	}
	snd_pcm_stream_lock(substream);

	itvsc->hwptr_done_capture += length;
	if (itvsc->hwptr_done_capture >=
	    runtime->buffer_size)
		itvsc->hwptr_done_capture -=
			runtime->buffer_size;

	itvsc->capture_transfer_done += length;
	if (itvsc->capture_transfer_done >=
	    runtime->period_size) {
		itvsc->capture_transfer_done -=
			runtime->period_size;
		period_elapsed = 1;
	}

	snd_pcm_stream_unlock(substream);

	if (period_elapsed)
		snd_pcm_period_elapsed(substream);
}

static int snd_ivtv_pcm_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_ivtv_card *itvsc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct v4l2_device *v4l2_dev = itvsc->v4l2_dev;
	struct ivtv *itv = to_ivtv(v4l2_dev);
	struct ivtv_stream *s;
	struct ivtv_open_id item;
	int ret;

	/* Instruct the CX2341[56] to start sending packets */
	snd_ivtv_lock(itvsc);

	if (ivtv_init_on_first_open(itv)) {
		snd_ivtv_unlock(itvsc);
		return -ENXIO;
	}

	s = &itv->streams[IVTV_ENC_STREAM_TYPE_PCM];

	v4l2_fh_init(&item.fh, &s->vdev);
	item.itv = itv;
	item.type = s->type;

	/* See if the stream is available */
	if (ivtv_claim_stream(&item, item.type)) {
		/* No, it's already in use */
		v4l2_fh_exit(&item.fh);
		snd_ivtv_unlock(itvsc);
		return -EBUSY;
	}

	if (test_bit(IVTV_F_S_STREAMOFF, &s->s_flags) ||
	    test_and_set_bit(IVTV_F_S_STREAMING, &s->s_flags)) {
		/* We're already streaming.  No additional action required */
		snd_ivtv_unlock(itvsc);
		return 0;
	}


	runtime->hw = snd_ivtv_hw_capture;
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	itvsc->capture_pcm_substream = substream;
	runtime->private_data = itv;

	itv->pcm_announce_callback = ivtv_alsa_announce_pcm_data;

	/* Not currently streaming, so start it up */
	set_bit(IVTV_F_S_STREAMING, &s->s_flags);
	ret = ivtv_start_v4l2_encode_stream(s);
	snd_ivtv_unlock(itvsc);

	return ret;
}

static int snd_ivtv_pcm_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_ivtv_card *itvsc = snd_pcm_substream_chip(substream);
	struct v4l2_device *v4l2_dev = itvsc->v4l2_dev;
	struct ivtv *itv = to_ivtv(v4l2_dev);
	struct ivtv_stream *s;

	/* Instruct the ivtv to stop sending packets */
	snd_ivtv_lock(itvsc);
	s = &itv->streams[IVTV_ENC_STREAM_TYPE_PCM];
	ivtv_stop_v4l2_encode_stream(s, 0);
	clear_bit(IVTV_F_S_STREAMING, &s->s_flags);

	ivtv_release_stream(s);

	itv->pcm_announce_callback = NULL;
	snd_ivtv_unlock(itvsc);

	return 0;
}

static int snd_ivtv_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ivtv_card *itvsc = snd_pcm_substream_chip(substream);

	itvsc->hwptr_done_capture = 0;
	itvsc->capture_transfer_done = 0;

	return 0;
}

static int snd_ivtv_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	return 0;
}

static
snd_pcm_uframes_t snd_ivtv_pcm_pointer(struct snd_pcm_substream *substream)
{
	unsigned long flags;
	snd_pcm_uframes_t hwptr_done;
	struct snd_ivtv_card *itvsc = snd_pcm_substream_chip(substream);

	spin_lock_irqsave(&itvsc->slock, flags);
	hwptr_done = itvsc->hwptr_done_capture;
	spin_unlock_irqrestore(&itvsc->slock, flags);

	return hwptr_done;
}

static const struct snd_pcm_ops snd_ivtv_pcm_capture_ops = {
	.open		= snd_ivtv_pcm_capture_open,
	.close		= snd_ivtv_pcm_capture_close,
	.prepare	= snd_ivtv_pcm_prepare,
	.trigger	= snd_ivtv_pcm_trigger,
	.pointer	= snd_ivtv_pcm_pointer,
};

int snd_ivtv_pcm_create(struct snd_ivtv_card *itvsc)
{
	struct snd_pcm *sp;
	struct snd_card *sc = itvsc->sc;
	struct v4l2_device *v4l2_dev = itvsc->v4l2_dev;
	struct ivtv *itv = to_ivtv(v4l2_dev);
	int ret;

	ret = snd_pcm_new(sc, "CX2341[56] PCM",
			  0, /* PCM device 0, the only one for this card */
			  0, /* 0 playback substreams */
			  1, /* 1 capture substream */
			  &sp);
	if (ret) {
		IVTV_ALSA_ERR("%s: snd_ivtv_pcm_create() failed with err %d\n",
			      __func__, ret);
		goto err_exit;
	}

	spin_lock_init(&itvsc->slock);

	snd_pcm_set_ops(sp, SNDRV_PCM_STREAM_CAPTURE,
			&snd_ivtv_pcm_capture_ops);
	snd_pcm_set_managed_buffer_all(sp, SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);
	sp->info_flags = 0;
	sp->private_data = itvsc;
	strscpy(sp->name, itv->card_name, sizeof(sp->name));

	return 0;

err_exit:
	return ret;
}
