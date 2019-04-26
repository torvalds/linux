/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 Collabora ltd. */

#ifndef __PANFROST_JOB_H__
#define __PANFROST_JOB_H__

#include <uapi/drm/panfrost_drm.h>
#include <drm/gpu_scheduler.h>

struct panfrost_device;
struct panfrost_gem_object;
struct panfrost_file_priv;

struct panfrost_job {
	struct drm_sched_job base;

	struct kref refcount;

	struct panfrost_device *pfdev;
	struct panfrost_file_priv *file_priv;

	/* Optional fences userspace can pass in for the job to depend on. */
	struct dma_fence **in_fences;
	u32 in_fence_count;

	/* Fence to be signaled by IRQ handler when the job is complete. */
	struct dma_fence *done_fence;

	__u64 jc;
	__u32 requirements;
	__u32 flush_id;

	/* Exclusive fences we have taken from the BOs to wait for */
	struct dma_fence **implicit_fences;
	struct drm_gem_object **bos;
	u32 bo_count;

	/* Fence to be signaled by drm-sched once its done with the job */
	struct dma_fence *render_done_fence;
};

int panfrost_job_init(struct panfrost_device *pfdev);
void panfrost_job_fini(struct panfrost_device *pfdev);
int panfrost_job_open(struct panfrost_file_priv *panfrost_priv);
void panfrost_job_close(struct panfrost_file_priv *panfrost_priv);
int panfrost_job_push(struct panfrost_job *job);
void panfrost_job_put(struct panfrost_job *job);
void panfrost_job_enable_interrupts(struct panfrost_device *pfdev);
int panfrost_job_is_idle(struct panfrost_device *pfdev);

#endif
