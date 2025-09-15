/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_DEP_JOB_TYPES_H_
#define _XE_DEP_JOB_TYPES_H_

#include <drm/gpu_scheduler.h>

struct xe_dep_job;

/** struct xe_dep_job_ops - Generic Xe dependency job operations */
struct xe_dep_job_ops {
	/** @run_job: Run generic Xe dependency job */
	struct dma_fence *(*run_job)(struct xe_dep_job *job);
	/** @free_job: Free generic Xe dependency job */
	void (*free_job)(struct xe_dep_job *job);
};

/** struct xe_dep_job - Generic dependency Xe job */
struct xe_dep_job {
	/** @drm: base DRM scheduler job */
	struct drm_sched_job drm;
	/** @ops: dependency job operations */
	const struct xe_dep_job_ops *ops;
};

#endif
