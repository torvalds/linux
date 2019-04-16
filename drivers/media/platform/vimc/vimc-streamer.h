/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vimc-streamer.h Virtual Media Controller Driver
 *
 * Copyright (C) 2018 Lucas A. M. Magalhães <lucmaga@gmail.com>
 *
 */

#ifndef _VIMC_STREAMER_H_
#define _VIMC_STREAMER_H_

#include <media/media-device.h>

#include "vimc-common.h"

#define VIMC_STREAMER_PIPELINE_MAX_SIZE 16

struct vimc_stream {
	struct media_pipeline pipe;
	struct vimc_ent_device *ved_pipeline[VIMC_STREAMER_PIPELINE_MAX_SIZE];
	unsigned int pipe_size;
	u8 *frame;
	struct task_struct *kthread;
};

/**
 * vimc_streamer_s_streamer - start/stop the stream
 *
 * @stream:	the pointer to the stream to start or stop
 * @ved:	The last entity of the streamer pipeline
 * @enable:	any non-zero number start the stream, zero stop
 *
 */
int vimc_streamer_s_stream(struct vimc_stream *stream,
			   struct vimc_ent_device *ved,
			   int enable);

#endif  //_VIMC_STREAMER_H_
