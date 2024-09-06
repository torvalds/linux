/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_SCHED_JOB_H_
#define _XE_SCHED_JOB_H_

#include "xe_sched_job_types.h"

struct drm_printer;
struct xe_vm;
struct xe_sync_entry;

#define XE_SCHED_HANG_LIMIT 1
#define XE_SCHED_JOB_TIMEOUT LONG_MAX

int xe_sched_job_module_init(void);
void xe_sched_job_module_exit(void);

struct xe_sched_job *xe_sched_job_create(struct xe_exec_queue *q,
					 u64 *batch_addr);
void xe_sched_job_destroy(struct kref *ref);

/**
 * xe_sched_job_get - get reference to XE schedule job
 * @job: XE schedule job object
 *
 * Increment XE schedule job's reference count
 */
static inline struct xe_sched_job *xe_sched_job_get(struct xe_sched_job *job)
{
	kref_get(&job->refcount);
	return job;
}

/**
 * xe_sched_job_put - put reference to XE schedule job
 * @job: XE schedule job object
 *
 * Decrement XE schedule job's reference count, call xe_sched_job_destroy when
 * reference count == 0.
 */
static inline void xe_sched_job_put(struct xe_sched_job *job)
{
	kref_put(&job->refcount, xe_sched_job_destroy);
}

void xe_sched_job_set_error(struct xe_sched_job *job, int error);
static inline bool xe_sched_job_is_error(struct xe_sched_job *job)
{
	return job->fence->error < 0;
}

bool xe_sched_job_started(struct xe_sched_job *job);
bool xe_sched_job_completed(struct xe_sched_job *job);

void xe_sched_job_arm(struct xe_sched_job *job);
void xe_sched_job_push(struct xe_sched_job *job);

int xe_sched_job_last_fence_add_dep(struct xe_sched_job *job, struct xe_vm *vm);
void xe_sched_job_init_user_fence(struct xe_sched_job *job,
				  struct xe_sync_entry *sync);

static inline struct xe_sched_job *
to_xe_sched_job(struct drm_sched_job *drm)
{
	return container_of(drm, struct xe_sched_job, drm);
}

static inline u32 xe_sched_job_seqno(struct xe_sched_job *job)
{
	return job->fence ? job->fence->seqno : 0;
}

static inline u32 xe_sched_job_lrc_seqno(struct xe_sched_job *job)
{
	return job->lrc_seqno;
}

static inline void
xe_sched_job_add_migrate_flush(struct xe_sched_job *job, u32 flags)
{
	job->migrate_flush_flags = flags;
}

bool xe_sched_job_is_migration(struct xe_exec_queue *q);

struct xe_sched_job_snapshot *xe_sched_job_snapshot_capture(struct xe_sched_job *job);
void xe_sched_job_snapshot_free(struct xe_sched_job_snapshot *snapshot);
void xe_sched_job_snapshot_print(struct xe_sched_job_snapshot *snapshot, struct drm_printer *p);

int xe_sched_job_add_deps(struct xe_sched_job *job, struct dma_resv *resv,
			  enum dma_resv_usage usage);

#endif
