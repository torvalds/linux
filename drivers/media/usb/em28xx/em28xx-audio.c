/*
 *  Empiatech em28x1 audio extension
 *
 *  Copyright (C) 2006 Markus Rechberger <mrechberger@gmail.com>
 *
 *  Copyright (C) 2007-2016 Mauro Carvalho Chehab
 *	- Port to work with the in-kernel driver
 *	- Cleanups, fixes, alsa-controls, etc.
 *
 *  This driver is based on my previous au600 usb pstn audio driver
 *  and inherits all the copyrights
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

#include "em28xx.h"

#include <linux/kernel.h>
#include <linux/usb.h>
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
#include <sound/tlv.h>
#include <sound/ac97_codec.h>
#include <media/v4l2-common.h>

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

#define EM28XX_MAX_AUDIO_BUFS		5
#define EM28XX_MIN_AUDIO_PACKETS	64

#define dprintk(fmt, arg...) do {					\
	if (debug)						\
		dev_printk(KERN_DEBUG, &dev->intf->dev,			\
			   "video: %s: " fmt, __func__, ## arg);	\
} while (0)

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;

static int em28xx_deinit_isoc_audio(struct em28xx *dev)
{
	int i;

	dprintk("Stopping isoc\n");
	for (i = 0; i < dev->adev.num_urb; i++) {
		struct urb *urb = dev->adev.urb[i];

		if (!irqs_disabled())
			usb_kill_urb(urb);
		else
			usb_unlink_urb(urb);
	}

	return 0;
}

static void em28xx_audio_isocirq(struct urb *urb)
{
	struct em28xx            *dev = urb->context;
	int                      i;
	unsigned int             oldptr;
	int                      period_elapsed = 0;
	int                      status;
	unsigned char            *cp;
	unsigned int             stride;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime   *runtime;

	if (dev->disconnected) {
		dprintk("device disconnected while streaming. URB status=%d.\n",
			urb->status);
		atomic_set(&dev->adev.stream_started, 0);
		return;
	}

	switch (urb->status) {
	case 0:             /* success */
	case -ETIMEDOUT:    /* NAK */
		break;
	case -ECONNRESET:   /* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:            /* error */
		dprintk("urb completition error %d.\n", urb->status);
		break;
	}

	if (atomic_read(&dev->adev.stream_started) == 0)
		return;

	if (dev->adev.capture_pcm_substream) {
		substream = dev->adev.capture_pcm_substream;
		runtime = substream->runtime;
		stride = runtime->frame_bits >> 3;

		for (i = 0; i < urb->number_of_packets; i++) {
			int length =
			    urb->iso_frame_desc[i].actual_length / stride;
			cp = (unsigned char *)urb->transfer_buffer +
			    urb->iso_frame_desc[i].offset;

			if (!length)
				continue;

			oldptr = dev->adev.hwptr_done_capture;
			if (oldptr + length >= runtime->buffer_size) {
				unsigned int cnt =
				    runtime->buffer_size - oldptr;
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
	if (status < 0)
		dev_err(&dev->intf->dev,
			"resubmit of audio urb failed (error=%i)\n",
			status);
	return;
}

static int em28xx_init_audio_isoc(struct em28xx *dev)
{
	int       i, errCode;

	dprintk("Starting isoc transfers\n");

	/* Start streaming */
	for (i = 0; i < dev->adev.num_urb; i++) {
		memset(dev->adev.transfer_buffer[i], 0x80,
		       dev->adev.urb[i]->transfer_buffer_length);

		errCode = usb_submit_urb(dev->adev.urb[i], GFP_ATOMIC);
		if (errCode) {
			dev_err(&dev->intf->dev,
				"submit of audio urb failed (error=%i)\n",
				errCode);
			em28xx_deinit_isoc_audio(dev);
			atomic_set(&dev->adev.stream_started, 0);
			return errCode;
		}

	}

	return 0;
}

static int snd_pcm_alloc_vmalloc_buffer(struct snd_pcm_substream *subs,
					size_t size)
{
	struct em28xx *dev = snd_pcm_substream_chip(subs);
	struct snd_pcm_runtime *runtime = subs->runtime;

	dprintk("Allocating vbuffer\n");
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

static const struct snd_pcm_hardware snd_em28xx_hw_capture = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP           |
		SNDRV_PCM_INFO_INTERLEAVED    |
		SNDRV_PCM_INFO_BATCH	      |
		SNDRV_PCM_INFO_MMAP_VALID,

	.formats = SNDRV_PCM_FMTBIT_S16_LE,

	.rates = SNDRV_PCM_RATE_48000,

	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = 62720 * 8,	/* just about the value in usbaudio.c */

	/*
	 * The period is 12.288 bytes. Allow a 10% of variation along its
	 * value, in order to avoid overruns/underruns due to some clock
	 * drift.
	 *
	 * FIXME: This period assumes 64 packets, and a 48000 PCM rate.
	 * Calculate it dynamically.
	 */
	.period_bytes_min = 11059,
	.period_bytes_max = 13516,

	.periods_min = 2,
	.periods_max = 98,		/* 12544, */
};

static int snd_em28xx_capture_open(struct snd_pcm_substream *substream)
{
	struct em28xx *dev = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int nonblock, ret = 0;

	if (!dev) {
		pr_err("em28xx-audio: BUG: em28xx can't find device struct. Can't proceed with open\n");
		return -ENODEV;
	}

	if (dev->disconnected)
		return -ENODEV;

	dprintk("opening device and trying to acquire exclusive lock\n");

	nonblock = !!(substream->f_flags & O_NONBLOCK);
	if (nonblock) {
		if (!mutex_trylock(&dev->lock))
			return -EAGAIN;
	} else
		mutex_lock(&dev->lock);

	runtime->hw = snd_em28xx_hw_capture;

	if (dev->adev.users == 0) {
		if (dev->alt == 0 || dev->is_audio_only) {
			struct usb_device *udev = interface_to_usbdev(dev->intf);

			if (dev->is_audio_only)
				/* audio is on a separate interface */
				dev->alt = 1;
			else
				/* audio is on the same interface as video */
				dev->alt = 7;
				/*
				 * FIXME: The intention seems to be to select
				 * the alt setting with the largest
				 * wMaxPacketSize for the video endpoint.
				 * At least dev->alt should be used instead, but
				 * we should probably not touch it at all if it
				 * is already >0, because wMaxPacketSize of the
				 * audio endpoints seems to be the same for all.
				 */
			dprintk("changing alternate number on interface %d to %d\n",
				dev->ifnum, dev->alt);
			usb_set_interface(udev, dev->ifnum, dev->alt);
		}

		/* Sets volume, mute, etc */
		dev->mute = 0;
		ret = em28xx_audio_analog_set(dev);
		if (ret < 0)
			goto err;
	}

	kref_get(&dev->ref);
	dev->adev.users++;
	mutex_unlock(&dev->lock);

	/* Dynamically adjust the period size */
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				     dev->adev.period * 95 / 100,
				     dev->adev.period * 105 / 100);

	dev->adev.capture_pcm_substream = substream;

	return 0;
err:
	mutex_unlock(&dev->lock);

	dev_err(&dev->intf->dev,
		"Error while configuring em28xx mixer\n");
	return ret;
}

static int snd_em28xx_pcm_close(struct snd_pcm_substream *substream)
{
	struct em28xx *dev = snd_pcm_substream_chip(substream);

	dprintk("closing device\n");

	dev->mute = 1;
	mutex_lock(&dev->lock);
	dev->adev.users--;
	if (atomic_read(&dev->adev.stream_started) > 0) {
		atomic_set(&dev->adev.stream_started, 0);
		schedule_work(&dev->adev.wq_trigger);
	}

	em28xx_audio_analog_set(dev);
	if (substream->runtime->dma_area) {
		dprintk("freeing\n");
		vfree(substream->runtime->dma_area);
		substream->runtime->dma_area = NULL;
	}
	mutex_unlock(&dev->lock);
	kref_put(&dev->ref, em28xx_free_device);

	return 0;
}

static int snd_em28xx_hw_capture_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	int ret;
	struct em28xx *dev = snd_pcm_substream_chip(substream);

	if (dev->disconnected)
		return -ENODEV;

	dprintk("Setting capture parameters\n");

	ret = snd_pcm_alloc_vmalloc_buffer(substream,
					   params_buffer_bytes(hw_params));
	if (ret < 0)
		return ret;
#if 0
	/* TODO: set up em28xx audio chip to deliver the correct audio format,
	   current default is 48000hz multiplexed => 96000hz mono
	   which shouldn't matter since analogue TV only supports mono */
	unsigned int channels, rate, format;

	format = params_format(hw_params);
	rate = params_rate(hw_params);
	channels = params_channels(hw_params);
#endif

	return 0;
}

static int snd_em28xx_hw_capture_free(struct snd_pcm_substream *substream)
{
	struct em28xx *dev = snd_pcm_substream_chip(substream);
	struct em28xx_audio *adev = &dev->adev;

	dprintk("Stop capture, if needed\n");

	if (atomic_read(&adev->stream_started) > 0) {
		atomic_set(&adev->stream_started, 0);
		schedule_work(&adev->wq_trigger);
	}

	return 0;
}

static int snd_em28xx_prepare(struct snd_pcm_substream *substream)
{
	struct em28xx *dev = snd_pcm_substream_chip(substream);

	if (dev->disconnected)
		return -ENODEV;

	dev->adev.hwptr_done_capture = 0;
	dev->adev.capture_transfer_done = 0;

	return 0;
}

static void audio_trigger(struct work_struct *work)
{
	struct em28xx_audio *adev =
			    container_of(work, struct em28xx_audio, wq_trigger);
	struct em28xx *dev = container_of(adev, struct em28xx, adev);

	if (atomic_read(&adev->stream_started)) {
		dprintk("starting capture");
		em28xx_init_audio_isoc(dev);
	} else {
		dprintk("stopping capture");
		em28xx_deinit_isoc_audio(dev);
	}
}

static int snd_em28xx_capture_trigger(struct snd_pcm_substream *substream,
				      int cmd)
{
	struct em28xx *dev = snd_pcm_substream_chip(substream);
	int retval = 0;

	if (dev->disconnected)
		return -ENODEV;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE: /* fall through */
	case SNDRV_PCM_TRIGGER_RESUME: /* fall through */
	case SNDRV_PCM_TRIGGER_START:
		atomic_set(&dev->adev.stream_started, 1);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH: /* fall through */
	case SNDRV_PCM_TRIGGER_SUSPEND: /* fall through */
	case SNDRV_PCM_TRIGGER_STOP:
		atomic_set(&dev->adev.stream_started, 0);
		break;
	default:
		retval = -EINVAL;
	}
	schedule_work(&dev->adev.wq_trigger);
	return retval;
}

static snd_pcm_uframes_t snd_em28xx_capture_pointer(struct snd_pcm_substream
						    *substream)
{
	unsigned long flags;
	struct em28xx *dev;
	snd_pcm_uframes_t hwptr_done;

	dev = snd_pcm_substream_chip(substream);
	if (dev->disconnected)
		return SNDRV_PCM_POS_XRUN;

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

/*
 * AC97 volume control support
 */
static int em28xx_vol_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *info)
{
	struct em28xx *dev = snd_kcontrol_chip(kcontrol);

	if (dev->disconnected)
		return -ENODEV;

	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 2;
	info->value.integer.min = 0;
	info->value.integer.max = 0x1f;

	return 0;
}

static int em28xx_vol_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *value)
{
	struct em28xx *dev = snd_kcontrol_chip(kcontrol);
	struct snd_pcm_substream *substream = dev->adev.capture_pcm_substream;
	u16 val = (0x1f - (value->value.integer.value[0] & 0x1f)) |
		  (0x1f - (value->value.integer.value[1] & 0x1f)) << 8;
	int nonblock = 0;
	int rc;

	if (dev->disconnected)
		return -ENODEV;

	if (substream)
		nonblock = !!(substream->f_flags & O_NONBLOCK);
	if (nonblock) {
		if (!mutex_trylock(&dev->lock))
			return -EAGAIN;
	} else
		mutex_lock(&dev->lock);
	rc = em28xx_read_ac97(dev, kcontrol->private_value);
	if (rc < 0)
		goto err;

	val |= rc & 0x8000;	/* Preserve the mute flag */

	rc = em28xx_write_ac97(dev, kcontrol->private_value, val);
	if (rc < 0)
		goto err;

	dprintk("%sleft vol %d, right vol %d (0x%04x) to ac97 volume control 0x%04x\n",
		(val & 0x8000) ? "muted " : "",
		0x1f - ((val >> 8) & 0x1f), 0x1f - (val & 0x1f),
		val, (int)kcontrol->private_value);

err:
	mutex_unlock(&dev->lock);
	return rc;
}

static int em28xx_vol_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *value)
{
	struct em28xx *dev = snd_kcontrol_chip(kcontrol);
	struct snd_pcm_substream *substream = dev->adev.capture_pcm_substream;
	int nonblock = 0;
	int val;

	if (dev->disconnected)
		return -ENODEV;

	if (substream)
		nonblock = !!(substream->f_flags & O_NONBLOCK);
	if (nonblock) {
		if (!mutex_trylock(&dev->lock))
			return -EAGAIN;
	} else
		mutex_lock(&dev->lock);
	val = em28xx_read_ac97(dev, kcontrol->private_value);
	mutex_unlock(&dev->lock);
	if (val < 0)
		return val;

	dprintk("%sleft vol %d, right vol %d (0x%04x) from ac97 volume control 0x%04x\n",
		(val & 0x8000) ? "muted " : "",
		0x1f - ((val >> 8) & 0x1f), 0x1f - (val & 0x1f),
		val, (int)kcontrol->private_value);

	value->value.integer.value[0] = 0x1f - (val & 0x1f);
	value->value.integer.value[1] = 0x1f - ((val >> 8) & 0x1f);

	return 0;
}

static int em28xx_vol_put_mute(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *value)
{
	struct em28xx *dev = snd_kcontrol_chip(kcontrol);
	u16 val = value->value.integer.value[0];
	struct snd_pcm_substream *substream = dev->adev.capture_pcm_substream;
	int nonblock = 0;
	int rc;

	if (dev->disconnected)
		return -ENODEV;

	if (substream)
		nonblock = !!(substream->f_flags & O_NONBLOCK);
	if (nonblock) {
		if (!mutex_trylock(&dev->lock))
			return -EAGAIN;
	} else
		mutex_lock(&dev->lock);
	rc = em28xx_read_ac97(dev, kcontrol->private_value);
	if (rc < 0)
		goto err;

	if (val)
		rc &= 0x1f1f;
	else
		rc |= 0x8000;

	rc = em28xx_write_ac97(dev, kcontrol->private_value, rc);
	if (rc < 0)
		goto err;

	dprintk("%sleft vol %d, right vol %d (0x%04x) to ac97 volume control 0x%04x\n",
		(val & 0x8000) ? "muted " : "",
		0x1f - ((val >> 8) & 0x1f), 0x1f - (val & 0x1f),
		val, (int)kcontrol->private_value);

err:
	mutex_unlock(&dev->lock);
	return rc;
}

static int em28xx_vol_get_mute(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *value)
{
	struct em28xx *dev = snd_kcontrol_chip(kcontrol);
	struct snd_pcm_substream *substream = dev->adev.capture_pcm_substream;
	int nonblock = 0;
	int val;

	if (dev->disconnected)
		return -ENODEV;

	if (substream)
		nonblock = !!(substream->f_flags & O_NONBLOCK);
	if (nonblock) {
		if (!mutex_trylock(&dev->lock))
			return -EAGAIN;
	} else
		mutex_lock(&dev->lock);
	val = em28xx_read_ac97(dev, kcontrol->private_value);
	mutex_unlock(&dev->lock);
	if (val < 0)
		return val;

	if (val & 0x8000)
		value->value.integer.value[0] = 0;
	else
		value->value.integer.value[0] = 1;

	dprintk("%sleft vol %d, right vol %d (0x%04x) from ac97 volume control 0x%04x\n",
		(val & 0x8000) ? "muted " : "",
		0x1f - ((val >> 8) & 0x1f), 0x1f - (val & 0x1f),
		val, (int)kcontrol->private_value);

	return 0;
}

static const DECLARE_TLV_DB_SCALE(em28xx_db_scale, -3450, 150, 0);

static int em28xx_cvol_new(struct snd_card *card, struct em28xx *dev,
			   char *name, int id)
{
	int err;
	char ctl_name[44];
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_new tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	tmp.private_value = id,
	tmp.name  = ctl_name,

	/* Add Mute Control */
	sprintf(ctl_name, "%s Switch", name);
	tmp.get  = em28xx_vol_get_mute;
	tmp.put  = em28xx_vol_put_mute;
	tmp.info = snd_ctl_boolean_mono_info;
	kctl = snd_ctl_new1(&tmp, dev);
	err = snd_ctl_add(card, kctl);
	if (err < 0)
		return err;
	dprintk("Added control %s for ac97 volume control 0x%04x\n",
		ctl_name, id);

	memset(&tmp, 0, sizeof(tmp));
	tmp.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	tmp.private_value = id,
	tmp.name  = ctl_name,

	/* Add Volume Control */
	sprintf(ctl_name, "%s Volume", name);
	tmp.get   = em28xx_vol_get;
	tmp.put   = em28xx_vol_put;
	tmp.info  = em28xx_vol_info;
	tmp.tlv.p = em28xx_db_scale,
	kctl = snd_ctl_new1(&tmp, dev);
	err = snd_ctl_add(card, kctl);
	if (err < 0)
		return err;
	dprintk("Added control %s for ac97 volume control 0x%04x\n",
		ctl_name, id);

	return 0;
}

/*
 * register/unregister code and data
 */
static const struct snd_pcm_ops snd_em28xx_pcm_capture = {
	.open      = snd_em28xx_capture_open,
	.close     = snd_em28xx_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = snd_em28xx_hw_capture_params,
	.hw_free   = snd_em28xx_hw_capture_free,
	.prepare   = snd_em28xx_prepare,
	.trigger   = snd_em28xx_capture_trigger,
	.pointer   = snd_em28xx_capture_pointer,
	.page      = snd_pcm_get_vmalloc_page,
};

static void em28xx_audio_free_urb(struct em28xx *dev)
{
	struct usb_device *udev = interface_to_usbdev(dev->intf);
	int i;

	for (i = 0; i < dev->adev.num_urb; i++) {
		struct urb *urb = dev->adev.urb[i];

		if (!urb)
			continue;

		usb_free_coherent(udev, urb->transfer_buffer_length,
				  dev->adev.transfer_buffer[i],
				  urb->transfer_dma);

		usb_free_urb(urb);
	}
	kfree(dev->adev.urb);
	kfree(dev->adev.transfer_buffer);
	dev->adev.num_urb = 0;
}

/* high bandwidth multiplier, as encoded in highspeed endpoint descriptors */
static int em28xx_audio_ep_packet_size(struct usb_device *udev,
				       struct usb_endpoint_descriptor *e)
{
	int size = le16_to_cpu(e->wMaxPacketSize);

	if (udev->speed == USB_SPEED_HIGH)
		return (size & 0x7ff) *  (1 + (((size) >> 11) & 0x03));

	return size & 0x7ff;
}

static int em28xx_audio_urb_init(struct em28xx *dev)
{
	struct usb_interface *intf;
	struct usb_endpoint_descriptor *e, *ep = NULL;
	struct usb_device *udev = interface_to_usbdev(dev->intf);
	int                 i, ep_size, interval, num_urb, npackets;
	int		    urb_size, bytes_per_transfer;
	u8 alt;

	if (dev->ifnum)
		alt = 1;
	else
		alt = 7;

	intf = usb_ifnum_to_if(udev, dev->ifnum);

	if (intf->num_altsetting <= alt) {
		dev_err(&dev->intf->dev, "alt %d doesn't exist on interface %d\n",
			      dev->ifnum, alt);
		return -ENODEV;
	}

	for (i = 0; i < intf->altsetting[alt].desc.bNumEndpoints; i++) {
		e = &intf->altsetting[alt].endpoint[i].desc;
		if (!usb_endpoint_dir_in(e))
			continue;
		if (e->bEndpointAddress == EM28XX_EP_AUDIO) {
			ep = e;
			break;
		}
	}

	if (!ep) {
		dev_err(&dev->intf->dev, "Couldn't find an audio endpoint");
		return -ENODEV;
	}

	ep_size = em28xx_audio_ep_packet_size(udev, ep);
	interval = 1 << (ep->bInterval - 1);

	dev_info(&dev->intf->dev,
		 "Endpoint 0x%02x %s on intf %d alt %d interval = %d, size %d\n",
		 EM28XX_EP_AUDIO, usb_speed_string(udev->speed),
		 dev->ifnum, alt, interval, ep_size);

	/* Calculate the number and size of URBs to better fit the audio samples */

	/*
	 * Estimate the number of bytes per DMA transfer.
	 *
	 * This is given by the bit rate (for now, only 48000 Hz) multiplied
	 * by 2 channels and 2 bytes/sample divided by the number of microframe
	 * intervals and by the microframe rate (125 us)
	 */
	bytes_per_transfer = DIV_ROUND_UP(48000 * 2 * 2, 125 * interval);

	/*
	 * Estimate the number of transfer URBs. Don't let it go past the
	 * maximum number of URBs that is known to be supported by the device.
	 */
	num_urb = DIV_ROUND_UP(bytes_per_transfer, ep_size);
	if (num_urb > EM28XX_MAX_AUDIO_BUFS)
		num_urb = EM28XX_MAX_AUDIO_BUFS;

	/*
	 * Now that we know the number of bytes per transfer and the number of
	 * URBs, estimate the typical size of an URB, in order to adjust the
	 * minimal number of packets.
	 */
	urb_size = bytes_per_transfer / num_urb;

	/*
	 * Now, calculate the amount of audio packets to be filled on each
	 * URB. In order to preserve the old behaviour, use a minimal
	 * threshold for this value.
	 */
	npackets = EM28XX_MIN_AUDIO_PACKETS;
	if (urb_size > ep_size * npackets)
		npackets = DIV_ROUND_UP(urb_size, ep_size);

	dev_info(&dev->intf->dev,
		 "Number of URBs: %d, with %d packets and %d size\n",
		 num_urb, npackets, urb_size);

	/* Estimate the bytes per period */
	dev->adev.period = urb_size * npackets;

	/* Allocate space to store the number of URBs to be used */

	dev->adev.transfer_buffer = kcalloc(num_urb,
					    sizeof(*dev->adev.transfer_buffer),
					    GFP_ATOMIC);
	if (!dev->adev.transfer_buffer) {
		return -ENOMEM;
	}

	dev->adev.urb = kcalloc(num_urb, sizeof(*dev->adev.urb), GFP_ATOMIC);
	if (!dev->adev.urb) {
		kfree(dev->adev.transfer_buffer);
		return -ENOMEM;
	}

	/* Alloc memory for each URB and for each transfer buffer */
	dev->adev.num_urb = num_urb;
	for (i = 0; i < num_urb; i++) {
		struct urb *urb;
		int j, k;
		void *buf;

		urb = usb_alloc_urb(npackets, GFP_ATOMIC);
		if (!urb) {
			em28xx_audio_free_urb(dev);
			return -ENOMEM;
		}
		dev->adev.urb[i] = urb;

		buf = usb_alloc_coherent(udev, npackets * ep_size, GFP_ATOMIC,
					 &urb->transfer_dma);
		if (!buf) {
			dev_err(&dev->intf->dev,
				"usb_alloc_coherent failed!\n");
			em28xx_audio_free_urb(dev);
			return -ENOMEM;
		}
		dev->adev.transfer_buffer[i] = buf;

		urb->dev = udev;
		urb->context = dev;
		urb->pipe = usb_rcvisocpipe(udev, EM28XX_EP_AUDIO);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_buffer = buf;
		urb->interval = interval;
		urb->complete = em28xx_audio_isocirq;
		urb->number_of_packets = npackets;
		urb->transfer_buffer_length = ep_size * npackets;

		for (j = k = 0; j < npackets; j++, k += ep_size) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = ep_size;
		}
	}

	return 0;
}

static int em28xx_audio_init(struct em28xx *dev)
{
	struct em28xx_audio *adev = &dev->adev;
	struct usb_device *udev = interface_to_usbdev(dev->intf);
	struct snd_pcm      *pcm;
	struct snd_card     *card;
	static int          devnr;
	int		    err;

	if (dev->usb_audio_type != EM28XX_USB_AUDIO_VENDOR) {
		/* This device does not support the extension (in this case
		   the device is expecting the snd-usb-audio module or
		   doesn't have analog audio support at all) */
		return 0;
	}

	dev_info(&dev->intf->dev, "Binding audio extension\n");

	kref_get(&dev->ref);

	dev_info(&dev->intf->dev,
		 "em28xx-audio.c: Copyright (C) 2006 Markus Rechberger\n");
	dev_info(&dev->intf->dev,
		 "em28xx-audio.c: Copyright (C) 2007-2016 Mauro Carvalho Chehab\n");

	err = snd_card_new(&dev->intf->dev, index[devnr], "Em28xx Audio",
			   THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	spin_lock_init(&adev->slock);
	adev->sndcard = card;
	adev->udev = udev;

	err = snd_pcm_new(card, "Em28xx Audio", 0, 0, 1, &pcm);
	if (err < 0)
		goto card_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_em28xx_pcm_capture);
	pcm->info_flags = 0;
	pcm->private_data = dev;
	strcpy(pcm->name, "Empia 28xx Capture");

	strcpy(card->driver, "Em28xx-Audio");
	strcpy(card->shortname, "Em28xx Audio");
	strcpy(card->longname, "Empia Em28xx Audio");

	INIT_WORK(&adev->wq_trigger, audio_trigger);

	if (dev->audio_mode.ac97 != EM28XX_NO_AC97) {
		em28xx_cvol_new(card, dev, "Video", AC97_VIDEO);
		em28xx_cvol_new(card, dev, "Line In", AC97_LINE);
		em28xx_cvol_new(card, dev, "Phone", AC97_PHONE);
		em28xx_cvol_new(card, dev, "Microphone", AC97_MIC);
		em28xx_cvol_new(card, dev, "CD", AC97_CD);
		em28xx_cvol_new(card, dev, "AUX", AC97_AUX);
		em28xx_cvol_new(card, dev, "PCM", AC97_PCM);

		em28xx_cvol_new(card, dev, "Master", AC97_MASTER);
		em28xx_cvol_new(card, dev, "Line", AC97_HEADPHONE);
		em28xx_cvol_new(card, dev, "Mono", AC97_MASTER_MONO);
		em28xx_cvol_new(card, dev, "LFE", AC97_CENTER_LFE_MASTER);
		em28xx_cvol_new(card, dev, "Surround", AC97_SURROUND_MASTER);
	}

	err = em28xx_audio_urb_init(dev);
	if (err)
		goto card_free;

	err = snd_card_register(card);
	if (err < 0)
		goto urb_free;

	dev_info(&dev->intf->dev, "Audio extension successfully initialized\n");
	return 0;

urb_free:
	em28xx_audio_free_urb(dev);

card_free:
	snd_card_free(card);
	adev->sndcard = NULL;

	return err;
}

static int em28xx_audio_fini(struct em28xx *dev)
{
	if (dev == NULL)
		return 0;

	if (dev->usb_audio_type != EM28XX_USB_AUDIO_VENDOR) {
		/* This device does not support the extension (in this case
		   the device is expecting the snd-usb-audio module or
		   doesn't have analog audio support at all) */
		return 0;
	}

	dev_info(&dev->intf->dev, "Closing audio extension\n");

	if (dev->adev.sndcard) {
		snd_card_disconnect(dev->adev.sndcard);
		flush_work(&dev->adev.wq_trigger);

		em28xx_audio_free_urb(dev);

		snd_card_free(dev->adev.sndcard);
		dev->adev.sndcard = NULL;
	}

	kref_put(&dev->ref, em28xx_free_device);
	return 0;
}

static int em28xx_audio_suspend(struct em28xx *dev)
{
	if (dev == NULL)
		return 0;

	if (dev->usb_audio_type != EM28XX_USB_AUDIO_VENDOR)
		return 0;

	dev_info(&dev->intf->dev, "Suspending audio extension\n");
	em28xx_deinit_isoc_audio(dev);
	atomic_set(&dev->adev.stream_started, 0);
	return 0;
}

static int em28xx_audio_resume(struct em28xx *dev)
{
	if (dev == NULL)
		return 0;

	if (dev->usb_audio_type != EM28XX_USB_AUDIO_VENDOR)
		return 0;

	dev_info(&dev->intf->dev, "Resuming audio extension\n");
	/* Nothing to do other than schedule_work() ?? */
	schedule_work(&dev->adev.wq_trigger);
	return 0;
}

static struct em28xx_ops audio_ops = {
	.id   = EM28XX_AUDIO,
	.name = "Em28xx Audio Extension",
	.init = em28xx_audio_init,
	.fini = em28xx_audio_fini,
	.suspend = em28xx_audio_suspend,
	.resume = em28xx_audio_resume,
};

static int __init em28xx_alsa_register(void)
{
	return em28xx_register_extension(&audio_ops);
}

static void __exit em28xx_alsa_unregister(void)
{
	em28xx_unregister_extension(&audio_ops);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markus Rechberger <mrechberger@gmail.com>");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION(DRIVER_DESC " - audio interface");
MODULE_VERSION(EM28XX_VERSION);

module_init(em28xx_alsa_register);
module_exit(em28xx_alsa_unregister);
