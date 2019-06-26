/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "../i915_drv.h"

#include "../i915_selftest.h"
#include "igt_flush_test.h"
#include "igt_live_test.h"

int igt_live_test_begin(struct igt_live_test *t,
			struct drm_i915_private *i915,
			const char *func,
			const char *name)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err;

	lockdep_assert_held(&i915->drm.struct_mutex);

	t->i915 = i915;
	t->func = func;
	t->name = name;

	err = i915_gem_wait_for_idle(i915,
				     I915_WAIT_INTERRUPTIBLE |
				     I915_WAIT_LOCKED,
				     MAX_SCHEDULE_TIMEOUT);
	if (err) {
		pr_err("%s(%s): failed to idle before, with err=%d!",
		       func, name, err);
		return err;
	}

	t->reset_global = i915_reset_count(&i915->gpu_error);

	for_each_engine(engine, i915, id)
		t->reset_engine[id] =
			i915_reset_engine_count(&i915->gpu_error, engine);

	return 0;
}

int igt_live_test_end(struct igt_live_test *t)
{
	struct drm_i915_private *i915 = t->i915;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	lockdep_assert_held(&i915->drm.struct_mutex);

	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		return -EIO;

	if (t->reset_global != i915_reset_count(&i915->gpu_error)) {
		pr_err("%s(%s): GPU was reset %d times!\n",
		       t->func, t->name,
		       i915_reset_count(&i915->gpu_error) - t->reset_global);
		return -EIO;
	}

	for_each_engine(engine, i915, id) {
		if (t->reset_engine[id] ==
		    i915_reset_engine_count(&i915->gpu_error, engine))
			continue;

		pr_err("%s(%s): engine '%s' was reset %d times!\n",
		       t->func, t->name, engine->name,
		       i915_reset_engine_count(&i915->gpu_error, engine) -
		       t->reset_engine[id]);
		return -EIO;
	}

	return 0;
}
