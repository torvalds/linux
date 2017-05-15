/*****************************************************************************
 * Copyright 2011 Broadcom Corporation.  All rights reserved.
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available at
 * http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *****************************************************************************/

#include <linux/interrupt.h>
#include <linux/slab.h>

#include <sound/asoundef.h>

#include "bcm2835.h"

/* hardware definition */
static struct snd_pcm_hardware snd_bcm2835_playback_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
	SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
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

static struct snd_pcm_hardware snd_bcm2835_playback_spdif_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
	SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
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
	audio_info("Freeing up alsa stream here ..\n");
	kfree(runtime->private_data);
	runtime->private_data = NULL;
}

void bcm2835_playback_fifo(struct bcm2835_alsa_stream *alsa_stream)
{
	unsigned int consumed = 0;
	int new_period = 0;


	audio_info("alsa_stream=%p substream=%p\n", alsa_stream,
		alsa_stream ? alsa_stream->substream : 0);

	if (alsa_stream->open)
		consumed = bcm2835_audio_retrieve_buffers(alsa_stream);

	/* We get called only if playback was triggered, So, the number of buffers we retrieve in
	 * each iteration are the buffers that have been played out already
	 */

	if (alsa_stream->period_size) {
		if ((alsa_stream->pos / alsa_stream->period_size) !=
			((alsa_stream->pos + consumed) / alsa_stream->period_size))
			new_period = 1;
	}
	audio_debug("updating pos cur: %d + %d max:%d period_bytes:%d, hw_ptr: %d new_period:%d\n",
		alsa_stream->pos,
		consumed,
		alsa_stream->buffer_size,
		(int) (alsa_stream->period_size * alsa_stream->substream->runtime->periods),
		frames_to_bytes(alsa_stream->substream->runtime, alsa_stream->substream->runtime->status->hw_ptr),
		new_period);
	if (alsa_stream->buffer_size) {
		alsa_stream->pos += consumed & ~(1 << 30);
		alsa_stream->pos %= alsa_stream->buffer_size;
	}

	if (alsa_stream->substream) {
		if (new_period)
			snd_pcm_period_elapsed(alsa_stream->substream);
	} else {
		audio_warning(" unexpected NULL substream\n");
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


	if (mutex_lock_interruptible(&chip->audio_mutex)) {
		audio_error("Interrupted whilst waiting for lock\n");
		return -EINTR;
	}
	audio_info("Alsa open (%d)\n", substream->number);
	idx = substream->number;

	if (spdif && chip->opened) {
		err = -EBUSY;
		goto out;
	} else if (!spdif && (chip->opened & (1 << idx))) {
		err = -EBUSY;
		goto out;
	}
	if (idx >= MAX_SUBSTREAMS) {
		audio_error
			("substream(%d) device doesn't exist max(%d) substreams allowed\n",
			idx, MAX_SUBSTREAMS);
		err = -ENODEV;
		goto out;
	}

	/* Check if we are ready */
	if (!(chip->avail_substreams & (1 << idx))) {
		/* We are not ready yet */
		audio_error("substream(%d) device is not ready yet\n", idx);
		err = -EAGAIN;
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

	spin_lock_init(&alsa_stream->lock);

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

	chip->alsa_stream[idx] = alsa_stream;

	chip->opened |= (1 << idx);
	alsa_stream->open = 1;
	alsa_stream->draining = 1;

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

/* close callback */
static int snd_bcm2835_playback_close(struct snd_pcm_substream *substream)
{
	/* the hardware-specific codes will be here */

	struct bcm2835_chip *chip;
	struct snd_pcm_runtime *runtime;
	struct bcm2835_alsa_stream *alsa_stream;


	chip = snd_pcm_substream_chip(substream);
	if (mutex_lock_interruptible(&chip->audio_mutex)) {
		audio_error("Interrupted whilst waiting for lock\n");
		return -EINTR;
	}
	runtime = substream->runtime;
	alsa_stream = runtime->private_data;

	audio_info("Alsa close\n");

	/*
	 * Call stop if it's still running. This happens when app
	 * is force killed and we don't get a stop trigger.
	 */
	if (alsa_stream->running) {
		int err;
		err = bcm2835_audio_stop(alsa_stream);
		alsa_stream->running = 0;
		if (err)
			audio_error(" Failed to STOP alsa device\n");
	}

	alsa_stream->period_size = 0;
	alsa_stream->buffer_size = 0;

	if (alsa_stream->open) {
		alsa_stream->open = 0;
		bcm2835_audio_close(alsa_stream);
	}
	if (alsa_stream->chip)
		alsa_stream->chip->alsa_stream[alsa_stream->idx] = NULL;
	/*
	 * Do not free up alsa_stream here, it will be freed up by
	 * runtime->private_free callback we registered in *_open above
	 */

	chip->opened &= ~(1 << substream->number);

	mutex_unlock(&chip->audio_mutex);

	return 0;
}

/* hw_params callback */
static int snd_bcm2835_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;
	int err;


	err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (err < 0) {
		audio_error
			(" pcm_lib_malloc failed to allocated pages for buffers\n");
		return err;
	}

	alsa_stream->channels = params_channels(params);
	alsa_stream->params_rate = params_rate(params);
	alsa_stream->pcm_format_width = snd_pcm_format_width(params_format(params));

	return err;
}

/* hw_free callback */
static int snd_bcm2835_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/* prepare callback */
static int snd_bcm2835_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct bcm2835_chip *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;
	int channels;
	int err;


	if (mutex_lock_interruptible(&chip->audio_mutex))
		return -EINTR;

	/* notify the vchiq that it should enter spdif passthrough mode by
	 * setting channels=0 (see
	 * https://github.com/raspberrypi/linux/issues/528) */
	if (chip->spdif_status & IEC958_AES0_NONAUDIO)
		channels = 0;
	else
		channels = alsa_stream->channels;

	err = bcm2835_audio_set_params(alsa_stream, channels,
		alsa_stream->params_rate,
		alsa_stream->pcm_format_width);
	if (err < 0)
		audio_error(" error setting hw params\n");


	bcm2835_audio_setup(alsa_stream);

	/* in preparation of the stream, set the controls (volume level) of the stream */
	bcm2835_audio_set_ctls(alsa_stream->chip);


	memset(&alsa_stream->pcm_indirect, 0, sizeof(alsa_stream->pcm_indirect));

	alsa_stream->pcm_indirect.hw_buffer_size =
		alsa_stream->pcm_indirect.sw_buffer_size =
		snd_pcm_lib_buffer_bytes(substream);

	alsa_stream->buffer_size = snd_pcm_lib_buffer_bytes(substream);
	alsa_stream->period_size = snd_pcm_lib_period_bytes(substream);
	alsa_stream->pos = 0;

	audio_debug("buffer_size=%d, period_size=%d pos=%d frame_bits=%d\n",
		alsa_stream->buffer_size, alsa_stream->period_size,
		alsa_stream->pos, runtime->frame_bits);

	mutex_unlock(&chip->audio_mutex);
	return 0;
}

static void snd_bcm2835_pcm_transfer(struct snd_pcm_substream *substream,
	struct snd_pcm_indirect *rec, size_t bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;
	void *src = (void *) (substream->runtime->dma_area + rec->sw_data);
	int err;

	err = bcm2835_audio_write(alsa_stream, bytes, src);
	if (err)
		audio_error(" Failed to transfer to alsa device (%d)\n", err);

}

static int snd_bcm2835_pcm_ack(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;
	struct snd_pcm_indirect *pcm_indirect = &alsa_stream->pcm_indirect;

	pcm_indirect->hw_queue_size = runtime->hw.buffer_bytes_max;
	snd_pcm_indirect_playback_transfer(substream, pcm_indirect,
					   snd_bcm2835_pcm_transfer);
	return 0;
}

/* trigger callback */
static int snd_bcm2835_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;
	int err = 0;


	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		audio_debug("bcm2835_AUDIO_TRIGGER_START running=%d\n",
			alsa_stream->running);
		if (!alsa_stream->running) {
			err = bcm2835_audio_start(alsa_stream);
			if (!err) {
				alsa_stream->pcm_indirect.hw_io =
					alsa_stream->pcm_indirect.hw_data =
					bytes_to_frames(runtime,
					alsa_stream->pos);
				substream->ops->ack(substream);
				alsa_stream->running = 1;
				alsa_stream->draining = 1;
			} else {
				audio_error(" Failed to START alsa device (%d)\n", err);
			}
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		audio_debug
			("bcm2835_AUDIO_TRIGGER_STOP running=%d draining=%d\n",
			alsa_stream->running, runtime->status->state == SNDRV_PCM_STATE_DRAINING);
		if (runtime->status->state == SNDRV_PCM_STATE_DRAINING) {
			audio_info("DRAINING\n");
			alsa_stream->draining = 1;
		} else {
			audio_info("DROPPING\n");
			alsa_stream->draining = 0;
		}
		if (alsa_stream->running) {
			err = bcm2835_audio_stop(alsa_stream);
			if (err != 0)
				audio_error(" Failed to STOP alsa device (%d)\n", err);
			alsa_stream->running = 0;
		}
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

/* pointer callback */
static snd_pcm_uframes_t
snd_bcm2835_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct bcm2835_alsa_stream *alsa_stream = runtime->private_data;


	audio_debug("pcm_pointer... (%d) hwptr=%d appl=%d pos=%d\n", 0,
		frames_to_bytes(runtime, runtime->status->hw_ptr),
		frames_to_bytes(runtime, runtime->control->appl_ptr),
		alsa_stream->pos);

	return snd_pcm_indirect_playback_pointer(substream,
		&alsa_stream->pcm_indirect,
		alsa_stream->pos);
}

static int snd_bcm2835_pcm_lib_ioctl(struct snd_pcm_substream *substream,
	unsigned int cmd, void *arg)
{
	int ret = snd_pcm_lib_ioctl(substream, cmd, arg);

	audio_info(" .. substream=%p, cmd=%d, arg=%p (%x) ret=%d\n", substream,
		cmd, arg, arg ? *(unsigned *) arg : 0, ret);
	return ret;
}

/* operators */
static struct snd_pcm_ops snd_bcm2835_playback_ops = {
	.open = snd_bcm2835_playback_open,
	.close = snd_bcm2835_playback_close,
	.ioctl = snd_bcm2835_pcm_lib_ioctl,
	.hw_params = snd_bcm2835_pcm_hw_params,
	.hw_free = snd_bcm2835_pcm_hw_free,
	.prepare = snd_bcm2835_pcm_prepare,
	.trigger = snd_bcm2835_pcm_trigger,
	.pointer = snd_bcm2835_pcm_pointer,
	.ack = snd_bcm2835_pcm_ack,
};

static struct snd_pcm_ops snd_bcm2835_playback_spdif_ops = {
	.open = snd_bcm2835_playback_spdif_open,
	.close = snd_bcm2835_playback_close,
	.ioctl = snd_bcm2835_pcm_lib_ioctl,
	.hw_params = snd_bcm2835_pcm_hw_params,
	.hw_free = snd_bcm2835_pcm_hw_free,
	.prepare = snd_bcm2835_pcm_prepare,
	.trigger = snd_bcm2835_pcm_trigger,
	.pointer = snd_bcm2835_pcm_pointer,
	.ack = snd_bcm2835_pcm_ack,
};

/* create a pcm device */
int snd_bcm2835_new_pcm(struct bcm2835_chip *chip, u32 numchannels)
{
	struct snd_pcm *pcm;
	int err;

	mutex_init(&chip->audio_mutex);
	if (mutex_lock_interruptible(&chip->audio_mutex)) {
		audio_error("Interrupted whilst waiting for lock\n");
		return -EINTR;
	}
	err = snd_pcm_new(chip->card, "bcm2835 ALSA", 0, numchannels, 0, &pcm);
	if (err < 0)
		goto out;
	pcm->private_data = chip;
	strcpy(pcm->name, "bcm2835 ALSA");
	chip->pcm = pcm;
	chip->dest = AUDIO_DEST_AUTO;
	chip->volume = alsa2chip(0);
	chip->mute = CTRL_VOL_UNMUTE; /*disable mute on startup */
	/* set operators */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_bcm2835_playback_ops);

	/* pre-allocation of buffers */
	/* NOTE: this may fail */
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
					      snd_dma_continuous_data(GFP_KERNEL),
					      snd_bcm2835_playback_hw.buffer_bytes_max,
					      snd_bcm2835_playback_hw.buffer_bytes_max);


out:
	mutex_unlock(&chip->audio_mutex);

	return 0;
}

int snd_bcm2835_new_spdif_pcm(struct bcm2835_chip *chip)
{
	struct snd_pcm *pcm;
	int err;

	if (mutex_lock_interruptible(&chip->audio_mutex)) {
		audio_error("Interrupted whilst waiting for lock\n");
		return -EINTR;
	}
	err = snd_pcm_new(chip->card, "bcm2835 ALSA", 1, 1, 0, &pcm);
	if (err < 0)
		goto out;

	pcm->private_data = chip;
	strcpy(pcm->name, "bcm2835 IEC958/HDMI");
	chip->pcm_spdif = pcm;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_bcm2835_playback_spdif_ops);

	/* pre-allocation of buffers */
	/* NOTE: this may fail */
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL),
		snd_bcm2835_playback_spdif_hw.buffer_bytes_max, snd_bcm2835_playback_spdif_hw.buffer_bytes_max);
out:
	mutex_unlock(&chip->audio_mutex);

	return 0;
}

int snd_bcm2835_new_simple_pcm(struct bcm2835_chip *chip,
			       const char *name,
			       enum snd_bcm2835_route route,
			       u32 numchannels)
{
	struct snd_pcm *pcm;
	int err;

	mutex_init(&chip->audio_mutex);

	err = snd_pcm_new(chip->card, name, 0, numchannels,
			  0, &pcm);
	if (err)
		return err;

	pcm->private_data = chip;
	strcpy(pcm->name, name);
	chip->pcm = pcm;
	chip->dest = route;
	chip->volume = alsa2chip(0);
	chip->mute = CTRL_VOL_UNMUTE;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_bcm2835_playback_ops);

	snd_pcm_lib_preallocate_pages_for_all(
		pcm,
		SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL),
		snd_bcm2835_playback_hw.buffer_bytes_max,
		snd_bcm2835_playback_hw.buffer_bytes_max);

	return 0;
}

