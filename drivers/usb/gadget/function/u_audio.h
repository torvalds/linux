/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * u_audio.h -- interface to USB gadget "ALSA sound card" utilities
 *
 * Copyright (C) 2016
 * Author: Ruslan Bilovol <ruslan.bilovol@gmail.com>
 */

#ifndef __U_AUDIO_H
#define __U_AUDIO_H

#include <linux/usb/composite.h>

/*
 * Same maximum frequency deviation on the slower side as in
 * sound/usb/endpoint.c. Value is expressed in per-mil deviation.
 * The maximum deviation on the faster side will be provided as
 * parameter, as it impacts the endpoint required bandwidth.
 */
#define FBACK_SLOW_MAX	250

/* Feature Unit parameters */
struct uac_fu_params {
	int id;			/* Feature Unit ID */

	bool mute_present;	/* mute control enable */

	bool volume_present;	/* volume control enable */
	s16 volume_min;		/* min volume in 1/256 dB */
	s16 volume_max;		/* max volume in 1/256 dB */
	s16 volume_res;		/* volume resolution in 1/256 dB */
};

struct uac_params {
	/* playback */
	int p_chmask;	/* channel mask */
	int p_srate;	/* rate in Hz */
	int p_ssize;	/* sample size */
	struct uac_fu_params p_fu;	/* Feature Unit parameters */

	/* capture */
	int c_chmask;	/* channel mask */
	int c_srate;	/* rate in Hz */
	int c_ssize;	/* sample size */
	struct uac_fu_params c_fu;	/* Feature Unit parameters */

	int req_number; /* number of preallocated requests */
	int fb_max;	/* upper frequency drift feedback limit per-mil */
};

struct g_audio {
	struct usb_function func;
	struct usb_gadget *gadget;

	struct usb_ep *in_ep;

	struct usb_ep *out_ep;
	/* feedback IN endpoint corresponding to out_ep */
	struct usb_ep *in_ep_fback;

	/* Max packet size for all in_ep possible speeds */
	unsigned int in_ep_maxpsize;
	/* Max packet size for all out_ep possible speeds */
	unsigned int out_ep_maxpsize;

	/* Notify UAC driver about control change */
	int (*notify)(struct g_audio *g_audio, int unit_id, int cs);

	/* The ALSA Sound Card it represents on the USB-Client side */
	struct snd_uac_chip *uac;

	struct uac_params params;
};

static inline struct g_audio *func_to_g_audio(struct usb_function *f)
{
	return container_of(f, struct g_audio, func);
}

static inline uint num_channels(uint chanmask)
{
	uint num = 0;

	while (chanmask) {
		num += (chanmask & 1);
		chanmask >>= 1;
	}

	return num;
}

/*
 * g_audio_setup - initialize one virtual ALSA sound card
 * @g_audio: struct with filled params, in_ep_maxpsize, out_ep_maxpsize
 * @pcm_name: the id string for a PCM instance of this sound card
 * @card_name: name of this soundcard
 *
 * This sets up the single virtual ALSA sound card that may be exported by a
 * gadget driver using this framework.
 *
 * Context: may sleep
 *
 * Returns zero on success, or a negative error on failure.
 */
int g_audio_setup(struct g_audio *g_audio, const char *pcm_name,
					const char *card_name);
void g_audio_cleanup(struct g_audio *g_audio);

int u_audio_start_capture(struct g_audio *g_audio);
void u_audio_stop_capture(struct g_audio *g_audio);
int u_audio_start_playback(struct g_audio *g_audio);
void u_audio_stop_playback(struct g_audio *g_audio);

int u_audio_get_volume(struct g_audio *g_audio, int playback, s16 *val);
int u_audio_set_volume(struct g_audio *g_audio, int playback, s16 val);
int u_audio_get_mute(struct g_audio *g_audio, int playback, int *val);
int u_audio_set_mute(struct g_audio *g_audio, int playback, int val);

#endif /* __U_AUDIO_H */
