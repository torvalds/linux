// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020-2022 Intel Corporation
 */

#include <kunit/test.h>

#include "xe_pci.h"

static bool sanity_fence_failed(struct xe_device *xe, struct dma_fence *fence,
				const char *str, struct kunit *test)
{
	long ret;

	if (IS_ERR(fence)) {
		KUNIT_FAIL(test, "Failed to create fence for %s: %li\n", str,
			   PTR_ERR(fence));
		return true;
	}
	if (!fence)
		return true;

	ret = dma_fence_wait_timeout(fence, false, 5 * HZ);
	if (ret <= 0) {
		KUNIT_FAIL(test, "Fence timed out for %s: %li\n", str, ret);
		return true;
	}

	return false;
}

static int run_sanity_job(struct xe_migrate *m, struct xe_device *xe,
			  struct xe_bb *bb, u32 second_idx, const char *str,
			  struct kunit *test)
{
	struct xe_sched_job *job = xe_bb_create_migration_job(m->eng, bb,
							      m->batch_base_ofs,
							      second_idx);
	struct dma_fence *fence;

	if (IS_ERR(job)) {
		KUNIT_FAIL(test, "Failed to allocate fake pt: %li\n",
			   PTR_ERR(job));
		return PTR_ERR(job);
	}

	xe_sched_job_arm(job);
	fence = dma_fence_get(&job->drm.s_fence->finished);
	xe_sched_job_push(job);

	if (sanity_fence_failed(xe, fence, str, test))
		return -ETIMEDOUT;

	dma_fence_put(fence);
	kunit_info(test, "%s: Job completed\n", str);
	return 0;
}

static void
sanity_populate_cb(struct xe_migrate_pt_update *pt_update,
		   struct xe_gt *gt, struct iosys_map *map, void *dst,
		   u32 qword_ofs, u32 num_qwords,
		   const struct xe_vm_pgtable_update *update)
{
	int i;
	u64 *ptr = dst;

	for (i = 0; i < num_qwords; i++)
		ptr[i] = (qword_ofs + i - update->ofs) * 0x1111111111111111ULL;
}

static const struct xe_migrate_pt_update_ops sanity_ops = {
	.populate = sanity_populate_cb,
};

#define check(_retval, _expected, str, _test)				\
	do { if ((_retval) != (_expected)) {				\
			KUNIT_FAIL(_test, "Sanity check failed: " str	\
				   " expected %llx, got %llx\n",	\
				   (u64)(_expected), (u64)(_retval));	\
		} } while (0)

static void test_copy(struct xe_migrate *m, struct xe_bo *bo,
		      struct kunit *test)
{
	struct xe_device *xe = gt_to_xe(m->gt);
	u64 retval, expected = 0xc0c0c0c0c0c0c0c0ULL;
	bool big = bo->size >= SZ_2M;
	struct dma_fence *fence;
	const char *str = big ? "Copying big bo" : "Copying small bo";
	int err;

	struct xe_bo *sysmem = xe_bo_create_locked(xe, m->gt, NULL,
						   bo->size,
						   ttm_bo_type_kernel,
						   XE_BO_CREATE_SYSTEM_BIT);
	if (IS_ERR(sysmem)) {
		KUNIT_FAIL(test, "Failed to allocate sysmem bo for %s: %li\n",
			   str, PTR_ERR(sysmem));
		return;
	}

	err = xe_bo_validate(sysmem, NULL, false);
	if (err) {
		KUNIT_FAIL(test, "Failed to validate system bo for %s: %li\n",
			   str, err);
		goto out_unlock;
	}

	err = xe_bo_vmap(sysmem);
	if (err) {
		KUNIT_FAIL(test, "Failed to vmap system bo for %s: %li\n",
			   str, err);
		goto out_unlock;
	}

	xe_map_memset(xe, &sysmem->vmap, 0, 0xd0, sysmem->size);
	fence = xe_migrate_clear(m, sysmem, sysmem->ttm.resource, 0xc0c0c0c0);
	if (!sanity_fence_failed(xe, fence, big ? "Clearing sysmem big bo" :
				 "Clearing sysmem small bo", test)) {
		retval = xe_map_rd(xe, &sysmem->vmap, 0, u64);
		check(retval, expected, "sysmem first offset should be cleared",
		      test);
		retval = xe_map_rd(xe, &sysmem->vmap, sysmem->size - 8, u64);
		check(retval, expected, "sysmem last offset should be cleared",
		      test);
	}
	dma_fence_put(fence);

	/* Try to copy 0xc0 from sysmem to lmem with 2MB or 64KiB/4KiB pages */
	xe_map_memset(xe, &sysmem->vmap, 0, 0xc0, sysmem->size);
	xe_map_memset(xe, &bo->vmap, 0, 0xd0, bo->size);

	fence = xe_migrate_copy(m, sysmem, sysmem->ttm.resource,
				bo->ttm.resource);
	if (!sanity_fence_failed(xe, fence, big ? "Copying big bo sysmem -> vram" :
				 "Copying small bo sysmem -> vram", test)) {
		retval = xe_map_rd(xe, &bo->vmap, 0, u64);
		check(retval, expected,
		      "sysmem -> vram bo first offset should be copied", test);
		retval = xe_map_rd(xe, &bo->vmap, bo->size - 8, u64);
		check(retval, expected,
		      "sysmem -> vram bo offset should be copied", test);
	}
	dma_fence_put(fence);

	/* And other way around.. slightly hacky.. */
	xe_map_memset(xe, &sysmem->vmap, 0, 0xd0, sysmem->size);
	xe_map_memset(xe, &bo->vmap, 0, 0xc0, bo->size);

	fence = xe_migrate_copy(m, sysmem, bo->ttm.resource,
				sysmem->ttm.resource);
	if (!sanity_fence_failed(xe, fence, big ? "Copying big bo vram -> sysmem" :
				 "Copying small bo vram -> sysmem", test)) {
		retval = xe_map_rd(xe, &sysmem->vmap, 0, u64);
		check(retval, expected,
		      "vram -> sysmem bo first offset should be copied", test);
		retval = xe_map_rd(xe, &sysmem->vmap, bo->size - 8, u64);
		check(retval, expected,
		      "vram -> sysmem bo last offset should be copied", test);
	}
	dma_fence_put(fence);

	xe_bo_vunmap(sysmem);
out_unlock:
	xe_bo_unlock_no_vm(sysmem);
	xe_bo_put(sysmem);
}

static void test_pt_update(struct xe_migrate *m, struct xe_bo *pt,
			   struct kunit *test)
{
	struct xe_device *xe = gt_to_xe(m->gt);
	struct dma_fence *fence;
	u64 retval, expected;
	int i;

	struct xe_vm_pgtable_update update = {
		.ofs = 1,
		.qwords = 0x10,
		.pt_bo = pt,
	};
	struct xe_migrate_pt_update pt_update = {
		.ops = &sanity_ops,
	};

	/* Test xe_migrate_update_pgtables() updates the pagetable as expected */
	expected = 0xf0f0f0f0f0f0f0f0ULL;
	xe_map_memset(xe, &pt->vmap, 0, (u8)expected, pt->size);

	fence = xe_migrate_update_pgtables(m, NULL, NULL, m->eng, &update, 1,
					   NULL, 0, &pt_update);
	if (sanity_fence_failed(xe, fence, "Migration pagetable update", test))
		return;

	dma_fence_put(fence);
	retval = xe_map_rd(xe, &pt->vmap, 0, u64);
	check(retval, expected, "PTE[0] must stay untouched", test);

	for (i = 0; i < update.qwords; i++) {
		retval = xe_map_rd(xe, &pt->vmap, (update.ofs + i) * 8, u64);
		check(retval, i * 0x1111111111111111ULL, "PTE update", test);
	}

	retval = xe_map_rd(xe, &pt->vmap, 8 * (update.ofs + update.qwords),
			   u64);
	check(retval, expected, "PTE[0x11] must stay untouched", test);
}

static void xe_migrate_sanity_test(struct xe_migrate *m, struct kunit *test)
{
	struct xe_gt *gt = m->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_bo *pt, *bo = m->pt_bo, *big, *tiny;
	struct xe_res_cursor src_it;
	struct dma_fence *fence;
	u64 retval, expected;
	struct xe_bb *bb;
	int err;
	u8 id = gt->info.id;

	err = xe_bo_vmap(bo);
	if (err) {
		KUNIT_FAIL(test, "Failed to vmap our pagetables: %li\n",
			   PTR_ERR(bo));
		return;
	}

	big = xe_bo_create_pin_map(xe, m->gt, m->eng->vm, SZ_4M,
				   ttm_bo_type_kernel,
				   XE_BO_CREATE_VRAM_IF_DGFX(m->gt) |
				   XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(big)) {
		KUNIT_FAIL(test, "Failed to allocate bo: %li\n", PTR_ERR(big));
		goto vunmap;
	}

	pt = xe_bo_create_pin_map(xe, m->gt, m->eng->vm, GEN8_PAGE_SIZE,
				  ttm_bo_type_kernel,
				  XE_BO_CREATE_VRAM_IF_DGFX(m->gt) |
				  XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(pt)) {
		KUNIT_FAIL(test, "Failed to allocate fake pt: %li\n",
			   PTR_ERR(pt));
		goto free_big;
	}

	tiny = xe_bo_create_pin_map(xe, m->gt, m->eng->vm,
				    2 * SZ_4K,
				    ttm_bo_type_kernel,
				    XE_BO_CREATE_VRAM_IF_DGFX(m->gt) |
				    XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(tiny)) {
		KUNIT_FAIL(test, "Failed to allocate fake pt: %li\n",
			   PTR_ERR(pt));
		goto free_pt;
	}

	bb = xe_bb_new(m->gt, 32, xe->info.supports_usm);
	if (IS_ERR(bb)) {
		KUNIT_FAIL(test, "Failed to create batchbuffer: %li\n",
			   PTR_ERR(bb));
		goto free_tiny;
	}

	kunit_info(test, "Starting tests, top level PT addr: %lx, special pagetable base addr: %lx\n",
		   (unsigned long)xe_bo_main_addr(m->eng->vm->pt_root[id]->bo, GEN8_PAGE_SIZE),
		   (unsigned long)xe_bo_main_addr(m->pt_bo, GEN8_PAGE_SIZE));

	/* First part of the test, are we updating our pagetable bo with a new entry? */
	xe_map_wr(xe, &bo->vmap, GEN8_PAGE_SIZE * (NUM_KERNEL_PDE - 1), u64, 0xdeaddeadbeefbeef);
	expected = gen8_pte_encode(NULL, pt, 0, XE_CACHE_WB, 0, 0);
	if (m->eng->vm->flags & XE_VM_FLAGS_64K)
		expected |= GEN12_PTE_PS64;
	xe_res_first(pt->ttm.resource, 0, pt->size, &src_it);
	emit_pte(m, bb, NUM_KERNEL_PDE - 1, xe_bo_is_vram(pt),
		 &src_it, GEN8_PAGE_SIZE, pt);
	run_sanity_job(m, xe, bb, bb->len, "Writing PTE for our fake PT", test);

	retval = xe_map_rd(xe, &bo->vmap, GEN8_PAGE_SIZE * (NUM_KERNEL_PDE - 1),
			   u64);
	check(retval, expected, "PTE entry write", test);

	/* Now try to write data to our newly mapped 'pagetable', see if it succeeds */
	bb->len = 0;
	bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
	xe_map_wr(xe, &pt->vmap, 0, u32, 0xdeaddead);
	expected = 0x12345678U;

	emit_clear(m->gt, bb, xe_migrate_vm_addr(NUM_KERNEL_PDE - 1, 0), 4, 4,
		   expected, IS_DGFX(xe));
	run_sanity_job(m, xe, bb, 1, "Writing to our newly mapped pagetable",
		       test);

	retval = xe_map_rd(xe, &pt->vmap, 0, u32);
	check(retval, expected, "Write to PT after adding PTE", test);

	/* Sanity checks passed, try the full ones! */

	/* Clear a small bo */
	kunit_info(test, "Clearing small buffer object\n");
	xe_map_memset(xe, &tiny->vmap, 0, 0x22, tiny->size);
	expected = 0x224488ff;
	fence = xe_migrate_clear(m, tiny, tiny->ttm.resource, expected);
	if (sanity_fence_failed(xe, fence, "Clearing small bo", test))
		goto out;

	dma_fence_put(fence);
	retval = xe_map_rd(xe, &tiny->vmap, 0, u32);
	check(retval, expected, "Command clear small first value", test);
	retval = xe_map_rd(xe, &tiny->vmap, tiny->size - 4, u32);
	check(retval, expected, "Command clear small last value", test);

	if (IS_DGFX(xe)) {
		kunit_info(test, "Copying small buffer object to system\n");
		test_copy(m, tiny, test);
	}

	/* Clear a big bo with a fixed value */
	kunit_info(test, "Clearing big buffer object\n");
	xe_map_memset(xe, &big->vmap, 0, 0x11, big->size);
	expected = 0x11223344U;
	fence = xe_migrate_clear(m, big, big->ttm.resource, expected);
	if (sanity_fence_failed(xe, fence, "Clearing big bo", test))
		goto out;

	dma_fence_put(fence);
	retval = xe_map_rd(xe, &big->vmap, 0, u32);
	check(retval, expected, "Command clear big first value", test);
	retval = xe_map_rd(xe, &big->vmap, big->size - 4, u32);
	check(retval, expected, "Command clear big last value", test);

	if (IS_DGFX(xe)) {
		kunit_info(test, "Copying big buffer object to system\n");
		test_copy(m, big, test);
	}

	test_pt_update(m, pt, test);

out:
	xe_bb_free(bb, NULL);
free_tiny:
	xe_bo_unpin(tiny);
	xe_bo_put(tiny);
free_pt:
	xe_bo_unpin(pt);
	xe_bo_put(pt);
free_big:
	xe_bo_unpin(big);
	xe_bo_put(big);
vunmap:
	xe_bo_vunmap(m->pt_bo);
}

static int migrate_test_run_device(struct xe_device *xe)
{
	struct kunit *test = xe_cur_kunit();
	struct xe_gt *gt;
	int id;

	for_each_gt(gt, xe, id) {
		struct xe_migrate *m = gt->migrate;
		struct ww_acquire_ctx ww;

		kunit_info(test, "Testing gt id %d.\n", id);
		xe_vm_lock(m->eng->vm, &ww, 0, true);
		xe_migrate_sanity_test(m, test);
		xe_vm_unlock(m->eng->vm, &ww);
	}

	return 0;
}

void xe_migrate_sanity_kunit(struct kunit *test)
{
	xe_call_for_each_device(migrate_test_run_device);
}
EXPORT_SYMBOL(xe_migrate_sanity_kunit);
