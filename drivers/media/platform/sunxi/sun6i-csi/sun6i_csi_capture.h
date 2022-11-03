/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * All rights reserved.
 * Author: Yong Deng <yong.deng@magewell.com>
 */

#ifndef __SUN6I_CAPTURE_H__
#define __SUN6I_CAPTURE_H__

#include <media/v4l2-dev.h>
#include <media/videobuf2-core.h>

struct sun6i_csi_device;

#undef current
struct sun6i_csi_capture_state {
	struct list_head		queue;
	spinlock_t			lock; /* Queue and buffers lock. */

	struct sun6i_csi_buffer		*pending;
	struct sun6i_csi_buffer		*current;
	struct sun6i_csi_buffer		*complete;

	unsigned int			sequence;
};

struct sun6i_csi_capture {
	struct sun6i_csi_capture_state	state;

	struct video_device		video_dev;
	struct vb2_queue		queue;
	struct mutex			lock; /* Queue lock. */
	struct media_pad		pad;

	struct v4l2_format		format;
	u32				mbus_code;
};

void sun6i_csi_capture_dimensions(struct sun6i_csi_device *csi_dev,
				  unsigned int *width, unsigned int *height);
void sun6i_csi_capture_format(struct sun6i_csi_device *csi_dev,
			      u32 *pixelformat, u32 *field);

void sun6i_csi_capture_sync(struct sun6i_csi_device *csi_dev);
void sun6i_csi_capture_frame_done(struct sun6i_csi_device *csi_dev);

int sun6i_csi_capture_setup(struct sun6i_csi_device *csi_dev);
void sun6i_csi_capture_cleanup(struct sun6i_csi_device *csi_dev);

#endif /* __SUN6I_CAPTURE_H__ */
