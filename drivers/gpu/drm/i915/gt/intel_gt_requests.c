/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h" /* for_each_engine() */
#include "i915_request.h"
#include "intel_gt.h"
#include "intel_gt_pm.h"
#include "intel_gt_requests.h"
#include "intel_timeline.h"

static void retire_requests(struct intel_timeline *tl)
{
	struct i915_request *rq, *rn;

	list_for_each_entry_safe(rq, rn, &tl->requests, link)
		if (!i915_request_retire(rq))
			break;
}

static void flush_submission(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt, id)
		intel_engine_flush_submission(engine);
}

long intel_gt_retire_requests_timeout(struct intel_gt *gt, long timeout)
{
	struct intel_gt_timelines *timelines = &gt->timelines;
	struct intel_timeline *tl, *tn;
	unsigned long active_count = 0;
	unsigned long flags;
	bool interruptible;
	LIST_HEAD(free);

	interruptible = true;
	if (unlikely(timeout < 0))
		timeout = -timeout, interruptible = false;

	flush_submission(gt); /* kick the ksoftirqd tasklets */

	spin_lock_irqsave(&timelines->lock, flags);
	list_for_each_entry_safe(tl, tn, &timelines->active_list, link) {
		if (!mutex_trylock(&tl->mutex)) {
			active_count++; /* report busy to caller, try again? */
			continue;
		}

		intel_timeline_get(tl);
		GEM_BUG_ON(!tl->active_count);
		tl->active_count++; /* pin the list element */
		spin_unlock_irqrestore(&timelines->lock, flags);

		if (timeout > 0) {
			struct dma_fence *fence;

			fence = i915_active_fence_get(&tl->last_request);
			if (fence) {
				timeout = dma_fence_wait_timeout(fence,
								 interruptible,
								 timeout);
				dma_fence_put(fence);
			}
		}

		retire_requests(tl);

		spin_lock_irqsave(&timelines->lock, flags);

		/* Resume iteration after dropping lock */
		list_safe_reset_next(tl, tn, link);
		if (--tl->active_count)
			active_count += !!rcu_access_pointer(tl->last_request.fence);
		else
			list_del(&tl->link);

		mutex_unlock(&tl->mutex);

		/* Defer the final release to after the spinlock */
		if (refcount_dec_and_test(&tl->kref.refcount)) {
			GEM_BUG_ON(tl->active_count);
			list_add(&tl->link, &free);
		}
	}
	spin_unlock_irqrestore(&timelines->lock, flags);

	list_for_each_entry_safe(tl, tn, &free, link)
		__intel_timeline_free(&tl->kref);

	return active_count ? timeout : 0;
}

int intel_gt_wait_for_idle(struct intel_gt *gt, long timeout)
{
	/* If the device is asleep, we have no requests outstanding */
	if (!intel_gt_pm_is_awake(gt))
		return 0;

	while ((timeout = intel_gt_retire_requests_timeout(gt, timeout)) > 0) {
		cond_resched();
		if (signal_pending(current))
			return -EINTR;
	}

	return timeout;
}

static void retire_work_handler(struct work_struct *work)
{
	struct intel_gt *gt =
		container_of(work, typeof(*gt), requests.retire_work.work);

	intel_gt_retire_requests(gt);
	schedule_delayed_work(&gt->requests.retire_work,
			      round_jiffies_up_relative(HZ));
}

void intel_gt_init_requests(struct intel_gt *gt)
{
	INIT_DELAYED_WORK(&gt->requests.retire_work, retire_work_handler);
}

void intel_gt_park_requests(struct intel_gt *gt)
{
	cancel_delayed_work(&gt->requests.retire_work);
}

void intel_gt_unpark_requests(struct intel_gt *gt)
{
	schedule_delayed_work(&gt->requests.retire_work,
			      round_jiffies_up_relative(HZ));
}
