/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  ALSA interface to cobalt PCM capture streams
 *
 *  Copyright 2014-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

struct snd_card;

struct snd_cobalt_card {
	struct cobalt_stream *s;
	struct snd_card *sc;
	unsigned int capture_transfer_done;
	unsigned int hwptr_done_capture;
	unsigned alsa_record_cnt;
	struct snd_pcm_substream *capture_pcm_substream;

	unsigned int pb_size;
	unsigned int pb_count;
	unsigned int pb_pos;
	unsigned pb_filled;
	bool alsa_pb_channel;
	unsigned alsa_playback_cnt;
	struct snd_pcm_substream *playback_pcm_substream;
};

int cobalt_alsa_init(struct cobalt_stream *s);
void cobalt_alsa_exit(struct cobalt_stream *s);
