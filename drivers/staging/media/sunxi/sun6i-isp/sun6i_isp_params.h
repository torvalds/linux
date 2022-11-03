/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _SUN6I_ISP_PARAMS_H_
#define _SUN6I_ISP_PARAMS_H_

#include <media/v4l2-device.h>

#define SUN6I_ISP_PARAMS_NAME		"sun6i-isp-params"

struct sun6i_isp_device;

struct sun6i_isp_params_state {
	struct list_head		queue; /* Queue and buffers lock. */
	spinlock_t			lock;

	struct sun6i_isp_buffer		*pending;

	bool				configured;
	bool				streaming;
};

struct sun6i_isp_params {
	struct sun6i_isp_params_state	state;

	struct video_device		video_dev;
	struct vb2_queue		queue;
	struct mutex			lock; /* Queue lock. */
	struct media_pad		pad;

	struct v4l2_format		format;
};

/* Params */

void sun6i_isp_params_configure(struct sun6i_isp_device *isp_dev);

/* State */

void sun6i_isp_params_state_update(struct sun6i_isp_device *isp_dev,
				   bool *update);
void sun6i_isp_params_state_complete(struct sun6i_isp_device *isp_dev);

/* Params */

int sun6i_isp_params_setup(struct sun6i_isp_device *isp_dev);
void sun6i_isp_params_cleanup(struct sun6i_isp_device *isp_dev);

#endif
