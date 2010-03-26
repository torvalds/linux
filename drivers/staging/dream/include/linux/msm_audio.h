/* drivers/staging/dream/include/linux/msm_audio.h
 *
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_MSM_AUDIO_H
#define __LINUX_MSM_AUDIO_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/sizes.h>

/* PCM Audio */

#define AUDIO_IOCTL_MAGIC 'a'

#define AUDIO_START        _IOW(AUDIO_IOCTL_MAGIC, 0, unsigned)
#define AUDIO_STOP         _IOW(AUDIO_IOCTL_MAGIC, 1, unsigned)
#define AUDIO_FLUSH        _IOW(AUDIO_IOCTL_MAGIC, 2, unsigned)
#define AUDIO_GET_CONFIG   _IOR(AUDIO_IOCTL_MAGIC, 3, unsigned)
#define AUDIO_SET_CONFIG   _IOW(AUDIO_IOCTL_MAGIC, 4, unsigned)
#define AUDIO_GET_STATS    _IOR(AUDIO_IOCTL_MAGIC, 5, unsigned)
#define AUDIO_ENABLE_AUDPP _IOW(AUDIO_IOCTL_MAGIC, 6, unsigned)
#define AUDIO_SET_ADRC     _IOW(AUDIO_IOCTL_MAGIC, 7, unsigned)
#define AUDIO_SET_EQ       _IOW(AUDIO_IOCTL_MAGIC, 8, unsigned)
#define AUDIO_SET_RX_IIR   _IOW(AUDIO_IOCTL_MAGIC, 9, unsigned)
#define AUDIO_SET_VOLUME   _IOW(AUDIO_IOCTL_MAGIC, 10, unsigned)
#define AUDIO_ENABLE_AUDPRE  _IOW(AUDIO_IOCTL_MAGIC, 11, unsigned)
#define AUDIO_SET_AGC        _IOW(AUDIO_IOCTL_MAGIC, 12, unsigned)
#define AUDIO_SET_NS         _IOW(AUDIO_IOCTL_MAGIC, 13, unsigned)
#define AUDIO_SET_TX_IIR     _IOW(AUDIO_IOCTL_MAGIC, 14, unsigned)
#define AUDIO_PAUSE	     _IOW(AUDIO_IOCTL_MAGIC, 15, unsigned)
#define AUDIO_GET_PCM_CONFIG _IOR(AUDIO_IOCTL_MAGIC, 30, unsigned)
#define AUDIO_SET_PCM_CONFIG _IOW(AUDIO_IOCTL_MAGIC, 31, unsigned)
#define AUDIO_SWITCH_DEVICE  _IOW(AUDIO_IOCTL_MAGIC, 32, unsigned)

#define	AUDIO_MAX_COMMON_IOCTL_NUM	100

#define	AUDIO_MAX_COMMON_IOCTL_NUM	100

struct msm_audio_config {
	uint32_t buffer_size;
	uint32_t buffer_count;
	uint32_t channel_count;
	uint32_t sample_rate;
	uint32_t type;
	uint32_t unused[3];
};

struct msm_audio_stats {
	uint32_t byte_count;
	uint32_t sample_count;
	uint32_t unused[2];
};

/* Audio routing */

#define SND_IOCTL_MAGIC 's'

#define SND_MUTE_UNMUTED 0
#define SND_MUTE_MUTED   1

struct msm_snd_device_config {
	uint32_t device;
	uint32_t ear_mute;
	uint32_t mic_mute;
};

#define SND_SET_DEVICE _IOW(SND_IOCTL_MAGIC, 2, struct msm_device_config *)

#define SND_METHOD_VOICE 0

struct msm_snd_volume_config {
	uint32_t device;
	uint32_t method;
	uint32_t volume;
};

#define SND_SET_VOLUME _IOW(SND_IOCTL_MAGIC, 3, struct msm_snd_volume_config *)

/* Returns the number of SND endpoints supported. */

#define SND_GET_NUM_ENDPOINTS _IOR(SND_IOCTL_MAGIC, 4, unsigned *)

struct msm_snd_endpoint {
	int id; /* input and output */
	char name[64]; /* output only */
};

/* Takes an index between 0 and one less than the number returned by
 * SND_GET_NUM_ENDPOINTS, and returns the SND index and name of a
 * SND endpoint.  On input, the .id field contains the number of the
 * endpoint, and on exit it contains the SND index, while .name contains
 * the description of the endpoint.
 */

#define SND_GET_ENDPOINT _IOWR(SND_IOCTL_MAGIC, 5, struct msm_snd_endpoint *)

struct msm_audio_pcm_config {
	uint32_t pcm_feedback;	/* 0 - disable > 0 - enable */
	uint32_t buffer_count;	/* Number of buffers to allocate */
	uint32_t buffer_size;	/* Size of buffer for capturing of
				   PCM samples */
};
#endif
