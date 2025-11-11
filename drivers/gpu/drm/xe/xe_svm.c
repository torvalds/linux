// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <drm/drm_drv.h>

#include "xe_bo.h"
#include "xe_exec_queue_types.h"
#include "xe_gt_stats.h"
#include "xe_migrate.h"
#include "xe_module.h"
#include "xe_pm.h"
#include "xe_pt.h"
#include "xe_svm.h"
#include "xe_tile.h"
#include "xe_ttm_vram_mgr.h"
#include "xe_vm.h"
#include "xe_vm_types.h"
#include "xe_vram_types.h"

static bool xe_svm_range_in_vram(struct xe_svm_range *range)
{
	/*
	 * Advisory only check whether the range is currently backed by VRAM
	 * memory.
	 */

	struct drm_gpusvm_pages_flags flags = {
		/* Pairs with WRITE_ONCE in drm_gpusvm.c */
		.__flags = READ_ONCE(range->base.pages.flags.__flags),
	};

	return flags.has_devmem_pages;
}

static bool xe_svm_range_has_vram_binding(struct xe_svm_range *range)
{
	/* Not reliable without notifier lock */
	return xe_svm_range_in_vram(range) && range->tile_present;
}

static struct xe_vm *gpusvm_to_vm(struct drm_gpusvm *gpusvm)
{
	return container_of(gpusvm, struct xe_vm, svm.gpusvm);
}

static struct xe_vm *range_to_vm(struct drm_gpusvm_range *r)
{
	return gpusvm_to_vm(r->gpusvm);
}

#define range_debug(r__, operation__)					\
	vm_dbg(&range_to_vm(&(r__)->base)->xe->drm,			\
	       "%s: asid=%u, gpusvm=%p, vram=%d,%d, seqno=%lu, " \
	       "start=0x%014lx, end=0x%014lx, size=%lu",		\
	       (operation__), range_to_vm(&(r__)->base)->usm.asid,	\
	       (r__)->base.gpusvm,					\
	       xe_svm_range_in_vram((r__)) ? 1 : 0,			\
	       xe_svm_range_has_vram_binding((r__)) ? 1 : 0,		\
	       (r__)->base.pages.notifier_seq,				\
	       xe_svm_range_start((r__)), xe_svm_range_end((r__)),	\
	       xe_svm_range_size((r__)))

void xe_svm_range_debug(struct xe_svm_range *range, const char *operation)
{
	range_debug(range, operation);
}

static struct drm_gpusvm_range *
xe_svm_range_alloc(struct drm_gpusvm *gpusvm)
{
	struct xe_svm_range *range;

	range = kzalloc(sizeof(*range), GFP_KERNEL);
	if (!range)
		return NULL;

	INIT_LIST_HEAD(&range->garbage_collector_link);
	xe_vm_get(gpusvm_to_vm(gpusvm));

	return &range->base;
}

static void xe_svm_range_free(struct drm_gpusvm_range *range)
{
	xe_vm_put(range_to_vm(range));
	kfree(range);
}

static void
xe_svm_garbage_collector_add_range(struct xe_vm *vm, struct xe_svm_range *range,
				   const struct mmu_notifier_range *mmu_range)
{
	struct xe_device *xe = vm->xe;

	range_debug(range, "GARBAGE COLLECTOR ADD");

	drm_gpusvm_range_set_unmapped(&range->base, mmu_range);

	spin_lock(&vm->svm.garbage_collector.lock);
	if (list_empty(&range->garbage_collector_link))
		list_add_tail(&range->garbage_collector_link,
			      &vm->svm.garbage_collector.range_list);
	spin_unlock(&vm->svm.garbage_collector.lock);

	queue_work(xe_device_get_root_tile(xe)->primary_gt->usm.pf_wq,
		   &vm->svm.garbage_collector.work);
}

static void xe_svm_tlb_inval_count_stats_incr(struct xe_gt *gt)
{
	xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_TLB_INVAL_COUNT, 1);
}

static u8
xe_svm_range_notifier_event_begin(struct xe_vm *vm, struct drm_gpusvm_range *r,
				  const struct mmu_notifier_range *mmu_range,
				  u64 *adj_start, u64 *adj_end)
{
	struct xe_svm_range *range = to_xe_range(r);
	struct xe_device *xe = vm->xe;
	struct xe_tile *tile;
	u8 tile_mask = 0;
	u8 id;

	xe_svm_assert_in_notifier(vm);

	range_debug(range, "NOTIFIER");

	/* Skip if already unmapped or if no binding exist */
	if (range->base.pages.flags.unmapped || !range->tile_present)
		return 0;

	range_debug(range, "NOTIFIER - EXECUTE");

	/* Adjust invalidation to range boundaries */
	*adj_start = min(xe_svm_range_start(range), mmu_range->start);
	*adj_end = max(xe_svm_range_end(range), mmu_range->end);

	/*
	 * XXX: Ideally would zap PTEs in one shot in xe_svm_invalidate but the
	 * invalidation code can't correctly cope with sparse ranges or
	 * invalidations spanning multiple ranges.
	 */
	for_each_tile(tile, xe, id)
		if (xe_pt_zap_ptes_range(tile, vm, range)) {
			/*
			 * WRITE_ONCE pairs with READ_ONCE in
			 * xe_vm_has_valid_gpu_mapping()
			 */
			WRITE_ONCE(range->tile_invalidated,
				   range->tile_invalidated | BIT(id));

			if (!(tile_mask & BIT(id))) {
				xe_svm_tlb_inval_count_stats_incr(tile->primary_gt);
				if (tile->media_gt)
					xe_svm_tlb_inval_count_stats_incr(tile->media_gt);
				tile_mask |= BIT(id);
			}
		}

	return tile_mask;
}

static void
xe_svm_range_notifier_event_end(struct xe_vm *vm, struct drm_gpusvm_range *r,
				const struct mmu_notifier_range *mmu_range)
{
	struct drm_gpusvm_ctx ctx = { .in_notifier = true, };

	xe_svm_assert_in_notifier(vm);

	drm_gpusvm_range_unmap_pages(&vm->svm.gpusvm, r, &ctx);
	if (!xe_vm_is_closed(vm) && mmu_range->event == MMU_NOTIFY_UNMAP)
		xe_svm_garbage_collector_add_range(vm, to_xe_range(r),
						   mmu_range);
}

static s64 xe_svm_stats_ktime_us_delta(ktime_t start)
{
	return IS_ENABLED(CONFIG_DEBUG_FS) ?
		ktime_us_delta(ktime_get(), start) : 0;
}

static void xe_svm_tlb_inval_us_stats_incr(struct xe_gt *gt, ktime_t start)
{
	s64 us_delta = xe_svm_stats_ktime_us_delta(start);

	xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_TLB_INVAL_US, us_delta);
}

static ktime_t xe_svm_stats_ktime_get(void)
{
	return IS_ENABLED(CONFIG_DEBUG_FS) ? ktime_get() : 0;
}

static void xe_svm_invalidate(struct drm_gpusvm *gpusvm,
			      struct drm_gpusvm_notifier *notifier,
			      const struct mmu_notifier_range *mmu_range)
{
	struct xe_vm *vm = gpusvm_to_vm(gpusvm);
	struct xe_device *xe = vm->xe;
	struct drm_gpusvm_range *r, *first;
	struct xe_tile *tile;
	ktime_t start = xe_svm_stats_ktime_get();
	u64 adj_start = mmu_range->start, adj_end = mmu_range->end;
	u8 tile_mask = 0, id;
	long err;

	xe_svm_assert_in_notifier(vm);

	vm_dbg(&gpusvm_to_vm(gpusvm)->xe->drm,
	       "INVALIDATE: asid=%u, gpusvm=%p, seqno=%lu, start=0x%016lx, end=0x%016lx, event=%d",
	       vm->usm.asid, gpusvm, notifier->notifier.invalidate_seq,
	       mmu_range->start, mmu_range->end, mmu_range->event);

	/* Adjust invalidation to notifier boundaries */
	adj_start = max(drm_gpusvm_notifier_start(notifier), adj_start);
	adj_end = min(drm_gpusvm_notifier_end(notifier), adj_end);

	first = drm_gpusvm_range_find(notifier, adj_start, adj_end);
	if (!first)
		return;

	/*
	 * PTs may be getting destroyed so not safe to touch these but PT should
	 * be invalidated at this point in time. Regardless we still need to
	 * ensure any dma mappings are unmapped in the here.
	 */
	if (xe_vm_is_closed(vm))
		goto range_notifier_event_end;

	/*
	 * XXX: Less than ideal to always wait on VM's resv slots if an
	 * invalidation is not required. Could walk range list twice to figure
	 * out if an invalidations is need, but also not ideal.
	 */
	err = dma_resv_wait_timeout(xe_vm_resv(vm),
				    DMA_RESV_USAGE_BOOKKEEP,
				    false, MAX_SCHEDULE_TIMEOUT);
	XE_WARN_ON(err <= 0);

	r = first;
	drm_gpusvm_for_each_range(r, notifier, adj_start, adj_end)
		tile_mask |= xe_svm_range_notifier_event_begin(vm, r, mmu_range,
							       &adj_start,
							       &adj_end);
	if (!tile_mask)
		goto range_notifier_event_end;

	xe_device_wmb(xe);

	err = xe_vm_range_tilemask_tlb_inval(vm, adj_start, adj_end, tile_mask);
	WARN_ON_ONCE(err);

range_notifier_event_end:
	r = first;
	drm_gpusvm_for_each_range(r, notifier, adj_start, adj_end)
		xe_svm_range_notifier_event_end(vm, r, mmu_range);
	for_each_tile(tile, xe, id) {
		if (tile_mask & BIT(id)) {
			xe_svm_tlb_inval_us_stats_incr(tile->primary_gt, start);
			if (tile->media_gt)
				xe_svm_tlb_inval_us_stats_incr(tile->media_gt, start);
		}
	}
}

static int __xe_svm_garbage_collector(struct xe_vm *vm,
				      struct xe_svm_range *range)
{
	struct dma_fence *fence;

	range_debug(range, "GARBAGE COLLECTOR");

	xe_vm_lock(vm, false);
	fence = xe_vm_range_unbind(vm, range);
	xe_vm_unlock(vm);
	if (IS_ERR(fence))
		return PTR_ERR(fence);
	dma_fence_put(fence);

	drm_gpusvm_range_remove(&vm->svm.gpusvm, &range->base);

	return 0;
}

static int xe_svm_range_set_default_attr(struct xe_vm *vm, u64 range_start, u64 range_end)
{
	struct xe_vma *vma;
	struct xe_vma_mem_attr default_attr = {
		.preferred_loc = {
			.devmem_fd = DRM_XE_PREFERRED_LOC_DEFAULT_DEVICE,
			.migration_policy = DRM_XE_MIGRATE_ALL_PAGES,
		},
		.atomic_access = DRM_XE_ATOMIC_UNDEFINED,
	};
	int err = 0;

	vma = xe_vm_find_vma_by_addr(vm, range_start);
	if (!vma)
		return -EINVAL;

	if (!(vma->gpuva.flags & XE_VMA_MADV_AUTORESET)) {
		drm_dbg(&vm->xe->drm, "Skipping madvise reset for vma.\n");
		return 0;
	}

	if (xe_vma_has_default_mem_attrs(vma))
		return 0;

	vm_dbg(&vm->xe->drm, "Existing VMA start=0x%016llx, vma_end=0x%016llx",
	       xe_vma_start(vma), xe_vma_end(vma));

	if (xe_vma_start(vma) == range_start && xe_vma_end(vma) == range_end) {
		default_attr.pat_index = vma->attr.default_pat_index;
		default_attr.default_pat_index  = vma->attr.default_pat_index;
		vma->attr = default_attr;
	} else {
		vm_dbg(&vm->xe->drm, "Split VMA start=0x%016llx, vma_end=0x%016llx",
		       range_start, range_end);
		err = xe_vm_alloc_cpu_addr_mirror_vma(vm, range_start, range_end - range_start);
		if (err) {
			drm_warn(&vm->xe->drm, "VMA SPLIT failed: %pe\n", ERR_PTR(err));
			xe_vm_kill(vm, true);
			return err;
		}
	}

	/*
	 * On call from xe_svm_handle_pagefault original VMA might be changed
	 * signal this to lookup for VMA again.
	 */
	return -EAGAIN;
}

static int xe_svm_garbage_collector(struct xe_vm *vm)
{
	struct xe_svm_range *range;
	u64 range_start;
	u64 range_end;
	int err, ret = 0;

	lockdep_assert_held_write(&vm->lock);

	if (xe_vm_is_closed_or_banned(vm))
		return -ENOENT;

	for (;;) {
		spin_lock(&vm->svm.garbage_collector.lock);
		range = list_first_entry_or_null(&vm->svm.garbage_collector.range_list,
						 typeof(*range),
						 garbage_collector_link);
		if (!range)
			break;

		range_start = xe_svm_range_start(range);
		range_end = xe_svm_range_end(range);

		list_del(&range->garbage_collector_link);
		spin_unlock(&vm->svm.garbage_collector.lock);

		err = __xe_svm_garbage_collector(vm, range);
		if (err) {
			drm_warn(&vm->xe->drm,
				 "Garbage collection failed: %pe\n",
				 ERR_PTR(err));
			xe_vm_kill(vm, true);
			return err;
		}

		err = xe_svm_range_set_default_attr(vm, range_start, range_end);
		if (err) {
			if (err == -EAGAIN)
				ret = -EAGAIN;
			else
				return err;
		}
	}
	spin_unlock(&vm->svm.garbage_collector.lock);

	return ret;
}

static void xe_svm_garbage_collector_work_func(struct work_struct *w)
{
	struct xe_vm *vm = container_of(w, struct xe_vm,
					svm.garbage_collector.work);

	down_write(&vm->lock);
	xe_svm_garbage_collector(vm);
	up_write(&vm->lock);
}

#if IS_ENABLED(CONFIG_DRM_XE_PAGEMAP)

static struct xe_vram_region *page_to_vr(struct page *page)
{
	return container_of(page_pgmap(page), struct xe_vram_region, pagemap);
}

static u64 xe_vram_region_page_to_dpa(struct xe_vram_region *vr,
				      struct page *page)
{
	u64 dpa;
	u64 pfn = page_to_pfn(page);
	u64 offset;

	xe_assert(vr->xe, is_device_private_page(page));
	xe_assert(vr->xe, (pfn << PAGE_SHIFT) >= vr->hpa_base);

	offset = (pfn << PAGE_SHIFT) - vr->hpa_base;
	dpa = vr->dpa_base + offset;

	return dpa;
}

enum xe_svm_copy_dir {
	XE_SVM_COPY_TO_VRAM,
	XE_SVM_COPY_TO_SRAM,
};

static void xe_svm_copy_kb_stats_incr(struct xe_gt *gt,
				      const enum xe_svm_copy_dir dir,
				      int kb)
{
	if (dir == XE_SVM_COPY_TO_VRAM)
		xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_DEVICE_COPY_KB, kb);
	else
		xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_CPU_COPY_KB, kb);
}

static void xe_svm_copy_us_stats_incr(struct xe_gt *gt,
				      const enum xe_svm_copy_dir dir,
				      unsigned long npages,
				      ktime_t start)
{
	s64 us_delta = xe_svm_stats_ktime_us_delta(start);

	if (dir == XE_SVM_COPY_TO_VRAM) {
		switch (npages) {
		case 1:
			xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_4K_DEVICE_COPY_US,
					 us_delta);
			break;
		case 16:
			xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_64K_DEVICE_COPY_US,
					 us_delta);
			break;
		case 512:
			xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_2M_DEVICE_COPY_US,
					 us_delta);
			break;
		}
		xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_DEVICE_COPY_US,
				 us_delta);
	} else {
		switch (npages) {
		case 1:
			xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_4K_CPU_COPY_US,
					 us_delta);
			break;
		case 16:
			xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_64K_CPU_COPY_US,
					 us_delta);
			break;
		case 512:
			xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_2M_CPU_COPY_US,
					 us_delta);
			break;
		}
		xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_CPU_COPY_US,
				 us_delta);
	}
}

static int xe_svm_copy(struct page **pages,
		       struct drm_pagemap_addr *pagemap_addr,
		       unsigned long npages, const enum xe_svm_copy_dir dir)
{
	struct xe_vram_region *vr = NULL;
	struct xe_gt *gt = NULL;
	struct xe_device *xe;
	struct dma_fence *fence = NULL;
	unsigned long i;
#define XE_VRAM_ADDR_INVALID	~0x0ull
	u64 vram_addr = XE_VRAM_ADDR_INVALID;
	int err = 0, pos = 0;
	bool sram = dir == XE_SVM_COPY_TO_SRAM;
	ktime_t start = xe_svm_stats_ktime_get();

	/*
	 * This flow is complex: it locates physically contiguous device pages,
	 * derives the starting physical address, and performs a single GPU copy
	 * to for every 8M chunk in a DMA address array. Both device pages and
	 * DMA addresses may be sparsely populated. If either is NULL, a copy is
	 * triggered based on the current search state. The last GPU copy is
	 * waited on to ensure all copies are complete.
	 */

	for (i = 0; i < npages; ++i) {
		struct page *spage = pages[i];
		struct dma_fence *__fence;
		u64 __vram_addr;
		bool match = false, chunk, last;

#define XE_MIGRATE_CHUNK_SIZE	SZ_8M
		chunk = (i - pos) == (XE_MIGRATE_CHUNK_SIZE / PAGE_SIZE);
		last = (i + 1) == npages;

		/* No CPU page and no device pages queue'd to copy */
		if (!pagemap_addr[i].addr && vram_addr == XE_VRAM_ADDR_INVALID)
			continue;

		if (!vr && spage) {
			vr = page_to_vr(spage);
			gt = xe_migrate_exec_queue(vr->migrate)->gt;
			xe = vr->xe;
		}
		XE_WARN_ON(spage && page_to_vr(spage) != vr);

		/*
		 * CPU page and device page valid, capture physical address on
		 * first device page, check if physical contiguous on subsequent
		 * device pages.
		 */
		if (pagemap_addr[i].addr && spage) {
			__vram_addr = xe_vram_region_page_to_dpa(vr, spage);
			if (vram_addr == XE_VRAM_ADDR_INVALID) {
				vram_addr = __vram_addr;
				pos = i;
			}

			match = vram_addr + PAGE_SIZE * (i - pos) == __vram_addr;
			/* Expected with contiguous memory */
			xe_assert(vr->xe, match);

			if (pagemap_addr[i].order) {
				i += NR_PAGES(pagemap_addr[i].order) - 1;
				chunk = (i - pos) == (XE_MIGRATE_CHUNK_SIZE / PAGE_SIZE);
				last = (i + 1) == npages;
			}
		}

		/*
		 * Mismatched physical address, 8M copy chunk, or last page -
		 * trigger a copy.
		 */
		if (!match || chunk || last) {
			/*
			 * Extra page for first copy if last page and matching
			 * physical address.
			 */
			int incr = (match && last) ? 1 : 0;

			if (vram_addr != XE_VRAM_ADDR_INVALID) {
				xe_svm_copy_kb_stats_incr(gt, dir,
							  (i - pos + incr) *
							  (PAGE_SIZE / SZ_1K));
				if (sram) {
					vm_dbg(&xe->drm,
					       "COPY TO SRAM - 0x%016llx -> 0x%016llx, NPAGES=%ld",
					       vram_addr,
					       (u64)pagemap_addr[pos].addr, i - pos + incr);
					__fence = xe_migrate_from_vram(vr->migrate,
								       i - pos + incr,
								       vram_addr,
								       &pagemap_addr[pos]);
				} else {
					vm_dbg(&xe->drm,
					       "COPY TO VRAM - 0x%016llx -> 0x%016llx, NPAGES=%ld",
					       (u64)pagemap_addr[pos].addr, vram_addr,
					       i - pos + incr);
					__fence = xe_migrate_to_vram(vr->migrate,
								     i - pos + incr,
								     &pagemap_addr[pos],
								     vram_addr);
				}
				if (IS_ERR(__fence)) {
					err = PTR_ERR(__fence);
					goto err_out;
				}

				dma_fence_put(fence);
				fence = __fence;
			}

			/* Setup physical address of next device page */
			if (pagemap_addr[i].addr && spage) {
				vram_addr = __vram_addr;
				pos = i;
			} else {
				vram_addr = XE_VRAM_ADDR_INVALID;
			}

			/* Extra mismatched device page, copy it */
			if (!match && last && vram_addr != XE_VRAM_ADDR_INVALID) {
				xe_svm_copy_kb_stats_incr(gt, dir,
							  (PAGE_SIZE / SZ_1K));
				if (sram) {
					vm_dbg(&xe->drm,
					       "COPY TO SRAM - 0x%016llx -> 0x%016llx, NPAGES=%d",
					       vram_addr, (u64)pagemap_addr[pos].addr, 1);
					__fence = xe_migrate_from_vram(vr->migrate, 1,
								       vram_addr,
								       &pagemap_addr[pos]);
				} else {
					vm_dbg(&xe->drm,
					       "COPY TO VRAM - 0x%016llx -> 0x%016llx, NPAGES=%d",
					       (u64)pagemap_addr[pos].addr, vram_addr, 1);
					__fence = xe_migrate_to_vram(vr->migrate, 1,
								     &pagemap_addr[pos],
								     vram_addr);
				}
				if (IS_ERR(__fence)) {
					err = PTR_ERR(__fence);
					goto err_out;
				}

				dma_fence_put(fence);
				fence = __fence;
			}
		}
	}

err_out:
	/* Wait for all copies to complete */
	if (fence) {
		dma_fence_wait(fence, false);
		dma_fence_put(fence);
	}

	/*
	 * XXX: We can't derive the GT here (or anywhere in this functions, but
	 * compute always uses the primary GT so accumlate stats on the likely
	 * GT of the fault.
	 */
	if (gt)
		xe_svm_copy_us_stats_incr(gt, dir, npages, start);

	return err;
#undef XE_MIGRATE_CHUNK_SIZE
#undef XE_VRAM_ADDR_INVALID
}

static int xe_svm_copy_to_devmem(struct page **pages,
				 struct drm_pagemap_addr *pagemap_addr,
				 unsigned long npages)
{
	return xe_svm_copy(pages, pagemap_addr, npages, XE_SVM_COPY_TO_VRAM);
}

static int xe_svm_copy_to_ram(struct page **pages,
			      struct drm_pagemap_addr *pagemap_addr,
			      unsigned long npages)
{
	return xe_svm_copy(pages, pagemap_addr, npages, XE_SVM_COPY_TO_SRAM);
}

static struct xe_bo *to_xe_bo(struct drm_pagemap_devmem *devmem_allocation)
{
	return container_of(devmem_allocation, struct xe_bo, devmem_allocation);
}

static void xe_svm_devmem_release(struct drm_pagemap_devmem *devmem_allocation)
{
	struct xe_bo *bo = to_xe_bo(devmem_allocation);
	struct xe_device *xe = xe_bo_device(bo);

	xe_bo_put_async(bo);
	xe_pm_runtime_put(xe);
}

static u64 block_offset_to_pfn(struct xe_vram_region *vr, u64 offset)
{
	return PHYS_PFN(offset + vr->hpa_base);
}

static struct drm_buddy *vram_to_buddy(struct xe_vram_region *vram)
{
	return &vram->ttm.mm;
}

static int xe_svm_populate_devmem_pfn(struct drm_pagemap_devmem *devmem_allocation,
				      unsigned long npages, unsigned long *pfn)
{
	struct xe_bo *bo = to_xe_bo(devmem_allocation);
	struct ttm_resource *res = bo->ttm.resource;
	struct list_head *blocks = &to_xe_ttm_vram_mgr_resource(res)->blocks;
	struct drm_buddy_block *block;
	int j = 0;

	list_for_each_entry(block, blocks, link) {
		struct xe_vram_region *vr = block->private;
		struct drm_buddy *buddy = vram_to_buddy(vr);
		u64 block_pfn = block_offset_to_pfn(vr, drm_buddy_block_offset(block));
		int i;

		for (i = 0; i < drm_buddy_block_size(buddy, block) >> PAGE_SHIFT; ++i)
			pfn[j++] = block_pfn + i;
	}

	return 0;
}

static const struct drm_pagemap_devmem_ops dpagemap_devmem_ops = {
	.devmem_release = xe_svm_devmem_release,
	.populate_devmem_pfn = xe_svm_populate_devmem_pfn,
	.copy_to_devmem = xe_svm_copy_to_devmem,
	.copy_to_ram = xe_svm_copy_to_ram,
};

#endif

static const struct drm_gpusvm_ops gpusvm_ops = {
	.range_alloc = xe_svm_range_alloc,
	.range_free = xe_svm_range_free,
	.invalidate = xe_svm_invalidate,
};

static const unsigned long fault_chunk_sizes[] = {
	SZ_2M,
	SZ_64K,
	SZ_4K,
};

/**
 * xe_svm_init() - SVM initialize
 * @vm: The VM.
 *
 * Initialize SVM state which is embedded within the VM.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_svm_init(struct xe_vm *vm)
{
	int err;

	if (vm->flags & XE_VM_FLAG_FAULT_MODE) {
		spin_lock_init(&vm->svm.garbage_collector.lock);
		INIT_LIST_HEAD(&vm->svm.garbage_collector.range_list);
		INIT_WORK(&vm->svm.garbage_collector.work,
			  xe_svm_garbage_collector_work_func);

		err = drm_gpusvm_init(&vm->svm.gpusvm, "Xe SVM", &vm->xe->drm,
				      current->mm, 0, vm->size,
				      xe_modparam.svm_notifier_size * SZ_1M,
				      &gpusvm_ops, fault_chunk_sizes,
				      ARRAY_SIZE(fault_chunk_sizes));
		drm_gpusvm_driver_set_lock(&vm->svm.gpusvm, &vm->lock);
	} else {
		err = drm_gpusvm_init(&vm->svm.gpusvm, "Xe SVM (simple)",
				      &vm->xe->drm, NULL, 0, 0, 0, NULL,
				      NULL, 0);
	}

	return err;
}

/**
 * xe_svm_close() - SVM close
 * @vm: The VM.
 *
 * Close SVM state (i.e., stop and flush all SVM actions).
 */
void xe_svm_close(struct xe_vm *vm)
{
	xe_assert(vm->xe, xe_vm_is_closed(vm));
	flush_work(&vm->svm.garbage_collector.work);
}

/**
 * xe_svm_fini() - SVM finalize
 * @vm: The VM.
 *
 * Finalize SVM state which is embedded within the VM.
 */
void xe_svm_fini(struct xe_vm *vm)
{
	xe_assert(vm->xe, xe_vm_is_closed(vm));

	drm_gpusvm_fini(&vm->svm.gpusvm);
}

static bool xe_svm_range_is_valid(struct xe_svm_range *range,
				  struct xe_tile *tile,
				  bool devmem_only)
{
	return (xe_vm_has_valid_gpu_mapping(tile, range->tile_present,
					    range->tile_invalidated) &&
		(!devmem_only || xe_svm_range_in_vram(range)));
}

/** xe_svm_range_migrate_to_smem() - Move range pages from VRAM to SMEM
 * @vm: xe_vm pointer
 * @range: Pointer to the SVM range structure
 *
 * The xe_svm_range_migrate_to_smem() checks range has pages in VRAM
 * and migrates them to SMEM
 */
void xe_svm_range_migrate_to_smem(struct xe_vm *vm, struct xe_svm_range *range)
{
	if (xe_svm_range_in_vram(range))
		drm_gpusvm_range_evict(&vm->svm.gpusvm, &range->base);
}

/**
 * xe_svm_range_validate() - Check if the SVM range is valid
 * @vm: xe_vm pointer
 * @range: Pointer to the SVM range structure
 * @tile_mask: Mask representing the tiles to be checked
 * @devmem_preferred : if true range needs to be in devmem
 *
 * The xe_svm_range_validate() function checks if a range is
 * valid and located in the desired memory region.
 *
 * Return: true if the range is valid, false otherwise
 */
bool xe_svm_range_validate(struct xe_vm *vm,
			   struct xe_svm_range *range,
			   u8 tile_mask, bool devmem_preferred)
{
	bool ret;

	xe_svm_notifier_lock(vm);

	ret = (range->tile_present & ~range->tile_invalidated & tile_mask) == tile_mask &&
	       (devmem_preferred == range->base.pages.flags.has_devmem_pages);

	xe_svm_notifier_unlock(vm);

	return ret;
}

/**
 * xe_svm_find_vma_start - Find start of CPU VMA
 * @vm: xe_vm pointer
 * @start: start address
 * @end: end address
 * @vma: Pointer to struct xe_vma
 *
 *
 * This function searches for a cpu vma, within the specified
 * range [start, end] in the given VM. It adjusts the range based on the
 * xe_vma start and end addresses. If no cpu VMA is found, it returns ULONG_MAX.
 *
 * Return: The starting address of the VMA within the range,
 * or ULONG_MAX if no VMA is found
 */
u64 xe_svm_find_vma_start(struct xe_vm *vm, u64 start, u64 end, struct xe_vma *vma)
{
	return drm_gpusvm_find_vma_start(&vm->svm.gpusvm,
					 max(start, xe_vma_start(vma)),
					 min(end, xe_vma_end(vma)));
}

#if IS_ENABLED(CONFIG_DRM_XE_PAGEMAP)
static int xe_drm_pagemap_populate_mm(struct drm_pagemap *dpagemap,
				      unsigned long start, unsigned long end,
				      struct mm_struct *mm,
				      unsigned long timeslice_ms)
{
	struct xe_vram_region *vr = container_of(dpagemap, typeof(*vr), dpagemap);
	struct xe_device *xe = vr->xe;
	struct device *dev = xe->drm.dev;
	struct drm_buddy_block *block;
	struct xe_validation_ctx vctx;
	struct list_head *blocks;
	struct drm_exec exec;
	struct xe_bo *bo;
	int err = 0, idx;

	if (!drm_dev_enter(&xe->drm, &idx))
		return -ENODEV;

	xe_pm_runtime_get(xe);

	xe_validation_guard(&vctx, &xe->val, &exec, (struct xe_val_flags) {}, err) {
		bo = xe_bo_create_locked(xe, NULL, NULL, end - start,
					 ttm_bo_type_device,
					 (IS_DGFX(xe) ? XE_BO_FLAG_VRAM(vr) : XE_BO_FLAG_SYSTEM) |
					 XE_BO_FLAG_CPU_ADDR_MIRROR, &exec);
		drm_exec_retry_on_contention(&exec);
		if (IS_ERR(bo)) {
			err = PTR_ERR(bo);
			xe_validation_retry_on_oom(&vctx, &err);
			break;
		}

		drm_pagemap_devmem_init(&bo->devmem_allocation, dev, mm,
					&dpagemap_devmem_ops, dpagemap, end - start);

		blocks = &to_xe_ttm_vram_mgr_resource(bo->ttm.resource)->blocks;
		list_for_each_entry(block, blocks, link)
			block->private = vr;

		xe_bo_get(bo);

		/* Ensure the device has a pm ref while there are device pages active. */
		xe_pm_runtime_get_noresume(xe);
		err = drm_pagemap_migrate_to_devmem(&bo->devmem_allocation, mm,
						    start, end, timeslice_ms,
						    xe_svm_devm_owner(xe));
		if (err)
			xe_svm_devmem_release(&bo->devmem_allocation);
		xe_bo_unlock(bo);
		xe_bo_put(bo);
	}
	xe_pm_runtime_put(xe);
	drm_dev_exit(idx);

	return err;
}
#endif

static bool supports_4K_migration(struct xe_device *xe)
{
	if (xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
		return false;

	return true;
}

/**
 * xe_svm_range_needs_migrate_to_vram() - SVM range needs migrate to VRAM or not
 * @range: SVM range for which migration needs to be decided
 * @vma: vma which has range
 * @preferred_region_is_vram: preferred region for range is vram
 *
 * Return: True for range needing migration and migration is supported else false
 */
bool xe_svm_range_needs_migrate_to_vram(struct xe_svm_range *range, struct xe_vma *vma,
					bool preferred_region_is_vram)
{
	struct xe_vm *vm = range_to_vm(&range->base);
	u64 range_size = xe_svm_range_size(range);

	if (!range->base.pages.flags.migrate_devmem || !preferred_region_is_vram)
		return false;

	xe_assert(vm->xe, IS_DGFX(vm->xe));

	if (xe_svm_range_in_vram(range)) {
		drm_info(&vm->xe->drm, "Range is already in VRAM\n");
		return false;
	}

	if (range_size < SZ_64K && !supports_4K_migration(vm->xe)) {
		drm_dbg(&vm->xe->drm, "Platform doesn't support SZ_4K range migration\n");
		return false;
	}

	return true;
}

#define DECL_SVM_RANGE_COUNT_STATS(elem, stat) \
static void xe_svm_range_##elem##_count_stats_incr(struct xe_gt *gt, \
						   struct xe_svm_range *range) \
{ \
	switch (xe_svm_range_size(range)) { \
	case SZ_4K: \
		xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_4K_##stat##_COUNT, 1); \
		break; \
	case SZ_64K: \
		xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_64K_##stat##_COUNT, 1); \
		break; \
	case SZ_2M: \
		xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_2M_##stat##_COUNT, 1); \
		break; \
	} \
} \

DECL_SVM_RANGE_COUNT_STATS(fault, PAGEFAULT)
DECL_SVM_RANGE_COUNT_STATS(valid_fault, VALID_PAGEFAULT)
DECL_SVM_RANGE_COUNT_STATS(migrate, MIGRATE)

#define DECL_SVM_RANGE_US_STATS(elem, stat) \
static void xe_svm_range_##elem##_us_stats_incr(struct xe_gt *gt, \
						struct xe_svm_range *range, \
						ktime_t start) \
{ \
	s64 us_delta = xe_svm_stats_ktime_us_delta(start); \
\
	switch (xe_svm_range_size(range)) { \
	case SZ_4K: \
		xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_4K_##stat##_US, \
				 us_delta); \
		break; \
	case SZ_64K: \
		xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_64K_##stat##_US, \
				 us_delta); \
		break; \
	case SZ_2M: \
		xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_2M_##stat##_US, \
				 us_delta); \
		break; \
	} \
} \

DECL_SVM_RANGE_US_STATS(migrate, MIGRATE)
DECL_SVM_RANGE_US_STATS(get_pages, GET_PAGES)
DECL_SVM_RANGE_US_STATS(bind, BIND)
DECL_SVM_RANGE_US_STATS(fault, PAGEFAULT)

static int __xe_svm_handle_pagefault(struct xe_vm *vm, struct xe_vma *vma,
				     struct xe_gt *gt, u64 fault_addr,
				     bool need_vram)
{
	int devmem_possible = IS_DGFX(vm->xe) &&
		IS_ENABLED(CONFIG_DRM_XE_PAGEMAP);
	struct drm_gpusvm_ctx ctx = {
		.read_only = xe_vma_read_only(vma),
		.devmem_possible = devmem_possible,
		.check_pages_threshold = devmem_possible ? SZ_64K : 0,
		.devmem_only = need_vram && devmem_possible,
		.timeslice_ms = need_vram && devmem_possible ?
			vm->xe->atomic_svm_timeslice_ms : 0,
		.device_private_page_owner = xe_svm_devm_owner(vm->xe),
	};
	struct xe_validation_ctx vctx;
	struct drm_exec exec;
	struct xe_svm_range *range;
	struct dma_fence *fence;
	struct drm_pagemap *dpagemap;
	struct xe_tile *tile = gt_to_tile(gt);
	int migrate_try_count = ctx.devmem_only ? 3 : 1;
	ktime_t start = xe_svm_stats_ktime_get(), bind_start, get_pages_start;
	int err;

	lockdep_assert_held_write(&vm->lock);
	xe_assert(vm->xe, xe_vma_is_cpu_addr_mirror(vma));

	xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_PAGEFAULT_COUNT, 1);

retry:
	/* Always process UNMAPs first so view SVM ranges is current */
	err = xe_svm_garbage_collector(vm);
	if (err)
		return err;

	dpagemap = xe_vma_resolve_pagemap(vma, tile);
	if (!dpagemap && !ctx.devmem_only)
		ctx.device_private_page_owner = NULL;
	range = xe_svm_range_find_or_insert(vm, fault_addr, vma, &ctx);

	if (IS_ERR(range))
		return PTR_ERR(range);

	xe_svm_range_fault_count_stats_incr(gt, range);

	if (ctx.devmem_only && !range->base.pages.flags.migrate_devmem) {
		err = -EACCES;
		goto out;
	}

	if (xe_svm_range_is_valid(range, tile, ctx.devmem_only)) {
		xe_svm_range_valid_fault_count_stats_incr(gt, range);
		range_debug(range, "PAGE FAULT - VALID");
		goto out;
	}

	range_debug(range, "PAGE FAULT");

	if (--migrate_try_count >= 0 &&
	    xe_svm_range_needs_migrate_to_vram(range, vma, !!dpagemap || ctx.devmem_only)) {
		ktime_t migrate_start = xe_svm_stats_ktime_get();

		/* TODO : For multi-device dpagemap will be used to find the
		 * remote tile and remote device. Will need to modify
		 * xe_svm_alloc_vram to use dpagemap for future multi-device
		 * support.
		 */
		xe_svm_range_migrate_count_stats_incr(gt, range);
		err = xe_svm_alloc_vram(tile, range, &ctx);
		xe_svm_range_migrate_us_stats_incr(gt, range, migrate_start);
		ctx.timeslice_ms <<= 1;	/* Double timeslice if we have to retry */
		if (err) {
			if (migrate_try_count || !ctx.devmem_only) {
				drm_dbg(&vm->xe->drm,
					"VRAM allocation failed, falling back to retrying fault, asid=%u, errno=%pe\n",
					vm->usm.asid, ERR_PTR(err));

				/*
				 * In the devmem-only case, mixed mappings may
				 * be found. The get_pages function will fix
				 * these up to a single location, allowing the
				 * page fault handler to make forward progress.
				 */
				if (ctx.devmem_only)
					goto get_pages;
				else
					goto retry;
			} else {
				drm_err(&vm->xe->drm,
					"VRAM allocation failed, retry count exceeded, asid=%u, errno=%pe\n",
					vm->usm.asid, ERR_PTR(err));
				return err;
			}
		}
	}

get_pages:
	get_pages_start = xe_svm_stats_ktime_get();

	range_debug(range, "GET PAGES");
	err = xe_svm_range_get_pages(vm, range, &ctx);
	/* Corner where CPU mappings have changed */
	if (err == -EOPNOTSUPP || err == -EFAULT || err == -EPERM) {
		ctx.timeslice_ms <<= 1;	/* Double timeslice if we have to retry */
		if (migrate_try_count > 0 || !ctx.devmem_only) {
			drm_dbg(&vm->xe->drm,
				"Get pages failed, falling back to retrying, asid=%u, gpusvm=%p, errno=%pe\n",
				vm->usm.asid, &vm->svm.gpusvm, ERR_PTR(err));
			range_debug(range, "PAGE FAULT - RETRY PAGES");
			goto retry;
		} else {
			drm_err(&vm->xe->drm,
				"Get pages failed, retry count exceeded, asid=%u, gpusvm=%p, errno=%pe\n",
				vm->usm.asid, &vm->svm.gpusvm, ERR_PTR(err));
		}
	}
	if (err) {
		range_debug(range, "PAGE FAULT - FAIL PAGE COLLECT");
		goto out;
	}

	xe_svm_range_get_pages_us_stats_incr(gt, range, get_pages_start);
	range_debug(range, "PAGE FAULT - BIND");

	bind_start = xe_svm_stats_ktime_get();
	xe_validation_guard(&vctx, &vm->xe->val, &exec, (struct xe_val_flags) {}, err) {
		err = xe_vm_drm_exec_lock(vm, &exec);
		drm_exec_retry_on_contention(&exec);

		xe_vm_set_validation_exec(vm, &exec);
		fence = xe_vm_range_rebind(vm, vma, range, BIT(tile->id));
		xe_vm_set_validation_exec(vm, NULL);
		if (IS_ERR(fence)) {
			drm_exec_retry_on_contention(&exec);
			err = PTR_ERR(fence);
			xe_validation_retry_on_oom(&vctx, &err);
			xe_svm_range_bind_us_stats_incr(gt, range, bind_start);
			break;
		}
	}
	if (err)
		goto err_out;

	dma_fence_wait(fence, false);
	dma_fence_put(fence);
	xe_svm_range_bind_us_stats_incr(gt, range, bind_start);

out:
	xe_svm_range_fault_us_stats_incr(gt, range, start);
	return 0;

err_out:
	if (err == -EAGAIN) {
		ctx.timeslice_ms <<= 1;	/* Double timeslice if we have to retry */
		range_debug(range, "PAGE FAULT - RETRY BIND");
		goto retry;
	}

	return err;
}

/**
 * xe_svm_handle_pagefault() - SVM handle page fault
 * @vm: The VM.
 * @vma: The CPU address mirror VMA.
 * @gt: The gt upon the fault occurred.
 * @fault_addr: The GPU fault address.
 * @atomic: The fault atomic access bit.
 *
 * Create GPU bindings for a SVM page fault. Optionally migrate to device
 * memory.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_svm_handle_pagefault(struct xe_vm *vm, struct xe_vma *vma,
			    struct xe_gt *gt, u64 fault_addr,
			    bool atomic)
{
	int need_vram, ret;
retry:
	need_vram = xe_vma_need_vram_for_atomic(vm->xe, vma, atomic);
	if (need_vram < 0)
		return need_vram;

	ret =  __xe_svm_handle_pagefault(vm, vma, gt, fault_addr,
					 need_vram ? true : false);
	if (ret == -EAGAIN) {
		/*
		 * Retry once on -EAGAIN to re-lookup the VMA, as the original VMA
		 * may have been split by xe_svm_range_set_default_attr.
		 */
		vma = xe_vm_find_vma_by_addr(vm, fault_addr);
		if (!vma)
			return -EINVAL;

		goto retry;
	}
	return ret;
}

/**
 * xe_svm_has_mapping() - SVM has mappings
 * @vm: The VM.
 * @start: Start address.
 * @end: End address.
 *
 * Check if an address range has SVM mappings.
 *
 * Return: True if address range has a SVM mapping, False otherwise
 */
bool xe_svm_has_mapping(struct xe_vm *vm, u64 start, u64 end)
{
	return drm_gpusvm_has_mapping(&vm->svm.gpusvm, start, end);
}

/**
 * xe_svm_unmap_address_range - UNMAP SVM mappings and ranges
 * @vm: The VM
 * @start: start addr
 * @end: end addr
 *
 * This function UNMAPS svm ranges if start or end address are inside them.
 */
void xe_svm_unmap_address_range(struct xe_vm *vm, u64 start, u64 end)
{
	struct drm_gpusvm_notifier *notifier, *next;

	lockdep_assert_held_write(&vm->lock);

	drm_gpusvm_for_each_notifier_safe(notifier, next, &vm->svm.gpusvm, start, end) {
		struct drm_gpusvm_range *range, *__next;

		drm_gpusvm_for_each_range_safe(range, __next, notifier, start, end) {
			if (start > drm_gpusvm_range_start(range) ||
			    end < drm_gpusvm_range_end(range)) {
				if (IS_DGFX(vm->xe) && xe_svm_range_in_vram(to_xe_range(range)))
					drm_gpusvm_range_evict(&vm->svm.gpusvm, range);
				drm_gpusvm_range_get(range);
				__xe_svm_garbage_collector(vm, to_xe_range(range));
				if (!list_empty(&to_xe_range(range)->garbage_collector_link)) {
					spin_lock(&vm->svm.garbage_collector.lock);
					list_del(&to_xe_range(range)->garbage_collector_link);
					spin_unlock(&vm->svm.garbage_collector.lock);
				}
				drm_gpusvm_range_put(range);
			}
		}
	}
}

/**
 * xe_svm_bo_evict() - SVM evict BO to system memory
 * @bo: BO to evict
 *
 * SVM evict BO to system memory. GPU SVM layer ensures all device pages
 * are evicted before returning.
 *
 * Return: 0 on success standard error code otherwise
 */
int xe_svm_bo_evict(struct xe_bo *bo)
{
	return drm_pagemap_evict_to_ram(&bo->devmem_allocation);
}

/**
 * xe_svm_range_find_or_insert- Find or insert GPU SVM range
 * @vm: xe_vm pointer
 * @addr: address for which range needs to be found/inserted
 * @vma:  Pointer to struct xe_vma which mirrors CPU
 * @ctx: GPU SVM context
 *
 * This function finds or inserts a newly allocated a SVM range based on the
 * address.
 *
 * Return: Pointer to the SVM range on success, ERR_PTR() on failure.
 */
struct xe_svm_range *xe_svm_range_find_or_insert(struct xe_vm *vm, u64 addr,
						 struct xe_vma *vma, struct drm_gpusvm_ctx *ctx)
{
	struct drm_gpusvm_range *r;

	r = drm_gpusvm_range_find_or_insert(&vm->svm.gpusvm, max(addr, xe_vma_start(vma)),
					    xe_vma_start(vma), xe_vma_end(vma), ctx);
	if (IS_ERR(r))
		return ERR_CAST(r);

	return to_xe_range(r);
}

/**
 * xe_svm_range_get_pages() - Get pages for a SVM range
 * @vm: Pointer to the struct xe_vm
 * @range: Pointer to the xe SVM range structure
 * @ctx: GPU SVM context
 *
 * This function gets pages for a SVM range and ensures they are mapped for
 * DMA access. In case of failure with -EOPNOTSUPP, it evicts the range.
 *
 * Return: 0 on success, negative error code on failure.
 */
int xe_svm_range_get_pages(struct xe_vm *vm, struct xe_svm_range *range,
			   struct drm_gpusvm_ctx *ctx)
{
	int err = 0;

	err = drm_gpusvm_range_get_pages(&vm->svm.gpusvm, &range->base, ctx);
	if (err == -EOPNOTSUPP) {
		range_debug(range, "PAGE FAULT - EVICT PAGES");
		drm_gpusvm_range_evict(&vm->svm.gpusvm, &range->base);
	}

	return err;
}

/**
 * xe_svm_ranges_zap_ptes_in_range - clear ptes of svm ranges in input range
 * @vm: Pointer to the xe_vm structure
 * @start: Start of the input range
 * @end: End of the input range
 *
 * This function removes the page table entries (PTEs) associated
 * with the svm ranges within the given input start and end
 *
 * Return: tile_mask for which gt's need to be tlb invalidated.
 */
u8 xe_svm_ranges_zap_ptes_in_range(struct xe_vm *vm, u64 start, u64 end)
{
	struct drm_gpusvm_notifier *notifier;
	struct xe_svm_range *range;
	u64 adj_start, adj_end;
	struct xe_tile *tile;
	u8 tile_mask = 0;
	u8 id;

	lockdep_assert(lockdep_is_held_type(&vm->svm.gpusvm.notifier_lock, 1) &&
		       lockdep_is_held_type(&vm->lock, 0));

	drm_gpusvm_for_each_notifier(notifier, &vm->svm.gpusvm, start, end) {
		struct drm_gpusvm_range *r = NULL;

		adj_start = max(start, drm_gpusvm_notifier_start(notifier));
		adj_end = min(end, drm_gpusvm_notifier_end(notifier));
		drm_gpusvm_for_each_range(r, notifier, adj_start, adj_end) {
			range = to_xe_range(r);
			for_each_tile(tile, vm->xe, id) {
				if (xe_pt_zap_ptes_range(tile, vm, range)) {
					tile_mask |= BIT(id);
					/*
					 * WRITE_ONCE pairs with READ_ONCE in
					 * xe_vm_has_valid_gpu_mapping().
					 * Must not fail after setting
					 * tile_invalidated and before
					 * TLB invalidation.
					 */
					WRITE_ONCE(range->tile_invalidated,
						   range->tile_invalidated | BIT(id));
				}
			}
		}
	}

	return tile_mask;
}

#if IS_ENABLED(CONFIG_DRM_XE_PAGEMAP)

static struct drm_pagemap *tile_local_pagemap(struct xe_tile *tile)
{
	return &tile->mem.vram->dpagemap;
}

/**
 * xe_vma_resolve_pagemap - Resolve the appropriate DRM pagemap for a VMA
 * @vma: Pointer to the xe_vma structure containing memory attributes
 * @tile: Pointer to the xe_tile structure used as fallback for VRAM mapping
 *
 * This function determines the correct DRM pagemap to use for a given VMA.
 * It first checks if a valid devmem_fd is provided in the VMA's preferred
 * location. If the devmem_fd is negative, it returns NULL, indicating no
 * pagemap is available and smem to be used as preferred location.
 * If the devmem_fd is equal to the default faulting
 * GT identifier, it returns the VRAM pagemap associated with the tile.
 *
 * Future support for multi-device configurations may use drm_pagemap_from_fd()
 * to resolve pagemaps from arbitrary file descriptors.
 *
 * Return: A pointer to the resolved drm_pagemap, or NULL if none is applicable.
 */
struct drm_pagemap *xe_vma_resolve_pagemap(struct xe_vma *vma, struct xe_tile *tile)
{
	s32 fd = (s32)vma->attr.preferred_loc.devmem_fd;

	if (fd == DRM_XE_PREFERRED_LOC_DEFAULT_SYSTEM)
		return NULL;

	if (fd == DRM_XE_PREFERRED_LOC_DEFAULT_DEVICE)
		return IS_DGFX(tile_to_xe(tile)) ? tile_local_pagemap(tile) : NULL;

	/* TODO: Support multi-device with drm_pagemap_from_fd(fd) */
	return NULL;
}

/**
 * xe_svm_alloc_vram()- Allocate device memory pages for range,
 * migrating existing data.
 * @tile: tile to allocate vram from
 * @range: SVM range
 * @ctx: DRM GPU SVM context
 *
 * Return: 0 on success, error code on failure.
 */
int xe_svm_alloc_vram(struct xe_tile *tile, struct xe_svm_range *range,
		      const struct drm_gpusvm_ctx *ctx)
{
	struct drm_pagemap *dpagemap;

	xe_assert(tile_to_xe(tile), range->base.pages.flags.migrate_devmem);
	range_debug(range, "ALLOCATE VRAM");

	dpagemap = tile_local_pagemap(tile);
	return drm_pagemap_populate_mm(dpagemap, xe_svm_range_start(range),
				       xe_svm_range_end(range),
				       range->base.gpusvm->mm,
				       ctx->timeslice_ms);
}

static struct drm_pagemap_addr
xe_drm_pagemap_device_map(struct drm_pagemap *dpagemap,
			  struct device *dev,
			  struct page *page,
			  unsigned int order,
			  enum dma_data_direction dir)
{
	struct device *pgmap_dev = dpagemap->dev;
	enum drm_interconnect_protocol prot;
	dma_addr_t addr;

	if (pgmap_dev == dev) {
		addr = xe_vram_region_page_to_dpa(page_to_vr(page), page);
		prot = XE_INTERCONNECT_VRAM;
	} else {
		addr = DMA_MAPPING_ERROR;
		prot = 0;
	}

	return drm_pagemap_addr_encode(addr, prot, order, dir);
}

static const struct drm_pagemap_ops xe_drm_pagemap_ops = {
	.device_map = xe_drm_pagemap_device_map,
	.populate_mm = xe_drm_pagemap_populate_mm,
};

/**
 * xe_devm_add: Remap and provide memmap backing for device memory
 * @tile: tile that the memory region belongs to
 * @vr: vram memory region to remap
 *
 * This remap device memory to host physical address space and create
 * struct page to back device memory
 *
 * Return: 0 on success standard error code otherwise
 */
int xe_devm_add(struct xe_tile *tile, struct xe_vram_region *vr)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct device *dev = &to_pci_dev(xe->drm.dev)->dev;
	struct resource *res;
	void *addr;
	int ret;

	res = devm_request_free_mem_region(dev, &iomem_resource,
					   vr->usable_size);
	if (IS_ERR(res)) {
		ret = PTR_ERR(res);
		return ret;
	}

	vr->pagemap.type = MEMORY_DEVICE_PRIVATE;
	vr->pagemap.range.start = res->start;
	vr->pagemap.range.end = res->end;
	vr->pagemap.nr_range = 1;
	vr->pagemap.ops = drm_pagemap_pagemap_ops_get();
	vr->pagemap.owner = xe_svm_devm_owner(xe);
	addr = devm_memremap_pages(dev, &vr->pagemap);

	vr->dpagemap.dev = dev;
	vr->dpagemap.ops = &xe_drm_pagemap_ops;

	if (IS_ERR(addr)) {
		devm_release_mem_region(dev, res->start, resource_size(res));
		ret = PTR_ERR(addr);
		drm_err(&xe->drm, "Failed to remap tile %d memory, errno %pe\n",
			tile->id, ERR_PTR(ret));
		return ret;
	}
	vr->hpa_base = res->start;

	drm_dbg(&xe->drm, "Added tile %d memory [%llx-%llx] to devm, remapped to %pr\n",
		tile->id, vr->io_start, vr->io_start + vr->usable_size, res);
	return 0;
}
#else
int xe_svm_alloc_vram(struct xe_tile *tile,
		      struct xe_svm_range *range,
		      const struct drm_gpusvm_ctx *ctx)
{
	return -EOPNOTSUPP;
}

int xe_devm_add(struct xe_tile *tile, struct xe_vram_region *vr)
{
	return 0;
}

struct drm_pagemap *xe_vma_resolve_pagemap(struct xe_vma *vma, struct xe_tile *tile)
{
	return NULL;
}
#endif

/**
 * xe_svm_flush() - SVM flush
 * @vm: The VM.
 *
 * Flush all SVM actions.
 */
void xe_svm_flush(struct xe_vm *vm)
{
	if (xe_vm_in_fault_mode(vm))
		flush_work(&vm->svm.garbage_collector.work);
}
