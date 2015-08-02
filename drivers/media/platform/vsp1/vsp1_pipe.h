/*
 * vsp1_pipe.h  --  R-Car VSP1 Pipeline
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
#ifndef __VSP1_PIPE_H__
#define __VSP1_PIPE_H__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <media/media-entity.h>

struct vsp1_rwpf;

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

	void (*frame_end)(struct vsp1_pipeline *pipe);

	struct mutex lock;
	unsigned int use_count;
	unsigned int stream_count;
	unsigned int buffers_ready;

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

void vsp1_pipeline_reset(struct vsp1_pipeline *pipe);

void vsp1_pipeline_run(struct vsp1_pipeline *pipe);
bool vsp1_pipeline_stopped(struct vsp1_pipeline *pipe);
int vsp1_pipeline_stop(struct vsp1_pipeline *pipe);
bool vsp1_pipeline_ready(struct vsp1_pipeline *pipe);

void vsp1_pipeline_frame_end(struct vsp1_pipeline *pipe);

void vsp1_pipeline_propagate_alpha(struct vsp1_pipeline *pipe,
				   struct vsp1_entity *input,
				   unsigned int alpha);

void vsp1_pipelines_suspend(struct vsp1_device *vsp1);
void vsp1_pipelines_resume(struct vsp1_device *vsp1);

#endif /* __VSP1_PIPE_H__ */
