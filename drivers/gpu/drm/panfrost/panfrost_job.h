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
	struct panfrost_mmu *mmu;
	struct panfrost_jm_ctx *ctx;

	/* Fence to be signaled by IRQ handler when the job is complete. */
	struct dma_fence *done_fence;

	__u64 jc;
	__u32 requirements;
	__u32 flush_id;

	struct panfrost_gem_mapping **mappings;
	struct drm_gem_object **bos;
	u32 bo_count;

	/* Fence to be signaled by drm-sched once its done with the job */
	struct dma_fence *render_done_fence;

	struct panfrost_engine_usage *engine_usage;
	bool is_profiled;
	ktime_t start_time;
	u64 start_cycles;
};

struct panfrost_js_ctx {
	struct drm_sched_entity sched_entity;
	bool enabled;
};

#define NUM_JOB_SLOTS 3

struct panfrost_jm_ctx {
	struct kref refcnt;
	bool destroyed;
	struct drm_sched_entity slot_entity[NUM_JOB_SLOTS];
};

extern const char * const panfrost_engine_names[];

int panfrost_jm_ctx_create(struct drm_file *file,
			   struct drm_panfrost_jm_ctx_create *args);
int panfrost_jm_ctx_destroy(struct drm_file *file, u32 handle);
void panfrost_jm_ctx_put(struct panfrost_jm_ctx *jm_ctx);
struct panfrost_jm_ctx *panfrost_jm_ctx_get(struct panfrost_jm_ctx *jm_ctx);
struct panfrost_jm_ctx *panfrost_jm_ctx_from_handle(struct drm_file *file, u32 handle);

int panfrost_jm_init(struct panfrost_device *pfdev);
void panfrost_jm_fini(struct panfrost_device *pfdev);
int panfrost_jm_open(struct drm_file *file);
void panfrost_jm_close(struct drm_file *file);
void panfrost_jm_reset_interrupts(struct panfrost_device *pfdev);
void panfrost_jm_enable_interrupts(struct panfrost_device *pfdev);
void panfrost_jm_suspend_irq(struct panfrost_device *pfdev);
int panfrost_jm_is_idle(struct panfrost_device *pfdev);
int panfrost_job_get_slot(struct panfrost_job *job);
int panfrost_job_push(struct panfrost_job *job);
void panfrost_job_put(struct panfrost_job *job);

#endif
