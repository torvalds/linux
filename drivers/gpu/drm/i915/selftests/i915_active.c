/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/kref.h>

#include "gem/i915_gem_pm.h"
#include "gt/intel_gt.h"

#include "i915_selftest.h"

#include "igt_flush_test.h"
#include "lib_sw_fence.h"

struct live_active {
	struct i915_active base;
	struct kref ref;
	bool retired;
};

static void __live_get(struct live_active *active)
{
	kref_get(&active->ref);
}

static void __live_free(struct live_active *active)
{
	i915_active_fini(&active->base);
	kfree(active);
}

static void __live_release(struct kref *ref)
{
	struct live_active *active = container_of(ref, typeof(*active), ref);

	__live_free(active);
}

static void __live_put(struct live_active *active)
{
	kref_put(&active->ref, __live_release);
}

static int __live_active(struct i915_active *base)
{
	struct live_active *active = container_of(base, typeof(*active), base);

	__live_get(active);
	return 0;
}

static void __live_retire(struct i915_active *base)
{
	struct live_active *active = container_of(base, typeof(*active), base);

	active->retired = true;
	__live_put(active);
}

static struct live_active *__live_alloc(struct drm_i915_private *i915)
{
	struct live_active *active;

	active = kzalloc(sizeof(*active), GFP_KERNEL);
	if (!active)
		return NULL;

	kref_init(&active->ref);
	i915_active_init(i915, &active->base, __live_active, __live_retire);

	return active;
}

static struct live_active *
__live_active_setup(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	struct i915_sw_fence *submit;
	struct live_active *active;
	enum intel_engine_id id;
	unsigned int count = 0;
	int err = 0;

	active = __live_alloc(i915);
	if (!active)
		return ERR_PTR(-ENOMEM);

	submit = heap_fence_create(GFP_KERNEL);
	if (!submit) {
		kfree(active);
		return ERR_PTR(-ENOMEM);
	}

	err = i915_active_acquire(&active->base);
	if (err)
		goto out;

	for_each_engine(engine, i915, id) {
		struct i915_request *rq;

		rq = i915_request_create(engine->kernel_context);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			break;
		}

		err = i915_sw_fence_await_sw_fence_gfp(&rq->submit,
						       submit,
						       GFP_KERNEL);
		if (err >= 0)
			err = i915_active_ref(&active->base, rq->timeline, rq);
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
	if (atomic_read(&active->base.count) != count) {
		pr_err("i915_active not tracking all requests, found %d, expected %d\n",
		       atomic_read(&active->base.count), count);
		err = -EINVAL;
	}

out:
	i915_sw_fence_commit(submit);
	heap_fence_put(submit);
	if (err) {
		__live_put(active);
		active = ERR_PTR(err);
	}

	return active;
}

static int live_active_wait(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct live_active *active;
	intel_wakeref_t wakeref;
	int err = 0;

	/* Check that we get a callback when requests retire upon waiting */

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	active = __live_active_setup(i915);
	if (IS_ERR(active)) {
		err = PTR_ERR(active);
		goto err;
	}

	i915_active_wait(&active->base);
	if (!active->retired) {
		pr_err("i915_active not retired after waiting!\n");
		err = -EINVAL;
	}

	__live_put(active);

	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		err = -EIO;

err:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);

	return err;
}

static int live_active_retire(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct live_active *active;
	intel_wakeref_t wakeref;
	int err = 0;

	/* Check that we get a callback when requests are indirectly retired */

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	active = __live_active_setup(i915);
	if (IS_ERR(active)) {
		err = PTR_ERR(active);
		goto err;
	}

	/* waits for & retires all requests */
	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		err = -EIO;

	if (!active->retired) {
		pr_err("i915_active not retired after flushing!\n");
		err = -EINVAL;
	}

	__live_put(active);

err:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);

	return err;
}

int i915_active_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_active_wait),
		SUBTEST(live_active_retire),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return i915_subtests(tests, i915);
}
