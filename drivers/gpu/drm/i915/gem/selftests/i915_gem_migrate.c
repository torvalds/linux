// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#include "gt/intel_migrate.h"

static int igt_fill_check_buffer(struct drm_i915_gem_object *obj,
				 bool fill)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	unsigned int i, count = obj->base.size / sizeof(u32);
	enum i915_map_type map_type =
		i915_coherent_map_type(i915, obj, false);
	u32 *cur;
	int err = 0;

	assert_object_held(obj);
	cur = i915_gem_object_pin_map(obj, map_type);
	if (IS_ERR(cur))
		return PTR_ERR(cur);

	if (fill)
		for (i = 0; i < count; ++i)
			*cur++ = i;
	else
		for (i = 0; i < count; ++i)
			if (*cur++ != i) {
				pr_err("Object content mismatch at location %d of %d\n", i, count);
				err = -EINVAL;
				break;
			}

	i915_gem_object_unpin_map(obj);

	return err;
}

static int igt_create_migrate(struct intel_gt *gt, enum intel_region_id src,
			      enum intel_region_id dst)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_memory_region *src_mr = i915->mm.regions[src];
	struct drm_i915_gem_object *obj;
	struct i915_gem_ww_ctx ww;
	int err = 0;

	GEM_BUG_ON(!src_mr);

	/* Switch object backing-store on create */
	obj = i915_gem_object_create_region(src_mr, PAGE_SIZE, 0, 0);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	for_i915_gem_ww(&ww, err, true) {
		err = i915_gem_object_lock(obj, &ww);
		if (err)
			continue;

		err = igt_fill_check_buffer(obj, true);
		if (err)
			continue;

		err = i915_gem_object_migrate(obj, &ww, dst);
		if (err)
			continue;

		err = i915_gem_object_pin_pages(obj);
		if (err)
			continue;

		if (i915_gem_object_can_migrate(obj, src))
			err = -EINVAL;

		i915_gem_object_unpin_pages(obj);
		err = i915_gem_object_wait_migration(obj, true);
		if (err)
			continue;

		err = igt_fill_check_buffer(obj, false);
	}
	i915_gem_object_put(obj);

	return err;
}

static int igt_smem_create_migrate(void *arg)
{
	return igt_create_migrate(arg, INTEL_REGION_LMEM, INTEL_REGION_SMEM);
}

static int igt_lmem_create_migrate(void *arg)
{
	return igt_create_migrate(arg, INTEL_REGION_SMEM, INTEL_REGION_LMEM);
}

static int igt_same_create_migrate(void *arg)
{
	return igt_create_migrate(arg, INTEL_REGION_LMEM, INTEL_REGION_LMEM);
}

static int lmem_pages_migrate_one(struct i915_gem_ww_ctx *ww,
				  struct drm_i915_gem_object *obj)
{
	int err;

	err = i915_gem_object_lock(obj, ww);
	if (err)
		return err;

	if (i915_gem_object_is_lmem(obj)) {
		err = i915_gem_object_migrate(obj, ww, INTEL_REGION_SMEM);
		if (err) {
			pr_err("Object failed migration to smem\n");
			if (err)
				return err;
		}

		if (i915_gem_object_is_lmem(obj)) {
			pr_err("object still backed by lmem\n");
			err = -EINVAL;
		}

		if (!i915_gem_object_has_struct_page(obj)) {
			pr_err("object not backed by struct page\n");
			err = -EINVAL;
		}

	} else {
		err = i915_gem_object_migrate(obj, ww, INTEL_REGION_LMEM);
		if (err) {
			pr_err("Object failed migration to lmem\n");
			if (err)
				return err;
		}

		if (i915_gem_object_has_struct_page(obj)) {
			pr_err("object still backed by struct page\n");
			err = -EINVAL;
		}

		if (!i915_gem_object_is_lmem(obj)) {
			pr_err("object not backed by lmem\n");
			err = -EINVAL;
		}
	}

	return err;
}

static int igt_lmem_pages_migrate(void *arg)
{
	struct intel_gt *gt = arg;
	struct drm_i915_private *i915 = gt->i915;
	struct drm_i915_gem_object *obj;
	struct i915_gem_ww_ctx ww;
	struct i915_request *rq;
	int err;
	int i;

	/* From LMEM to shmem and back again */

	obj = i915_gem_object_create_lmem(i915, SZ_2M, 0);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	/* Initial GPU fill, sync, CPU initialization. */
	for_i915_gem_ww(&ww, err, true) {
		err = i915_gem_object_lock(obj, &ww);
		if (err)
			continue;

		err = ____i915_gem_object_get_pages(obj);
		if (err)
			continue;

		err = intel_migrate_clear(&gt->migrate, &ww, NULL,
					  obj->mm.pages->sgl, obj->cache_level,
					  i915_gem_object_is_lmem(obj),
					  0xdeadbeaf, &rq);
		if (rq) {
			dma_resv_add_excl_fence(obj->base.resv, &rq->fence);
			i915_request_put(rq);
		}
		if (err)
			continue;

		err = i915_gem_object_wait(obj, I915_WAIT_INTERRUPTIBLE,
					   5 * HZ);
		if (err)
			continue;

		err = igt_fill_check_buffer(obj, true);
		if (err)
			continue;
	}
	if (err)
		goto out_put;

	/*
	 * Migrate to and from smem without explicitly syncing.
	 * Finalize with data in smem for fast readout.
	 */
	for (i = 1; i <= 5; ++i) {
		for_i915_gem_ww(&ww, err, true)
			err = lmem_pages_migrate_one(&ww, obj);
		if (err)
			goto out_put;
	}

	err = i915_gem_object_lock_interruptible(obj, NULL);
	if (err)
		goto out_put;

	/* Finally sync migration and check content. */
	err = i915_gem_object_wait_migration(obj, true);
	if (err)
		goto out_unlock;

	err = igt_fill_check_buffer(obj, false);

out_unlock:
	i915_gem_object_unlock(obj);
out_put:
	i915_gem_object_put(obj);

	return err;
}

int i915_gem_migrate_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_smem_create_migrate),
		SUBTEST(igt_lmem_create_migrate),
		SUBTEST(igt_same_create_migrate),
		SUBTEST(igt_lmem_pages_migrate),
	};

	if (!HAS_LMEM(i915))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}
