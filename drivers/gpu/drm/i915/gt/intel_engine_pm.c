// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"

#include "intel_breadcrumbs.h"
#include "intel_context.h"
#include "intel_engine.h"
#include "intel_engine_heartbeat.h"
#include "intel_engine_pm.h"
#include "intel_gt.h"
#include "intel_gt_pm.h"
#include "intel_rc6.h"
#include "intel_ring.h"
#include "shmem_utils.h"
#include "intel_gt_regs.h"

static void intel_gsc_idle_msg_enable(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;

	if (MEDIA_VER(i915) >= 13 && engine->id == GSC0) {
		intel_uncore_write(engine->gt->uncore,
				   RC_PSMI_CTRL_GSCCS,
				   _MASKED_BIT_DISABLE(IDLE_MSG_DISABLE));
		/* hysteresis 0xA=5us as recommended in spec*/
		intel_uncore_write(engine->gt->uncore,
				   PWRCTX_MAXCNT_GSCCS,
				   0xA);
	}
}

static void dbg_poison_ce(struct intel_context *ce)
{
	if (!IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		return;

	if (ce->state) {
		struct drm_i915_gem_object *obj = ce->state->obj;
		int type = intel_gt_coherent_map_type(ce->engine->gt, obj, true);
		void *map;

		if (!i915_gem_object_trylock(obj, NULL))
			return;

		map = i915_gem_object_pin_map(obj, type);
		if (!IS_ERR(map)) {
			memset(map, CONTEXT_REDZONE, obj->base.size);
			i915_gem_object_flush_map(obj);
			i915_gem_object_unpin_map(obj);
		}
		i915_gem_object_unlock(obj);
	}
}

static int __engine_unpark(struct intel_wakeref *wf)
{
	struct intel_engine_cs *engine =
		container_of(wf, typeof(*engine), wakeref);
	struct intel_context *ce;

	ENGINE_TRACE(engine, "\n");

	engine->wakeref_track = intel_gt_pm_get(engine->gt);

	/* Discard stale context state from across idling */
	ce = engine->kernel_context;
	if (ce) {
		GEM_BUG_ON(test_bit(CONTEXT_VALID_BIT, &ce->flags));

		/* Flush all pending HW writes before we touch the context */
		while (unlikely(intel_context_inflight(ce)))
			intel_engine_flush_submission(engine);

		/* First poison the image to verify we never fully trust it */
		dbg_poison_ce(ce);

		/* Scrub the context image after our loss of control */
		ce->ops->reset(ce);

		CE_TRACE(ce, "reset { seqno:%x, *hwsp:%x, ring:%x }\n",
			 ce->timeline->seqno,
			 READ_ONCE(*ce->timeline->hwsp_seqno),
			 ce->ring->emit);
		GEM_BUG_ON(ce->timeline->seqno !=
			   READ_ONCE(*ce->timeline->hwsp_seqno));
	}

	if (engine->unpark)
		engine->unpark(engine);

	intel_breadcrumbs_unpark(engine->breadcrumbs);
	intel_engine_unpark_heartbeat(engine);
	return 0;
}

static void duration(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct i915_request *rq = to_request(fence);

	ewma__engine_latency_add(&rq->engine->latency,
				 ktime_us_delta(rq->fence.timestamp,
						rq->duration.emitted));
}

static void
__queue_and_release_pm(struct i915_request *rq,
		       struct intel_timeline *tl,
		       struct intel_engine_cs *engine)
{
	struct intel_gt_timelines *timelines = &engine->gt->timelines;

	ENGINE_TRACE(engine, "parking\n");

	/*
	 * Open coded one half of intel_context_enter, which we have to omit
	 * here (see the large comment below) and because the other part must
	 * not be called due constructing directly with __i915_request_create
	 * which increments active count via intel_context_mark_active.
	 */
	GEM_BUG_ON(rq->context->active_count != 1);
	__intel_gt_pm_get(engine->gt);
	rq->context->wakeref = intel_wakeref_track(&engine->gt->wakeref);

	/*
	 * We have to serialise all potential retirement paths with our
	 * submission, as we don't want to underflow either the
	 * engine->wakeref.counter or our timeline->active_count.
	 *
	 * Equally, we cannot allow a new submission to start until
	 * after we finish queueing, nor could we allow that submitter
	 * to retire us before we are ready!
	 */
	spin_lock(&timelines->lock);

	/* Let intel_gt_retire_requests() retire us (acquired under lock) */
	if (!atomic_fetch_inc(&tl->active_count))
		list_add_tail(&tl->link, &timelines->active_list);

	/* Hand the request over to HW and so engine_retire() */
	__i915_request_queue_bh(rq);

	/* Let new submissions commence (and maybe retire this timeline) */
	__intel_wakeref_defer_park(&engine->wakeref);

	spin_unlock(&timelines->lock);
}

static bool switch_to_kernel_context(struct intel_engine_cs *engine)
{
	struct intel_context *ce = engine->kernel_context;
	struct i915_request *rq;
	bool result = true;

	/*
	 * This is execlist specific behaviour intended to ensure the GPU is
	 * idle by switching to a known 'safe' context. With GuC submission, the
	 * same idle guarantee is achieved by other means (disabling
	 * scheduling). Further, switching to a 'safe' context has no effect
	 * with GuC submission as the scheduler can just switch back again.
	 *
	 * FIXME: Move this backend scheduler specific behaviour into the
	 * scheduler backend.
	 */
	if (intel_engine_uses_guc(engine))
		return true;

	/* GPU is pointing to the void, as good as in the kernel context. */
	if (intel_gt_is_wedged(engine->gt))
		return true;

	GEM_BUG_ON(!intel_context_is_barrier(ce));
	GEM_BUG_ON(ce->timeline->hwsp_ggtt != engine->status_page.vma);

	/* Already inside the kernel context, safe to power down. */
	if (engine->wakeref_serial == engine->serial)
		return true;

	/*
	 * Note, we do this without taking the timeline->mutex. We cannot
	 * as we may be called while retiring the kernel context and so
	 * already underneath the timeline->mutex. Instead we rely on the
	 * exclusive property of the __engine_park that prevents anyone
	 * else from creating a request on this engine. This also requires
	 * that the ring is empty and we avoid any waits while constructing
	 * the context, as they assume protection by the timeline->mutex.
	 * This should hold true as we can only park the engine after
	 * retiring the last request, thus all rings should be empty and
	 * all timelines idle.
	 *
	 * For unlocking, there are 2 other parties and the GPU who have a
	 * stake here.
	 *
	 * A new gpu user will be waiting on the engine-pm to start their
	 * engine_unpark. New waiters are predicated on engine->wakeref.count
	 * and so intel_wakeref_defer_park() acts like a mutex_unlock of the
	 * engine->wakeref.
	 *
	 * The other party is intel_gt_retire_requests(), which is walking the
	 * list of active timelines looking for completions. Meanwhile as soon
	 * as we call __i915_request_queue(), the GPU may complete our request.
	 * Ergo, if we put ourselves on the timelines.active_list
	 * (se intel_timeline_enter()) before we increment the
	 * engine->wakeref.count, we may see the request completion and retire
	 * it causing an underflow of the engine->wakeref.
	 */
	set_bit(CONTEXT_IS_PARKING, &ce->flags);
	GEM_BUG_ON(atomic_read(&ce->timeline->active_count) < 0);

	rq = __i915_request_create(ce, GFP_NOWAIT);
	if (IS_ERR(rq))
		/* Context switch failed, hope for the best! Maybe reset? */
		goto out_unlock;

	/* Check again on the next retirement. */
	engine->wakeref_serial = engine->serial + 1;
	i915_request_add_active_barriers(rq);

	/* Install ourselves as a preemption barrier */
	rq->sched.attr.priority = I915_PRIORITY_BARRIER;
	if (likely(!__i915_request_commit(rq))) { /* engine should be idle! */
		/*
		 * Use an interrupt for precise measurement of duration,
		 * otherwise we rely on someone else retiring all the requests
		 * which may delay the signaling (i.e. we will likely wait
		 * until the background request retirement running every
		 * second or two).
		 */
		BUILD_BUG_ON(sizeof(rq->duration) > sizeof(rq->submitq));
		dma_fence_add_callback(&rq->fence, &rq->duration.cb, duration);
		rq->duration.emitted = ktime_get();
	}

	/* Expose ourselves to the world */
	__queue_and_release_pm(rq, ce->timeline, engine);

	result = false;
out_unlock:
	clear_bit(CONTEXT_IS_PARKING, &ce->flags);
	return result;
}

static void call_idle_barriers(struct intel_engine_cs *engine)
{
	struct llist_node *node, *next;

	llist_for_each_safe(node, next, llist_del_all(&engine->barrier_tasks)) {
		struct dma_fence_cb *cb =
			container_of((struct list_head *)node,
				     typeof(*cb), node);

		cb->func(ERR_PTR(-EAGAIN), cb);
	}
}

static int __engine_park(struct intel_wakeref *wf)
{
	struct intel_engine_cs *engine =
		container_of(wf, typeof(*engine), wakeref);

	engine->saturated = 0;

	/*
	 * If one and only one request is completed between pm events,
	 * we know that we are inside the kernel context and it is
	 * safe to power down. (We are paranoid in case that runtime
	 * suspend causes corruption to the active context image, and
	 * want to avoid that impacting userspace.)
	 */
	if (!switch_to_kernel_context(engine))
		return -EBUSY;

	ENGINE_TRACE(engine, "parked\n");

	call_idle_barriers(engine); /* cleanup after wedging */

	intel_engine_park_heartbeat(engine);
	intel_breadcrumbs_park(engine->breadcrumbs);

	if (engine->park)
		engine->park(engine);

	/* While gt calls i915_vma_parked(), we have to break the lock cycle */
	intel_gt_pm_put_async(engine->gt, engine->wakeref_track);
	return 0;
}

static const struct intel_wakeref_ops wf_ops = {
	.get = __engine_unpark,
	.put = __engine_park,
};

void intel_engine_init__pm(struct intel_engine_cs *engine)
{
	intel_wakeref_init(&engine->wakeref, engine->i915, &wf_ops, engine->name);
	intel_engine_init_heartbeat(engine);

	intel_gsc_idle_msg_enable(engine);
}

/**
 * intel_engine_reset_pinned_contexts - Reset the pinned contexts of
 * an engine.
 * @engine: The engine whose pinned contexts we want to reset.
 *
 * Typically the pinned context LMEM images lose or get their content
 * corrupted on suspend. This function resets their images.
 */
void intel_engine_reset_pinned_contexts(struct intel_engine_cs *engine)
{
	struct intel_context *ce;

	list_for_each_entry(ce, &engine->pinned_contexts_list,
			    pinned_contexts_link) {
		/* kernel context gets reset at __engine_unpark() */
		if (ce == engine->kernel_context)
			continue;

		dbg_poison_ce(ce);
		ce->ops->reset(ce);
	}
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_engine_pm.c"
#endif
