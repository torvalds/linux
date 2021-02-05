// SPDX-License-Identifier: GPL-2.0
/* Copyright 2011 Broadcom Corporation.  All rights reserved. */

#include <linux/interrupt.h>
#include <linux/slab.h>

#include <sound/asoundef.h>

#include "bcm2835.h"

/* hardware definition */
static const struct snd_pcm_hardware snd_bcm2835_playback_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_SYNC_APPLPTR),
	.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min = 8000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = 128 * 1024,
	.period_bytes_min = 1 * 1024,
	.period_bytes_max = 128 * 1024,
	.periods_min = 1,
	.periods_max = 128,
};

static const struct snd_pcm_hardware snd_bcm2835_playback_spdif_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_SYNC_APPLPTR),
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_44100 |
	SNDRV_PCM_RATE_48000,
	.rate_min = 44100,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = 128 * 1024,
	.period_bytes_min = 1 * 1024,
	.period_bytes_max = 128 * 1024,
	.periods_min = 1,
	.periods_max = 128,
};

static void snd_bcm2835_playback_free(struct snd_pcm_runtime *runtime)
{
	kfree(runtime->private_data);
}

void bcm2835_playback_fifo(struct bcm2835_alsa_stream *alsa_stream,
			   unsigned int bytes)
{
	struct snd_pcm_substream *substream = alsa_stream->substream;
	unsigned int pos;

	if (!alsa_stream->period_size)
		return;

	if (bytes >= alsa_stream->buffer_size) {
		snd_pcm_stream_lock(substream);
		snd_pcm_stop(substream,
			     alsa_stream->draining ?
			     SNDRV_PCM_STATE_SETUP :
			     SNDRV_PCM_STATE_XRUN);
		snd_pcm_stream_unlock(substream);
		return;
	}

	pos = atomic_read(&alsa_stream->pos);
	pos += bytes;
	pos %= alsa_stream->buffer_size;
	atomic_set(&alsa_stream->pos, pos);

	alsa_stream->period_offset += bytes;
	alsa_stream->interpolate_start = ktime_get();
	if (alsa_stream->period_offset >= alsa_stream->period_size) {
		alsa_stream->period_offset %= alsa_stream->period_size;
		snd_pcm_period_elapsed(substream);
	}
}

/* open callback */
static int snd_bcm2835_playback_open_generic(
	struct snd_pcm_substream *substream, int spdif)
{
	struct bcm2835_chip *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream;
	int idx;
	int err;

	mutex_lock(&chip->audio_mutex);
	idx = substream->number;

	if (spdif && chip->opened) {
		err = -EBUSY;
		goto out;
	} else if (!spdif && (chip->opened & (1 << idx))) {
		err = -EBUSY;
		goto out;
	}
	if (idx >= MAX_SUBSTREAMS) {
		dev_err(chip->dev,
			"substream(%d) device doesn't exist max(%d) substreams allowed\n",
			idx, MAX_SUBSTREAMS);
		err = -ENODEV;
		goto out;
	}

	alsa_stream = kzalloc(sizeof(*alsa_stream), GFP_KERNEL);
	if (!alsa_stream) {
		err = -ENOMEM;
		goto out;
	}

	/* Initialise alsa_stream */
	alsa_stream->chip = chip;
	alsa_stream->substream = substream;
	alsa_stream->idx = idx;

	err = bcm2835_audio_open(alsa_stream);
	if (err) {
		kfree(alsa_stream);
		goto out;
	}
	runtime->private_data = alsa_stream;
	runtime->private_free = snd_bcm2835_playback_free;
	if (spdif) {
		runtime->hw = snd_bcm2835_playback_spdif_hw;
	} else {
		/* clear spdif status, as we are not in spdif mode */
		chip->spdif_status = 0;
		runtime->hw = snd_bcm2835_playback_hw;
	}
	/* minimum 16 bytes alignment (for vchiq bulk transfers) */
	snd_pcm_hw_constraint_step(runtime,
				   0,
				   SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   16);

	/* position update is in 10ms order */
	snd_pcm_hw_constraint_minmax(runtime,
				     SNDRV_PCM_HW_PARAM_PERIOD_TIME,
				     10 * 1000, UINT_MAX);

	chip->alsa_stream[idx] = alsa_stream;

	chip->opened |= (1 << idx);

out:
	mutex_unlock(&chip->audio_mutex);

	return err;
}

static int snd_bcm2835_playback_open(struct snd_pcm_substream *substream)
{
	return snd_bcm2835_playback_open_generic(substream, 0);
}

static int snd_bcm2835_playback_spdif_open(struct snd_pcm_substream *substream)
{
	return snd_bcm2835_playback_open_generic(substream, 1);
}

static int snd_bcm2835_playback_close(struct snd_pcm_substream *substream)
{
	struct bcm2835_alsa_stream *alsa_stream;
	struct snd_pcm_runtime *runtime;
	struct bcm2835_chip *chip;

	chip = snd_pcm_substream_chip(substream);
	mutex_lock(&chip->audio_mutex);
	runtime = substream->runtime;
	alsa_stream = runtime->private_data;

	alsa_stream->period_size = 0;
	alsa_stream->buffer_size = 0;

	bcm2835_audio_close(alsa_stream);
	alsa_stream->chip->alsa_stream[alsa_stream->idx] = NULL;
	/*
	 * Do not free up alsa_stream here, it will be freed up by
	 * runtime->private_free callback we registered in *_open above
	 */

	chip->opened &= ~(1 << substream->number);

	mutex_unlock(&chip->audio_mutex);

	return 0;
}

static int snd_bcm2835_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct bcm2835_chip *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;
	int channels;
	int err;

	/* notify the vchiq that it should enter spdif passthrough mode by
	 * setting channels=0 (see
	 * https://github.com/raspberrypi/linux/issues/528)
	 */
	if (chip->spdif_status & IEC958_AES0_NONAUDIO)
		channels = 0;
	else
		channels = runtime->channels;

	err = bcm2835_audio_set_params(alsa_stream, channels,
				       runtime->rate,
				       snd_pcm_format_width(runtime->format));
	if (err < 0)
		return err;

	memset(&alsa_stream->pcm_indirect, 0, sizeof(alsa_stream->pcm_indirect));

	alsa_stream->pcm_indirect.hw_buffer_size =
		alsa_stream->pcm_indirect.sw_buffer_size =
		snd_pcm_lib_buffer_bytes(substream);

	alsa_stream->buffer_size = snd_pcm_lib_buffer_bytes(substream);
	alsa_stream->period_size = snd_pcm_lib_period_bytes(substream);
	atomic_set(&alsa_stream->pos, 0);
	alsa_stream->period_offset = 0;
	alsa_stream->draining = false;
	alsa_stream->interpolate_start = ktime_get();

	return 0;
}

static void snd_bcm2835_pcm_transfer(struct snd_pcm_substream *substream,
				     struct snd_pcm_indirect *rec, size_t bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;
	void *src = (void *) (substream->runtime->dma_area + rec->sw_data);

	bcm2835_audio_write(alsa_stream, bytes, src);
}

static int snd_bcm2835_pcm_ack(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;
	struct snd_pcm_indirect *pcm_indirect = &alsa_stream->pcm_indirect;

	return snd_pcm_indirect_playback_transfer(substream, pcm_indirect,
						  snd_bcm2835_pcm_transfer);
}

/* trigger callback */
static int snd_bcm2835_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		return bcm2835_audio_start(alsa_stream);
	case SNDRV_PCM_TRIGGER_DRAIN:
		alsa_stream->draining = true;
		return bcm2835_audio_drain(alsa_stream);
	case SNDRV_PCM_TRIGGER_STOP:
		return bcm2835_audio_stop(alsa_stream);
	default:
		return -EINVAL;
	}
}

/* pointer callback */
static snd_pcm_uframes_t
snd_bcm2835_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;
	ktime_t now = ktime_get();

	/* Give userspace better delay reporting by interpolating between GPU
	 * notifications, assuming audio speed is close enough to the clock
	 * used for ktime
	 */

	if ((ktime_to_ns(alsa_stream->interpolate_start)) &&
	    (ktime_compare(alsa_stream->interpolate_start, now) < 0)) {
		u64 interval =
			(ktime_to_ns(ktime_sub(now,
				alsa_stream->interpolate_start)));
		u64 frames_output_in_interval =
			div_u64((interval * runtime->rate), 1000000000);
		snd_pcm_sframes_t frames_output_in_interval_sized =
			-frames_output_in_interval;
		runtime->delay = frames_output_in_interval_sized;
	}

	return snd_pcm_indirect_playback_pointer(substream,
		&alsa_stream->pcm_indirect,
		atomic_read(&alsa_stream->pos));
}

/* operators */
static const struct snd_pcm_ops snd_bcm2835_playback_ops = {
	.open = snd_bcm2835_playback_open,
	.close = snd_bcm2835_playback_close,
	.prepare = snd_bcm2835_pcm_prepare,
	.trigger = snd_bcm2835_pcm_trigger,
	.pointer = snd_bcm2835_pcm_pointer,
	.ack = snd_bcm2835_pcm_ack,
};

static const struct snd_pcm_ops snd_bcm2835_playback_spdif_ops = {
	.open = snd_bcm2835_playback_spdif_open,
	.close = snd_bcm2835_playback_close,
	.prepare = snd_bcm2835_pcm_prepare,
	.trigger = snd_bcm2835_pcm_trigger,
	.pointer = snd_bcm2835_pcm_pointer,
	.ack = snd_bcm2835_pcm_ack,
};

/* create a pcm device */
int snd_bcm2835_new_pcm(struct bcm2835_chip *chip, const char *name,
			int idx, enum snd_bcm2835_route route,
			u32 numchannels, bool spdif)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(chip->card, name, idx, numchannels, 0, &pcm);
	if (err)
		return err;

	pcm->private_data = chip;
	pcm->nonatomic = true;
	strscpy(pcm->name, name, sizeof(pcm->name));
	if (!spdif) {
		chip->dest = route;
		chip->volume = 0;
		chip->mute = CTRL_VOL_UNMUTE;
	}

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			spdif ? &snd_bcm2835_playback_spdif_ops :
			&snd_bcm2835_playback_ops);

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
				       chip->card->dev, 128 * 1024, 128 * 1024);

	if (spdif)
		chip->pcm_spdif = pcm;
	else
		chip->pcm = pcm;
	return 0;
}
