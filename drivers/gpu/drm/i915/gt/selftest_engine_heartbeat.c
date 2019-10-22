/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "i915_drv.h"

#include "intel_gt_requests.h"
#include "i915_selftest.h"

struct pulse {
	struct i915_active active;
	struct kref kref;
};

static int pulse_active(struct i915_active *active)
{
	kref_get(&container_of(active, struct pulse, active)->kref);
	return 0;
}

static void pulse_free(struct kref *kref)
{
	kfree(container_of(kref, struct pulse, kref));
}

static void pulse_put(struct pulse *p)
{
	kref_put(&p->kref, pulse_free);
}

static void pulse_retire(struct i915_active *active)
{
	pulse_put(container_of(active, struct pulse, active));
}

static struct pulse *pulse_create(void)
{
	struct pulse *p;

	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return p;

	kref_init(&p->kref);
	i915_active_init(&p->active, pulse_active, pulse_retire);

	return p;
}

static void pulse_unlock_wait(struct pulse *p)
{
	mutex_lock(&p->active.mutex);
	mutex_unlock(&p->active.mutex);
}

static int __live_idle_pulse(struct intel_engine_cs *engine,
			     int (*fn)(struct intel_engine_cs *cs))
{
	struct pulse *p;
	int err;

	GEM_BUG_ON(!intel_engine_pm_is_awake(engine));

	p = pulse_create();
	if (!p)
		return -ENOMEM;

	err = i915_active_acquire(&p->active);
	if (err)
		goto out;

	err = i915_active_acquire_preallocate_barrier(&p->active, engine);
	if (err) {
		i915_active_release(&p->active);
		goto out;
	}

	i915_active_acquire_barrier(&p->active);
	i915_active_release(&p->active);

	GEM_BUG_ON(i915_active_is_idle(&p->active));
	GEM_BUG_ON(llist_empty(&engine->barrier_tasks));

	err = fn(engine);
	if (err)
		goto out;

	GEM_BUG_ON(!llist_empty(&engine->barrier_tasks));

	if (intel_gt_retire_requests_timeout(engine->gt, HZ / 5)) {
		err = -ETIME;
		goto out;
	}

	pulse_unlock_wait(p); /* synchronize with the retirement callback */

	if (!i915_active_is_idle(&p->active)) {
		pr_err("%s: heartbeat pulse did not flush idle tasks\n",
		       engine->name);
		err = -EINVAL;
		goto out;
	}

out:
	pulse_put(p);
	return err;
}

static int live_idle_flush(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/* Check that we can flush the idle barriers */

	for_each_engine(engine, gt, id) {
		intel_engine_pm_get(engine);
		err = __live_idle_pulse(engine, intel_engine_flush_barriers);
		intel_engine_pm_put(engine);
		if (err)
			break;
	}

	return err;
}

static int live_idle_pulse(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/* Check that heartbeat pulses flush the idle barriers */

	for_each_engine(engine, gt, id) {
		intel_engine_pm_get(engine);
		err = __live_idle_pulse(engine, intel_engine_pulse);
		intel_engine_pm_put(engine);
		if (err && err != -ENODEV)
			break;

		err = 0;
	}

	return err;
}

int intel_heartbeat_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_idle_flush),
		SUBTEST(live_idle_pulse),
	};
	int saved_hangcheck;
	int err;

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	saved_hangcheck = i915_modparams.enable_hangcheck;
	i915_modparams.enable_hangcheck = INT_MAX;

	err =  intel_gt_live_subtests(tests, &i915->gt);

	i915_modparams.enable_hangcheck = saved_hangcheck;
	return err;
}
