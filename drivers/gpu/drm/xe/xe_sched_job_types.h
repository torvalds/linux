/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_SCHED_JOB_TYPES_H_
#define _XE_SCHED_JOB_TYPES_H_

#include <linux/kref.h>

#include <drm/gpu_scheduler.h>

struct xe_exec_queue;

/**
 * struct xe_sched_job - XE schedule job (batch buffer tracking)
 */
struct xe_sched_job {
	/** @drm: base DRM scheduler job */
	struct drm_sched_job drm;
	/** @q: Exec queue */
	struct xe_exec_queue *q;
	/** @refcount: ref count of this job */
	struct kref refcount;
	/**
	 * @fence: dma fence to indicate completion. 1 way relationship - job
	 * can safely reference fence, fence cannot safely reference job.
	 */
#define JOB_FLAG_SUBMIT		DMA_FENCE_FLAG_USER_BITS
	struct dma_fence *fence;
	/** @user_fence: write back value when BB is complete */
	struct {
		/** @used: user fence is used */
		bool used;
		/** @addr: address to write to */
		u64 addr;
		/** @value: write back value */
		u64 value;
	} user_fence;
	/** @migrate_flush_flags: Additional flush flags for migration jobs */
	u32 migrate_flush_flags;
	/** @batch_addr: batch buffer address of job */
	u64 batch_addr[];
};

#endif
