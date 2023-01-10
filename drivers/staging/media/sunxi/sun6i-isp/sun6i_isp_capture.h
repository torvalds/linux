/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _SUN6I_ISP_CAPTURE_H_
#define _SUN6I_ISP_CAPTURE_H_

#include <media/v4l2-device.h>

#define SUN6I_ISP_CAPTURE_NAME		"sun6i-isp-capture"

#define SUN6I_ISP_CAPTURE_WIDTH_MIN	16
#define SUN6I_ISP_CAPTURE_WIDTH_MAX	3264
#define SUN6I_ISP_CAPTURE_HEIGHT_MIN	16
#define SUN6I_ISP_CAPTURE_HEIGHT_MAX	2448

struct sun6i_isp_device;

struct sun6i_isp_capture_format {
	u32	pixelformat;
	u8	output_format;
};

#undef current
struct sun6i_isp_capture_state {
	struct list_head		queue;
	spinlock_t			lock; /* Queue and buffers lock. */

	struct sun6i_isp_buffer		*pending;
	struct sun6i_isp_buffer		*current;
	struct sun6i_isp_buffer		*complete;

	unsigned int			sequence;
	bool				streaming;
};

struct sun6i_isp_capture {
	struct sun6i_isp_capture_state	state;

	struct video_device		video_dev;
	struct vb2_queue		queue;
	struct mutex			lock; /* Queue lock. */
	struct media_pad		pad;

	struct v4l2_format		format;
};

/* Helpers */

void sun6i_isp_capture_dimensions(struct sun6i_isp_device *isp_dev,
				  unsigned int *width, unsigned int *height);
void sun6i_isp_capture_format(struct sun6i_isp_device *isp_dev,
			      u32 *pixelformat);

/* Format */

const struct sun6i_isp_capture_format *
sun6i_isp_capture_format_find(u32 pixelformat);

/* Capture */

void sun6i_isp_capture_configure(struct sun6i_isp_device *isp_dev);

/* State */

void sun6i_isp_capture_state_update(struct sun6i_isp_device *isp_dev,
				    bool *update);
void sun6i_isp_capture_state_complete(struct sun6i_isp_device *isp_dev);
void sun6i_isp_capture_finish(struct sun6i_isp_device *isp_dev);

/* Capture */

int sun6i_isp_capture_setup(struct sun6i_isp_device *isp_dev);
void sun6i_isp_capture_cleanup(struct sun6i_isp_device *isp_dev);

#endif
