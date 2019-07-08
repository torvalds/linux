/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  ALSA PCM device for the
 *  ALSA interface to cx18 PCM capture streams
 *
 *  Copyright (C) 2009  Andy Walls <awalls@md.metrocast.net>
 */

int snd_cx18_pcm_create(struct snd_cx18_card *cxsc);

/* Used by cx18-mailbox to announce the PCM data to the module */
void cx18_alsa_announce_pcm_data(struct snd_cx18_card *card, u8 *pcm_data,
				 size_t num_bytes);
