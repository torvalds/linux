/*
 *  Conexant Cx231xx audio extension
 *
 *  Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
 *       Based on em28xx driver
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include "cx231xx.h"
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sound.h>
#include <linux/spinlock.h>
#include <linux/soundcard.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <media/v4l2-common.h>

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;

static int cx231xx_isoc_audio_deinit(struct cx231xx *dev)
{
	int i;

	dev_dbg(dev->dev, "Stopping isoc\n");

	for (i = 0; i < CX231XX_AUDIO_BUFS; i++) {
		if (dev->adev.urb[i]) {
			if (!irqs_disabled())
				usb_kill_urb(dev->adev.urb[i]);
			else
				usb_unlink_urb(dev->adev.urb[i]);

			usb_free_urb(dev->adev.urb[i]);
			dev->adev.urb[i] = NULL;

			kfree(dev->adev.transfer_buffer[i]);
			dev->adev.transfer_buffer[i] = NULL;
		}
	}

	return 0;
}

static int cx231xx_bulk_audio_deinit(struct cx231xx *dev)
{
	int i;

	dev_dbg(dev->dev, "Stopping bulk\n");

	for (i = 0; i < CX231XX_AUDIO_BUFS; i++) {
		if (dev->adev.urb[i]) {
			if (!irqs_disabled())
				usb_kill_urb(dev->adev.urb[i]);
			else
				usb_unlink_urb(dev->adev.urb[i]);

			usb_free_urb(dev->adev.urb[i]);
			dev->adev.urb[i] = NULL;

			kfree(dev->adev.transfer_buffer[i]);
			dev->adev.transfer_buffer[i] = NULL;
		}
	}

	return 0;
}

static void cx231xx_audio_isocirq(struct urb *urb)
{
	struct cx231xx *dev = urb->context;
	int i;
	unsigned int oldptr;
	int period_elapsed = 0;
	int status;
	unsigned char *cp;
	unsigned int stride;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;

	if (dev->state & DEV_DISCONNECTED)
		return;

	switch (urb->status) {
	case 0:		/* success */
	case -ETIMEDOUT:	/* NAK */
		break;
	case -ECONNRESET:	/* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:		/* error */
		dev_dbg(dev->dev, "urb completition error %d.\n",
			urb->status);
		break;
	}

	if (atomic_read(&dev->stream_started) == 0)
		return;

	if (dev->adev.capture_pcm_substream) {
		substream = dev->adev.capture_pcm_substream;
		runtime = substream->runtime;
		stride = runtime->frame_bits >> 3;

		for (i = 0; i < urb->number_of_packets; i++) {
			int length = urb->iso_frame_desc[i].actual_length /
				     stride;
			cp = (unsigned char *)urb->transfer_buffer +
					      urb->iso_frame_desc[i].offset;

			if (!length)
				continue;

			oldptr = dev->adev.hwptr_done_capture;
			if (oldptr + length >= runtime->buffer_size) {
				unsigned int cnt;

				cnt = runtime->buffer_size - oldptr;
				memcpy(runtime->dma_area + oldptr * stride, cp,
				       cnt * stride);
				memcpy(runtime->dma_area, cp + cnt * stride,
				       length * stride - cnt * stride);
			} else {
				memcpy(runtime->dma_area + oldptr * stride, cp,
				       length * stride);
			}

			snd_pcm_stream_lock(substream);

			dev->adev.hwptr_done_capture += length;
			if (dev->adev.hwptr_done_capture >=
						runtime->buffer_size)
				dev->adev.hwptr_done_capture -=
						runtime->buffer_size;

			dev->adev.capture_transfer_done += length;
			if (dev->adev.capture_transfer_done >=
				runtime->period_size) {
				dev->adev.capture_transfer_done -=
						runtime->period_size;
				period_elapsed = 1;
			}
			snd_pcm_stream_unlock(substream);
		}
		if (period_elapsed)
			snd_pcm_period_elapsed(substream);
	}
	urb->status = 0;

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status < 0) {
		dev_err(dev->dev,
			"resubmit of audio urb failed (error=%i)\n",
			status);
	}
	return;
}

static void cx231xx_audio_bulkirq(struct urb *urb)
{
	struct cx231xx *dev = urb->context;
	unsigned int oldptr;
	int period_elapsed = 0;
	int status;
	unsigned char *cp;
	unsigned int stride;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;

	if (dev->state & DEV_DISCONNECTED)
		return;

	switch (urb->status) {
	case 0:		/* success */
	case -ETIMEDOUT:	/* NAK */
		break;
	case -ECONNRESET:	/* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:		/* error */
		dev_dbg(dev->dev, "urb completition error %d.\n",
			urb->status);
		break;
	}

	if (atomic_read(&dev->stream_started) == 0)
		return;

	if (dev->adev.capture_pcm_substream) {
		substream = dev->adev.capture_pcm_substream;
		runtime = substream->runtime;
		stride = runtime->frame_bits >> 3;

		if (1) {
			int length = urb->actual_length /
				     stride;
			cp = (unsigned char *)urb->transfer_buffer;

			oldptr = dev->adev.hwptr_done_capture;
			if (oldptr + length >= runtime->buffer_size) {
				unsigned int cnt;

				cnt = runtime->buffer_size - oldptr;
				memcpy(runtime->dma_area + oldptr * stride, cp,
				       cnt * stride);
				memcpy(runtime->dma_area, cp + cnt * stride,
				       length * stride - cnt * stride);
			} else {
				memcpy(runtime->dma_area + oldptr * stride, cp,
				       length * stride);
			}

			snd_pcm_stream_lock(substream);

			dev->adev.hwptr_done_capture += length;
			if (dev->adev.hwptr_done_capture >=
						runtime->buffer_size)
				dev->adev.hwptr_done_capture -=
						runtime->buffer_size;

			dev->adev.capture_transfer_done += length;
			if (dev->adev.capture_transfer_done >=
				runtime->period_size) {
				dev->adev.capture_transfer_done -=
						runtime->period_size;
				period_elapsed = 1;
			}
			snd_pcm_stream_unlock(substream);
		}
		if (period_elapsed)
			snd_pcm_period_elapsed(substream);
	}
	urb->status = 0;

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status < 0) {
		dev_err(dev->dev,
			"resubmit of audio urb failed (error=%i)\n",
			status);
	}
	return;
}

static int cx231xx_init_audio_isoc(struct cx231xx *dev)
{
	int i, errCode;
	int sb_size;

	dev_dbg(dev->dev,
		"%s: Starting ISO AUDIO transfers\n", __func__);

	if (dev->state & DEV_DISCONNECTED)
		return -ENODEV;

	sb_size = CX231XX_ISO_NUM_AUDIO_PACKETS * dev->adev.max_pkt_size;

	for (i = 0; i < CX231XX_AUDIO_BUFS; i++) {
		struct urb *urb;
		int j, k;

		dev->adev.transfer_buffer[i] = kmalloc(sb_size, GFP_ATOMIC);
		if (!dev->adev.transfer_buffer[i])
			return -ENOMEM;

		memset(dev->adev.transfer_buffer[i], 0x80, sb_size);
		urb = usb_alloc_urb(CX231XX_ISO_NUM_AUDIO_PACKETS, GFP_ATOMIC);
		if (!urb) {
			for (j = 0; j < i; j++) {
				usb_free_urb(dev->adev.urb[j]);
				kfree(dev->adev.transfer_buffer[j]);
			}
			return -ENOMEM;
		}

		urb->dev = dev->udev;
		urb->context = dev;
		urb->pipe = usb_rcvisocpipe(dev->udev,
						dev->adev.end_point_addr);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = dev->adev.transfer_buffer[i];
		urb->interval = 1;
		urb->complete = cx231xx_audio_isocirq;
		urb->number_of_packets = CX231XX_ISO_NUM_AUDIO_PACKETS;
		urb->transfer_buffer_length = sb_size;

		for (j = k = 0; j < CX231XX_ISO_NUM_AUDIO_PACKETS;
			j++, k += dev->adev.max_pkt_size) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = dev->adev.max_pkt_size;
		}
		dev->adev.urb[i] = urb;
	}

	for (i = 0; i < CX231XX_AUDIO_BUFS; i++) {
		errCode = usb_submit_urb(dev->adev.urb[i], GFP_ATOMIC);
		if (errCode < 0) {
			cx231xx_isoc_audio_deinit(dev);
			return errCode;
		}
	}

	return errCode;
}

static int cx231xx_init_audio_bulk(struct cx231xx *dev)
{
	int i, errCode;
	int sb_size;

	dev_dbg(dev->dev,
		"%s: Starting BULK AUDIO transfers\n", __func__);

	if (dev->state & DEV_DISCONNECTED)
		return -ENODEV;

	sb_size = CX231XX_NUM_AUDIO_PACKETS * dev->adev.max_pkt_size;

	for (i = 0; i < CX231XX_AUDIO_BUFS; i++) {
		struct urb *urb;
		int j;

		dev->adev.transfer_buffer[i] = kmalloc(sb_size, GFP_ATOMIC);
		if (!dev->adev.transfer_buffer[i])
			return -ENOMEM;

		memset(dev->adev.transfer_buffer[i], 0x80, sb_size);
		urb = usb_alloc_urb(CX231XX_NUM_AUDIO_PACKETS, GFP_ATOMIC);
		if (!urb) {
			for (j = 0; j < i; j++) {
				usb_free_urb(dev->adev.urb[j]);
				kfree(dev->adev.transfer_buffer[j]);
			}
			return -ENOMEM;
		}

		urb->dev = dev->udev;
		urb->context = dev;
		urb->pipe = usb_rcvbulkpipe(dev->udev,
						dev->adev.end_point_addr);
		urb->transfer_flags = 0;
		urb->transfer_buffer = dev->adev.transfer_buffer[i];
		urb->complete = cx231xx_audio_bulkirq;
		urb->transfer_buffer_length = sb_size;

		dev->adev.urb[i] = urb;

	}

	for (i = 0; i < CX231XX_AUDIO_BUFS; i++) {
		errCode = usb_submit_urb(dev->adev.urb[i], GFP_ATOMIC);
		if (errCode < 0) {
			cx231xx_bulk_audio_deinit(dev);
			return errCode;
		}
	}

	return errCode;
}

static int snd_pcm_alloc_vmalloc_buffer(struct snd_pcm_substream *subs,
					size_t size)
{
	struct snd_pcm_runtime *runtime = subs->runtime;
	struct cx231xx *dev = snd_pcm_substream_chip(subs);

	dev_dbg(dev->dev, "Allocating vbuffer\n");
	if (runtime->dma_area) {
		if (runtime->dma_bytes > size)
			return 0;

		vfree(runtime->dma_area);
	}
	runtime->dma_area = vmalloc(size);
	if (!runtime->dma_area)
		return -ENOMEM;

	runtime->dma_bytes = size;

	return 0;
}

static const struct snd_pcm_hardware snd_cx231xx_hw_capture = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER	|
	    SNDRV_PCM_INFO_MMAP			|
	    SNDRV_PCM_INFO_INTERLEAVED		|
	    SNDRV_PCM_INFO_MMAP_VALID,

	.formats = SNDRV_PCM_FMTBIT_S16_LE,

	.rates = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_KNOT,

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

static int snd_cx231xx_capture_open(struct snd_pcm_substream *substream)
{
	struct cx231xx *dev = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;

	dev_dbg(dev->dev,
		"opening device and trying to acquire exclusive lock\n");

	if (dev->state & DEV_DISCONNECTED) {
		dev_err(dev->dev,
			"Can't open. the device was removed.\n");
		return -ENODEV;
	}

	/* set alternate setting for audio interface */
	/* 1 - 48000 samples per sec */
	mutex_lock(&dev->lock);
	if (dev->USE_ISO)
		ret = cx231xx_set_alt_setting(dev, INDEX_AUDIO, 1);
	else
		ret = cx231xx_set_alt_setting(dev, INDEX_AUDIO, 0);
	mutex_unlock(&dev->lock);
	if (ret < 0) {
		dev_err(dev->dev,
			"failed to set alternate setting !\n");

		return ret;
	}

	runtime->hw = snd_cx231xx_hw_capture;

	mutex_lock(&dev->lock);
	/* inform hardware to start streaming */
	ret = cx231xx_capture_start(dev, 1, Audio);

	dev->adev.users++;
	mutex_unlock(&dev->lock);

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	dev->adev.capture_pcm_substream = substream;
	runtime->private_data = dev;

	return 0;
}

static int snd_cx231xx_pcm_close(struct snd_pcm_substream *substream)
{
	int ret;
	struct cx231xx *dev = snd_pcm_substream_chip(substream);

	dev_dbg(dev->dev, "closing device\n");

	/* inform hardware to stop streaming */
	mutex_lock(&dev->lock);
	ret = cx231xx_capture_start(dev, 0, Audio);

	/* set alternate setting for audio interface */
	/* 1 - 48000 samples per sec */
	ret = cx231xx_set_alt_setting(dev, INDEX_AUDIO, 0);
	if (ret < 0) {
		dev_err(dev->dev,
			"failed to set alternate setting !\n");

		mutex_unlock(&dev->lock);
		return ret;
	}

	dev->adev.users--;
	if (substream->runtime->dma_area) {
		dev_dbg(dev->dev, "freeing\n");
		vfree(substream->runtime->dma_area);
		substream->runtime->dma_area = NULL;
	}
	mutex_unlock(&dev->lock);

	if (dev->adev.users == 0 && dev->adev.shutdown == 1) {
		dev_dbg(dev->dev, "audio users: %d\n", dev->adev.users);
		dev_dbg(dev->dev, "disabling audio stream!\n");
		dev->adev.shutdown = 0;
		dev_dbg(dev->dev, "released lock\n");
		if (atomic_read(&dev->stream_started) > 0) {
			atomic_set(&dev->stream_started, 0);
			schedule_work(&dev->wq_trigger);
		}
	}
	return 0;
}

static int snd_cx231xx_hw_capture_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *hw_params)
{
	struct cx231xx *dev = snd_pcm_substream_chip(substream);
	int ret;

	dev_dbg(dev->dev, "Setting capture parameters\n");

	ret = snd_pcm_alloc_vmalloc_buffer(substream,
					   params_buffer_bytes(hw_params));
#if 0
	/* TODO: set up cx231xx audio chip to deliver the correct audio format,
	   current default is 48000hz multiplexed => 96000hz mono
	   which shouldn't matter since analogue TV only supports mono */
	unsigned int channels, rate, format;

	format = params_format(hw_params);
	rate = params_rate(hw_params);
	channels = params_channels(hw_params);
#endif

	return ret;
}

static int snd_cx231xx_hw_capture_free(struct snd_pcm_substream *substream)
{
	struct cx231xx *dev = snd_pcm_substream_chip(substream);

	dev_dbg(dev->dev, "Stop capture, if needed\n");

	if (atomic_read(&dev->stream_started) > 0) {
		atomic_set(&dev->stream_started, 0);
		schedule_work(&dev->wq_trigger);
	}

	return 0;
}

static int snd_cx231xx_prepare(struct snd_pcm_substream *substream)
{
	struct cx231xx *dev = snd_pcm_substream_chip(substream);

	dev->adev.hwptr_done_capture = 0;
	dev->adev.capture_transfer_done = 0;

	return 0;
}

static void audio_trigger(struct work_struct *work)
{
	struct cx231xx *dev = container_of(work, struct cx231xx, wq_trigger);

	if (atomic_read(&dev->stream_started)) {
		dev_dbg(dev->dev, "starting capture");
		if (is_fw_load(dev) == 0)
			cx25840_call(dev, core, load_fw);
		if (dev->USE_ISO)
			cx231xx_init_audio_isoc(dev);
		else
			cx231xx_init_audio_bulk(dev);
	} else {
		dev_dbg(dev->dev, "stopping capture");
		cx231xx_isoc_audio_deinit(dev);
	}
}

static int snd_cx231xx_capture_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
	struct cx231xx *dev = snd_pcm_substream_chip(substream);
	int retval = 0;

	if (dev->state & DEV_DISCONNECTED)
		return -ENODEV;

	spin_lock(&dev->adev.slock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		atomic_set(&dev->stream_started, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		atomic_set(&dev->stream_started, 0);
		break;
	default:
		retval = -EINVAL;
		break;
	}
	spin_unlock(&dev->adev.slock);

	schedule_work(&dev->wq_trigger);

	return retval;
}

static snd_pcm_uframes_t snd_cx231xx_capture_pointer(struct snd_pcm_substream
						     *substream)
{
	struct cx231xx *dev;
	unsigned long flags;
	snd_pcm_uframes_t hwptr_done;

	dev = snd_pcm_substream_chip(substream);

	spin_lock_irqsave(&dev->adev.slock, flags);
	hwptr_done = dev->adev.hwptr_done_capture;
	spin_unlock_irqrestore(&dev->adev.slock, flags);

	return hwptr_done;
}

static struct page *snd_pcm_get_vmalloc_page(struct snd_pcm_substream *subs,
					     unsigned long offset)
{
	void *pageptr = subs->runtime->dma_area + offset;

	return vmalloc_to_page(pageptr);
}

static const struct snd_pcm_ops snd_cx231xx_pcm_capture = {
	.open = snd_cx231xx_capture_open,
	.close = snd_cx231xx_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_cx231xx_hw_capture_params,
	.hw_free = snd_cx231xx_hw_capture_free,
	.prepare = snd_cx231xx_prepare,
	.trigger = snd_cx231xx_capture_trigger,
	.pointer = snd_cx231xx_capture_pointer,
	.page = snd_pcm_get_vmalloc_page,
};

static int cx231xx_audio_init(struct cx231xx *dev)
{
	struct cx231xx_audio *adev = &dev->adev;
	struct snd_pcm *pcm;
	struct snd_card *card;
	static int devnr;
	int err;
	struct usb_interface *uif;
	int i, isoc_pipe = 0;

	if (dev->has_alsa_audio != 1) {
		/* This device does not support the extension (in this case
		   the device is expecting the snd-usb-audio module or
		   doesn't have analog audio support at all) */
		return 0;
	}

	dev_dbg(dev->dev,
		"probing for cx231xx non standard usbaudio\n");

	err = snd_card_new(dev->dev, index[devnr], "Cx231xx Audio",
			   THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	spin_lock_init(&adev->slock);
	err = snd_pcm_new(card, "Cx231xx Audio", 0, 0, 1, &pcm);
	if (err < 0)
		goto err_free_card;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_cx231xx_pcm_capture);
	pcm->info_flags = 0;
	pcm->private_data = dev;
	strcpy(pcm->name, "Conexant cx231xx Capture");
	strcpy(card->driver, "Cx231xx-Audio");
	strcpy(card->shortname, "Cx231xx Audio");
	strcpy(card->longname, "Conexant cx231xx Audio");

	INIT_WORK(&dev->wq_trigger, audio_trigger);

	err = snd_card_register(card);
	if (err < 0)
		goto err_free_card;

	adev->sndcard = card;
	adev->udev = dev->udev;

	/* compute alternate max packet sizes for Audio */
	uif =
	    dev->udev->actconfig->interface[dev->current_pcb_config.
					    hs_config_info[0].interface_info.
					    audio_index + 1];

	if (uif->altsetting[0].desc.bNumEndpoints < isoc_pipe + 1) {
		err = -ENODEV;
		goto err_free_card;
	}

	adev->end_point_addr =
	    uif->altsetting[0].endpoint[isoc_pipe].desc.
			bEndpointAddress;

	adev->num_alt = uif->num_altsetting;
	dev_info(dev->dev,
		"audio EndPoint Addr 0x%x, Alternate settings: %i\n",
		adev->end_point_addr, adev->num_alt);
	adev->alt_max_pkt_size = kmalloc_array(32, adev->num_alt, GFP_KERNEL);
	if (!adev->alt_max_pkt_size) {
		err = -ENOMEM;
		goto err_free_card;
	}

	for (i = 0; i < adev->num_alt; i++) {
		u16 tmp;

		if (uif->altsetting[i].desc.bNumEndpoints < isoc_pipe + 1) {
			err = -ENODEV;
			goto err_free_pkt_size;
		}

		tmp = le16_to_cpu(uif->altsetting[i].endpoint[isoc_pipe].desc.
				wMaxPacketSize);
		adev->alt_max_pkt_size[i] =
		    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
		dev_dbg(dev->dev,
			"audio alternate setting %i, max size= %i\n", i,
			adev->alt_max_pkt_size[i]);
	}

	return 0;

err_free_pkt_size:
	kfree(adev->alt_max_pkt_size);
err_free_card:
	snd_card_free(card);

	return err;
}

static int cx231xx_audio_fini(struct cx231xx *dev)
{
	if (dev == NULL)
		return 0;

	if (dev->has_alsa_audio != 1) {
		/* This device does not support the extension (in this case
		   the device is expecting the snd-usb-audio module or
		   doesn't have analog audio support at all) */
		return 0;
	}

	if (dev->adev.sndcard) {
		snd_card_free(dev->adev.sndcard);
		kfree(dev->adev.alt_max_pkt_size);
		dev->adev.sndcard = NULL;
	}

	return 0;
}

static struct cx231xx_ops audio_ops = {
	.id = CX231XX_AUDIO,
	.name = "Cx231xx Audio Extension",
	.init = cx231xx_audio_init,
	.fini = cx231xx_audio_fini,
};

static int __init cx231xx_alsa_register(void)
{
	return cx231xx_register_extension(&audio_ops);
}

static void __exit cx231xx_alsa_unregister(void)
{
	cx231xx_unregister_extension(&audio_ops);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Srinivasa Deevi <srinivasa.deevi@conexant.com>");
MODULE_DESCRIPTION("Cx231xx Audio driver");

module_init(cx231xx_alsa_register);
module_exit(cx231xx_alsa_unregister);
