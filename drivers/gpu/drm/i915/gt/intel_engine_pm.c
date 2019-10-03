/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"

#include "intel_engine.h"
#include "intel_engine_pm.h"
#include "intel_engine_pool.h"
#include "intel_gt.h"
#include "intel_gt_pm.h"

static int __engine_unpark(struct intel_wakeref *wf)
{
	struct intel_engine_cs *engine =
		container_of(wf, typeof(*engine), wakeref);
	void *map;

	GEM_TRACE("%s\n", engine->name);

	intel_gt_pm_get(engine->gt);

	/* Pin the default state for fast resets from atomic context. */
	map = NULL;
	if (engine->default_state)
		map = i915_gem_object_pin_map(engine->default_state,
					      I915_MAP_WB);
	if (!IS_ERR_OR_NULL(map))
		engine->pinned_default_state = map;

	if (engine->unpark)
		engine->unpark(engine);

	intel_engine_init_hangcheck(engine);
	return 0;
}

#if IS_ENABLED(CONFIG_LOCKDEP)

static inline unsigned long __timeline_mark_lock(struct intel_context *ce)
{
	unsigned long flags;

	local_irq_save(flags);
	mutex_acquire(&ce->timeline->mutex.dep_map, 2, 0, _THIS_IP_);

	return flags;
}

static inline void __timeline_mark_unlock(struct intel_context *ce,
					  unsigned long flags)
{
	mutex_release(&ce->timeline->mutex.dep_map, 0, _THIS_IP_);
	local_irq_restore(flags);
}

#else

static inline unsigned long __timeline_mark_lock(struct intel_context *ce)
{
	return 0;
}

static inline void __timeline_mark_unlock(struct intel_context *ce,
					  unsigned long flags)
{
}

#endif /* !IS_ENABLED(CONFIG_LOCKDEP) */

static bool switch_to_kernel_context(struct intel_engine_cs *engine)
{
	struct i915_request *rq;
	unsigned long flags;
	bool result = true;

	/* Already inside the kernel context, safe to power down. */
	if (engine->wakeref_serial == engine->serial)
		return true;

	/* GPU is pointing to the void, as good as in the kernel context. */
	if (intel_gt_is_wedged(engine->gt))
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
	 */
	flags = __timeline_mark_lock(engine->kernel_context);

	rq = __i915_request_create(engine->kernel_context, GFP_NOWAIT);
	if (IS_ERR(rq))
		/* Context switch failed, hope for the best! Maybe reset? */
		goto out_unlock;

	intel_timeline_enter(rq->timeline);

	/* Check again on the next retirement. */
	engine->wakeref_serial = engine->serial + 1;
	i915_request_add_active_barriers(rq);

	/* Install ourselves as a preemption barrier */
	rq->sched.attr.priority = I915_PRIORITY_UNPREEMPTABLE;
	__i915_request_commit(rq);

	/* Release our exclusive hold on the engine */
	__intel_wakeref_defer_park(&engine->wakeref);
	__i915_request_queue(rq, NULL);

	result = false;
out_unlock:
	__timeline_mark_unlock(engine->kernel_context, flags);
	return result;
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

	GEM_TRACE("%s\n", engine->name);

	intel_engine_disarm_breadcrumbs(engine);
	intel_engine_pool_park(&engine->pool);

	/* Must be reset upon idling, or we may miss the busy wakeup. */
	GEM_BUG_ON(engine->execlists.queue_priority_hint != INT_MIN);

	if (engine->park)
		engine->park(engine);

	if (engine->pinned_default_state) {
		i915_gem_object_unpin_map(engine->default_state);
		engine->pinned_default_state = NULL;
	}

	engine->execlists.no_priolist = false;

	intel_gt_pm_put(engine->gt);
	return 0;
}

static const struct intel_wakeref_ops wf_ops = {
	.get = __engine_unpark,
	.put = __engine_park,
};

void intel_engine_init__pm(struct intel_engine_cs *engine)
{
	struct intel_runtime_pm *rpm = &engine->i915->runtime_pm;

	intel_wakeref_init(&engine->wakeref, rpm, &wf_ops);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_engine_pm.c"
#endif
