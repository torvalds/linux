/*
 * platform_sst_audio.h:  sst audio platform data header file
 *
 * Copyright (C) 2012-14 Intel Corporation
 * Author: Jeeja KP <jeeja.kp@intel.com>
 * 	Omair Mohammed Abdullah <omair.m.abdullah@intel.com>
 *	Vinod Koul ,vinod.koul@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _PLATFORM_SST_AUDIO_H_
#define _PLATFORM_SST_AUDIO_H_

#include <linux/sfi.h>

enum sst_audio_task_id_mrfld {
	SST_TASK_ID_NONE = 0,
	SST_TASK_ID_SBA = 1,
	SST_TASK_ID_MEDIA = 3,
	SST_TASK_ID_MAX = SST_TASK_ID_MEDIA,
};

/* Device IDs for Merrifield are Pipe IDs,
 * ref: DSP spec v0.75 */
enum sst_audio_device_id_mrfld {
	/* Output pipeline IDs */
	PIPE_ID_OUT_START = 0x0,
	PIPE_CODEC_OUT0 = 0x2,
	PIPE_CODEC_OUT1 = 0x3,
	PIPE_SPROT_LOOP_OUT = 0x4,
	PIPE_MEDIA_LOOP1_OUT = 0x5,
	PIPE_MEDIA_LOOP2_OUT = 0x6,
	PIPE_VOIP_OUT = 0xC,
	PIPE_PCM0_OUT = 0xD,
	PIPE_PCM1_OUT = 0xE,
	PIPE_PCM2_OUT = 0xF,
	PIPE_MEDIA0_OUT = 0x12,
	PIPE_MEDIA1_OUT = 0x13,
/* Input Pipeline IDs */
	PIPE_ID_IN_START = 0x80,
	PIPE_CODEC_IN0 = 0x82,
	PIPE_CODEC_IN1 = 0x83,
	PIPE_SPROT_LOOP_IN = 0x84,
	PIPE_MEDIA_LOOP1_IN = 0x85,
	PIPE_MEDIA_LOOP2_IN = 0x86,
	PIPE_VOIP_IN = 0x8C,
	PIPE_PCM0_IN = 0x8D,
	PIPE_PCM1_IN = 0x8E,
	PIPE_MEDIA0_IN = 0x8F,
	PIPE_MEDIA1_IN = 0x90,
	PIPE_MEDIA2_IN = 0x91,
	PIPE_RSVD = 0xFF,
};

/* The stream map for each platform consists of an array of the below
 * stream map structure.
 */
struct sst_dev_stream_map {
	u8 dev_num;		/* device id */
	u8 subdev_num;		/* substream */
	u8 direction;
	u8 device_id;		/* fw id */
	u8 task_id;		/* fw task */
	u8 status;
};

struct sst_platform_data {
	/* Intel software platform id*/
	struct sst_dev_stream_map *pdev_strm_map;
	unsigned int strm_map_size;
};

int add_sst_platform_device(void);
#endif

