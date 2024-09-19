// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <kunit/test.h>
#include <kunit/visibility.h>

#include "tests/xe_kunit_helpers.h"
#include "tests/xe_pci_test.h"
#include "tests/xe_test.h"

#include "xe_bo_evict.h"
#include "xe_pci.h"
#include "xe_pm.h"

static int ccs_test_migrate(struct xe_tile *tile, struct xe_bo *bo,
			    bool clear, u64 get_val, u64 assign_val,
			    struct kunit *test)
{
	struct dma_fence *fence;
	struct ttm_tt *ttm;
	struct page *page;
	pgoff_t ccs_page;
	long timeout;
	u64 *cpu_map;
	int ret;
	u32 offset;

	/* Move bo to VRAM if not already there. */
	ret = xe_bo_validate(bo, NULL, false);
	if (ret) {
		KUNIT_FAIL(test, "Failed to validate bo.\n");
		return ret;
	}

	/* Optionally clear bo *and* CCS data in VRAM. */
	if (clear) {
		fence = xe_migrate_clear(tile->migrate, bo, bo->ttm.resource,
					 XE_MIGRATE_CLEAR_FLAG_FULL);
		if (IS_ERR(fence)) {
			KUNIT_FAIL(test, "Failed to submit bo clear.\n");
			return PTR_ERR(fence);
		}
		dma_fence_put(fence);
	}

	/* Evict to system. CCS data should be copied. */
	ret = xe_bo_evict(bo, true);
	if (ret) {
		KUNIT_FAIL(test, "Failed to evict bo.\n");
		return ret;
	}

	/* Sync all migration blits */
	timeout = dma_resv_wait_timeout(bo->ttm.base.resv,
					DMA_RESV_USAGE_KERNEL,
					true,
					5 * HZ);
	if (timeout <= 0) {
		KUNIT_FAIL(test, "Failed to sync bo eviction.\n");
		return -ETIME;
	}

	/*
	 * Bo with CCS data is now in system memory. Verify backing store
	 * and data integrity. Then assign for the next testing round while
	 * we still have a CPU map.
	 */
	ttm = bo->ttm.ttm;
	if (!ttm || !ttm_tt_is_populated(ttm)) {
		KUNIT_FAIL(test, "Bo was not in expected placement.\n");
		return -EINVAL;
	}

	ccs_page = xe_bo_ccs_pages_start(bo) >> PAGE_SHIFT;
	if (ccs_page >= ttm->num_pages) {
		KUNIT_FAIL(test, "No TTM CCS pages present.\n");
		return -EINVAL;
	}

	page = ttm->pages[ccs_page];
	cpu_map = kmap_local_page(page);

	/* Check first CCS value */
	if (cpu_map[0] != get_val) {
		KUNIT_FAIL(test,
			   "Expected CCS readout 0x%016llx, got 0x%016llx.\n",
			   (unsigned long long)get_val,
			   (unsigned long long)cpu_map[0]);
		ret = -EINVAL;
	}

	/* Check last CCS value, or at least last value in page. */
	offset = xe_device_ccs_bytes(tile_to_xe(tile), bo->size);
	offset = min_t(u32, offset, PAGE_SIZE) / sizeof(u64) - 1;
	if (cpu_map[offset] != get_val) {
		KUNIT_FAIL(test,
			   "Expected CCS readout 0x%016llx, got 0x%016llx.\n",
			   (unsigned long long)get_val,
			   (unsigned long long)cpu_map[offset]);
		ret = -EINVAL;
	}

	cpu_map[0] = assign_val;
	cpu_map[offset] = assign_val;
	kunmap_local(cpu_map);

	return ret;
}

static void ccs_test_run_tile(struct xe_device *xe, struct xe_tile *tile,
			      struct kunit *test)
{
	struct xe_bo *bo;

	int ret;

	/* TODO: Sanity check */
	unsigned int bo_flags = XE_BO_FLAG_VRAM_IF_DGFX(tile);

	if (IS_DGFX(xe))
		kunit_info(test, "Testing vram id %u\n", tile->id);
	else
		kunit_info(test, "Testing system memory\n");

	bo = xe_bo_create_user(xe, NULL, NULL, SZ_1M, DRM_XE_GEM_CPU_CACHING_WC,
			       bo_flags);
	if (IS_ERR(bo)) {
		KUNIT_FAIL(test, "Failed to create bo.\n");
		return;
	}

	xe_bo_lock(bo, false);

	kunit_info(test, "Verifying that CCS data is cleared on creation.\n");
	ret = ccs_test_migrate(tile, bo, false, 0ULL, 0xdeadbeefdeadbeefULL,
			       test);
	if (ret)
		goto out_unlock;

	kunit_info(test, "Verifying that CCS data survives migration.\n");
	ret = ccs_test_migrate(tile, bo, false, 0xdeadbeefdeadbeefULL,
			       0xdeadbeefdeadbeefULL, test);
	if (ret)
		goto out_unlock;

	kunit_info(test, "Verifying that CCS data can be properly cleared.\n");
	ret = ccs_test_migrate(tile, bo, true, 0ULL, 0ULL, test);

out_unlock:
	xe_bo_unlock(bo);
	xe_bo_put(bo);
}

static int ccs_test_run_device(struct xe_device *xe)
{
	struct kunit *test = kunit_get_current_test();
	struct xe_tile *tile;
	int id;

	if (!xe_device_has_flat_ccs(xe)) {
		kunit_skip(test, "non-flat-ccs device\n");
		return 0;
	}

	/* For xe2+ dgfx, we don't handle ccs metadata */
	if (GRAPHICS_VER(xe) >= 20 && IS_DGFX(xe)) {
		kunit_skip(test, "xe2+ dgfx device\n");
		return 0;
	}

	xe_pm_runtime_get(xe);

	for_each_tile(tile, xe, id) {
		/* For igfx run only for primary tile */
		if (!IS_DGFX(xe) && id > 0)
			continue;
		ccs_test_run_tile(xe, tile, test);
	}

	xe_pm_runtime_put(xe);

	return 0;
}

static void xe_ccs_migrate_kunit(struct kunit *test)
{
	struct xe_device *xe = test->priv;

	ccs_test_run_device(xe);
}

static int evict_test_run_tile(struct xe_device *xe, struct xe_tile *tile, struct kunit *test)
{
	struct xe_bo *bo, *external;
	unsigned int bo_flags = XE_BO_FLAG_VRAM_IF_DGFX(tile);
	struct xe_vm *vm = xe_migrate_get_vm(xe_device_get_root_tile(xe)->migrate);
	struct xe_gt *__gt;
	int err, i, id;

	kunit_info(test, "Testing device %s vram id %u\n",
		   dev_name(xe->drm.dev), tile->id);

	for (i = 0; i < 2; ++i) {
		xe_vm_lock(vm, false);
		bo = xe_bo_create_user(xe, NULL, vm, 0x10000,
				       DRM_XE_GEM_CPU_CACHING_WC,
				       bo_flags);
		xe_vm_unlock(vm);
		if (IS_ERR(bo)) {
			KUNIT_FAIL(test, "bo create err=%pe\n", bo);
			break;
		}

		external = xe_bo_create_user(xe, NULL, NULL, 0x10000,
					     DRM_XE_GEM_CPU_CACHING_WC,
					     bo_flags);
		if (IS_ERR(external)) {
			KUNIT_FAIL(test, "external bo create err=%pe\n", external);
			goto cleanup_bo;
		}

		xe_bo_lock(external, false);
		err = xe_bo_pin_external(external);
		xe_bo_unlock(external);
		if (err) {
			KUNIT_FAIL(test, "external bo pin err=%pe\n",
				   ERR_PTR(err));
			goto cleanup_external;
		}

		err = xe_bo_evict_all(xe);
		if (err) {
			KUNIT_FAIL(test, "evict err=%pe\n", ERR_PTR(err));
			goto cleanup_all;
		}

		for_each_gt(__gt, xe, id)
			xe_gt_sanitize(__gt);
		err = xe_bo_restore_kernel(xe);
		/*
		 * Snapshotting the CTB and copying back a potentially old
		 * version seems risky, depending on what might have been
		 * inflight. Also it seems snapshotting the ADS object and
		 * copying back results in serious breakage. Normally when
		 * calling xe_bo_restore_kernel() we always fully restart the
		 * GT, which re-intializes such things.  We could potentially
		 * skip saving and restoring such objects in xe_bo_evict_all()
		 * however seems quite fragile not to also restart the GT. Try
		 * to do that here by triggering a GT reset.
		 */
		for_each_gt(__gt, xe, id) {
			xe_gt_reset_async(__gt);
			flush_work(&__gt->reset.worker);
		}
		if (err) {
			KUNIT_FAIL(test, "restore kernel err=%pe\n",
				   ERR_PTR(err));
			goto cleanup_all;
		}

		err = xe_bo_restore_user(xe);
		if (err) {
			KUNIT_FAIL(test, "restore user err=%pe\n", ERR_PTR(err));
			goto cleanup_all;
		}

		if (!xe_bo_is_vram(external)) {
			KUNIT_FAIL(test, "external bo is not vram\n");
			err = -EPROTO;
			goto cleanup_all;
		}

		if (xe_bo_is_vram(bo)) {
			KUNIT_FAIL(test, "bo is vram\n");
			err = -EPROTO;
			goto cleanup_all;
		}

		if (i) {
			down_read(&vm->lock);
			xe_vm_lock(vm, false);
			err = xe_bo_validate(bo, bo->vm, false);
			xe_vm_unlock(vm);
			up_read(&vm->lock);
			if (err) {
				KUNIT_FAIL(test, "bo valid err=%pe\n",
					   ERR_PTR(err));
				goto cleanup_all;
			}
			xe_bo_lock(external, false);
			err = xe_bo_validate(external, NULL, false);
			xe_bo_unlock(external);
			if (err) {
				KUNIT_FAIL(test, "external bo valid err=%pe\n",
					   ERR_PTR(err));
				goto cleanup_all;
			}
		}

		xe_bo_lock(external, false);
		xe_bo_unpin_external(external);
		xe_bo_unlock(external);

		xe_bo_put(external);

		xe_bo_lock(bo, false);
		__xe_bo_unset_bulk_move(bo);
		xe_bo_unlock(bo);
		xe_bo_put(bo);
		continue;

cleanup_all:
		xe_bo_lock(external, false);
		xe_bo_unpin_external(external);
		xe_bo_unlock(external);
cleanup_external:
		xe_bo_put(external);
cleanup_bo:
		xe_bo_lock(bo, false);
		__xe_bo_unset_bulk_move(bo);
		xe_bo_unlock(bo);
		xe_bo_put(bo);
		break;
	}

	xe_vm_put(vm);

	return 0;
}

static int evict_test_run_device(struct xe_device *xe)
{
	struct kunit *test = kunit_get_current_test();
	struct xe_tile *tile;
	int id;

	if (!IS_DGFX(xe)) {
		kunit_skip(test, "non-discrete device\n");
		return 0;
	}

	xe_pm_runtime_get(xe);

	for_each_tile(tile, xe, id)
		evict_test_run_tile(xe, tile, test);

	xe_pm_runtime_put(xe);

	return 0;
}

static void xe_bo_evict_kunit(struct kunit *test)
{
	struct xe_device *xe = test->priv;

	evict_test_run_device(xe);
}

static struct kunit_case xe_bo_tests[] = {
	KUNIT_CASE_PARAM(xe_ccs_migrate_kunit, xe_pci_live_device_gen_param),
	KUNIT_CASE_PARAM(xe_bo_evict_kunit, xe_pci_live_device_gen_param),
	{}
};

VISIBLE_IF_KUNIT
struct kunit_suite xe_bo_test_suite = {
	.name = "xe_bo",
	.test_cases = xe_bo_tests,
	.init = xe_kunit_helper_xe_device_live_test_init,
};
EXPORT_SYMBOL_IF_KUNIT(xe_bo_test_suite);
