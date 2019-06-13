// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include "i915_selftest.h"
#include "selftests/igt_reset.h"
#include "selftests/igt_atomic.h"

static int igt_global_reset(void *arg)
{
	struct drm_i915_private *i915 = arg;
	unsigned int reset_count;
	int err = 0;

	/* Check that we can issue a global GPU reset */

	igt_global_reset_lock(i915);

	reset_count = i915_reset_count(&i915->gpu_error);

	i915_reset(i915, ALL_ENGINES, NULL);

	if (i915_reset_count(&i915->gpu_error) == reset_count) {
		pr_err("No GPU reset recorded!\n");
		err = -EINVAL;
	}

	igt_global_reset_unlock(i915);

	if (i915_reset_failed(i915))
		err = -EIO;

	return err;
}

static int igt_wedged_reset(void *arg)
{
	struct drm_i915_private *i915 = arg;
	intel_wakeref_t wakeref;

	/* Check that we can recover a wedged device with a GPU reset */

	igt_global_reset_lock(i915);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	i915_gem_set_wedged(i915);

	GEM_BUG_ON(!i915_reset_failed(i915));
	i915_reset(i915, ALL_ENGINES, NULL);

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	igt_global_reset_unlock(i915);

	return i915_reset_failed(i915) ? -EIO : 0;
}

static int igt_atomic_reset(void *arg)
{
	struct drm_i915_private *i915 = arg;
	const typeof(*igt_atomic_phases) *p;
	int err = 0;

	/* Check that the resets are usable from atomic context */

	igt_global_reset_lock(i915);
	mutex_lock(&i915->drm.struct_mutex);

	/* Flush any requests before we get started and check basics */
	if (!igt_force_reset(i915))
		goto unlock;

	for (p = igt_atomic_phases; p->name; p++) {
		GEM_TRACE("intel_gpu_reset under %s\n", p->name);

		p->critical_section_begin();
		reset_prepare(i915);
		err = intel_gpu_reset(i915, ALL_ENGINES);
		reset_finish(i915);
		p->critical_section_end();

		if (err) {
			pr_err("intel_gpu_reset failed under %s\n", p->name);
			break;
		}
	}

	/* As we poke around the guts, do a full reset before continuing. */
	igt_force_reset(i915);

unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	igt_global_reset_unlock(i915);

	return err;
}

int intel_reset_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_global_reset), /* attempt to recover GPU first */
		SUBTEST(igt_wedged_reset),
		SUBTEST(igt_atomic_reset),
	};
	intel_wakeref_t wakeref;
	int err = 0;

	if (!intel_has_gpu_reset(i915))
		return 0;

	if (i915_terminally_wedged(i915))
		return -EIO; /* we're long past hope of a successful reset */

	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		err = i915_subtests(tests, i915);

	return err;
}
