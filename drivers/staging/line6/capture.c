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

#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "audio.h"
#include "capture.h"
#include "driver.h"
#include "pcm.h"
#include "pod.h"

/*
	Find a free URB and submit it.
*/
static int submit_audio_in_urb(struct snd_line6_pcm *line6pcm)
{
	int index;
	unsigned long flags;
	int i, urb_size;
	int ret;
	struct urb *urb_in;

	spin_lock_irqsave(&line6pcm->lock_audio_in, flags);
	index =
	    find_first_zero_bit(&line6pcm->active_urb_in, LINE6_ISO_BUFFERS);

	if (index < 0 || index >= LINE6_ISO_BUFFERS) {
		spin_unlock_irqrestore(&line6pcm->lock_audio_in, flags);
		dev_err(line6pcm->line6->ifcdev, "no free URB found\n");
		return -EINVAL;
	}

	urb_in = line6pcm->urb_audio_in[index];
	urb_size = 0;

	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		struct usb_iso_packet_descriptor *fin =
		    &urb_in->iso_frame_desc[i];
		fin->offset = urb_size;
		fin->length = line6pcm->max_packet_size;
		urb_size += line6pcm->max_packet_size;
	}

	urb_in->transfer_buffer =
	    line6pcm->buffer_in +
	    index * LINE6_ISO_PACKETS * line6pcm->max_packet_size;
	urb_in->transfer_buffer_length = urb_size;
	urb_in->context = line6pcm;

	ret = usb_submit_urb(urb_in, GFP_ATOMIC);

	if (ret == 0)
		set_bit(index, &line6pcm->active_urb_in);
	else
		dev_err(line6pcm->line6->ifcdev,
			"URB in #%d submission failed (%d)\n", index, ret);

	spin_unlock_irqrestore(&line6pcm->lock_audio_in, flags);
	return 0;
}

/*
	Submit all currently available capture URBs.
*/
int line6_submit_audio_in_all_urbs(struct snd_line6_pcm *line6pcm)
{
	int ret, i;

	for (i = 0; i < LINE6_ISO_BUFFERS; ++i) {
		ret = submit_audio_in_urb(line6pcm);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
	Unlink all currently active capture URBs.
*/
void line6_unlink_audio_in_urbs(struct snd_line6_pcm *line6pcm)
{
	unsigned int i;

	for (i = LINE6_ISO_BUFFERS; i--;) {
		if (test_bit(i, &line6pcm->active_urb_in)) {
			if (!test_and_set_bit(i, &line6pcm->unlink_urb_in)) {
				struct urb *u = line6pcm->urb_audio_in[i];
				usb_unlink_urb(u);
			}
		}
	}
}

/*
	Wait until unlinking of all currently active capture URBs has been
	finished.
*/
static void wait_clear_audio_in_urbs(struct snd_line6_pcm *line6pcm)
{
	int timeout = HZ;
	unsigned int i;
	int alive;

	do {
		alive = 0;
		for (i = LINE6_ISO_BUFFERS; i--;) {
			if (test_bit(i, &line6pcm->active_urb_in))
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
	Unlink all currently active capture URBs, and wait for finishing.
*/
void line6_unlink_wait_clear_audio_in_urbs(struct snd_line6_pcm *line6pcm)
{
	line6_unlink_audio_in_urbs(line6pcm);
	wait_clear_audio_in_urbs(line6pcm);
}

/*
	Copy data into ALSA capture buffer.
*/
void line6_capture_copy(struct snd_line6_pcm *line6pcm, char *fbuf, int fsize)
{
	struct snd_pcm_substream *substream =
	    get_substream(line6pcm, SNDRV_PCM_STREAM_CAPTURE);
	struct snd_pcm_runtime *runtime = substream->runtime;
	const int bytes_per_frame = line6pcm->properties->bytes_per_frame;
	int frames = fsize / bytes_per_frame;

	if (runtime == NULL)
		return;

	if (line6pcm->pos_in_done + frames > runtime->buffer_size) {
		/*
		   The transferred area goes over buffer boundary,
		   copy two separate chunks.
		 */
		int len;
		len = runtime->buffer_size - line6pcm->pos_in_done;

		if (len > 0) {
			memcpy(runtime->dma_area +
			       line6pcm->pos_in_done * bytes_per_frame, fbuf,
			       len * bytes_per_frame);
			memcpy(runtime->dma_area, fbuf + len * bytes_per_frame,
			       (frames - len) * bytes_per_frame);
		} else {
			/* this is somewhat paranoid */
			dev_err(line6pcm->line6->ifcdev,
				"driver bug: len = %d\n", len);
		}
	} else {
		/* copy single chunk */
		memcpy(runtime->dma_area +
		       line6pcm->pos_in_done * bytes_per_frame, fbuf, fsize);
	}

	line6pcm->pos_in_done += frames;
	if (line6pcm->pos_in_done >= runtime->buffer_size)
		line6pcm->pos_in_done -= runtime->buffer_size;
}

void line6_capture_check_period(struct snd_line6_pcm *line6pcm, int length)
{
	struct snd_pcm_substream *substream =
	    get_substream(line6pcm, SNDRV_PCM_STREAM_CAPTURE);

	line6pcm->bytes_in += length;
	if (line6pcm->bytes_in >= line6pcm->period_in) {
		line6pcm->bytes_in %= line6pcm->period_in;
		snd_pcm_period_elapsed(substream);
	}
}

int line6_alloc_capture_buffer(struct snd_line6_pcm *line6pcm)
{
	/* We may be invoked multiple times in a row so allocate once only */
	if (line6pcm->buffer_in)
		return 0;

	line6pcm->buffer_in =
		kmalloc(LINE6_ISO_BUFFERS * LINE6_ISO_PACKETS *
			line6pcm->max_packet_size, GFP_KERNEL);

	if (!line6pcm->buffer_in) {
		dev_err(line6pcm->line6->ifcdev,
			"cannot malloc capture buffer\n");
		return -ENOMEM;
	}

	return 0;
}

void line6_free_capture_buffer(struct snd_line6_pcm *line6pcm)
{
	kfree(line6pcm->buffer_in);
	line6pcm->buffer_in = NULL;
}

/*
 * Callback for completed capture URB.
 */
static void audio_in_callback(struct urb *urb)
{
	int i, index, length = 0, shutdown = 0;
	unsigned long flags;

	struct snd_line6_pcm *line6pcm = (struct snd_line6_pcm *)urb->context;

	line6pcm->last_frame_in = urb->start_frame;

	/* find index of URB */
	for (index = 0; index < LINE6_ISO_BUFFERS; ++index)
		if (urb == line6pcm->urb_audio_in[index])
			break;

#ifdef CONFIG_LINE6_USB_DUMP_PCM
	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		struct usb_iso_packet_descriptor *fout =
		    &urb->iso_frame_desc[i];
		line6_write_hexdump(line6pcm->line6, 'C',
				    urb->transfer_buffer + fout->offset,
				    fout->length);
	}
#endif

	spin_lock_irqsave(&line6pcm->lock_audio_in, flags);

	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		char *fbuf;
		int fsize;
		struct usb_iso_packet_descriptor *fin = &urb->iso_frame_desc[i];

		if (fin->status == -EXDEV) {
			shutdown = 1;
			break;
		}

		fbuf = urb->transfer_buffer + fin->offset;
		fsize = fin->actual_length;

		if (fsize > line6pcm->max_packet_size) {
			dev_err(line6pcm->line6->ifcdev,
				"driver and/or device bug: packet too large (%d > %d)\n",
				fsize, line6pcm->max_packet_size);
		}

		length += fsize;

		/* the following assumes LINE6_ISO_PACKETS == 1: */
		line6pcm->prev_fbuf = fbuf;
		line6pcm->prev_fsize = fsize;

#ifdef CONFIG_LINE6_USB_IMPULSE_RESPONSE
		if (!(line6pcm->flags & MASK_PCM_IMPULSE))
#endif
			if (test_bit(BIT_PCM_ALSA_CAPTURE, &line6pcm->flags)
			    && (fsize > 0))
				line6_capture_copy(line6pcm, fbuf, fsize);
	}

	clear_bit(index, &line6pcm->active_urb_in);

	if (test_and_clear_bit(index, &line6pcm->unlink_urb_in))
		shutdown = 1;

	spin_unlock_irqrestore(&line6pcm->lock_audio_in, flags);

	if (!shutdown) {
		submit_audio_in_urb(line6pcm);

#ifdef CONFIG_LINE6_USB_IMPULSE_RESPONSE
		if (!(line6pcm->flags & MASK_PCM_IMPULSE))
#endif
			if (test_bit(BIT_PCM_ALSA_CAPTURE, &line6pcm->flags))
				line6_capture_check_period(line6pcm, length);
	}
}

/* open capture callback */
static int snd_line6_capture_open(struct snd_pcm_substream *substream)
{
	int err;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);

	err = snd_pcm_hw_constraint_ratdens(runtime, 0,
					    SNDRV_PCM_HW_PARAM_RATE,
					    (&line6pcm->
					     properties->snd_line6_rates));
	if (err < 0)
		return err;

	runtime->hw = line6pcm->properties->snd_line6_capture_hw;
	return 0;
}

/* close capture callback */
static int snd_line6_capture_close(struct snd_pcm_substream *substream)
{
	return 0;
}

/* hw_params capture callback */
static int snd_line6_capture_hw_params(struct snd_pcm_substream *substream,
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

	if ((line6pcm->flags & MASK_CAPTURE) == 0) {
		ret = line6_alloc_capture_buffer(line6pcm);

		if (ret < 0)
			return ret;
	}

	ret = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	if (ret < 0)
		return ret;

	line6pcm->period_in = params_period_bytes(hw_params);
	return 0;
}

/* hw_free capture callback */
static int snd_line6_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);

	if ((line6pcm->flags & MASK_CAPTURE) == 0) {
		line6_unlink_wait_clear_audio_in_urbs(line6pcm);
		line6_free_capture_buffer(line6pcm);
	}

	return snd_pcm_lib_free_pages(substream);
}

/* trigger callback */
int snd_line6_capture_trigger(struct snd_line6_pcm *line6pcm, int cmd)
{
	int err;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
#ifdef CONFIG_PM
	case SNDRV_PCM_TRIGGER_RESUME:
#endif
		err = line6_pcm_start(line6pcm, MASK_PCM_ALSA_CAPTURE);

		if (err < 0)
			return err;

		break;

	case SNDRV_PCM_TRIGGER_STOP:
#ifdef CONFIG_PM
	case SNDRV_PCM_TRIGGER_SUSPEND:
#endif
		err = line6_pcm_stop(line6pcm, MASK_PCM_ALSA_CAPTURE);

		if (err < 0)
			return err;

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* capture pointer callback */
static snd_pcm_uframes_t
snd_line6_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	return line6pcm->pos_in_done;
}

/* capture operators */
struct snd_pcm_ops snd_line6_capture_ops = {
	.open = snd_line6_capture_open,
	.close = snd_line6_capture_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_line6_capture_hw_params,
	.hw_free = snd_line6_capture_hw_free,
	.prepare = snd_line6_prepare,
	.trigger = snd_line6_trigger,
	.pointer = snd_line6_capture_pointer,
};

int line6_create_audio_in_urbs(struct snd_line6_pcm *line6pcm)
{
	int i;

	/* create audio URBs and fill in constant values: */
	for (i = 0; i < LINE6_ISO_BUFFERS; ++i) {
		struct urb *urb;

		/* URB for audio in: */
		urb = line6pcm->urb_audio_in[i] =
		    usb_alloc_urb(LINE6_ISO_PACKETS, GFP_KERNEL);

		if (urb == NULL) {
			dev_err(line6pcm->line6->ifcdev, "Out of memory\n");
			return -ENOMEM;
		}

		urb->dev = line6pcm->line6->usbdev;
		urb->pipe =
		    usb_rcvisocpipe(line6pcm->line6->usbdev,
				    line6pcm->ep_audio_read &
				    USB_ENDPOINT_NUMBER_MASK);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->start_frame = -1;
		urb->number_of_packets = LINE6_ISO_PACKETS;
		urb->interval = LINE6_ISO_INTERVAL;
		urb->error_count = 0;
		urb->complete = audio_in_callback;
	}

	return 0;
}
