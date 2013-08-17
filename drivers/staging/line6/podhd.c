/*
 * Line6 Pod HD
 *
 * Copyright (C) 2011 Stefan Hajnoczi <stefanha@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <sound/core.h>
#include <sound/pcm.h>

#include "audio.h"
#include "driver.h"
#include "pcm.h"
#include "podhd.h"

#define PODHD_BYTES_PER_FRAME 6	/* 24bit audio (stereo) */

static struct snd_ratden podhd_ratden = {
	.num_min = 48000,
	.num_max = 48000,
	.num_step = 1,
	.den = 1,
};

static struct line6_pcm_properties podhd_pcm_properties = {
	.snd_line6_playback_hw = {
				  .info = (SNDRV_PCM_INFO_MMAP |
					   SNDRV_PCM_INFO_INTERLEAVED |
					   SNDRV_PCM_INFO_BLOCK_TRANSFER |
					   SNDRV_PCM_INFO_MMAP_VALID |
					   SNDRV_PCM_INFO_PAUSE |
#ifdef CONFIG_PM
					   SNDRV_PCM_INFO_RESUME |
#endif
					   SNDRV_PCM_INFO_SYNC_START),
				  .formats = SNDRV_PCM_FMTBIT_S24_3LE,
				  .rates = SNDRV_PCM_RATE_48000,
				  .rate_min = 48000,
				  .rate_max = 48000,
				  .channels_min = 2,
				  .channels_max = 2,
				  .buffer_bytes_max = 60000,
				  .period_bytes_min = 64,
				  .period_bytes_max = 8192,
				  .periods_min = 1,
				  .periods_max = 1024},
	.snd_line6_capture_hw = {
				 .info = (SNDRV_PCM_INFO_MMAP |
					  SNDRV_PCM_INFO_INTERLEAVED |
					  SNDRV_PCM_INFO_BLOCK_TRANSFER |
					  SNDRV_PCM_INFO_MMAP_VALID |
#ifdef CONFIG_PM
					  SNDRV_PCM_INFO_RESUME |
#endif
					  SNDRV_PCM_INFO_SYNC_START),
				 .formats = SNDRV_PCM_FMTBIT_S24_3LE,
				 .rates = SNDRV_PCM_RATE_48000,
				 .rate_min = 48000,
				 .rate_max = 48000,
				 .channels_min = 2,
				 .channels_max = 2,
				 .buffer_bytes_max = 60000,
				 .period_bytes_min = 64,
				 .period_bytes_max = 8192,
				 .periods_min = 1,
				 .periods_max = 1024},
	.snd_line6_rates = {
			    .nrats = 1,
			    .rats = &podhd_ratden},
	.bytes_per_frame = PODHD_BYTES_PER_FRAME
};

/*
	POD HD destructor.
*/
static void podhd_destruct(struct usb_interface *interface)
{
	struct usb_line6_podhd *podhd = usb_get_intfdata(interface);

	if (podhd == NULL)
		return;
	line6_cleanup_audio(&podhd->line6);
}

/*
	Try to init POD HD device.
*/
static int podhd_try_init(struct usb_interface *interface,
			  struct usb_line6_podhd *podhd)
{
	int err;
	struct usb_line6 *line6 = &podhd->line6;

	if ((interface == NULL) || (podhd == NULL))
		return -ENODEV;

	/* initialize audio system: */
	err = line6_init_audio(line6);
	if (err < 0)
		return err;

	/* initialize MIDI subsystem: */
	err = line6_init_midi(line6);
	if (err < 0)
		return err;

	/* initialize PCM subsystem: */
	err = line6_init_pcm(line6, &podhd_pcm_properties);
	if (err < 0)
		return err;

	/* register USB audio system: */
	err = line6_register_audio(line6);
	return err;
}

/*
	Init POD HD device (and clean up in case of failure).
*/
int line6_podhd_init(struct usb_interface *interface,
		     struct usb_line6_podhd *podhd)
{
	int err = podhd_try_init(interface, podhd);

	if (err < 0)
		podhd_destruct(interface);

	return err;
}

/*
	POD HD device disconnected.
*/
void line6_podhd_disconnect(struct usb_interface *interface)
{
	struct usb_line6_podhd *podhd;

	if (interface == NULL)
		return;
	podhd = usb_get_intfdata(interface);

	if (podhd != NULL) {
		struct snd_line6_pcm *line6pcm = podhd->line6.line6pcm;

		if (line6pcm != NULL)
			line6_pcm_disconnect(line6pcm);
	}

	podhd_destruct(interface);
}
