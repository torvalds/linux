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
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "audio.h"
#include "capture.h"
#include "driver.h"
#include "playback.h"
#include "pod.h"

#ifdef CONFIG_LINE6_USB_IMPULSE_RESPONSE

static struct snd_line6_pcm *dev2pcm(struct device *dev)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6 *line6 = usb_get_intfdata(interface);
	struct snd_line6_pcm *line6pcm = line6->line6pcm;
	return line6pcm;
}

/*
	"read" request on "impulse_volume" special file.
*/
static ssize_t pcm_get_impulse_volume(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dev2pcm(dev)->impulse_volume);
}

/*
	"write" request on "impulse_volume" special file.
*/
static ssize_t pcm_set_impulse_volume(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct snd_line6_pcm *line6pcm = dev2pcm(dev);
	int value;
	int ret;

	ret = kstrtoint(buf, 10, &value);
	if (ret < 0)
		return ret;

	line6pcm->impulse_volume = value;

	if (value > 0)
		line6_pcm_acquire(line6pcm, LINE6_BITS_PCM_IMPULSE);
	else
		line6_pcm_release(line6pcm, LINE6_BITS_PCM_IMPULSE);

	return count;
}

/*
	"read" request on "impulse_period" special file.
*/
static ssize_t pcm_get_impulse_period(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dev2pcm(dev)->impulse_period);
}

/*
	"write" request on "impulse_period" special file.
*/
static ssize_t pcm_set_impulse_period(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int value;
	int ret;

	ret = kstrtoint(buf, 10, &value);
	if (ret < 0)
		return ret;

	dev2pcm(dev)->impulse_period = value;
	return count;
}

static DEVICE_ATTR(impulse_volume, S_IWUSR | S_IRUGO, pcm_get_impulse_volume,
		   pcm_set_impulse_volume);
static DEVICE_ATTR(impulse_period, S_IWUSR | S_IRUGO, pcm_get_impulse_period,
		   pcm_set_impulse_period);

#endif

static bool test_flags(unsigned long flags0, unsigned long flags1,
		       unsigned long mask)
{
	return ((flags0 & mask) == 0) && ((flags1 & mask) != 0);
}

int line6_pcm_acquire(struct snd_line6_pcm *line6pcm, int channels)
{
	unsigned long flags_old =
	    __sync_fetch_and_or(&line6pcm->flags, channels);
	unsigned long flags_new = flags_old | channels;
	unsigned long flags_final = flags_old;
	int err = 0;

	line6pcm->prev_fbuf = NULL;

	if (test_flags(flags_old, flags_new, LINE6_BITS_CAPTURE_BUFFER)) {
		/* Invoked multiple times in a row so allocate once only */
		if (!line6pcm->buffer_in) {
			line6pcm->buffer_in =
				kmalloc(LINE6_ISO_BUFFERS * LINE6_ISO_PACKETS *
					line6pcm->max_packet_size, GFP_KERNEL);
			if (!line6pcm->buffer_in) {
				err = -ENOMEM;
				goto pcm_acquire_error;
			}

			flags_final |= channels & LINE6_BITS_CAPTURE_BUFFER;
		}
	}

	if (test_flags(flags_old, flags_new, LINE6_BITS_CAPTURE_STREAM)) {
		/*
		   Waiting for completion of active URBs in the stop handler is
		   a bug, we therefore report an error if capturing is restarted
		   too soon.
		 */
		if (line6pcm->active_urb_in | line6pcm->unlink_urb_in) {
			dev_err(line6pcm->line6->ifcdev, "Device not yet ready\n");
			return -EBUSY;
		}

		line6pcm->count_in = 0;
		line6pcm->prev_fsize = 0;
		err = line6_submit_audio_in_all_urbs(line6pcm);

		if (err < 0)
			goto pcm_acquire_error;

		flags_final |= channels & LINE6_BITS_CAPTURE_STREAM;
	}

	if (test_flags(flags_old, flags_new, LINE6_BITS_PLAYBACK_BUFFER)) {
		/* Invoked multiple times in a row so allocate once only */
		if (!line6pcm->buffer_out) {
			line6pcm->buffer_out =
				kmalloc(LINE6_ISO_BUFFERS * LINE6_ISO_PACKETS *
					line6pcm->max_packet_size, GFP_KERNEL);
			if (!line6pcm->buffer_out) {
				err = -ENOMEM;
				goto pcm_acquire_error;
			}

			flags_final |= channels & LINE6_BITS_PLAYBACK_BUFFER;
		}
	}

	if (test_flags(flags_old, flags_new, LINE6_BITS_PLAYBACK_STREAM)) {
		/*
		  See comment above regarding PCM restart.
		*/
		if (line6pcm->active_urb_out | line6pcm->unlink_urb_out) {
			dev_err(line6pcm->line6->ifcdev, "Device not yet ready\n");
			return -EBUSY;
		}

		line6pcm->count_out = 0;
		err = line6_submit_audio_out_all_urbs(line6pcm);

		if (err < 0)
			goto pcm_acquire_error;

		flags_final |= channels & LINE6_BITS_PLAYBACK_STREAM;
	}

	return 0;

pcm_acquire_error:
	/*
	   If not all requested resources/streams could be obtained, release
	   those which were successfully obtained (if any).
	*/
	line6_pcm_release(line6pcm, flags_final & channels);
	return err;
}

int line6_pcm_release(struct snd_line6_pcm *line6pcm, int channels)
{
	unsigned long flags_old =
	    __sync_fetch_and_and(&line6pcm->flags, ~channels);
	unsigned long flags_new = flags_old & ~channels;

	if (test_flags(flags_new, flags_old, LINE6_BITS_CAPTURE_STREAM))
		line6_unlink_audio_in_urbs(line6pcm);

	if (test_flags(flags_new, flags_old, LINE6_BITS_CAPTURE_BUFFER)) {
		line6_wait_clear_audio_in_urbs(line6pcm);
		line6_free_capture_buffer(line6pcm);
	}

	if (test_flags(flags_new, flags_old, LINE6_BITS_PLAYBACK_STREAM))
		line6_unlink_audio_out_urbs(line6pcm);

	if (test_flags(flags_new, flags_old, LINE6_BITS_PLAYBACK_BUFFER)) {
		line6_wait_clear_audio_out_urbs(line6pcm);
		line6_free_playback_buffer(line6pcm);
	}

	return 0;
}

/* trigger callback */
int snd_line6_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *s;
	int err;
	unsigned long flags;

	spin_lock_irqsave(&line6pcm->lock_trigger, flags);
	clear_bit(LINE6_INDEX_PREPARED, &line6pcm->flags);

	snd_pcm_group_for_each_entry(s, substream) {
		switch (s->stream) {
		case SNDRV_PCM_STREAM_PLAYBACK:
			err = snd_line6_playback_trigger(line6pcm, cmd);

			if (err < 0) {
				spin_unlock_irqrestore(&line6pcm->lock_trigger,
						       flags);
				return err;
			}

			break;

		case SNDRV_PCM_STREAM_CAPTURE:
			err = snd_line6_capture_trigger(line6pcm, cmd);

			if (err < 0) {
				spin_unlock_irqrestore(&line6pcm->lock_trigger,
						       flags);
				return err;
			}

			break;

		default:
			dev_err(line6pcm->line6->ifcdev,
				"Unknown stream direction %d\n", s->stream);
		}
	}

	spin_unlock_irqrestore(&line6pcm->lock_trigger, flags);
	return 0;
}

/* control info callback */
static int snd_line6_control_playback_info(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 256;
	return 0;
}

/* control get callback */
static int snd_line6_control_playback_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	int i;
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	for (i = 2; i--;)
		ucontrol->value.integer.value[i] = line6pcm->volume_playback[i];

	return 0;
}

/* control put callback */
static int snd_line6_control_playback_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	int i, changed = 0;
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	for (i = 2; i--;)
		if (line6pcm->volume_playback[i] !=
		    ucontrol->value.integer.value[i]) {
			line6pcm->volume_playback[i] =
			    ucontrol->value.integer.value[i];
			changed = 1;
		}

	return changed;
}

/* control definition */
static struct snd_kcontrol_new line6_control_playback = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PCM Playback Volume",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_line6_control_playback_info,
	.get = snd_line6_control_playback_get,
	.put = snd_line6_control_playback_put
};

/*
	Cleanup the PCM device.
*/
static void line6_cleanup_pcm(struct snd_pcm *pcm)
{
	int i;
	struct snd_line6_pcm *line6pcm = snd_pcm_chip(pcm);

#ifdef CONFIG_LINE6_USB_IMPULSE_RESPONSE
	device_remove_file(line6pcm->line6->ifcdev, &dev_attr_impulse_volume);
	device_remove_file(line6pcm->line6->ifcdev, &dev_attr_impulse_period);
#endif

	for (i = LINE6_ISO_BUFFERS; i--;) {
		if (line6pcm->urb_audio_out[i]) {
			usb_kill_urb(line6pcm->urb_audio_out[i]);
			usb_free_urb(line6pcm->urb_audio_out[i]);
		}
		if (line6pcm->urb_audio_in[i]) {
			usb_kill_urb(line6pcm->urb_audio_in[i]);
			usb_free_urb(line6pcm->urb_audio_in[i]);
		}
	}
}

/* create a PCM device */
static int snd_line6_new_pcm(struct snd_line6_pcm *line6pcm)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(line6pcm->line6->card,
			  (char *)line6pcm->line6->properties->name,
			  0, 1, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = line6pcm;
	pcm->private_free = line6_cleanup_pcm;
	line6pcm->pcm = pcm;
	strcpy(pcm->name, line6pcm->line6->properties->name);

	/* set operators */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_line6_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_line6_capture_ops);

	/* pre-allocation of buffers */
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
					      snd_dma_continuous_data
					      (GFP_KERNEL), 64 * 1024,
					      128 * 1024);

	return 0;
}

/* PCM device destructor */
static int snd_line6_pcm_free(struct snd_device *device)
{
	return 0;
}

/*
	Stop substream if still running.
*/
static void pcm_disconnect_substream(struct snd_pcm_substream *substream)
{
	if (substream->runtime && snd_pcm_running(substream)) {
		snd_pcm_stream_lock_irq(substream);
		snd_pcm_stop(substream, SNDRV_PCM_STATE_DISCONNECTED);
		snd_pcm_stream_unlock_irq(substream);
	}
}

/*
	Stop PCM stream.
*/
void line6_pcm_disconnect(struct snd_line6_pcm *line6pcm)
{
	pcm_disconnect_substream(get_substream
				 (line6pcm, SNDRV_PCM_STREAM_CAPTURE));
	pcm_disconnect_substream(get_substream
				 (line6pcm, SNDRV_PCM_STREAM_PLAYBACK));
	line6_unlink_wait_clear_audio_out_urbs(line6pcm);
	line6_unlink_wait_clear_audio_in_urbs(line6pcm);
}

/*
	Create and register the PCM device and mixer entries.
	Create URBs for playback and capture.
*/
int line6_init_pcm(struct usb_line6 *line6,
		   struct line6_pcm_properties *properties)
{
	static struct snd_device_ops pcm_ops = {
		.dev_free = snd_line6_pcm_free,
	};

	int err;
	int ep_read = 0, ep_write = 0;
	struct snd_line6_pcm *line6pcm;

	if (!(line6->properties->capabilities & LINE6_BIT_PCM))
		return 0;	/* skip PCM initialization and report success */

	/* initialize PCM subsystem based on product id: */
	switch (line6->product) {
	case LINE6_DEVID_BASSPODXT:
	case LINE6_DEVID_BASSPODXTLIVE:
	case LINE6_DEVID_BASSPODXTPRO:
	case LINE6_DEVID_PODXT:
	case LINE6_DEVID_PODXTLIVE:
	case LINE6_DEVID_PODXTPRO:
	case LINE6_DEVID_PODHD300:
		ep_read = 0x82;
		ep_write = 0x01;
		break;

	case LINE6_DEVID_PODHD500:
	case LINE6_DEVID_PODX3:
	case LINE6_DEVID_PODX3LIVE:
		ep_read = 0x86;
		ep_write = 0x02;
		break;

	case LINE6_DEVID_POCKETPOD:
		ep_read = 0x82;
		ep_write = 0x02;
		break;

	case LINE6_DEVID_GUITARPORT:
	case LINE6_DEVID_PODSTUDIO_GX:
	case LINE6_DEVID_PODSTUDIO_UX1:
	case LINE6_DEVID_PODSTUDIO_UX2:
	case LINE6_DEVID_TONEPORT_GX:
	case LINE6_DEVID_TONEPORT_UX1:
	case LINE6_DEVID_TONEPORT_UX2:
		ep_read = 0x82;
		ep_write = 0x01;
		break;

	/* this is for interface_number == 1:
	case LINE6_DEVID_TONEPORT_UX2:
	case LINE6_DEVID_PODSTUDIO_UX2:
		ep_read  = 0x87;
		ep_write = 0x00;
		break; */

	default:
		MISSING_CASE;
	}

	line6pcm = kzalloc(sizeof(struct snd_line6_pcm), GFP_KERNEL);

	if (line6pcm == NULL)
		return -ENOMEM;

	line6pcm->volume_playback[0] = line6pcm->volume_playback[1] = 255;
	line6pcm->volume_monitor = 255;
	line6pcm->line6 = line6;
	line6pcm->ep_audio_read = ep_read;
	line6pcm->ep_audio_write = ep_write;

	/* Read and write buffers are sized identically, so choose minimum */
	line6pcm->max_packet_size = min(
			usb_maxpacket(line6->usbdev,
				usb_rcvisocpipe(line6->usbdev, ep_read), 0),
			usb_maxpacket(line6->usbdev,
				usb_sndisocpipe(line6->usbdev, ep_write), 1));

	line6pcm->properties = properties;
	line6->line6pcm = line6pcm;

	/* PCM device: */
	err = snd_device_new(line6->card, SNDRV_DEV_PCM, line6, &pcm_ops);
	if (err < 0)
		return err;

	snd_card_set_dev(line6->card, line6->ifcdev);

	err = snd_line6_new_pcm(line6pcm);
	if (err < 0)
		return err;

	spin_lock_init(&line6pcm->lock_audio_out);
	spin_lock_init(&line6pcm->lock_audio_in);
	spin_lock_init(&line6pcm->lock_trigger);

	err = line6_create_audio_out_urbs(line6pcm);
	if (err < 0)
		return err;

	err = line6_create_audio_in_urbs(line6pcm);
	if (err < 0)
		return err;

	/* mixer: */
	err =
	    snd_ctl_add(line6->card,
			snd_ctl_new1(&line6_control_playback, line6pcm));
	if (err < 0)
		return err;

#ifdef CONFIG_LINE6_USB_IMPULSE_RESPONSE
	/* impulse response test: */
	err = device_create_file(line6->ifcdev, &dev_attr_impulse_volume);
	if (err < 0)
		return err;

	err = device_create_file(line6->ifcdev, &dev_attr_impulse_period);
	if (err < 0)
		return err;

	line6pcm->impulse_period = LINE6_IMPULSE_DEFAULT_PERIOD;
#endif

	return 0;
}

/* prepare pcm callback */
int snd_line6_prepare(struct snd_pcm_substream *substream)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		if ((line6pcm->flags & LINE6_BITS_PLAYBACK_STREAM) == 0)
			line6_unlink_wait_clear_audio_out_urbs(line6pcm);

		break;

	case SNDRV_PCM_STREAM_CAPTURE:
		if ((line6pcm->flags & LINE6_BITS_CAPTURE_STREAM) == 0)
			line6_unlink_wait_clear_audio_in_urbs(line6pcm);

		break;

	default:
		MISSING_CASE;
	}

	if (!test_and_set_bit(LINE6_INDEX_PREPARED, &line6pcm->flags)) {
		line6pcm->count_out = 0;
		line6pcm->pos_out = 0;
		line6pcm->pos_out_done = 0;
		line6pcm->bytes_out = 0;
		line6pcm->count_in = 0;
		line6pcm->pos_in_done = 0;
		line6pcm->bytes_in = 0;
	}

	return 0;
}
