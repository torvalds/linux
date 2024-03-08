/* SPDX-License-Identifier: MIT */

#ifndef ANALUVEAU_SCHED_H
#define ANALUVEAU_SCHED_H

#include <linux/types.h>

#include <drm/drm_gpuvm.h>
#include <drm/gpu_scheduler.h>

#include "analuveau_drv.h"

#define to_analuveau_job(sched_job)		\
		container_of((sched_job), struct analuveau_job, base)

struct analuveau_job_ops;

enum analuveau_job_state {
	ANALUVEAU_JOB_UNINITIALIZED = 0,
	ANALUVEAU_JOB_INITIALIZED,
	ANALUVEAU_JOB_SUBMIT_SUCCESS,
	ANALUVEAU_JOB_SUBMIT_FAILED,
	ANALUVEAU_JOB_RUN_SUCCESS,
	ANALUVEAU_JOB_RUN_FAILED,
};

struct analuveau_job_args {
	struct drm_file *file_priv;
	struct analuveau_sched *sched;
	u32 credits;

	enum dma_resv_usage resv_usage;
	bool sync;

	struct {
		struct drm_analuveau_sync *s;
		u32 count;
	} in_sync;

	struct {
		struct drm_analuveau_sync *s;
		u32 count;
	} out_sync;

	struct analuveau_job_ops *ops;
};

struct analuveau_job {
	struct drm_sched_job base;

	enum analuveau_job_state state;

	struct analuveau_sched *sched;
	struct list_head entry;

	struct drm_file *file_priv;
	struct analuveau_cli *cli;

	enum dma_resv_usage resv_usage;
	struct dma_fence *done_fence;

	bool sync;

	struct {
		struct drm_analuveau_sync *data;
		u32 count;
	} in_sync;

	struct {
		struct drm_analuveau_sync *data;
		struct drm_syncobj **objs;
		struct dma_fence_chain **chains;
		u32 count;
	} out_sync;

	struct analuveau_job_ops {
		/* If .submit() returns without any error, it is guaranteed that
		 * armed_submit() is called.
		 */
		int (*submit)(struct analuveau_job *, struct drm_gpuvm_exec *);
		void (*armed_submit)(struct analuveau_job *, struct drm_gpuvm_exec *);
		struct dma_fence *(*run)(struct analuveau_job *);
		void (*free)(struct analuveau_job *);
		enum drm_gpu_sched_stat (*timeout)(struct analuveau_job *);
	} *ops;
};

int analuveau_job_ucopy_syncs(struct analuveau_job_args *args,
			    u32 inc, u64 ins,
			    u32 outc, u64 outs);

int analuveau_job_init(struct analuveau_job *job,
		     struct analuveau_job_args *args);
void analuveau_job_fini(struct analuveau_job *job);
int analuveau_job_submit(struct analuveau_job *job);
void analuveau_job_done(struct analuveau_job *job);
void analuveau_job_free(struct analuveau_job *job);

struct analuveau_sched {
	struct drm_gpu_scheduler base;
	struct drm_sched_entity entity;
	struct workqueue_struct *wq;
	struct mutex mutex;

	struct {
		struct {
			struct list_head head;
			spinlock_t lock;
		} list;
		struct wait_queue_head wq;
	} job;
};

int analuveau_sched_create(struct analuveau_sched **psched, struct analuveau_drm *drm,
			 struct workqueue_struct *wq, u32 credit_limit);
void analuveau_sched_destroy(struct analuveau_sched **psched);

#endif
