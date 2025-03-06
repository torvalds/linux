// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "xe_gt_tlb_invalidation.h"
#include "xe_pt.h"
#include "xe_svm.h"
#include "xe_vm.h"
#include "xe_vm_types.h"

static struct xe_vm *gpusvm_to_vm(struct drm_gpusvm *gpusvm)
{
	return container_of(gpusvm, struct xe_vm, svm.gpusvm);
}

static struct xe_vm *range_to_vm(struct drm_gpusvm_range *r)
{
	return gpusvm_to_vm(r->gpusvm);
}

static unsigned long xe_svm_range_start(struct xe_svm_range *range)
{
	return drm_gpusvm_range_start(&range->base);
}

static unsigned long xe_svm_range_end(struct xe_svm_range *range)
{
	return drm_gpusvm_range_end(&range->base);
}

static struct drm_gpusvm_range *
xe_svm_range_alloc(struct drm_gpusvm *gpusvm)
{
	struct xe_svm_range *range;

	range = kzalloc(sizeof(*range), GFP_KERNEL);
	if (!range)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&range->garbage_collector_link);
	xe_vm_get(gpusvm_to_vm(gpusvm));

	return &range->base;
}

static void xe_svm_range_free(struct drm_gpusvm_range *range)
{
	xe_vm_put(range_to_vm(range));
	kfree(range);
}

static struct xe_svm_range *to_xe_range(struct drm_gpusvm_range *r)
{
	return container_of(r, struct xe_svm_range, base);
}

static void
xe_svm_garbage_collector_add_range(struct xe_vm *vm, struct xe_svm_range *range,
				   const struct mmu_notifier_range *mmu_range)
{
	struct xe_device *xe = vm->xe;

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

	/* Skip if already unmapped or if no binding exist */
	if (range->base.flags.unmapped || !range->tile_present)
		return 0;

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
			range->tile_invalidated |= BIT(id);
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
	struct xe_tile *tile;
	struct drm_gpusvm_range *r, *first;
	struct xe_gt_tlb_invalidation_fence
		fence[XE_MAX_TILES_PER_DEVICE * XE_MAX_GT_PER_TILE];
	u64 adj_start = mmu_range->start, adj_end = mmu_range->end;
	u8 tile_mask = 0;
	u8 id;
	u32 fence_id = 0;
	long err;

	xe_svm_assert_in_notifier(vm);

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

	for_each_tile(tile, xe, id) {
		if (tile_mask & BIT(id)) {
			int err;

			xe_gt_tlb_invalidation_fence_init(tile->primary_gt,
							  &fence[fence_id], true);

			err = xe_gt_tlb_invalidation_range(tile->primary_gt,
							   &fence[fence_id],
							   adj_start,
							   adj_end,
							   vm->usm.asid);
			if (WARN_ON_ONCE(err < 0))
				goto wait;
			++fence_id;

			if (!tile->media_gt)
				continue;

			xe_gt_tlb_invalidation_fence_init(tile->media_gt,
							  &fence[fence_id], true);

			err = xe_gt_tlb_invalidation_range(tile->media_gt,
							   &fence[fence_id],
							   adj_start,
							   adj_end,
							   vm->usm.asid);
			if (WARN_ON_ONCE(err < 0))
				goto wait;
			++fence_id;
		}
	}

wait:
	for (id = 0; id < fence_id; ++id)
		xe_gt_tlb_invalidation_fence_wait(&fence[id]);

range_notifier_event_end:
	r = first;
	drm_gpusvm_for_each_range(r, notifier, adj_start, adj_end)
		xe_svm_range_notifier_event_end(vm, r, mmu_range);
}

static int __xe_svm_garbage_collector(struct xe_vm *vm,
				      struct xe_svm_range *range)
{
	/* TODO: Do unbind */

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
			      current->mm, NULL, 0, vm->size,
			      SZ_512M, &gpusvm_ops, fault_chunk_sizes,
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
				  struct xe_tile *tile)
{
	return (range->tile_present & ~range->tile_invalidated) & BIT(tile->id);
}

/**
 * xe_svm_handle_pagefault() - SVM handle page fault
 * @vm: The VM.
 * @vma: The CPU address mirror VMA.
 * @tile: The tile upon the fault occurred.
 * @fault_addr: The GPU fault address.
 * @atomic: The fault atomic access bit.
 *
 * Create GPU bindings for a SVM page fault.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_svm_handle_pagefault(struct xe_vm *vm, struct xe_vma *vma,
			    struct xe_tile *tile, u64 fault_addr,
			    bool atomic)
{
	struct drm_gpusvm_ctx ctx = { .read_only = xe_vma_read_only(vma), };
	struct xe_svm_range *range;
	struct drm_gpusvm_range *r;
	struct drm_exec exec;
	struct dma_fence *fence;
	ktime_t end = 0;
	int err;

	lockdep_assert_held_write(&vm->lock);
	xe_assert(vm->xe, xe_vma_is_cpu_addr_mirror(vma));

retry:
	/* Always process UNMAPs first so view SVM ranges is current */
	err = xe_svm_garbage_collector(vm);
	if (err)
		return err;

	r = drm_gpusvm_range_find_or_insert(&vm->svm.gpusvm, fault_addr,
					    xe_vma_start(vma), xe_vma_end(vma),
					    &ctx);
	if (IS_ERR(r))
		return PTR_ERR(r);

	range = to_xe_range(r);
	if (xe_svm_range_is_valid(range, tile))
		return 0;

	err = drm_gpusvm_range_get_pages(&vm->svm.gpusvm, r, &ctx);
	if (err == -EFAULT || err == -EPERM)	/* Corner where CPU mappings have changed */
		goto retry;
	if (err)
		goto err_out;

retry_bind:
	drm_exec_init(&exec, 0, 0);
	drm_exec_until_all_locked(&exec) {
		err = drm_exec_lock_obj(&exec, vm->gpuvm.r_obj);
		drm_exec_retry_on_contention(&exec);
		if (err) {
			drm_exec_fini(&exec);
			goto err_out;
		}

		fence = xe_vm_range_rebind(vm, vma, range, BIT(tile->id));
		if (IS_ERR(fence)) {
			drm_exec_fini(&exec);
			err = PTR_ERR(fence);
			if (err == -EAGAIN)
				goto retry;
			if (xe_vm_validate_should_retry(&exec, err, &end))
				goto retry_bind;
			goto err_out;
		}
	}
	drm_exec_fini(&exec);

	dma_fence_wait(fence, false);
	dma_fence_put(fence);

err_out:

	return err;
}
