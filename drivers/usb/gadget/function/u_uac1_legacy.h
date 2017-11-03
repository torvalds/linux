// SPDX-License-Identifier: GPL-2.0+
/*
 * u_uac1.h -- interface to USB gadget "ALSA AUDIO" utilities
 *
 * Copyright (C) 2008 Bryan Wu <cooloney@kernel.org>
 * Copyright (C) 2008 Analog Devices, Inc
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __U_UAC1_LEGACY_H
#define __U_UAC1_LEGACY_H

#include <linux/device.h>
#include <linux/err.h>
#include <linux/usb/audio.h>
#include <linux/usb/composite.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#define FILE_PCM_PLAYBACK	"/dev/snd/pcmC0D0p"
#define FILE_PCM_CAPTURE	"/dev/snd/pcmC0D0c"
#define FILE_CONTROL		"/dev/snd/controlC0"

#define UAC1_OUT_EP_MAX_PACKET_SIZE	200
#define UAC1_REQ_COUNT			256
#define UAC1_AUDIO_BUF_SIZE		48000

/*
 * This represents the USB side of an audio card device, managed by a USB
 * function which provides control and stream interfaces.
 */

struct gaudio_snd_dev {
	struct gaudio			*card;
	struct file			*filp;
	struct snd_pcm_substream	*substream;
	int				access;
	int				format;
	int				channels;
	int				rate;
};

struct gaudio {
	struct usb_function		func;
	struct usb_gadget		*gadget;

	/* ALSA sound device interfaces */
	struct gaudio_snd_dev		control;
	struct gaudio_snd_dev		playback;
	struct gaudio_snd_dev		capture;

	/* TODO */
};

struct f_uac1_legacy_opts {
	struct usb_function_instance	func_inst;
	int				req_buf_size;
	int				req_count;
	int				audio_buf_size;
	char				*fn_play;
	char				*fn_cap;
	char				*fn_cntl;
	unsigned			bound:1;
	unsigned			fn_play_alloc:1;
	unsigned			fn_cap_alloc:1;
	unsigned			fn_cntl_alloc:1;
	struct mutex			lock;
	int				refcnt;
};

int gaudio_setup(struct gaudio *card);
void gaudio_cleanup(struct gaudio *the_card);

size_t u_audio_playback(struct gaudio *card, void *buf, size_t count);
int u_audio_get_playback_channels(struct gaudio *card);
int u_audio_get_playback_rate(struct gaudio *card);

#endif /* __U_UAC1_LEGACY_H */
