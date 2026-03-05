// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_svm.h"
#include "xe_userptr.h"

#include <linux/mm.h>

#include "xe_tlb_inval.h"
#include "xe_trace_bo.h"

static void xe_userptr_assert_in_notifier(struct xe_vm *vm)
{
	lockdep_assert(lockdep_is_held_type(&vm->svm.gpusvm.notifier_lock, 0) ||
		       (lockdep_is_held(&vm->lock) &&
			lockdep_is_held_type(&vm->svm.gpusvm.notifier_lock, 1) &&
			dma_resv_held(xe_vm_resv(vm))));
}

/**
 * xe_vma_userptr_check_repin() - Advisory check for repin needed
 * @uvma: The userptr vma
 *
 * Check if the userptr vma has been invalidated since last successful
 * repin. The check is advisory only and can the function can be called
 * without the vm->svm.gpusvm.notifier_lock held. There is no guarantee that the
 * vma userptr will remain valid after a lockless check, so typically
 * the call needs to be followed by a proper check under the notifier_lock.
 *
 * Return: 0 if userptr vma is valid, -EAGAIN otherwise; repin recommended.
 */
int xe_vma_userptr_check_repin(struct xe_userptr_vma *uvma)
{
	return mmu_interval_check_retry(&uvma->userptr.notifier,
					uvma->userptr.pages.notifier_seq) ?
		-EAGAIN : 0;
}

/**
 * __xe_vm_userptr_needs_repin() - Check whether the VM does have userptrs
 * that need repinning.
 * @vm: The VM.
 *
 * This function checks for whether the VM has userptrs that need repinning,
 * and provides a release-type barrier on the svm.gpusvm.notifier_lock after
 * checking.
 *
 * Return: 0 if there are no userptrs needing repinning, -EAGAIN if there are.
 */
int __xe_vm_userptr_needs_repin(struct xe_vm *vm)
{
	lockdep_assert_held_read(&vm->svm.gpusvm.notifier_lock);

	return (list_empty(&vm->userptr.repin_list) &&
		list_empty(&vm->userptr.invalidated)) ? 0 : -EAGAIN;
}

int xe_vma_userptr_pin_pages(struct xe_userptr_vma *uvma)
{
	struct xe_vma *vma = &uvma->vma;
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_device *xe = vm->xe;
	struct drm_gpusvm_ctx ctx = {
		.read_only = xe_vma_read_only(vma),
		.device_private_page_owner = xe_svm_private_page_owner(vm, false),
		.allow_mixed = true,
	};

	lockdep_assert_held(&vm->lock);
	xe_assert(xe, xe_vma_is_userptr(vma));

	if (vma->gpuva.flags & XE_VMA_DESTROYED)
		return 0;

	return drm_gpusvm_get_pages(&vm->svm.gpusvm, &uvma->userptr.pages,
				    uvma->userptr.notifier.mm,
				    &uvma->userptr.notifier,
				    xe_vma_userptr(vma),
				    xe_vma_userptr(vma) + xe_vma_size(vma),
				    &ctx);
}

static struct mmu_interval_notifier_finish *
xe_vma_userptr_do_inval(struct xe_vm *vm, struct xe_userptr_vma *uvma, bool is_deferred)
{
	struct xe_userptr *userptr = &uvma->userptr;
	struct xe_vma *vma = &uvma->vma;
	struct drm_gpusvm_ctx ctx = {
		.in_notifier = true,
		.read_only = xe_vma_read_only(vma),
	};
	long err;

	xe_userptr_assert_in_notifier(vm);
	if (is_deferred)
		xe_assert(vm->xe, userptr->finish_inuse && !userptr->tlb_inval_submitted);

	err = dma_resv_wait_timeout(xe_vm_resv(vm),
				    DMA_RESV_USAGE_BOOKKEEP,
				    false, MAX_SCHEDULE_TIMEOUT);
	XE_WARN_ON(err <= 0);

	if (xe_vm_in_fault_mode(vm) && userptr->initial_bind) {
		if (!userptr->finish_inuse) {
			/*
			 * Defer the TLB wait to an extra pass so the caller
			 * can pipeline TLB flushes across GPUs before waiting
			 * on any of them.
			 */
			xe_assert(vm->xe, !userptr->tlb_inval_submitted);
			userptr->finish_inuse = true;
			userptr->tlb_inval_submitted = true;
			err = xe_vm_invalidate_vma_submit(vma, &userptr->inval_batch);
			XE_WARN_ON(err);
			return &userptr->finish;
		}
		err = xe_vm_invalidate_vma(vma);
		XE_WARN_ON(err);
	}

	if (is_deferred)
		userptr->finish_inuse = false;
	drm_gpusvm_unmap_pages(&vm->svm.gpusvm, &uvma->userptr.pages,
			       xe_vma_size(vma) >> PAGE_SHIFT, &ctx);
	return NULL;
}

static void
xe_vma_userptr_complete_tlb_inval(struct xe_vm *vm, struct xe_userptr_vma *uvma)
{
	struct xe_userptr *userptr = &uvma->userptr;
	struct xe_vma *vma = &uvma->vma;
	struct drm_gpusvm_ctx ctx = {
		.in_notifier = true,
		.read_only = xe_vma_read_only(vma),
	};

	xe_userptr_assert_in_notifier(vm);
	xe_assert(vm->xe, userptr->finish_inuse);
	xe_assert(vm->xe, userptr->tlb_inval_submitted);

	xe_tlb_inval_batch_wait(&userptr->inval_batch);
	userptr->tlb_inval_submitted = false;
	userptr->finish_inuse = false;
	drm_gpusvm_unmap_pages(&vm->svm.gpusvm, &uvma->userptr.pages,
			       xe_vma_size(vma) >> PAGE_SHIFT, &ctx);
}

static struct mmu_interval_notifier_finish *
xe_vma_userptr_invalidate_pass1(struct xe_vm *vm, struct xe_userptr_vma *uvma)
{
	struct xe_userptr *userptr = &uvma->userptr;
	struct xe_vma *vma = &uvma->vma;
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	bool signaled = true;

	xe_userptr_assert_in_notifier(vm);

	/*
	 * Tell exec and rebind worker they need to repin and rebind this
	 * userptr.
	 */
	if (!xe_vm_in_fault_mode(vm) &&
	    !(vma->gpuva.flags & XE_VMA_DESTROYED)) {
		spin_lock(&vm->userptr.invalidated_lock);
		list_move_tail(&userptr->invalidate_link,
			       &vm->userptr.invalidated);
		spin_unlock(&vm->userptr.invalidated_lock);
	}

	/*
	 * Preempt fences turn into schedule disables, pipeline these.
	 * Note that even in fault mode, we need to wait for binds and
	 * unbinds to complete, and those are attached as BOOKMARK fences
	 * to the vm.
	 */
	dma_resv_iter_begin(&cursor, xe_vm_resv(vm),
			    DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		dma_fence_enable_sw_signaling(fence);
		if (signaled && !dma_fence_is_signaled(fence))
			signaled = false;
	}
	dma_resv_iter_end(&cursor);

	/*
	 * Only one caller at a time can use the multi-pass state.
	 * If it's already in use, or all fences are already signaled,
	 * proceed directly to invalidation without deferring.
	 */
	if (signaled || userptr->finish_inuse)
		return xe_vma_userptr_do_inval(vm, uvma, false);

	/* Defer: the notifier core will call invalidate_finish once done. */
	userptr->finish_inuse = true;

	return &userptr->finish;
}

static bool xe_vma_userptr_invalidate_start(struct mmu_interval_notifier *mni,
					    const struct mmu_notifier_range *range,
					    unsigned long cur_seq,
					    struct mmu_interval_notifier_finish **p_finish)
{
	struct xe_userptr_vma *uvma = container_of(mni, typeof(*uvma), userptr.notifier);
	struct xe_vma *vma = &uvma->vma;
	struct xe_vm *vm = xe_vma_vm(vma);

	xe_assert(vm->xe, xe_vma_is_userptr(vma));
	trace_xe_vma_userptr_invalidate(vma);

	if (!mmu_notifier_range_blockable(range))
		return false;

	vm_dbg(&xe_vma_vm(vma)->xe->drm,
	       "NOTIFIER PASS1: addr=0x%016llx, range=0x%016llx",
		xe_vma_start(vma), xe_vma_size(vma));

	down_write(&vm->svm.gpusvm.notifier_lock);
	mmu_interval_set_seq(mni, cur_seq);

	*p_finish = xe_vma_userptr_invalidate_pass1(vm, uvma);

	up_write(&vm->svm.gpusvm.notifier_lock);
	if (!*p_finish)
		trace_xe_vma_userptr_invalidate_complete(vma);

	return true;
}

static void xe_vma_userptr_invalidate_finish(struct mmu_interval_notifier_finish *finish)
{
	struct xe_userptr_vma *uvma = container_of(finish, typeof(*uvma), userptr.finish);
	struct xe_vma *vma = &uvma->vma;
	struct xe_vm *vm = xe_vma_vm(vma);

	vm_dbg(&xe_vma_vm(vma)->xe->drm,
	       "NOTIFIER PASS2: addr=0x%016llx, range=0x%016llx",
		xe_vma_start(vma), xe_vma_size(vma));

	down_write(&vm->svm.gpusvm.notifier_lock);
	/*
	 * If a TLB invalidation was previously submitted (deferred from the
	 * synchronous pass1 fallback), wait for it and unmap pages.
	 * Otherwise, fences have now completed: invalidate the TLB and unmap.
	 */
	if (uvma->userptr.tlb_inval_submitted)
		xe_vma_userptr_complete_tlb_inval(vm, uvma);
	else
		xe_vma_userptr_do_inval(vm, uvma, true);
	up_write(&vm->svm.gpusvm.notifier_lock);
	trace_xe_vma_userptr_invalidate_complete(vma);
}

static const struct mmu_interval_notifier_ops vma_userptr_notifier_ops = {
	.invalidate_start = xe_vma_userptr_invalidate_start,
	.invalidate_finish = xe_vma_userptr_invalidate_finish,
};

#if IS_ENABLED(CONFIG_DRM_XE_USERPTR_INVAL_INJECT)
/**
 * xe_vma_userptr_force_invalidate() - force invalidate a userptr
 * @uvma: The userptr vma to invalidate
 *
 * Perform a forced userptr invalidation for testing purposes.
 */
void xe_vma_userptr_force_invalidate(struct xe_userptr_vma *uvma)
{
	static struct mmu_interval_notifier_finish *finish;
	struct xe_vm *vm = xe_vma_vm(&uvma->vma);

	/* Protect against concurrent userptr pinning */
	lockdep_assert_held(&vm->lock);
	/* Protect against concurrent notifiers */
	lockdep_assert_held(&vm->svm.gpusvm.notifier_lock);
	/*
	 * Protect against concurrent instances of this function and
	 * the critical exec sections
	 */
	xe_vm_assert_held(vm);

	if (!mmu_interval_read_retry(&uvma->userptr.notifier,
				     uvma->userptr.pages.notifier_seq))
		uvma->userptr.pages.notifier_seq -= 2;

	finish = xe_vma_userptr_invalidate_pass1(vm, uvma);
	if (finish)
		finish = xe_vma_userptr_do_inval(vm, uvma, true);
	if (finish)
		xe_vma_userptr_complete_tlb_inval(vm, uvma);
}
#endif

int xe_vm_userptr_pin(struct xe_vm *vm)
{
	struct xe_userptr_vma *uvma, *next;
	int err = 0;

	xe_assert(vm->xe, !xe_vm_in_fault_mode(vm));
	lockdep_assert_held_write(&vm->lock);

	/* Collect invalidated userptrs */
	spin_lock(&vm->userptr.invalidated_lock);
	xe_assert(vm->xe, list_empty(&vm->userptr.repin_list));
	list_for_each_entry_safe(uvma, next, &vm->userptr.invalidated,
				 userptr.invalidate_link) {
		list_del_init(&uvma->userptr.invalidate_link);
		list_add_tail(&uvma->userptr.repin_link,
			      &vm->userptr.repin_list);
	}
	spin_unlock(&vm->userptr.invalidated_lock);

	/* Pin and move to bind list */
	list_for_each_entry_safe(uvma, next, &vm->userptr.repin_list,
				 userptr.repin_link) {
		err = xe_vma_userptr_pin_pages(uvma);
		if (err == -EFAULT) {
			list_del_init(&uvma->userptr.repin_link);
			/*
			 * We might have already done the pin once already, but
			 * then had to retry before the re-bind happened, due
			 * some other condition in the caller, but in the
			 * meantime the userptr got dinged by the notifier such
			 * that we need to revalidate here, but this time we hit
			 * the EFAULT. In such a case make sure we remove
			 * ourselves from the rebind list to avoid going down in
			 * flames.
			 */
			if (!list_empty(&uvma->vma.combined_links.rebind))
				list_del_init(&uvma->vma.combined_links.rebind);

			/* Wait for pending binds */
			xe_vm_lock(vm, false);
			dma_resv_wait_timeout(xe_vm_resv(vm),
					      DMA_RESV_USAGE_BOOKKEEP,
					      false, MAX_SCHEDULE_TIMEOUT);

			down_read(&vm->svm.gpusvm.notifier_lock);
			err = xe_vm_invalidate_vma(&uvma->vma);
			up_read(&vm->svm.gpusvm.notifier_lock);
			xe_vm_unlock(vm);
			if (err)
				break;
		} else {
			if (err)
				break;

			list_del_init(&uvma->userptr.repin_link);
			list_move_tail(&uvma->vma.combined_links.rebind,
				       &vm->rebind_list);
		}
	}

	if (err) {
		down_write(&vm->svm.gpusvm.notifier_lock);
		spin_lock(&vm->userptr.invalidated_lock);
		list_for_each_entry_safe(uvma, next, &vm->userptr.repin_list,
					 userptr.repin_link) {
			list_del_init(&uvma->userptr.repin_link);
			list_move_tail(&uvma->userptr.invalidate_link,
				       &vm->userptr.invalidated);
		}
		spin_unlock(&vm->userptr.invalidated_lock);
		up_write(&vm->svm.gpusvm.notifier_lock);
	}
	return err;
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

int xe_userptr_setup(struct xe_userptr_vma *uvma, unsigned long start,
		     unsigned long range)
{
	struct xe_userptr *userptr = &uvma->userptr;
	int err;

	INIT_LIST_HEAD(&userptr->invalidate_link);
	INIT_LIST_HEAD(&userptr->repin_link);

	err = mmu_interval_notifier_insert(&userptr->notifier, current->mm,
					   start, range,
					   &vma_userptr_notifier_ops);
	if (err)
		return err;

	userptr->pages.notifier_seq = LONG_MAX;

	return 0;
}

void xe_userptr_remove(struct xe_userptr_vma *uvma)
{
	struct xe_vm *vm = xe_vma_vm(&uvma->vma);
	struct xe_userptr *userptr = &uvma->userptr;

	drm_gpusvm_free_pages(&vm->svm.gpusvm, &uvma->userptr.pages,
			      xe_vma_size(&uvma->vma) >> PAGE_SHIFT);

	/*
	 * Since userptr pages are not pinned, we can't remove
	 * the notifier until we're sure the GPU is not accessing
	 * them anymore
	 */
	mmu_interval_notifier_remove(&userptr->notifier);
}

void xe_userptr_destroy(struct xe_userptr_vma *uvma)
{
	struct xe_vm *vm = xe_vma_vm(&uvma->vma);

	spin_lock(&vm->userptr.invalidated_lock);
	xe_assert(vm->xe, list_empty(&uvma->userptr.repin_link));
	list_del(&uvma->userptr.invalidate_link);
	spin_unlock(&vm->userptr.invalidated_lock);
}
