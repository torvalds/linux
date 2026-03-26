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
#include "xe_tlb_inval.h"
#include "xe_vm.h"

struct xe_vmas_in_madvise_range {
	u64 addr;
	u64 range;
	struct xe_vma **vmas;
	int num_vmas;
	bool has_bo_vmas;
	bool has_svm_userptr_vmas;
};

/**
 * struct xe_madvise_details - Argument to madvise_funcs
 * @dpagemap: Reference-counted pointer to a struct drm_pagemap.
 * @has_purged_bo: Track if any BO was purged (for purgeable state)
 * @retained_ptr: User pointer for retained value (for purgeable state)
 *
 * The madvise IOCTL handler may, in addition to the user-space
 * args, have additional info to pass into the madvise_func that
 * handles the madvise type. Use a struct_xe_madvise_details
 * for that and extend the struct as necessary.
 */
struct xe_madvise_details {
	struct drm_pagemap *dpagemap;
	bool has_purged_bo;
	u64 retained_ptr;
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
	madvise_range->vmas = kmalloc_objs(*madvise_range->vmas, max_vmas);
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
				      struct drm_xe_madvise *op,
				      struct xe_madvise_details *details)
{
	int i;

	xe_assert(vm->xe, op->type == DRM_XE_MEM_RANGE_ATTR_PREFERRED_LOC);

	for (i = 0; i < num_vmas; i++) {
		struct xe_vma *vma = vmas[i];
		struct xe_vma_preferred_loc *loc = &vma->attr.preferred_loc;

		/*TODO: Extend attributes to bo based vmas */
		if ((loc->devmem_fd == op->preferred_mem_loc.devmem_fd &&
		     loc->migration_policy == op->preferred_mem_loc.migration_policy) ||
		    !xe_vma_is_cpu_addr_mirror(vma)) {
			vma->skip_invalidation = true;
		} else {
			vma->skip_invalidation = false;
			loc->devmem_fd = op->preferred_mem_loc.devmem_fd;
			/* Till multi-device support is not added migration_policy
			 * is of no use and can be ignored.
			 */
			loc->migration_policy = op->preferred_mem_loc.migration_policy;
			drm_pagemap_put(loc->dpagemap);
			loc->dpagemap = NULL;
			if (details->dpagemap)
				loc->dpagemap = drm_pagemap_get(details->dpagemap);
		}
	}
}

static void madvise_atomic(struct xe_device *xe, struct xe_vm *vm,
			   struct xe_vma **vmas, int num_vmas,
			   struct drm_xe_madvise *op,
			   struct xe_madvise_details *details)
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
			      struct drm_xe_madvise *op,
			      struct xe_madvise_details *details)
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

/**
 * xe_bo_is_dmabuf_shared() - Check if BO is shared via dma-buf
 * @bo: Buffer object
 *
 * Prevent marking imported or exported dma-bufs as purgeable.
 * For imported BOs, Xe doesn't own the backing store and cannot
 * safely reclaim pages (exporter or other devices may still be
 * using them). For exported BOs, external devices may have active
 * mappings we cannot track.
 *
 * Return: true if BO is imported or exported, false otherwise
 */
static bool xe_bo_is_dmabuf_shared(struct xe_bo *bo)
{
	struct drm_gem_object *obj = &bo->ttm.base;

	/* Imported: exporter owns backing store */
	if (drm_gem_is_imported(obj))
		return true;

	/* Exported: external devices may be accessing */
	if (obj->dma_buf)
		return true;

	return false;
}

/**
 * enum xe_bo_vmas_purge_state - VMA purgeable state aggregation
 *
 * Distinguishes whether a BO's VMAs are all DONTNEED, have at least
 * one WILLNEED, or have no VMAs at all.
 *
 * Enum values align with XE_MADV_PURGEABLE_* states for consistency.
 */
enum xe_bo_vmas_purge_state {
	/** @XE_BO_VMAS_STATE_WILLNEED: At least one VMA is WILLNEED */
	XE_BO_VMAS_STATE_WILLNEED = 0,
	/** @XE_BO_VMAS_STATE_DONTNEED: All VMAs are DONTNEED */
	XE_BO_VMAS_STATE_DONTNEED = 1,
	/** @XE_BO_VMAS_STATE_NO_VMAS: BO has no VMAs */
	XE_BO_VMAS_STATE_NO_VMAS = 2,
};

/*
 * xe_bo_recompute_purgeable_state() casts between xe_bo_vmas_purge_state and
 * xe_madv_purgeable_state. Enforce that WILLNEED=0 and DONTNEED=1 match across
 * both enums so the single-line cast is always valid.
 */
static_assert(XE_BO_VMAS_STATE_WILLNEED == (int)XE_MADV_PURGEABLE_WILLNEED,
	      "VMA purge state WILLNEED must equal madv purgeable WILLNEED");
static_assert(XE_BO_VMAS_STATE_DONTNEED == (int)XE_MADV_PURGEABLE_DONTNEED,
	      "VMA purge state DONTNEED must equal madv purgeable DONTNEED");

/**
 * xe_bo_all_vmas_dontneed() - Determine BO VMA purgeable state
 * @bo: Buffer object
 *
 * Check all VMAs across all VMs to determine aggregate purgeable state.
 * Shared BOs require unanimous DONTNEED state from all mappings.
 *
 * Caller must hold BO dma-resv lock.
 *
 * Return: XE_BO_VMAS_STATE_DONTNEED if all VMAs are DONTNEED,
 *         XE_BO_VMAS_STATE_WILLNEED if at least one VMA is not DONTNEED,
 *         XE_BO_VMAS_STATE_NO_VMAS if BO has no VMAs
 */
static enum xe_bo_vmas_purge_state xe_bo_all_vmas_dontneed(struct xe_bo *bo)
{
	struct drm_gpuvm_bo *vm_bo;
	struct drm_gpuva *gpuva;
	struct drm_gem_object *obj = &bo->ttm.base;
	bool has_vmas = false;

	xe_bo_assert_held(bo);

	/* Shared dma-bufs cannot be purgeable */
	if (xe_bo_is_dmabuf_shared(bo))
		return XE_BO_VMAS_STATE_WILLNEED;

	drm_gem_for_each_gpuvm_bo(vm_bo, obj) {
		drm_gpuvm_bo_for_each_va(gpuva, vm_bo) {
			struct xe_vma *vma = gpuva_to_vma(gpuva);

			has_vmas = true;

			/* Any non-DONTNEED VMA prevents purging */
			if (vma->attr.purgeable_state != XE_MADV_PURGEABLE_DONTNEED)
				return XE_BO_VMAS_STATE_WILLNEED;
		}
	}

	/*
	 * No VMAs => preserve existing BO purgeable state.
	 * Avoids incorrectly flipping DONTNEED -> WILLNEED when last VMA unmapped.
	 */
	if (!has_vmas)
		return XE_BO_VMAS_STATE_NO_VMAS;

	return XE_BO_VMAS_STATE_DONTNEED;
}

/**
 * xe_bo_recompute_purgeable_state() - Recompute BO purgeable state from VMAs
 * @bo: Buffer object
 *
 * Walk all VMAs to determine if BO should be purgeable or not.
 * Shared BOs require unanimous DONTNEED state from all mappings.
 * If the BO has no VMAs the existing state is preserved.
 *
 * Locking: Caller must hold BO dma-resv lock. When iterating GPUVM lists,
 * VM lock must also be held (write) to prevent concurrent VMA modifications.
 * This is satisfied at both call sites:
 * - xe_vma_destroy(): holds vm->lock write
 * - madvise_purgeable(): holds vm->lock write (from madvise ioctl path)
 *
 * Return: nothing
 */
void xe_bo_recompute_purgeable_state(struct xe_bo *bo)
{
	enum xe_bo_vmas_purge_state vma_state;

	if (!bo)
		return;

	xe_bo_assert_held(bo);

	/*
	 * Once purged, always purged. Cannot transition back to WILLNEED.
	 * This matches i915 semantics where purged BOs are permanently invalid.
	 */
	if (bo->madv_purgeable == XE_MADV_PURGEABLE_PURGED)
		return;

	vma_state = xe_bo_all_vmas_dontneed(bo);

	if (vma_state != (enum xe_bo_vmas_purge_state)bo->madv_purgeable &&
	    vma_state != XE_BO_VMAS_STATE_NO_VMAS)
		xe_bo_set_purgeable_state(bo, (enum xe_madv_purgeable_state)vma_state);
}

/**
 * madvise_purgeable - Handle purgeable buffer object advice
 * @xe: XE device
 * @vm: VM
 * @vmas: Array of VMAs
 * @num_vmas: Number of VMAs
 * @op: Madvise operation
 * @details: Madvise details for return values
 *
 * Handles DONTNEED/WILLNEED/PURGED states. Tracks if any BO was purged
 * in details->has_purged_bo for later copy to userspace.
 */
static void madvise_purgeable(struct xe_device *xe, struct xe_vm *vm,
			      struct xe_vma **vmas, int num_vmas,
			      struct drm_xe_madvise *op,
			      struct xe_madvise_details *details)
{
	int i;

	xe_assert(vm->xe, op->type == DRM_XE_VMA_ATTR_PURGEABLE_STATE);

	for (i = 0; i < num_vmas; i++) {
		struct xe_bo *bo = xe_vma_bo(vmas[i]);

		if (!bo) {
			/* Purgeable state applies to BOs only, skip non-BO VMAs */
			vmas[i]->skip_invalidation = true;
			continue;
		}

		/* BO must be locked before modifying madv state */
		xe_bo_assert_held(bo);

		/* Skip shared dma-bufs - no PTEs to zap */
		if (xe_bo_is_dmabuf_shared(bo)) {
			vmas[i]->skip_invalidation = true;
			continue;
		}

		/*
		 * Once purged, always purged. Cannot transition back to WILLNEED.
		 * This matches i915 semantics where purged BOs are permanently invalid.
		 */
		if (xe_bo_is_purged(bo)) {
			details->has_purged_bo = true;
			vmas[i]->skip_invalidation = true;
			continue;
		}

		switch (op->purge_state_val.val) {
		case DRM_XE_VMA_PURGEABLE_STATE_WILLNEED:
			vmas[i]->attr.purgeable_state = XE_MADV_PURGEABLE_WILLNEED;
			vmas[i]->skip_invalidation = true;

			xe_bo_recompute_purgeable_state(bo);
			break;
		case DRM_XE_VMA_PURGEABLE_STATE_DONTNEED:
			vmas[i]->attr.purgeable_state = XE_MADV_PURGEABLE_DONTNEED;
			/*
			 * Don't zap PTEs at DONTNEED time -- pages are still
			 * alive. The zap happens in xe_bo_move_notify() right
			 * before the shrinker frees them.
			 */
			vmas[i]->skip_invalidation = true;

			xe_bo_recompute_purgeable_state(bo);
			break;
		default:
			/* Should never hit - values validated in madvise_args_are_sane() */
			xe_assert(vm->xe, 0);
			return;
		}
	}
}

typedef void (*madvise_func)(struct xe_device *xe, struct xe_vm *vm,
			     struct xe_vma **vmas, int num_vmas,
			     struct drm_xe_madvise *op,
			     struct xe_madvise_details *details);

static const madvise_func madvise_funcs[] = {
	[DRM_XE_MEM_RANGE_ATTR_PREFERRED_LOC] = madvise_preferred_mem_loc,
	[DRM_XE_MEM_RANGE_ATTR_ATOMIC] = madvise_atomic,
	[DRM_XE_MEM_RANGE_ATTR_PAT] = madvise_pat_index,
	[DRM_XE_VMA_ATTR_PURGEABLE_STATE] = madvise_purgeable,
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
	struct xe_tlb_inval_batch batch;
	int err;

	if (!tile_mask)
		return 0;

	xe_device_wmb(vm->xe);

	err = xe_tlb_inval_range_tilemask_submit(vm->xe, vm->usm.asid, start, end,
						 tile_mask, &batch);
	if (!err)
		xe_tlb_inval_batch_wait(&batch);

	return err;
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

		if (XE_IOCTL_DBG(xe, fd <= DRM_XE_PREFERRED_LOC_DEFAULT_DEVICE &&
				 args->preferred_mem_loc.region_instance != 0))
			return false;

		if (XE_IOCTL_DBG(xe, args->preferred_mem_loc.migration_policy >
				     DRM_XE_MIGRATE_ONLY_SYSTEM_PAGES))
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
		u16 pat_index, coh_mode;

		if (XE_IOCTL_DBG(xe, args->pat_index.val >= xe->pat.n_entries))
			return false;

		pat_index = array_index_nospec(args->pat_index.val, xe->pat.n_entries);
		coh_mode = xe_pat_index_get_coh_mode(xe, pat_index);
		if (XE_IOCTL_DBG(xe, !coh_mode))
			return false;

		if (XE_WARN_ON(coh_mode > XE_COH_2WAY))
			return false;

		if (XE_IOCTL_DBG(xe, args->pat_index.pad))
			return false;

		if (XE_IOCTL_DBG(xe, args->pat_index.reserved))
			return false;
		break;
	}
	case DRM_XE_VMA_ATTR_PURGEABLE_STATE:
	{
		u32 val = args->purge_state_val.val;

		if (XE_IOCTL_DBG(xe, !(val == DRM_XE_VMA_PURGEABLE_STATE_WILLNEED ||
				       val == DRM_XE_VMA_PURGEABLE_STATE_DONTNEED)))
			return false;

		if (XE_IOCTL_DBG(xe, args->purge_state_val.pad))
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

static int xe_madvise_details_init(struct xe_vm *vm, const struct drm_xe_madvise *args,
				   struct xe_madvise_details *details)
{
	struct xe_device *xe = vm->xe;

	memset(details, 0, sizeof(*details));

	/* Store retained pointer for purgeable state */
	if (args->type == DRM_XE_VMA_ATTR_PURGEABLE_STATE) {
		details->retained_ptr = args->purge_state_val.retained_ptr;
		return 0;
	}

	if (args->type == DRM_XE_MEM_RANGE_ATTR_PREFERRED_LOC) {
		int fd = args->preferred_mem_loc.devmem_fd;
		struct drm_pagemap *dpagemap;

		if (fd <= 0)
			return 0;

		dpagemap = xe_drm_pagemap_from_fd(args->preferred_mem_loc.devmem_fd,
						  args->preferred_mem_loc.region_instance);
		if (XE_IOCTL_DBG(xe, IS_ERR(dpagemap)))
			return PTR_ERR(dpagemap);

		/* Don't allow a foreign placement without a fast interconnect! */
		if (XE_IOCTL_DBG(xe, dpagemap->pagemap->owner != vm->svm.peer.owner)) {
			drm_pagemap_put(dpagemap);
			return -ENOLINK;
		}
		details->dpagemap = dpagemap;
	}

	return 0;
}

static void xe_madvise_details_fini(struct xe_madvise_details *details)
{
	drm_pagemap_put(details->dpagemap);
}

static int xe_madvise_purgeable_retained_to_user(const struct xe_madvise_details *details)
{
	u32 retained;

	if (!details->retained_ptr)
		return 0;

	retained = !details->has_purged_bo;

	if (put_user(retained, (u32 __user *)u64_to_user_ptr(details->retained_ptr)))
		return -EFAULT;

	return 0;
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
	struct xe_vmas_in_madvise_range madvise_range = {
		/*
		 * Userspace may pass canonical (sign-extended) addresses.
		 * Strip the sign extension to get the internal non-canonical
		 * form used by the GPUVM, matching xe_vm_bind_ioctl() behavior.
		 */
		.addr = xe_device_uncanonicalize_addr(xe, args->start),
		.range = args->range,
	};
	struct xe_madvise_details details;
	u16 pat_index, coh_mode;
	struct xe_vm *vm;
	struct drm_exec exec;
	int err, attr_type;
	bool do_retained;

	vm = xe_vm_lookup(xef, args->vm_id);
	if (XE_IOCTL_DBG(xe, !vm))
		return -EINVAL;

	if (!madvise_args_are_sane(vm->xe, args)) {
		err = -EINVAL;
		goto put_vm;
	}

	/* Cache whether we need to write retained, and validate it's initialized to 0 */
	do_retained = args->type == DRM_XE_VMA_ATTR_PURGEABLE_STATE &&
		      args->purge_state_val.retained_ptr;
	if (do_retained) {
		u32 retained;
		u32 __user *retained_ptr;

		retained_ptr = u64_to_user_ptr(args->purge_state_val.retained_ptr);
		if (get_user(retained, retained_ptr)) {
			err = -EFAULT;
			goto put_vm;
		}

		if (XE_IOCTL_DBG(xe, retained != 0)) {
			err = -EINVAL;
			goto put_vm;
		}
	}

	xe_svm_flush(vm);

	err = down_write_killable(&vm->lock);
	if (err)
		goto put_vm;

	if (XE_IOCTL_DBG(xe, xe_vm_is_closed_or_banned(vm))) {
		err = -ENOENT;
		goto unlock_vm;
	}

	err = xe_madvise_details_init(vm, args, &details);
	if (err)
		goto unlock_vm;

	err = xe_vm_alloc_madvise_vma(vm, madvise_range.addr, args->range);
	if (err)
		goto madv_fini;

	err = get_vmas(vm, &madvise_range);
	if (err || !madvise_range.num_vmas)
		goto madv_fini;

	if (args->type == DRM_XE_MEM_RANGE_ATTR_PAT) {
		pat_index = array_index_nospec(args->pat_index.val, xe->pat.n_entries);
		coh_mode = xe_pat_index_get_coh_mode(xe, pat_index);
		if (XE_IOCTL_DBG(xe, madvise_range.has_svm_userptr_vmas &&
				 xe_device_is_l2_flush_optimized(xe) &&
				 (pat_index != 19 && coh_mode != XE_COH_2WAY))) {
			err = -EINVAL;
			goto madv_fini;
		}
	}

	if (madvise_range.has_bo_vmas) {
		if (args->type == DRM_XE_MEM_RANGE_ATTR_ATOMIC) {
			if (!check_bo_args_are_sane(vm, madvise_range.vmas,
						    madvise_range.num_vmas,
						    args->atomic.val)) {
				err = -EINVAL;
				goto free_vmas;
			}
		}

		drm_exec_init(&exec, DRM_EXEC_IGNORE_DUPLICATES | DRM_EXEC_INTERRUPTIBLE_WAIT, 0);
		drm_exec_until_all_locked(&exec) {
			for (int i = 0; i < madvise_range.num_vmas; i++) {
				struct xe_bo *bo = xe_vma_bo(madvise_range.vmas[i]);

				if (!bo)
					continue;

				if (args->type == DRM_XE_MEM_RANGE_ATTR_PAT) {
					if (XE_IOCTL_DBG(xe, bo->ttm.base.import_attach &&
							 xe_device_is_l2_flush_optimized(xe) &&
							 (pat_index != 19 &&
							  coh_mode != XE_COH_2WAY))) {
						err = -EINVAL;
						goto err_fini;
					}
				}

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

	/* Ensure the madvise function exists for this type */
	if (!madvise_funcs[attr_type]) {
		err = -EINVAL;
		goto err_fini;
	}

	madvise_funcs[attr_type](xe, vm, madvise_range.vmas, madvise_range.num_vmas, args,
				 &details);

	err = xe_vm_invalidate_madvise_range(vm, madvise_range.addr,
					     madvise_range.addr + args->range);

	if (madvise_range.has_svm_userptr_vmas)
		xe_svm_notifier_unlock(vm);

err_fini:
	if (madvise_range.has_bo_vmas)
		drm_exec_fini(&exec);
free_vmas:
	kfree(madvise_range.vmas);
	madvise_range.vmas = NULL;
madv_fini:
	xe_madvise_details_fini(&details);
unlock_vm:
	up_write(&vm->lock);

	/* Write retained value to user after releasing all locks */
	if (!err && do_retained)
		err = xe_madvise_purgeable_retained_to_user(&details);
put_vm:
	xe_vm_put(vm);
	return err;
}
