// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#include "gt/intel_migrate.h"
#include "gt/intel_gpu_commands.h"
#include "gem/i915_gem_ttm_move.h"

#include "i915_deps.h"

#include "selftests/igt_reset.h"
#include "selftests/igt_spinner.h"

static int igt_fill_check_buffer(struct drm_i915_gem_object *obj,
				 struct intel_gt *gt,
				 bool fill)
{
	unsigned int i, count = obj->base.size / sizeof(u32);
	enum i915_map_type map_type =
		intel_gt_coherent_map_type(gt, obj, false);
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
	struct intel_memory_region *dst_mr = i915->mm.regions[dst];
	struct drm_i915_gem_object *obj;
	struct i915_gem_ww_ctx ww;
	int err = 0;

	GEM_BUG_ON(!src_mr);
	GEM_BUG_ON(!dst_mr);

	/* Switch object backing-store on create */
	obj = i915_gem_object_create_region(src_mr, dst_mr->min_page_size, 0, 0);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	for_i915_gem_ww(&ww, err, true) {
		err = i915_gem_object_lock(obj, &ww);
		if (err)
			continue;

		err = igt_fill_check_buffer(obj, gt, true);
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

		err = igt_fill_check_buffer(obj, gt, false);
	}
	i915_gem_object_put(obj);

	return err;
}

static int igt_smem_create_migrate(void *arg)
{
	return igt_create_migrate(arg, INTEL_REGION_LMEM_0, INTEL_REGION_SMEM);
}

static int igt_lmem_create_migrate(void *arg)
{
	return igt_create_migrate(arg, INTEL_REGION_SMEM, INTEL_REGION_LMEM_0);
}

static int igt_same_create_migrate(void *arg)
{
	return igt_create_migrate(arg, INTEL_REGION_LMEM_0, INTEL_REGION_LMEM_0);
}

static int lmem_pages_migrate_one(struct i915_gem_ww_ctx *ww,
				  struct drm_i915_gem_object *obj,
				  struct i915_vma *vma,
				  bool silent_migrate)
{
	int err;

	err = i915_gem_object_lock(obj, ww);
	if (err)
		return err;

	if (vma) {
		err = i915_vma_pin_ww(vma, ww, obj->base.size, 0,
				      0UL | PIN_OFFSET_FIXED |
				      PIN_USER);
		if (err) {
			if (err != -EINTR && err != ERESTARTSYS &&
			    err != -EDEADLK)
				pr_err("Failed to pin vma.\n");
			return err;
		}

		i915_vma_unpin(vma);
	}

	/*
	 * Migration will implicitly unbind (asynchronously) any bound
	 * vmas.
	 */
	if (i915_gem_object_is_lmem(obj)) {
		err = i915_gem_object_migrate(obj, ww, INTEL_REGION_SMEM);
		if (err) {
			if (!silent_migrate)
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
		err = i915_gem_object_migrate(obj, ww, INTEL_REGION_LMEM_0);
		if (err) {
			if (!silent_migrate)
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

static int __igt_lmem_pages_migrate(struct intel_gt *gt,
				    struct i915_address_space *vm,
				    struct i915_deps *deps,
				    struct igt_spinner *spin,
				    struct dma_fence *spin_fence,
				    bool borked_migrate)
{
	struct drm_i915_private *i915 = gt->i915;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma = NULL;
	struct i915_gem_ww_ctx ww;
	struct i915_request *rq;
	int err;
	int i;

	/* From LMEM to shmem and back again */

	obj = i915_gem_object_create_lmem(i915, SZ_2M, 0);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (vm) {
		vma = i915_vma_instance(obj, vm, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out_put;
		}
	}

	/* Initial GPU fill, sync, CPU initialization. */
	for_i915_gem_ww(&ww, err, true) {
		err = i915_gem_object_lock(obj, &ww);
		if (err)
			continue;

		err = ____i915_gem_object_get_pages(obj);
		if (err)
			continue;

		err = intel_migrate_clear(&gt->migrate, &ww, deps,
					  obj->mm.pages->sgl, obj->pat_index,
					  i915_gem_object_is_lmem(obj),
					  0xdeadbeaf, &rq);
		if (rq) {
			err = dma_resv_reserve_fences(obj->base.resv, 1);
			if (!err)
				dma_resv_add_fence(obj->base.resv, &rq->fence,
						   DMA_RESV_USAGE_KERNEL);
			i915_request_put(rq);
		}
		if (err)
			continue;

		if (!vma) {
			err = igt_fill_check_buffer(obj, gt, true);
			if (err)
				continue;
		}
	}
	if (err)
		goto out_put;

	/*
	 * Migrate to and from smem without explicitly syncing.
	 * Finalize with data in smem for fast readout.
	 */
	for (i = 1; i <= 5; ++i) {
		for_i915_gem_ww(&ww, err, true)
			err = lmem_pages_migrate_one(&ww, obj, vma,
						     borked_migrate);
		if (err)
			goto out_put;
	}

	err = i915_gem_object_lock_interruptible(obj, NULL);
	if (err)
		goto out_put;

	if (spin) {
		if (dma_fence_is_signaled(spin_fence)) {
			pr_err("Spinner was terminated by hangcheck.\n");
			err = -EBUSY;
			goto out_unlock;
		}
		igt_spinner_end(spin);
	}

	/* Finally sync migration and check content. */
	err = i915_gem_object_wait_migration(obj, true);
	if (err)
		goto out_unlock;

	if (vma) {
		err = i915_vma_wait_for_bind(vma);
		if (err)
			goto out_unlock;
	} else {
		err = igt_fill_check_buffer(obj, gt, false);
	}

out_unlock:
	i915_gem_object_unlock(obj);
out_put:
	i915_gem_object_put(obj);

	return err;
}

static int igt_lmem_pages_failsafe_migrate(void *arg)
{
	int fail_gpu, fail_alloc, ban_memcpy, ret;
	struct intel_gt *gt = arg;

	for (fail_gpu = 0; fail_gpu < 2; ++fail_gpu) {
		for (fail_alloc = 0; fail_alloc < 2; ++fail_alloc) {
			for (ban_memcpy = 0; ban_memcpy < 2; ++ban_memcpy) {
				pr_info("Simulated failure modes: gpu: %d, alloc:%d, ban_memcpy: %d\n",
					fail_gpu, fail_alloc, ban_memcpy);
				i915_ttm_migrate_set_ban_memcpy(ban_memcpy);
				i915_ttm_migrate_set_failure_modes(fail_gpu,
								   fail_alloc);
				ret = __igt_lmem_pages_migrate(gt, NULL, NULL,
							       NULL, NULL,
							       ban_memcpy &&
							       fail_gpu);

				if (ban_memcpy && fail_gpu) {
					struct intel_gt *__gt;
					unsigned int id;

					if (ret != -EIO) {
						pr_err("expected -EIO, got (%d)\n", ret);
						ret = -EINVAL;
					} else {
						ret = 0;
					}

					for_each_gt(__gt, gt->i915, id) {
						intel_wakeref_t wakeref;
						bool wedged;

						mutex_lock(&__gt->reset.mutex);
						wedged = test_bit(I915_WEDGED, &__gt->reset.flags);
						mutex_unlock(&__gt->reset.mutex);

						if (fail_gpu && !fail_alloc) {
							if (!wedged) {
								pr_err("gt(%u) not wedged\n", id);
								ret = -EINVAL;
								continue;
							}
						} else if (wedged) {
							pr_err("gt(%u) incorrectly wedged\n", id);
							ret = -EINVAL;
						} else {
							continue;
						}

						wakeref = intel_runtime_pm_get(__gt->uncore->rpm);
						igt_global_reset_lock(__gt);
						intel_gt_reset(__gt, ALL_ENGINES, NULL);
						igt_global_reset_unlock(__gt);
						intel_runtime_pm_put(__gt->uncore->rpm, wakeref);
					}
					if (ret)
						goto out_err;
				}
			}
		}
	}

out_err:
	i915_ttm_migrate_set_failure_modes(false, false);
	i915_ttm_migrate_set_ban_memcpy(false);
	return ret;
}

/*
 * This subtest tests that unbinding at migration is indeed performed
 * async. We launch a spinner and a number of migrations depending on
 * that spinner to have terminated. Before each migration we bind a
 * vma, which should then be async unbound by the migration operation.
 * If we are able to schedule migrations without blocking while the
 * spinner is still running, those unbinds are indeed async and non-
 * blocking.
 *
 * Note that each async bind operation is awaiting the previous migration
 * due to the moving fence resulting from the migration.
 */
static int igt_async_migrate(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct i915_ppgtt *ppgtt;
	struct igt_spinner spin;
	int err;

	ppgtt = i915_ppgtt_create(gt, 0);
	if (IS_ERR(ppgtt))
		return PTR_ERR(ppgtt);

	if (igt_spinner_init(&spin, gt)) {
		err = -ENOMEM;
		goto out_spin;
	}

	for_each_engine(engine, gt, id) {
		struct ttm_operation_ctx ctx = {
			.interruptible = true
		};
		struct dma_fence *spin_fence;
		struct intel_context *ce;
		struct i915_request *rq;
		struct i915_deps deps;

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto out_ce;
		}

		/*
		 * Use MI_NOOP, making the spinner non-preemptible. If there
		 * is a code path where we fail async operation due to the
		 * running spinner, we will block and fail to end the
		 * spinner resulting in a deadlock. But with a non-
		 * preemptible spinner, hangcheck will terminate the spinner
		 * for us, and we will later detect that and fail the test.
		 */
		rq = igt_spinner_create_request(&spin, ce, MI_NOOP);
		intel_context_put(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_ce;
		}

		i915_deps_init(&deps, GFP_KERNEL);
		err = i915_deps_add_dependency(&deps, &rq->fence, &ctx);
		spin_fence = dma_fence_get(&rq->fence);
		i915_request_add(rq);
		if (err)
			goto out_ce;

		err = __igt_lmem_pages_migrate(gt, &ppgtt->vm, &deps, &spin,
					       spin_fence, false);
		i915_deps_fini(&deps);
		dma_fence_put(spin_fence);
		if (err)
			goto out_ce;
	}

out_ce:
	igt_spinner_fini(&spin);
out_spin:
	i915_vm_put(&ppgtt->vm);

	return err;
}

/*
 * Setting ASYNC_FAIL_ALLOC to 2 will simulate memory allocation failure while
 * arming the migration error check and block async migration. This
 * will cause us to deadlock and hangcheck will terminate the spinner
 * causing the test to fail.
 */
#define ASYNC_FAIL_ALLOC 1
static int igt_lmem_async_migrate(void *arg)
{
	int fail_gpu, fail_alloc, ban_memcpy, ret;
	struct intel_gt *gt = arg;

	for (fail_gpu = 0; fail_gpu < 2; ++fail_gpu) {
		for (fail_alloc = 0; fail_alloc < ASYNC_FAIL_ALLOC; ++fail_alloc) {
			for (ban_memcpy = 0; ban_memcpy < 2; ++ban_memcpy) {
				pr_info("Simulated failure modes: gpu: %d, alloc: %d, ban_memcpy: %d\n",
					fail_gpu, fail_alloc, ban_memcpy);
				i915_ttm_migrate_set_ban_memcpy(ban_memcpy);
				i915_ttm_migrate_set_failure_modes(fail_gpu,
								   fail_alloc);
				ret = igt_async_migrate(gt);

				if (fail_gpu && ban_memcpy) {
					struct intel_gt *__gt;
					unsigned int id;

					if (ret != -EIO) {
						pr_err("expected -EIO, got (%d)\n", ret);
						ret = -EINVAL;
					} else {
						ret = 0;
					}

					for_each_gt(__gt, gt->i915, id) {
						intel_wakeref_t wakeref;
						bool wedged;

						mutex_lock(&__gt->reset.mutex);
						wedged = test_bit(I915_WEDGED, &__gt->reset.flags);
						mutex_unlock(&__gt->reset.mutex);

						if (fail_gpu && !fail_alloc) {
							if (!wedged) {
								pr_err("gt(%u) not wedged\n", id);
								ret = -EINVAL;
								continue;
							}
						} else if (wedged) {
							pr_err("gt(%u) incorrectly wedged\n", id);
							ret = -EINVAL;
						} else {
							continue;
						}

						wakeref = intel_runtime_pm_get(__gt->uncore->rpm);
						igt_global_reset_lock(__gt);
						intel_gt_reset(__gt, ALL_ENGINES, NULL);
						igt_global_reset_unlock(__gt);
						intel_runtime_pm_put(__gt->uncore->rpm, wakeref);
					}
				}
				if (ret)
					goto out_err;
			}
		}
	}

out_err:
	i915_ttm_migrate_set_failure_modes(false, false);
	i915_ttm_migrate_set_ban_memcpy(false);
	return ret;
}

int i915_gem_migrate_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_smem_create_migrate),
		SUBTEST(igt_lmem_create_migrate),
		SUBTEST(igt_same_create_migrate),
		SUBTEST(igt_lmem_pages_failsafe_migrate),
		SUBTEST(igt_lmem_async_migrate),
	};

	if (!HAS_LMEM(i915))
		return 0;

	return intel_gt_live_subtests(tests, to_gt(i915));
}
