/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_QUEUE_H
#define PVR_QUEUE_H

#include <drm/gpu_scheduler.h>
#include <linux/workqueue.h>

#include "pvr_cccb.h"
#include "pvr_device.h"

struct pvr_context;
struct pvr_queue;

/**
 * struct pvr_queue_fence_ctx - Queue fence context
 *
 * Used to implement dma_fence_ops for pvr_job::{done,cccb}_fence.
 */
struct pvr_queue_fence_ctx {
	/** @id: Fence context ID allocated with dma_fence_context_alloc(). */
	u64 id;

	/** @seqno: Sequence number incremented each time a fence is created. */
	atomic_t seqno;

	/** @lock: Lock used to synchronize access to fences allocated by this context. */
	spinlock_t lock;
};

/**
 * struct pvr_queue_cccb_fence_ctx - CCCB fence context
 *
 * Context used to manage fences controlling access to the CCCB. No fences are
 * issued if there's enough space in the CCCB to push job commands.
 */
struct pvr_queue_cccb_fence_ctx {
	/** @base: Base queue fence context. */
	struct pvr_queue_fence_ctx base;

	/**
	 * @job: Job waiting for CCCB space.
	 *
	 * Thanks to the serializationg done at the drm_sched_entity level,
	 * there's no more than one job waiting for CCCB at a given time.
	 *
	 * This field is NULL if no jobs are currently waiting for CCCB space.
	 *
	 * Must be accessed with @job_lock held.
	 */
	struct pvr_job *job;

	/** @job_lock: Lock protecting access to the job object. */
	struct mutex job_lock;
};

/**
 * struct pvr_queue_fence - Queue fence object
 */
struct pvr_queue_fence {
	/** @base: Base dma_fence. */
	struct dma_fence base;

	/** @queue: Queue that created this fence. */
	struct pvr_queue *queue;

	/** @release_work: Fence release work structure. */
	struct work_struct release_work;
};

/**
 * struct pvr_queue - Job queue
 *
 * Used to queue and track execution of pvr_job objects.
 */
struct pvr_queue {
	/** @scheduler: Single entity scheduler use to push jobs to this queue. */
	struct drm_gpu_scheduler scheduler;

	/** @entity: Scheduling entity backing this queue. */
	struct drm_sched_entity entity;

	/** @type: Type of jobs queued to this queue. */
	enum drm_pvr_job_type type;

	/** @ctx: Context object this queue is bound to. */
	struct pvr_context *ctx;

	/** @node: Used to add the queue to the active/idle queue list. */
	struct list_head node;

	/**
	 * @in_flight_job_count: Number of jobs submitted to the CCCB that
	 * have not been processed yet.
	 */
	atomic_t in_flight_job_count;

	/**
	 * @cccb_fence_ctx: CCCB fence context.
	 *
	 * Used to control access to the CCCB is full, such that we don't
	 * end up trying to push commands to the CCCB if there's not enough
	 * space to receive all commands needed for a job to complete.
	 */
	struct pvr_queue_cccb_fence_ctx cccb_fence_ctx;

	/** @job_fence_ctx: Job fence context object. */
	struct pvr_queue_fence_ctx job_fence_ctx;

	/** @timeline_ufo: Timeline UFO for the context queue. */
	struct {
		/** @fw_obj: FW object representing the UFO value. */
		struct pvr_fw_object *fw_obj;

		/** @value: CPU mapping of the UFO value. */
		u32 *value;
	} timeline_ufo;

	/**
	 * @last_queued_job_scheduled_fence: The scheduled fence of the last
	 * job queued to this queue.
	 *
	 * We use it to insert frag -> geom dependencies when issuing combined
	 * geom+frag jobs, to guarantee that the fragment job that's part of
	 * the combined operation comes after all fragment jobs that were queued
	 * before it.
	 */
	struct dma_fence *last_queued_job_scheduled_fence;

	/** @cccb: Client Circular Command Buffer. */
	struct pvr_cccb cccb;

	/** @reg_state_obj: FW object representing the register state of this queue. */
	struct pvr_fw_object *reg_state_obj;

	/** @ctx_offset: Offset of the queue context in the FW context object. */
	u32 ctx_offset;

	/** @callstack_addr: Initial call stack address for register state object. */
	u64 callstack_addr;
};

bool pvr_queue_fence_is_ufo_backed(struct dma_fence *f);

int pvr_queue_job_init(struct pvr_job *job);

void pvr_queue_job_cleanup(struct pvr_job *job);

void pvr_queue_job_push(struct pvr_job *job);

struct dma_fence *pvr_queue_job_arm(struct pvr_job *job);

struct pvr_queue *pvr_queue_create(struct pvr_context *ctx,
				   enum drm_pvr_job_type type,
				   struct drm_pvr_ioctl_create_context_args *args,
				   void *fw_ctx_map);

void pvr_queue_kill(struct pvr_queue *queue);

void pvr_queue_destroy(struct pvr_queue *queue);

void pvr_queue_process(struct pvr_queue *queue);

void pvr_queue_device_pre_reset(struct pvr_device *pvr_dev);

void pvr_queue_device_post_reset(struct pvr_device *pvr_dev);

int pvr_queue_device_init(struct pvr_device *pvr_dev);

void pvr_queue_device_fini(struct pvr_device *pvr_dev);

#endif /* PVR_QUEUE_H */
