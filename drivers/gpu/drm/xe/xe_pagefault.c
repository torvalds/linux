// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/circ_buf.h>

#include <drm/drm_exec.h>
#include <drm/drm_managed.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt_printk.h"
#include "xe_gt_types.h"
#include "xe_gt_stats.h"
#include "xe_hw_engine.h"
#include "xe_pagefault.h"
#include "xe_pagefault_types.h"
#include "xe_svm.h"
#include "xe_trace_bo.h"
#include "xe_vm.h"

/**
 * DOC: Xe page faults
 *
 * Xe page faults are handled in two layers. The producer layer interacts with
 * hardware or firmware to receive and parse faults into struct xe_pagefault,
 * then forwards them to the consumer. The consumer layer services the faults
 * (e.g., memory migration, page table updates) and acknowledges the result back
 * to the producer, which then forwards the results to the hardware or firmware.
 * The consumer uses a page fault queue sized to absorb all potential faults and
 * a multi-threaded worker to process them. Multiple producers are supported,
 * with a single shared consumer.
 *
 * xe_pagefault.c implements the consumer layer.
 */

static int xe_pagefault_entry_size(void)
{
	/*
	 * Power of two alignment is not a hardware requirement, rather a
	 * software restriction which makes the math for page fault queue
	 * management simplier.
	 */
	return roundup_pow_of_two(sizeof(struct xe_pagefault));
}

static int xe_pagefault_begin(struct drm_exec *exec, struct xe_vma *vma,
			      struct xe_vram_region *vram, bool need_vram_move)
{
	struct xe_bo *bo = xe_vma_bo(vma);
	struct xe_vm *vm = xe_vma_vm(vma);
	int err;

	err = xe_vm_lock_vma(exec, vma);
	if (err)
		return err;

	if (!bo)
		return 0;

	return need_vram_move ? xe_bo_migrate(bo, vram->placement, NULL, exec) :
		xe_bo_validate(bo, vm, true, exec);
}

static int xe_pagefault_handle_vma(struct xe_gt *gt, struct xe_vma *vma,
				   bool atomic)
{
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_validation_ctx ctx;
	struct drm_exec exec;
	struct dma_fence *fence;
	int err, needs_vram;

	lockdep_assert_held_write(&vm->lock);

	needs_vram = xe_vma_need_vram_for_atomic(vm->xe, vma, atomic);
	if (needs_vram < 0 || (needs_vram && xe_vma_is_userptr(vma)))
		return needs_vram < 0 ? needs_vram : -EACCES;

	xe_gt_stats_incr(gt, XE_GT_STATS_ID_VMA_PAGEFAULT_COUNT, 1);
	xe_gt_stats_incr(gt, XE_GT_STATS_ID_VMA_PAGEFAULT_KB,
			 xe_vma_size(vma) / SZ_1K);

	trace_xe_vma_pagefault(vma);

	/* Check if VMA is valid, opportunistic check only */
	if (xe_vm_has_valid_gpu_mapping(tile, vma->tile_present,
					vma->tile_invalidated) && !atomic)
		return 0;

retry_userptr:
	if (xe_vma_is_userptr(vma) &&
	    xe_vma_userptr_check_repin(to_userptr_vma(vma))) {
		struct xe_userptr_vma *uvma = to_userptr_vma(vma);

		err = xe_vma_userptr_pin_pages(uvma);
		if (err)
			return err;
	}

	/* Lock VM and BOs dma-resv */
	xe_validation_ctx_init(&ctx, &vm->xe->val, &exec, (struct xe_val_flags) {});
	drm_exec_until_all_locked(&exec) {
		err = xe_pagefault_begin(&exec, vma, tile->mem.vram,
					 needs_vram == 1);
		drm_exec_retry_on_contention(&exec);
		xe_validation_retry_on_oom(&ctx, &err);
		if (err)
			goto unlock_dma_resv;

		/* Bind VMA only to the GT that has faulted */
		trace_xe_vma_pf_bind(vma);
		xe_vm_set_validation_exec(vm, &exec);
		fence = xe_vma_rebind(vm, vma, BIT(tile->id));
		xe_vm_set_validation_exec(vm, NULL);
		if (IS_ERR(fence)) {
			err = PTR_ERR(fence);
			xe_validation_retry_on_oom(&ctx, &err);
			goto unlock_dma_resv;
		}
	}

	dma_fence_wait(fence, false);
	dma_fence_put(fence);

unlock_dma_resv:
	xe_validation_ctx_fini(&ctx);
	if (err == -EAGAIN)
		goto retry_userptr;

	return err;
}

static bool
xe_pagefault_access_is_atomic(enum xe_pagefault_access_type access_type)
{
	return access_type == XE_PAGEFAULT_ACCESS_TYPE_ATOMIC;
}

static struct xe_vm *xe_pagefault_asid_to_vm(struct xe_device *xe, u32 asid)
{
	struct xe_vm *vm;

	down_read(&xe->usm.lock);
	vm = xa_load(&xe->usm.asid_to_vm, asid);
	if (vm && xe_vm_in_fault_mode(vm))
		xe_vm_get(vm);
	else
		vm = ERR_PTR(-EINVAL);
	up_read(&xe->usm.lock);

	return vm;
}

static int xe_pagefault_service(struct xe_pagefault *pf)
{
	struct xe_gt *gt = pf->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_vm *vm;
	struct xe_vma *vma = NULL;
	int err;
	bool atomic;

	/* Producer flagged this fault to be nacked */
	if (pf->consumer.fault_level == XE_PAGEFAULT_LEVEL_NACK)
		return -EFAULT;

	vm = xe_pagefault_asid_to_vm(xe, pf->consumer.asid);
	if (IS_ERR(vm))
		return PTR_ERR(vm);

	/*
	 * TODO: Change to read lock? Using write lock for simplicity.
	 */
	down_write(&vm->lock);

	if (xe_vm_is_closed(vm)) {
		err = -ENOENT;
		goto unlock_vm;
	}

	vma = xe_vm_find_vma_by_addr(vm, pf->consumer.page_addr);
	if (!vma) {
		err = -EINVAL;
		goto unlock_vm;
	}

	atomic = xe_pagefault_access_is_atomic(pf->consumer.access_type);

	if (xe_vma_is_cpu_addr_mirror(vma))
		err = xe_svm_handle_pagefault(vm, vma, gt,
					      pf->consumer.page_addr, atomic);
	else
		err = xe_pagefault_handle_vma(gt, vma, atomic);

unlock_vm:
	if (!err)
		vm->usm.last_fault_vma = vma;
	up_write(&vm->lock);
	xe_vm_put(vm);

	return err;
}

static bool xe_pagefault_queue_pop(struct xe_pagefault_queue *pf_queue,
				   struct xe_pagefault *pf)
{
	bool found_fault = false;

	spin_lock_irq(&pf_queue->lock);
	if (pf_queue->tail != pf_queue->head) {
		memcpy(pf, pf_queue->data + pf_queue->tail, sizeof(*pf));
		pf_queue->tail = (pf_queue->tail + xe_pagefault_entry_size()) %
			pf_queue->size;
		found_fault = true;
	}
	spin_unlock_irq(&pf_queue->lock);

	return found_fault;
}

static void xe_pagefault_print(struct xe_pagefault *pf)
{
	xe_gt_info(pf->gt, "\n\tASID: %d\n"
		   "\tFaulted Address: 0x%08x%08x\n"
		   "\tFaultType: %d\n"
		   "\tAccessType: %d\n"
		   "\tFaultLevel: %d\n"
		   "\tEngineClass: %d %s\n"
		   "\tEngineInstance: %d\n",
		   pf->consumer.asid,
		   upper_32_bits(pf->consumer.page_addr),
		   lower_32_bits(pf->consumer.page_addr),
		   pf->consumer.fault_type,
		   pf->consumer.access_type,
		   pf->consumer.fault_level,
		   pf->consumer.engine_class,
		   xe_hw_engine_class_to_str(pf->consumer.engine_class),
		   pf->consumer.engine_instance);
}

static void xe_pagefault_queue_work(struct work_struct *w)
{
	struct xe_pagefault_queue *pf_queue =
		container_of(w, typeof(*pf_queue), worker);
	struct xe_pagefault pf;
	unsigned long threshold;

#define USM_QUEUE_MAX_RUNTIME_MS      20
	threshold = jiffies + msecs_to_jiffies(USM_QUEUE_MAX_RUNTIME_MS);

	while (xe_pagefault_queue_pop(pf_queue, &pf)) {
		int err;

		if (!pf.gt)	/* Fault squashed during reset */
			continue;

		err = xe_pagefault_service(&pf);
		if (err) {
			xe_pagefault_print(&pf);
			xe_gt_info(pf.gt, "Fault response: Unsuccessful %pe\n",
				   ERR_PTR(err));
		}

		pf.producer.ops->ack_fault(&pf, err);

		if (time_after(jiffies, threshold)) {
			queue_work(gt_to_xe(pf.gt)->usm.pf_wq, w);
			break;
		}
	}
#undef USM_QUEUE_MAX_RUNTIME_MS
}

static int xe_pagefault_queue_init(struct xe_device *xe,
				   struct xe_pagefault_queue *pf_queue)
{
	struct xe_gt *gt;
	int total_num_eus = 0;
	u8 id;

	for_each_gt(gt, xe, id) {
		xe_dss_mask_t all_dss;
		int num_dss, num_eus;

		bitmap_or(all_dss, gt->fuse_topo.g_dss_mask,
			  gt->fuse_topo.c_dss_mask, XE_MAX_DSS_FUSE_BITS);

		num_dss = bitmap_weight(all_dss, XE_MAX_DSS_FUSE_BITS);
		num_eus = bitmap_weight(gt->fuse_topo.eu_mask_per_dss,
					XE_MAX_EU_FUSE_BITS) * num_dss;

		total_num_eus += num_eus;
	}

	xe_assert(xe, total_num_eus);

	/*
	 * user can issue separate page faults per EU and per CS
	 *
	 * XXX: Multiplier required as compute UMD are getting PF queue errors
	 * without it. Follow on why this multiplier is required.
	 */
#define PF_MULTIPLIER	8
	pf_queue->size = (total_num_eus + XE_NUM_HW_ENGINES) *
		xe_pagefault_entry_size() * PF_MULTIPLIER;
	pf_queue->size = roundup_pow_of_two(pf_queue->size);
#undef PF_MULTIPLIER

	drm_dbg(&xe->drm, "xe_pagefault_entry_size=%d, total_num_eus=%d, pf_queue->size=%u",
		xe_pagefault_entry_size(), total_num_eus, pf_queue->size);

	spin_lock_init(&pf_queue->lock);
	INIT_WORK(&pf_queue->worker, xe_pagefault_queue_work);

	pf_queue->data = drmm_kzalloc(&xe->drm, pf_queue->size, GFP_KERNEL);
	if (!pf_queue->data)
		return -ENOMEM;

	return 0;
}

static void xe_pagefault_fini(void *arg)
{
	struct xe_device *xe = arg;

	destroy_workqueue(xe->usm.pf_wq);
}

/**
 * xe_pagefault_init() - Page fault init
 * @xe: xe device instance
 *
 * Initialize Xe page fault state. Must be done after reading fuses.
 *
 * Return: 0 on Success, errno on failure
 */
int xe_pagefault_init(struct xe_device *xe)
{
	int err, i;

	if (!xe->info.has_usm)
		return 0;

	xe->usm.pf_wq = alloc_workqueue("xe_page_fault_work_queue",
					WQ_UNBOUND | WQ_HIGHPRI,
					XE_PAGEFAULT_QUEUE_COUNT);
	if (!xe->usm.pf_wq)
		return -ENOMEM;

	for (i = 0; i < XE_PAGEFAULT_QUEUE_COUNT; ++i) {
		err = xe_pagefault_queue_init(xe, xe->usm.pf_queue + i);
		if (err)
			goto err_out;
	}

	return devm_add_action_or_reset(xe->drm.dev, xe_pagefault_fini, xe);

err_out:
	destroy_workqueue(xe->usm.pf_wq);
	return err;
}

static void xe_pagefault_queue_reset(struct xe_device *xe, struct xe_gt *gt,
				     struct xe_pagefault_queue *pf_queue)
{
	u32 i;

	/* Driver load failure guard / USM not enabled guard */
	if (!pf_queue->data)
		return;

	/* Squash all pending faults on the GT */

	spin_lock_irq(&pf_queue->lock);
	for (i = pf_queue->tail; i != pf_queue->head;
	     i = (i + xe_pagefault_entry_size()) % pf_queue->size) {
		struct xe_pagefault *pf = pf_queue->data + i;

		if (pf->gt == gt)
			pf->gt = NULL;
	}
	spin_unlock_irq(&pf_queue->lock);
}

/**
 * xe_pagefault_reset() - Page fault reset for a GT
 * @xe: xe device instance
 * @gt: GT being reset
 *
 * Reset the Xe page fault state for a GT; that is, squash any pending faults on
 * the GT.
 */
void xe_pagefault_reset(struct xe_device *xe, struct xe_gt *gt)
{
	int i;

	for (i = 0; i < XE_PAGEFAULT_QUEUE_COUNT; ++i)
		xe_pagefault_queue_reset(xe, gt, xe->usm.pf_queue + i);
}

static bool xe_pagefault_queue_full(struct xe_pagefault_queue *pf_queue)
{
	lockdep_assert_held(&pf_queue->lock);

	return CIRC_SPACE(pf_queue->head, pf_queue->tail, pf_queue->size) <=
		xe_pagefault_entry_size();
}

/**
 * xe_pagefault_handler() - Page fault handler
 * @xe: xe device instance
 * @pf: Page fault
 *
 * Sink the page fault to a queue (i.e., a memory buffer) and queue a worker to
 * service it. Safe to be called from IRQ or process context. Reclaim safe.
 *
 * Return: 0 on success, errno on failure
 */
int xe_pagefault_handler(struct xe_device *xe, struct xe_pagefault *pf)
{
	struct xe_pagefault_queue *pf_queue = xe->usm.pf_queue +
		(pf->consumer.asid % XE_PAGEFAULT_QUEUE_COUNT);
	unsigned long flags;
	bool full;

	spin_lock_irqsave(&pf_queue->lock, flags);
	full = xe_pagefault_queue_full(pf_queue);
	if (!full) {
		memcpy(pf_queue->data + pf_queue->head, pf, sizeof(*pf));
		pf_queue->head = (pf_queue->head + xe_pagefault_entry_size()) %
			pf_queue->size;
		queue_work(xe->usm.pf_wq, &pf_queue->worker);
	} else {
		drm_warn(&xe->drm,
			 "PageFault Queue (%d) full, shouldn't be possible\n",
			 pf->consumer.asid % XE_PAGEFAULT_QUEUE_COUNT);
	}
	spin_unlock_irqrestore(&pf_queue->lock, flags);

	return full ? -ENOSPC : 0;
}
