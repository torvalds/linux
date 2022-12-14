/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * All rights reserved.
 * Author: Yong Deng <yong.deng@magewell.com>
 */

#ifndef __SUN6I_VIDEO_H__
#define __SUN6I_VIDEO_H__

#include <media/v4l2-dev.h>
#include <media/videobuf2-core.h>

struct sun6i_csi_device;

struct sun6i_video {
	struct video_device		video_dev;
	struct vb2_queue		queue;
	struct mutex			lock; /* Queue lock. */
	struct media_pad		pad;

	struct list_head		dma_queue;
	spinlock_t			dma_queue_lock; /* DMA queue lock. */

	struct v4l2_format		format;
	u32				mbus_code;
	unsigned int			sequence;
};

int sun6i_video_setup(struct sun6i_csi_device *csi_dev);
void sun6i_video_cleanup(struct sun6i_csi_device *csi_dev);

void sun6i_video_frame_done(struct sun6i_csi_device *csi_dev);

#endif /* __SUN6I_VIDEO_H__ */
