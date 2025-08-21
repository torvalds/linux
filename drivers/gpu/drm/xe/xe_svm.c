// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <drm/drm_drv.h>

#include "xe_bo.h"
#include "xe_gt_stats.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_migrate.h"
#include "xe_module.h"
#include "xe_pm.h"
#include "xe_pt.h"
#include "xe_svm.h"
#include "xe_tile.h"
#include "xe_ttm_vram_mgr.h"
#include "xe_vm.h"
#include "xe_vm_types.h"

static bool xe_svm_range_in_vram(struct xe_svm_range *range)
{
	/*
	 * Advisory only check whether the range is currently backed by VRAM
	 * memory.
	 */

	struct drm_gpusvm_range_flags flags = {
		/* Pairs with WRITE_ONCE in drm_gpusvm.c */
		.__flags = READ_ONCE(range->base.flags.__flags),
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

#define range_debug(r__, operaton__)					\
	vm_dbg(&range_to_vm(&(r__)->base)->xe->drm,			\
	       "%s: asid=%u, gpusvm=%p, vram=%d,%d, seqno=%lu, " \
	       "start=0x%014lx, end=0x%014lx, size=%lu",		\
	       (operaton__), range_to_vm(&(r__)->base)->usm.asid,	\
	       (r__)->base.gpusvm,					\
	       xe_svm_range_in_vram((r__)) ? 1 : 0,			\
	       xe_svm_range_has_vram_binding((r__)) ? 1 : 0,		\
	       (r__)->base.notifier_seq,				\
	       xe_svm_range_start((r__)), xe_svm_range_end((r__)),	\
	       xe_svm_range_size((r__)))

void xe_svm_range_debug(struct xe_svm_range *range, const char *operation)
{
	range_debug(range, operation);
}

static void *xe_svm_devm_owner(struct xe_device *xe)
{
	return xe;
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
	if (range->base.flags.unmapped || !range->tile_present)
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
			tile_mask |= BIT(id);
			/*
			 * WRITE_ONCE pairs with READ_ONCE in
			 * xe_vm_has_valid_gpu_mapping()
			 */
			WRITE_ONCE(range->tile_invalidated,
				   range->tile_invalidated | BIT(id));
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

static void xe_svm_invalidate(struct drm_gpusvm *gpusvm,
			      struct drm_gpusvm_notifier *notifier,
			      const struct mmu_notifier_range *mmu_range)
{
	struct xe_vm *vm = gpusvm_to_vm(gpusvm);
	struct xe_device *xe = vm->xe;
	struct drm_gpusvm_range *r, *first;
	u64 adj_start = mmu_range->start, adj_end = mmu_range->end;
	u8 tile_mask = 0;
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

	err = xe_vm_range_tilemask_tlb_invalidation(vm, adj_start, adj_end, tile_mask);
	WARN_ON_ONCE(err);

range_notifier_event_end:
	r = first;
	drm_gpusvm_for_each_range(r, notifier, adj_start, adj_end)
		xe_svm_range_notifier_event_end(vm, r, mmu_range);
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

static int xe_svm_garbage_collector(struct xe_vm *vm)
{
	struct xe_svm_range *range;
	int err;

	lockdep_assert_held_write(&vm->lock);

	if (xe_vm_is_closed_or_banned(vm))
		return -ENOENT;

	spin_lock(&vm->svm.garbage_collector.lock);
	for (;;) {
		range = list_first_entry_or_null(&vm->svm.garbage_collector.range_list,
						 typeof(*range),
						 garbage_collector_link);
		if (!range)
			break;

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

		spin_lock(&vm->svm.garbage_collector.lock);
	}
	spin_unlock(&vm->svm.garbage_collector.lock);

	return 0;
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

static struct xe_tile *vr_to_tile(struct xe_vram_region *vr)
{
	return container_of(vr, struct xe_tile, mem.vram);
}

static u64 xe_vram_region_page_to_dpa(struct xe_vram_region *vr,
				      struct page *page)
{
	u64 dpa;
	struct xe_tile *tile = vr_to_tile(vr);
	u64 pfn = page_to_pfn(page);
	u64 offset;

	xe_tile_assert(tile, is_device_private_page(page));
	xe_tile_assert(tile, (pfn << PAGE_SHIFT) >= vr->hpa_base);

	offset = (pfn << PAGE_SHIFT) - vr->hpa_base;
	dpa = vr->dpa_base + offset;

	return dpa;
}

enum xe_svm_copy_dir {
	XE_SVM_COPY_TO_VRAM,
	XE_SVM_COPY_TO_SRAM,
};

static int xe_svm_copy(struct page **pages, dma_addr_t *dma_addr,
		       unsigned long npages, const enum xe_svm_copy_dir dir)
{
	struct xe_vram_region *vr = NULL;
	struct xe_tile *tile;
	struct dma_fence *fence = NULL;
	unsigned long i;
#define XE_VRAM_ADDR_INVALID	~0x0ull
	u64 vram_addr = XE_VRAM_ADDR_INVALID;
	int err = 0, pos = 0;
	bool sram = dir == XE_SVM_COPY_TO_SRAM;

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
		if (!dma_addr[i] && vram_addr == XE_VRAM_ADDR_INVALID)
			continue;

		if (!vr && spage) {
			vr = page_to_vr(spage);
			tile = vr_to_tile(vr);
		}
		XE_WARN_ON(spage && page_to_vr(spage) != vr);

		/*
		 * CPU page and device page valid, capture physical address on
		 * first device page, check if physical contiguous on subsequent
		 * device pages.
		 */
		if (dma_addr[i] && spage) {
			__vram_addr = xe_vram_region_page_to_dpa(vr, spage);
			if (vram_addr == XE_VRAM_ADDR_INVALID) {
				vram_addr = __vram_addr;
				pos = i;
			}

			match = vram_addr + PAGE_SIZE * (i - pos) == __vram_addr;
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
				if (sram) {
					vm_dbg(&tile->xe->drm,
					       "COPY TO SRAM - 0x%016llx -> 0x%016llx, NPAGES=%ld",
					       vram_addr, (u64)dma_addr[pos], i - pos + incr);
					__fence = xe_migrate_from_vram(tile->migrate,
								       i - pos + incr,
								       vram_addr,
								       dma_addr + pos);
				} else {
					vm_dbg(&tile->xe->drm,
					       "COPY TO VRAM - 0x%016llx -> 0x%016llx, NPAGES=%ld",
					       (u64)dma_addr[pos], vram_addr, i - pos + incr);
					__fence = xe_migrate_to_vram(tile->migrate,
								     i - pos + incr,
								     dma_addr + pos,
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
			if (dma_addr[i] && spage) {
				vram_addr = __vram_addr;
				pos = i;
			} else {
				vram_addr = XE_VRAM_ADDR_INVALID;
			}

			/* Extra mismatched device page, copy it */
			if (!match && last && vram_addr != XE_VRAM_ADDR_INVALID) {
				if (sram) {
					vm_dbg(&tile->xe->drm,
					       "COPY TO SRAM - 0x%016llx -> 0x%016llx, NPAGES=%d",
					       vram_addr, (u64)dma_addr[pos], 1);
					__fence = xe_migrate_from_vram(tile->migrate, 1,
								       vram_addr,
								       dma_addr + pos);
				} else {
					vm_dbg(&tile->xe->drm,
					       "COPY TO VRAM - 0x%016llx -> 0x%016llx, NPAGES=%d",
					       (u64)dma_addr[pos], vram_addr, 1);
					__fence = xe_migrate_to_vram(tile->migrate, 1,
								     dma_addr + pos,
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

	return err;
#undef XE_MIGRATE_CHUNK_SIZE
#undef XE_VRAM_ADDR_INVALID
}

static int xe_svm_copy_to_devmem(struct page **pages, dma_addr_t *dma_addr,
				 unsigned long npages)
{
	return xe_svm_copy(pages, dma_addr, npages, XE_SVM_COPY_TO_VRAM);
}

static int xe_svm_copy_to_ram(struct page **pages, dma_addr_t *dma_addr,
			      unsigned long npages)
{
	return xe_svm_copy(pages, dma_addr, npages, XE_SVM_COPY_TO_SRAM);
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

static struct drm_buddy *tile_to_buddy(struct xe_tile *tile)
{
	return &tile->mem.vram.ttm.mm;
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
		struct xe_tile *tile = vr_to_tile(vr);
		struct drm_buddy *buddy = tile_to_buddy(tile);
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

	spin_lock_init(&vm->svm.garbage_collector.lock);
	INIT_LIST_HEAD(&vm->svm.garbage_collector.range_list);
	INIT_WORK(&vm->svm.garbage_collector.work,
		  xe_svm_garbage_collector_work_func);

	err = drm_gpusvm_init(&vm->svm.gpusvm, "Xe SVM", &vm->xe->drm,
			      current->mm, xe_svm_devm_owner(vm->xe), 0,
			      vm->size, xe_modparam.svm_notifier_size * SZ_1M,
			      &gpusvm_ops, fault_chunk_sizes,
			      ARRAY_SIZE(fault_chunk_sizes));
	if (err)
		return err;

	drm_gpusvm_driver_set_lock(&vm->svm.gpusvm, &vm->lock);

	return 0;
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
	       (devmem_preferred == range->base.flags.has_devmem_pages);

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
static struct xe_vram_region *tile_to_vr(struct xe_tile *tile)
{
	return &tile->mem.vram;
}

static int xe_drm_pagemap_populate_mm(struct drm_pagemap *dpagemap,
				      unsigned long start, unsigned long end,
				      struct mm_struct *mm,
				      unsigned long timeslice_ms)
{
	struct xe_tile *tile = container_of(dpagemap, typeof(*tile), mem.vram.dpagemap);
	struct xe_device *xe = tile_to_xe(tile);
	struct device *dev = xe->drm.dev;
	struct xe_vram_region *vr = tile_to_vr(tile);
	struct drm_buddy_block *block;
	struct list_head *blocks;
	struct xe_bo *bo;
	ktime_t time_end = 0;
	int err, idx;

	if (!drm_dev_enter(&xe->drm, &idx))
		return -ENODEV;

	xe_pm_runtime_get(xe);

 retry:
	bo = xe_bo_create_locked(tile_to_xe(tile), NULL, NULL, end - start,
				 ttm_bo_type_device,
				 XE_BO_FLAG_VRAM_IF_DGFX(tile) |
				 XE_BO_FLAG_CPU_ADDR_MIRROR);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		if (xe_vm_validate_should_retry(NULL, err, &time_end))
			goto retry;
		goto out_pm_put;
	}

	drm_pagemap_devmem_init(&bo->devmem_allocation, dev, mm,
				&dpagemap_devmem_ops,
				&tile->mem.vram.dpagemap,
				end - start);

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

out_pm_put:
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

	if (!range->base.flags.migrate_devmem || !preferred_region_is_vram)
		return false;

	xe_assert(vm->xe, IS_DGFX(vm->xe));

	if (preferred_region_is_vram && xe_svm_range_in_vram(range)) {
		drm_info(&vm->xe->drm, "Range is already in VRAM\n");
		return false;
	}

	if (preferred_region_is_vram && range_size < SZ_64K && !supports_4K_migration(vm->xe)) {
		drm_dbg(&vm->xe->drm, "Platform doesn't support SZ_4K range migration\n");
		return false;
	}

	return true;
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
	struct drm_gpusvm_ctx ctx = {
		.read_only = xe_vma_read_only(vma),
		.devmem_possible = IS_DGFX(vm->xe) &&
			IS_ENABLED(CONFIG_DRM_XE_PAGEMAP),
		.check_pages_threshold = IS_DGFX(vm->xe) &&
			IS_ENABLED(CONFIG_DRM_XE_PAGEMAP) ? SZ_64K : 0,
		.devmem_only = atomic && IS_DGFX(vm->xe) &&
			IS_ENABLED(CONFIG_DRM_XE_PAGEMAP),
		.timeslice_ms = atomic && IS_DGFX(vm->xe) &&
			IS_ENABLED(CONFIG_DRM_XE_PAGEMAP) ?
			vm->xe->atomic_svm_timeslice_ms : 0,
	};
	struct xe_svm_range *range;
	struct dma_fence *fence;
	struct xe_tile *tile = gt_to_tile(gt);
	int migrate_try_count = ctx.devmem_only ? 3 : 1;
	ktime_t end = 0;
	int err;

	lockdep_assert_held_write(&vm->lock);
	xe_assert(vm->xe, xe_vma_is_cpu_addr_mirror(vma));

	xe_gt_stats_incr(gt, XE_GT_STATS_ID_SVM_PAGEFAULT_COUNT, 1);

retry:
	/* Always process UNMAPs first so view SVM ranges is current */
	err = xe_svm_garbage_collector(vm);
	if (err)
		return err;

	range = xe_svm_range_find_or_insert(vm, fault_addr, vma, &ctx);

	if (IS_ERR(range))
		return PTR_ERR(range);

	if (ctx.devmem_only && !range->base.flags.migrate_devmem)
		return -EACCES;

	if (xe_svm_range_is_valid(range, tile, ctx.devmem_only))
		return 0;

	range_debug(range, "PAGE FAULT");

	if (--migrate_try_count >= 0 &&
	    xe_svm_range_needs_migrate_to_vram(range, vma, IS_DGFX(vm->xe))) {
		err = xe_svm_alloc_vram(tile, range, &ctx);
		ctx.timeslice_ms <<= 1;	/* Double timeslice if we have to retry */
		if (err) {
			if (migrate_try_count || !ctx.devmem_only) {
				drm_dbg(&vm->xe->drm,
					"VRAM allocation failed, falling back to retrying fault, asid=%u, errno=%pe\n",
					vm->usm.asid, ERR_PTR(err));
				goto retry;
			} else {
				drm_err(&vm->xe->drm,
					"VRAM allocation failed, retry count exceeded, asid=%u, errno=%pe\n",
					vm->usm.asid, ERR_PTR(err));
				return err;
			}
		}
	}

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
		goto err_out;
	}

	range_debug(range, "PAGE FAULT - BIND");

retry_bind:
	xe_vm_lock(vm, false);
	fence = xe_vm_range_rebind(vm, vma, range, BIT(tile->id));
	if (IS_ERR(fence)) {
		xe_vm_unlock(vm);
		err = PTR_ERR(fence);
		if (err == -EAGAIN) {
			ctx.timeslice_ms <<= 1;	/* Double timeslice if we have to retry */
			range_debug(range, "PAGE FAULT - RETRY BIND");
			goto retry;
		}
		if (xe_vm_validate_should_retry(NULL, err, &end))
			goto retry_bind;
		goto err_out;
	}
	xe_vm_unlock(vm);

	dma_fence_wait(fence, false);
	dma_fence_put(fence);

err_out:

	return err;
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
		return ERR_PTR(PTR_ERR(r));

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

#if IS_ENABLED(CONFIG_DRM_XE_PAGEMAP)

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

	xe_assert(tile_to_xe(tile), range->base.flags.migrate_devmem);
	range_debug(range, "ALLOCATE VRAM");

	dpagemap = xe_tile_local_pagemap(tile);
	return drm_pagemap_populate_mm(dpagemap, xe_svm_range_start(range),
				       xe_svm_range_end(range),
				       range->base.gpusvm->mm,
				       ctx->timeslice_ms);
}

static struct drm_pagemap_device_addr
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

	return drm_pagemap_device_addr_encode(addr, prot, order, dir);
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
