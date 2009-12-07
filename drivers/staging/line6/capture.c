/*
 * Line6 Linux USB driver - 0.8.0
 *
 * Copyright (C) 2004-2009 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include "driver.h"

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "audio.h"
#include "pcm.h"
#include "pod.h"
#include "capture.h"


/*
	Find a free URB and submit it.
*/
static int submit_audio_in_urb(struct snd_pcm_substream *substream)
{
	unsigned int index;
	unsigned long flags;
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	int i, urb_size;
	struct urb *urb_in;

	spin_lock_irqsave(&line6pcm->lock_audio_in, flags);
	index = find_first_zero_bit(&line6pcm->active_urb_in, LINE6_ISO_BUFFERS);

	if (index >= LINE6_ISO_BUFFERS) {
		spin_unlock_irqrestore(&line6pcm->lock_audio_in, flags);
		dev_err(s2m(substream), "no free URB found\n");
		return -EINVAL;
	}

	urb_in = line6pcm->urb_audio_in[index];
	urb_size = 0;

	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		struct usb_iso_packet_descriptor *fin = &urb_in->iso_frame_desc[i];
		fin->offset = urb_size;
		fin->length = line6pcm->max_packet_size;
		urb_size += line6pcm->max_packet_size;
	}

	urb_in->transfer_buffer = line6pcm->buffer_in + index * LINE6_ISO_PACKETS * line6pcm->max_packet_size;
	urb_in->transfer_buffer_length = urb_size;
	urb_in->context = substream;

	if (usb_submit_urb(urb_in, GFP_ATOMIC) == 0)
		set_bit(index, &line6pcm->active_urb_in);
	else
		dev_err(s2m(substream), "URB in #%d submission failed\n", index);

	spin_unlock_irqrestore(&line6pcm->lock_audio_in, flags);
	return 0;
}

/*
	Submit all currently available capture URBs.
*/
static int submit_audio_in_all_urbs(struct snd_pcm_substream *substream)
{
	int ret, i;

	for (i = 0; i < LINE6_ISO_BUFFERS; ++i) {
		ret = submit_audio_in_urb(substream);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
	Unlink all currently active capture URBs.
*/
static void unlink_audio_in_urbs(struct snd_line6_pcm *line6pcm)
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

	line6pcm->active_urb_in = 0;
	line6pcm->unlink_urb_in = 0;
}

/*
	Unlink all currently active capture URBs, and wait for finishing.
*/
void unlink_wait_clear_audio_in_urbs(struct snd_line6_pcm *line6pcm)
{
	unlink_audio_in_urbs(line6pcm);
	wait_clear_audio_in_urbs(line6pcm);
}

/*
	Callback for completed capture URB.
*/
static void audio_in_callback(struct urb *urb)
{
	int i, index, length = 0, shutdown = 0;
	int frames;
	unsigned long flags;

	struct snd_pcm_substream *substream = (struct snd_pcm_substream *)urb->context;
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	const int bytes_per_frame = line6pcm->properties->bytes_per_frame;
	struct snd_pcm_runtime *runtime = substream->runtime;

	/* find index of URB */
	for (index = 0; index < LINE6_ISO_BUFFERS; ++index)
		if (urb == line6pcm->urb_audio_in[index])
			break;

#if DO_DUMP_PCM_RECEIVE
	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		struct usb_iso_packet_descriptor *fout = &urb->iso_frame_desc[i];
		line6_write_hexdump(line6pcm->line6, 'C', urb->transfer_buffer + fout->offset, fout->length);
	}
#endif

	spin_lock_irqsave(&line6pcm->lock_audio_in, flags);

	for (i = 0; i < LINE6_ISO_PACKETS; ++i) {
		char *fbuf;
		int fsize;
		struct usb_iso_packet_descriptor *fin = &urb->iso_frame_desc[i];

		if (fin->status == -18) {
			shutdown = 1;
			break;
		}

		fbuf = urb->transfer_buffer + fin->offset;
		fsize = fin->actual_length;
		length += fsize;

		if (fsize > 0) {
			frames = fsize / bytes_per_frame;

			if (line6pcm->pos_in_done + frames > runtime->buffer_size) {
				/*
					The transferred area goes over buffer boundary,
					copy two separate chunks.
				*/
				int len;
				len = runtime->buffer_size - line6pcm->pos_in_done;

				if (len > 0) {
					memcpy(runtime->dma_area + line6pcm->pos_in_done * bytes_per_frame, fbuf, len * bytes_per_frame);
					memcpy(runtime->dma_area, fbuf + len * bytes_per_frame, (frames - len) * bytes_per_frame);
				} else
					dev_err(s2m(substream), "driver bug: len = %d\n", len);  /* this is somewhat paranoid */
			} else {
				/* copy single chunk */
				memcpy(runtime->dma_area + line6pcm->pos_in_done * bytes_per_frame, fbuf, fsize * bytes_per_frame);
			}

			if ((line6pcm->pos_in_done += frames) >= runtime->buffer_size)
				line6pcm->pos_in_done -= runtime->buffer_size;
		}
	}

	clear_bit(index, &line6pcm->active_urb_in);

	if (test_bit(index, &line6pcm->unlink_urb_in))
		shutdown = 1;

	spin_unlock_irqrestore(&line6pcm->lock_audio_in, flags);

	if (!shutdown) {
		submit_audio_in_urb(substream);

		if ((line6pcm->bytes_in += length) >= line6pcm->period_in) {
			line6pcm->bytes_in -= line6pcm->period_in;
			snd_pcm_period_elapsed(substream);
		}
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
					    (&line6pcm->properties->snd_line6_rates));
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

	ret = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	if (ret < 0)
		return ret;

	line6pcm->period_in = params_period_bytes(hw_params);
	line6pcm->buffer_in = kmalloc(LINE6_ISO_BUFFERS * LINE6_ISO_PACKETS * LINE6_ISO_PACKET_SIZE_MAX, GFP_KERNEL);

	if (!line6pcm->buffer_in) {
		dev_err(s2m(substream), "cannot malloc buffer_in\n");
		return -ENOMEM;
	}

	return 0;
}

/* hw_free capture callback */
static int snd_line6_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	unlink_wait_clear_audio_in_urbs(line6pcm);

	kfree(line6pcm->buffer_in);
	line6pcm->buffer_in = NULL;

	return snd_pcm_lib_free_pages(substream);
}

/* trigger callback */
int snd_line6_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	int err;
	line6pcm->count_in = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (!test_and_set_bit(BIT_RUNNING_CAPTURE, &line6pcm->flags)) {
			err = submit_audio_in_all_urbs(substream);

			if (err < 0) {
				clear_bit(BIT_RUNNING_CAPTURE, &line6pcm->flags);
				return err;
			}
		}

		break;

	case SNDRV_PCM_TRIGGER_STOP:
		if (test_and_clear_bit(BIT_RUNNING_CAPTURE, &line6pcm->flags))
			unlink_audio_in_urbs(line6pcm);

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
	.open =        snd_line6_capture_open,
	.close =       snd_line6_capture_close,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_line6_capture_hw_params,
	.hw_free =     snd_line6_capture_hw_free,
	.prepare =     snd_line6_prepare,
	.trigger =     snd_line6_trigger,
	.pointer =     snd_line6_capture_pointer,
};

int create_audio_in_urbs(struct snd_line6_pcm *line6pcm)
{
	int i;

	/* create audio URBs and fill in constant values: */
	for (i = 0; i < LINE6_ISO_BUFFERS; ++i) {
		struct urb *urb;

		/* URB for audio in: */
		urb = line6pcm->urb_audio_in[i] = usb_alloc_urb(LINE6_ISO_PACKETS, GFP_KERNEL);

		if (urb == NULL) {
			dev_err(line6pcm->line6->ifcdev, "Out of memory\n");
			return -ENOMEM;
		}

		urb->dev = line6pcm->line6->usbdev;
		urb->pipe = usb_rcvisocpipe(line6pcm->line6->usbdev, line6pcm->ep_audio_read & USB_ENDPOINT_NUMBER_MASK);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->start_frame = -1;
		urb->number_of_packets = LINE6_ISO_PACKETS;
		urb->interval = LINE6_ISO_INTERVAL;
		urb->error_count = 0;
		urb->complete = audio_in_callback;
	}

	return 0;
}
