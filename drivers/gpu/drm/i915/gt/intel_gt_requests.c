/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/workqueue.h>

#include "i915_drv.h" /* for_each_engine() */
#include "i915_request.h"
#include "intel_engine_heartbeat.h"
#include "intel_gt.h"
#include "intel_gt_pm.h"
#include "intel_gt_requests.h"
#include "intel_timeline.h"

static bool retire_requests(struct intel_timeline *tl)
{
	struct i915_request *rq, *rn;

	list_for_each_entry_safe(rq, rn, &tl->requests, link)
		if (!i915_request_retire(rq))
			return false;

	/* And check nothing new was submitted */
	return !i915_active_fence_isset(&tl->last_request);
}

static bool engine_active(const struct intel_engine_cs *engine)
{
	return !list_empty(&engine->kernel_context->timeline->requests);
}

static bool flush_submission(struct intel_gt *gt, long timeout)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	bool active = false;

	if (!timeout)
		return false;

	if (!intel_gt_pm_is_awake(gt))
		return false;

	for_each_engine(engine, gt, id) {
		intel_engine_flush_submission(engine);

		/* Flush the background retirement and idle barriers */
		flush_work(&engine->retire_work);
		flush_delayed_work(&engine->wakeref.work);

		/* Is the idle barrier still outstanding? */
		active |= engine_active(engine);
	}

	return active;
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
#define STUB ((struct intel_timeline *)1)
	struct intel_timeline *first;

	/*
	 * We open-code a llist here to include the additional tag [BIT(0)]
	 * so that we know when the timeline is already on a
	 * retirement queue: either this engine or another.
	 */

	if (cmpxchg(&tl->retire, NULL, STUB)) /* already queued */
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
	/* We don't deal well with the engine disappearing beneath us */
	GEM_BUG_ON(intel_engine_is_virtual(engine));

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
	bool interruptible;
	LIST_HEAD(free);

	interruptible = true;
	if (unlikely(timeout < 0))
		timeout = -timeout, interruptible = false;

	flush_submission(gt, timeout); /* kick the ksoftirqd tasklets */
	spin_lock(&timelines->lock);
	list_for_each_entry_safe(tl, tn, &timelines->active_list, link) {
		if (!mutex_trylock(&tl->mutex)) {
			active_count++; /* report busy to caller, try again? */
			continue;
		}

		intel_timeline_get(tl);
		GEM_BUG_ON(!atomic_read(&tl->active_count));
		atomic_inc(&tl->active_count); /* pin the list element */
		spin_unlock(&timelines->lock);

		if (timeout > 0) {
			struct dma_fence *fence;

			fence = i915_active_fence_get(&tl->last_request);
			if (fence) {
				mutex_unlock(&tl->mutex);

				timeout = dma_fence_wait_timeout(fence,
								 interruptible,
								 timeout);
				dma_fence_put(fence);

				/* Retirement is best effort */
				if (!mutex_trylock(&tl->mutex)) {
					active_count++;
					goto out_active;
				}
			}
		}

		if (!retire_requests(tl))
			active_count++;
		mutex_unlock(&tl->mutex);

out_active:	spin_lock(&timelines->lock);

		/* Resume list iteration after reacquiring spinlock */
		list_safe_reset_next(tl, tn, link);
		if (atomic_dec_and_test(&tl->active_count))
			list_del(&tl->link);

		/* Defer the final release to after the spinlock */
		if (refcount_dec_and_test(&tl->kref.refcount)) {
			GEM_BUG_ON(atomic_read(&tl->active_count));
			list_add(&tl->link, &free);
		}
	}
	spin_unlock(&timelines->lock);

	list_for_each_entry_safe(tl, tn, &free, link)
		__intel_timeline_free(&tl->kref);

	if (flush_submission(gt, timeout)) /* Wait, there's more! */
		active_count++;

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

	schedule_delayed_work(&gt->requests.retire_work,
			      round_jiffies_up_relative(HZ));
	intel_gt_retire_requests(gt);
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

void intel_gt_fini_requests(struct intel_gt *gt)
{
	/* Wait until the work is marked as finished before unloading! */
	cancel_delayed_work_sync(&gt->requests.retire_work);
}
