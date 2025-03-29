// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_gpu_scheduler.h"

static void xe_sched_process_msg_queue(struct xe_gpu_scheduler *sched)
{
	if (!READ_ONCE(sched->base.pause_submit))
		queue_work(sched->base.submit_wq, &sched->work_process_msg);
}

static void xe_sched_process_msg_queue_if_ready(struct xe_gpu_scheduler *sched)
{
	struct xe_sched_msg *msg;

	xe_sched_msg_lock(sched);
	msg = list_first_entry_or_null(&sched->msgs, struct xe_sched_msg, link);
	if (msg)
		xe_sched_process_msg_queue(sched);
	xe_sched_msg_unlock(sched);
}

static struct xe_sched_msg *
xe_sched_get_msg(struct xe_gpu_scheduler *sched)
{
	struct xe_sched_msg *msg;

	xe_sched_msg_lock(sched);
	msg = list_first_entry_or_null(&sched->msgs,
				       struct xe_sched_msg, link);
	if (msg)
		list_del_init(&msg->link);
	xe_sched_msg_unlock(sched);

	return msg;
}

static void xe_sched_process_msg_work(struct work_struct *w)
{
	struct xe_gpu_scheduler *sched =
		container_of(w, struct xe_gpu_scheduler, work_process_msg);
	struct xe_sched_msg *msg;

	if (READ_ONCE(sched->base.pause_submit))
		return;

	msg = xe_sched_get_msg(sched);
	if (msg) {
		sched->ops->process_msg(msg);

		xe_sched_process_msg_queue_if_ready(sched);
	}
}

int xe_sched_init(struct xe_gpu_scheduler *sched,
		  const struct drm_sched_backend_ops *ops,
		  const struct xe_sched_backend_ops *xe_ops,
		  struct workqueue_struct *submit_wq,
		  uint32_t hw_submission, unsigned hang_limit,
		  long timeout, struct workqueue_struct *timeout_wq,
		  atomic_t *score, const char *name,
		  struct device *dev)
{
	const struct drm_sched_init_args args = {
		.ops = ops,
		.submit_wq = submit_wq,
		.num_rqs = 1,
		.credit_limit = hw_submission,
		.hang_limit = hang_limit,
		.timeout = timeout,
		.timeout_wq = timeout_wq,
		.score = score,
		.name = name,
		.dev = dev,
	};

	sched->ops = xe_ops;
	INIT_LIST_HEAD(&sched->msgs);
	INIT_WORK(&sched->work_process_msg, xe_sched_process_msg_work);

	return drm_sched_init(&sched->base, &args);
}

void xe_sched_fini(struct xe_gpu_scheduler *sched)
{
	xe_sched_submission_stop(sched);
	drm_sched_fini(&sched->base);
}

void xe_sched_submission_start(struct xe_gpu_scheduler *sched)
{
	drm_sched_wqueue_start(&sched->base);
	queue_work(sched->base.submit_wq, &sched->work_process_msg);
}

void xe_sched_submission_stop(struct xe_gpu_scheduler *sched)
{
	drm_sched_wqueue_stop(&sched->base);
	cancel_work_sync(&sched->work_process_msg);
}

void xe_sched_submission_resume_tdr(struct xe_gpu_scheduler *sched)
{
	drm_sched_resume_timeout(&sched->base, sched->base.timeout);
}

void xe_sched_add_msg(struct xe_gpu_scheduler *sched,
		      struct xe_sched_msg *msg)
{
	xe_sched_msg_lock(sched);
	xe_sched_add_msg_locked(sched, msg);
	xe_sched_msg_unlock(sched);
}

void xe_sched_add_msg_locked(struct xe_gpu_scheduler *sched,
			     struct xe_sched_msg *msg)
{
	lockdep_assert_held(&sched->base.job_list_lock);

	list_add_tail(&msg->link, &sched->msgs);
	xe_sched_process_msg_queue(sched);
}
