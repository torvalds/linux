// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020-2022 Intel Corporation
 */

#include <kunit/test.h>
#include <kunit/visibility.h>

#include "tests/xe_migrate_test.h"
#include "tests/xe_pci_test.h"

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

static void
sanity_populate_cb(struct xe_migrate_pt_update *pt_update,
		   struct xe_tile *tile, struct iosys_map *map, void *dst,
		   u32 qword_ofs, u32 num_qwords,
		   const struct xe_vm_pgtable_update *update)
{
	struct migrate_test_params *p =
		to_migrate_test_params(xe_cur_kunit_priv(XE_TEST_LIVE_MIGRATE));
	int i;
	u64 *ptr = dst;
	u64 value;

	for (i = 0; i < num_qwords; i++) {
		value = (qword_ofs + i - update->ofs) * 0x1111111111111111ULL;
		if (map)
			xe_map_wr(tile_to_xe(tile), map, (qword_ofs + i) *
				  sizeof(u64), u64, value);
		else
			ptr[i] = value;
	}

	kunit_info(xe_cur_kunit(), "Used %s.\n", map ? "CPU" : "GPU");
	if (p->force_gpu && map)
		KUNIT_FAIL(xe_cur_kunit(), "GPU pagetable update used CPU.\n");
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
						   XE_BO_NEEDS_CPU_ACCESS);
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
	fence = xe_migrate_clear(m, remote, remote->ttm.resource);
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
	test_copy(m, bo, test, XE_BO_CREATE_SYSTEM_BIT);
}

static void test_copy_vram(struct xe_migrate *m, struct xe_bo *bo,
			   struct kunit *test)
{
	u32 region;

	if (bo->ttm.resource->mem_type == XE_PL_SYSTEM)
		return;

	if (bo->ttm.resource->mem_type == XE_PL_VRAM0)
		region = XE_BO_CREATE_VRAM1_BIT;
	else
		region = XE_BO_CREATE_VRAM0_BIT;
	test_copy(m, bo, test, region);
}

static void test_pt_update(struct xe_migrate *m, struct xe_bo *pt,
			   struct kunit *test, bool force_gpu)
{
	struct xe_device *xe = tile_to_xe(m->tile);
	struct dma_fence *fence;
	u64 retval, expected;
	ktime_t then, now;
	int i;

	struct xe_vm_pgtable_update update = {
		.ofs = 1,
		.qwords = 0x10,
		.pt_bo = pt,
	};
	struct xe_migrate_pt_update pt_update = {
		.ops = &sanity_ops,
	};
	struct migrate_test_params p = {
		.base.id = XE_TEST_LIVE_MIGRATE,
		.force_gpu = force_gpu,
	};

	test->priv = &p;
	/* Test xe_migrate_update_pgtables() updates the pagetable as expected */
	expected = 0xf0f0f0f0f0f0f0f0ULL;
	xe_map_memset(xe, &pt->vmap, 0, (u8)expected, pt->size);

	then = ktime_get();
	fence = xe_migrate_update_pgtables(m, m->q->vm, NULL, m->q, &update, 1,
					   NULL, 0, &pt_update);
	now = ktime_get();
	if (sanity_fence_failed(xe, fence, "Migration pagetable update", test))
		return;

	kunit_info(test, "Updating without syncing took %llu us,\n",
		   (unsigned long long)ktime_to_us(ktime_sub(now, then)));

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
				   XE_BO_CREATE_VRAM_IF_DGFX(tile) |
				   XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(big)) {
		KUNIT_FAIL(test, "Failed to allocate bo: %li\n", PTR_ERR(big));
		goto vunmap;
	}

	pt = xe_bo_create_pin_map(xe, tile, m->q->vm, XE_PAGE_SIZE,
				  ttm_bo_type_kernel,
				  XE_BO_CREATE_VRAM_IF_DGFX(tile) |
				  XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(pt)) {
		KUNIT_FAIL(test, "Failed to allocate fake pt: %li\n",
			   PTR_ERR(pt));
		goto free_big;
	}

	tiny = xe_bo_create_pin_map(xe, tile, m->q->vm,
				    2 * SZ_4K,
				    ttm_bo_type_kernel,
				    XE_BO_CREATE_VRAM_IF_DGFX(tile) |
				    XE_BO_CREATE_PINNED_BIT);
	if (IS_ERR(tiny)) {
		KUNIT_FAIL(test, "Failed to allocate fake pt: %li\n",
			   PTR_ERR(pt));
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
	fence = xe_migrate_clear(m, tiny, tiny->ttm.resource);
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
	fence = xe_migrate_clear(m, big, big->ttm.resource);
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

	kunit_info(test, "Testing page table update using CPU if GPU idle.\n");
	test_pt_update(m, pt, test, false);
	kunit_info(test, "Testing page table update using GPU\n");
	test_pt_update(m, pt, test, true);

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
	struct xe_tile *tile;
	int id;

	for_each_tile(tile, xe, id) {
		struct xe_migrate *m = tile->migrate;

		kunit_info(test, "Testing tile id %d.\n", id);
		xe_vm_lock(m->q->vm, true);
		xe_device_mem_access_get(xe);
		xe_migrate_sanity_test(m, test);
		xe_device_mem_access_put(xe);
		xe_vm_unlock(m->q->vm);
	}

	return 0;
}

void xe_migrate_sanity_kunit(struct kunit *test)
{
	xe_call_for_each_device(migrate_test_run_device);
}
EXPORT_SYMBOL_IF_KUNIT(xe_migrate_sanity_kunit);
