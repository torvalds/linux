/* SPDX-License-Identifier: MIT */

#ifndef NOUVEAU_SCHED_H
#define NOUVEAU_SCHED_H

#include <linux/types.h>

#include <drm/drm_exec.h>
#include <drm/gpu_scheduler.h>

#include "nouveau_drv.h"

#define to_nouveau_job(sched_job)		\
		container_of((sched_job), struct nouveau_job, base)

struct nouveau_job_ops;

enum nouveau_job_state {
	NOUVEAU_JOB_UNINITIALIZED = 0,
	NOUVEAU_JOB_INITIALIZED,
	NOUVEAU_JOB_SUBMIT_SUCCESS,
	NOUVEAU_JOB_SUBMIT_FAILED,
	NOUVEAU_JOB_RUN_SUCCESS,
	NOUVEAU_JOB_RUN_FAILED,
};

struct nouveau_job_args {
	struct drm_file *file_priv;
	struct nouveau_sched_entity *sched_entity;

	enum dma_resv_usage resv_usage;
	bool sync;

	struct {
		struct drm_nouveau_sync *s;
		u32 count;
	} in_sync;

	struct {
		struct drm_nouveau_sync *s;
		u32 count;
	} out_sync;

	struct nouveau_job_ops *ops;
};

struct nouveau_job {
	struct drm_sched_job base;

	enum nouveau_job_state state;

	struct nouveau_sched_entity *entity;

	struct drm_file *file_priv;
	struct nouveau_cli *cli;

	struct drm_exec exec;
	enum dma_resv_usage resv_usage;
	struct dma_fence *done_fence;

	bool sync;

	struct {
		struct drm_nouveau_sync *data;
		u32 count;
	} in_sync;

	struct {
		struct drm_nouveau_sync *data;
		struct drm_syncobj **objs;
		struct dma_fence_chain **chains;
		u32 count;
	} out_sync;

	struct nouveau_job_ops {
		/* If .submit() returns without any error, it is guaranteed that
		 * armed_submit() is called.
		 */
		int (*submit)(struct nouveau_job *);
		void (*armed_submit)(struct nouveau_job *);
		struct dma_fence *(*run)(struct nouveau_job *);
		void (*free)(struct nouveau_job *);
		enum drm_gpu_sched_stat (*timeout)(struct nouveau_job *);
	} *ops;
};

int nouveau_job_ucopy_syncs(struct nouveau_job_args *args,
			    u32 inc, u64 ins,
			    u32 outc, u64 outs);

int nouveau_job_init(struct nouveau_job *job,
		     struct nouveau_job_args *args);
void nouveau_job_free(struct nouveau_job *job);

int nouveau_job_submit(struct nouveau_job *job);
void nouveau_job_fini(struct nouveau_job *job);

#define to_nouveau_sched_entity(entity)		\
		container_of((entity), struct nouveau_sched_entity, base)

struct nouveau_sched_entity {
	struct drm_sched_entity base;
	struct mutex mutex;

	struct workqueue_struct *sched_wq;

	struct {
		struct {
			struct list_head head;
			spinlock_t lock;
		} list;
		struct wait_queue_head wq;
	} job;
};

int nouveau_sched_entity_init(struct nouveau_sched_entity *entity,
			      struct drm_gpu_scheduler *sched,
			      struct workqueue_struct *sched_wq);
void nouveau_sched_entity_fini(struct nouveau_sched_entity *entity);

bool nouveau_sched_entity_qwork(struct nouveau_sched_entity *entity,
				struct work_struct *work);

int nouveau_sched_init(struct nouveau_drm *drm);
void nouveau_sched_fini(struct nouveau_drm *drm);

#endif
