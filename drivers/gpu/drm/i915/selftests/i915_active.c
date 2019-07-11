/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "../i915_selftest.h"

#include "igt_flush_test.h"
#include "lib_sw_fence.h"

struct live_active {
	struct i915_active base;
	bool retired;
};

static void __live_active_retire(struct i915_active *base)
{
	struct live_active *active = container_of(base, typeof(*active), base);

	active->retired = true;
}

static int __live_active_setup(struct drm_i915_private *i915,
			       struct live_active *active)
{
	struct intel_engine_cs *engine;
	struct i915_sw_fence *submit;
	enum intel_engine_id id;
	unsigned int count = 0;
	int err = 0;

	submit = heap_fence_create(GFP_KERNEL);
	if (!submit)
		return -ENOMEM;

	i915_active_init(i915, &active->base, __live_active_retire);
	active->retired = false;

	if (!i915_active_acquire(&active->base)) {
		pr_err("First i915_active_acquire should report being idle\n");
		err = -EINVAL;
		goto out;
	}

	for_each_engine(engine, i915, id) {
		struct i915_request *rq;

		rq = i915_request_alloc(engine, i915->kernel_context);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			break;
		}

		err = i915_sw_fence_await_sw_fence_gfp(&rq->submit,
						       submit,
						       GFP_KERNEL);
		if (err >= 0)
			err = i915_active_ref(&active->base,
					      rq->fence.context, rq);
		i915_request_add(rq);
		if (err) {
			pr_err("Failed to track active ref!\n");
			break;
		}

		count++;
	}

	i915_active_release(&active->base);
	if (active->retired && count) {
		pr_err("i915_active retired before submission!\n");
		err = -EINVAL;
	}
	if (active->base.count != count) {
		pr_err("i915_active not tracking all requests, found %d, expected %d\n",
		       active->base.count, count);
		err = -EINVAL;
	}

out:
	i915_sw_fence_commit(submit);
	heap_fence_put(submit);

	return err;
}

static int live_active_wait(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct live_active active;
	intel_wakeref_t wakeref;
	int err;

	/* Check that we get a callback when requests retire upon waiting */

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	err = __live_active_setup(i915, &active);

	i915_active_wait(&active.base);
	if (!active.retired) {
		pr_err("i915_active not retired after waiting!\n");
		err = -EINVAL;
	}

	i915_active_fini(&active.base);
	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		err = -EIO;

	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int live_active_retire(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct live_active active;
	intel_wakeref_t wakeref;
	int err;

	/* Check that we get a callback when requests are indirectly retired */

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	err = __live_active_setup(i915, &active);

	/* waits for & retires all requests */
	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		err = -EIO;

	if (!active.retired) {
		pr_err("i915_active not retired after flushing!\n");
		err = -EINVAL;
	}

	i915_active_fini(&active.base);
	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

int i915_active_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_active_wait),
		SUBTEST(live_active_retire),
	};

	if (i915_terminally_wedged(&i915->gpu_error))
		return 0;

	return i915_subtests(tests, i915);
}
