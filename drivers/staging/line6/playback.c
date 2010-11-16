/*
 * Line6 Linux USB driver - 0.9.1beta
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "audio.h"
#include "capture.h"
#include "driver.h"
#include "pcm.h"
#include "pod.h"
#include "playback.h"

/*
	Software stereo volume control.
*/
static void change_volume(struct urb *urb_out, int volume[],
			  int bytes_per_frame)
{
	int chn = 0;

	if (volume[0] == 256 && volume[1] == 256)
		return;		/* maximum volume - no change */

	if (bytes_per_frame == 4) {
		short *p, *buf_end;
		p = (short *)urb_out->transfer_buffer;
		buf_end = p + urb_out->transfer_buffer_length / sizeof(*p);

		for (; p < buf_end; ++p) {
			*p = (*p * volume[chn & 1]) >> 8;
			++chn;
		}
	} else if (bytes_per_frame == 6) {
		unsigned char *p, *buf_end;
		p = (unsigned char *)urb_out->transfer_buffer;
		buf_end = p + urb_out->transfer_buffer_length;

		for (; p < buf_end; p += 3) {
			int val;
			val = p[0] + (p[1] << 8) + ((signed char)p[2] << 16);
			val = (val * volume[chn & 1]) >> 8;
			p[0] = val;
			p[1] = val >> 8;
			p[2] = val >> 16;
			++chn;
		}
	}
}

#ifdef CONFIG_LINE6_USB_IMPULSE_RESPONSE

/*
	Create signal for impulse response test.
*/
static void create_impulse_test_signal(struct snd_line6_pcm *line6pcm,
				       struct urb *urb_out, int bytes_per_frame)
{
	int frames = urb_out->transfer_buffer_length / bytes_per_frame;

	if (bytes_per_frame == 4) {
		int i;
		short *pi = (short *)line6pcm->prev_fbuf;
		short *po = (short *)urb_out->transfer_buffer;

		for (i = 0; i < frames; ++i) {
			po[0] = pi[0];
			po[1] = 0;
			pi += 2;
			po += 2;
		}
	} else if (bytes_per_frame == 6) {
		int i, j;
		unsigned char *pi = line6pcm->prev_fbuf;
		unsigned char *po = urb_out->transfer_buffer;

		for (i = 0; i < frames; ++i) {
			for (j = 0; j < bytes_per_frame / 2; ++j)
				po[j] = pi[j];

			for (; j < bytes_per_frame; ++j)
				po[j] = 0;

			pi += bytes_per_frame;
			po += bytes_per_frame;
		}
	}
	if (--line6pcm->impulse_count <= 0) {
		((unsigned char *)(urb_out->transfer_buffer))[bytes_per_frame -
							      1] =
		    line6pcm->impulse_volume;
		line6pcm->impulse_count = line6pcm->impulse_period;
	}
}

#endif

/*
	Add signal to buffer for software monitoring.
*/
static void add_monitor_signal(struct urb *urb_out, unsigned char *signal,
			       int volume, int bytes_per_frame)
{
	if (volume == 0)
		return;		/* zero volume - no change */

	if (bytes_per_frame == 4) {
		short *pi, *po, *buf_end;
		pi = (short *)signal;
		po = (short *)urb_out->transfer_buffer;
		buf_end = po + urb_out->transfer_buffer_length / sizeof(*po);

		for (; po < buf_end; ++pi, ++po)
			*po += (*pi * volume) >> 8;
	}

	/*
	   We don't need to handle devices with 6 bytes per frame here
	   since they all support hardware monitoring.
	 */
}

/*
	Find a free URB, prepare audio data, and submit URB.
*/
static int submit_audio_out_urb(struct snd_line6_pcm *line6pcm)
{
	int index;
	unsigned long flags;
	int i, urb_size, urb_frames;
	int ret;
	const int bytes_per_frame = line6pcm->properties->bytes_per_frame;
	const int frame_increment =
	    line6pcm->properties->snd_line6_rates.rats[0].num_min;
	const int frame_factor =
	    line6pcm->properties->snd_line6_rates.rats[0].den *
	    (USB_INTERVALS_PER_SECOND / LINE6_ISO_INTERVAL);
	struct urb *urb_out;

	spin_lock_irqsave(&line6pcm->lock_audio_out, flags);
	index =
	    find_first_zero_bit(&line6pcm->active_urb_out, LINE6_ISO_BUFFERS);

	if (index < 0 || index >= LINE6_ISO_BUFFERS) {
		spin_unlock_irqrestore(&line6pcm->lock_audio_out, flags);
		dev_err(line6pcm->line6->ifcdev, "no free URB found\n");
		return -EINVAL;
	}

	urb_out = line6pcm->urb_audio_out[index];
	urb_size = 0;

	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		/* compute frame size for given sampling rate */
		int fsize = 0;
		struct usb_iso_packet_descriptor *fout =
		    &urb_out->iso_frame_desc[i];

		if (line6pcm->flags & MASK_CAPTURE)
			fsize = line6pcm->prev_fsize;

		if (fsize == 0) {
			int n;
			line6pcm->count_out += frame_increment;
			n = line6pcm->count_out / frame_factor;
			line6pcm->count_out -= n * frame_factor;
			fsize = n * bytes_per_frame;
		}

		fout->offset = urb_size;
		fout->length = fsize;
		urb_size += fsize;
	}

	if (urb_size == 0) {
		/* can't determine URB size */
		spin_unlock_irqrestore(&line6pcm->lock_audio_out, flags);
		dev_err(line6pcm->line6->ifcdev, "driver bug: urb_size = 0\n");	/* this is somewhat paranoid */
		return -EINVAL;
	}

	urb_frames = urb_size / bytes_per_frame;
	urb_out->transfer_buffer =
	    line6pcm->buffer_out +
	    line6pcm->max_packet_size * line6pcm->index_out;
	urb_out->transfer_buffer_length = urb_size;
	urb_out->context = line6pcm;

	if (++line6pcm->index_out == LINE6_ISO_BUFFERS)
		line6pcm->index_out = 0;

	if (test_bit(BIT_PCM_ALSA_PLAYBACK, &line6pcm->flags) &&
	    !test_bit(BIT_PAUSE_PLAYBACK, &line6pcm->flags)) {
		struct snd_pcm_runtime *runtime =
		    get_substream(line6pcm, SNDRV_PCM_STREAM_PLAYBACK)->runtime;

		if (line6pcm->pos_out + urb_frames > runtime->buffer_size) {
			/*
			   The transferred area goes over buffer boundary,
			   copy the data to the temp buffer.
			 */
			int len;
			len = runtime->buffer_size - line6pcm->pos_out;

			if (len > 0) {
				memcpy(urb_out->transfer_buffer,
				       runtime->dma_area +
				       line6pcm->pos_out * bytes_per_frame,
				       len * bytes_per_frame);
				memcpy(urb_out->transfer_buffer +
				       len * bytes_per_frame, runtime->dma_area,
				       (urb_frames - len) * bytes_per_frame);
			} else
				dev_err(line6pcm->line6->ifcdev, "driver bug: len = %d\n", len);	/* this is somewhat paranoid */
		} else {
#if LINE6_REUSE_DMA_AREA_FOR_PLAYBACK
			/* set the buffer pointer */
			urb_out->transfer_buffer =
			    runtime->dma_area +
			    line6pcm->pos_out * bytes_per_frame;
#else
			/* copy data */
			memcpy(urb_out->transfer_buffer,
			       runtime->dma_area +
			       line6pcm->pos_out * bytes_per_frame,
			       urb_out->transfer_buffer_length);
#endif
		}

		line6pcm->pos_out += urb_frames;
		if (line6pcm->pos_out >= runtime->buffer_size)
			line6pcm->pos_out -= runtime->buffer_size;
	} else {
		memset(urb_out->transfer_buffer, 0,
		       urb_out->transfer_buffer_length);
	}

	change_volume(urb_out, line6pcm->volume_playback, bytes_per_frame);

	if (line6pcm->prev_fbuf != 0) {
#ifdef CONFIG_LINE6_USB_IMPULSE_RESPONSE
		if (line6pcm->flags & MASK_PCM_IMPULSE) {
			create_impulse_test_signal(line6pcm, urb_out,
						   bytes_per_frame);
			if (line6pcm->flags & MASK_PCM_ALSA_CAPTURE) {
				line6_capture_copy(line6pcm,
						   urb_out->transfer_buffer,
						   urb_out->
						   transfer_buffer_length);
				line6_capture_check_period(line6pcm,
							   urb_out->transfer_buffer_length);
			}
		} else {
#endif
			if (!
			    (line6pcm->line6->
			     properties->capabilities & LINE6_BIT_HWMON)
&& (line6pcm->flags & MASK_PLAYBACK)
&& (line6pcm->flags & MASK_CAPTURE))
				add_monitor_signal(urb_out, line6pcm->prev_fbuf,
						   line6pcm->volume_monitor,
						   bytes_per_frame);
#ifdef CONFIG_LINE6_USB_IMPULSE_RESPONSE
		}
#endif
	}
#ifdef CONFIG_LINE6_USB_DUMP_PCM
	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		struct usb_iso_packet_descriptor *fout =
		    &urb_out->iso_frame_desc[i];
		line6_write_hexdump(line6pcm->line6, 'P',
				    urb_out->transfer_buffer + fout->offset,
				    fout->length);
	}
#endif

	ret = usb_submit_urb(urb_out, GFP_ATOMIC);

	if (ret == 0)
		set_bit(index, &line6pcm->active_urb_out);
	else
		dev_err(line6pcm->line6->ifcdev,
			"URB out #%d submission failed (%d)\n", index, ret);

	spin_unlock_irqrestore(&line6pcm->lock_audio_out, flags);
	return 0;
}

/*
	Submit all currently available playback URBs.
*/
int line6_submit_audio_out_all_urbs(struct snd_line6_pcm *line6pcm)
{
	int ret, i;

	for (i = 0; i < LINE6_ISO_BUFFERS; ++i) {
		ret = submit_audio_out_urb(line6pcm);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
	Unlink all currently active playback URBs.
*/
void line6_unlink_audio_out_urbs(struct snd_line6_pcm *line6pcm)
{
	unsigned int i;

	for (i = LINE6_ISO_BUFFERS; i--;) {
		if (test_bit(i, &line6pcm->active_urb_out)) {
			if (!test_and_set_bit(i, &line6pcm->unlink_urb_out)) {
				struct urb *u = line6pcm->urb_audio_out[i];
				usb_unlink_urb(u);
			}
		}
	}
}

/*
	Wait until unlinking of all currently active playback URBs has been finished.
*/
static void wait_clear_audio_out_urbs(struct snd_line6_pcm *line6pcm)
{
	int timeout = HZ;
	unsigned int i;
	int alive;

	do {
		alive = 0;
		for (i = LINE6_ISO_BUFFERS; i--;) {
			if (test_bit(i, &line6pcm->active_urb_out))
				alive++;
		}
		if (!alive)
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (--timeout > 0);
	if (alive)
		snd_printk(KERN_ERR "timeout: still %d active urbs..\n", alive);
}

/*
	Unlink all currently active playback URBs, and wait for finishing.
*/
void line6_unlink_wait_clear_audio_out_urbs(struct snd_line6_pcm *line6pcm)
{
	line6_unlink_audio_out_urbs(line6pcm);
	wait_clear_audio_out_urbs(line6pcm);
}

/*
	Callback for completed playback URB.
*/
static void audio_out_callback(struct urb *urb)
{
	int i, index, length = 0, shutdown = 0;
	unsigned long flags;

	struct snd_line6_pcm *line6pcm = (struct snd_line6_pcm *)urb->context;
	struct snd_pcm_substream *substream =
	    get_substream(line6pcm, SNDRV_PCM_STREAM_PLAYBACK);

#if USE_CLEAR_BUFFER_WORKAROUND
	memset(urb->transfer_buffer, 0, urb->transfer_buffer_length);
#endif

	line6pcm->last_frame_out = urb->start_frame;

	/* find index of URB */
	for (index = LINE6_ISO_BUFFERS; index--;)
		if (urb == line6pcm->urb_audio_out[index])
			break;

	if (index < 0)
		return;		/* URB has been unlinked asynchronously */

	for (i = LINE6_ISO_PACKETS; i--;)
		length += urb->iso_frame_desc[i].length;

	spin_lock_irqsave(&line6pcm->lock_audio_out, flags);

	if (test_bit(BIT_PCM_ALSA_PLAYBACK, &line6pcm->flags)) {
		struct snd_pcm_runtime *runtime = substream->runtime;
		line6pcm->pos_out_done +=
		    length / line6pcm->properties->bytes_per_frame;

		if (line6pcm->pos_out_done >= runtime->buffer_size)
			line6pcm->pos_out_done -= runtime->buffer_size;
	}

	clear_bit(index, &line6pcm->active_urb_out);

	for (i = LINE6_ISO_PACKETS; i--;)
		if (urb->iso_frame_desc[i].status == -EXDEV) {
			shutdown = 1;
			break;
		}

	if (test_and_clear_bit(index, &line6pcm->unlink_urb_out))
		shutdown = 1;

	spin_unlock_irqrestore(&line6pcm->lock_audio_out, flags);

	if (!shutdown) {
		submit_audio_out_urb(line6pcm);

		if (test_bit(BIT_PCM_ALSA_PLAYBACK, &line6pcm->flags)) {
			line6pcm->bytes_out += length;
			if (line6pcm->bytes_out >= line6pcm->period_out) {
				line6pcm->bytes_out %= line6pcm->period_out;
				snd_pcm_period_elapsed(substream);
			}
		}
	}
}

/* open playback callback */
static int snd_line6_playback_open(struct snd_pcm_substream *substream)
{
	int err;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);

	err = snd_pcm_hw_constraint_ratdens(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					    (&line6pcm->
					     properties->snd_line6_rates));
	if (err < 0)
		return err;

	runtime->hw = line6pcm->properties->snd_line6_playback_hw;
	return 0;
}

/* close playback callback */
static int snd_line6_playback_close(struct snd_pcm_substream *substream)
{
	return 0;
}

/* hw_params playback callback */
static int snd_line6_playback_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	int ret;
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);

	/* -- Florian Demski [FD] */
	/* don't ask me why, but this fixes the bug on my machine */
	if (line6pcm == NULL) {
		if (substream->pcm == NULL)
			return -ENOMEM;
		if (substream->pcm->private_data == NULL)
			return -ENOMEM;
		substream->private_data = substream->pcm->private_data;
		line6pcm = snd_pcm_substream_chip(substream);
	}
	/* -- [FD] end */

	ret = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	if (ret < 0)
		return ret;

	line6pcm->period_out = params_period_bytes(hw_params);
	return 0;
}

/* hw_free playback callback */
static int snd_line6_playback_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/* trigger playback callback */
int snd_line6_playback_trigger(struct snd_line6_pcm *line6pcm, int cmd)
{
	int err;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
#ifdef CONFIG_PM
	case SNDRV_PCM_TRIGGER_RESUME:
#endif
		err = line6_pcm_start(line6pcm, MASK_PCM_ALSA_PLAYBACK);

		if (err < 0)
			return err;

		break;

	case SNDRV_PCM_TRIGGER_STOP:
#ifdef CONFIG_PM
	case SNDRV_PCM_TRIGGER_SUSPEND:
#endif
		err = line6_pcm_stop(line6pcm, MASK_PCM_ALSA_PLAYBACK);

		if (err < 0)
			return err;

		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		set_bit(BIT_PAUSE_PLAYBACK, &line6pcm->flags);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		clear_bit(BIT_PAUSE_PLAYBACK, &line6pcm->flags);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* playback pointer callback */
static snd_pcm_uframes_t
snd_line6_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	return line6pcm->pos_out_done;
}

/* playback operators */
struct snd_pcm_ops snd_line6_playback_ops = {
	.open = snd_line6_playback_open,
	.close = snd_line6_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_line6_playback_hw_params,
	.hw_free = snd_line6_playback_hw_free,
	.prepare = snd_line6_prepare,
	.trigger = snd_line6_trigger,
	.pointer = snd_line6_playback_pointer,
};

int line6_create_audio_out_urbs(struct snd_line6_pcm *line6pcm)
{
	int i;

	/* create audio URBs and fill in constant values: */
	for (i = 0; i < LINE6_ISO_BUFFERS; ++i) {
		struct urb *urb;

		/* URB for audio out: */
		urb = line6pcm->urb_audio_out[i] =
		    usb_alloc_urb(LINE6_ISO_PACKETS, GFP_KERNEL);

		if (urb == NULL) {
			dev_err(line6pcm->line6->ifcdev, "Out of memory\n");
			return -ENOMEM;
		}

		urb->dev = line6pcm->line6->usbdev;
		urb->pipe =
		    usb_sndisocpipe(line6pcm->line6->usbdev,
				    line6pcm->ep_audio_write &
				    USB_ENDPOINT_NUMBER_MASK);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->start_frame = -1;
		urb->number_of_packets = LINE6_ISO_PACKETS;
		urb->interval = LINE6_ISO_INTERVAL;
		urb->error_count = 0;
		urb->complete = audio_out_callback;
	}

	return 0;
}
