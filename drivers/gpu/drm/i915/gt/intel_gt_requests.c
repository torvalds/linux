/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/workqueue.h>

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

static void engine_retire(struct work_struct *work)
{
	struct intel_engine_cs *engine =
		container_of(work, typeof(*engine), retire_work);
	struct intel_timeline *tl = xchg(&engine->retire, NULL);

	do {
		struct intel_timeline *next = xchg(&tl->retire, NULL);

		/*
		 * Our goal here is to retire _idle_ timelines as soon as
		 * possible (as they are idle, we do not expect userspace
		 * to be cleaning up anytime soon).
		 *
		 * If the timeline is currently locked, either it is being
		 * retired elsewhere or about to be!
		 */
		if (mutex_trylock(&tl->mutex)) {
			retire_requests(tl);
			mutex_unlock(&tl->mutex);
		}
		intel_timeline_put(tl);

		GEM_BUG_ON(!next);
		tl = ptr_mask_bits(next, 1);
	} while (tl);
}

static bool add_retire(struct intel_engine_cs *engine,
		       struct intel_timeline *tl)
{
	struct intel_timeline *first;

	/*
	 * We open-code a llist here to include the additional tag [BIT(0)]
	 * so that we know when the timeline is already on a
	 * retirement queue: either this engine or another.
	 *
	 * However, we rely on that a timeline can only be active on a single
	 * engine at any one time and that add_retire() is called before the
	 * engine releases the timeline and transferred to another to retire.
	 */

	if (READ_ONCE(tl->retire)) /* already queued */
		return false;

	intel_timeline_get(tl);
	first = READ_ONCE(engine->retire);
	do
		tl->retire = ptr_pack_bits(first, 1, 1);
	while (!try_cmpxchg(&engine->retire, &first, tl));

	return !first;
}

void intel_engine_add_retire(struct intel_engine_cs *engine,
			     struct intel_timeline *tl)
{
	if (add_retire(engine, tl))
		schedule_work(&engine->retire_work);
}

void intel_engine_init_retire(struct intel_engine_cs *engine)
{
	INIT_WORK(&engine->retire_work, engine_retire);
}

void intel_engine_fini_retire(struct intel_engine_cs *engine)
{
	flush_work(&engine->retire_work);
	GEM_BUG_ON(engine->retire);
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
		GEM_BUG_ON(!atomic_read(&tl->active_count));
		atomic_inc(&tl->active_count); /* pin the list element */
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
		if (atomic_dec_and_test(&tl->active_count))
			list_del(&tl->link);
		else
			active_count += !!rcu_access_pointer(tl->last_request.fence);

		mutex_unlock(&tl->mutex);

		/* Defer the final release to after the spinlock */
		if (refcount_dec_and_test(&tl->kref.refcount)) {
			GEM_BUG_ON(atomic_read(&tl->active_count));
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
