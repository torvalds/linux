// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_vm_madvise.h"

#include <linux/nospec.h>
#include <drm/xe_drm.h>

#include "xe_bo.h"
#include "xe_pat.h"
#include "xe_pt.h"
#include "xe_svm.h"

struct xe_vmas_in_madvise_range {
	u64 addr;
	u64 range;
	struct xe_vma **vmas;
	int num_vmas;
	bool has_bo_vmas;
	bool has_svm_userptr_vmas;
};

static int get_vmas(struct xe_vm *vm, struct xe_vmas_in_madvise_range *madvise_range)
{
	u64 addr = madvise_range->addr;
	u64 range = madvise_range->range;

	struct xe_vma  **__vmas;
	struct drm_gpuva *gpuva;
	int max_vmas = 8;

	lockdep_assert_held(&vm->lock);

	madvise_range->num_vmas = 0;
	madvise_range->vmas = kmalloc_array(max_vmas, sizeof(*madvise_range->vmas), GFP_KERNEL);
	if (!madvise_range->vmas)
		return -ENOMEM;

	vm_dbg(&vm->xe->drm, "VMA's in range: start=0x%016llx, end=0x%016llx", addr, addr + range);

	drm_gpuvm_for_each_va_range(gpuva, &vm->gpuvm, addr, addr + range) {
		struct xe_vma *vma = gpuva_to_vma(gpuva);

		if (xe_vma_bo(vma))
			madvise_range->has_bo_vmas = true;
		else if (xe_vma_is_cpu_addr_mirror(vma) || xe_vma_is_userptr(vma))
			madvise_range->has_svm_userptr_vmas = true;

		if (madvise_range->num_vmas == max_vmas) {
			max_vmas <<= 1;
			__vmas = krealloc(madvise_range->vmas,
					  max_vmas * sizeof(*madvise_range->vmas),
					  GFP_KERNEL);
			if (!__vmas) {
				kfree(madvise_range->vmas);
				return -ENOMEM;
			}
			madvise_range->vmas = __vmas;
		}

		madvise_range->vmas[madvise_range->num_vmas] = vma;
		(madvise_range->num_vmas)++;
	}

	if (!madvise_range->num_vmas)
		kfree(madvise_range->vmas);

	vm_dbg(&vm->xe->drm, "madvise_range-num_vmas = %d\n", madvise_range->num_vmas);

	return 0;
}

static void madvise_preferred_mem_loc(struct xe_device *xe, struct xe_vm *vm,
				      struct xe_vma **vmas, int num_vmas,
				      struct drm_xe_madvise *op)
{
	int i;

	xe_assert(vm->xe, op->type == DRM_XE_MEM_RANGE_ATTR_PREFERRED_LOC);

	for (i = 0; i < num_vmas; i++) {
		/*TODO: Extend attributes to bo based vmas */
		if ((vmas[i]->attr.preferred_loc.devmem_fd == op->preferred_mem_loc.devmem_fd &&
		     vmas[i]->attr.preferred_loc.migration_policy ==
		     op->preferred_mem_loc.migration_policy) ||
		    !xe_vma_is_cpu_addr_mirror(vmas[i])) {
			vmas[i]->skip_invalidation = true;
		} else {
			vmas[i]->skip_invalidation = false;
			vmas[i]->attr.preferred_loc.devmem_fd = op->preferred_mem_loc.devmem_fd;
			/* Till multi-device support is not added migration_policy
			 * is of no use and can be ignored.
			 */
			vmas[i]->attr.preferred_loc.migration_policy =
						op->preferred_mem_loc.migration_policy;
		}
	}
}

static void madvise_atomic(struct xe_device *xe, struct xe_vm *vm,
			   struct xe_vma **vmas, int num_vmas,
			   struct drm_xe_madvise *op)
{
	struct xe_bo *bo;
	int i;

	xe_assert(vm->xe, op->type == DRM_XE_MEM_RANGE_ATTR_ATOMIC);
	xe_assert(vm->xe, op->atomic.val <= DRM_XE_ATOMIC_CPU);

	for (i = 0; i < num_vmas; i++) {
		if (xe_vma_is_userptr(vmas[i]) &&
		    !(op->atomic.val == DRM_XE_ATOMIC_DEVICE &&
		      xe->info.has_device_atomics_on_smem)) {
			vmas[i]->skip_invalidation = true;
			continue;
		}

		if (vmas[i]->attr.atomic_access == op->atomic.val) {
			vmas[i]->skip_invalidation = true;
		} else {
			vmas[i]->skip_invalidation = false;
			vmas[i]->attr.atomic_access = op->atomic.val;
		}

		bo = xe_vma_bo(vmas[i]);
		if (!bo || bo->attr.atomic_access == op->atomic.val)
			continue;

		vmas[i]->skip_invalidation = false;
		xe_bo_assert_held(bo);
		bo->attr.atomic_access = op->atomic.val;

		/* Invalidate cpu page table, so bo can migrate to smem in next access */
		if (xe_bo_is_vram(bo) &&
		    (bo->attr.atomic_access == DRM_XE_ATOMIC_CPU ||
		     bo->attr.atomic_access == DRM_XE_ATOMIC_GLOBAL))
			ttm_bo_unmap_virtual(&bo->ttm);
	}
}

static void madvise_pat_index(struct xe_device *xe, struct xe_vm *vm,
			      struct xe_vma **vmas, int num_vmas,
			      struct drm_xe_madvise *op)
{
	int i;

	xe_assert(vm->xe, op->type == DRM_XE_MEM_RANGE_ATTR_PAT);

	for (i = 0; i < num_vmas; i++) {
		if (vmas[i]->attr.pat_index == op->pat_index.val) {
			vmas[i]->skip_invalidation = true;
		} else {
			vmas[i]->skip_invalidation = false;
			vmas[i]->attr.pat_index = op->pat_index.val;
		}
	}
}

typedef void (*madvise_func)(struct xe_device *xe, struct xe_vm *vm,
			     struct xe_vma **vmas, int num_vmas,
			     struct drm_xe_madvise *op);

static const madvise_func madvise_funcs[] = {
	[DRM_XE_MEM_RANGE_ATTR_PREFERRED_LOC] = madvise_preferred_mem_loc,
	[DRM_XE_MEM_RANGE_ATTR_ATOMIC] = madvise_atomic,
	[DRM_XE_MEM_RANGE_ATTR_PAT] = madvise_pat_index,
};

static u8 xe_zap_ptes_in_madvise_range(struct xe_vm *vm, u64 start, u64 end)
{
	struct drm_gpuva *gpuva;
	struct xe_tile *tile;
	u8 id, tile_mask = 0;

	lockdep_assert_held_write(&vm->lock);

	/* Wait for pending binds */
	if (dma_resv_wait_timeout(xe_vm_resv(vm), DMA_RESV_USAGE_BOOKKEEP,
				  false, MAX_SCHEDULE_TIMEOUT) <= 0)
		XE_WARN_ON(1);

	drm_gpuvm_for_each_va_range(gpuva, &vm->gpuvm, start, end) {
		struct xe_vma *vma = gpuva_to_vma(gpuva);

		if (vma->skip_invalidation || xe_vma_is_null(vma))
			continue;

		if (xe_vma_is_cpu_addr_mirror(vma)) {
			tile_mask |= xe_svm_ranges_zap_ptes_in_range(vm,
								      xe_vma_start(vma),
								      xe_vma_end(vma));
		} else {
			for_each_tile(tile, vm->xe, id) {
				if (xe_pt_zap_ptes(tile, vma)) {
					tile_mask |= BIT(id);

					/*
					 * WRITE_ONCE pairs with READ_ONCE
					 * in xe_vm_has_valid_gpu_mapping()
					 */
					WRITE_ONCE(vma->tile_invalidated,
						   vma->tile_invalidated | BIT(id));
				}
			}
		}
	}

	return tile_mask;
}

static int xe_vm_invalidate_madvise_range(struct xe_vm *vm, u64 start, u64 end)
{
	u8 tile_mask = xe_zap_ptes_in_madvise_range(vm, start, end);

	if (!tile_mask)
		return 0;

	xe_device_wmb(vm->xe);

	return xe_vm_range_tilemask_tlb_inval(vm, start, end, tile_mask);
}

static bool madvise_args_are_sane(struct xe_device *xe, const struct drm_xe_madvise *args)
{
	if (XE_IOCTL_DBG(xe, !args))
		return false;

	if (XE_IOCTL_DBG(xe, !IS_ALIGNED(args->start, SZ_4K)))
		return false;

	if (XE_IOCTL_DBG(xe, !IS_ALIGNED(args->range, SZ_4K)))
		return false;

	if (XE_IOCTL_DBG(xe, args->range < SZ_4K))
		return false;

	switch (args->type) {
	case DRM_XE_MEM_RANGE_ATTR_PREFERRED_LOC:
	{
		s32 fd = (s32)args->preferred_mem_loc.devmem_fd;

		if (XE_IOCTL_DBG(xe, fd < DRM_XE_PREFERRED_LOC_DEFAULT_SYSTEM))
			return false;

		if (XE_IOCTL_DBG(xe, args->preferred_mem_loc.migration_policy >
				     DRM_XE_MIGRATE_ONLY_SYSTEM_PAGES))
			return false;

		if (XE_IOCTL_DBG(xe, args->preferred_mem_loc.pad))
			return false;

		if (XE_IOCTL_DBG(xe, args->preferred_mem_loc.reserved))
			return false;
		break;
	}
	case DRM_XE_MEM_RANGE_ATTR_ATOMIC:
		if (XE_IOCTL_DBG(xe, args->atomic.val > DRM_XE_ATOMIC_CPU))
			return false;

		if (XE_IOCTL_DBG(xe, args->atomic.pad))
			return false;

		if (XE_IOCTL_DBG(xe, args->atomic.reserved))
			return false;

		break;
	case DRM_XE_MEM_RANGE_ATTR_PAT:
	{
		u16 coh_mode = xe_pat_index_get_coh_mode(xe, args->pat_index.val);

		if (XE_IOCTL_DBG(xe, !coh_mode))
			return false;

		if (XE_WARN_ON(coh_mode > XE_COH_AT_LEAST_1WAY))
			return false;

		if (XE_IOCTL_DBG(xe, args->pat_index.pad))
			return false;

		if (XE_IOCTL_DBG(xe, args->pat_index.reserved))
			return false;
		break;
	}
	default:
		if (XE_IOCTL_DBG(xe, 1))
			return false;
	}

	if (XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return false;

	return true;
}

static bool check_bo_args_are_sane(struct xe_vm *vm, struct xe_vma **vmas,
				   int num_vmas, u32 atomic_val)
{
	struct xe_device *xe = vm->xe;
	struct xe_bo *bo;
	int i;

	for (i = 0; i < num_vmas; i++) {
		bo = xe_vma_bo(vmas[i]);
		if (!bo)
			continue;
		/*
		 * NOTE: The following atomic checks are platform-specific. For example,
		 * if a device supports CXL atomics, these may not be necessary or
		 * may behave differently.
		 */
		if (XE_IOCTL_DBG(xe, atomic_val == DRM_XE_ATOMIC_CPU &&
				 !(bo->flags & XE_BO_FLAG_SYSTEM)))
			return false;

		if (XE_IOCTL_DBG(xe, atomic_val == DRM_XE_ATOMIC_DEVICE &&
				 !(bo->flags & XE_BO_FLAG_VRAM0) &&
				 !(bo->flags & XE_BO_FLAG_VRAM1) &&
				 !(bo->flags & XE_BO_FLAG_SYSTEM &&
				   xe->info.has_device_atomics_on_smem)))
			return false;

		if (XE_IOCTL_DBG(xe, atomic_val == DRM_XE_ATOMIC_GLOBAL &&
				 (!(bo->flags & XE_BO_FLAG_SYSTEM) ||
				  (!(bo->flags & XE_BO_FLAG_VRAM0) &&
				   !(bo->flags & XE_BO_FLAG_VRAM1)))))
			return false;
	}
	return true;
}
/**
 * xe_vm_madvise_ioctl - Handle MADVise ioctl for a VM
 * @dev: DRM device pointer
 * @data: Pointer to ioctl data (drm_xe_madvise*)
 * @file: DRM file pointer
 *
 * Handles the MADVISE ioctl to provide memory advice for vma's within
 * input range.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_vm_madvise_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_madvise *args = data;
	struct xe_vmas_in_madvise_range madvise_range = {.addr = args->start,
							 .range =  args->range, };
	struct xe_vm *vm;
	struct drm_exec exec;
	int err, attr_type;

	vm = xe_vm_lookup(xef, args->vm_id);
	if (XE_IOCTL_DBG(xe, !vm))
		return -EINVAL;

	if (!madvise_args_are_sane(vm->xe, args)) {
		err = -EINVAL;
		goto put_vm;
	}

	xe_svm_flush(vm);

	err = down_write_killable(&vm->lock);
	if (err)
		goto put_vm;

	if (XE_IOCTL_DBG(xe, xe_vm_is_closed_or_banned(vm))) {
		err = -ENOENT;
		goto unlock_vm;
	}

	err = xe_vm_alloc_madvise_vma(vm, args->start, args->range);
	if (err)
		goto unlock_vm;

	err = get_vmas(vm, &madvise_range);
	if (err || !madvise_range.num_vmas)
		goto unlock_vm;

	if (madvise_range.has_bo_vmas) {
		if (args->type == DRM_XE_MEM_RANGE_ATTR_ATOMIC) {
			if (!check_bo_args_are_sane(vm, madvise_range.vmas,
						    madvise_range.num_vmas,
						    args->atomic.val)) {
				err = -EINVAL;
				goto unlock_vm;
			}
		}

		drm_exec_init(&exec, DRM_EXEC_IGNORE_DUPLICATES | DRM_EXEC_INTERRUPTIBLE_WAIT, 0);
		drm_exec_until_all_locked(&exec) {
			for (int i = 0; i < madvise_range.num_vmas; i++) {
				struct xe_bo *bo = xe_vma_bo(madvise_range.vmas[i]);

				if (!bo)
					continue;
				err = drm_exec_lock_obj(&exec, &bo->ttm.base);
				drm_exec_retry_on_contention(&exec);
				if (err)
					goto err_fini;
			}
		}
	}

	if (madvise_range.has_svm_userptr_vmas) {
		err = xe_svm_notifier_lock_interruptible(vm);
		if (err)
			goto err_fini;
	}

	attr_type = array_index_nospec(args->type, ARRAY_SIZE(madvise_funcs));
	madvise_funcs[attr_type](xe, vm, madvise_range.vmas, madvise_range.num_vmas, args);

	err = xe_vm_invalidate_madvise_range(vm, args->start, args->start + args->range);

	if (madvise_range.has_svm_userptr_vmas)
		xe_svm_notifier_unlock(vm);

err_fini:
	if (madvise_range.has_bo_vmas)
		drm_exec_fini(&exec);
	kfree(madvise_range.vmas);
	madvise_range.vmas = NULL;
unlock_vm:
	up_write(&vm->lock);
put_vm:
	xe_vm_put(vm);
	return err;
}
