// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_vm_madvise.h"

#include <linux/nospec.h>

#include <drm/ttm/ttm_tt.h>
#include <drm/xe_drm.h>

#include "xe_bo.h"
#include "xe_vm.h"

static int madvise_preferred_mem_class(struct xe_device *xe, struct xe_vm *vm,
				       struct xe_vma **vmas, int num_vmas,
				       u64 value)
{
	int i, err;

	if (XE_IOCTL_ERR(xe, value > XE_MEM_REGION_CLASS_VRAM))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, value == XE_MEM_REGION_CLASS_VRAM &&
			 !xe->info.is_dgfx))
		return -EINVAL;

	for (i = 0; i < num_vmas; ++i) {
		struct xe_bo *bo;
		struct ww_acquire_ctx ww;

		bo = xe_vma_bo(vmas[i]);

		err = xe_bo_lock(bo, &ww, 0, true);
		if (err)
			return err;
		bo->props.preferred_mem_class = value;
		xe_bo_placement_for_flags(xe, bo, bo->flags);
		xe_bo_unlock(bo, &ww);
	}

	return 0;
}

static int madvise_preferred_gt(struct xe_device *xe, struct xe_vm *vm,
				struct xe_vma **vmas, int num_vmas, u64 value)
{
	int i, err;

	if (XE_IOCTL_ERR(xe, value > xe->info.tile_count))
		return -EINVAL;

	for (i = 0; i < num_vmas; ++i) {
		struct xe_bo *bo;
		struct ww_acquire_ctx ww;

		bo = xe_vma_bo(vmas[i]);

		err = xe_bo_lock(bo, &ww, 0, true);
		if (err)
			return err;
		bo->props.preferred_gt = value;
		xe_bo_placement_for_flags(xe, bo, bo->flags);
		xe_bo_unlock(bo, &ww);
	}

	return 0;
}

static int madvise_preferred_mem_class_gt(struct xe_device *xe,
					  struct xe_vm *vm,
					  struct xe_vma **vmas, int num_vmas,
					  u64 value)
{
	int i, err;
	u32 gt_id = upper_32_bits(value);
	u32 mem_class = lower_32_bits(value);

	if (XE_IOCTL_ERR(xe, mem_class > XE_MEM_REGION_CLASS_VRAM))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, mem_class == XE_MEM_REGION_CLASS_VRAM &&
			 !xe->info.is_dgfx))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, gt_id > xe->info.tile_count))
		return -EINVAL;

	for (i = 0; i < num_vmas; ++i) {
		struct xe_bo *bo;
		struct ww_acquire_ctx ww;

		bo = xe_vma_bo(vmas[i]);

		err = xe_bo_lock(bo, &ww, 0, true);
		if (err)
			return err;
		bo->props.preferred_mem_class = mem_class;
		bo->props.preferred_gt = gt_id;
		xe_bo_placement_for_flags(xe, bo, bo->flags);
		xe_bo_unlock(bo, &ww);
	}

	return 0;
}

static int madvise_cpu_atomic(struct xe_device *xe, struct xe_vm *vm,
			      struct xe_vma **vmas, int num_vmas, u64 value)
{
	int i, err;

	for (i = 0; i < num_vmas; ++i) {
		struct xe_bo *bo;
		struct ww_acquire_ctx ww;

		bo = xe_vma_bo(vmas[i]);
		if (XE_IOCTL_ERR(xe, !(bo->flags & XE_BO_CREATE_SYSTEM_BIT)))
			return -EINVAL;

		err = xe_bo_lock(bo, &ww, 0, true);
		if (err)
			return err;
		bo->props.cpu_atomic = !!value;

		/*
		 * All future CPU accesses must be from system memory only, we
		 * just invalidate the CPU page tables which will trigger a
		 * migration on next access.
		 */
		if (bo->props.cpu_atomic)
			ttm_bo_unmap_virtual(&bo->ttm);
		xe_bo_unlock(bo, &ww);
	}

	return 0;
}

static int madvise_device_atomic(struct xe_device *xe, struct xe_vm *vm,
				 struct xe_vma **vmas, int num_vmas, u64 value)
{
	int i, err;

	for (i = 0; i < num_vmas; ++i) {
		struct xe_bo *bo;
		struct ww_acquire_ctx ww;

		bo = xe_vma_bo(vmas[i]);
		if (XE_IOCTL_ERR(xe, !(bo->flags & XE_BO_CREATE_VRAM0_BIT) &&
				 !(bo->flags & XE_BO_CREATE_VRAM1_BIT)))
			return -EINVAL;

		err = xe_bo_lock(bo, &ww, 0, true);
		if (err)
			return err;
		bo->props.device_atomic = !!value;
		xe_bo_unlock(bo, &ww);
	}

	return 0;
}

static int madvise_priority(struct xe_device *xe, struct xe_vm *vm,
			    struct xe_vma **vmas, int num_vmas, u64 value)
{
	int i, err;

	if (XE_IOCTL_ERR(xe, value > DRM_XE_VMA_PRIORITY_HIGH))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, value == DRM_XE_VMA_PRIORITY_HIGH &&
			 !capable(CAP_SYS_NICE)))
		return -EPERM;

	for (i = 0; i < num_vmas; ++i) {
		struct xe_bo *bo;
		struct ww_acquire_ctx ww;

		bo = xe_vma_bo(vmas[i]);

		err = xe_bo_lock(bo, &ww, 0, true);
		if (err)
			return err;
		bo->ttm.priority = value;
		ttm_bo_move_to_lru_tail(&bo->ttm);
		xe_bo_unlock(bo, &ww);
	}

	return 0;
}

static int madvise_pin(struct xe_device *xe, struct xe_vm *vm,
		       struct xe_vma **vmas, int num_vmas, u64 value)
{
	XE_WARN_ON("NIY");
	return 0;
}

typedef int (*madvise_func)(struct xe_device *xe, struct xe_vm *vm,
			    struct xe_vma **vmas, int num_vmas, u64 value);

static const madvise_func madvise_funcs[] = {
	[DRM_XE_VM_MADVISE_PREFERRED_MEM_CLASS] = madvise_preferred_mem_class,
	[DRM_XE_VM_MADVISE_PREFERRED_GT] = madvise_preferred_gt,
	[DRM_XE_VM_MADVISE_PREFERRED_MEM_CLASS_GT] =
		madvise_preferred_mem_class_gt,
	[DRM_XE_VM_MADVISE_CPU_ATOMIC] = madvise_cpu_atomic,
	[DRM_XE_VM_MADVISE_DEVICE_ATOMIC] = madvise_device_atomic,
	[DRM_XE_VM_MADVISE_PRIORITY] = madvise_priority,
	[DRM_XE_VM_MADVISE_PIN] = madvise_pin,
};

static struct xe_vma **
get_vmas(struct xe_vm *vm, int *num_vmas, u64 addr, u64 range)
{
	struct xe_vma **vmas, **__vmas;
	struct drm_gpuva *gpuva;
	int max_vmas = 8;

	lockdep_assert_held(&vm->lock);

	vmas = kmalloc(max_vmas * sizeof(*vmas), GFP_KERNEL);
	if (!vmas)
		return NULL;

	drm_gpuvm_for_each_va_range(gpuva, &vm->gpuvm, addr, addr + range) {
		struct xe_vma *vma = gpuva_to_vma(gpuva);

		if (xe_vma_is_userptr(vma))
			continue;

		if (*num_vmas == max_vmas) {
			max_vmas <<= 1;
			__vmas = krealloc(vmas, max_vmas * sizeof(*vmas),
					  GFP_KERNEL);
			if (!__vmas)
				return NULL;
			vmas = __vmas;
		}

		vmas[*num_vmas] = vma;
		*num_vmas += 1;
	}

	return vmas;
}

int xe_vm_madvise_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_vm_madvise *args = data;
	struct xe_vm *vm;
	struct xe_vma **vmas = NULL;
	int num_vmas = 0, err = 0, idx;

	if (XE_IOCTL_ERR(xe, args->extensions) ||
	    XE_IOCTL_ERR(xe, args->pad || args->pad2) ||
	    XE_IOCTL_ERR(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->property > ARRAY_SIZE(madvise_funcs)))
		return -EINVAL;

	vm = xe_vm_lookup(xef, args->vm_id);
	if (XE_IOCTL_ERR(xe, !vm))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, xe_vm_is_closed_or_banned(vm))) {
		err = -ENOENT;
		goto put_vm;
	}

	if (XE_IOCTL_ERR(xe, !xe_vm_in_fault_mode(vm))) {
		err = -EINVAL;
		goto put_vm;
	}

	down_read(&vm->lock);

	vmas = get_vmas(vm, &num_vmas, args->addr, args->range);
	if (XE_IOCTL_ERR(xe, err))
		goto unlock_vm;

	if (XE_IOCTL_ERR(xe, !vmas)) {
		err = -ENOMEM;
		goto unlock_vm;
	}

	if (XE_IOCTL_ERR(xe, !num_vmas)) {
		err = -EINVAL;
		goto unlock_vm;
	}

	idx = array_index_nospec(args->property, ARRAY_SIZE(madvise_funcs));
	err = madvise_funcs[idx](xe, vm, vmas, num_vmas, args->value);

unlock_vm:
	up_read(&vm->lock);
put_vm:
	xe_vm_put(vm);
	kfree(vmas);
	return err;
}
