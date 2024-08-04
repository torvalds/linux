/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vsp1_pipe.h  --  R-Car VSP1 Pipeline
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */
#ifndef __VSP1_PIPE_H__
#define __VSP1_PIPE_H__

#include <linux/dynamic_debug.h>
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
 * struct vsp1_partition - A description of a slice for the partition algorithm
 * @rpf: The RPF partition window configuration
 * @uds_sink: The UDS input partition window configuration
 * @uds_source: The UDS output partition window configuration
 * @sru: The SRU partition window configuration
 * @wpf: The WPF partition window configuration
 */
struct vsp1_partition {
	struct v4l2_rect rpf[VSP1_MAX_RPF];
	struct v4l2_rect uds_sink;
	struct v4l2_rect uds_source;
	struct v4l2_rect sru;
	struct v4l2_rect wpf;
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
 * @brx: BRx entity, if present
 * @hgo: HGO entity, if present
 * @hgt: HGT entity, if present
 * @lif: LIF entity, if present
 * @uds: UDS entity, if present
 * @uds_input: entity at the input of the UDS, if the UDS is present
 * @entities: list of entities in the pipeline
 * @stream_config: cached stream configuration for video pipelines
 * @configured: when false the @stream_config shall be written to the hardware
 * @interlaced: True when the pipeline is configured in interlaced mode
 * @partitions: The number of partitions used to process one frame
 * @part_table: The pre-calculated partitions used by the pipeline
 */
struct vsp1_pipeline {
	struct media_pipeline pipe;

	spinlock_t irqlock;
	enum vsp1_pipeline_state state;
	wait_queue_head_t wq;

	void (*frame_end)(struct vsp1_pipeline *pipe, unsigned int completion);

	struct mutex lock;
	struct kref kref;
	unsigned int stream_count;
	unsigned int buffers_ready;
	unsigned int sequence;

	unsigned int num_inputs;
	struct vsp1_rwpf *inputs[VSP1_MAX_RPF];
	struct vsp1_rwpf *output;
	struct vsp1_entity *brx;
	struct vsp1_entity *hgo;
	struct vsp1_entity *hgt;
	struct vsp1_entity *lif;
	struct vsp1_entity *uds;
	struct vsp1_entity *uds_input;

	/*
	 * The order of this list must be identical to the order of the entities
	 * in the pipeline, as it is assumed by the partition algorithm that we
	 * can walk this list in sequence.
	 */
	struct list_head entities;

	struct vsp1_dl_body *stream_config;
	bool configured;
	bool interlaced;

	unsigned int partitions;
	struct vsp1_partition *part_table;

	u32 underrun_count;
};

void vsp1_pipeline_reset(struct vsp1_pipeline *pipe);
void vsp1_pipeline_init(struct vsp1_pipeline *pipe);

void __vsp1_pipeline_dump(struct _ddebug *, struct vsp1_pipeline *pipe,
			  const char *msg);

#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))
#define vsp1_pipeline_dump(pipe, msg)			\
	_dynamic_func_call("vsp1_pipeline_dump()", __vsp1_pipeline_dump, pipe, msg)
#elif defined(DEBUG)
#define vsp1_pipeline_dump(pipe, msg)			\
	__vsp1_pipeline_dump(NULL, pipe, msg)
#else
#define vsp1_pipeline_dump(pipe, msg)			\
({							\
	if (0)						\
		__vsp1_pipeline_dump(NULL, pipe, msg);	\
})
#endif

void vsp1_pipeline_run(struct vsp1_pipeline *pipe);
bool vsp1_pipeline_stopped(struct vsp1_pipeline *pipe);
int vsp1_pipeline_stop(struct vsp1_pipeline *pipe);
bool vsp1_pipeline_ready(struct vsp1_pipeline *pipe);

void vsp1_pipeline_frame_end(struct vsp1_pipeline *pipe);

void vsp1_pipeline_propagate_alpha(struct vsp1_pipeline *pipe,
				   struct vsp1_dl_body *dlb,
				   unsigned int alpha);

void vsp1_pipeline_calculate_partition(struct vsp1_pipeline *pipe,
				       struct vsp1_partition *partition,
				       unsigned int div_size,
				       unsigned int index);

const struct vsp1_format_info *vsp1_get_format_info(struct vsp1_device *vsp1,
						    u32 fourcc);

#endif /* __VSP1_PIPE_H__ */
