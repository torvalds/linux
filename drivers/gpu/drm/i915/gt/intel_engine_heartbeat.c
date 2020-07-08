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
#include "intel_reset.h"

/*
 * While the engine is active, we send a periodic pulse along the engine
 * to check on its health and to flush any idle-barriers. If that request
 * is stuck, and we fail to preempt it, we declare the engine hung and
 * issue a reset -- in the hope that restores progress.
 */

static bool next_heartbeat(struct intel_engine_cs *engine)
{
	long delay;

	delay = READ_ONCE(engine->props.heartbeat_interval_ms);
	if (!delay)
		return false;

	delay = msecs_to_jiffies_timeout(delay);
	if (delay >= HZ)
		delay = round_jiffies_up_relative(delay);
	mod_delayed_work(system_wq, &engine->heartbeat.work, delay);

	return true;
}

static void idle_pulse(struct intel_engine_cs *engine, struct i915_request *rq)
{
	engine->wakeref_serial = READ_ONCE(engine->serial) + 1;
	i915_request_add_active_barriers(rq);
}

static void show_heartbeat(const struct i915_request *rq,
			   struct intel_engine_cs *engine)
{
	struct drm_printer p = drm_debug_printer("heartbeat");

	intel_engine_dump(engine, &p,
			  "%s heartbeat {prio:%d} not ticking\n",
			  engine->name,
			  rq->sched.attr.priority);
}

static void heartbeat(struct work_struct *wrk)
{
	struct i915_sched_attr attr = {
		.priority = I915_USER_PRIORITY(I915_PRIORITY_MIN),
	};
	struct intel_engine_cs *engine =
		container_of(wrk, typeof(*engine), heartbeat.work.work);
	struct intel_context *ce = engine->kernel_context;
	struct i915_request *rq;

	rq = engine->heartbeat.systole;
	if (rq && i915_request_completed(rq)) {
		i915_request_put(rq);
		engine->heartbeat.systole = NULL;
	}

	if (!intel_engine_pm_get_if_awake(engine))
		return;

	if (intel_gt_is_wedged(engine->gt))
		goto out;

	if (engine->heartbeat.systole) {
		if (engine->schedule &&
		    rq->sched.attr.priority < I915_PRIORITY_BARRIER) {
			/*
			 * Gradually raise the priority of the heartbeat to
			 * give high priority work [which presumably desires
			 * low latency and no jitter] the chance to naturally
			 * complete before being preempted.
			 */
			attr.priority = I915_PRIORITY_MASK;
			if (rq->sched.attr.priority >= attr.priority)
				attr.priority |= I915_USER_PRIORITY(I915_PRIORITY_HEARTBEAT);
			if (rq->sched.attr.priority >= attr.priority)
				attr.priority = I915_PRIORITY_BARRIER;

			local_bh_disable();
			engine->schedule(rq, &attr);
			local_bh_enable();
		} else {
			if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
				show_heartbeat(rq, engine);

			intel_gt_handle_error(engine->gt, engine->mask,
					      I915_ERROR_CAPTURE,
					      "stopped heartbeat on %s",
					      engine->name);
		}
		goto out;
	}

	if (engine->wakeref_serial == engine->serial)
		goto out;

	mutex_lock(&ce->timeline->mutex);

	intel_context_enter(ce);
	rq = __i915_request_create(ce, GFP_NOWAIT | __GFP_NOWARN);
	intel_context_exit(ce);
	if (IS_ERR(rq))
		goto unlock;

	idle_pulse(engine, rq);
	if (i915_modparams.enable_hangcheck)
		engine->heartbeat.systole = i915_request_get(rq);

	__i915_request_commit(rq);
	__i915_request_queue(rq, &attr);

unlock:
	mutex_unlock(&ce->timeline->mutex);
out:
	if (!next_heartbeat(engine))
		i915_request_put(fetch_and_zero(&engine->heartbeat.systole));
	intel_engine_pm_put(engine);
}

void intel_engine_unpark_heartbeat(struct intel_engine_cs *engine)
{
	if (!IS_ACTIVE(CONFIG_DRM_I915_HEARTBEAT_INTERVAL))
		return;

	next_heartbeat(engine);
}

void intel_engine_park_heartbeat(struct intel_engine_cs *engine)
{
	if (cancel_delayed_work(&engine->heartbeat.work))
		i915_request_put(fetch_and_zero(&engine->heartbeat.systole));
}

void intel_engine_init_heartbeat(struct intel_engine_cs *engine)
{
	INIT_DELAYED_WORK(&engine->heartbeat.work, heartbeat);
}

int intel_engine_set_heartbeat(struct intel_engine_cs *engine,
			       unsigned long delay)
{
	int err;

	/* Send one last pulse before to cleanup persistent hogs */
	if (!delay && IS_ACTIVE(CONFIG_DRM_I915_PREEMPT_TIMEOUT)) {
		err = intel_engine_pulse(engine);
		if (err)
			return err;
	}

	WRITE_ONCE(engine->props.heartbeat_interval_ms, delay);

	if (intel_engine_pm_get_if_awake(engine)) {
		if (delay)
			intel_engine_unpark_heartbeat(engine);
		else
			intel_engine_park_heartbeat(engine);
		intel_engine_pm_put(engine);
	}

	return 0;
}

int intel_engine_pulse(struct intel_engine_cs *engine)
{
	struct i915_sched_attr attr = { .priority = I915_PRIORITY_BARRIER };
	struct intel_context *ce = engine->kernel_context;
	struct i915_request *rq;
	int err;

	if (!intel_engine_has_preemption(engine))
		return -ENODEV;

	if (!intel_engine_pm_get_if_awake(engine))
		return 0;

	if (mutex_lock_interruptible(&ce->timeline->mutex)) {
		err = -EINTR;
		goto out_rpm;
	}

	intel_context_enter(ce);
	rq = __i915_request_create(ce, GFP_NOWAIT | __GFP_NOWARN);
	intel_context_exit(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_unlock;
	}

	__set_bit(I915_FENCE_FLAG_SENTINEL, &rq->fence.flags);
	idle_pulse(engine, rq);

	__i915_request_commit(rq);
	__i915_request_queue(rq, &attr);
	GEM_BUG_ON(rq->sched.attr.priority < I915_PRIORITY_BARRIER);
	err = 0;

out_unlock:
	mutex_unlock(&ce->timeline->mutex);
out_rpm:
	intel_engine_pm_put(engine);
	return err;
}

int intel_engine_flush_barriers(struct intel_engine_cs *engine)
{
	struct i915_request *rq;
	int err = 0;

	if (llist_empty(&engine->barrier_tasks))
		return 0;

	if (!intel_engine_pm_get_if_awake(engine))
		return 0;

	rq = i915_request_create(engine->kernel_context);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_rpm;
	}

	idle_pulse(engine, rq);
	i915_request_add(rq);

out_rpm:
	intel_engine_pm_put(engine);
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_engine_heartbeat.c"
#endif
