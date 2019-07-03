/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/random.h>

#include "gem/selftests/igt_gem_utils.h"
#include "gem/selftests/mock_context.h"

#include "i915_selftest.h"

#include "igt_flush_test.h"
#include "mock_drm.h"

static int switch_to_context(struct drm_i915_private *i915,
			     struct i915_gem_context *ctx)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, i915, id) {
		struct i915_request *rq;

		rq = igt_request_alloc(ctx, engine);
		if (IS_ERR(rq))
			return PTR_ERR(rq);

		i915_request_add(rq);
	}

	return 0;
}

static void trash_stolen(struct drm_i915_private *i915)
{
	struct i915_ggtt *ggtt = &i915->ggtt;
	const u64 slot = ggtt->error_capture.start;
	const resource_size_t size = resource_size(&i915->dsm);
	unsigned long page;
	u32 prng = 0x12345678;

	for (page = 0; page < size; page += PAGE_SIZE) {
		const dma_addr_t dma = i915->dsm.start + page;
		u32 __iomem *s;
		int x;

		ggtt->vm.insert_page(&ggtt->vm, dma, slot, I915_CACHE_NONE, 0);

		s = io_mapping_map_atomic_wc(&ggtt->iomap, slot);
		for (x = 0; x < PAGE_SIZE / sizeof(u32); x++) {
			prng = next_pseudo_random32(prng);
			iowrite32(prng, &s[x]);
		}
		io_mapping_unmap_atomic(s);
	}

	ggtt->vm.clear_range(&ggtt->vm, slot, PAGE_SIZE);
}

static void simulate_hibernate(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	/*
	 * As a final sting in the tail, invalidate stolen. Under a real S4,
	 * stolen is lost and needs to be refilled on resume. However, under
	 * CI we merely do S4-device testing (as full S4 is too unreliable
	 * for automated testing across a cluster), so to simulate the effect
	 * of stolen being trashed across S4, we trash it ourselves.
	 */
	trash_stolen(i915);

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
}

static int pm_prepare(struct drm_i915_private *i915)
{
	i915_gem_suspend(i915);

	return 0;
}

static void pm_suspend(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		i915_gem_suspend_gtt_mappings(i915);
		i915_gem_suspend_late(i915);
	}
}

static void pm_hibernate(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		i915_gem_suspend_gtt_mappings(i915);

		i915_gem_freeze(i915);
		i915_gem_freeze_late(i915);
	}
}

static void pm_resume(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref;

	/*
	 * Both suspend and hibernate follow the same wakeup path and assume
	 * that runtime-pm just works.
	 */
	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		intel_gt_sanitize(&i915->gt, false);
		i915_gem_sanitize(i915);
		i915_gem_resume(i915);
	}
}

static int igt_gem_suspend(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx;
	struct drm_file *file;
	int err;

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	err = -ENOMEM;
	mutex_lock(&i915->drm.struct_mutex);
	ctx = live_context(i915, file);
	if (!IS_ERR(ctx))
		err = switch_to_context(i915, ctx);
	mutex_unlock(&i915->drm.struct_mutex);
	if (err)
		goto out;

	err = pm_prepare(i915);
	if (err)
		goto out;

	pm_suspend(i915);

	/* Here be dragons! Note that with S3RST any S3 may become S4! */
	simulate_hibernate(i915);

	pm_resume(i915);

	mutex_lock(&i915->drm.struct_mutex);
	err = switch_to_context(i915, ctx);
	mutex_unlock(&i915->drm.struct_mutex);
out:
	mock_file_free(i915, file);
	return err;
}

static int igt_gem_hibernate(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx;
	struct drm_file *file;
	int err;

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	err = -ENOMEM;
	mutex_lock(&i915->drm.struct_mutex);
	ctx = live_context(i915, file);
	if (!IS_ERR(ctx))
		err = switch_to_context(i915, ctx);
	mutex_unlock(&i915->drm.struct_mutex);
	if (err)
		goto out;

	err = pm_prepare(i915);
	if (err)
		goto out;

	pm_hibernate(i915);

	/* Here be dragons! */
	simulate_hibernate(i915);

	pm_resume(i915);

	mutex_lock(&i915->drm.struct_mutex);
	err = switch_to_context(i915, ctx);
	mutex_unlock(&i915->drm.struct_mutex);
out:
	mock_file_free(i915, file);
	return err;
}

int i915_gem_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_gem_suspend),
		SUBTEST(igt_gem_hibernate),
	};

	if (i915_terminally_wedged(i915))
		return 0;

	return i915_live_subtests(tests, i915);
}
