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
 * xe_sched_job_get - get reference to Xe schedule job
 * @job: Xe schedule job object
 *
 * Increment Xe schedule job's reference count
 */
static inline struct xe_sched_job *xe_sched_job_get(struct xe_sched_job *job)
{
	kref_get(&job->refcount);
	return job;
}

/**
 * xe_sched_job_put - put reference to Xe schedule job
 * @job: Xe schedule job object
 *
 * Decrement Xe schedule job's reference count, call xe_sched_job_destroy when
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

/**
 * xe_sched_job_is_ulls() - Is a ULLS job
 * @job: Xe schedule job object
 *
 * Return: True if @job is submitted as part of a ULLS sequence, False
 * otherwise.
 */
static inline bool xe_sched_job_is_ulls(struct xe_sched_job *job)
{
	return job->ulls != ULLS_NONE;
}

/**
 * xe_sched_job_ulls_has_batch() - Does a job carry batch buffers
 * @job: Xe schedule job object
 *
 * The ULLS jobs which enter and exit ULLS mode exist only to move the
 * migration context on and off the hardware, and carry no batch buffers.
 *
 * Return: True if @job carries batch buffers, False otherwise.
 */
static inline bool xe_sched_job_ulls_has_batch(struct xe_sched_job *job)
{
	return job->ulls == ULLS_NONE || job->ulls == ULLS_ACTIVE;
}

/**
 * xe_sched_job_ulls_parks() - Does a job park the engine for its successor
 * @job: Xe schedule job object
 *
 * A ULLS job which is not the last one emits a postamble, parking the engine
 * on its successor's semaphore and publishing that successor's ring tail.
 *
 * Return: True if @job emits a ULLS postamble, False otherwise.
 */
static inline bool xe_sched_job_ulls_parks(struct xe_sched_job *job)
{
	return job->ulls == ULLS_ENTER || job->ulls == ULLS_ACTIVE;
}

/**
 * xe_sched_job_ulls_is_chained() - Has a job's predecessor already published it
 * @job: Xe schedule job object
 *
 * A ULLS job which is not the first one has had its ring tail published by its
 * predecessor's postamble, which also left the engine parked on this job's
 * semaphore. Submitting it is a semaphore write alone - no H2G and no ring
 * tail write.
 *
 * Return: True if @job was published by its predecessor, False otherwise.
 */
static inline bool xe_sched_job_ulls_is_chained(struct xe_sched_job *job)
{
	return job->ulls == ULLS_ACTIVE || job->ulls == ULLS_EXIT;
}

bool xe_sched_job_is_migration(struct xe_exec_queue *q);

struct xe_sched_job_snapshot *xe_sched_job_snapshot_capture(struct xe_sched_job *job);
void xe_sched_job_snapshot_free(struct xe_sched_job_snapshot *snapshot);
void xe_sched_job_snapshot_print(struct xe_sched_job_snapshot *snapshot, struct drm_printer *p);

int xe_sched_job_add_deps(struct xe_sched_job *job, struct dma_resv *resv,
			  enum dma_resv_usage usage);

#endif
