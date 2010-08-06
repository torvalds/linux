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

#include <linux/slab.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "audio.h"
#include "capture.h"
#include "playback.h"
#include "pod.h"


/* trigger callback */
int snd_line6_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *s;
	int err;
	unsigned long flags;

	spin_lock_irqsave(&line6pcm->lock_trigger, flags);
	clear_bit(BIT_PREPARED, &line6pcm->flags);

	snd_pcm_group_for_each_entry(s, substream) {
		switch (s->stream) {
		case SNDRV_PCM_STREAM_PLAYBACK:
			err = snd_line6_playback_trigger(s, cmd);

			if (err < 0) {
				spin_unlock_irqrestore(&line6pcm->lock_trigger,
						       flags);
				return err;
			}

			break;

		case SNDRV_PCM_STREAM_CAPTURE:
			err = snd_line6_capture_trigger(s, cmd);

			if (err < 0) {
				spin_unlock_irqrestore(&line6pcm->lock_trigger,
						       flags);
				return err;
			}

			break;

		default:
			dev_err(s2m(substream), "Unknown stream direction %d\n",
				s->stream);
		}
	}

	spin_unlock_irqrestore(&line6pcm->lock_trigger, flags);
	return 0;
}

/* control info callback */
static int snd_line6_control_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 256;
	return 0;
}

/* control get callback */
static int snd_line6_control_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int i;
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	for (i = 2; i--;)
		ucontrol->value.integer.value[i] = line6pcm->volume[i];

	return 0;
}

/* control put callback */
static int snd_line6_control_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int i, changed = 0;
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	for (i = 2; i--;)
		if (line6pcm->volume[i] != ucontrol->value.integer.value[i]) {
			line6pcm->volume[i] = ucontrol->value.integer.value[i];
			changed = 1;
		}

	return changed;
}

/* control definition */
static struct snd_kcontrol_new line6_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PCM Playback Volume",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_line6_control_info,
	.get = snd_line6_control_get,
	.put = snd_line6_control_put
};

/*
	Cleanup the PCM device.
*/
static void line6_cleanup_pcm(struct snd_pcm *pcm)
{
	int i;
	struct snd_line6_pcm *line6pcm = snd_pcm_chip(pcm);

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
					snd_dma_continuous_data(GFP_KERNEL),
					64 * 1024, 128 * 1024);

	return 0;
}

/* PCM device destructor */
static int snd_line6_pcm_free(struct snd_device *device)
{
	return 0;
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
		return 0;  /* skip PCM initialization and report success */

	/* initialize PCM subsystem based on product id: */
	switch (line6->product) {
	case LINE6_DEVID_BASSPODXT:
	case LINE6_DEVID_BASSPODXTLIVE:
	case LINE6_DEVID_BASSPODXTPRO:
	case LINE6_DEVID_PODXT:
	case LINE6_DEVID_PODXTLIVE:
	case LINE6_DEVID_PODXTPRO:
		ep_read  = 0x82;
		ep_write = 0x01;
		break;

	case LINE6_DEVID_PODX3:
	case LINE6_DEVID_PODX3LIVE:
		ep_read  = 0x86;
		ep_write = 0x02;
		break;

	case LINE6_DEVID_POCKETPOD:
		ep_read  = 0x82;
		ep_write = 0x02;
		break;

	case LINE6_DEVID_GUITARPORT:
	case LINE6_DEVID_TONEPORT_GX:
		ep_read  = 0x82;
		ep_write = 0x01;
		break;

	case LINE6_DEVID_TONEPORT_UX1:
		ep_read  = 0x00;
		ep_write = 0x00;
		break;

	case LINE6_DEVID_TONEPORT_UX2:
		ep_read  = 0x87;
		ep_write = 0x00;
		break;

	default:
		MISSING_CASE;
	}

	line6pcm = kzalloc(sizeof(struct snd_line6_pcm), GFP_KERNEL);

	if (line6pcm == NULL)
		return -ENOMEM;

	line6pcm->volume[0] = line6pcm->volume[1] = 128;
	line6pcm->line6 = line6;
	line6pcm->ep_audio_read = ep_read;
	line6pcm->ep_audio_write = ep_write;
	line6pcm->max_packet_size = usb_maxpacket(line6->usbdev,
						 usb_rcvintpipe(line6->usbdev,
								ep_read),
						  0);
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

	err = create_audio_out_urbs(line6pcm);
	if (err < 0)
		return err;

	err = create_audio_in_urbs(line6pcm);
	if (err < 0)
		return err;

	/* mixer: */
	err = snd_ctl_add(line6->card, snd_ctl_new1(&line6_control, line6pcm));
	if (err < 0)
		return err;

	return 0;
}

/* prepare pcm callback */
int snd_line6_prepare(struct snd_pcm_substream *substream)
{
	struct snd_line6_pcm *line6pcm = snd_pcm_substream_chip(substream);

	if (!test_and_set_bit(BIT_PREPARED, &line6pcm->flags)) {
		unlink_wait_clear_audio_out_urbs(line6pcm);
		line6pcm->pos_out = 0;
		line6pcm->pos_out_done = 0;

		unlink_wait_clear_audio_in_urbs(line6pcm);
		line6pcm->bytes_out = 0;
		line6pcm->pos_in_done = 0;
		line6pcm->bytes_in = 0;
	}

	return 0;
}
