/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_request.h"

#include "intel_context.h"
#include "intel_engine_heartbeat.h"
#include "intel_engine_pm.h"
#include "intel_engine.h"
#include "intel_gt.h"

static void idle_pulse(struct intel_engine_cs *engine, struct i915_request *rq)
{
	engine->wakeref_serial = READ_ONCE(engine->serial) + 1;
	i915_request_add_active_barriers(rq);
}

int intel_engine_pulse(struct intel_engine_cs *engine)
{
	struct i915_sched_attr attr = { .priority = I915_PRIORITY_BARRIER };
	struct intel_context *ce = engine->kernel_context;
	struct i915_request *rq;
	int err = 0;

	if (!intel_engine_has_preemption(engine))
		return -ENODEV;

	if (!intel_engine_pm_get_if_awake(engine))
		return 0;

	if (mutex_lock_interruptible(&ce->timeline->mutex))
		goto out_rpm;

	intel_context_enter(ce);
	rq = __i915_request_create(ce, GFP_NOWAIT | __GFP_NOWARN);
	intel_context_exit(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_unlock;
	}

	rq->flags |= I915_REQUEST_SENTINEL;
	idle_pulse(engine, rq);

	__i915_request_commit(rq);
	__i915_request_queue(rq, &attr);

out_unlock:
	mutex_unlock(&ce->timeline->mutex);
out_rpm:
	intel_engine_pm_put(engine);
	return err;
}

int intel_engine_flush_barriers(struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	if (llist_empty(&engine->barrier_tasks))
		return 0;

	rq = i915_request_create(engine->kernel_context);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	idle_pulse(engine, rq);
	i915_request_add(rq);

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_engine_heartbeat.c"
#endif
