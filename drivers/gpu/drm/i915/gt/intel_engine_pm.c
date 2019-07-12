/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"

#include "intel_engine.h"
#include "intel_engine_pm.h"
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

void intel_engine_pm_get(struct intel_engine_cs *engine)
{
	intel_wakeref_get(&engine->i915->runtime_pm, &engine->wakeref, __engine_unpark);
}

void intel_engine_park(struct intel_engine_cs *engine)
{
	/*
	 * We are committed now to parking this engine, make sure there
	 * will be no more interrupts arriving later and the engine
	 * is truly idle.
	 */
	if (wait_for(intel_engine_is_idle(engine), 10)) {
		struct drm_printer p = drm_debug_printer(__func__);

		dev_err(engine->i915->drm.dev,
			"%s is not idle before parking\n",
			engine->name);
		intel_engine_dump(engine, &p, NULL);
	}
}

static bool switch_to_kernel_context(struct intel_engine_cs *engine)
{
	struct i915_request *rq;

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
	rq = __i915_request_create(engine->kernel_context, GFP_NOWAIT);
	if (IS_ERR(rq))
		/* Context switch failed, hope for the best! Maybe reset? */
		return true;

	/* Check again on the next retirement. */
	engine->wakeref_serial = engine->serial + 1;

	i915_request_add_barriers(rq);
	__i915_request_commit(rq);

	return false;
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

void intel_engine_pm_put(struct intel_engine_cs *engine)
{
	intel_wakeref_put(&engine->i915->runtime_pm, &engine->wakeref, __engine_park);
}

void intel_engine_init__pm(struct intel_engine_cs *engine)
{
	intel_wakeref_init(&engine->wakeref);
}
