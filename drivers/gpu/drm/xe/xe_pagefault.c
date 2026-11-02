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
#include "xe_log.h"
#include "xe_migrate.h"
#include "xe_pagefault.h"
#include "xe_pagefault_types.h"
#include "xe_pm.h"
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

/**
 * DOC: Xe page fault cache
 *
 * Some Xe hardware can trigger “fault storms,” which are many page faults to
 * the same address within a short period of time. An example is many EU threads
 * faulting on the same page simultaneously. With the current page fault locking
 * structure, only one page fault for a given address range can be processed at
 * a time. This causes head-of-queue blocking across workers, killing
 * parallelism. If the page fault handler must repeatedly look up resources
 * (VMAs, ranges) to determine that the pages are valid for each fault in the
 * storm, the time complexity grows rapidly.
 *
 * To address this, each page fault worker maintains a cache of the active fault
 * being processed. Subsequent faults that hit in the cache are chained to the
 * pending fault, and all chained faults are acknowledged once the initial fault
 * completes. This alleviates head-of-queue blocking and quickly chains faults
 * in the upper layers, avoiding expensive lookups in the main fault-handling
 * path.
 *
 * Faults are buffered in the page fault queue in a way that provides stable
 * storage for outstanding faults. In particular, faults may be chained directly
 * while still resident in the queue storage (i.e., outside the worker’s current
 * head/tail dequeue position). This allows the IRQ handler to match newly
 * arrived faults against the per-worker cache and immediately chain cache hits
 * onto the active fault under the queue lock, without allocating memory or
 * waiting for the worker to pop the fault first.
 *
 * A per-fault state field is used to assert correctness of these invariants.
 * The state tracks whether an entry is free, queued, chained, or currently
 * active. Transitions are performed under the page fault queue lock, and the
 * worker acknowledges faults by walking the chain and returning entries to the
 * free state once they are complete.
 */

/**
 * enum xe_pagefault_alloc_state - lifetime state for a page fault queue entry
 * @XE_PAGEFAULT_ALLOC_STATE_FREE:
 *	Entry is unused and may be overwritten by the producer, consumer retry
 *	or requeue..
 * @XE_PAGEFAULT_ALLOC_STATE_QUEUED:
 *	Entry has been enqueued and may be dequeued by a worker.
 * @XE_PAGEFAULT_ALLOC_STATE_ACTIVE:
 *	Entry has been dequeued and is the worker's currently serviced fault.
 *	The worker may attach additional faults to it via consumer.next.
 * @XE_PAGEFAULT_ALLOC_STATE_CHAINED:
 *	Entry is not independently serviced; it has been chained onto an
 *	ACTIVE entry via consumer.next and will be acknowledged when the
 *	leading fault completes.
 * @XE_PAGEFAULT_ALLOC_STATE_COUNT:
 *	Count of allocation states.
 *
 * The page fault queue provides stable storage for outstanding faults so the
 * IRQ handler can chain new cache hits directly onto a worker's active fault.
 * Because entries may remain referenced outside the consumer dequeue window,
 * the producer must only write into entries in the FREE state.
 *
 * State transitions are protected by the page fault queue lock. Workers return
 * entries to FREE after acknowledging the fault (either as ACTIVE or CHAINED).
 */
enum xe_pagefault_alloc_state {
	XE_PAGEFAULT_ALLOC_STATE_FREE		= 0,
	XE_PAGEFAULT_ALLOC_STATE_QUEUED		= 1,
	XE_PAGEFAULT_ALLOC_STATE_CHAINED	= 2,
	XE_PAGEFAULT_ALLOC_STATE_ACTIVE		= 3,
	XE_PAGEFAULT_ALLOC_STATE_COUNT		= 4,
};

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

	/*
	 * Skip validate/migrate for DONTNEED/purged BOs - repopulating
	 * their pages would prevent the shrinker from reclaiming them.
	 * For non-scratch VMs there is no safe fallback so fail the fault.
	 * For scratch VMs let xe_vma_rebind() run normally; it will install
	 * scratch PTEs so the GPU gets safe zero reads instead of faulting.
	 */
	if (unlikely(xe_bo_madv_is_dontneed(bo) || xe_bo_is_purged(bo))) {
		if (!xe_vm_has_scratch(vm))
			return -EACCES;
		return 0;
	}

	return need_vram_move ? xe_bo_migrate(bo, vram->placement, NULL, exec) :
		xe_bo_validate(bo, vm, true, exec);
}

static int xe_pagefault_handle_vma(struct xe_gt *gt, struct xe_vma *vma,
				   struct xe_pagefault *pf, bool atomic)
{
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_validation_ctx ctx;
	struct drm_exec exec;
	struct dma_fence *fence;
	int err = 0, needs_vram;

	lockdep_assert_held(&vm->lock);

	needs_vram = xe_vma_need_vram_for_atomic(vm->xe, vma, atomic);
	if (needs_vram < 0) {
		xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_VMA_NEEDS_VRAM_CHECK);
		return needs_vram;
	}
	if (needs_vram && xe_vma_is_userptr(vma)) {
		xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_VMA_ATOMIC_USERPTR);
		return -EACCES;
	}

	xe_gt_stats_incr(gt, XE_GT_STATS_ID_VMA_PAGEFAULT_COUNT, 1);
	xe_gt_stats_incr(gt, XE_GT_STATS_ID_VMA_PAGEFAULT_KB,
			 xe_vma_size(vma) / SZ_1K);

	trace_xe_vma_pagefault(vma);

	guard(mutex)(&vma->fault_lock);

	/* Check if VMA is valid, opportunistic check only */
	if (xe_vm_has_valid_gpu_mapping(tile, vma->tile_present,
					vma->tile_invalidated) && !atomic) {
		xe_pagefault_set_start_addr(pf, xe_vma_start(vma));
		xe_pagefault_set_end_addr(pf, xe_vma_end(vma));
		return 0;
	}

	do {
		xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_NONE);

		if (xe_vma_is_userptr(vma) &&
		    xe_vma_userptr_check_repin(to_userptr_vma(vma))) {
			struct xe_userptr_vma *uvma = to_userptr_vma(vma);

			err = xe_vma_userptr_pin_pages(uvma);
			if (err) {
				xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_VMA_USERPTR_PIN);
				return err;
			}
		}

		/* Lock VM and BOs dma-resv */
		xe_validation_ctx_init(&ctx, &vm->xe->val, &exec,
				       (struct xe_val_flags) {});
		drm_exec_until_all_locked(&exec) {
			err = xe_pagefault_begin(&exec, vma, tile->mem.vram,
						 needs_vram == 1);
			drm_exec_retry_on_contention(&exec);
			xe_validation_retry_on_oom(&ctx, &err);
			if (err) {
				xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_VMA_VALIDATE);
				break;
			}

			/* Bind VMA only to the GT that has faulted */
			trace_xe_vma_pf_bind(vma);
			xe_vm_set_validation_exec(vm, &exec);
			fence = xe_vma_rebind(vm, vma, BIT(tile->id));
			xe_vm_set_validation_exec(vm, NULL);
			if (IS_ERR(fence)) {
				err = PTR_ERR(fence);
				xe_validation_retry_on_oom(&ctx, &err);
				xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_VMA_REBIND);
				break;
			}
		}
		xe_validation_ctx_fini(&ctx);
	} while (err == -EAGAIN);

	if (!err) {
		/* Give hint to immediately ack faults */
		xe_pagefault_set_start_addr(pf, xe_vma_start(vma));
		xe_pagefault_set_end_addr(pf, xe_vma_end(vma));

		dma_fence_wait(fence, false);
		dma_fence_put(fence);
	}

	return err;
}

static bool
xe_pagefault_access_is_atomic(enum xe_pagefault_access_type access_type)
{
	return (access_type & XE_PAGEFAULT_ACCESS_TYPE_MASK) == XE_PAGEFAULT_ACCESS_TYPE_ATOMIC;
}

static struct xe_vm *xe_pagefault_asid_to_vm(struct xe_pagefault *pf, u32 asid)
{
	struct xe_device *xe = gt_to_xe(pf->gt);
	struct xe_vm *vm;

	down_read(&xe->usm.lock);
	vm = xa_load(&xe->usm.asid_to_vm, asid);
	if (!vm) {
		xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_VM_NOT_FOUND);
		vm = ERR_PTR(-EINVAL);
	} else if (!xe_vm_in_fault_mode(vm)) {
		xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_VM_NOT_IN_FAULT_MODE);
		vm = ERR_PTR(-EINVAL);
	} else {
		xe_vm_get(vm);
	}
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
	u32 asid = FIELD_GET(XE_PAGEFAULT_ASID_MASK, pf->consumer.id);

	/* Producer flagged this fault to be nacked */
	if (pf->consumer.fault_type_level == XE_PAGEFAULT_TYPE_LEVEL_NACK)
		return -EFAULT;

	vm = xe_pagefault_asid_to_vm(pf, asid);
	if (IS_ERR(vm)) {
		err = PTR_ERR(vm);
		trace_xe_pagefault_fail(xe, NULL, NULL, asid,
					xe_pagefault_addr(pf),
					xe_pagefault_get_error(pf), err);
		return err;
	}

	xe_migrate_ulls_enter(gt_to_tile(gt)->migrate);

	down_read(&vm->lock);

	if (xe_vm_is_closed(vm)) {
		err = -ENOENT;
		xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_VM_CLOSED);
		goto unlock_vm;
	}

	vma = xe_vm_find_vma_by_addr(vm, xe_pagefault_addr(pf));
	if (!vma) {
		err = -EINVAL;
		xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_VMA_NOT_FOUND);
		goto unlock_vm;
	}

	if (xe_vma_read_only(vma) &&
	    pf->consumer.access_type != XE_PAGEFAULT_ACCESS_TYPE_READ) {
		err = -EPERM;
		xe_pagefault_set_error(pf, XE_PAGEFAULT_ERROR_READ_ONLY_VIOLATION);
		goto unlock_vm;
	}

	atomic = xe_pagefault_access_is_atomic(pf->consumer.access_type);

	if (xe_vma_is_cpu_addr_mirror(vma))
		err = xe_svm_handle_pagefault(vm, vma, pf, gt,
					      xe_pagefault_addr(pf), atomic);
	else
		err = xe_pagefault_handle_vma(gt, vma, pf, atomic);

unlock_vm:
	if (err)
		trace_xe_pagefault_fail(xe, vm, vma, asid,
					xe_pagefault_addr(pf),
					xe_pagefault_get_error(pf), err);

	up_read(&vm->lock);
	xe_vm_put(vm);

	return err;
}

#define XE_PAGEFAULT_CACHE_START_INVALID	U64_MAX
#define xe_pagefault_cache_start_invalidate(val)	\
	(val = XE_PAGEFAULT_CACHE_START_INVALID)

static void
xe_pagefault_cache_invalidate(struct xe_pagefault_queue *pf_queue,
			      struct xe_pagefault_work *pf_work)
{
	lockdep_assert_held(&pf_queue->lock);

	xe_pagefault_cache_start_invalidate(pf_work->cache.start);
}

static bool xe_pagefault_queue_full(struct xe_pagefault_queue *pf_queue)
{
	lockdep_assert_held(&pf_queue->lock);

	return CIRC_SPACE(pf_queue->head, pf_queue->tail,
			  pf_queue->size) <= xe_pagefault_entry_size();
}

static struct xe_pagefault *
xe_pagefault_queue_add(struct xe_pagefault_queue *pf_queue,
		       struct xe_pagefault *pf)
{
	struct xe_device *xe = container_of(pf_queue, typeof(*xe),
					    usm.pf_queue);
	struct xe_pagefault *lpf;

	lockdep_assert_held(&pf_queue->lock);

	do {
		/* Not possible, warn on and drop page fault */
		if (WARN_ON_ONCE(xe_pagefault_queue_full(pf_queue))) {
			xe_log_err(xe, PAGEFAULT, -ENOSPC, "Queue full!\n");
			return NULL;
		}

		lpf = (pf_queue->data + pf_queue->head);
		pf_queue->head = (pf_queue->head + xe_pagefault_entry_size()) %
			pf_queue->size;
	} while (lpf->consumer.alloc_state != XE_PAGEFAULT_ALLOC_STATE_FREE);

	xe_assert(xe, lpf != pf);
	memcpy(lpf, pf, sizeof(*pf));
	lpf->consumer.alloc_state = XE_PAGEFAULT_ALLOC_STATE_QUEUED;

	return lpf;
}

static struct xe_pagefault *
xe_pagefault_queue_unchain_requeue(struct xe_pagefault_queue *pf_queue,
				   struct xe_pagefault *pf, struct xe_gt *gt)
{
	struct xe_device *xe = container_of(pf_queue, typeof(*xe),
					    usm.pf_queue);
	struct xe_pagefault *next = pf->consumer.next, *lpf;

	lockdep_assert_held(&pf_queue->lock);
	xe_assert(xe, pf->consumer.alloc_state ==
		  XE_PAGEFAULT_ALLOC_STATE_CHAINED);

	xe_gt_stats_incr(gt, XE_GT_STATS_ID_CHAIN_MISMATCH_PAGEFAULT_COUNT, 1);

	pf->consumer.alloc_state = XE_PAGEFAULT_ALLOC_STATE_FREE;
	lpf = xe_pagefault_queue_add(pf_queue, pf);
	if (lpf) {
		lpf->consumer.next = NULL;
		lpf->consumer.fault_type_level |= XE_PAGEFAULT_REQUEUE_MASK;
	}

	return next;
}

static bool xe_pagefault_match(struct xe_pagefault *pf, u64 start,
			       u64 end, u64 cache_asid)
{
	struct xe_device *xe = gt_to_xe(pf->gt);
	u64 page_addr = xe_pagefault_addr(pf);
	u32 pf_asid = FIELD_GET(XE_PAGEFAULT_ASID_MASK, pf->consumer.id);

	xe_assert(xe, pf->consumer.alloc_state !=
		  XE_PAGEFAULT_ALLOC_STATE_FREE);

	return page_addr >= start && page_addr < end &&
		pf_asid == cache_asid;
}

static bool xe_pagefault_try_chain(struct xe_pagefault_queue *pf_queue,
				   struct xe_pagefault *pf)
{
	struct xe_device *xe = container_of(pf_queue, typeof(*xe),
					    usm.pf_queue);
	struct xe_pagefault_work *pf_work;
	bool requeue = FIELD_GET(XE_PAGEFAULT_REQUEUE_MASK,
				 pf->consumer.fault_type_level);
	int i;

	lockdep_assert_held(&pf_queue->lock);
	xe_assert(xe, pf->consumer.alloc_state ==
		  XE_PAGEFAULT_ALLOC_STATE_QUEUED);

	/*
	 * If this is a retry, we may already have a chain attached. In that
	 * case, we cannot hit in the cache because chains cannot easily be
	 * combined.
	 */
	if (pf->consumer.next)
		return false;

	for (i = 0, pf_work = xe->usm.pf_workers;
	     i < xe->info.num_pf_work; ++i, ++pf_work) {
		u64 start = pf_work->cache.start;
		u64 end = requeue ? start + SZ_4K : pf_work->cache.end;
		u32 asid = pf_work->cache.asid;

		if (xe_pagefault_match(pf, start, end, asid)) {
			xe_assert(xe, pf_work->cache.pf->consumer.alloc_state ==
				  XE_PAGEFAULT_ALLOC_STATE_ACTIVE);

			if (pf->producer.private !=
			    pf_work->cache.pf->producer.private)
				continue;

			xe_gt_stats_incr(pf->gt,
					 XE_GT_STATS_ID_CHAIN_PAGEFAULT_COUNT,
					 1);

			pf->consumer.alloc_state =
				XE_PAGEFAULT_ALLOC_STATE_CHAINED;
			pf->consumer.next = pf_work->cache.pf->consumer.next;
			pf_work->cache.pf->consumer.next = pf;

			return true;
		}
	}

	return false;
}

static void xe_pagefault_queue_advance(struct xe_pagefault_queue *pf_queue)
{
	lockdep_assert_held(&pf_queue->lock);

	pf_queue->tail = (pf_queue->tail + xe_pagefault_entry_size()) %
		pf_queue->size;
}

static struct xe_pagefault *
xe_pagefault_queue_tail_fault(struct xe_pagefault_queue *pf_queue)
{
	lockdep_assert_held(&pf_queue->lock);

	return pf_queue->data + pf_queue->tail;
}

static bool xe_pagefault_queue_empty(struct xe_pagefault_queue *pf_queue)
{
	lockdep_assert_held(&pf_queue->lock);

	return pf_queue->head == pf_queue->tail;
}

static bool xe_pagefault_queue_pop(struct xe_pagefault_queue *pf_queue,
				   struct xe_pagefault **pf, int id)
{
	struct xe_device *xe = container_of(pf_queue, typeof(*xe),
					    usm.pf_queue);
	struct xe_pagefault_work *pf_work, *__pf_work;
	struct xe_pagefault *lpf;
	size_t align = SZ_2M;
	int i;

	guard(spinlock_irq)(&pf_queue->lock);

	for (*pf = NULL; !*pf;) {
		if (xe_pagefault_queue_empty(pf_queue))
			return false;

		lpf = xe_pagefault_queue_tail_fault(pf_queue);
		xe_pagefault_queue_advance(pf_queue);

		if (lpf->consumer.alloc_state !=
		    XE_PAGEFAULT_ALLOC_STATE_QUEUED)
			continue;

		if (xe_pagefault_try_chain(pf_queue, lpf))
			continue;

		*pf = lpf;	/* Hand back page fault for processing */
	}

	/*
	 * No cache hit; allocate a new cache entry. We assume most faults
	 * within a 2M range will hit the same pages. If this assumption proves
	 * false, the mismatched fault is requeued after the initial fault is
	 * acknowledged.
	 */
	pf_work = xe->usm.pf_workers + id;
	if (FIELD_GET(XE_PAGEFAULT_REQUEUE_MASK,
		      lpf->consumer.fault_type_level))
		align = SZ_4K;
	pf_work->cache.start = ALIGN_DOWN(xe_pagefault_addr(lpf), align);
	pf_work->cache.end = pf_work->cache.start + align;
	pf_work->cache.asid = FIELD_GET(XE_PAGEFAULT_ASID_MASK, lpf->consumer.id);
	pf_work->cache.pf = lpf;
	lpf->consumer.alloc_state = XE_PAGEFAULT_ALLOC_STATE_ACTIVE;

	for (i = 0, __pf_work = xe->usm.pf_workers;
	     i < xe->info.num_pf_work; ++i, ++__pf_work) {
		u64 cache_start = __pf_work->cache.start;

		if (__pf_work == pf_work)
			continue;

		if (cache_start != XE_PAGEFAULT_CACHE_START_INVALID) {
			xe_gt_stats_incr(xe_root_mmio_gt(xe),
					 XE_GT_STATS_ID_PARALLEL_PAGEFAULT_COUNT,
					 1);
			break;
		}
	}

	/* Drain queue until empty or new fault found */
	while (1) {
		if (xe_pagefault_queue_empty(pf_queue))
			break;

		lpf = xe_pagefault_queue_tail_fault(pf_queue);

		if (lpf->consumer.alloc_state !=
		    XE_PAGEFAULT_ALLOC_STATE_QUEUED) {
			xe_pagefault_queue_advance(pf_queue);
			continue;
		}

		if (!xe_pagefault_try_chain(pf_queue, lpf))
			break;

		xe_pagefault_queue_advance(pf_queue);
	}

	return true;
}

static const char *xe_pagefault_error_to_str(enum xe_pagefault_error error)
{
	switch (error) {
	case XE_PAGEFAULT_ERROR_NONE:
		return "NONE";
	case XE_PAGEFAULT_ERROR_VM_NOT_FOUND:
		return "VM_NOT_FOUND";
	case XE_PAGEFAULT_ERROR_VM_NOT_IN_FAULT_MODE:
		return "VM_NOT_IN_FAULT_MODE";
	case XE_PAGEFAULT_ERROR_VM_CLOSED:
		return "VM_CLOSED";
	case XE_PAGEFAULT_ERROR_VMA_NOT_FOUND:
		return "VMA_NOT_FOUND";
	case XE_PAGEFAULT_ERROR_READ_ONLY_VIOLATION:
		return "READ_ONLY_VIOLATION";
	case XE_PAGEFAULT_ERROR_VMA_NEEDS_VRAM_CHECK:
		return "VMA_NEEDS_VRAM_CHECK";
	case XE_PAGEFAULT_ERROR_VMA_ATOMIC_USERPTR:
		return "VMA_ATOMIC_USERPTR";
	case XE_PAGEFAULT_ERROR_VMA_USERPTR_PIN:
		return "VMA_USERPTR_PIN";
	case XE_PAGEFAULT_ERROR_VMA_VALIDATE:
		return "VMA_VALIDATE";
	case XE_PAGEFAULT_ERROR_VMA_REBIND:
		return "VMA_REBIND";
	case XE_PAGEFAULT_ERROR_SVM_GARBAGE_COLLECTOR:
		return "SVM_GARBAGE_COLLECTOR";
	case XE_PAGEFAULT_ERROR_SVM_RANGE_NOT_FOUND:
		return "SVM_RANGE_NOT_FOUND";
	case XE_PAGEFAULT_ERROR_SVM_REBIND:
		return "SVM_REBIND";
	case XE_PAGEFAULT_ERROR_SVM_NEEDS_VRAM_CHECK:
		return "SVM_NEEDS_VRAM_CHECK";
	case XE_PAGEFAULT_ERROR_SVM_VMA_NOT_FOUND:
		return "SVM_VMA_NOT_FOUND";
	case XE_PAGEFAULT_ERROR_SVM_SERVICE_FAILED:
		return "SVM_SERVICE_FAILED";
	default:
		return "UNKNOWN";
	}
}

static void xe_pagefault_print(struct xe_pagefault *pf)
{
	enum xe_pagefault_error err = xe_pagefault_get_error(pf);

	if (WARN_ON_ONCE(!pf->producer.ops->print))
		return;

	pf->producer.ops->print(pf, xe_pagefault_error_to_str(err));
}

static void xe_pagefault_save_to_vm(struct xe_device *xe, struct xe_pagefault *pf)
{
	struct xe_vm *vm;

	/*
	 * Pagefault may be asociated to VM that is not in fault mode.
	 * Perform asid_to_vm behavior, except if VM is not in fault
	 * mode, return VM anyways.
	 */
	down_read(&xe->usm.lock);
	vm = xa_load(&xe->usm.asid_to_vm,
		     FIELD_GET(XE_PAGEFAULT_ASID_MASK, pf->consumer.id));
	if (vm)
		xe_vm_get(vm);
	else
		vm = ERR_PTR(-EINVAL);
	up_read(&xe->usm.lock);

	if (IS_ERR(vm))
		return;

	xe_vm_add_fault_entry_pf(vm, pf);

	xe_vm_put(vm);
}

static void xe_pagefault_queue_work(struct work_struct *w)
{
	struct xe_pagefault_work *pf_work =
		container_of(w, typeof(*pf_work), work);
	struct xe_device *xe = pf_work->xe;
	struct xe_pagefault_queue *pf_queue = &xe->usm.pf_queue;
	struct xe_pagefault *pf;
	ktime_t start = xe_gt_stats_ktime_get();
	unsigned long threshold;
	u64 cache_start = XE_PAGEFAULT_CACHE_START_INVALID, cache_end = 0;
	u32 cache_asid = 0;

	/*
	 * A live VM holds a PM reference, but a torn-down VM does not.
	 * Guard the entire worker loop to safely drain stale faults and
	 * prevent autosuspends from desyncing batched CT flushes.
	 */
	guard(xe_pm_runtime)(xe);

	/*
	 * A live VM holds a PM reference, but a torn-down VM does not.
	 * Guard the entire worker loop to safely drain stale faults and
	 * prevent autosuspends from desyncing batched CT flushes.
	 */
	guard(xe_pm_runtime)(xe);

#define USM_QUEUE_MAX_RUNTIME_MS      20
	threshold = jiffies + msecs_to_jiffies(USM_QUEUE_MAX_RUNTIME_MS);

	while (xe_pagefault_queue_pop(pf_queue, &pf, pf_work->id)) {
		const struct xe_pagefault_ops *ops = pf->producer.ops;
		void *private = pf->producer.private;
		struct xe_gt *gt = pf->gt;
		u32 asid = FIELD_GET(XE_PAGEFAULT_ASID_MASK, pf->consumer.id);
		int err = 0;
		bool invalidated = false;

		/* Last fault same address, ack immediately */
		if (xe_pagefault_match(pf, cache_start, cache_end, cache_asid)) {
			xe_gt_stats_incr(gt, XE_GT_STATS_ID_LAST_PAGEFAULT_COUNT, 1);
			goto ack_fault;
		}

		err = xe_pagefault_service(pf);

		if (err) {
			if (!(pf->consumer.access_type & XE_PAGEFAULT_ACCESS_PREFETCH)) {
				xe_pagefault_save_to_vm(gt_to_xe(gt), pf);
				xe_pagefault_cache_start_invalidate(cache_start);
				xe_pagefault_print(pf);
				xe_log_err_info(pf->gt, PAGEFAULT, err, "Unsuccessful response\n");
			} else {
				xe_gt_stats_incr(pf->gt, XE_GT_STATS_ID_INVALID_PREFETCH_PAGEFAULT_COUNT, 1);
				xe_gt_dbg(pf->gt, "Prefetch Fault response: Unsuccessful %pe\n",
					  ERR_PTR(err));
			}
		} else {
			/* Cache valid fault locally */
			cache_start = xe_pagefault_start_addr(pf);
			cache_end = xe_pagefault_end_addr(pf);
			cache_asid = asid;
		}

ack_fault:
		xe_assert(xe, pf->consumer.alloc_state ==
			  XE_PAGEFAULT_ALLOC_STATE_ACTIVE);
		xe_assert(xe, pf == pf_work->cache.pf);

		ops->ack_fault_begin(private);
		while (pf) {
			xe_assert(xe, pf->consumer.alloc_state ==
				  XE_PAGEFAULT_ALLOC_STATE_ACTIVE);
			xe_assert(xe, ops == pf->producer.ops);
			xe_assert(xe, gt == pf->gt);

			ops->ack_fault(pf, err);

			spin_lock_irq(&pf_queue->lock);

			if (!invalidated) {
				invalidated = true;
				xe_pagefault_cache_invalidate(pf_queue,
							      pf_work);
			}

			pf->consumer.alloc_state = XE_PAGEFAULT_ALLOC_STATE_FREE;
			pf = pf->consumer.next;

			/*
			 * Requeue chained faults which do not match the last
			 * fault processed
			 */
			while (pf && !xe_pagefault_match(pf, cache_start,
							 cache_end, cache_asid))
				pf = xe_pagefault_queue_unchain_requeue(pf_queue, pf, gt);

			/* Ensure resets are safe */
			if (pf)
				pf->consumer.alloc_state =
					XE_PAGEFAULT_ALLOC_STATE_ACTIVE;

			spin_unlock_irq(&pf_queue->lock);
		}
		ops->ack_fault_end(private);

		if (time_after(jiffies, threshold)) {
			queue_work(xe->usm.pagefault_wq, w);
			break;
		}
	}
#undef USM_QUEUE_MAX_RUNTIME_MS

	xe_gt_stats_incr(xe_root_mmio_gt(xe), XE_GT_STATS_ID_PAGEFAULT_US,
			 xe_gt_stats_ktime_us_delta(start));
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

		num_dss = bitmap_weighted_or(all_dss, gt->fuse_topo.g_dss_mask,
			  gt->fuse_topo.c_dss_mask, XE_MAX_DSS_FUSE_BITS);

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

	pf_queue->xe = xe;
	spin_lock_init(&pf_queue->lock);

	pf_queue->data = drmm_kzalloc(&xe->drm, pf_queue->size, GFP_KERNEL);
	if (!pf_queue->data)
		return -ENOMEM;

	return 0;
}

static void xe_pagefault_fini(void *arg)
{
	struct xe_device *xe = arg;

	destroy_workqueue(xe->usm.prefetch_wq);
	destroy_workqueue(xe->usm.pagefault_wq);
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

	xe->usm.pagefault_wq = alloc_workqueue("xe_page_fault_work_queue",
					       WQ_UNBOUND | WQ_HIGHPRI,
					       xe->info.num_pf_work);
	if (!xe->usm.pagefault_wq)
		return -ENOMEM;

	xe->usm.prefetch_wq = alloc_workqueue("xe_prefetch_work_queue",
					      WQ_UNBOUND,
					      xe->info.num_pf_work);
	if (!xe->usm.prefetch_wq) {
		err = -ENOMEM;
		goto err_pagefault_wq;
	}

	err = xe_pagefault_queue_init(xe, &xe->usm.pf_queue);
	if (err)
		goto err_out;

	for (i = 0; i < xe->info.num_pf_work; ++i) {
		struct xe_pagefault_work *pf_work = xe->usm.pf_workers + i;

		pf_work->xe = xe;
		pf_work->id = i;
		xe_pagefault_cache_start_invalidate(pf_work->cache.start);
		INIT_WORK(&pf_work->work, xe_pagefault_queue_work);
	}

	return devm_add_action_or_reset(xe->drm.dev, xe_pagefault_fini, xe);

err_out:
	destroy_workqueue(xe->usm.prefetch_wq);
err_pagefault_wq:
	destroy_workqueue(xe->usm.pagefault_wq);
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

	guard(spinlock_irq)(&pf_queue->lock);

	for (i = 0; i < pf_queue->size; i += xe_pagefault_entry_size()) {
		struct xe_pagefault *pf = pf_queue->data + i;
		bool active = pf->consumer.alloc_state ==
			XE_PAGEFAULT_ALLOC_STATE_ACTIVE;

		if (pf->gt != gt || active) {
			if (active)
				pf->consumer.next = NULL;
			continue;
		}

		pf->consumer.alloc_state =
			XE_PAGEFAULT_ALLOC_STATE_FREE;
		pf->consumer.next = NULL;
	}
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
	xe_pagefault_queue_reset(xe, gt, &xe->usm.pf_queue);
}

/*
 * This function can race with multiple page fault producers, but worst case we
 * stick a page fault on the same queue for consumption.
 */
static int xe_pagefault_work_index(struct xe_device *xe)
{
	lockdep_assert_held(&xe->usm.pf_queue.lock);

	return xe->usm.current_pf_work++ % xe->info.num_pf_work;
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
	struct xe_pagefault_queue *pf_queue = &xe->usm.pf_queue;
	struct xe_pagefault *lpf;
	bool empty;

	guard(spinlock_irqsave)(&pf_queue->lock);

	empty = xe_pagefault_queue_empty(pf_queue);
	lpf = xe_pagefault_queue_add(pf_queue, pf);
	if (!lpf)
		return -ENOSPC;

	lpf->consumer.next = NULL;
	if (xe_pagefault_try_chain(pf_queue, lpf)) {
		xe_gt_stats_incr(pf->gt, XE_GT_STATS_ID_CHAIN_IRQ_PAGEFAULT_COUNT, 1);
		if (empty) {
			xe_gt_stats_incr(pf->gt,
					 XE_GT_STATS_ID_CHAIN_DRAIN_IRQ_PAGEFAULT_COUNT, 1);
			xe_pagefault_queue_advance(pf_queue);
		}
	} else {
		int work_index = xe_pagefault_work_index(xe);

		queue_work(xe->usm.pagefault_wq,
			   &xe->usm.pf_workers[work_index].work);
	}

	return 0;
}

/**
 * xe_pagefault_print_info() - dump page fault queue/cache debug information
 * @xe: Xe device
 * @p: DRM printer to emit output to
 *
 * Print a snapshot of the page fault queue state for debugging. The output
 * includes queue parameters (entry size, total size, head/tail), a histogram
 * of per-entry allocation state values, and the validity of each per-worker
 * page fault cache.
 *
 * This function is intended for debugfs and similar diagnostics. It acquires
 * the page fault queue spinlock internally to serialize against IRQ-side
 * producers and the worker consumer path, so callers must not hold the queue
 * lock.
 */
void xe_pagefault_print_info(struct xe_device *xe, struct drm_printer *p)
{
	struct xe_pagefault_queue *pf_queue = &xe->usm.pf_queue;
	struct xe_pagefault_work *pf_work;
	static const char * const alloc_state_names[] = {
		[XE_PAGEFAULT_ALLOC_STATE_FREE] = "free",
		[XE_PAGEFAULT_ALLOC_STATE_QUEUED] = "queued",
		[XE_PAGEFAULT_ALLOC_STATE_CHAINED] = "chained",
		[XE_PAGEFAULT_ALLOC_STATE_ACTIVE] = "active",
	};
	u32 i, counts[XE_PAGEFAULT_ALLOC_STATE_COUNT] = {};

	/* Driver load failure guard / USM not enabled guard */
	if (!pf_queue->data)
		return;

	guard(spinlock_irq)(&pf_queue->lock);

	drm_printf(p, "pagefault size: %u\n", xe_pagefault_entry_size());
	drm_printf(p, "pagefault queue size: %u\n", pf_queue->size);
	drm_printf(p, "pagefault queue head: %u\n", pf_queue->head);
	drm_printf(p, "pagefault queue tail: %u\n", pf_queue->tail);

	for (i = 0; i < pf_queue->size; i += xe_pagefault_entry_size()) {
		struct xe_pagefault *pf = pf_queue->data + i;

		if (pf->consumer.alloc_state >=
		    XE_PAGEFAULT_ALLOC_STATE_COUNT) {
			drm_printf(p, "pagefault[%u] corrupted alloc_state=%u\n",
				   i, pf->consumer.alloc_state);
			continue;
		}

		counts[pf->consumer.alloc_state]++;
	}

	for (i = 0; i < XE_PAGEFAULT_ALLOC_STATE_COUNT; ++i)
		drm_printf(p, "pagefault queue %s count: %u\n",
			   alloc_state_names[i], counts[i]);

	for (i = 0, pf_work = xe->usm.pf_workers;
	     i < xe->info.num_pf_work; ++i, ++pf_work) {
		if (pf_work->cache.start == XE_PAGEFAULT_CACHE_START_INVALID)
			drm_printf(p, "pagefault work[%u] cache invalid\n", i);
		else
			drm_printf(p, "pagefault work[%u] cache valid\n", i);
	}
}
