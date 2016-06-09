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

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <media/media-entity.h>

struct vsp1_dl_list;
struct vsp1_rwpf;

/*
 * struct vsp1_format_info - VSP1 video format description
 * @fourcc: V4L2 pixel format FCC identifier
 * @mbus: media bus format code
 * @hwfmt: VSP1 hardware format
 * @swap: swap register control
 * @planes: number of planes
 * @bpp: bits per pixel
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
 * @pipe: the media pipeline
 * @irqlock: protects the pipeline state
 * @state: current state
 * @wq: wait queue to wait for state change completion
 * @frame_end: frame end interrupt handler
 * @lock: protects the pipeline use count and stream count
 * @kref: pipeline reference count
 * @stream_count: number of streaming video nodes
 * @buffers_ready: bitmask of RPFs and WPFs with at least one buffer available
 * @sequence: frame sequence number
 * @num_inputs: number of RPFs
 * @inputs: array of RPFs in the pipeline (indexed by RPF index)
 * @output: WPF at the output of the pipeline
 * @bru: BRU entity, if present
 * @lif: LIF entity, if present
 * @uds: UDS entity, if present
 * @uds_input: entity at the input of the UDS, if the UDS is present
 * @entities: list of entities in the pipeline
 * @dl: display list associated with the pipeline
 * @div_size: The maximum allowed partition size for the pipeline
 * @partitions: The number of partitions used to process one frame
 * @current_partition: The partition number currently being configured
 */
struct vsp1_pipeline {
	struct media_pipeline pipe;

	spinlock_t irqlock;
	enum vsp1_pipeline_state state;
	wait_queue_head_t wq;

	void (*frame_end)(struct vsp1_pipeline *pipe);

	struct mutex lock;
	struct kref kref;
	unsigned int stream_count;
	unsigned int buffers_ready;
	unsigned int sequence;

	unsigned int num_inputs;
	struct vsp1_rwpf *inputs[VSP1_MAX_RPF];
	struct vsp1_rwpf *output;
	struct vsp1_entity *bru;
	struct vsp1_entity *lif;
	struct vsp1_entity *uds;
	struct vsp1_entity *uds_input;

	struct list_head entities;

	struct vsp1_dl_list *dl;

	unsigned int div_size;
	unsigned int partitions;
	struct v4l2_rect partition;
	unsigned int current_partition;
};

void vsp1_pipeline_reset(struct vsp1_pipeline *pipe);
void vsp1_pipeline_init(struct vsp1_pipeline *pipe);

void vsp1_pipeline_run(struct vsp1_pipeline *pipe);
bool vsp1_pipeline_stopped(struct vsp1_pipeline *pipe);
int vsp1_pipeline_stop(struct vsp1_pipeline *pipe);
bool vsp1_pipeline_ready(struct vsp1_pipeline *pipe);

void vsp1_pipeline_frame_end(struct vsp1_pipeline *pipe);

void vsp1_pipeline_propagate_alpha(struct vsp1_pipeline *pipe,
				   struct vsp1_dl_list *dl, unsigned int alpha);

void vsp1_pipelines_suspend(struct vsp1_device *vsp1);
void vsp1_pipelines_resume(struct vsp1_device *vsp1);

const struct vsp1_format_info *vsp1_get_format_info(struct vsp1_device *vsp1,
						    u32 fourcc);

#endif /* __VSP1_PIPE_H__ */
