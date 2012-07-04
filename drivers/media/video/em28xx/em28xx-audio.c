/*
 *  Empiatech em28x1 audio extension
 *
 *  Copyright (C) 2006 Markus Rechberger <mrechberger@gmail.com>
 *
 *  Copyright (C) 2007-2011 Mauro Carvalho Chehab <mchehab@redhat.com>
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

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
#include <media/v4l2-common.h>
#include "em28xx.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

#define dprintk(fmt, arg...) do {					\
	    if (debug)							\
		printk(KERN_INFO "em28xx-audio %s: " fmt,		\
				  __func__, ##arg); 		\
	} while (0)

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;

static int em28xx_deinit_isoc_audio(struct em28xx *dev)
{
	int i;

	dprintk("Stopping isoc\n");
	for (i = 0; i < EM28XX_AUDIO_BUFS; i++) {
		if (!irqs_disabled())
			usb_kill_urb(dev->adev.urb[i]);
		else
			usb_unlink_urb(dev->adev.urb[i]);

		usb_free_urb(dev->adev.urb[i]);
		dev->adev.urb[i] = NULL;

		kfree(dev->adev.transfer_buffer[i]);
		dev->adev.transfer_buffer[i] = NULL;
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

	if (atomic_read(&dev->stream_started) == 0)
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
	if (status < 0) {
		em28xx_errdev("resubmit of audio urb failed (error=%i)\n",
			      status);
	}
	return;
}

static int em28xx_init_audio_isoc(struct em28xx *dev)
{
	int       i, errCode;
	const int sb_size = EM28XX_NUM_AUDIO_PACKETS *
			    EM28XX_AUDIO_MAX_PACKET_SIZE;

	dprintk("Starting isoc transfers\n");

	for (i = 0; i < EM28XX_AUDIO_BUFS; i++) {
		struct urb *urb;
		int j, k;

		dev->adev.transfer_buffer[i] = kmalloc(sb_size, GFP_ATOMIC);
		if (!dev->adev.transfer_buffer[i])
			return -ENOMEM;

		memset(dev->adev.transfer_buffer[i], 0x80, sb_size);
		urb = usb_alloc_urb(EM28XX_NUM_AUDIO_PACKETS, GFP_ATOMIC);
		if (!urb) {
			em28xx_errdev("usb_alloc_urb failed!\n");
			for (j = 0; j < i; j++) {
				usb_free_urb(dev->adev.urb[j]);
				kfree(dev->adev.transfer_buffer[j]);
			}
			return -ENOMEM;
		}

		urb->dev = dev->udev;
		urb->context = dev;
		urb->pipe = usb_rcvisocpipe(dev->udev, EM28XX_EP_AUDIO);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = dev->adev.transfer_buffer[i];
		urb->interval = 1;
		urb->complete = em28xx_audio_isocirq;
		urb->number_of_packets = EM28XX_NUM_AUDIO_PACKETS;
		urb->transfer_buffer_length = sb_size;

		for (j = k = 0; j < EM28XX_NUM_AUDIO_PACKETS;
			     j++, k += EM28XX_AUDIO_MAX_PACKET_SIZE) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length =
			    EM28XX_AUDIO_MAX_PACKET_SIZE;
		}
		dev->adev.urb[i] = urb;
	}

	for (i = 0; i < EM28XX_AUDIO_BUFS; i++) {
		errCode = usb_submit_urb(dev->adev.urb[i], GFP_ATOMIC);
		if (errCode) {
			em28xx_errdev("submit of audio urb failed\n");
			em28xx_deinit_isoc_audio(dev);
			atomic_set(&dev->stream_started, 0);
			return errCode;
		}

	}

	return 0;
}

static int snd_pcm_alloc_vmalloc_buffer(struct snd_pcm_substream *subs,
					size_t size)
{
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

static struct snd_pcm_hardware snd_em28xx_hw_capture = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP           |
		SNDRV_PCM_INFO_INTERLEAVED    |
		SNDRV_PCM_INFO_BATCH	      |
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

static int snd_em28xx_capture_open(struct snd_pcm_substream *substream)
{
	struct em28xx *dev = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;

	dprintk("opening device and trying to acquire exclusive lock\n");

	if (!dev) {
		em28xx_err("BUG: em28xx can't find device struct."
				" Can't proceed with open\n");
		return -ENODEV;
	}

	runtime->hw = snd_em28xx_hw_capture;
	if ((dev->alt == 0 || dev->audio_ifnum) && dev->adev.users == 0) {
		if (dev->audio_ifnum)
			dev->alt = 1;
		else
			dev->alt = 7;

		dprintk("changing alternate number on interface %d to %d\n",
			dev->audio_ifnum, dev->alt);
		usb_set_interface(dev->udev, dev->audio_ifnum, dev->alt);

		/* Sets volume, mute, etc */
		dev->mute = 0;
		mutex_lock(&dev->lock);
		ret = em28xx_audio_analog_set(dev);
		if (ret < 0)
			goto err;

		dev->adev.users++;
		mutex_unlock(&dev->lock);
	}

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	dev->adev.capture_pcm_substream = substream;
	runtime->private_data = dev;

	return 0;
err:
	mutex_unlock(&dev->lock);

	em28xx_err("Error while configuring em28xx mixer\n");
	return ret;
}

static int snd_em28xx_pcm_close(struct snd_pcm_substream *substream)
{
	struct em28xx *dev = snd_pcm_substream_chip(substream);

	dprintk("closing device\n");

	dev->mute = 1;
	mutex_lock(&dev->lock);
	dev->adev.users--;
	if (atomic_read(&dev->stream_started) > 0) {
		atomic_set(&dev->stream_started, 0);
		schedule_work(&dev->wq_trigger);
	}

	em28xx_audio_analog_set(dev);
	if (substream->runtime->dma_area) {
		dprintk("freeing\n");
		vfree(substream->runtime->dma_area);
		substream->runtime->dma_area = NULL;
	}
	mutex_unlock(&dev->lock);

	return 0;
}

static int snd_em28xx_hw_capture_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	int ret;

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

	dprintk("Stop capture, if needed\n");

	if (atomic_read(&dev->stream_started) > 0) {
		atomic_set(&dev->stream_started, 0);
		schedule_work(&dev->wq_trigger);
	}

	return 0;
}

static int snd_em28xx_prepare(struct snd_pcm_substream *substream)
{
	struct em28xx *dev = snd_pcm_substream_chip(substream);

	dev->adev.hwptr_done_capture = 0;
	dev->adev.capture_transfer_done = 0;

	return 0;
}

static void audio_trigger(struct work_struct *work)
{
	struct em28xx *dev = container_of(work, struct em28xx, wq_trigger);

	if (atomic_read(&dev->stream_started)) {
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

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE: /* fall through */
	case SNDRV_PCM_TRIGGER_RESUME: /* fall through */
	case SNDRV_PCM_TRIGGER_START:
		atomic_set(&dev->stream_started, 1);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH: /* fall through */
	case SNDRV_PCM_TRIGGER_SUSPEND: /* fall through */
	case SNDRV_PCM_TRIGGER_STOP:
		atomic_set(&dev->stream_started, 0);
		break;
	default:
		retval = -EINVAL;
	}
	schedule_work(&dev->wq_trigger);
	return retval;
}

static snd_pcm_uframes_t snd_em28xx_capture_pointer(struct snd_pcm_substream
						    *substream)
{
	unsigned long flags;
	struct em28xx *dev;
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

/*
 * AC97 volume control support
 */
static int em28xx_vol_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *info)
{
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
	u16 val = (0x1f - (value->value.integer.value[0] & 0x1f)) |
		  (0x1f - (value->value.integer.value[1] & 0x1f)) << 8;
	int rc;

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
	int val;

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
	value->value.integer.value[1] = 0x1f - ((val << 8) & 0x1f);

	return 0;
}

static int em28xx_vol_put_mute(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *value)
{
	struct em28xx *dev = snd_kcontrol_chip(kcontrol);
	u16 val = value->value.integer.value[0];
	int rc;

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
	int val;

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

	memset (&tmp, 0, sizeof(tmp));
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

	memset (&tmp, 0, sizeof(tmp));
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
static struct snd_pcm_ops snd_em28xx_pcm_capture = {
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

static int em28xx_audio_init(struct em28xx *dev)
{
	struct em28xx_audio *adev = &dev->adev;
	struct snd_pcm      *pcm;
	struct snd_card     *card;
	static int          devnr;
	int                 err;

	if (!dev->has_alsa_audio || dev->audio_ifnum < 0) {
		/* This device does not support the extension (in this case
		   the device is expecting the snd-usb-audio module or
		   doesn't have analog audio support at all) */
		return 0;
	}

	printk(KERN_INFO "em28xx-audio.c: probing for em28xx Audio Vendor Class\n");
	printk(KERN_INFO "em28xx-audio.c: Copyright (C) 2006 Markus "
			 "Rechberger\n");
	printk(KERN_INFO "em28xx-audio.c: Copyright (C) 2007-2011 Mauro Carvalho Chehab\n");

	err = snd_card_create(index[devnr], "Em28xx Audio", THIS_MODULE, 0,
			      &card);
	if (err < 0)
		return err;

	spin_lock_init(&adev->slock);
	err = snd_pcm_new(card, "Em28xx Audio", 0, 0, 1, &pcm);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_em28xx_pcm_capture);
	pcm->info_flags = 0;
	pcm->private_data = dev;
	strcpy(pcm->name, "Empia 28xx Capture");

	snd_card_set_dev(card, &dev->udev->dev);
	strcpy(card->driver, "Em28xx-Audio");
	strcpy(card->shortname, "Em28xx Audio");
	strcpy(card->longname, "Empia Em28xx Audio");

	INIT_WORK(&dev->wq_trigger, audio_trigger);

	if (dev->audio_mode.ac97 != EM28XX_NO_AC97) {
		em28xx_cvol_new(card, dev, "Video", AC97_VIDEO_VOL);
		em28xx_cvol_new(card, dev, "Line In", AC97_LINEIN_VOL);
		em28xx_cvol_new(card, dev, "Phone", AC97_PHONE_VOL);
		em28xx_cvol_new(card, dev, "Microphone", AC97_PHONE_VOL);
		em28xx_cvol_new(card, dev, "CD", AC97_CD_VOL);
		em28xx_cvol_new(card, dev, "AUX", AC97_AUX_VOL);
		em28xx_cvol_new(card, dev, "PCM", AC97_PCM_OUT_VOL);

		em28xx_cvol_new(card, dev, "Master", AC97_MASTER_VOL);
		em28xx_cvol_new(card, dev, "Line", AC97_LINE_LEVEL_VOL);
		em28xx_cvol_new(card, dev, "Mono", AC97_MASTER_MONO_VOL);
		em28xx_cvol_new(card, dev, "LFE", AC97_LFE_MASTER_VOL);
		em28xx_cvol_new(card, dev, "Surround", AC97_SURR_MASTER_VOL);
	}

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}
	adev->sndcard = card;
	adev->udev = dev->udev;

	return 0;
}

static int em28xx_audio_fini(struct em28xx *dev)
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
		dev->adev.sndcard = NULL;
	}

	return 0;
}

static struct em28xx_ops audio_ops = {
	.id   = EM28XX_AUDIO,
	.name = "Em28xx Audio Extension",
	.init = em28xx_audio_init,
	.fini = em28xx_audio_fini,
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
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_DESCRIPTION("Em28xx Audio driver");

module_init(em28xx_alsa_register);
module_exit(em28xx_alsa_unregister);
