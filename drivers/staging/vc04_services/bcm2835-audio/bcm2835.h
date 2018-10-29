/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2011 Broadcom Corporation.  All rights reserved. */

#ifndef __SOUND_ARM_BCM2835_H
#define __SOUND_ARM_BCM2835_H

#include <linux/device.h>
#include <linux/wait.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm-indirect.h>
#include "interface/vchi/vchi.h"

#define MAX_SUBSTREAMS   (8)
#define AVAIL_SUBSTREAMS_MASK  (0xff)

enum {
	CTRL_VOL_MUTE,
	CTRL_VOL_UNMUTE
};

/* macros for alsa2chip and chip2alsa, instead of functions */

// convert alsa to chip volume (defined as macro rather than function call)
#define alsa2chip(vol) (uint)(-(((vol) << 8) / 100))

// convert chip to alsa volume
#define chip2alsa(vol) -(((vol) * 100) >> 8)

#define CHIP_MIN_VOLUME		26214 /* minimum level aka mute */

/* Some constants for values .. */
enum snd_bcm2835_route {
	AUDIO_DEST_AUTO = 0,
	AUDIO_DEST_HEADPHONES = 1,
	AUDIO_DEST_HDMI = 2,
	AUDIO_DEST_MAX,
};

enum snd_bcm2835_ctrl {
	PCM_PLAYBACK_VOLUME,
	PCM_PLAYBACK_MUTE,
	PCM_PLAYBACK_DEVICE,
};

struct bcm2835_vchi_ctx {
	VCHI_INSTANCE_T vchi_instance;
};

/* definition of the chip-specific record */
struct bcm2835_chip {
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm *pcm_spdif;
	struct device *dev;
	struct bcm2835_alsa_stream *alsa_stream[MAX_SUBSTREAMS];

	int volume;
	int dest;
	int mute;

	unsigned int opened;
	unsigned int spdif_status;
	struct mutex audio_mutex;

	struct bcm2835_vchi_ctx *vchi_ctx;
};

struct bcm2835_alsa_stream {
	struct bcm2835_chip *chip;
	struct snd_pcm_substream *substream;
	struct snd_pcm_indirect pcm_indirect;

	int draining;

	atomic_t pos;
	unsigned int period_offset;
	unsigned int buffer_size;
	unsigned int period_size;

	struct bcm2835_audio_instance *instance;
	int idx;
};

int snd_bcm2835_new_ctl(struct bcm2835_chip *chip);
int snd_bcm2835_new_pcm(struct bcm2835_chip *chip, const char *name,
			int idx, enum snd_bcm2835_route route,
			u32 numchannels, bool spdif);

int snd_bcm2835_new_hdmi_ctl(struct bcm2835_chip *chip);
int snd_bcm2835_new_headphones_ctl(struct bcm2835_chip *chip);

int bcm2835_new_vchi_ctx(struct device *dev, struct bcm2835_vchi_ctx *vchi_ctx);
void bcm2835_free_vchi_ctx(struct bcm2835_vchi_ctx *vchi_ctx);

int bcm2835_audio_open(struct bcm2835_alsa_stream *alsa_stream);
int bcm2835_audio_close(struct bcm2835_alsa_stream *alsa_stream);
int bcm2835_audio_set_params(struct bcm2835_alsa_stream *alsa_stream,
			     unsigned int channels, unsigned int samplerate,
			     unsigned int bps);
int bcm2835_audio_start(struct bcm2835_alsa_stream *alsa_stream);
int bcm2835_audio_stop(struct bcm2835_alsa_stream *alsa_stream);
int bcm2835_audio_drain(struct bcm2835_alsa_stream *alsa_stream);
int bcm2835_audio_set_ctls(struct bcm2835_alsa_stream *alsa_stream);
int bcm2835_audio_write(struct bcm2835_alsa_stream *alsa_stream,
			unsigned int count,
			void *src);
void bcm2835_playback_fifo(struct bcm2835_alsa_stream *alsa_stream,
			   unsigned int size);
unsigned int bcm2835_audio_retrieve_buffers(struct bcm2835_alsa_stream *alsa_stream);

#endif /* __SOUND_ARM_BCM2835_H */
