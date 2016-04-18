/*
 * vsp1_video.h  --  R-Car VSP1 Video Node
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_VIDEO_H__
#define __VSP1_VIDEO_H__

#include <linux/list.h>
#include <linux/spinlock.h>

#include <media/videobuf2-v4l2.h>

#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"

struct vsp1_vb2_buffer {
	struct vb2_v4l2_buffer buf;
	struct list_head queue;
	struct vsp1_rwpf_memory mem;
};

static inline struct vsp1_vb2_buffer *
to_vsp1_vb2_buffer(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct vsp1_vb2_buffer, buf);
}

struct vsp1_video {
	struct list_head list;
	struct vsp1_device *vsp1;
	struct vsp1_rwpf *rwpf;

	struct video_device video;
	enum v4l2_buf_type type;
	struct media_pad pad;

	struct mutex lock;

	struct vsp1_pipeline pipe;
	unsigned int pipe_index;

	struct vb2_queue queue;
	void *alloc_ctx;
	spinlock_t irqlock;
	struct list_head irqqueue;
	unsigned int sequence;
};

static inline struct vsp1_video *to_vsp1_video(struct video_device *vdev)
{
	return container_of(vdev, struct vsp1_video, video);
}

struct vsp1_video *vsp1_video_create(struct vsp1_device *vsp1,
				     struct vsp1_rwpf *rwpf);
void vsp1_video_cleanup(struct vsp1_video *video);

#endif /* __VSP1_VIDEO_H__ */
