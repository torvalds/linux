/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GPU_SCHEDULER_H_
#define _XE_GPU_SCHEDULER_H_

#include "xe_gpu_scheduler_types.h"
#include "xe_sched_job.h"

int xe_sched_init(struct xe_gpu_scheduler *sched,
		  const struct drm_sched_backend_ops *ops,
		  const struct xe_sched_backend_ops *xe_ops,
		  struct workqueue_struct *submit_wq,
		  uint32_t hw_submission, unsigned hang_limit,
		  long timeout, struct workqueue_struct *timeout_wq,
		  atomic_t *score, const char *name,
		  struct device *dev);
void xe_sched_fini(struct xe_gpu_scheduler *sched);

void xe_sched_submission_start(struct xe_gpu_scheduler *sched);
void xe_sched_submission_stop(struct xe_gpu_scheduler *sched);

void xe_sched_submission_resume_tdr(struct xe_gpu_scheduler *sched);

void xe_sched_add_msg(struct xe_gpu_scheduler *sched,
		      struct xe_sched_msg *msg);
void xe_sched_add_msg_locked(struct xe_gpu_scheduler *sched,
			     struct xe_sched_msg *msg);
void xe_sched_add_msg_head(struct xe_gpu_scheduler *sched,
			   struct xe_sched_msg *msg);

static inline void xe_sched_msg_lock(struct xe_gpu_scheduler *sched)
{
	spin_lock(&sched->msg_lock);
}

static inline void xe_sched_msg_unlock(struct xe_gpu_scheduler *sched)
{
	spin_unlock(&sched->msg_lock);
}

static inline void xe_sched_stop(struct xe_gpu_scheduler *sched)
{
	drm_sched_stop(&sched->base, NULL);
}

static inline void xe_sched_tdr_queue_imm(struct xe_gpu_scheduler *sched)
{
	drm_sched_tdr_queue_imm(&sched->base);
}

static inline void xe_sched_resubmit_jobs(struct xe_gpu_scheduler *sched)
{
	struct drm_sched_job *s_job;
	bool restore_replay = false;

	drm_sched_for_each_pending_job(s_job, &sched->base, NULL) {
		restore_replay |= to_xe_sched_job(s_job)->restore_replay;
		if (restore_replay || !drm_sched_job_is_signaled(s_job))
			sched->base.ops->run_job(s_job);
	}
}

static inline bool
xe_sched_invalidate_job(struct xe_sched_job *job, int threshold)
{
	return drm_sched_invalidate_job(&job->drm, threshold);
}

/**
 * xe_sched_first_pending_job() - Find first pending job which is unsignaled
 * @sched: Xe GPU scheduler
 *
 * Return first unsignaled job in pending list or NULL
 */
static inline
struct xe_sched_job *xe_sched_first_pending_job(struct xe_gpu_scheduler *sched)
{
	struct drm_sched_job *job;

	drm_sched_for_each_pending_job(job, &sched->base, NULL)
		if (!drm_sched_job_is_signaled(job))
			return to_xe_sched_job(job);

	return NULL;
}

static inline int
xe_sched_entity_init(struct xe_sched_entity *entity,
		     struct xe_gpu_scheduler *sched)
{
	return drm_sched_entity_init(entity, 0,
				     (struct drm_gpu_scheduler **)&sched,
				     1, NULL);
}

#define xe_sched_entity_fini drm_sched_entity_fini

#endif
