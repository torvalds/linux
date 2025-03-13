// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020-2022 Intel Corporation
 */

#include <kunit/test.h>
#include <kunit/visibility.h>

#include "tests/xe_kunit_helpers.h"
#include "tests/xe_pci_test.h"

#include "xe_pci.h"
#include "xe_pm.h"

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
	u64 batch_base = xe_migrate_batch_base(m, xe->info.has_usm);
	struct xe_sched_job *job = xe_bb_create_migration_job(m->q, bb,
							      batch_base,
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

#define check(_retval, _expected, str, _test)				\
	do { if ((_retval) != (_expected)) {				\
			KUNIT_FAIL(_test, "Sanity check failed: " str	\
				   " expected %llx, got %llx\n",	\
				   (u64)(_expected), (u64)(_retval));	\
		} } while (0)

static void test_copy(struct xe_migrate *m, struct xe_bo *bo,
		      struct kunit *test, u32 region)
{
	struct xe_device *xe = tile_to_xe(m->tile);
	u64 retval, expected = 0;
	bool big = bo->size >= SZ_2M;
	struct dma_fence *fence;
	const char *str = big ? "Copying big bo" : "Copying small bo";
	int err;

	struct xe_bo *remote = xe_bo_create_locked(xe, m->tile, NULL,
						   bo->size,
						   ttm_bo_type_kernel,
						   region |
						   XE_BO_FLAG_NEEDS_CPU_ACCESS |
						   XE_BO_FLAG_PINNED);
	if (IS_ERR(remote)) {
		KUNIT_FAIL(test, "Failed to allocate remote bo for %s: %pe\n",
			   str, remote);
		return;
	}

	err = xe_bo_validate(remote, NULL, false);
	if (err) {
		KUNIT_FAIL(test, "Failed to validate system bo for %s: %i\n",
			   str, err);
		goto out_unlock;
	}

	err = xe_bo_vmap(remote);
	if (err) {
		KUNIT_FAIL(test, "Failed to vmap system bo for %s: %i\n",
			   str, err);
		goto out_unlock;
	}

	xe_map_memset(xe, &remote->vmap, 0, 0xd0, remote->size);
	fence = xe_migrate_clear(m, remote, remote->ttm.resource,
				 XE_MIGRATE_CLEAR_FLAG_FULL);
	if (!sanity_fence_failed(xe, fence, big ? "Clearing remote big bo" :
				 "Clearing remote small bo", test)) {
		retval = xe_map_rd(xe, &remote->vmap, 0, u64);
		check(retval, expected, "remote first offset should be cleared",
		      test);
		retval = xe_map_rd(xe, &remote->vmap, remote->size - 8, u64);
		check(retval, expected, "remote last offset should be cleared",
		      test);
	}
	dma_fence_put(fence);

	/* Try to copy 0xc0 from remote to vram with 2MB or 64KiB/4KiB pages */
	xe_map_memset(xe, &remote->vmap, 0, 0xc0, remote->size);
	xe_map_memset(xe, &bo->vmap, 0, 0xd0, bo->size);

	expected = 0xc0c0c0c0c0c0c0c0;
	fence = xe_migrate_copy(m, remote, bo, remote->ttm.resource,
				bo->ttm.resource, false);
	if (!sanity_fence_failed(xe, fence, big ? "Copying big bo remote -> vram" :
				 "Copying small bo remote -> vram", test)) {
		retval = xe_map_rd(xe, &bo->vmap, 0, u64);
		check(retval, expected,
		      "remote -> vram bo first offset should be copied", test);
		retval = xe_map_rd(xe, &bo->vmap, bo->size - 8, u64);
		check(retval, expected,
		      "remote -> vram bo offset should be copied", test);
	}
	dma_fence_put(fence);

	/* And other way around.. slightly hacky.. */
	xe_map_memset(xe, &remote->vmap, 0, 0xd0, remote->size);
	xe_map_memset(xe, &bo->vmap, 0, 0xc0, bo->size);

	fence = xe_migrate_copy(m, bo, remote, bo->ttm.resource,
				remote->ttm.resource, false);
	if (!sanity_fence_failed(xe, fence, big ? "Copying big bo vram -> remote" :
				 "Copying small bo vram -> remote", test)) {
		retval = xe_map_rd(xe, &remote->vmap, 0, u64);
		check(retval, expected,
		      "vram -> remote bo first offset should be copied", test);
		retval = xe_map_rd(xe, &remote->vmap, bo->size - 8, u64);
		check(retval, expected,
		      "vram -> remote bo last offset should be copied", test);
	}
	dma_fence_put(fence);

	xe_bo_vunmap(remote);
out_unlock:
	xe_bo_unlock(remote);
	xe_bo_put(remote);
}

static void test_copy_sysmem(struct xe_migrate *m, struct xe_bo *bo,
			     struct kunit *test)
{
	test_copy(m, bo, test, XE_BO_FLAG_SYSTEM);
}

static void test_copy_vram(struct xe_migrate *m, struct xe_bo *bo,
			   struct kunit *test)
{
	u32 region;

	if (bo->ttm.resource->mem_type == XE_PL_SYSTEM)
		return;

	if (bo->ttm.resource->mem_type == XE_PL_VRAM0)
		region = XE_BO_FLAG_VRAM1;
	else
		region = XE_BO_FLAG_VRAM0;
	test_copy(m, bo, test, region);
}

static void xe_migrate_sanity_test(struct xe_migrate *m, struct kunit *test)
{
	struct xe_tile *tile = m->tile;
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_bo *pt, *bo = m->pt_bo, *big, *tiny;
	struct xe_res_cursor src_it;
	struct dma_fence *fence;
	u64 retval, expected;
	struct xe_bb *bb;
	int err;
	u8 id = tile->id;

	err = xe_bo_vmap(bo);
	if (err) {
		KUNIT_FAIL(test, "Failed to vmap our pagetables: %li\n",
			   PTR_ERR(bo));
		return;
	}

	big = xe_bo_create_pin_map(xe, tile, m->q->vm, SZ_4M,
				   ttm_bo_type_kernel,
				   XE_BO_FLAG_VRAM_IF_DGFX(tile) |
				   XE_BO_FLAG_PINNED);
	if (IS_ERR(big)) {
		KUNIT_FAIL(test, "Failed to allocate bo: %li\n", PTR_ERR(big));
		goto vunmap;
	}

	pt = xe_bo_create_pin_map(xe, tile, m->q->vm, XE_PAGE_SIZE,
				  ttm_bo_type_kernel,
				  XE_BO_FLAG_VRAM_IF_DGFX(tile) |
				  XE_BO_FLAG_PINNED);
	if (IS_ERR(pt)) {
		KUNIT_FAIL(test, "Failed to allocate fake pt: %li\n",
			   PTR_ERR(pt));
		goto free_big;
	}

	tiny = xe_bo_create_pin_map(xe, tile, m->q->vm,
				    2 * SZ_4K,
				    ttm_bo_type_kernel,
				    XE_BO_FLAG_VRAM_IF_DGFX(tile) |
				    XE_BO_FLAG_PINNED);
	if (IS_ERR(tiny)) {
		KUNIT_FAIL(test, "Failed to allocate tiny fake pt: %li\n",
			   PTR_ERR(tiny));
		goto free_pt;
	}

	bb = xe_bb_new(tile->primary_gt, 32, xe->info.has_usm);
	if (IS_ERR(bb)) {
		KUNIT_FAIL(test, "Failed to create batchbuffer: %li\n",
			   PTR_ERR(bb));
		goto free_tiny;
	}

	kunit_info(test, "Starting tests, top level PT addr: %lx, special pagetable base addr: %lx\n",
		   (unsigned long)xe_bo_main_addr(m->q->vm->pt_root[id]->bo, XE_PAGE_SIZE),
		   (unsigned long)xe_bo_main_addr(m->pt_bo, XE_PAGE_SIZE));

	/* First part of the test, are we updating our pagetable bo with a new entry? */
	xe_map_wr(xe, &bo->vmap, XE_PAGE_SIZE * (NUM_KERNEL_PDE - 1), u64,
		  0xdeaddeadbeefbeef);
	expected = m->q->vm->pt_ops->pte_encode_bo(pt, 0, xe->pat.idx[XE_CACHE_WB], 0);
	if (m->q->vm->flags & XE_VM_FLAG_64K)
		expected |= XE_PTE_PS64;
	if (xe_bo_is_vram(pt))
		xe_res_first(pt->ttm.resource, 0, pt->size, &src_it);
	else
		xe_res_first_sg(xe_bo_sg(pt), 0, pt->size, &src_it);

	emit_pte(m, bb, NUM_KERNEL_PDE - 1, xe_bo_is_vram(pt), false,
		 &src_it, XE_PAGE_SIZE, pt->ttm.resource);

	run_sanity_job(m, xe, bb, bb->len, "Writing PTE for our fake PT", test);

	retval = xe_map_rd(xe, &bo->vmap, XE_PAGE_SIZE * (NUM_KERNEL_PDE - 1),
			   u64);
	check(retval, expected, "PTE entry write", test);

	/* Now try to write data to our newly mapped 'pagetable', see if it succeeds */
	bb->len = 0;
	bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
	xe_map_wr(xe, &pt->vmap, 0, u32, 0xdeaddead);
	expected = 0;

	emit_clear(tile->primary_gt, bb, xe_migrate_vm_addr(NUM_KERNEL_PDE - 1, 0), 4, 4,
		   IS_DGFX(xe));
	run_sanity_job(m, xe, bb, 1, "Writing to our newly mapped pagetable",
		       test);

	retval = xe_map_rd(xe, &pt->vmap, 0, u32);
	check(retval, expected, "Write to PT after adding PTE", test);

	/* Sanity checks passed, try the full ones! */

	/* Clear a small bo */
	kunit_info(test, "Clearing small buffer object\n");
	xe_map_memset(xe, &tiny->vmap, 0, 0x22, tiny->size);
	expected = 0;
	fence = xe_migrate_clear(m, tiny, tiny->ttm.resource,
				 XE_MIGRATE_CLEAR_FLAG_FULL);
	if (sanity_fence_failed(xe, fence, "Clearing small bo", test))
		goto out;

	dma_fence_put(fence);
	retval = xe_map_rd(xe, &tiny->vmap, 0, u32);
	check(retval, expected, "Command clear small first value", test);
	retval = xe_map_rd(xe, &tiny->vmap, tiny->size - 4, u32);
	check(retval, expected, "Command clear small last value", test);

	kunit_info(test, "Copying small buffer object to system\n");
	test_copy_sysmem(m, tiny, test);
	if (xe->info.tile_count > 1) {
		kunit_info(test, "Copying small buffer object to other vram\n");
		test_copy_vram(m, tiny, test);
	}

	/* Clear a big bo */
	kunit_info(test, "Clearing big buffer object\n");
	xe_map_memset(xe, &big->vmap, 0, 0x11, big->size);
	expected = 0;
	fence = xe_migrate_clear(m, big, big->ttm.resource,
				 XE_MIGRATE_CLEAR_FLAG_FULL);
	if (sanity_fence_failed(xe, fence, "Clearing big bo", test))
		goto out;

	dma_fence_put(fence);
	retval = xe_map_rd(xe, &big->vmap, 0, u32);
	check(retval, expected, "Command clear big first value", test);
	retval = xe_map_rd(xe, &big->vmap, big->size - 4, u32);
	check(retval, expected, "Command clear big last value", test);

	kunit_info(test, "Copying big buffer object to system\n");
	test_copy_sysmem(m, big, test);
	if (xe->info.tile_count > 1) {
		kunit_info(test, "Copying big buffer object to other vram\n");
		test_copy_vram(m, big, test);
	}

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
	struct kunit *test = kunit_get_current_test();
	struct xe_tile *tile;
	int id;

	xe_pm_runtime_get(xe);

	for_each_tile(tile, xe, id) {
		struct xe_migrate *m = tile->migrate;

		kunit_info(test, "Testing tile id %d.\n", id);
		xe_vm_lock(m->q->vm, false);
		xe_migrate_sanity_test(m, test);
		xe_vm_unlock(m->q->vm);
	}

	xe_pm_runtime_put(xe);

	return 0;
}

static void xe_migrate_sanity_kunit(struct kunit *test)
{
	struct xe_device *xe = test->priv;

	migrate_test_run_device(xe);
}

static struct dma_fence *blt_copy(struct xe_tile *tile,
				  struct xe_bo *src_bo, struct xe_bo *dst_bo,
				  bool copy_only_ccs, const char *str, struct kunit *test)
{
	struct xe_gt *gt = tile->primary_gt;
	struct xe_migrate *m = tile->migrate;
	struct xe_device *xe = gt_to_xe(gt);
	struct dma_fence *fence = NULL;
	u64 size = src_bo->size;
	struct xe_res_cursor src_it, dst_it;
	struct ttm_resource *src = src_bo->ttm.resource, *dst = dst_bo->ttm.resource;
	u64 src_L0_ofs, dst_L0_ofs;
	u32 src_L0_pt, dst_L0_pt;
	u64 src_L0, dst_L0;
	int err;
	bool src_is_vram = mem_type_is_vram(src->mem_type);
	bool dst_is_vram = mem_type_is_vram(dst->mem_type);

	if (!src_is_vram)
		xe_res_first_sg(xe_bo_sg(src_bo), 0, size, &src_it);
	else
		xe_res_first(src, 0, size, &src_it);

	if (!dst_is_vram)
		xe_res_first_sg(xe_bo_sg(dst_bo), 0, size, &dst_it);
	else
		xe_res_first(dst, 0, size, &dst_it);

	while (size) {
		u32 batch_size = 2; /* arb_clear() + MI_BATCH_BUFFER_END */
		struct xe_sched_job *job;
		struct xe_bb *bb;
		u32 flush_flags = 0;
		u32 update_idx;
		u32 avail_pts = max_mem_transfer_per_pass(xe) / LEVEL0_PAGE_TABLE_ENCODE_SIZE;
		u32 pte_flags;

		src_L0 = xe_migrate_res_sizes(m, &src_it);
		dst_L0 = xe_migrate_res_sizes(m, &dst_it);

		src_L0 = min(src_L0, dst_L0);

		pte_flags = src_is_vram ? (PTE_UPDATE_FLAG_IS_VRAM |
					   PTE_UPDATE_FLAG_IS_COMP_PTE) : 0;
		batch_size += pte_update_size(m, pte_flags, src, &src_it, &src_L0,
					      &src_L0_ofs, &src_L0_pt, 0, 0,
					      avail_pts);

		pte_flags = dst_is_vram ? (PTE_UPDATE_FLAG_IS_VRAM |
					   PTE_UPDATE_FLAG_IS_COMP_PTE) : 0;
		batch_size += pte_update_size(m, pte_flags, dst, &dst_it, &src_L0,
					      &dst_L0_ofs, &dst_L0_pt, 0,
					      avail_pts, avail_pts);

		/* Add copy commands size here */
		batch_size += ((copy_only_ccs) ? 0 : EMIT_COPY_DW) +
			((xe_device_has_flat_ccs(xe) && copy_only_ccs) ? EMIT_COPY_CCS_DW : 0);

		bb = xe_bb_new(gt, batch_size, xe->info.has_usm);
		if (IS_ERR(bb)) {
			err = PTR_ERR(bb);
			goto err_sync;
		}

		if (src_is_vram)
			xe_res_next(&src_it, src_L0);
		else
			emit_pte(m, bb, src_L0_pt, src_is_vram, false,
				 &src_it, src_L0, src);

		if (dst_is_vram)
			xe_res_next(&dst_it, src_L0);
		else
			emit_pte(m, bb, dst_L0_pt, dst_is_vram, false,
				 &dst_it, src_L0, dst);

		bb->cs[bb->len++] = MI_BATCH_BUFFER_END;
		update_idx = bb->len;
		if (!copy_only_ccs)
			emit_copy(gt, bb, src_L0_ofs, dst_L0_ofs, src_L0, XE_PAGE_SIZE);

		if (copy_only_ccs)
			flush_flags = xe_migrate_ccs_copy(m, bb, src_L0_ofs,
							  src_is_vram, dst_L0_ofs,
							  dst_is_vram, src_L0, dst_L0_ofs,
							  copy_only_ccs);

		job = xe_bb_create_migration_job(m->q, bb,
						 xe_migrate_batch_base(m, xe->info.has_usm),
						 update_idx);
		if (IS_ERR(job)) {
			err = PTR_ERR(job);
			goto err;
		}

		xe_sched_job_add_migrate_flush(job, flush_flags);

		mutex_lock(&m->job_mutex);
		xe_sched_job_arm(job);
		dma_fence_put(fence);
		fence = dma_fence_get(&job->drm.s_fence->finished);
		xe_sched_job_push(job);

		dma_fence_put(m->fence);
		m->fence = dma_fence_get(fence);

		mutex_unlock(&m->job_mutex);

		xe_bb_free(bb, fence);
		size -= src_L0;
		continue;

err:
		xe_bb_free(bb, NULL);

err_sync:
		if (fence) {
			dma_fence_wait(fence, false);
			dma_fence_put(fence);
		}
		return ERR_PTR(err);
	}

	return fence;
}

static void test_migrate(struct xe_device *xe, struct xe_tile *tile,
			 struct xe_bo *sys_bo, struct xe_bo *vram_bo, struct xe_bo *ccs_bo,
			 struct kunit *test)
{
	struct dma_fence *fence;
	u64 expected, retval;
	long timeout;
	long ret;

	expected = 0xd0d0d0d0d0d0d0d0;
	xe_map_memset(xe, &sys_bo->vmap, 0, 0xd0, sys_bo->size);

	fence = blt_copy(tile, sys_bo, vram_bo, false, "Blit copy from sysmem to vram", test);
	if (!sanity_fence_failed(xe, fence, "Blit copy from sysmem to vram", test)) {
		retval = xe_map_rd(xe, &vram_bo->vmap, 0, u64);
		if (retval == expected)
			KUNIT_FAIL(test, "Sanity check failed: VRAM must have compressed value\n");
	}
	dma_fence_put(fence);

	kunit_info(test, "Evict vram buffer object\n");
	ret = xe_bo_evict(vram_bo, true);
	if (ret) {
		KUNIT_FAIL(test, "Failed to evict bo.\n");
		return;
	}

	ret = xe_bo_vmap(vram_bo);
	if (ret) {
		KUNIT_FAIL(test, "Failed to vmap vram bo: %li\n", ret);
		return;
	}

	retval = xe_map_rd(xe, &vram_bo->vmap, 0, u64);
	check(retval, expected, "Clear evicted vram data first value", test);
	retval = xe_map_rd(xe, &vram_bo->vmap, vram_bo->size - 8, u64);
	check(retval, expected, "Clear evicted vram data last value", test);

	fence = blt_copy(tile, vram_bo, ccs_bo,
			 true, "Blit surf copy from vram to sysmem", test);
	if (!sanity_fence_failed(xe, fence, "Clear ccs buffer data", test)) {
		retval = xe_map_rd(xe, &ccs_bo->vmap, 0, u64);
		check(retval, 0, "Clear ccs data first value", test);

		retval = xe_map_rd(xe, &ccs_bo->vmap, ccs_bo->size - 8, u64);
		check(retval, 0, "Clear ccs data last value", test);
	}
	dma_fence_put(fence);

	kunit_info(test, "Restore vram buffer object\n");
	ret = xe_bo_validate(vram_bo, NULL, false);
	if (ret) {
		KUNIT_FAIL(test, "Failed to validate vram bo for: %li\n", ret);
		return;
	}

	/* Sync all migration blits */
	timeout = dma_resv_wait_timeout(vram_bo->ttm.base.resv,
					DMA_RESV_USAGE_KERNEL,
					true,
					5 * HZ);
	if (timeout <= 0) {
		KUNIT_FAIL(test, "Failed to sync bo eviction.\n");
		return;
	}

	ret = xe_bo_vmap(vram_bo);
	if (ret) {
		KUNIT_FAIL(test, "Failed to vmap vram bo: %li\n", ret);
		return;
	}

	retval = xe_map_rd(xe, &vram_bo->vmap, 0, u64);
	check(retval, expected, "Restored value must be equal to initial value", test);
	retval = xe_map_rd(xe, &vram_bo->vmap, vram_bo->size - 8, u64);
	check(retval, expected, "Restored value must be equal to initial value", test);

	fence = blt_copy(tile, vram_bo, ccs_bo,
			 true, "Blit surf copy from vram to sysmem", test);
	if (!sanity_fence_failed(xe, fence, "Clear ccs buffer data", test)) {
		retval = xe_map_rd(xe, &ccs_bo->vmap, 0, u64);
		check(retval, 0, "Clear ccs data first value", test);
		retval = xe_map_rd(xe, &ccs_bo->vmap, ccs_bo->size - 8, u64);
		check(retval, 0, "Clear ccs data last value", test);
	}
	dma_fence_put(fence);
}

static void test_clear(struct xe_device *xe, struct xe_tile *tile,
		       struct xe_bo *sys_bo, struct xe_bo *vram_bo, struct kunit *test)
{
	struct dma_fence *fence;
	u64 expected, retval;

	expected = 0xd0d0d0d0d0d0d0d0;
	xe_map_memset(xe, &sys_bo->vmap, 0, 0xd0, sys_bo->size);

	fence = blt_copy(tile, sys_bo, vram_bo, false, "Blit copy from sysmem to vram", test);
	if (!sanity_fence_failed(xe, fence, "Blit copy from sysmem to vram", test)) {
		retval = xe_map_rd(xe, &vram_bo->vmap, 0, u64);
		if (retval == expected)
			KUNIT_FAIL(test, "Sanity check failed: VRAM must have compressed value\n");
	}
	dma_fence_put(fence);

	fence = blt_copy(tile, vram_bo, sys_bo, false, "Blit copy from vram to sysmem", test);
	if (!sanity_fence_failed(xe, fence, "Blit copy from vram to sysmem", test)) {
		retval = xe_map_rd(xe, &sys_bo->vmap, 0, u64);
		check(retval, expected, "Decompressed value must be equal to initial value", test);
		retval = xe_map_rd(xe, &sys_bo->vmap, sys_bo->size - 8, u64);
		check(retval, expected, "Decompressed value must be equal to initial value", test);
	}
	dma_fence_put(fence);

	kunit_info(test, "Clear vram buffer object\n");
	expected = 0x0000000000000000;
	fence = xe_migrate_clear(tile->migrate, vram_bo, vram_bo->ttm.resource,
				 XE_MIGRATE_CLEAR_FLAG_FULL);
	if (sanity_fence_failed(xe, fence, "Clear vram_bo", test))
		return;
	dma_fence_put(fence);

	fence = blt_copy(tile, vram_bo, sys_bo,
			 false, "Blit copy from vram to sysmem", test);
	if (!sanity_fence_failed(xe, fence, "Clear main buffer data", test)) {
		retval = xe_map_rd(xe, &sys_bo->vmap, 0, u64);
		check(retval, expected, "Clear main buffer first value", test);
		retval = xe_map_rd(xe, &sys_bo->vmap, sys_bo->size - 8, u64);
		check(retval, expected, "Clear main buffer last value", test);
	}
	dma_fence_put(fence);

	fence = blt_copy(tile, vram_bo, sys_bo,
			 true, "Blit surf copy from vram to sysmem", test);
	if (!sanity_fence_failed(xe, fence, "Clear ccs buffer data", test)) {
		retval = xe_map_rd(xe, &sys_bo->vmap, 0, u64);
		check(retval, expected, "Clear ccs data first value", test);
		retval = xe_map_rd(xe, &sys_bo->vmap, sys_bo->size - 8, u64);
		check(retval, expected, "Clear ccs data last value", test);
	}
	dma_fence_put(fence);
}

static void validate_ccs_test_run_tile(struct xe_device *xe, struct xe_tile *tile,
				       struct kunit *test)
{
	struct xe_bo *sys_bo, *vram_bo = NULL, *ccs_bo = NULL;
	unsigned int bo_flags = XE_BO_FLAG_VRAM_IF_DGFX(tile);
	long ret;

	sys_bo = xe_bo_create_user(xe, NULL, NULL, SZ_4M,
				   DRM_XE_GEM_CPU_CACHING_WC,
				   XE_BO_FLAG_SYSTEM |
				   XE_BO_FLAG_NEEDS_CPU_ACCESS |
				   XE_BO_FLAG_PINNED);

	if (IS_ERR(sys_bo)) {
		KUNIT_FAIL(test, "xe_bo_create() failed with err=%ld\n",
			   PTR_ERR(sys_bo));
		return;
	}

	xe_bo_lock(sys_bo, false);
	ret = xe_bo_validate(sys_bo, NULL, false);
	if (ret) {
		KUNIT_FAIL(test, "Failed to validate system bo for: %li\n", ret);
		goto free_sysbo;
	}

	ret = xe_bo_vmap(sys_bo);
	if (ret) {
		KUNIT_FAIL(test, "Failed to vmap system bo: %li\n", ret);
		goto free_sysbo;
	}
	xe_bo_unlock(sys_bo);

	ccs_bo = xe_bo_create_user(xe, NULL, NULL, SZ_4M,
				   DRM_XE_GEM_CPU_CACHING_WC,
				   bo_flags | XE_BO_FLAG_NEEDS_CPU_ACCESS |
				   XE_BO_FLAG_PINNED);

	if (IS_ERR(ccs_bo)) {
		KUNIT_FAIL(test, "xe_bo_create() failed with err=%ld\n",
			   PTR_ERR(ccs_bo));
		return;
	}

	xe_bo_lock(ccs_bo, false);
	ret = xe_bo_validate(ccs_bo, NULL, false);
	if (ret) {
		KUNIT_FAIL(test, "Failed to validate system bo for: %li\n", ret);
		goto free_ccsbo;
	}

	ret = xe_bo_vmap(ccs_bo);
	if (ret) {
		KUNIT_FAIL(test, "Failed to vmap system bo: %li\n", ret);
		goto free_ccsbo;
	}
	xe_bo_unlock(ccs_bo);

	vram_bo = xe_bo_create_user(xe, NULL, NULL, SZ_4M,
				    DRM_XE_GEM_CPU_CACHING_WC,
				    bo_flags | XE_BO_FLAG_NEEDS_CPU_ACCESS |
				    XE_BO_FLAG_PINNED);
	if (IS_ERR(vram_bo)) {
		KUNIT_FAIL(test, "xe_bo_create() failed with err=%ld\n",
			   PTR_ERR(vram_bo));
		return;
	}

	xe_bo_lock(vram_bo, false);
	ret = xe_bo_validate(vram_bo, NULL, false);
	if (ret) {
		KUNIT_FAIL(test, "Failed to validate vram bo for: %li\n", ret);
		goto free_vrambo;
	}

	ret = xe_bo_vmap(vram_bo);
	if (ret) {
		KUNIT_FAIL(test, "Failed to vmap vram bo: %li\n", ret);
		goto free_vrambo;
	}

	test_clear(xe, tile, sys_bo, vram_bo, test);
	test_migrate(xe, tile, sys_bo, vram_bo, ccs_bo, test);
	xe_bo_unlock(vram_bo);

	xe_bo_lock(vram_bo, false);
	xe_bo_vunmap(vram_bo);
	xe_bo_unlock(vram_bo);

	xe_bo_lock(ccs_bo, false);
	xe_bo_vunmap(ccs_bo);
	xe_bo_unlock(ccs_bo);

	xe_bo_lock(sys_bo, false);
	xe_bo_vunmap(sys_bo);
	xe_bo_unlock(sys_bo);
free_vrambo:
	xe_bo_put(vram_bo);
free_ccsbo:
	xe_bo_put(ccs_bo);
free_sysbo:
	xe_bo_put(sys_bo);
}

static int validate_ccs_test_run_device(struct xe_device *xe)
{
	struct kunit *test = kunit_get_current_test();
	struct xe_tile *tile;
	int id;

	if (!xe_device_has_flat_ccs(xe)) {
		kunit_skip(test, "non-flat-ccs device\n");
		return 0;
	}

	if (!(GRAPHICS_VER(xe) >= 20 && IS_DGFX(xe))) {
		kunit_skip(test, "non-xe2 discrete device\n");
		return 0;
	}

	xe_pm_runtime_get(xe);

	for_each_tile(tile, xe, id)
		validate_ccs_test_run_tile(xe, tile, test);

	xe_pm_runtime_put(xe);

	return 0;
}

static void xe_validate_ccs_kunit(struct kunit *test)
{
	struct xe_device *xe = test->priv;

	validate_ccs_test_run_device(xe);
}

static struct kunit_case xe_migrate_tests[] = {
	KUNIT_CASE_PARAM(xe_migrate_sanity_kunit, xe_pci_live_device_gen_param),
	KUNIT_CASE_PARAM(xe_validate_ccs_kunit, xe_pci_live_device_gen_param),
	{}
};

VISIBLE_IF_KUNIT
struct kunit_suite xe_migrate_test_suite = {
	.name = "xe_migrate",
	.test_cases = xe_migrate_tests,
	.init = xe_kunit_helper_xe_device_live_test_init,
};
EXPORT_SYMBOL_IF_KUNIT(xe_migrate_test_suite);
