/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_VM_H_
#define _XE_VM_H_

#include "xe_assert.h"
#include "xe_bo_types.h"
#include "xe_macros.h"
#include "xe_map.h"
#include "xe_vm_types.h"

struct drm_device;
struct drm_printer;
struct drm_file;

struct ttm_buffer_object;

struct dma_fence;

struct xe_exec_queue;
struct xe_file;
struct xe_sync_entry;
struct xe_svm_range;
struct drm_exec;

struct xe_vm *xe_vm_create(struct xe_device *xe, u32 flags);

struct xe_vm *xe_vm_lookup(struct xe_file *xef, u32 id);
int xe_vma_cmp_vma_cb(const void *key, const struct rb_node *node);

static inline struct xe_vm *xe_vm_get(struct xe_vm *vm)
{
	drm_gpuvm_get(&vm->gpuvm);
	return vm;
}

static inline void xe_vm_put(struct xe_vm *vm)
{
	drm_gpuvm_put(&vm->gpuvm);
}

int xe_vm_lock(struct xe_vm *vm, bool intr);

void xe_vm_unlock(struct xe_vm *vm);

static inline bool xe_vm_is_closed(struct xe_vm *vm)
{
	/* Only guaranteed not to change when vm->lock is held */
	return !vm->size;
}

static inline bool xe_vm_is_banned(struct xe_vm *vm)
{
	return vm->flags & XE_VM_FLAG_BANNED;
}

static inline bool xe_vm_is_closed_or_banned(struct xe_vm *vm)
{
	lockdep_assert_held(&vm->lock);
	return xe_vm_is_closed(vm) || xe_vm_is_banned(vm);
}

struct xe_vma *
xe_vm_find_overlapping_vma(struct xe_vm *vm, u64 start, u64 range);

/**
 * xe_vm_has_scratch() - Whether the vm is configured for scratch PTEs
 * @vm: The vm
 *
 * Return: whether the vm populates unmapped areas with scratch PTEs
 */
static inline bool xe_vm_has_scratch(const struct xe_vm *vm)
{
	return vm->flags & XE_VM_FLAG_SCRATCH_PAGE;
}

/**
 * gpuvm_to_vm() - Return the embedding xe_vm from a struct drm_gpuvm pointer
 * @gpuvm: The struct drm_gpuvm pointer
 *
 * Return: Pointer to the embedding struct xe_vm.
 */
static inline struct xe_vm *gpuvm_to_vm(struct drm_gpuvm *gpuvm)
{
	return container_of(gpuvm, struct xe_vm, gpuvm);
}

static inline struct xe_vm *gpuva_to_vm(struct drm_gpuva *gpuva)
{
	return gpuvm_to_vm(gpuva->vm);
}

static inline struct xe_vma *gpuva_to_vma(struct drm_gpuva *gpuva)
{
	return container_of(gpuva, struct xe_vma, gpuva);
}

static inline struct xe_vma_op *gpuva_op_to_vma_op(struct drm_gpuva_op *op)
{
	return container_of(op, struct xe_vma_op, base);
}

/**
 * DOC: Provide accessors for vma members to facilitate easy change of
 * implementation.
 */
static inline u64 xe_vma_start(struct xe_vma *vma)
{
	return vma->gpuva.va.addr;
}

static inline u64 xe_vma_size(struct xe_vma *vma)
{
	return vma->gpuva.va.range;
}

static inline u64 xe_vma_end(struct xe_vma *vma)
{
	return xe_vma_start(vma) + xe_vma_size(vma);
}

static inline u64 xe_vma_bo_offset(struct xe_vma *vma)
{
	return vma->gpuva.gem.offset;
}

static inline struct xe_bo *xe_vma_bo(struct xe_vma *vma)
{
	return !vma->gpuva.gem.obj ? NULL :
		container_of(vma->gpuva.gem.obj, struct xe_bo, ttm.base);
}

static inline struct xe_vm *xe_vma_vm(struct xe_vma *vma)
{
	return container_of(vma->gpuva.vm, struct xe_vm, gpuvm);
}

static inline bool xe_vma_read_only(struct xe_vma *vma)
{
	return vma->gpuva.flags & XE_VMA_READ_ONLY;
}

static inline u64 xe_vma_userptr(struct xe_vma *vma)
{
	return vma->gpuva.gem.offset;
}

static inline bool xe_vma_is_null(struct xe_vma *vma)
{
	return vma->gpuva.flags & DRM_GPUVA_SPARSE;
}

static inline bool xe_vma_is_cpu_addr_mirror(struct xe_vma *vma)
{
	return vma->gpuva.flags & XE_VMA_SYSTEM_ALLOCATOR;
}

static inline bool xe_vma_has_no_bo(struct xe_vma *vma)
{
	return !xe_vma_bo(vma);
}

static inline bool xe_vma_is_userptr(struct xe_vma *vma)
{
	return xe_vma_has_no_bo(vma) && !xe_vma_is_null(vma) &&
		!xe_vma_is_cpu_addr_mirror(vma);
}

/**
 * to_userptr_vma() - Return a pointer to an embedding userptr vma
 * @vma: Pointer to the embedded struct xe_vma
 *
 * Return: Pointer to the embedding userptr vma
 */
static inline struct xe_userptr_vma *to_userptr_vma(struct xe_vma *vma)
{
	xe_assert(xe_vma_vm(vma)->xe, xe_vma_is_userptr(vma));
	return container_of(vma, struct xe_userptr_vma, vma);
}

u64 xe_vm_pdp4_descriptor(struct xe_vm *vm, struct xe_tile *tile);

int xe_vm_create_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file);
int xe_vm_destroy_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file);
int xe_vm_bind_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file);

void xe_vm_close_and_put(struct xe_vm *vm);

static inline bool xe_vm_in_fault_mode(struct xe_vm *vm)
{
	return vm->flags & XE_VM_FLAG_FAULT_MODE;
}

static inline bool xe_vm_in_lr_mode(struct xe_vm *vm)
{
	return vm->flags & XE_VM_FLAG_LR_MODE;
}

static inline bool xe_vm_in_preempt_fence_mode(struct xe_vm *vm)
{
	return xe_vm_in_lr_mode(vm) && !xe_vm_in_fault_mode(vm);
}

int xe_vm_add_compute_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q);
void xe_vm_remove_compute_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q);

int xe_vm_userptr_pin(struct xe_vm *vm);

int __xe_vm_userptr_needs_repin(struct xe_vm *vm);

int xe_vm_userptr_check_repin(struct xe_vm *vm);

int xe_vm_rebind(struct xe_vm *vm, bool rebind_worker);
struct dma_fence *xe_vma_rebind(struct xe_vm *vm, struct xe_vma *vma,
				u8 tile_mask);
struct dma_fence *xe_vm_range_rebind(struct xe_vm *vm,
				     struct xe_vma *vma,
				     struct xe_svm_range *range,
				     u8 tile_mask);
struct dma_fence *xe_vm_range_unbind(struct xe_vm *vm,
				     struct xe_svm_range *range);

int xe_vm_invalidate_vma(struct xe_vma *vma);

int xe_vm_validate_protected(struct xe_vm *vm);

static inline void xe_vm_queue_rebind_worker(struct xe_vm *vm)
{
	xe_assert(vm->xe, xe_vm_in_preempt_fence_mode(vm));
	queue_work(vm->xe->ordered_wq, &vm->preempt.rebind_work);
}

/**
 * xe_vm_reactivate_rebind() - Reactivate the rebind functionality on compute
 * vms.
 * @vm: The vm.
 *
 * If the rebind functionality on a compute vm was disabled due
 * to nothing to execute. Reactivate it and run the rebind worker.
 * This function should be called after submitting a batch to a compute vm.
 */
static inline void xe_vm_reactivate_rebind(struct xe_vm *vm)
{
	if (xe_vm_in_preempt_fence_mode(vm) && vm->preempt.rebind_deactivated) {
		vm->preempt.rebind_deactivated = false;
		xe_vm_queue_rebind_worker(vm);
	}
}

int xe_vma_userptr_pin_pages(struct xe_userptr_vma *uvma);

int xe_vma_userptr_check_repin(struct xe_userptr_vma *uvma);

bool xe_vm_validate_should_retry(struct drm_exec *exec, int err, ktime_t *end);

int xe_vm_lock_vma(struct drm_exec *exec, struct xe_vma *vma);

int xe_vm_validate_rebind(struct xe_vm *vm, struct drm_exec *exec,
			  unsigned int num_fences);

struct dma_fence *xe_vm_bind_kernel_bo(struct xe_vm *vm, struct xe_bo *bo,
				       struct xe_exec_queue *q, u64 addr,
				       enum xe_cache_level cache_lvl);

/**
 * xe_vm_resv() - Return's the vm's reservation object
 * @vm: The vm
 *
 * Return: Pointer to the vm's reservation object.
 */
static inline struct dma_resv *xe_vm_resv(struct xe_vm *vm)
{
	return drm_gpuvm_resv(&vm->gpuvm);
}

void xe_vm_kill(struct xe_vm *vm, bool unlocked);

/**
 * xe_vm_assert_held(vm) - Assert that the vm's reservation object is held.
 * @vm: The vm
 */
#define xe_vm_assert_held(vm) dma_resv_assert_held(xe_vm_resv(vm))

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_VM)
#define vm_dbg drm_dbg
#else
__printf(2, 3)
static inline void vm_dbg(const struct drm_device *dev,
			  const char *format, ...)
{ /* noop */ }
#endif

struct xe_vm_snapshot *xe_vm_snapshot_capture(struct xe_vm *vm);
void xe_vm_snapshot_capture_delayed(struct xe_vm_snapshot *snap);
void xe_vm_snapshot_print(struct xe_vm_snapshot *snap, struct drm_printer *p);
void xe_vm_snapshot_free(struct xe_vm_snapshot *snap);

/**
 * xe_vm_set_validating() - Register this task as currently making bos resident
 * @allow_res_evict: Allow eviction of buffer objects bound to @vm when
 * validating.
 * @vm: Pointer to the vm or NULL.
 *
 * Register this task as currently making bos resident for the vm. Intended
 * to avoid eviction by the same task of shared bos bound to the vm.
 * Call with the vm's resv lock held.
 *
 * Return: A pin cookie that should be used for xe_vm_clear_validating().
 */
static inline struct pin_cookie xe_vm_set_validating(struct xe_vm *vm,
						     bool allow_res_evict)
{
	struct pin_cookie cookie = {};

	if (vm && !allow_res_evict) {
		xe_vm_assert_held(vm);
		cookie = lockdep_pin_lock(&xe_vm_resv(vm)->lock.base);
		/* Pairs with READ_ONCE in xe_vm_is_validating() */
		WRITE_ONCE(vm->validating, current);
	}

	return cookie;
}

/**
 * xe_vm_clear_validating() - Unregister this task as currently making bos resident
 * @vm: Pointer to the vm or NULL
 * @allow_res_evict: Eviction from @vm was allowed. Must be set to the same
 * value as for xe_vm_set_validation().
 * @cookie: Cookie obtained from xe_vm_set_validating().
 *
 * Register this task as currently making bos resident for the vm. Intended
 * to avoid eviction by the same task of shared bos bound to the vm.
 * Call with the vm's resv lock held.
 */
static inline void xe_vm_clear_validating(struct xe_vm *vm, bool allow_res_evict,
					  struct pin_cookie cookie)
{
	if (vm && !allow_res_evict) {
		lockdep_unpin_lock(&xe_vm_resv(vm)->lock.base, cookie);
		/* Pairs with READ_ONCE in xe_vm_is_validating() */
		WRITE_ONCE(vm->validating, NULL);
	}
}

/**
 * xe_vm_is_validating() - Whether bos bound to the vm are currently being made resident
 * by the current task.
 * @vm: Pointer to the vm.
 *
 * If this function returns %true, we should be in a vm resv locked region, since
 * the current process is the same task that called xe_vm_set_validating().
 * The function asserts that that's indeed the case.
 *
 * Return: %true if the task is currently making bos resident, %false otherwise.
 */
static inline bool xe_vm_is_validating(struct xe_vm *vm)
{
	/* Pairs with WRITE_ONCE in xe_vm_is_validating() */
	if (READ_ONCE(vm->validating) == current) {
		xe_vm_assert_held(vm);
		return true;
	}
	return false;
}

#if IS_ENABLED(CONFIG_DRM_XE_USERPTR_INVAL_INJECT)
void xe_vma_userptr_force_invalidate(struct xe_userptr_vma *uvma);
#else
static inline void xe_vma_userptr_force_invalidate(struct xe_userptr_vma *uvma)
{
}
#endif
#endif
