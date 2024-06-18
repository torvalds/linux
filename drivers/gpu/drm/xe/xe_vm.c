// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_vm.h"

#include <linux/dma-fence-array.h>
#include <linux/nospec.h>

#include <drm/drm_exec.h>
#include <drm/drm_print.h>
#include <drm/ttm/ttm_execbuf_util.h>
#include <drm/ttm/ttm_tt.h>
#include <drm/xe_drm.h>
#include <linux/ascii85.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/swap.h>

#include <generated/xe_wa_oob.h>

#include "regs/xe_gtt_defs.h"
#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_drm_client.h"
#include "xe_exec_queue.h"
#include "xe_gt_pagefault.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_migrate.h"
#include "xe_pat.h"
#include "xe_pm.h"
#include "xe_preempt_fence.h"
#include "xe_pt.h"
#include "xe_res_cursor.h"
#include "xe_sync.h"
#include "xe_trace.h"
#include "xe_wa.h"
#include "xe_hmm.h"

static struct drm_gem_object *xe_vm_obj(struct xe_vm *vm)
{
	return vm->gpuvm.r_obj;
}

/**
 * xe_vma_userptr_check_repin() - Advisory check for repin needed
 * @uvma: The userptr vma
 *
 * Check if the userptr vma has been invalidated since last successful
 * repin. The check is advisory only and can the function can be called
 * without the vm->userptr.notifier_lock held. There is no guarantee that the
 * vma userptr will remain valid after a lockless check, so typically
 * the call needs to be followed by a proper check under the notifier_lock.
 *
 * Return: 0 if userptr vma is valid, -EAGAIN otherwise; repin recommended.
 */
int xe_vma_userptr_check_repin(struct xe_userptr_vma *uvma)
{
	return mmu_interval_check_retry(&uvma->userptr.notifier,
					uvma->userptr.notifier_seq) ?
		-EAGAIN : 0;
}

int xe_vma_userptr_pin_pages(struct xe_userptr_vma *uvma)
{
	struct xe_vma *vma = &uvma->vma;
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_device *xe = vm->xe;

	lockdep_assert_held(&vm->lock);
	xe_assert(xe, xe_vma_is_userptr(vma));

	return xe_hmm_userptr_populate_range(uvma, false);
}

static bool preempt_fences_waiting(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link) {
		if (!q->compute.pfence ||
		    (q->compute.pfence && test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
						   &q->compute.pfence->flags))) {
			return true;
		}
	}

	return false;
}

static void free_preempt_fences(struct list_head *list)
{
	struct list_head *link, *next;

	list_for_each_safe(link, next, list)
		xe_preempt_fence_free(to_preempt_fence_from_link(link));
}

static int alloc_preempt_fences(struct xe_vm *vm, struct list_head *list,
				unsigned int *count)
{
	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	if (*count >= vm->preempt.num_exec_queues)
		return 0;

	for (; *count < vm->preempt.num_exec_queues; ++(*count)) {
		struct xe_preempt_fence *pfence = xe_preempt_fence_alloc();

		if (IS_ERR(pfence))
			return PTR_ERR(pfence);

		list_move_tail(xe_preempt_fence_link(pfence), list);
	}

	return 0;
}

static int wait_for_existing_preempt_fences(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	xe_vm_assert_held(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link) {
		if (q->compute.pfence) {
			long timeout = dma_fence_wait(q->compute.pfence, false);

			if (timeout < 0)
				return -ETIME;
			dma_fence_put(q->compute.pfence);
			q->compute.pfence = NULL;
		}
	}

	return 0;
}

static bool xe_vm_is_idle(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	xe_vm_assert_held(vm);
	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link) {
		if (!xe_exec_queue_is_idle(q))
			return false;
	}

	return true;
}

static void arm_preempt_fences(struct xe_vm *vm, struct list_head *list)
{
	struct list_head *link;
	struct xe_exec_queue *q;

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link) {
		struct dma_fence *fence;

		link = list->next;
		xe_assert(vm->xe, link != list);

		fence = xe_preempt_fence_arm(to_preempt_fence_from_link(link),
					     q, q->compute.context,
					     ++q->compute.seqno);
		dma_fence_put(q->compute.pfence);
		q->compute.pfence = fence;
	}
}

static int add_preempt_fences(struct xe_vm *vm, struct xe_bo *bo)
{
	struct xe_exec_queue *q;
	int err;

	if (!vm->preempt.num_exec_queues)
		return 0;

	err = xe_bo_lock(bo, true);
	if (err)
		return err;

	err = dma_resv_reserve_fences(bo->ttm.base.resv, vm->preempt.num_exec_queues);
	if (err)
		goto out_unlock;

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link)
		if (q->compute.pfence) {
			dma_resv_add_fence(bo->ttm.base.resv,
					   q->compute.pfence,
					   DMA_RESV_USAGE_BOOKKEEP);
		}

out_unlock:
	xe_bo_unlock(bo);
	return err;
}

static void resume_and_reinstall_preempt_fences(struct xe_vm *vm,
						struct drm_exec *exec)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link) {
		q->ops->resume(q);

		drm_gpuvm_resv_add_fence(&vm->gpuvm, exec, q->compute.pfence,
					 DMA_RESV_USAGE_BOOKKEEP, DMA_RESV_USAGE_BOOKKEEP);
	}
}

int xe_vm_add_compute_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q)
{
	struct drm_gpuvm_exec vm_exec = {
		.vm = &vm->gpuvm,
		.flags = DRM_EXEC_INTERRUPTIBLE_WAIT,
		.num_fences = 1,
	};
	struct drm_exec *exec = &vm_exec.exec;
	struct dma_fence *pfence;
	int err;
	bool wait;

	xe_assert(vm->xe, xe_vm_in_preempt_fence_mode(vm));

	down_write(&vm->lock);
	err = drm_gpuvm_exec_lock(&vm_exec);
	if (err)
		goto out_up_write;

	pfence = xe_preempt_fence_create(q, q->compute.context,
					 ++q->compute.seqno);
	if (!pfence) {
		err = -ENOMEM;
		goto out_fini;
	}

	list_add(&q->compute.link, &vm->preempt.exec_queues);
	++vm->preempt.num_exec_queues;
	q->compute.pfence = pfence;

	down_read(&vm->userptr.notifier_lock);

	drm_gpuvm_resv_add_fence(&vm->gpuvm, exec, pfence,
				 DMA_RESV_USAGE_BOOKKEEP, DMA_RESV_USAGE_BOOKKEEP);

	/*
	 * Check to see if a preemption on VM is in flight or userptr
	 * invalidation, if so trigger this preempt fence to sync state with
	 * other preempt fences on the VM.
	 */
	wait = __xe_vm_userptr_needs_repin(vm) || preempt_fences_waiting(vm);
	if (wait)
		dma_fence_enable_sw_signaling(pfence);

	up_read(&vm->userptr.notifier_lock);

out_fini:
	drm_exec_fini(exec);
out_up_write:
	up_write(&vm->lock);

	return err;
}

/**
 * xe_vm_remove_compute_exec_queue() - Remove compute exec queue from VM
 * @vm: The VM.
 * @q: The exec_queue
 */
void xe_vm_remove_compute_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q)
{
	if (!xe_vm_in_preempt_fence_mode(vm))
		return;

	down_write(&vm->lock);
	list_del(&q->compute.link);
	--vm->preempt.num_exec_queues;
	if (q->compute.pfence) {
		dma_fence_enable_sw_signaling(q->compute.pfence);
		dma_fence_put(q->compute.pfence);
		q->compute.pfence = NULL;
	}
	up_write(&vm->lock);
}

/**
 * __xe_vm_userptr_needs_repin() - Check whether the VM does have userptrs
 * that need repinning.
 * @vm: The VM.
 *
 * This function checks for whether the VM has userptrs that need repinning,
 * and provides a release-type barrier on the userptr.notifier_lock after
 * checking.
 *
 * Return: 0 if there are no userptrs needing repinning, -EAGAIN if there are.
 */
int __xe_vm_userptr_needs_repin(struct xe_vm *vm)
{
	lockdep_assert_held_read(&vm->userptr.notifier_lock);

	return (list_empty(&vm->userptr.repin_list) &&
		list_empty(&vm->userptr.invalidated)) ? 0 : -EAGAIN;
}

#define XE_VM_REBIND_RETRY_TIMEOUT_MS 1000

static void xe_vm_kill(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);

	xe_vm_lock(vm, false);
	vm->flags |= XE_VM_FLAG_BANNED;
	trace_xe_vm_kill(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link)
		q->ops->kill(q);
	xe_vm_unlock(vm);

	/* TODO: Inform user the VM is banned */
}

/**
 * xe_vm_validate_should_retry() - Whether to retry after a validate error.
 * @exec: The drm_exec object used for locking before validation.
 * @err: The error returned from ttm_bo_validate().
 * @end: A ktime_t cookie that should be set to 0 before first use and
 * that should be reused on subsequent calls.
 *
 * With multiple active VMs, under memory pressure, it is possible that
 * ttm_bo_validate() run into -EDEADLK and in such case returns -ENOMEM.
 * Until ttm properly handles locking in such scenarios, best thing the
 * driver can do is retry with a timeout. Check if that is necessary, and
 * if so unlock the drm_exec's objects while keeping the ticket to prepare
 * for a rerun.
 *
 * Return: true if a retry after drm_exec_init() is recommended;
 * false otherwise.
 */
bool xe_vm_validate_should_retry(struct drm_exec *exec, int err, ktime_t *end)
{
	ktime_t cur;

	if (err != -ENOMEM)
		return false;

	cur = ktime_get();
	*end = *end ? : ktime_add_ms(cur, XE_VM_REBIND_RETRY_TIMEOUT_MS);
	if (!ktime_before(cur, *end))
		return false;

	msleep(20);
	return true;
}

static int xe_gpuvm_validate(struct drm_gpuvm_bo *vm_bo, struct drm_exec *exec)
{
	struct xe_vm *vm = gpuvm_to_vm(vm_bo->vm);
	struct drm_gpuva *gpuva;
	int ret;

	lockdep_assert_held(&vm->lock);
	drm_gpuvm_bo_for_each_va(gpuva, vm_bo)
		list_move_tail(&gpuva_to_vma(gpuva)->combined_links.rebind,
			       &vm->rebind_list);

	ret = xe_bo_validate(gem_to_xe_bo(vm_bo->obj), vm, false);
	if (ret)
		return ret;

	vm_bo->evicted = false;
	return 0;
}

/**
 * xe_vm_validate_rebind() - Validate buffer objects and rebind vmas
 * @vm: The vm for which we are rebinding.
 * @exec: The struct drm_exec with the locked GEM objects.
 * @num_fences: The number of fences to reserve for the operation, not
 * including rebinds and validations.
 *
 * Validates all evicted gem objects and rebinds their vmas. Note that
 * rebindings may cause evictions and hence the validation-rebind
 * sequence is rerun until there are no more objects to validate.
 *
 * Return: 0 on success, negative error code on error. In particular,
 * may return -EINTR or -ERESTARTSYS if interrupted, and -EDEADLK if
 * the drm_exec transaction needs to be restarted.
 */
int xe_vm_validate_rebind(struct xe_vm *vm, struct drm_exec *exec,
			  unsigned int num_fences)
{
	struct drm_gem_object *obj;
	unsigned long index;
	int ret;

	do {
		ret = drm_gpuvm_validate(&vm->gpuvm, exec);
		if (ret)
			return ret;

		ret = xe_vm_rebind(vm, false);
		if (ret)
			return ret;
	} while (!list_empty(&vm->gpuvm.evict.list));

	drm_exec_for_each_locked_object(exec, index, obj) {
		ret = dma_resv_reserve_fences(obj->resv, num_fences);
		if (ret)
			return ret;
	}

	return 0;
}

static int xe_preempt_work_begin(struct drm_exec *exec, struct xe_vm *vm,
				 bool *done)
{
	int err;

	err = drm_gpuvm_prepare_vm(&vm->gpuvm, exec, 0);
	if (err)
		return err;

	if (xe_vm_is_idle(vm)) {
		vm->preempt.rebind_deactivated = true;
		*done = true;
		return 0;
	}

	if (!preempt_fences_waiting(vm)) {
		*done = true;
		return 0;
	}

	err = drm_gpuvm_prepare_objects(&vm->gpuvm, exec, 0);
	if (err)
		return err;

	err = wait_for_existing_preempt_fences(vm);
	if (err)
		return err;

	/*
	 * Add validation and rebinding to the locking loop since both can
	 * cause evictions which may require blocing dma_resv locks.
	 * The fence reservation here is intended for the new preempt fences
	 * we attach at the end of the rebind work.
	 */
	return xe_vm_validate_rebind(vm, exec, vm->preempt.num_exec_queues);
}

static void preempt_rebind_work_func(struct work_struct *w)
{
	struct xe_vm *vm = container_of(w, struct xe_vm, preempt.rebind_work);
	struct drm_exec exec;
	unsigned int fence_count = 0;
	LIST_HEAD(preempt_fences);
	ktime_t end = 0;
	int err = 0;
	long wait;
	int __maybe_unused tries = 0;

	xe_assert(vm->xe, xe_vm_in_preempt_fence_mode(vm));
	trace_xe_vm_rebind_worker_enter(vm);

	down_write(&vm->lock);

	if (xe_vm_is_closed_or_banned(vm)) {
		up_write(&vm->lock);
		trace_xe_vm_rebind_worker_exit(vm);
		return;
	}

retry:
	if (xe_vm_userptr_check_repin(vm)) {
		err = xe_vm_userptr_pin(vm);
		if (err)
			goto out_unlock_outer;
	}

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 0);

	drm_exec_until_all_locked(&exec) {
		bool done = false;

		err = xe_preempt_work_begin(&exec, vm, &done);
		drm_exec_retry_on_contention(&exec);
		if (err || done) {
			drm_exec_fini(&exec);
			if (err && xe_vm_validate_should_retry(&exec, err, &end))
				err = -EAGAIN;

			goto out_unlock_outer;
		}
	}

	err = alloc_preempt_fences(vm, &preempt_fences, &fence_count);
	if (err)
		goto out_unlock;

	err = xe_vm_rebind(vm, true);
	if (err)
		goto out_unlock;

	/* Wait on rebinds and munmap style VM unbinds */
	wait = dma_resv_wait_timeout(xe_vm_resv(vm),
				     DMA_RESV_USAGE_KERNEL,
				     false, MAX_SCHEDULE_TIMEOUT);
	if (wait <= 0) {
		err = -ETIME;
		goto out_unlock;
	}

#define retry_required(__tries, __vm) \
	(IS_ENABLED(CONFIG_DRM_XE_USERPTR_INVAL_INJECT) ? \
	(!(__tries)++ || __xe_vm_userptr_needs_repin(__vm)) : \
	__xe_vm_userptr_needs_repin(__vm))

	down_read(&vm->userptr.notifier_lock);
	if (retry_required(tries, vm)) {
		up_read(&vm->userptr.notifier_lock);
		err = -EAGAIN;
		goto out_unlock;
	}

#undef retry_required

	spin_lock(&vm->xe->ttm.lru_lock);
	ttm_lru_bulk_move_tail(&vm->lru_bulk_move);
	spin_unlock(&vm->xe->ttm.lru_lock);

	/* Point of no return. */
	arm_preempt_fences(vm, &preempt_fences);
	resume_and_reinstall_preempt_fences(vm, &exec);
	up_read(&vm->userptr.notifier_lock);

out_unlock:
	drm_exec_fini(&exec);
out_unlock_outer:
	if (err == -EAGAIN) {
		trace_xe_vm_rebind_worker_retry(vm);
		goto retry;
	}

	if (err) {
		drm_warn(&vm->xe->drm, "VM worker error: %d\n", err);
		xe_vm_kill(vm);
	}
	up_write(&vm->lock);

	free_preempt_fences(&preempt_fences);

	trace_xe_vm_rebind_worker_exit(vm);
}

static bool vma_userptr_invalidate(struct mmu_interval_notifier *mni,
				   const struct mmu_notifier_range *range,
				   unsigned long cur_seq)
{
	struct xe_userptr *userptr = container_of(mni, typeof(*userptr), notifier);
	struct xe_userptr_vma *uvma = container_of(userptr, typeof(*uvma), userptr);
	struct xe_vma *vma = &uvma->vma;
	struct xe_vm *vm = xe_vma_vm(vma);
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	long err;

	xe_assert(vm->xe, xe_vma_is_userptr(vma));
	trace_xe_vma_userptr_invalidate(vma);

	if (!mmu_notifier_range_blockable(range))
		return false;

	vm_dbg(&xe_vma_vm(vma)->xe->drm,
	       "NOTIFIER: addr=0x%016llx, range=0x%016llx",
		xe_vma_start(vma), xe_vma_size(vma));

	down_write(&vm->userptr.notifier_lock);
	mmu_interval_set_seq(mni, cur_seq);

	/* No need to stop gpu access if the userptr is not yet bound. */
	if (!userptr->initial_bind) {
		up_write(&vm->userptr.notifier_lock);
		return true;
	}

	/*
	 * Tell exec and rebind worker they need to repin and rebind this
	 * userptr.
	 */
	if (!xe_vm_in_fault_mode(vm) &&
	    !(vma->gpuva.flags & XE_VMA_DESTROYED) && vma->tile_present) {
		spin_lock(&vm->userptr.invalidated_lock);
		list_move_tail(&userptr->invalidate_link,
			       &vm->userptr.invalidated);
		spin_unlock(&vm->userptr.invalidated_lock);
	}

	up_write(&vm->userptr.notifier_lock);

	/*
	 * Preempt fences turn into schedule disables, pipeline these.
	 * Note that even in fault mode, we need to wait for binds and
	 * unbinds to complete, and those are attached as BOOKMARK fences
	 * to the vm.
	 */
	dma_resv_iter_begin(&cursor, xe_vm_resv(vm),
			    DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_for_each_fence_unlocked(&cursor, fence)
		dma_fence_enable_sw_signaling(fence);
	dma_resv_iter_end(&cursor);

	err = dma_resv_wait_timeout(xe_vm_resv(vm),
				    DMA_RESV_USAGE_BOOKKEEP,
				    false, MAX_SCHEDULE_TIMEOUT);
	XE_WARN_ON(err <= 0);

	if (xe_vm_in_fault_mode(vm)) {
		err = xe_vm_invalidate_vma(vma);
		XE_WARN_ON(err);
	}

	trace_xe_vma_userptr_invalidate_complete(vma);

	return true;
}

static const struct mmu_interval_notifier_ops vma_userptr_notifier_ops = {
	.invalidate = vma_userptr_invalidate,
};

int xe_vm_userptr_pin(struct xe_vm *vm)
{
	struct xe_userptr_vma *uvma, *next;
	int err = 0;
	LIST_HEAD(tmp_evict);

	xe_assert(vm->xe, !xe_vm_in_fault_mode(vm));
	lockdep_assert_held_write(&vm->lock);

	/* Collect invalidated userptrs */
	spin_lock(&vm->userptr.invalidated_lock);
	list_for_each_entry_safe(uvma, next, &vm->userptr.invalidated,
				 userptr.invalidate_link) {
		list_del_init(&uvma->userptr.invalidate_link);
		list_move_tail(&uvma->userptr.repin_link,
			       &vm->userptr.repin_list);
	}
	spin_unlock(&vm->userptr.invalidated_lock);

	/* Pin and move to temporary list */
	list_for_each_entry_safe(uvma, next, &vm->userptr.repin_list,
				 userptr.repin_link) {
		err = xe_vma_userptr_pin_pages(uvma);
		if (err == -EFAULT) {
			list_del_init(&uvma->userptr.repin_link);

			/* Wait for pending binds */
			xe_vm_lock(vm, false);
			dma_resv_wait_timeout(xe_vm_resv(vm),
					      DMA_RESV_USAGE_BOOKKEEP,
					      false, MAX_SCHEDULE_TIMEOUT);

			err = xe_vm_invalidate_vma(&uvma->vma);
			xe_vm_unlock(vm);
			if (err)
				return err;
		} else {
			if (err < 0)
				return err;

			list_del_init(&uvma->userptr.repin_link);
			list_move_tail(&uvma->vma.combined_links.rebind,
				       &vm->rebind_list);
		}
	}

	return 0;
}

/**
 * xe_vm_userptr_check_repin() - Check whether the VM might have userptrs
 * that need repinning.
 * @vm: The VM.
 *
 * This function does an advisory check for whether the VM has userptrs that
 * need repinning.
 *
 * Return: 0 if there are no indications of userptrs needing repinning,
 * -EAGAIN if there are.
 */
int xe_vm_userptr_check_repin(struct xe_vm *vm)
{
	return (list_empty_careful(&vm->userptr.repin_list) &&
		list_empty_careful(&vm->userptr.invalidated)) ? 0 : -EAGAIN;
}

static struct dma_fence *
xe_vm_bind_vma(struct xe_vma *vma, struct xe_exec_queue *q,
	       struct xe_sync_entry *syncs, u32 num_syncs,
	       bool first_op, bool last_op);

int xe_vm_rebind(struct xe_vm *vm, bool rebind_worker)
{
	struct dma_fence *fence;
	struct xe_vma *vma, *next;

	lockdep_assert_held(&vm->lock);
	if (xe_vm_in_lr_mode(vm) && !rebind_worker)
		return 0;

	xe_vm_assert_held(vm);
	list_for_each_entry_safe(vma, next, &vm->rebind_list,
				 combined_links.rebind) {
		xe_assert(vm->xe, vma->tile_present);

		list_del_init(&vma->combined_links.rebind);
		if (rebind_worker)
			trace_xe_vma_rebind_worker(vma);
		else
			trace_xe_vma_rebind_exec(vma);
		fence = xe_vm_bind_vma(vma, NULL, NULL, 0, false, false);
		if (IS_ERR(fence))
			return PTR_ERR(fence);
		dma_fence_put(fence);
	}

	return 0;
}

static void xe_vma_free(struct xe_vma *vma)
{
	if (xe_vma_is_userptr(vma))
		kfree(to_userptr_vma(vma));
	else
		kfree(vma);
}

#define VMA_CREATE_FLAG_READ_ONLY	BIT(0)
#define VMA_CREATE_FLAG_IS_NULL		BIT(1)
#define VMA_CREATE_FLAG_DUMPABLE	BIT(2)

static struct xe_vma *xe_vma_create(struct xe_vm *vm,
				    struct xe_bo *bo,
				    u64 bo_offset_or_userptr,
				    u64 start, u64 end,
				    u16 pat_index, unsigned int flags)
{
	struct xe_vma *vma;
	struct xe_tile *tile;
	u8 id;
	bool read_only = (flags & VMA_CREATE_FLAG_READ_ONLY);
	bool is_null = (flags & VMA_CREATE_FLAG_IS_NULL);
	bool dumpable = (flags & VMA_CREATE_FLAG_DUMPABLE);

	xe_assert(vm->xe, start < end);
	xe_assert(vm->xe, end < vm->size);

	/*
	 * Allocate and ensure that the xe_vma_is_userptr() return
	 * matches what was allocated.
	 */
	if (!bo && !is_null) {
		struct xe_userptr_vma *uvma = kzalloc(sizeof(*uvma), GFP_KERNEL);

		if (!uvma)
			return ERR_PTR(-ENOMEM);

		vma = &uvma->vma;
	} else {
		vma = kzalloc(sizeof(*vma), GFP_KERNEL);
		if (!vma)
			return ERR_PTR(-ENOMEM);

		if (is_null)
			vma->gpuva.flags |= DRM_GPUVA_SPARSE;
		if (bo)
			vma->gpuva.gem.obj = &bo->ttm.base;
	}

	INIT_LIST_HEAD(&vma->combined_links.rebind);

	INIT_LIST_HEAD(&vma->gpuva.gem.entry);
	vma->gpuva.vm = &vm->gpuvm;
	vma->gpuva.va.addr = start;
	vma->gpuva.va.range = end - start + 1;
	if (read_only)
		vma->gpuva.flags |= XE_VMA_READ_ONLY;
	if (dumpable)
		vma->gpuva.flags |= XE_VMA_DUMPABLE;

	for_each_tile(tile, vm->xe, id)
		vma->tile_mask |= 0x1 << id;

	if (GRAPHICS_VER(vm->xe) >= 20 || vm->xe->info.platform == XE_PVC)
		vma->gpuva.flags |= XE_VMA_ATOMIC_PTE_BIT;

	vma->pat_index = pat_index;

	if (bo) {
		struct drm_gpuvm_bo *vm_bo;

		xe_bo_assert_held(bo);

		vm_bo = drm_gpuvm_bo_obtain(vma->gpuva.vm, &bo->ttm.base);
		if (IS_ERR(vm_bo)) {
			xe_vma_free(vma);
			return ERR_CAST(vm_bo);
		}

		drm_gpuvm_bo_extobj_add(vm_bo);
		drm_gem_object_get(&bo->ttm.base);
		vma->gpuva.gem.offset = bo_offset_or_userptr;
		drm_gpuva_link(&vma->gpuva, vm_bo);
		drm_gpuvm_bo_put(vm_bo);
	} else /* userptr or null */ {
		if (!is_null) {
			struct xe_userptr *userptr = &to_userptr_vma(vma)->userptr;
			u64 size = end - start + 1;
			int err;

			INIT_LIST_HEAD(&userptr->invalidate_link);
			INIT_LIST_HEAD(&userptr->repin_link);
			vma->gpuva.gem.offset = bo_offset_or_userptr;

			err = mmu_interval_notifier_insert(&userptr->notifier,
							   current->mm,
							   xe_vma_userptr(vma), size,
							   &vma_userptr_notifier_ops);
			if (err) {
				xe_vma_free(vma);
				return ERR_PTR(err);
			}

			userptr->notifier_seq = LONG_MAX;
		}

		xe_vm_get(vm);
	}

	return vma;
}

static void xe_vma_destroy_late(struct xe_vma *vma)
{
	struct xe_vm *vm = xe_vma_vm(vma);

	if (vma->ufence) {
		xe_sync_ufence_put(vma->ufence);
		vma->ufence = NULL;
	}

	if (xe_vma_is_userptr(vma)) {
		struct xe_userptr_vma *uvma = to_userptr_vma(vma);
		struct xe_userptr *userptr = &uvma->userptr;

		if (userptr->sg)
			xe_hmm_userptr_free_sg(uvma);

		/*
		 * Since userptr pages are not pinned, we can't remove
		 * the notifer until we're sure the GPU is not accessing
		 * them anymore
		 */
		mmu_interval_notifier_remove(&userptr->notifier);
		xe_vm_put(vm);
	} else if (xe_vma_is_null(vma)) {
		xe_vm_put(vm);
	} else {
		xe_bo_put(xe_vma_bo(vma));
	}

	xe_vma_free(vma);
}

static void vma_destroy_work_func(struct work_struct *w)
{
	struct xe_vma *vma =
		container_of(w, struct xe_vma, destroy_work);

	xe_vma_destroy_late(vma);
}

static void vma_destroy_cb(struct dma_fence *fence,
			   struct dma_fence_cb *cb)
{
	struct xe_vma *vma = container_of(cb, struct xe_vma, destroy_cb);

	INIT_WORK(&vma->destroy_work, vma_destroy_work_func);
	queue_work(system_unbound_wq, &vma->destroy_work);
}

static void xe_vma_destroy(struct xe_vma *vma, struct dma_fence *fence)
{
	struct xe_vm *vm = xe_vma_vm(vma);

	lockdep_assert_held_write(&vm->lock);
	xe_assert(vm->xe, list_empty(&vma->combined_links.destroy));

	if (xe_vma_is_userptr(vma)) {
		xe_assert(vm->xe, vma->gpuva.flags & XE_VMA_DESTROYED);

		spin_lock(&vm->userptr.invalidated_lock);
		list_del(&to_userptr_vma(vma)->userptr.invalidate_link);
		spin_unlock(&vm->userptr.invalidated_lock);
	} else if (!xe_vma_is_null(vma)) {
		xe_bo_assert_held(xe_vma_bo(vma));

		drm_gpuva_unlink(&vma->gpuva);
	}

	xe_vm_assert_held(vm);
	if (fence) {
		int ret = dma_fence_add_callback(fence, &vma->destroy_cb,
						 vma_destroy_cb);

		if (ret) {
			XE_WARN_ON(ret != -ENOENT);
			xe_vma_destroy_late(vma);
		}
	} else {
		xe_vma_destroy_late(vma);
	}
}

/**
 * xe_vm_lock_vma() - drm_exec utility to lock a vma
 * @exec: The drm_exec object we're currently locking for.
 * @vma: The vma for witch we want to lock the vm resv and any attached
 * object's resv.
 *
 * Return: 0 on success, negative error code on error. In particular
 * may return -EDEADLK on WW transaction contention and -EINTR if
 * an interruptible wait is terminated by a signal.
 */
int xe_vm_lock_vma(struct drm_exec *exec, struct xe_vma *vma)
{
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_bo *bo = xe_vma_bo(vma);
	int err;

	XE_WARN_ON(!vm);

	err = drm_exec_lock_obj(exec, xe_vm_obj(vm));
	if (!err && bo && !bo->vm)
		err = drm_exec_lock_obj(exec, &bo->ttm.base);

	return err;
}

static void xe_vma_destroy_unlocked(struct xe_vma *vma)
{
	struct drm_exec exec;
	int err;

	drm_exec_init(&exec, 0, 0);
	drm_exec_until_all_locked(&exec) {
		err = xe_vm_lock_vma(&exec, vma);
		drm_exec_retry_on_contention(&exec);
		if (XE_WARN_ON(err))
			break;
	}

	xe_vma_destroy(vma, NULL);

	drm_exec_fini(&exec);
}

struct xe_vma *
xe_vm_find_overlapping_vma(struct xe_vm *vm, u64 start, u64 range)
{
	struct drm_gpuva *gpuva;

	lockdep_assert_held(&vm->lock);

	if (xe_vm_is_closed_or_banned(vm))
		return NULL;

	xe_assert(vm->xe, start + range <= vm->size);

	gpuva = drm_gpuva_find_first(&vm->gpuvm, start, range);

	return gpuva ? gpuva_to_vma(gpuva) : NULL;
}

static int xe_vm_insert_vma(struct xe_vm *vm, struct xe_vma *vma)
{
	int err;

	xe_assert(vm->xe, xe_vma_vm(vma) == vm);
	lockdep_assert_held(&vm->lock);

	mutex_lock(&vm->snap_mutex);
	err = drm_gpuva_insert(&vm->gpuvm, &vma->gpuva);
	mutex_unlock(&vm->snap_mutex);
	XE_WARN_ON(err);	/* Shouldn't be possible */

	return err;
}

static void xe_vm_remove_vma(struct xe_vm *vm, struct xe_vma *vma)
{
	xe_assert(vm->xe, xe_vma_vm(vma) == vm);
	lockdep_assert_held(&vm->lock);

	mutex_lock(&vm->snap_mutex);
	drm_gpuva_remove(&vma->gpuva);
	mutex_unlock(&vm->snap_mutex);
	if (vm->usm.last_fault_vma == vma)
		vm->usm.last_fault_vma = NULL;
}

static struct drm_gpuva_op *xe_vm_op_alloc(void)
{
	struct xe_vma_op *op;

	op = kzalloc(sizeof(*op), GFP_KERNEL);

	if (unlikely(!op))
		return NULL;

	return &op->base;
}

static void xe_vm_free(struct drm_gpuvm *gpuvm);

static const struct drm_gpuvm_ops gpuvm_ops = {
	.op_alloc = xe_vm_op_alloc,
	.vm_bo_validate = xe_gpuvm_validate,
	.vm_free = xe_vm_free,
};

static u64 pde_encode_pat_index(struct xe_device *xe, u16 pat_index)
{
	u64 pte = 0;

	if (pat_index & BIT(0))
		pte |= XE_PPGTT_PTE_PAT0;

	if (pat_index & BIT(1))
		pte |= XE_PPGTT_PTE_PAT1;

	return pte;
}

static u64 pte_encode_pat_index(struct xe_device *xe, u16 pat_index,
				u32 pt_level)
{
	u64 pte = 0;

	if (pat_index & BIT(0))
		pte |= XE_PPGTT_PTE_PAT0;

	if (pat_index & BIT(1))
		pte |= XE_PPGTT_PTE_PAT1;

	if (pat_index & BIT(2)) {
		if (pt_level)
			pte |= XE_PPGTT_PDE_PDPE_PAT2;
		else
			pte |= XE_PPGTT_PTE_PAT2;
	}

	if (pat_index & BIT(3))
		pte |= XELPG_PPGTT_PTE_PAT3;

	if (pat_index & (BIT(4)))
		pte |= XE2_PPGTT_PTE_PAT4;

	return pte;
}

static u64 pte_encode_ps(u32 pt_level)
{
	XE_WARN_ON(pt_level > MAX_HUGEPTE_LEVEL);

	if (pt_level == 1)
		return XE_PDE_PS_2M;
	else if (pt_level == 2)
		return XE_PDPE_PS_1G;

	return 0;
}

static u64 xelp_pde_encode_bo(struct xe_bo *bo, u64 bo_offset,
			      const u16 pat_index)
{
	struct xe_device *xe = xe_bo_device(bo);
	u64 pde;

	pde = xe_bo_addr(bo, bo_offset, XE_PAGE_SIZE);
	pde |= XE_PAGE_PRESENT | XE_PAGE_RW;
	pde |= pde_encode_pat_index(xe, pat_index);

	return pde;
}

static u64 xelp_pte_encode_bo(struct xe_bo *bo, u64 bo_offset,
			      u16 pat_index, u32 pt_level)
{
	struct xe_device *xe = xe_bo_device(bo);
	u64 pte;

	pte = xe_bo_addr(bo, bo_offset, XE_PAGE_SIZE);
	pte |= XE_PAGE_PRESENT | XE_PAGE_RW;
	pte |= pte_encode_pat_index(xe, pat_index, pt_level);
	pte |= pte_encode_ps(pt_level);

	if (xe_bo_is_vram(bo) || xe_bo_is_stolen_devmem(bo))
		pte |= XE_PPGTT_PTE_DM;

	return pte;
}

static u64 xelp_pte_encode_vma(u64 pte, struct xe_vma *vma,
			       u16 pat_index, u32 pt_level)
{
	struct xe_device *xe = xe_vma_vm(vma)->xe;

	pte |= XE_PAGE_PRESENT;

	if (likely(!xe_vma_read_only(vma)))
		pte |= XE_PAGE_RW;

	pte |= pte_encode_pat_index(xe, pat_index, pt_level);
	pte |= pte_encode_ps(pt_level);

	if (unlikely(xe_vma_is_null(vma)))
		pte |= XE_PTE_NULL;

	return pte;
}

static u64 xelp_pte_encode_addr(struct xe_device *xe, u64 addr,
				u16 pat_index,
				u32 pt_level, bool devmem, u64 flags)
{
	u64 pte;

	/* Avoid passing random bits directly as flags */
	xe_assert(xe, !(flags & ~XE_PTE_PS64));

	pte = addr;
	pte |= XE_PAGE_PRESENT | XE_PAGE_RW;
	pte |= pte_encode_pat_index(xe, pat_index, pt_level);
	pte |= pte_encode_ps(pt_level);

	if (devmem)
		pte |= XE_PPGTT_PTE_DM;

	pte |= flags;

	return pte;
}

static const struct xe_pt_ops xelp_pt_ops = {
	.pte_encode_bo = xelp_pte_encode_bo,
	.pte_encode_vma = xelp_pte_encode_vma,
	.pte_encode_addr = xelp_pte_encode_addr,
	.pde_encode_bo = xelp_pde_encode_bo,
};

/**
 * xe_vm_create_scratch() - Setup a scratch memory pagetable tree for the
 * given tile and vm.
 * @xe: xe device.
 * @tile: tile to set up for.
 * @vm: vm to set up for.
 *
 * Sets up a pagetable tree with one page-table per level and a single
 * leaf PTE. All pagetable entries point to the single page-table or,
 * for MAX_HUGEPTE_LEVEL, a NULL huge PTE returning 0 on read and
 * writes become NOPs.
 *
 * Return: 0 on success, negative error code on error.
 */
static int xe_vm_create_scratch(struct xe_device *xe, struct xe_tile *tile,
				struct xe_vm *vm)
{
	u8 id = tile->id;
	int i;

	for (i = MAX_HUGEPTE_LEVEL; i < vm->pt_root[id]->level; i++) {
		vm->scratch_pt[id][i] = xe_pt_create(vm, tile, i);
		if (IS_ERR(vm->scratch_pt[id][i]))
			return PTR_ERR(vm->scratch_pt[id][i]);

		xe_pt_populate_empty(tile, vm, vm->scratch_pt[id][i]);
	}

	return 0;
}

static void xe_vm_free_scratch(struct xe_vm *vm)
{
	struct xe_tile *tile;
	u8 id;

	if (!xe_vm_has_scratch(vm))
		return;

	for_each_tile(tile, vm->xe, id) {
		u32 i;

		if (!vm->pt_root[id])
			continue;

		for (i = MAX_HUGEPTE_LEVEL; i < vm->pt_root[id]->level; ++i)
			if (vm->scratch_pt[id][i])
				xe_pt_destroy(vm->scratch_pt[id][i], vm->flags, NULL);
	}
}

struct xe_vm *xe_vm_create(struct xe_device *xe, u32 flags)
{
	struct drm_gem_object *vm_resv_obj;
	struct xe_vm *vm;
	int err, number_tiles = 0;
	struct xe_tile *tile;
	u8 id;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return ERR_PTR(-ENOMEM);

	vm->xe = xe;

	vm->size = 1ull << xe->info.va_bits;

	vm->flags = flags;

	init_rwsem(&vm->lock);
	mutex_init(&vm->snap_mutex);

	INIT_LIST_HEAD(&vm->rebind_list);

	INIT_LIST_HEAD(&vm->userptr.repin_list);
	INIT_LIST_HEAD(&vm->userptr.invalidated);
	init_rwsem(&vm->userptr.notifier_lock);
	spin_lock_init(&vm->userptr.invalidated_lock);

	INIT_LIST_HEAD(&vm->preempt.exec_queues);
	vm->preempt.min_run_period_ms = 10;	/* FIXME: Wire up to uAPI */

	for_each_tile(tile, xe, id)
		xe_range_fence_tree_init(&vm->rftree[id]);

	vm->pt_ops = &xelp_pt_ops;

	if (!(flags & XE_VM_FLAG_MIGRATION))
		xe_pm_runtime_get_noresume(xe);

	vm_resv_obj = drm_gpuvm_resv_object_alloc(&xe->drm);
	if (!vm_resv_obj) {
		err = -ENOMEM;
		goto err_no_resv;
	}

	drm_gpuvm_init(&vm->gpuvm, "Xe VM", DRM_GPUVM_RESV_PROTECTED, &xe->drm,
		       vm_resv_obj, 0, vm->size, 0, 0, &gpuvm_ops);

	drm_gem_object_put(vm_resv_obj);

	err = dma_resv_lock_interruptible(xe_vm_resv(vm), NULL);
	if (err)
		goto err_close;

	if (IS_DGFX(xe) && xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
		vm->flags |= XE_VM_FLAG_64K;

	for_each_tile(tile, xe, id) {
		if (flags & XE_VM_FLAG_MIGRATION &&
		    tile->id != XE_VM_FLAG_TILE_ID(flags))
			continue;

		vm->pt_root[id] = xe_pt_create(vm, tile, xe->info.vm_max_level);
		if (IS_ERR(vm->pt_root[id])) {
			err = PTR_ERR(vm->pt_root[id]);
			vm->pt_root[id] = NULL;
			goto err_unlock_close;
		}
	}

	if (xe_vm_has_scratch(vm)) {
		for_each_tile(tile, xe, id) {
			if (!vm->pt_root[id])
				continue;

			err = xe_vm_create_scratch(xe, tile, vm);
			if (err)
				goto err_unlock_close;
		}
		vm->batch_invalidate_tlb = true;
	}

	if (vm->flags & XE_VM_FLAG_LR_MODE) {
		INIT_WORK(&vm->preempt.rebind_work, preempt_rebind_work_func);
		vm->batch_invalidate_tlb = false;
	}

	/* Fill pt_root after allocating scratch tables */
	for_each_tile(tile, xe, id) {
		if (!vm->pt_root[id])
			continue;

		xe_pt_populate_empty(tile, vm, vm->pt_root[id]);
	}
	dma_resv_unlock(xe_vm_resv(vm));

	/* Kernel migration VM shouldn't have a circular loop.. */
	if (!(flags & XE_VM_FLAG_MIGRATION)) {
		for_each_tile(tile, xe, id) {
			struct xe_gt *gt = tile->primary_gt;
			struct xe_vm *migrate_vm;
			struct xe_exec_queue *q;
			u32 create_flags = EXEC_QUEUE_FLAG_VM;

			if (!vm->pt_root[id])
				continue;

			migrate_vm = xe_migrate_get_vm(tile->migrate);
			q = xe_exec_queue_create_class(xe, gt, migrate_vm,
						       XE_ENGINE_CLASS_COPY,
						       create_flags);
			xe_vm_put(migrate_vm);
			if (IS_ERR(q)) {
				err = PTR_ERR(q);
				goto err_close;
			}
			vm->q[id] = q;
			number_tiles++;
		}
	}

	if (number_tiles > 1)
		vm->composite_fence_ctx = dma_fence_context_alloc(1);

	mutex_lock(&xe->usm.lock);
	if (flags & XE_VM_FLAG_FAULT_MODE)
		xe->usm.num_vm_in_fault_mode++;
	else if (!(flags & XE_VM_FLAG_MIGRATION))
		xe->usm.num_vm_in_non_fault_mode++;
	mutex_unlock(&xe->usm.lock);

	trace_xe_vm_create(vm);

	return vm;

err_unlock_close:
	dma_resv_unlock(xe_vm_resv(vm));
err_close:
	xe_vm_close_and_put(vm);
	return ERR_PTR(err);

err_no_resv:
	mutex_destroy(&vm->snap_mutex);
	for_each_tile(tile, xe, id)
		xe_range_fence_tree_fini(&vm->rftree[id]);
	kfree(vm);
	if (!(flags & XE_VM_FLAG_MIGRATION))
		xe_pm_runtime_put(xe);
	return ERR_PTR(err);
}

static void xe_vm_close(struct xe_vm *vm)
{
	down_write(&vm->lock);
	vm->size = 0;
	up_write(&vm->lock);
}

void xe_vm_close_and_put(struct xe_vm *vm)
{
	LIST_HEAD(contested);
	struct xe_device *xe = vm->xe;
	struct xe_tile *tile;
	struct xe_vma *vma, *next_vma;
	struct drm_gpuva *gpuva, *next;
	u8 id;

	xe_assert(xe, !vm->preempt.num_exec_queues);

	xe_vm_close(vm);
	if (xe_vm_in_preempt_fence_mode(vm))
		flush_work(&vm->preempt.rebind_work);

	down_write(&vm->lock);
	for_each_tile(tile, xe, id) {
		if (vm->q[id])
			xe_exec_queue_last_fence_put(vm->q[id], vm);
	}
	up_write(&vm->lock);

	for_each_tile(tile, xe, id) {
		if (vm->q[id]) {
			xe_exec_queue_kill(vm->q[id]);
			xe_exec_queue_put(vm->q[id]);
			vm->q[id] = NULL;
		}
	}

	down_write(&vm->lock);
	xe_vm_lock(vm, false);
	drm_gpuvm_for_each_va_safe(gpuva, next, &vm->gpuvm) {
		vma = gpuva_to_vma(gpuva);

		if (xe_vma_has_no_bo(vma)) {
			down_read(&vm->userptr.notifier_lock);
			vma->gpuva.flags |= XE_VMA_DESTROYED;
			up_read(&vm->userptr.notifier_lock);
		}

		xe_vm_remove_vma(vm, vma);

		/* easy case, remove from VMA? */
		if (xe_vma_has_no_bo(vma) || xe_vma_bo(vma)->vm) {
			list_del_init(&vma->combined_links.rebind);
			xe_vma_destroy(vma, NULL);
			continue;
		}

		list_move_tail(&vma->combined_links.destroy, &contested);
		vma->gpuva.flags |= XE_VMA_DESTROYED;
	}

	/*
	 * All vm operations will add shared fences to resv.
	 * The only exception is eviction for a shared object,
	 * but even so, the unbind when evicted would still
	 * install a fence to resv. Hence it's safe to
	 * destroy the pagetables immediately.
	 */
	xe_vm_free_scratch(vm);

	for_each_tile(tile, xe, id) {
		if (vm->pt_root[id]) {
			xe_pt_destroy(vm->pt_root[id], vm->flags, NULL);
			vm->pt_root[id] = NULL;
		}
	}
	xe_vm_unlock(vm);

	/*
	 * VM is now dead, cannot re-add nodes to vm->vmas if it's NULL
	 * Since we hold a refcount to the bo, we can remove and free
	 * the members safely without locking.
	 */
	list_for_each_entry_safe(vma, next_vma, &contested,
				 combined_links.destroy) {
		list_del_init(&vma->combined_links.destroy);
		xe_vma_destroy_unlocked(vma);
	}

	up_write(&vm->lock);

	mutex_lock(&xe->usm.lock);
	if (vm->flags & XE_VM_FLAG_FAULT_MODE)
		xe->usm.num_vm_in_fault_mode--;
	else if (!(vm->flags & XE_VM_FLAG_MIGRATION))
		xe->usm.num_vm_in_non_fault_mode--;

	if (vm->usm.asid) {
		void *lookup;

		xe_assert(xe, xe->info.has_asid);
		xe_assert(xe, !(vm->flags & XE_VM_FLAG_MIGRATION));

		lookup = xa_erase(&xe->usm.asid_to_vm, vm->usm.asid);
		xe_assert(xe, lookup == vm);
	}
	mutex_unlock(&xe->usm.lock);

	for_each_tile(tile, xe, id)
		xe_range_fence_tree_fini(&vm->rftree[id]);

	xe_vm_put(vm);
}

static void xe_vm_free(struct drm_gpuvm *gpuvm)
{
	struct xe_vm *vm = container_of(gpuvm, struct xe_vm, gpuvm);
	struct xe_device *xe = vm->xe;
	struct xe_tile *tile;
	u8 id;

	/* xe_vm_close_and_put was not called? */
	xe_assert(xe, !vm->size);

	if (xe_vm_in_preempt_fence_mode(vm))
		flush_work(&vm->preempt.rebind_work);

	mutex_destroy(&vm->snap_mutex);

	if (!(vm->flags & XE_VM_FLAG_MIGRATION))
		xe_pm_runtime_put(xe);

	for_each_tile(tile, xe, id)
		XE_WARN_ON(vm->pt_root[id]);

	trace_xe_vm_free(vm);
	kfree(vm);
}

struct xe_vm *xe_vm_lookup(struct xe_file *xef, u32 id)
{
	struct xe_vm *vm;

	mutex_lock(&xef->vm.lock);
	vm = xa_load(&xef->vm.xa, id);
	if (vm)
		xe_vm_get(vm);
	mutex_unlock(&xef->vm.lock);

	return vm;
}

u64 xe_vm_pdp4_descriptor(struct xe_vm *vm, struct xe_tile *tile)
{
	return vm->pt_ops->pde_encode_bo(vm->pt_root[tile->id]->bo, 0,
					 tile_to_xe(tile)->pat.idx[XE_CACHE_WB]);
}

static struct xe_exec_queue *
to_wait_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q)
{
	return q ? q : vm->q[0];
}

static struct dma_fence *
xe_vm_unbind_vma(struct xe_vma *vma, struct xe_exec_queue *q,
		 struct xe_sync_entry *syncs, u32 num_syncs,
		 bool first_op, bool last_op)
{
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_exec_queue *wait_exec_queue = to_wait_exec_queue(vm, q);
	struct xe_tile *tile;
	struct dma_fence *fence = NULL;
	struct dma_fence **fences = NULL;
	struct dma_fence_array *cf = NULL;
	int cur_fence = 0, i;
	int number_tiles = hweight8(vma->tile_present);
	int err;
	u8 id;

	trace_xe_vma_unbind(vma);

	if (vma->ufence) {
		struct xe_user_fence * const f = vma->ufence;

		if (!xe_sync_ufence_get_status(f))
			return ERR_PTR(-EBUSY);

		vma->ufence = NULL;
		xe_sync_ufence_put(f);
	}

	if (number_tiles > 1) {
		fences = kmalloc_array(number_tiles, sizeof(*fences),
				       GFP_KERNEL);
		if (!fences)
			return ERR_PTR(-ENOMEM);
	}

	for_each_tile(tile, vm->xe, id) {
		if (!(vma->tile_present & BIT(id)))
			goto next;

		fence = __xe_pt_unbind_vma(tile, vma, q ? q : vm->q[id],
					   first_op ? syncs : NULL,
					   first_op ? num_syncs : 0);
		if (IS_ERR(fence)) {
			err = PTR_ERR(fence);
			goto err_fences;
		}

		if (fences)
			fences[cur_fence++] = fence;

next:
		if (q && vm->pt_root[id] && !list_empty(&q->multi_gt_list))
			q = list_next_entry(q, multi_gt_list);
	}

	if (fences) {
		cf = dma_fence_array_create(number_tiles, fences,
					    vm->composite_fence_ctx,
					    vm->composite_fence_seqno++,
					    false);
		if (!cf) {
			--vm->composite_fence_seqno;
			err = -ENOMEM;
			goto err_fences;
		}
	}

	fence = cf ? &cf->base : !fence ?
		xe_exec_queue_last_fence_get(wait_exec_queue, vm) : fence;
	if (last_op) {
		for (i = 0; i < num_syncs; i++)
			xe_sync_entry_signal(&syncs[i], fence);
	}

	return fence;

err_fences:
	if (fences) {
		while (cur_fence)
			dma_fence_put(fences[--cur_fence]);
		kfree(fences);
	}

	return ERR_PTR(err);
}

static struct dma_fence *
xe_vm_bind_vma(struct xe_vma *vma, struct xe_exec_queue *q,
	       struct xe_sync_entry *syncs, u32 num_syncs,
	       bool first_op, bool last_op)
{
	struct xe_tile *tile;
	struct dma_fence *fence;
	struct dma_fence **fences = NULL;
	struct dma_fence_array *cf = NULL;
	struct xe_vm *vm = xe_vma_vm(vma);
	int cur_fence = 0, i;
	int number_tiles = hweight8(vma->tile_mask);
	int err;
	u8 id;

	trace_xe_vma_bind(vma);

	if (number_tiles > 1) {
		fences = kmalloc_array(number_tiles, sizeof(*fences),
				       GFP_KERNEL);
		if (!fences)
			return ERR_PTR(-ENOMEM);
	}

	for_each_tile(tile, vm->xe, id) {
		if (!(vma->tile_mask & BIT(id)))
			goto next;

		fence = __xe_pt_bind_vma(tile, vma, q ? q : vm->q[id],
					 first_op ? syncs : NULL,
					 first_op ? num_syncs : 0,
					 vma->tile_present & BIT(id));
		if (IS_ERR(fence)) {
			err = PTR_ERR(fence);
			goto err_fences;
		}

		if (fences)
			fences[cur_fence++] = fence;

next:
		if (q && vm->pt_root[id] && !list_empty(&q->multi_gt_list))
			q = list_next_entry(q, multi_gt_list);
	}

	if (fences) {
		cf = dma_fence_array_create(number_tiles, fences,
					    vm->composite_fence_ctx,
					    vm->composite_fence_seqno++,
					    false);
		if (!cf) {
			--vm->composite_fence_seqno;
			err = -ENOMEM;
			goto err_fences;
		}
	}

	if (last_op) {
		for (i = 0; i < num_syncs; i++)
			xe_sync_entry_signal(&syncs[i],
					     cf ? &cf->base : fence);
	}

	return cf ? &cf->base : fence;

err_fences:
	if (fences) {
		while (cur_fence)
			dma_fence_put(fences[--cur_fence]);
		kfree(fences);
	}

	return ERR_PTR(err);
}

static struct xe_user_fence *
find_ufence_get(struct xe_sync_entry *syncs, u32 num_syncs)
{
	unsigned int i;

	for (i = 0; i < num_syncs; i++) {
		struct xe_sync_entry *e = &syncs[i];

		if (xe_sync_is_ufence(e))
			return xe_sync_ufence_get(e);
	}

	return NULL;
}

static int __xe_vm_bind(struct xe_vm *vm, struct xe_vma *vma,
			struct xe_exec_queue *q, struct xe_sync_entry *syncs,
			u32 num_syncs, bool immediate, bool first_op,
			bool last_op)
{
	struct dma_fence *fence;
	struct xe_exec_queue *wait_exec_queue = to_wait_exec_queue(vm, q);
	struct xe_user_fence *ufence;

	xe_vm_assert_held(vm);

	ufence = find_ufence_get(syncs, num_syncs);
	if (vma->ufence && ufence)
		xe_sync_ufence_put(vma->ufence);

	vma->ufence = ufence ?: vma->ufence;

	if (immediate) {
		fence = xe_vm_bind_vma(vma, q, syncs, num_syncs, first_op,
				       last_op);
		if (IS_ERR(fence))
			return PTR_ERR(fence);
	} else {
		int i;

		xe_assert(vm->xe, xe_vm_in_fault_mode(vm));

		fence = xe_exec_queue_last_fence_get(wait_exec_queue, vm);
		if (last_op) {
			for (i = 0; i < num_syncs; i++)
				xe_sync_entry_signal(&syncs[i], fence);
		}
	}

	if (last_op)
		xe_exec_queue_last_fence_set(wait_exec_queue, vm, fence);
	dma_fence_put(fence);

	return 0;
}

static int xe_vm_bind(struct xe_vm *vm, struct xe_vma *vma, struct xe_exec_queue *q,
		      struct xe_bo *bo, struct xe_sync_entry *syncs,
		      u32 num_syncs, bool immediate, bool first_op,
		      bool last_op)
{
	int err;

	xe_vm_assert_held(vm);
	xe_bo_assert_held(bo);

	if (bo && immediate) {
		err = xe_bo_validate(bo, vm, true);
		if (err)
			return err;
	}

	return __xe_vm_bind(vm, vma, q, syncs, num_syncs, immediate, first_op,
			    last_op);
}

static int xe_vm_unbind(struct xe_vm *vm, struct xe_vma *vma,
			struct xe_exec_queue *q, struct xe_sync_entry *syncs,
			u32 num_syncs, bool first_op, bool last_op)
{
	struct dma_fence *fence;
	struct xe_exec_queue *wait_exec_queue = to_wait_exec_queue(vm, q);

	xe_vm_assert_held(vm);
	xe_bo_assert_held(xe_vma_bo(vma));

	fence = xe_vm_unbind_vma(vma, q, syncs, num_syncs, first_op, last_op);
	if (IS_ERR(fence))
		return PTR_ERR(fence);

	xe_vma_destroy(vma, fence);
	if (last_op)
		xe_exec_queue_last_fence_set(wait_exec_queue, vm, fence);
	dma_fence_put(fence);

	return 0;
}

#define ALL_DRM_XE_VM_CREATE_FLAGS (DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE | \
				    DRM_XE_VM_CREATE_FLAG_LR_MODE | \
				    DRM_XE_VM_CREATE_FLAG_FAULT_MODE)

int xe_vm_create_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_vm_create *args = data;
	struct xe_tile *tile;
	struct xe_vm *vm;
	u32 id, asid;
	int err;
	u32 flags = 0;

	if (XE_IOCTL_DBG(xe, args->extensions))
		return -EINVAL;

	if (XE_WA(xe_root_mmio_gt(xe), 14016763929))
		args->flags |= DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE;

	if (XE_IOCTL_DBG(xe, args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE &&
			 !xe->info.has_usm))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & ~ALL_DRM_XE_VM_CREATE_FLAGS))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE &&
			 args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, !(args->flags & DRM_XE_VM_CREATE_FLAG_LR_MODE) &&
			 args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE &&
			 xe_device_in_non_fault_mode(xe)))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, !(args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE) &&
			 xe_device_in_fault_mode(xe)))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->extensions))
		return -EINVAL;

	if (args->flags & DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE)
		flags |= XE_VM_FLAG_SCRATCH_PAGE;
	if (args->flags & DRM_XE_VM_CREATE_FLAG_LR_MODE)
		flags |= XE_VM_FLAG_LR_MODE;
	if (args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE)
		flags |= XE_VM_FLAG_FAULT_MODE;

	vm = xe_vm_create(xe, flags);
	if (IS_ERR(vm))
		return PTR_ERR(vm);

	mutex_lock(&xef->vm.lock);
	err = xa_alloc(&xef->vm.xa, &id, vm, xa_limit_32b, GFP_KERNEL);
	mutex_unlock(&xef->vm.lock);
	if (err)
		goto err_close_and_put;

	if (xe->info.has_asid) {
		mutex_lock(&xe->usm.lock);
		err = xa_alloc_cyclic(&xe->usm.asid_to_vm, &asid, vm,
				      XA_LIMIT(1, XE_MAX_ASID - 1),
				      &xe->usm.next_asid, GFP_KERNEL);
		mutex_unlock(&xe->usm.lock);
		if (err < 0)
			goto err_free_id;

		vm->usm.asid = asid;
	}

	args->vm_id = id;
	vm->xef = xef;

	/* Record BO memory for VM pagetable created against client */
	for_each_tile(tile, xe, id)
		if (vm->pt_root[id])
			xe_drm_client_add_bo(vm->xef->client, vm->pt_root[id]->bo);

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_MEM)
	/* Warning: Security issue - never enable by default */
	args->reserved[0] = xe_bo_main_addr(vm->pt_root[0]->bo, XE_PAGE_SIZE);
#endif

	return 0;

err_free_id:
	mutex_lock(&xef->vm.lock);
	xa_erase(&xef->vm.xa, id);
	mutex_unlock(&xef->vm.lock);
err_close_and_put:
	xe_vm_close_and_put(vm);

	return err;
}

int xe_vm_destroy_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_vm_destroy *args = data;
	struct xe_vm *vm;
	int err = 0;

	if (XE_IOCTL_DBG(xe, args->pad) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	mutex_lock(&xef->vm.lock);
	vm = xa_load(&xef->vm.xa, args->vm_id);
	if (XE_IOCTL_DBG(xe, !vm))
		err = -ENOENT;
	else if (XE_IOCTL_DBG(xe, vm->preempt.num_exec_queues))
		err = -EBUSY;
	else
		xa_erase(&xef->vm.xa, args->vm_id);
	mutex_unlock(&xef->vm.lock);

	if (!err)
		xe_vm_close_and_put(vm);

	return err;
}

static const u32 region_to_mem_type[] = {
	XE_PL_TT,
	XE_PL_VRAM0,
	XE_PL_VRAM1,
};

static int xe_vm_prefetch(struct xe_vm *vm, struct xe_vma *vma,
			  struct xe_exec_queue *q, u32 region,
			  struct xe_sync_entry *syncs, u32 num_syncs,
			  bool first_op, bool last_op)
{
	struct xe_exec_queue *wait_exec_queue = to_wait_exec_queue(vm, q);
	int err;

	xe_assert(vm->xe, region < ARRAY_SIZE(region_to_mem_type));

	if (!xe_vma_has_no_bo(vma)) {
		err = xe_bo_migrate(xe_vma_bo(vma), region_to_mem_type[region]);
		if (err)
			return err;
	}

	if (vma->tile_mask != (vma->tile_present & ~vma->tile_invalidated)) {
		return xe_vm_bind(vm, vma, q, xe_vma_bo(vma), syncs, num_syncs,
				  true, first_op, last_op);
	} else {
		int i;

		/* Nothing to do, signal fences now */
		if (last_op) {
			for (i = 0; i < num_syncs; i++) {
				struct dma_fence *fence =
					xe_exec_queue_last_fence_get(wait_exec_queue, vm);

				xe_sync_entry_signal(&syncs[i], fence);
				dma_fence_put(fence);
			}
		}

		return 0;
	}
}

static void prep_vma_destroy(struct xe_vm *vm, struct xe_vma *vma,
			     bool post_commit)
{
	down_read(&vm->userptr.notifier_lock);
	vma->gpuva.flags |= XE_VMA_DESTROYED;
	up_read(&vm->userptr.notifier_lock);
	if (post_commit)
		xe_vm_remove_vma(vm, vma);
}

#undef ULL
#define ULL	unsigned long long

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_VM)
static void print_op(struct xe_device *xe, struct drm_gpuva_op *op)
{
	struct xe_vma *vma;

	switch (op->op) {
	case DRM_GPUVA_OP_MAP:
		vm_dbg(&xe->drm, "MAP: addr=0x%016llx, range=0x%016llx",
		       (ULL)op->map.va.addr, (ULL)op->map.va.range);
		break;
	case DRM_GPUVA_OP_REMAP:
		vma = gpuva_to_vma(op->remap.unmap->va);
		vm_dbg(&xe->drm, "REMAP:UNMAP: addr=0x%016llx, range=0x%016llx, keep=%d",
		       (ULL)xe_vma_start(vma), (ULL)xe_vma_size(vma),
		       op->remap.unmap->keep ? 1 : 0);
		if (op->remap.prev)
			vm_dbg(&xe->drm,
			       "REMAP:PREV: addr=0x%016llx, range=0x%016llx",
			       (ULL)op->remap.prev->va.addr,
			       (ULL)op->remap.prev->va.range);
		if (op->remap.next)
			vm_dbg(&xe->drm,
			       "REMAP:NEXT: addr=0x%016llx, range=0x%016llx",
			       (ULL)op->remap.next->va.addr,
			       (ULL)op->remap.next->va.range);
		break;
	case DRM_GPUVA_OP_UNMAP:
		vma = gpuva_to_vma(op->unmap.va);
		vm_dbg(&xe->drm, "UNMAP: addr=0x%016llx, range=0x%016llx, keep=%d",
		       (ULL)xe_vma_start(vma), (ULL)xe_vma_size(vma),
		       op->unmap.keep ? 1 : 0);
		break;
	case DRM_GPUVA_OP_PREFETCH:
		vma = gpuva_to_vma(op->prefetch.va);
		vm_dbg(&xe->drm, "PREFETCH: addr=0x%016llx, range=0x%016llx",
		       (ULL)xe_vma_start(vma), (ULL)xe_vma_size(vma));
		break;
	default:
		drm_warn(&xe->drm, "NOT POSSIBLE");
	}
}
#else
static void print_op(struct xe_device *xe, struct drm_gpuva_op *op)
{
}
#endif

/*
 * Create operations list from IOCTL arguments, setup operations fields so parse
 * and commit steps are decoupled from IOCTL arguments. This step can fail.
 */
static struct drm_gpuva_ops *
vm_bind_ioctl_ops_create(struct xe_vm *vm, struct xe_bo *bo,
			 u64 bo_offset_or_userptr, u64 addr, u64 range,
			 u32 operation, u32 flags,
			 u32 prefetch_region, u16 pat_index)
{
	struct drm_gem_object *obj = bo ? &bo->ttm.base : NULL;
	struct drm_gpuva_ops *ops;
	struct drm_gpuva_op *__op;
	struct drm_gpuvm_bo *vm_bo;
	int err;

	lockdep_assert_held_write(&vm->lock);

	vm_dbg(&vm->xe->drm,
	       "op=%d, addr=0x%016llx, range=0x%016llx, bo_offset_or_userptr=0x%016llx",
	       operation, (ULL)addr, (ULL)range,
	       (ULL)bo_offset_or_userptr);

	switch (operation) {
	case DRM_XE_VM_BIND_OP_MAP:
	case DRM_XE_VM_BIND_OP_MAP_USERPTR:
		ops = drm_gpuvm_sm_map_ops_create(&vm->gpuvm, addr, range,
						  obj, bo_offset_or_userptr);
		break;
	case DRM_XE_VM_BIND_OP_UNMAP:
		ops = drm_gpuvm_sm_unmap_ops_create(&vm->gpuvm, addr, range);
		break;
	case DRM_XE_VM_BIND_OP_PREFETCH:
		ops = drm_gpuvm_prefetch_ops_create(&vm->gpuvm, addr, range);
		break;
	case DRM_XE_VM_BIND_OP_UNMAP_ALL:
		xe_assert(vm->xe, bo);

		err = xe_bo_lock(bo, true);
		if (err)
			return ERR_PTR(err);

		vm_bo = drm_gpuvm_bo_obtain(&vm->gpuvm, obj);
		if (IS_ERR(vm_bo)) {
			xe_bo_unlock(bo);
			return ERR_CAST(vm_bo);
		}

		ops = drm_gpuvm_bo_unmap_ops_create(vm_bo);
		drm_gpuvm_bo_put(vm_bo);
		xe_bo_unlock(bo);
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
		ops = ERR_PTR(-EINVAL);
	}
	if (IS_ERR(ops))
		return ops;

	drm_gpuva_for_each_op(__op, ops) {
		struct xe_vma_op *op = gpuva_op_to_vma_op(__op);

		if (__op->op == DRM_GPUVA_OP_MAP) {
			op->map.immediate =
				flags & DRM_XE_VM_BIND_FLAG_IMMEDIATE;
			op->map.read_only =
				flags & DRM_XE_VM_BIND_FLAG_READONLY;
			op->map.is_null = flags & DRM_XE_VM_BIND_FLAG_NULL;
			op->map.dumpable = flags & DRM_XE_VM_BIND_FLAG_DUMPABLE;
			op->map.pat_index = pat_index;
		} else if (__op->op == DRM_GPUVA_OP_PREFETCH) {
			op->prefetch.region = prefetch_region;
		}

		print_op(vm->xe, __op);
	}

	return ops;
}

static struct xe_vma *new_vma(struct xe_vm *vm, struct drm_gpuva_op_map *op,
			      u16 pat_index, unsigned int flags)
{
	struct xe_bo *bo = op->gem.obj ? gem_to_xe_bo(op->gem.obj) : NULL;
	struct drm_exec exec;
	struct xe_vma *vma;
	int err;

	lockdep_assert_held_write(&vm->lock);

	if (bo) {
		drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 0);
		drm_exec_until_all_locked(&exec) {
			err = 0;
			if (!bo->vm) {
				err = drm_exec_lock_obj(&exec, xe_vm_obj(vm));
				drm_exec_retry_on_contention(&exec);
			}
			if (!err) {
				err = drm_exec_lock_obj(&exec, &bo->ttm.base);
				drm_exec_retry_on_contention(&exec);
			}
			if (err) {
				drm_exec_fini(&exec);
				return ERR_PTR(err);
			}
		}
	}
	vma = xe_vma_create(vm, bo, op->gem.offset,
			    op->va.addr, op->va.addr +
			    op->va.range - 1, pat_index, flags);
	if (bo)
		drm_exec_fini(&exec);

	if (xe_vma_is_userptr(vma)) {
		err = xe_vma_userptr_pin_pages(to_userptr_vma(vma));
		if (err) {
			prep_vma_destroy(vm, vma, false);
			xe_vma_destroy_unlocked(vma);
			return ERR_PTR(err);
		}
	} else if (!xe_vma_has_no_bo(vma) && !bo->vm) {
		err = add_preempt_fences(vm, bo);
		if (err) {
			prep_vma_destroy(vm, vma, false);
			xe_vma_destroy_unlocked(vma);
			return ERR_PTR(err);
		}
	}

	return vma;
}

static u64 xe_vma_max_pte_size(struct xe_vma *vma)
{
	if (vma->gpuva.flags & XE_VMA_PTE_1G)
		return SZ_1G;
	else if (vma->gpuva.flags & (XE_VMA_PTE_2M | XE_VMA_PTE_COMPACT))
		return SZ_2M;
	else if (vma->gpuva.flags & XE_VMA_PTE_64K)
		return SZ_64K;
	else if (vma->gpuva.flags & XE_VMA_PTE_4K)
		return SZ_4K;

	return SZ_1G;	/* Uninitialized, used max size */
}

static void xe_vma_set_pte_size(struct xe_vma *vma, u64 size)
{
	switch (size) {
	case SZ_1G:
		vma->gpuva.flags |= XE_VMA_PTE_1G;
		break;
	case SZ_2M:
		vma->gpuva.flags |= XE_VMA_PTE_2M;
		break;
	case SZ_64K:
		vma->gpuva.flags |= XE_VMA_PTE_64K;
		break;
	case SZ_4K:
		vma->gpuva.flags |= XE_VMA_PTE_4K;
		break;
	}
}

static int xe_vma_op_commit(struct xe_vm *vm, struct xe_vma_op *op)
{
	int err = 0;

	lockdep_assert_held_write(&vm->lock);

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		err |= xe_vm_insert_vma(vm, op->map.vma);
		if (!err)
			op->flags |= XE_VMA_OP_COMMITTED;
		break;
	case DRM_GPUVA_OP_REMAP:
	{
		u8 tile_present =
			gpuva_to_vma(op->base.remap.unmap->va)->tile_present;

		prep_vma_destroy(vm, gpuva_to_vma(op->base.remap.unmap->va),
				 true);
		op->flags |= XE_VMA_OP_COMMITTED;

		if (op->remap.prev) {
			err |= xe_vm_insert_vma(vm, op->remap.prev);
			if (!err)
				op->flags |= XE_VMA_OP_PREV_COMMITTED;
			if (!err && op->remap.skip_prev) {
				op->remap.prev->tile_present =
					tile_present;
				op->remap.prev = NULL;
			}
		}
		if (op->remap.next) {
			err |= xe_vm_insert_vma(vm, op->remap.next);
			if (!err)
				op->flags |= XE_VMA_OP_NEXT_COMMITTED;
			if (!err && op->remap.skip_next) {
				op->remap.next->tile_present =
					tile_present;
				op->remap.next = NULL;
			}
		}

		/* Adjust for partial unbind after removin VMA from VM */
		if (!err) {
			op->base.remap.unmap->va->va.addr = op->remap.start;
			op->base.remap.unmap->va->va.range = op->remap.range;
		}
		break;
	}
	case DRM_GPUVA_OP_UNMAP:
		prep_vma_destroy(vm, gpuva_to_vma(op->base.unmap.va), true);
		op->flags |= XE_VMA_OP_COMMITTED;
		break;
	case DRM_GPUVA_OP_PREFETCH:
		op->flags |= XE_VMA_OP_COMMITTED;
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	return err;
}


static int vm_bind_ioctl_ops_parse(struct xe_vm *vm, struct xe_exec_queue *q,
				   struct drm_gpuva_ops *ops,
				   struct xe_sync_entry *syncs, u32 num_syncs,
				   struct list_head *ops_list, bool last)
{
	struct xe_device *xe = vm->xe;
	struct xe_vma_op *last_op = NULL;
	struct drm_gpuva_op *__op;
	int err = 0;

	lockdep_assert_held_write(&vm->lock);

	drm_gpuva_for_each_op(__op, ops) {
		struct xe_vma_op *op = gpuva_op_to_vma_op(__op);
		struct xe_vma *vma;
		bool first = list_empty(ops_list);
		unsigned int flags = 0;

		INIT_LIST_HEAD(&op->link);
		list_add_tail(&op->link, ops_list);

		if (first) {
			op->flags |= XE_VMA_OP_FIRST;
			op->num_syncs = num_syncs;
			op->syncs = syncs;
		}

		op->q = q;

		switch (op->base.op) {
		case DRM_GPUVA_OP_MAP:
		{
			flags |= op->map.read_only ?
				VMA_CREATE_FLAG_READ_ONLY : 0;
			flags |= op->map.is_null ?
				VMA_CREATE_FLAG_IS_NULL : 0;
			flags |= op->map.dumpable ?
				VMA_CREATE_FLAG_DUMPABLE : 0;

			vma = new_vma(vm, &op->base.map, op->map.pat_index,
				      flags);
			if (IS_ERR(vma))
				return PTR_ERR(vma);

			op->map.vma = vma;
			break;
		}
		case DRM_GPUVA_OP_REMAP:
		{
			struct xe_vma *old =
				gpuva_to_vma(op->base.remap.unmap->va);

			op->remap.start = xe_vma_start(old);
			op->remap.range = xe_vma_size(old);

			if (op->base.remap.prev) {
				flags |= op->base.remap.unmap->va->flags &
					XE_VMA_READ_ONLY ?
					VMA_CREATE_FLAG_READ_ONLY : 0;
				flags |= op->base.remap.unmap->va->flags &
					DRM_GPUVA_SPARSE ?
					VMA_CREATE_FLAG_IS_NULL : 0;
				flags |= op->base.remap.unmap->va->flags &
					XE_VMA_DUMPABLE ?
					VMA_CREATE_FLAG_DUMPABLE : 0;

				vma = new_vma(vm, op->base.remap.prev,
					      old->pat_index, flags);
				if (IS_ERR(vma))
					return PTR_ERR(vma);

				op->remap.prev = vma;

				/*
				 * Userptr creates a new SG mapping so
				 * we must also rebind.
				 */
				op->remap.skip_prev = !xe_vma_is_userptr(old) &&
					IS_ALIGNED(xe_vma_end(vma),
						   xe_vma_max_pte_size(old));
				if (op->remap.skip_prev) {
					xe_vma_set_pte_size(vma, xe_vma_max_pte_size(old));
					op->remap.range -=
						xe_vma_end(vma) -
						xe_vma_start(old);
					op->remap.start = xe_vma_end(vma);
					vm_dbg(&xe->drm, "REMAP:SKIP_PREV: addr=0x%016llx, range=0x%016llx",
					       (ULL)op->remap.start,
					       (ULL)op->remap.range);
				}
			}

			if (op->base.remap.next) {
				flags |= op->base.remap.unmap->va->flags &
					XE_VMA_READ_ONLY ?
					VMA_CREATE_FLAG_READ_ONLY : 0;
				flags |= op->base.remap.unmap->va->flags &
					DRM_GPUVA_SPARSE ?
					VMA_CREATE_FLAG_IS_NULL : 0;
				flags |= op->base.remap.unmap->va->flags &
					XE_VMA_DUMPABLE ?
					VMA_CREATE_FLAG_DUMPABLE : 0;

				vma = new_vma(vm, op->base.remap.next,
					      old->pat_index, flags);
				if (IS_ERR(vma))
					return PTR_ERR(vma);

				op->remap.next = vma;

				/*
				 * Userptr creates a new SG mapping so
				 * we must also rebind.
				 */
				op->remap.skip_next = !xe_vma_is_userptr(old) &&
					IS_ALIGNED(xe_vma_start(vma),
						   xe_vma_max_pte_size(old));
				if (op->remap.skip_next) {
					xe_vma_set_pte_size(vma, xe_vma_max_pte_size(old));
					op->remap.range -=
						xe_vma_end(old) -
						xe_vma_start(vma);
					vm_dbg(&xe->drm, "REMAP:SKIP_NEXT: addr=0x%016llx, range=0x%016llx",
					       (ULL)op->remap.start,
					       (ULL)op->remap.range);
				}
			}
			break;
		}
		case DRM_GPUVA_OP_UNMAP:
		case DRM_GPUVA_OP_PREFETCH:
			/* Nothing to do */
			break;
		default:
			drm_warn(&vm->xe->drm, "NOT POSSIBLE");
		}

		last_op = op;

		err = xe_vma_op_commit(vm, op);
		if (err)
			return err;
	}

	/* FIXME: Unhandled corner case */
	XE_WARN_ON(!last_op && last && !list_empty(ops_list));

	if (!last_op)
		return 0;

	last_op->ops = ops;
	if (last) {
		last_op->flags |= XE_VMA_OP_LAST;
		last_op->num_syncs = num_syncs;
		last_op->syncs = syncs;
	}

	return 0;
}

static int op_execute(struct drm_exec *exec, struct xe_vm *vm,
		      struct xe_vma *vma, struct xe_vma_op *op)
{
	int err;

	lockdep_assert_held_write(&vm->lock);

	err = xe_vm_lock_vma(exec, vma);
	if (err)
		return err;

	xe_vm_assert_held(vm);
	xe_bo_assert_held(xe_vma_bo(vma));

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		err = xe_vm_bind(vm, vma, op->q, xe_vma_bo(vma),
				 op->syncs, op->num_syncs,
				 op->map.immediate || !xe_vm_in_fault_mode(vm),
				 op->flags & XE_VMA_OP_FIRST,
				 op->flags & XE_VMA_OP_LAST);
		break;
	case DRM_GPUVA_OP_REMAP:
	{
		bool prev = !!op->remap.prev;
		bool next = !!op->remap.next;

		if (!op->remap.unmap_done) {
			if (prev || next)
				vma->gpuva.flags |= XE_VMA_FIRST_REBIND;
			err = xe_vm_unbind(vm, vma, op->q, op->syncs,
					   op->num_syncs,
					   op->flags & XE_VMA_OP_FIRST,
					   op->flags & XE_VMA_OP_LAST &&
					   !prev && !next);
			if (err)
				break;
			op->remap.unmap_done = true;
		}

		if (prev) {
			op->remap.prev->gpuva.flags |= XE_VMA_LAST_REBIND;
			err = xe_vm_bind(vm, op->remap.prev, op->q,
					 xe_vma_bo(op->remap.prev), op->syncs,
					 op->num_syncs, true, false,
					 op->flags & XE_VMA_OP_LAST && !next);
			op->remap.prev->gpuva.flags &= ~XE_VMA_LAST_REBIND;
			if (err)
				break;
			op->remap.prev = NULL;
		}

		if (next) {
			op->remap.next->gpuva.flags |= XE_VMA_LAST_REBIND;
			err = xe_vm_bind(vm, op->remap.next, op->q,
					 xe_vma_bo(op->remap.next),
					 op->syncs, op->num_syncs,
					 true, false,
					 op->flags & XE_VMA_OP_LAST);
			op->remap.next->gpuva.flags &= ~XE_VMA_LAST_REBIND;
			if (err)
				break;
			op->remap.next = NULL;
		}

		break;
	}
	case DRM_GPUVA_OP_UNMAP:
		err = xe_vm_unbind(vm, vma, op->q, op->syncs,
				   op->num_syncs, op->flags & XE_VMA_OP_FIRST,
				   op->flags & XE_VMA_OP_LAST);
		break;
	case DRM_GPUVA_OP_PREFETCH:
		err = xe_vm_prefetch(vm, vma, op->q, op->prefetch.region,
				     op->syncs, op->num_syncs,
				     op->flags & XE_VMA_OP_FIRST,
				     op->flags & XE_VMA_OP_LAST);
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	if (err)
		trace_xe_vma_fail(vma);

	return err;
}

static int __xe_vma_op_execute(struct xe_vm *vm, struct xe_vma *vma,
			       struct xe_vma_op *op)
{
	struct drm_exec exec;
	int err;

retry_userptr:
	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 0);
	drm_exec_until_all_locked(&exec) {
		err = op_execute(&exec, vm, vma, op);
		drm_exec_retry_on_contention(&exec);
		if (err)
			break;
	}
	drm_exec_fini(&exec);

	if (err == -EAGAIN) {
		lockdep_assert_held_write(&vm->lock);

		if (op->base.op == DRM_GPUVA_OP_REMAP) {
			if (!op->remap.unmap_done)
				vma = gpuva_to_vma(op->base.remap.unmap->va);
			else if (op->remap.prev)
				vma = op->remap.prev;
			else
				vma = op->remap.next;
		}

		if (xe_vma_is_userptr(vma)) {
			err = xe_vma_userptr_pin_pages(to_userptr_vma(vma));
			if (!err)
				goto retry_userptr;

			trace_xe_vma_fail(vma);
		}
	}

	return err;
}

static int xe_vma_op_execute(struct xe_vm *vm, struct xe_vma_op *op)
{
	int ret = 0;

	lockdep_assert_held_write(&vm->lock);

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		ret = __xe_vma_op_execute(vm, op->map.vma, op);
		break;
	case DRM_GPUVA_OP_REMAP:
	{
		struct xe_vma *vma;

		if (!op->remap.unmap_done)
			vma = gpuva_to_vma(op->base.remap.unmap->va);
		else if (op->remap.prev)
			vma = op->remap.prev;
		else
			vma = op->remap.next;

		ret = __xe_vma_op_execute(vm, vma, op);
		break;
	}
	case DRM_GPUVA_OP_UNMAP:
		ret = __xe_vma_op_execute(vm, gpuva_to_vma(op->base.unmap.va),
					  op);
		break;
	case DRM_GPUVA_OP_PREFETCH:
		ret = __xe_vma_op_execute(vm,
					  gpuva_to_vma(op->base.prefetch.va),
					  op);
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	return ret;
}

static void xe_vma_op_cleanup(struct xe_vm *vm, struct xe_vma_op *op)
{
	bool last = op->flags & XE_VMA_OP_LAST;

	if (last) {
		while (op->num_syncs--)
			xe_sync_entry_cleanup(&op->syncs[op->num_syncs]);
		kfree(op->syncs);
		if (op->q)
			xe_exec_queue_put(op->q);
	}
	if (!list_empty(&op->link))
		list_del(&op->link);
	if (op->ops)
		drm_gpuva_ops_free(&vm->gpuvm, op->ops);
	if (last)
		xe_vm_put(vm);
}

static void xe_vma_op_unwind(struct xe_vm *vm, struct xe_vma_op *op,
			     bool post_commit, bool prev_post_commit,
			     bool next_post_commit)
{
	lockdep_assert_held_write(&vm->lock);

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		if (op->map.vma) {
			prep_vma_destroy(vm, op->map.vma, post_commit);
			xe_vma_destroy_unlocked(op->map.vma);
		}
		break;
	case DRM_GPUVA_OP_UNMAP:
	{
		struct xe_vma *vma = gpuva_to_vma(op->base.unmap.va);

		if (vma) {
			down_read(&vm->userptr.notifier_lock);
			vma->gpuva.flags &= ~XE_VMA_DESTROYED;
			up_read(&vm->userptr.notifier_lock);
			if (post_commit)
				xe_vm_insert_vma(vm, vma);
		}
		break;
	}
	case DRM_GPUVA_OP_REMAP:
	{
		struct xe_vma *vma = gpuva_to_vma(op->base.remap.unmap->va);

		if (op->remap.prev) {
			prep_vma_destroy(vm, op->remap.prev, prev_post_commit);
			xe_vma_destroy_unlocked(op->remap.prev);
		}
		if (op->remap.next) {
			prep_vma_destroy(vm, op->remap.next, next_post_commit);
			xe_vma_destroy_unlocked(op->remap.next);
		}
		if (vma) {
			down_read(&vm->userptr.notifier_lock);
			vma->gpuva.flags &= ~XE_VMA_DESTROYED;
			up_read(&vm->userptr.notifier_lock);
			if (post_commit)
				xe_vm_insert_vma(vm, vma);
		}
		break;
	}
	case DRM_GPUVA_OP_PREFETCH:
		/* Nothing to do */
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}
}

static void vm_bind_ioctl_ops_unwind(struct xe_vm *vm,
				     struct drm_gpuva_ops **ops,
				     int num_ops_list)
{
	int i;

	for (i = num_ops_list - 1; i >= 0; --i) {
		struct drm_gpuva_ops *__ops = ops[i];
		struct drm_gpuva_op *__op;

		if (!__ops)
			continue;

		drm_gpuva_for_each_op_reverse(__op, __ops) {
			struct xe_vma_op *op = gpuva_op_to_vma_op(__op);

			xe_vma_op_unwind(vm, op,
					 op->flags & XE_VMA_OP_COMMITTED,
					 op->flags & XE_VMA_OP_PREV_COMMITTED,
					 op->flags & XE_VMA_OP_NEXT_COMMITTED);
		}

		drm_gpuva_ops_free(&vm->gpuvm, __ops);
	}
}

static int vm_bind_ioctl_ops_execute(struct xe_vm *vm,
				     struct list_head *ops_list)
{
	struct xe_vma_op *op, *next;
	int err;

	lockdep_assert_held_write(&vm->lock);

	list_for_each_entry_safe(op, next, ops_list, link) {
		err = xe_vma_op_execute(vm, op);
		if (err) {
			drm_warn(&vm->xe->drm, "VM op(%d) failed with %d",
				 op->base.op, err);
			/*
			 * FIXME: Killing VM rather than proper error handling
			 */
			xe_vm_kill(vm);
			return -ENOSPC;
		}
		xe_vma_op_cleanup(vm, op);
	}

	return 0;
}

#define SUPPORTED_FLAGS	\
	(DRM_XE_VM_BIND_FLAG_READONLY | \
	 DRM_XE_VM_BIND_FLAG_IMMEDIATE | \
	 DRM_XE_VM_BIND_FLAG_NULL | \
	 DRM_XE_VM_BIND_FLAG_DUMPABLE)
#define XE_64K_PAGE_MASK 0xffffull
#define ALL_DRM_XE_SYNCS_FLAGS (DRM_XE_SYNCS_FLAG_WAIT_FOR_OP)

static int vm_bind_ioctl_check_args(struct xe_device *xe,
				    struct drm_xe_vm_bind *args,
				    struct drm_xe_vm_bind_op **bind_ops)
{
	int err;
	int i;

	if (XE_IOCTL_DBG(xe, args->pad || args->pad2) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->extensions))
		return -EINVAL;

	if (args->num_binds > 1) {
		u64 __user *bind_user =
			u64_to_user_ptr(args->vector_of_binds);

		*bind_ops = kvmalloc_array(args->num_binds,
					   sizeof(struct drm_xe_vm_bind_op),
					   GFP_KERNEL | __GFP_ACCOUNT);
		if (!*bind_ops)
			return -ENOMEM;

		err = __copy_from_user(*bind_ops, bind_user,
				       sizeof(struct drm_xe_vm_bind_op) *
				       args->num_binds);
		if (XE_IOCTL_DBG(xe, err)) {
			err = -EFAULT;
			goto free_bind_ops;
		}
	} else {
		*bind_ops = &args->bind;
	}

	for (i = 0; i < args->num_binds; ++i) {
		u64 range = (*bind_ops)[i].range;
		u64 addr = (*bind_ops)[i].addr;
		u32 op = (*bind_ops)[i].op;
		u32 flags = (*bind_ops)[i].flags;
		u32 obj = (*bind_ops)[i].obj;
		u64 obj_offset = (*bind_ops)[i].obj_offset;
		u32 prefetch_region = (*bind_ops)[i].prefetch_mem_region_instance;
		bool is_null = flags & DRM_XE_VM_BIND_FLAG_NULL;
		u16 pat_index = (*bind_ops)[i].pat_index;
		u16 coh_mode;

		if (XE_IOCTL_DBG(xe, pat_index >= xe->pat.n_entries)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		pat_index = array_index_nospec(pat_index, xe->pat.n_entries);
		(*bind_ops)[i].pat_index = pat_index;
		coh_mode = xe_pat_index_get_coh_mode(xe, pat_index);
		if (XE_IOCTL_DBG(xe, !coh_mode)) { /* hw reserved */
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_WARN_ON(coh_mode > XE_COH_AT_LEAST_1WAY)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_DBG(xe, op > DRM_XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_DBG(xe, flags & ~SUPPORTED_FLAGS) ||
		    XE_IOCTL_DBG(xe, obj && is_null) ||
		    XE_IOCTL_DBG(xe, obj_offset && is_null) ||
		    XE_IOCTL_DBG(xe, op != DRM_XE_VM_BIND_OP_MAP &&
				 is_null) ||
		    XE_IOCTL_DBG(xe, !obj &&
				 op == DRM_XE_VM_BIND_OP_MAP &&
				 !is_null) ||
		    XE_IOCTL_DBG(xe, !obj &&
				 op == DRM_XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_DBG(xe, addr &&
				 op == DRM_XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_DBG(xe, range &&
				 op == DRM_XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_DBG(xe, obj &&
				 op == DRM_XE_VM_BIND_OP_MAP_USERPTR) ||
		    XE_IOCTL_DBG(xe, coh_mode == XE_COH_NONE &&
				 op == DRM_XE_VM_BIND_OP_MAP_USERPTR) ||
		    XE_IOCTL_DBG(xe, obj &&
				 op == DRM_XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_DBG(xe, prefetch_region &&
				 op != DRM_XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_DBG(xe, !(BIT(prefetch_region) &
				       xe->info.mem_region_mask)) ||
		    XE_IOCTL_DBG(xe, obj &&
				 op == DRM_XE_VM_BIND_OP_UNMAP)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_DBG(xe, obj_offset & ~PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, addr & ~PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, range & ~PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, !range &&
				 op != DRM_XE_VM_BIND_OP_UNMAP_ALL)) {
			err = -EINVAL;
			goto free_bind_ops;
		}
	}

	return 0;

free_bind_ops:
	if (args->num_binds > 1)
		kvfree(*bind_ops);
	return err;
}

static int vm_bind_ioctl_signal_fences(struct xe_vm *vm,
				       struct xe_exec_queue *q,
				       struct xe_sync_entry *syncs,
				       int num_syncs)
{
	struct dma_fence *fence;
	int i, err = 0;

	fence = xe_sync_in_fence_get(syncs, num_syncs,
				     to_wait_exec_queue(vm, q), vm);
	if (IS_ERR(fence))
		return PTR_ERR(fence);

	for (i = 0; i < num_syncs; i++)
		xe_sync_entry_signal(&syncs[i], fence);

	xe_exec_queue_last_fence_set(to_wait_exec_queue(vm, q), vm,
				     fence);
	dma_fence_put(fence);

	return err;
}

int xe_vm_bind_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_vm_bind *args = data;
	struct drm_xe_sync __user *syncs_user;
	struct xe_bo **bos = NULL;
	struct drm_gpuva_ops **ops = NULL;
	struct xe_vm *vm;
	struct xe_exec_queue *q = NULL;
	u32 num_syncs, num_ufence = 0;
	struct xe_sync_entry *syncs = NULL;
	struct drm_xe_vm_bind_op *bind_ops;
	LIST_HEAD(ops_list);
	int err;
	int i;

	err = vm_bind_ioctl_check_args(xe, args, &bind_ops);
	if (err)
		return err;

	if (args->exec_queue_id) {
		q = xe_exec_queue_lookup(xef, args->exec_queue_id);
		if (XE_IOCTL_DBG(xe, !q)) {
			err = -ENOENT;
			goto free_objs;
		}

		if (XE_IOCTL_DBG(xe, !(q->flags & EXEC_QUEUE_FLAG_VM))) {
			err = -EINVAL;
			goto put_exec_queue;
		}
	}

	vm = xe_vm_lookup(xef, args->vm_id);
	if (XE_IOCTL_DBG(xe, !vm)) {
		err = -EINVAL;
		goto put_exec_queue;
	}

	err = down_write_killable(&vm->lock);
	if (err)
		goto put_vm;

	if (XE_IOCTL_DBG(xe, xe_vm_is_closed_or_banned(vm))) {
		err = -ENOENT;
		goto release_vm_lock;
	}

	for (i = 0; i < args->num_binds; ++i) {
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;

		if (XE_IOCTL_DBG(xe, range > vm->size) ||
		    XE_IOCTL_DBG(xe, addr > vm->size - range)) {
			err = -EINVAL;
			goto release_vm_lock;
		}
	}

	if (args->num_binds) {
		bos = kvcalloc(args->num_binds, sizeof(*bos),
			       GFP_KERNEL | __GFP_ACCOUNT);
		if (!bos) {
			err = -ENOMEM;
			goto release_vm_lock;
		}

		ops = kvcalloc(args->num_binds, sizeof(*ops),
			       GFP_KERNEL | __GFP_ACCOUNT);
		if (!ops) {
			err = -ENOMEM;
			goto release_vm_lock;
		}
	}

	for (i = 0; i < args->num_binds; ++i) {
		struct drm_gem_object *gem_obj;
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;
		u32 obj = bind_ops[i].obj;
		u64 obj_offset = bind_ops[i].obj_offset;
		u16 pat_index = bind_ops[i].pat_index;
		u16 coh_mode;

		if (!obj)
			continue;

		gem_obj = drm_gem_object_lookup(file, obj);
		if (XE_IOCTL_DBG(xe, !gem_obj)) {
			err = -ENOENT;
			goto put_obj;
		}
		bos[i] = gem_to_xe_bo(gem_obj);

		if (XE_IOCTL_DBG(xe, range > bos[i]->size) ||
		    XE_IOCTL_DBG(xe, obj_offset >
				 bos[i]->size - range)) {
			err = -EINVAL;
			goto put_obj;
		}

		if (bos[i]->flags & XE_BO_FLAG_INTERNAL_64K) {
			if (XE_IOCTL_DBG(xe, obj_offset &
					 XE_64K_PAGE_MASK) ||
			    XE_IOCTL_DBG(xe, addr & XE_64K_PAGE_MASK) ||
			    XE_IOCTL_DBG(xe, range & XE_64K_PAGE_MASK)) {
				err = -EINVAL;
				goto put_obj;
			}
		}

		coh_mode = xe_pat_index_get_coh_mode(xe, pat_index);
		if (bos[i]->cpu_caching) {
			if (XE_IOCTL_DBG(xe, coh_mode == XE_COH_NONE &&
					 bos[i]->cpu_caching == DRM_XE_GEM_CPU_CACHING_WB)) {
				err = -EINVAL;
				goto put_obj;
			}
		} else if (XE_IOCTL_DBG(xe, coh_mode == XE_COH_NONE)) {
			/*
			 * Imported dma-buf from a different device should
			 * require 1way or 2way coherency since we don't know
			 * how it was mapped on the CPU. Just assume is it
			 * potentially cached on CPU side.
			 */
			err = -EINVAL;
			goto put_obj;
		}
	}

	if (args->num_syncs) {
		syncs = kcalloc(args->num_syncs, sizeof(*syncs), GFP_KERNEL);
		if (!syncs) {
			err = -ENOMEM;
			goto put_obj;
		}
	}

	syncs_user = u64_to_user_ptr(args->syncs);
	for (num_syncs = 0; num_syncs < args->num_syncs; num_syncs++) {
		err = xe_sync_entry_parse(xe, xef, &syncs[num_syncs],
					  &syncs_user[num_syncs],
					  (xe_vm_in_lr_mode(vm) ?
					   SYNC_PARSE_FLAG_LR_MODE : 0) |
					  (!args->num_binds ?
					   SYNC_PARSE_FLAG_DISALLOW_USER_FENCE : 0));
		if (err)
			goto free_syncs;

		if (xe_sync_is_ufence(&syncs[num_syncs]))
			num_ufence++;
	}

	if (XE_IOCTL_DBG(xe, num_ufence > 1)) {
		err = -EINVAL;
		goto free_syncs;
	}

	if (!args->num_binds) {
		err = -ENODATA;
		goto free_syncs;
	}

	for (i = 0; i < args->num_binds; ++i) {
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;
		u32 op = bind_ops[i].op;
		u32 flags = bind_ops[i].flags;
		u64 obj_offset = bind_ops[i].obj_offset;
		u32 prefetch_region = bind_ops[i].prefetch_mem_region_instance;
		u16 pat_index = bind_ops[i].pat_index;

		ops[i] = vm_bind_ioctl_ops_create(vm, bos[i], obj_offset,
						  addr, range, op, flags,
						  prefetch_region, pat_index);
		if (IS_ERR(ops[i])) {
			err = PTR_ERR(ops[i]);
			ops[i] = NULL;
			goto unwind_ops;
		}

		err = vm_bind_ioctl_ops_parse(vm, q, ops[i], syncs, num_syncs,
					      &ops_list,
					      i == args->num_binds - 1);
		if (err)
			goto unwind_ops;
	}

	/* Nothing to do */
	if (list_empty(&ops_list)) {
		err = -ENODATA;
		goto unwind_ops;
	}

	xe_vm_get(vm);
	if (q)
		xe_exec_queue_get(q);

	err = vm_bind_ioctl_ops_execute(vm, &ops_list);

	up_write(&vm->lock);

	if (q)
		xe_exec_queue_put(q);
	xe_vm_put(vm);

	for (i = 0; bos && i < args->num_binds; ++i)
		xe_bo_put(bos[i]);

	kvfree(bos);
	kvfree(ops);
	if (args->num_binds > 1)
		kvfree(bind_ops);

	return err;

unwind_ops:
	vm_bind_ioctl_ops_unwind(vm, ops, args->num_binds);
free_syncs:
	if (err == -ENODATA)
		err = vm_bind_ioctl_signal_fences(vm, q, syncs, num_syncs);
	while (num_syncs--)
		xe_sync_entry_cleanup(&syncs[num_syncs]);

	kfree(syncs);
put_obj:
	for (i = 0; i < args->num_binds; ++i)
		xe_bo_put(bos[i]);
release_vm_lock:
	up_write(&vm->lock);
put_vm:
	xe_vm_put(vm);
put_exec_queue:
	if (q)
		xe_exec_queue_put(q);
free_objs:
	kvfree(bos);
	kvfree(ops);
	if (args->num_binds > 1)
		kvfree(bind_ops);
	return err;
}

/**
 * xe_vm_lock() - Lock the vm's dma_resv object
 * @vm: The struct xe_vm whose lock is to be locked
 * @intr: Whether to perform any wait interruptible
 *
 * Return: 0 on success, -EINTR if @intr is true and the wait for a
 * contended lock was interrupted. If @intr is false, the function
 * always returns 0.
 */
int xe_vm_lock(struct xe_vm *vm, bool intr)
{
	if (intr)
		return dma_resv_lock_interruptible(xe_vm_resv(vm), NULL);

	return dma_resv_lock(xe_vm_resv(vm), NULL);
}

/**
 * xe_vm_unlock() - Unlock the vm's dma_resv object
 * @vm: The struct xe_vm whose lock is to be released.
 *
 * Unlock a buffer object lock that was locked by xe_vm_lock().
 */
void xe_vm_unlock(struct xe_vm *vm)
{
	dma_resv_unlock(xe_vm_resv(vm));
}

/**
 * xe_vm_invalidate_vma - invalidate GPU mappings for VMA without a lock
 * @vma: VMA to invalidate
 *
 * Walks a list of page tables leaves which it memset the entries owned by this
 * VMA to zero, invalidates the TLBs, and block until TLBs invalidation is
 * complete.
 *
 * Returns 0 for success, negative error code otherwise.
 */
int xe_vm_invalidate_vma(struct xe_vma *vma)
{
	struct xe_device *xe = xe_vma_vm(vma)->xe;
	struct xe_tile *tile;
	u32 tile_needs_invalidate = 0;
	int seqno[XE_MAX_TILES_PER_DEVICE];
	u8 id;
	int ret;

	xe_assert(xe, !xe_vma_is_null(vma));
	trace_xe_vma_invalidate(vma);

	vm_dbg(&xe_vma_vm(vma)->xe->drm,
	       "INVALIDATE: addr=0x%016llx, range=0x%016llx",
		xe_vma_start(vma), xe_vma_size(vma));

	/* Check that we don't race with page-table updates */
	if (IS_ENABLED(CONFIG_PROVE_LOCKING)) {
		if (xe_vma_is_userptr(vma)) {
			WARN_ON_ONCE(!mmu_interval_check_retry
				     (&to_userptr_vma(vma)->userptr.notifier,
				      to_userptr_vma(vma)->userptr.notifier_seq));
			WARN_ON_ONCE(!dma_resv_test_signaled(xe_vm_resv(xe_vma_vm(vma)),
							     DMA_RESV_USAGE_BOOKKEEP));

		} else {
			xe_bo_assert_held(xe_vma_bo(vma));
		}
	}

	for_each_tile(tile, xe, id) {
		if (xe_pt_zap_ptes(tile, vma)) {
			tile_needs_invalidate |= BIT(id);
			xe_device_wmb(xe);
			/*
			 * FIXME: We potentially need to invalidate multiple
			 * GTs within the tile
			 */
			seqno[id] = xe_gt_tlb_invalidation_vma(tile->primary_gt, NULL, vma);
			if (seqno[id] < 0)
				return seqno[id];
		}
	}

	for_each_tile(tile, xe, id) {
		if (tile_needs_invalidate & BIT(id)) {
			ret = xe_gt_tlb_invalidation_wait(tile->primary_gt, seqno[id]);
			if (ret < 0)
				return ret;
		}
	}

	vma->tile_invalidated = vma->tile_mask;

	return 0;
}

int xe_analyze_vm(struct drm_printer *p, struct xe_vm *vm, int gt_id)
{
	struct drm_gpuva *gpuva;
	bool is_vram;
	uint64_t addr;

	if (!down_read_trylock(&vm->lock)) {
		drm_printf(p, " Failed to acquire VM lock to dump capture");
		return 0;
	}
	if (vm->pt_root[gt_id]) {
		addr = xe_bo_addr(vm->pt_root[gt_id]->bo, 0, XE_PAGE_SIZE);
		is_vram = xe_bo_is_vram(vm->pt_root[gt_id]->bo);
		drm_printf(p, " VM root: A:0x%llx %s\n", addr,
			   is_vram ? "VRAM" : "SYS");
	}

	drm_gpuvm_for_each_va(gpuva, &vm->gpuvm) {
		struct xe_vma *vma = gpuva_to_vma(gpuva);
		bool is_userptr = xe_vma_is_userptr(vma);
		bool is_null = xe_vma_is_null(vma);

		if (is_null) {
			addr = 0;
		} else if (is_userptr) {
			struct sg_table *sg = to_userptr_vma(vma)->userptr.sg;
			struct xe_res_cursor cur;

			if (sg) {
				xe_res_first_sg(sg, 0, XE_PAGE_SIZE, &cur);
				addr = xe_res_dma(&cur);
			} else {
				addr = 0;
			}
		} else {
			addr = __xe_bo_addr(xe_vma_bo(vma), 0, XE_PAGE_SIZE);
			is_vram = xe_bo_is_vram(xe_vma_bo(vma));
		}
		drm_printf(p, " [%016llx-%016llx] S:0x%016llx A:%016llx %s\n",
			   xe_vma_start(vma), xe_vma_end(vma) - 1,
			   xe_vma_size(vma),
			   addr, is_null ? "NULL" : is_userptr ? "USR" :
			   is_vram ? "VRAM" : "SYS");
	}
	up_read(&vm->lock);

	return 0;
}

struct xe_vm_snapshot {
	unsigned long num_snaps;
	struct {
		u64 ofs, bo_ofs;
		unsigned long len;
		struct xe_bo *bo;
		void *data;
		struct mm_struct *mm;
	} snap[];
};

struct xe_vm_snapshot *xe_vm_snapshot_capture(struct xe_vm *vm)
{
	unsigned long num_snaps = 0, i;
	struct xe_vm_snapshot *snap = NULL;
	struct drm_gpuva *gpuva;

	if (!vm)
		return NULL;

	mutex_lock(&vm->snap_mutex);
	drm_gpuvm_for_each_va(gpuva, &vm->gpuvm) {
		if (gpuva->flags & XE_VMA_DUMPABLE)
			num_snaps++;
	}

	if (num_snaps)
		snap = kvzalloc(offsetof(struct xe_vm_snapshot, snap[num_snaps]), GFP_NOWAIT);
	if (!snap) {
		snap = num_snaps ? ERR_PTR(-ENOMEM) : ERR_PTR(-ENODEV);
		goto out_unlock;
	}

	snap->num_snaps = num_snaps;
	i = 0;
	drm_gpuvm_for_each_va(gpuva, &vm->gpuvm) {
		struct xe_vma *vma = gpuva_to_vma(gpuva);
		struct xe_bo *bo = vma->gpuva.gem.obj ?
			gem_to_xe_bo(vma->gpuva.gem.obj) : NULL;

		if (!(gpuva->flags & XE_VMA_DUMPABLE))
			continue;

		snap->snap[i].ofs = xe_vma_start(vma);
		snap->snap[i].len = xe_vma_size(vma);
		if (bo) {
			snap->snap[i].bo = xe_bo_get(bo);
			snap->snap[i].bo_ofs = xe_vma_bo_offset(vma);
		} else if (xe_vma_is_userptr(vma)) {
			struct mm_struct *mm =
				to_userptr_vma(vma)->userptr.notifier.mm;

			if (mmget_not_zero(mm))
				snap->snap[i].mm = mm;
			else
				snap->snap[i].data = ERR_PTR(-EFAULT);

			snap->snap[i].bo_ofs = xe_vma_userptr(vma);
		} else {
			snap->snap[i].data = ERR_PTR(-ENOENT);
		}
		i++;
	}

out_unlock:
	mutex_unlock(&vm->snap_mutex);
	return snap;
}

void xe_vm_snapshot_capture_delayed(struct xe_vm_snapshot *snap)
{
	if (IS_ERR_OR_NULL(snap))
		return;

	for (int i = 0; i < snap->num_snaps; i++) {
		struct xe_bo *bo = snap->snap[i].bo;
		struct iosys_map src;
		int err;

		if (IS_ERR(snap->snap[i].data))
			continue;

		snap->snap[i].data = kvmalloc(snap->snap[i].len, GFP_USER);
		if (!snap->snap[i].data) {
			snap->snap[i].data = ERR_PTR(-ENOMEM);
			goto cleanup_bo;
		}

		if (bo) {
			dma_resv_lock(bo->ttm.base.resv, NULL);
			err = ttm_bo_vmap(&bo->ttm, &src);
			if (!err) {
				xe_map_memcpy_from(xe_bo_device(bo),
						   snap->snap[i].data,
						   &src, snap->snap[i].bo_ofs,
						   snap->snap[i].len);
				ttm_bo_vunmap(&bo->ttm, &src);
			}
			dma_resv_unlock(bo->ttm.base.resv);
		} else {
			void __user *userptr = (void __user *)(size_t)snap->snap[i].bo_ofs;

			kthread_use_mm(snap->snap[i].mm);
			if (!copy_from_user(snap->snap[i].data, userptr, snap->snap[i].len))
				err = 0;
			else
				err = -EFAULT;
			kthread_unuse_mm(snap->snap[i].mm);

			mmput(snap->snap[i].mm);
			snap->snap[i].mm = NULL;
		}

		if (err) {
			kvfree(snap->snap[i].data);
			snap->snap[i].data = ERR_PTR(err);
		}

cleanup_bo:
		xe_bo_put(bo);
		snap->snap[i].bo = NULL;
	}
}

void xe_vm_snapshot_print(struct xe_vm_snapshot *snap, struct drm_printer *p)
{
	unsigned long i, j;

	if (IS_ERR_OR_NULL(snap)) {
		drm_printf(p, "[0].error: %li\n", PTR_ERR(snap));
		return;
	}

	for (i = 0; i < snap->num_snaps; i++) {
		drm_printf(p, "[%llx].length: 0x%lx\n", snap->snap[i].ofs, snap->snap[i].len);

		if (IS_ERR(snap->snap[i].data)) {
			drm_printf(p, "[%llx].error: %li\n", snap->snap[i].ofs,
				   PTR_ERR(snap->snap[i].data));
			continue;
		}

		drm_printf(p, "[%llx].data: ", snap->snap[i].ofs);

		for (j = 0; j < snap->snap[i].len; j += sizeof(u32)) {
			u32 *val = snap->snap[i].data + j;
			char dumped[ASCII85_BUFSZ];

			drm_puts(p, ascii85_encode(*val, dumped));
		}

		drm_puts(p, "\n");
	}
}

void xe_vm_snapshot_free(struct xe_vm_snapshot *snap)
{
	unsigned long i;

	if (IS_ERR_OR_NULL(snap))
		return;

	for (i = 0; i < snap->num_snaps; i++) {
		if (!IS_ERR(snap->snap[i].data))
			kvfree(snap->snap[i].data);
		xe_bo_put(snap->snap[i].bo);
		if (snap->snap[i].mm)
			mmput(snap->snap[i].mm);
	}
	kvfree(snap);
}
