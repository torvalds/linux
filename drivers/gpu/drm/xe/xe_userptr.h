/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_USERPTR_H_
#define _XE_USERPTR_H_

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>

#include <drm/drm_gpusvm.h>

#include "xe_tlb_inval_types.h"

struct xe_vm;
struct xe_vma;
struct xe_userptr_vma;

/** struct xe_userptr_vm - User pointer VM level state */
struct xe_userptr_vm {
	/**
	 * @userptr.repin_list: list of VMAs which are user pointers,
	 * and needs repinning. Protected by @lock.
	 */
	struct list_head repin_list;
	/**
	 * @userptr.invalidated_lock: Protects the
	 * @userptr.invalidated list.
	 */
	spinlock_t invalidated_lock;
	/**
	 * @userptr.invalidated: List of invalidated userptrs, not yet
	 * picked
	 * up for revalidation. Protected from access with the
	 * @invalidated_lock. Removing items from the list
	 * additionally requires @lock in write mode, and adding
	 * items to the list requires either the @svm.gpusvm.notifier_lock in
	 * write mode, OR @lock in write mode.
	 */
	struct list_head invalidated;
};

/** struct xe_userptr - User pointer */
struct xe_userptr {
	/** @invalidate_link: Link for the vm::userptr.invalidated list */
	struct list_head invalidate_link;
	/** @userptr: link into VM repin list if userptr. */
	struct list_head repin_link;
	/**
	 * @pages: gpusvm pages for this user pointer.
	 */
	struct drm_gpusvm_pages pages;
	/**
	 * @notifier: MMU notifier for user pointer (invalidation call back)
	 */
	struct mmu_interval_notifier notifier;
	/**
	 * @finish: MMU notifier finish structure for two-pass invalidation.
	 * Embedded here to avoid allocation in the notifier callback.
	 * Protected by struct xe_vm::svm.gpusvm.notifier_lock in write mode
	 * alternatively by the same lock in read mode *and* the vm resv held.
	 */
	struct mmu_interval_notifier_finish finish;
	/**
	 * @inval_batch: TLB invalidation batch for deferred completion.
	 * Stores an in-flight TLB invalidation submitted during a two-pass
	 * notifier so the wait can be deferred to a subsequent pass, allowing
	 * multiple GPUs to be signalled before any of them are waited on.
	 * Protected using the same locking as @finish.
	 */
	struct xe_tlb_inval_batch inval_batch;
	/**
	 * @finish_inuse: Whether @finish is currently in use by an in-progress
	 * two-pass invalidation.
	 * Protected using the same locking as @finish.
	 */
	bool finish_inuse;
	/**
	 * @tlb_inval_submitted: Whether a TLB invalidation has been submitted
	 * via @inval_batch and is pending completion.  When set, the next pass
	 * must call xe_tlb_inval_batch_wait() before reusing @inval_batch.
	 * Protected using the same locking as @finish.
	 */
	bool tlb_inval_submitted;
	/**
	 * @initial_bind: user pointer has been bound at least once.
	 * write: vm->svm.gpusvm.notifier_lock in read mode and vm->resv held.
	 * read: vm->svm.gpusvm.notifier_lock in write mode or vm->resv held.
	 */
	bool initial_bind;
#if IS_ENABLED(CONFIG_DRM_XE_USERPTR_INVAL_INJECT)
	u32 divisor;
#endif
};

#if IS_ENABLED(CONFIG_DRM_GPUSVM)
void xe_userptr_remove(struct xe_userptr_vma *uvma);
int xe_userptr_setup(struct xe_userptr_vma *uvma, unsigned long start,
		     unsigned long range);
void xe_userptr_destroy(struct xe_userptr_vma *uvma);

int xe_vm_userptr_pin(struct xe_vm *vm);
int __xe_vm_userptr_needs_repin(struct xe_vm *vm);
int xe_vm_userptr_check_repin(struct xe_vm *vm);
int xe_vma_userptr_pin_pages(struct xe_userptr_vma *uvma);
int xe_vma_userptr_check_repin(struct xe_userptr_vma *uvma);
#else
static inline void xe_userptr_remove(struct xe_userptr_vma *uvma) {}

static inline int xe_userptr_setup(struct xe_userptr_vma *uvma,
				   unsigned long start, unsigned long range)
{
	return -ENODEV;
}

static inline void xe_userptr_destroy(struct xe_userptr_vma *uvma) {}

static inline int xe_vm_userptr_pin(struct xe_vm *vm) { return 0; }
static inline int __xe_vm_userptr_needs_repin(struct xe_vm *vm) { return 0; }
static inline int xe_vm_userptr_check_repin(struct xe_vm *vm) { return 0; }
static inline int xe_vma_userptr_pin_pages(struct xe_userptr_vma *uvma) { return -ENODEV; }
static inline int xe_vma_userptr_check_repin(struct xe_userptr_vma *uvma) { return -ENODEV; };
#endif

#if IS_ENABLED(CONFIG_DRM_XE_USERPTR_INVAL_INJECT)
void xe_vma_userptr_force_invalidate(struct xe_userptr_vma *uvma);
#else
static inline void xe_vma_userptr_force_invalidate(struct xe_userptr_vma *uvma)
{
}
#endif
#endif
