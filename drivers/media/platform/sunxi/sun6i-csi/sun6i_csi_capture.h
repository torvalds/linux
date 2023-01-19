/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * Author: Yong Deng <yong.deng@magewell.com>
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _SUN6I_CAPTURE_H_
#define _SUN6I_CAPTURE_H_

#include <media/v4l2-device.h>

#define SUN6I_CSI_CAPTURE_NAME	"sun6i-csi-capture"

#define SUN6I_CSI_CAPTURE_WIDTH_MIN	32
#define SUN6I_CSI_CAPTURE_WIDTH_MAX	4800
#define SUN6I_CSI_CAPTURE_HEIGHT_MIN	32
#define SUN6I_CSI_CAPTURE_HEIGHT_MAX	4800

struct sun6i_csi_device;

struct sun6i_csi_capture_format {
	u32	pixelformat;
	u8	output_format_field;
	u8	output_format_frame;
	bool	input_yuv_seq_invert;
	bool	input_format_raw;
	u32	hsize_len_factor;
};

struct sun6i_csi_capture_format_match {
	u32	pixelformat;
	u32	mbus_code;
};

#undef current
struct sun6i_csi_capture_state {
	struct list_head		queue;
	spinlock_t			lock; /* Queue and buffers lock. */

	struct sun6i_csi_buffer		*pending;
	struct sun6i_csi_buffer		*current;
	struct sun6i_csi_buffer		*complete;

	unsigned int			sequence;
	bool				streaming;
	bool				setup;
};

struct sun6i_csi_capture {
	struct sun6i_csi_capture_state	state;

	struct video_device		video_dev;
	struct vb2_queue		queue;
	struct mutex			lock; /* Queue lock. */
	struct media_pad		pad;

	struct v4l2_format		format;
};

/* Helpers */

void sun6i_csi_capture_dimensions(struct sun6i_csi_device *csi_dev,
				  unsigned int *width, unsigned int *height);
void sun6i_csi_capture_format(struct sun6i_csi_device *csi_dev,
			      u32 *pixelformat, u32 *field);

/* Format */

const
struct sun6i_csi_capture_format *sun6i_csi_capture_format_find(u32 pixelformat);

/* Capture */

void sun6i_csi_capture_configure(struct sun6i_csi_device *csi_dev);
void sun6i_csi_capture_state_update(struct sun6i_csi_device *csi_dev);

/* State */

void sun6i_csi_capture_sync(struct sun6i_csi_device *csi_dev);
void sun6i_csi_capture_frame_done(struct sun6i_csi_device *csi_dev);

/* Capture */

int sun6i_csi_capture_setup(struct sun6i_csi_device *csi_dev);
void sun6i_csi_capture_cleanup(struct sun6i_csi_device *csi_dev);

#endif
