/*
 *  ALSA PCM device for the
 *  ALSA interface to cx18 PCM capture streams
 *
 *  Copyright (C) 2009  Andy Walls <awalls@md.metrocast.net>
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

int snd_cx18_pcm_create(struct snd_cx18_card *cxsc);

/* Used by cx18-mailbox to announce the PCM data to the module */
void cx18_alsa_announce_pcm_data(struct snd_cx18_card *card, u8 *pcm_data,
				 size_t num_bytes);
