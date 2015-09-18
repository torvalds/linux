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
#include <linux/wait.h>

#include <media/media-entity.h>
#include <media/videobuf2-core.h>

struct vsp1_video;

/*
 * struct vsp1_format_info - VSP1 video format description
 * @mbus: media bus format code
 * @fourcc: V4L2 pixel format FCC identifier
 * @planes: number of planes
 * @bpp: bits per pixel
 * @hwfmt: VSP1 hardware format
 * @swap_yc: the Y and C components are swapped (Y comes before C)
 * @swap_uv: the U and V components are swapped (V comes before U)
 * @hsub: horizontal subsampling factor
 * @vsub: vertical subsampling factor
 * @alpha: has an alpha channel
 */
struct vsp1_format_info {
	u32 fourcc;
	unsigned int mbus;
	unsigned int hwfmt;
	unsigned int swap;
	unsigned int planes;
	unsigned int bpp[3];
	bool swap_yc;
	bool swap_uv;
	unsigned int hsub;
	unsigned int vsub;
	bool alpha;
};

enum vsp1_pipeline_state {
	VSP1_PIPELINE_STOPPED,
	VSP1_PIPELINE_RUNNING,
	VSP1_PIPELINE_STOPPING,
};

/*
 * struct vsp1_pipeline - A VSP1 hardware pipeline
 * @media: the media pipeline
 * @irqlock: protects the pipeline state
 * @lock: protects the pipeline use count and stream count
 */
struct vsp1_pipeline {
	struct media_pipeline pipe;

	spinlock_t irqlock;
	enum vsp1_pipeline_state state;
	wait_queue_head_t wq;

	struct mutex lock;
	unsigned int use_count;
	unsigned int stream_count;
	unsigned int buffers_ready;

	unsigned int num_video;
	unsigned int num_inputs;
	struct vsp1_rwpf *inputs[VSP1_MAX_RPF];
	struct vsp1_rwpf *output;
	struct vsp1_entity *bru;
	struct vsp1_entity *lif;
	struct vsp1_entity *uds;
	struct vsp1_entity *uds_input;

	struct list_head entities;
};

static inline struct vsp1_pipeline *to_vsp1_pipeline(struct media_entity *e)
{
	if (likely(e->pipe))
		return container_of(e->pipe, struct vsp1_pipeline, pipe);
	else
		return NULL;
}

struct vsp1_video_buffer {
	struct vb2_buffer buf;
	struct list_head queue;

	dma_addr_t addr[3];
	unsigned int length[3];
};

static inline struct vsp1_video_buffer *
to_vsp1_video_buffer(struct vb2_buffer *vb)
{
	return container_of(vb, struct vsp1_video_buffer, buf);
}

struct vsp1_video_operations {
	void (*queue)(struct vsp1_video *video, struct vsp1_video_buffer *buf);
};

struct vsp1_video {
	struct vsp1_device *vsp1;
	struct vsp1_entity *rwpf;

	const struct vsp1_video_operations *ops;

	struct video_device video;
	enum v4l2_buf_type type;
	struct media_pad pad;

	struct mutex lock;
	struct v4l2_pix_format_mplane format;
	const struct vsp1_format_info *fmtinfo;

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

int vsp1_video_init(struct vsp1_video *video, struct vsp1_entity *rwpf);
void vsp1_video_cleanup(struct vsp1_video *video);

void vsp1_pipeline_frame_end(struct vsp1_pipeline *pipe);

void vsp1_pipeline_propagate_alpha(struct vsp1_pipeline *pipe,
				   struct vsp1_entity *input,
				   unsigned int alpha);

void vsp1_pipelines_suspend(struct vsp1_device *vsp1);
void vsp1_pipelines_resume(struct vsp1_device *vsp1);

#endif /* __VSP1_VIDEO_H__ */
