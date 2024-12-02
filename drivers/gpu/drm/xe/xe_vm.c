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
#include <uapi/drm/xe_drm.h>
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
#include "xe_trace_bo.h"
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

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link) {
		if (!q->lr.pfence ||
		    test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			     &q->lr.pfence->flags)) {
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

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link) {
		if (q->lr.pfence) {
			long timeout = dma_fence_wait(q->lr.pfence, false);

			/* Only -ETIME on fence indicates VM needs to be killed */
			if (timeout < 0 || q->lr.pfence->error == -ETIME)
				return -ETIME;

			dma_fence_put(q->lr.pfence);
			q->lr.pfence = NULL;
		}
	}

	return 0;
}

static bool xe_vm_is_idle(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	xe_vm_assert_held(vm);
	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link) {
		if (!xe_exec_queue_is_idle(q))
			return false;
	}

	return true;
}

static void arm_preempt_fences(struct xe_vm *vm, struct list_head *list)
{
	struct list_head *link;
	struct xe_exec_queue *q;

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link) {
		struct dma_fence *fence;

		link = list->next;
		xe_assert(vm->xe, link != list);

		fence = xe_preempt_fence_arm(to_preempt_fence_from_link(link),
					     q, q->lr.context,
					     ++q->lr.seqno);
		dma_fence_put(q->lr.pfence);
		q->lr.pfence = fence;
	}
}

static int add_preempt_fences(struct xe_vm *vm, struct xe_bo *bo)
{
	struct xe_exec_queue *q;
	int err;

	xe_bo_assert_held(bo);

	if (!vm->preempt.num_exec_queues)
		return 0;

	err = dma_resv_reserve_fences(bo->ttm.base.resv, vm->preempt.num_exec_queues);
	if (err)
		return err;

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link)
		if (q->lr.pfence) {
			dma_resv_add_fence(bo->ttm.base.resv,
					   q->lr.pfence,
					   DMA_RESV_USAGE_BOOKKEEP);
		}

	return 0;
}

static void resume_and_reinstall_preempt_fences(struct xe_vm *vm,
						struct drm_exec *exec)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link) {
		q->ops->resume(q);

		drm_gpuvm_resv_add_fence(&vm->gpuvm, exec, q->lr.pfence,
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

	pfence = xe_preempt_fence_create(q, q->lr.context,
					 ++q->lr.seqno);
	if (!pfence) {
		err = -ENOMEM;
		goto out_fini;
	}

	list_add(&q->lr.link, &vm->preempt.exec_queues);
	++vm->preempt.num_exec_queues;
	q->lr.pfence = pfence;

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
 *
 * Note that this function might be called multiple times on the same queue.
 */
void xe_vm_remove_compute_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q)
{
	if (!xe_vm_in_preempt_fence_mode(vm))
		return;

	down_write(&vm->lock);
	if (!list_empty(&q->lr.link)) {
		list_del_init(&q->lr.link);
		--vm->preempt.num_exec_queues;
	}
	if (q->lr.pfence) {
		dma_fence_enable_sw_signaling(q->lr.pfence);
		dma_fence_put(q->lr.pfence);
		q->lr.pfence = NULL;
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

/**
 * xe_vm_kill() - VM Kill
 * @vm: The VM.
 * @unlocked: Flag indicates the VM's dma-resv is not held
 *
 * Kill the VM by setting banned flag indicated VM is no longer available for
 * use. If in preempt fence mode, also kill all exec queue attached to the VM.
 */
void xe_vm_kill(struct xe_vm *vm, bool unlocked)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);

	if (unlocked)
		xe_vm_lock(vm, false);

	vm->flags |= XE_VM_FLAG_BANNED;
	trace_xe_vm_kill(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link)
		q->ops->kill(q);

	if (unlocked)
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
		xe_vm_kill(vm, true);
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

static int xe_vma_ops_alloc(struct xe_vma_ops *vops, bool array_of_binds)
{
	int i;

	for (i = 0; i < XE_MAX_TILES_PER_DEVICE; ++i) {
		if (!vops->pt_update_ops[i].num_ops)
			continue;

		vops->pt_update_ops[i].ops =
			kmalloc_array(vops->pt_update_ops[i].num_ops,
				      sizeof(*vops->pt_update_ops[i].ops),
				      GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
		if (!vops->pt_update_ops[i].ops)
			return array_of_binds ? -ENOBUFS : -ENOMEM;
	}

	return 0;
}
ALLOW_ERROR_INJECTION(xe_vma_ops_alloc, ERRNO);

static void xe_vma_ops_fini(struct xe_vma_ops *vops)
{
	int i;

	for (i = 0; i < XE_MAX_TILES_PER_DEVICE; ++i)
		kfree(vops->pt_update_ops[i].ops);
}

static void xe_vma_ops_incr_pt_update_ops(struct xe_vma_ops *vops, u8 tile_mask)
{
	int i;

	for (i = 0; i < XE_MAX_TILES_PER_DEVICE; ++i)
		if (BIT(i) & tile_mask)
			++vops->pt_update_ops[i].num_ops;
}

static void xe_vm_populate_rebind(struct xe_vma_op *op, struct xe_vma *vma,
				  u8 tile_mask)
{
	INIT_LIST_HEAD(&op->link);
	op->tile_mask = tile_mask;
	op->base.op = DRM_GPUVA_OP_MAP;
	op->base.map.va.addr = vma->gpuva.va.addr;
	op->base.map.va.range = vma->gpuva.va.range;
	op->base.map.gem.obj = vma->gpuva.gem.obj;
	op->base.map.gem.offset = vma->gpuva.gem.offset;
	op->map.vma = vma;
	op->map.immediate = true;
	op->map.dumpable = vma->gpuva.flags & XE_VMA_DUMPABLE;
	op->map.is_null = xe_vma_is_null(vma);
}

static int xe_vm_ops_add_rebind(struct xe_vma_ops *vops, struct xe_vma *vma,
				u8 tile_mask)
{
	struct xe_vma_op *op;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	xe_vm_populate_rebind(op, vma, tile_mask);
	list_add_tail(&op->link, &vops->list);
	xe_vma_ops_incr_pt_update_ops(vops, tile_mask);

	return 0;
}

static struct dma_fence *ops_execute(struct xe_vm *vm,
				     struct xe_vma_ops *vops);
static void xe_vma_ops_init(struct xe_vma_ops *vops, struct xe_vm *vm,
			    struct xe_exec_queue *q,
			    struct xe_sync_entry *syncs, u32 num_syncs);

int xe_vm_rebind(struct xe_vm *vm, bool rebind_worker)
{
	struct dma_fence *fence;
	struct xe_vma *vma, *next;
	struct xe_vma_ops vops;
	struct xe_vma_op *op, *next_op;
	int err, i;

	lockdep_assert_held(&vm->lock);
	if ((xe_vm_in_lr_mode(vm) && !rebind_worker) ||
	    list_empty(&vm->rebind_list))
		return 0;

	xe_vma_ops_init(&vops, vm, NULL, NULL, 0);
	for (i = 0; i < XE_MAX_TILES_PER_DEVICE; ++i)
		vops.pt_update_ops[i].wait_vm_bookkeep = true;

	xe_vm_assert_held(vm);
	list_for_each_entry(vma, &vm->rebind_list, combined_links.rebind) {
		xe_assert(vm->xe, vma->tile_present);

		if (rebind_worker)
			trace_xe_vma_rebind_worker(vma);
		else
			trace_xe_vma_rebind_exec(vma);

		err = xe_vm_ops_add_rebind(&vops, vma,
					   vma->tile_present);
		if (err)
			goto free_ops;
	}

	err = xe_vma_ops_alloc(&vops, false);
	if (err)
		goto free_ops;

	fence = ops_execute(vm, &vops);
	if (IS_ERR(fence)) {
		err = PTR_ERR(fence);
	} else {
		dma_fence_put(fence);
		list_for_each_entry_safe(vma, next, &vm->rebind_list,
					 combined_links.rebind)
			list_del_init(&vma->combined_links.rebind);
	}
free_ops:
	list_for_each_entry_safe(op, next_op, &vops.list, link) {
		list_del(&op->link);
		kfree(op);
	}
	xe_vma_ops_fini(&vops);

	return err;
}

struct dma_fence *xe_vma_rebind(struct xe_vm *vm, struct xe_vma *vma, u8 tile_mask)
{
	struct dma_fence *fence = NULL;
	struct xe_vma_ops vops;
	struct xe_vma_op *op, *next_op;
	struct xe_tile *tile;
	u8 id;
	int err;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);
	xe_assert(vm->xe, xe_vm_in_fault_mode(vm));

	xe_vma_ops_init(&vops, vm, NULL, NULL, 0);
	for_each_tile(tile, vm->xe, id) {
		vops.pt_update_ops[id].wait_vm_bookkeep = true;
		vops.pt_update_ops[tile->id].q =
			xe_tile_migrate_exec_queue(tile);
	}

	err = xe_vm_ops_add_rebind(&vops, vma, tile_mask);
	if (err)
		return ERR_PTR(err);

	err = xe_vma_ops_alloc(&vops, false);
	if (err) {
		fence = ERR_PTR(err);
		goto free_ops;
	}

	fence = ops_execute(vm, &vops);

free_ops:
	list_for_each_entry_safe(op, next_op, &vops.list, link) {
		list_del(&op->link);
		kfree(op);
	}
	xe_vma_ops_fini(&vops);

	return fence;
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

	if (vm->xe->info.has_atomic_enable_pte_bit)
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

static u64 pde_encode_pat_index(u16 pat_index)
{
	u64 pte = 0;

	if (pat_index & BIT(0))
		pte |= XE_PPGTT_PTE_PAT0;

	if (pat_index & BIT(1))
		pte |= XE_PPGTT_PTE_PAT1;

	return pte;
}

static u64 pte_encode_pat_index(u16 pat_index, u32 pt_level)
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
	u64 pde;

	pde = xe_bo_addr(bo, bo_offset, XE_PAGE_SIZE);
	pde |= XE_PAGE_PRESENT | XE_PAGE_RW;
	pde |= pde_encode_pat_index(pat_index);

	return pde;
}

static u64 xelp_pte_encode_bo(struct xe_bo *bo, u64 bo_offset,
			      u16 pat_index, u32 pt_level)
{
	u64 pte;

	pte = xe_bo_addr(bo, bo_offset, XE_PAGE_SIZE);
	pte |= XE_PAGE_PRESENT | XE_PAGE_RW;
	pte |= pte_encode_pat_index(pat_index, pt_level);
	pte |= pte_encode_ps(pt_level);

	if (xe_bo_is_vram(bo) || xe_bo_is_stolen_devmem(bo))
		pte |= XE_PPGTT_PTE_DM;

	return pte;
}

static u64 xelp_pte_encode_vma(u64 pte, struct xe_vma *vma,
			       u16 pat_index, u32 pt_level)
{
	pte |= XE_PAGE_PRESENT;

	if (likely(!xe_vma_read_only(vma)))
		pte |= XE_PAGE_RW;

	pte |= pte_encode_pat_index(pat_index, pt_level);
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
	pte |= pte_encode_pat_index(pat_index, pt_level);
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

static void vm_destroy_work_func(struct work_struct *w);

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
ALLOW_ERROR_INJECTION(xe_vm_create_scratch, ERRNO);

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

	ttm_lru_bulk_move_init(&vm->lru_bulk_move);

	INIT_WORK(&vm->destroy_work, vm_destroy_work_func);

	INIT_LIST_HEAD(&vm->preempt.exec_queues);
	vm->preempt.min_run_period_ms = 10;	/* FIXME: Wire up to uAPI */

	for_each_tile(tile, xe, id)
		xe_range_fence_tree_init(&vm->rftree[id]);

	vm->pt_ops = &xelp_pt_ops;

	/*
	 * Long-running workloads are not protected by the scheduler references.
	 * By design, run_job for long-running workloads returns NULL and the
	 * scheduler drops all the references of it, hence protecting the VM
	 * for this case is necessary.
	 */
	if (flags & XE_VM_FLAG_LR_MODE)
		xe_pm_runtime_get_noresume(xe);

	vm_resv_obj = drm_gpuvm_resv_object_alloc(&xe->drm);
	if (!vm_resv_obj) {
		err = -ENOMEM;
		goto err_no_resv;
	}

	drm_gpuvm_init(&vm->gpuvm, "Xe VM", DRM_GPUVM_RESV_PROTECTED, &xe->drm,
		       vm_resv_obj, 0, vm->size, 0, 0, &gpuvm_ops);

	drm_gem_object_put(vm_resv_obj);

	err = xe_vm_lock(vm, true);
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
	xe_vm_unlock(vm);

	/* Kernel migration VM shouldn't have a circular loop.. */
	if (!(flags & XE_VM_FLAG_MIGRATION)) {
		for_each_tile(tile, xe, id) {
			struct xe_exec_queue *q;
			u32 create_flags = EXEC_QUEUE_FLAG_VM;

			if (!vm->pt_root[id])
				continue;

			q = xe_exec_queue_create_bind(xe, tile, create_flags, 0);
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

	trace_xe_vm_create(vm);

	return vm;

err_unlock_close:
	xe_vm_unlock(vm);
err_close:
	xe_vm_close_and_put(vm);
	return ERR_PTR(err);

err_no_resv:
	mutex_destroy(&vm->snap_mutex);
	for_each_tile(tile, xe, id)
		xe_range_fence_tree_fini(&vm->rftree[id]);
	ttm_lru_bulk_move_fini(&xe->ttm, &vm->lru_bulk_move);
	kfree(vm);
	if (flags & XE_VM_FLAG_LR_MODE)
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

	down_write(&xe->usm.lock);
	if (vm->usm.asid) {
		void *lookup;

		xe_assert(xe, xe->info.has_asid);
		xe_assert(xe, !(vm->flags & XE_VM_FLAG_MIGRATION));

		lookup = xa_erase(&xe->usm.asid_to_vm, vm->usm.asid);
		xe_assert(xe, lookup == vm);
	}
	up_write(&xe->usm.lock);

	for_each_tile(tile, xe, id)
		xe_range_fence_tree_fini(&vm->rftree[id]);

	xe_vm_put(vm);
}

static void vm_destroy_work_func(struct work_struct *w)
{
	struct xe_vm *vm =
		container_of(w, struct xe_vm, destroy_work);
	struct xe_device *xe = vm->xe;
	struct xe_tile *tile;
	u8 id;

	/* xe_vm_close_and_put was not called? */
	xe_assert(xe, !vm->size);

	if (xe_vm_in_preempt_fence_mode(vm))
		flush_work(&vm->preempt.rebind_work);

	mutex_destroy(&vm->snap_mutex);

	if (vm->flags & XE_VM_FLAG_LR_MODE)
		xe_pm_runtime_put(xe);

	for_each_tile(tile, xe, id)
		XE_WARN_ON(vm->pt_root[id]);

	trace_xe_vm_free(vm);

	ttm_lru_bulk_move_fini(&xe->ttm, &vm->lru_bulk_move);

	if (vm->xef)
		xe_file_put(vm->xef);

	kfree(vm);
}

static void xe_vm_free(struct drm_gpuvm *gpuvm)
{
	struct xe_vm *vm = container_of(gpuvm, struct xe_vm, gpuvm);

	/* To destroy the VM we need to be able to sleep */
	queue_work(system_unbound_wq, &vm->destroy_work);
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

	if (xe->info.has_asid) {
		down_write(&xe->usm.lock);
		err = xa_alloc_cyclic(&xe->usm.asid_to_vm, &asid, vm,
				      XA_LIMIT(1, XE_MAX_ASID - 1),
				      &xe->usm.next_asid, GFP_KERNEL);
		up_write(&xe->usm.lock);
		if (err < 0)
			goto err_close_and_put;

		vm->usm.asid = asid;
	}

	vm->xef = xe_file_get(xef);

	/* Record BO memory for VM pagetable created against client */
	for_each_tile(tile, xe, id)
		if (vm->pt_root[id])
			xe_drm_client_add_bo(vm->xef->client, vm->pt_root[id]->bo);

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_MEM)
	/* Warning: Security issue - never enable by default */
	args->reserved[0] = xe_bo_main_addr(vm->pt_root[0]->bo, XE_PAGE_SIZE);
#endif

	/* user id alloc must always be last in ioctl to prevent UAF */
	err = xa_alloc(&xef->vm.xa, &id, vm, xa_limit_32b, GFP_KERNEL);
	if (err)
		goto err_close_and_put;

	args->vm_id = id;

	return 0;

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
ALLOW_ERROR_INJECTION(vm_bind_ioctl_ops_create, ERRNO);

static struct xe_vma *new_vma(struct xe_vm *vm, struct drm_gpuva_op_map *op,
			      u16 pat_index, unsigned int flags)
{
	struct xe_bo *bo = op->gem.obj ? gem_to_xe_bo(op->gem.obj) : NULL;
	struct drm_exec exec;
	struct xe_vma *vma;
	int err = 0;

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
	if (IS_ERR(vma))
		goto err_unlock;

	if (xe_vma_is_userptr(vma))
		err = xe_vma_userptr_pin_pages(to_userptr_vma(vma));
	else if (!xe_vma_has_no_bo(vma) && !bo->vm)
		err = add_preempt_fences(vm, bo);

err_unlock:
	if (bo)
		drm_exec_fini(&exec);

	if (err) {
		prep_vma_destroy(vm, vma, false);
		xe_vma_destroy_unlocked(vma);
		vma = ERR_PTR(err);
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

static int vm_bind_ioctl_ops_parse(struct xe_vm *vm, struct drm_gpuva_ops *ops,
				   struct xe_vma_ops *vops)
{
	struct xe_device *xe = vm->xe;
	struct drm_gpuva_op *__op;
	struct xe_tile *tile;
	u8 id, tile_mask = 0;
	int err = 0;

	lockdep_assert_held_write(&vm->lock);

	for_each_tile(tile, vm->xe, id)
		tile_mask |= 0x1 << id;

	drm_gpuva_for_each_op(__op, ops) {
		struct xe_vma_op *op = gpuva_op_to_vma_op(__op);
		struct xe_vma *vma;
		unsigned int flags = 0;

		INIT_LIST_HEAD(&op->link);
		list_add_tail(&op->link, &vops->list);
		op->tile_mask = tile_mask;

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
			if (op->map.immediate || !xe_vm_in_fault_mode(vm))
				xe_vma_ops_incr_pt_update_ops(vops,
							      op->tile_mask);
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
				} else {
					xe_vma_ops_incr_pt_update_ops(vops, op->tile_mask);
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
				} else {
					xe_vma_ops_incr_pt_update_ops(vops, op->tile_mask);
				}
			}
			xe_vma_ops_incr_pt_update_ops(vops, op->tile_mask);
			break;
		}
		case DRM_GPUVA_OP_UNMAP:
		case DRM_GPUVA_OP_PREFETCH:
			/* FIXME: Need to skip some prefetch ops */
			xe_vma_ops_incr_pt_update_ops(vops, op->tile_mask);
			break;
		default:
			drm_warn(&vm->xe->drm, "NOT POSSIBLE");
		}

		err = xe_vma_op_commit(vm, op);
		if (err)
			return err;
	}

	return 0;
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
	}
}

static int vma_lock_and_validate(struct drm_exec *exec, struct xe_vma *vma,
				 bool validate)
{
	struct xe_bo *bo = xe_vma_bo(vma);
	int err = 0;

	if (bo) {
		if (!bo->vm)
			err = drm_exec_lock_obj(exec, &bo->ttm.base);
		if (!err && validate)
			err = xe_bo_validate(bo, xe_vma_vm(vma), true);
	}

	return err;
}

static int check_ufence(struct xe_vma *vma)
{
	if (vma->ufence) {
		struct xe_user_fence * const f = vma->ufence;

		if (!xe_sync_ufence_get_status(f))
			return -EBUSY;

		vma->ufence = NULL;
		xe_sync_ufence_put(f);
	}

	return 0;
}

static int op_lock_and_prep(struct drm_exec *exec, struct xe_vm *vm,
			    struct xe_vma_op *op)
{
	int err = 0;

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		err = vma_lock_and_validate(exec, op->map.vma,
					    !xe_vm_in_fault_mode(vm) ||
					    op->map.immediate);
		break;
	case DRM_GPUVA_OP_REMAP:
		err = check_ufence(gpuva_to_vma(op->base.remap.unmap->va));
		if (err)
			break;

		err = vma_lock_and_validate(exec,
					    gpuva_to_vma(op->base.remap.unmap->va),
					    false);
		if (!err && op->remap.prev)
			err = vma_lock_and_validate(exec, op->remap.prev, true);
		if (!err && op->remap.next)
			err = vma_lock_and_validate(exec, op->remap.next, true);
		break;
	case DRM_GPUVA_OP_UNMAP:
		err = check_ufence(gpuva_to_vma(op->base.unmap.va));
		if (err)
			break;

		err = vma_lock_and_validate(exec,
					    gpuva_to_vma(op->base.unmap.va),
					    false);
		break;
	case DRM_GPUVA_OP_PREFETCH:
	{
		struct xe_vma *vma = gpuva_to_vma(op->base.prefetch.va);
		u32 region = op->prefetch.region;

		xe_assert(vm->xe, region <= ARRAY_SIZE(region_to_mem_type));

		err = vma_lock_and_validate(exec,
					    gpuva_to_vma(op->base.prefetch.va),
					    false);
		if (!err && !xe_vma_has_no_bo(vma))
			err = xe_bo_migrate(xe_vma_bo(vma),
					    region_to_mem_type[region]);
		break;
	}
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	return err;
}

static int vm_bind_ioctl_ops_lock_and_prep(struct drm_exec *exec,
					   struct xe_vm *vm,
					   struct xe_vma_ops *vops)
{
	struct xe_vma_op *op;
	int err;

	err = drm_exec_lock_obj(exec, xe_vm_obj(vm));
	if (err)
		return err;

	list_for_each_entry(op, &vops->list, link) {
		err = op_lock_and_prep(exec, vm, op);
		if (err)
			return err;
	}

#ifdef TEST_VM_OPS_ERROR
	if (vops->inject_error &&
	    vm->xe->vm_inject_error_position == FORCE_OP_ERROR_LOCK)
		return -ENOSPC;
#endif

	return 0;
}

static void op_trace(struct xe_vma_op *op)
{
	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		trace_xe_vma_bind(op->map.vma);
		break;
	case DRM_GPUVA_OP_REMAP:
		trace_xe_vma_unbind(gpuva_to_vma(op->base.remap.unmap->va));
		if (op->remap.prev)
			trace_xe_vma_bind(op->remap.prev);
		if (op->remap.next)
			trace_xe_vma_bind(op->remap.next);
		break;
	case DRM_GPUVA_OP_UNMAP:
		trace_xe_vma_unbind(gpuva_to_vma(op->base.unmap.va));
		break;
	case DRM_GPUVA_OP_PREFETCH:
		trace_xe_vma_bind(gpuva_to_vma(op->base.prefetch.va));
		break;
	default:
		XE_WARN_ON("NOT POSSIBLE");
	}
}

static void trace_xe_vm_ops_execute(struct xe_vma_ops *vops)
{
	struct xe_vma_op *op;

	list_for_each_entry(op, &vops->list, link)
		op_trace(op);
}

static int vm_ops_setup_tile_args(struct xe_vm *vm, struct xe_vma_ops *vops)
{
	struct xe_exec_queue *q = vops->q;
	struct xe_tile *tile;
	int number_tiles = 0;
	u8 id;

	for_each_tile(tile, vm->xe, id) {
		if (vops->pt_update_ops[id].num_ops)
			++number_tiles;

		if (vops->pt_update_ops[id].q)
			continue;

		if (q) {
			vops->pt_update_ops[id].q = q;
			if (vm->pt_root[id] && !list_empty(&q->multi_gt_list))
				q = list_next_entry(q, multi_gt_list);
		} else {
			vops->pt_update_ops[id].q = vm->q[id];
		}
	}

	return number_tiles;
}

static struct dma_fence *ops_execute(struct xe_vm *vm,
				     struct xe_vma_ops *vops)
{
	struct xe_tile *tile;
	struct dma_fence *fence = NULL;
	struct dma_fence **fences = NULL;
	struct dma_fence_array *cf = NULL;
	int number_tiles = 0, current_fence = 0, err;
	u8 id;

	number_tiles = vm_ops_setup_tile_args(vm, vops);
	if (number_tiles == 0)
		return ERR_PTR(-ENODATA);

	if (number_tiles > 1) {
		fences = kmalloc_array(number_tiles, sizeof(*fences),
				       GFP_KERNEL);
		if (!fences) {
			fence = ERR_PTR(-ENOMEM);
			goto err_trace;
		}
	}

	for_each_tile(tile, vm->xe, id) {
		if (!vops->pt_update_ops[id].num_ops)
			continue;

		err = xe_pt_update_ops_prepare(tile, vops);
		if (err) {
			fence = ERR_PTR(err);
			goto err_out;
		}
	}

	trace_xe_vm_ops_execute(vops);

	for_each_tile(tile, vm->xe, id) {
		if (!vops->pt_update_ops[id].num_ops)
			continue;

		fence = xe_pt_update_ops_run(tile, vops);
		if (IS_ERR(fence))
			goto err_out;

		if (fences)
			fences[current_fence++] = fence;
	}

	if (fences) {
		cf = dma_fence_array_create(number_tiles, fences,
					    vm->composite_fence_ctx,
					    vm->composite_fence_seqno++,
					    false);
		if (!cf) {
			--vm->composite_fence_seqno;
			fence = ERR_PTR(-ENOMEM);
			goto err_out;
		}
		fence = &cf->base;
	}

	for_each_tile(tile, vm->xe, id) {
		if (!vops->pt_update_ops[id].num_ops)
			continue;

		xe_pt_update_ops_fini(tile, vops);
	}

	return fence;

err_out:
	for_each_tile(tile, vm->xe, id) {
		if (!vops->pt_update_ops[id].num_ops)
			continue;

		xe_pt_update_ops_abort(tile, vops);
	}
	while (current_fence)
		dma_fence_put(fences[--current_fence]);
	kfree(fences);
	kfree(cf);

err_trace:
	trace_xe_vm_ops_fail(vm);
	return fence;
}

static void vma_add_ufence(struct xe_vma *vma, struct xe_user_fence *ufence)
{
	if (vma->ufence)
		xe_sync_ufence_put(vma->ufence);
	vma->ufence = __xe_sync_ufence_get(ufence);
}

static void op_add_ufence(struct xe_vm *vm, struct xe_vma_op *op,
			  struct xe_user_fence *ufence)
{
	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		vma_add_ufence(op->map.vma, ufence);
		break;
	case DRM_GPUVA_OP_REMAP:
		if (op->remap.prev)
			vma_add_ufence(op->remap.prev, ufence);
		if (op->remap.next)
			vma_add_ufence(op->remap.next, ufence);
		break;
	case DRM_GPUVA_OP_UNMAP:
		break;
	case DRM_GPUVA_OP_PREFETCH:
		vma_add_ufence(gpuva_to_vma(op->base.prefetch.va), ufence);
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}
}

static void vm_bind_ioctl_ops_fini(struct xe_vm *vm, struct xe_vma_ops *vops,
				   struct dma_fence *fence)
{
	struct xe_exec_queue *wait_exec_queue = to_wait_exec_queue(vm, vops->q);
	struct xe_user_fence *ufence;
	struct xe_vma_op *op;
	int i;

	ufence = find_ufence_get(vops->syncs, vops->num_syncs);
	list_for_each_entry(op, &vops->list, link) {
		if (ufence)
			op_add_ufence(vm, op, ufence);

		if (op->base.op == DRM_GPUVA_OP_UNMAP)
			xe_vma_destroy(gpuva_to_vma(op->base.unmap.va), fence);
		else if (op->base.op == DRM_GPUVA_OP_REMAP)
			xe_vma_destroy(gpuva_to_vma(op->base.remap.unmap->va),
				       fence);
	}
	if (ufence)
		xe_sync_ufence_put(ufence);
	for (i = 0; i < vops->num_syncs; i++)
		xe_sync_entry_signal(vops->syncs + i, fence);
	xe_exec_queue_last_fence_set(wait_exec_queue, vm, fence);
	dma_fence_put(fence);
}

static int vm_bind_ioctl_ops_execute(struct xe_vm *vm,
				     struct xe_vma_ops *vops)
{
	struct drm_exec exec;
	struct dma_fence *fence;
	int err;

	lockdep_assert_held_write(&vm->lock);

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT |
		      DRM_EXEC_IGNORE_DUPLICATES, 0);
	drm_exec_until_all_locked(&exec) {
		err = vm_bind_ioctl_ops_lock_and_prep(&exec, vm, vops);
		drm_exec_retry_on_contention(&exec);
		if (err)
			goto unlock;

		fence = ops_execute(vm, vops);
		if (IS_ERR(fence)) {
			err = PTR_ERR(fence);
			goto unlock;
		}

		vm_bind_ioctl_ops_fini(vm, vops, fence);
	}

unlock:
	drm_exec_fini(&exec);
	return err;
}
ALLOW_ERROR_INJECTION(vm_bind_ioctl_ops_execute, ERRNO);

#define SUPPORTED_FLAGS_STUB  \
	(DRM_XE_VM_BIND_FLAG_READONLY | \
	 DRM_XE_VM_BIND_FLAG_IMMEDIATE | \
	 DRM_XE_VM_BIND_FLAG_NULL | \
	 DRM_XE_VM_BIND_FLAG_DUMPABLE)

#ifdef TEST_VM_OPS_ERROR
#define SUPPORTED_FLAGS	(SUPPORTED_FLAGS_STUB | FORCE_OP_ERROR)
#else
#define SUPPORTED_FLAGS	SUPPORTED_FLAGS_STUB
#endif

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
					   GFP_KERNEL | __GFP_ACCOUNT |
					   __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
		if (!*bind_ops)
			return args->num_binds > 1 ? -ENOBUFS : -ENOMEM;

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

static void xe_vma_ops_init(struct xe_vma_ops *vops, struct xe_vm *vm,
			    struct xe_exec_queue *q,
			    struct xe_sync_entry *syncs, u32 num_syncs)
{
	memset(vops, 0, sizeof(*vops));
	INIT_LIST_HEAD(&vops->list);
	vops->vm = vm;
	vops->q = q;
	vops->syncs = syncs;
	vops->num_syncs = num_syncs;
}

static int xe_vm_bind_ioctl_validate_bo(struct xe_device *xe, struct xe_bo *bo,
					u64 addr, u64 range, u64 obj_offset,
					u16 pat_index)
{
	u16 coh_mode;

	if (XE_IOCTL_DBG(xe, range > bo->size) ||
	    XE_IOCTL_DBG(xe, obj_offset >
			 bo->size - range)) {
		return -EINVAL;
	}

	/*
	 * Some platforms require 64k VM_BIND alignment,
	 * specifically those with XE_VRAM_FLAGS_NEED64K.
	 *
	 * Other platforms may have BO's set to 64k physical placement,
	 * but can be mapped at 4k offsets anyway. This check is only
	 * there for the former case.
	 */
	if ((bo->flags & XE_BO_FLAG_INTERNAL_64K) &&
	    (xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)) {
		if (XE_IOCTL_DBG(xe, obj_offset &
				 XE_64K_PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, addr & XE_64K_PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, range & XE_64K_PAGE_MASK)) {
			return  -EINVAL;
		}
	}

	coh_mode = xe_pat_index_get_coh_mode(xe, pat_index);
	if (bo->cpu_caching) {
		if (XE_IOCTL_DBG(xe, coh_mode == XE_COH_NONE &&
				 bo->cpu_caching == DRM_XE_GEM_CPU_CACHING_WB)) {
			return  -EINVAL;
		}
	} else if (XE_IOCTL_DBG(xe, coh_mode == XE_COH_NONE)) {
		/*
		 * Imported dma-buf from a different device should
		 * require 1way or 2way coherency since we don't know
		 * how it was mapped on the CPU. Just assume is it
		 * potentially cached on CPU side.
		 */
		return  -EINVAL;
	}

	return 0;
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
	struct xe_vma_ops vops;
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
			       GFP_KERNEL | __GFP_ACCOUNT |
			       __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
		if (!bos) {
			err = -ENOMEM;
			goto release_vm_lock;
		}

		ops = kvcalloc(args->num_binds, sizeof(*ops),
			       GFP_KERNEL | __GFP_ACCOUNT |
			       __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
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

		if (!obj)
			continue;

		gem_obj = drm_gem_object_lookup(file, obj);
		if (XE_IOCTL_DBG(xe, !gem_obj)) {
			err = -ENOENT;
			goto put_obj;
		}
		bos[i] = gem_to_xe_bo(gem_obj);

		err = xe_vm_bind_ioctl_validate_bo(xe, bos[i], addr, range,
						   obj_offset, pat_index);
		if (err)
			goto put_obj;
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

	xe_vma_ops_init(&vops, vm, q, syncs, num_syncs);
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

		err = vm_bind_ioctl_ops_parse(vm, ops[i], &vops);
		if (err)
			goto unwind_ops;

#ifdef TEST_VM_OPS_ERROR
		if (flags & FORCE_OP_ERROR) {
			vops.inject_error = true;
			vm->xe->vm_inject_error_position =
				(vm->xe->vm_inject_error_position + 1) %
				FORCE_OP_ERROR_COUNT;
		}
#endif
	}

	/* Nothing to do */
	if (list_empty(&vops.list)) {
		err = -ENODATA;
		goto unwind_ops;
	}

	err = xe_vma_ops_alloc(&vops, args->num_binds > 1);
	if (err)
		goto unwind_ops;

	err = vm_bind_ioctl_ops_execute(vm, &vops);

unwind_ops:
	if (err && err != -ENODATA)
		vm_bind_ioctl_ops_unwind(vm, ops, args->num_binds);
	xe_vma_ops_fini(&vops);
	for (i = args->num_binds - 1; i >= 0; --i)
		if (ops[i])
			drm_gpuva_ops_free(&vm->gpuvm, ops[i]);
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
	struct xe_gt_tlb_invalidation_fence
		fence[XE_MAX_TILES_PER_DEVICE * XE_MAX_GT_PER_TILE];
	u8 id;
	u32 fence_id = 0;
	int ret = 0;

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
			xe_device_wmb(xe);
			xe_gt_tlb_invalidation_fence_init(tile->primary_gt,
							  &fence[fence_id],
							  true);

			ret = xe_gt_tlb_invalidation_vma(tile->primary_gt,
							 &fence[fence_id], vma);
			if (ret)
				goto wait;
			++fence_id;

			if (!tile->media_gt)
				continue;

			xe_gt_tlb_invalidation_fence_init(tile->media_gt,
							  &fence[fence_id],
							  true);

			ret = xe_gt_tlb_invalidation_vma(tile->media_gt,
							 &fence[fence_id], vma);
			if (ret)
				goto wait;
			++fence_id;
		}
	}

wait:
	for (id = 0; id < fence_id; ++id)
		xe_gt_tlb_invalidation_fence_wait(&fence[id]);

	vma->tile_invalidated = vma->tile_mask;

	return ret;
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
		int err;

		if (IS_ERR(snap->snap[i].data))
			continue;

		snap->snap[i].data = kvmalloc(snap->snap[i].len, GFP_USER);
		if (!snap->snap[i].data) {
			snap->snap[i].data = ERR_PTR(-ENOMEM);
			goto cleanup_bo;
		}

		if (bo) {
			err = xe_bo_read(bo, snap->snap[i].bo_ofs,
					 snap->snap[i].data, snap->snap[i].len);
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
