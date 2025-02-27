// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_sa.h"

#include <linux/kernel.h>

#include <drm/drm_managed.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_map.h"

static void xe_sa_bo_manager_fini(struct drm_device *drm, void *arg)
{
	struct xe_sa_manager *sa_manager = arg;
	struct xe_bo *bo = sa_manager->bo;

	if (!bo) {
		drm_err(drm, "no bo for sa manager\n");
		return;
	}

	drm_suballoc_manager_fini(&sa_manager->base);

	if (sa_manager->is_iomem)
		kvfree(sa_manager->cpu_ptr);

	sa_manager->bo = NULL;
}

/**
 * __xe_sa_bo_manager_init() - Create and initialize the suballocator
 * @tile: the &xe_tile where allocate
 * @size: number of bytes to allocate
 * @guard: number of bytes to exclude from suballocations
 * @align: alignment for each suballocated chunk
 *
 * Prepares the suballocation manager for suballocations.
 *
 * Return: a pointer to the &xe_sa_manager or an ERR_PTR on failure.
 */
struct xe_sa_manager *__xe_sa_bo_manager_init(struct xe_tile *tile, u32 size, u32 guard, u32 align)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_sa_manager *sa_manager;
	u32 managed_size;
	struct xe_bo *bo;
	int ret;

	xe_tile_assert(tile, size > guard);
	managed_size = size - guard;

	sa_manager = drmm_kzalloc(&xe->drm, sizeof(*sa_manager), GFP_KERNEL);
	if (!sa_manager)
		return ERR_PTR(-ENOMEM);

	bo = xe_managed_bo_create_pin_map(xe, tile, size,
					  XE_BO_FLAG_VRAM_IF_DGFX(tile) |
					  XE_BO_FLAG_GGTT |
					  XE_BO_FLAG_GGTT_INVALIDATE);
	if (IS_ERR(bo)) {
		drm_err(&xe->drm, "Failed to prepare %uKiB BO for SA manager (%pe)\n",
			size / SZ_1K, bo);
		return ERR_CAST(bo);
	}
	sa_manager->bo = bo;
	sa_manager->is_iomem = bo->vmap.is_iomem;
	sa_manager->gpu_addr = xe_bo_ggtt_addr(bo);

	if (bo->vmap.is_iomem) {
		sa_manager->cpu_ptr = kvzalloc(managed_size, GFP_KERNEL);
		if (!sa_manager->cpu_ptr)
			return ERR_PTR(-ENOMEM);
	} else {
		sa_manager->cpu_ptr = bo->vmap.vaddr;
		memset(sa_manager->cpu_ptr, 0, bo->ttm.base.size);
	}

	drm_suballoc_manager_init(&sa_manager->base, managed_size, align);
	ret = drmm_add_action_or_reset(&xe->drm, xe_sa_bo_manager_fini,
				       sa_manager);
	if (ret)
		return ERR_PTR(ret);

	return sa_manager;
}

/**
 * __xe_sa_bo_new() - Make a suballocation but use custom gfp flags.
 * @sa_manager: the &xe_sa_manager
 * @size: number of bytes we want to suballocate
 * @gfp: gfp flags used for memory allocation. Typically GFP_KERNEL.
 *
 * Try to make a suballocation of size @size.
 *
 * Return: a &drm_suballoc, or an ERR_PTR.
 */
struct drm_suballoc *__xe_sa_bo_new(struct xe_sa_manager *sa_manager, u32 size, gfp_t gfp)
{
	/*
	 * BB to large, return -ENOBUFS indicating user should split
	 * array of binds into smaller chunks.
	 */
	if (size > sa_manager->base.size)
		return ERR_PTR(-ENOBUFS);

	return drm_suballoc_new(&sa_manager->base, size, gfp, true, 0);
}

void xe_sa_bo_flush_write(struct drm_suballoc *sa_bo)
{
	struct xe_sa_manager *sa_manager = to_xe_sa_manager(sa_bo->manager);
	struct xe_device *xe = tile_to_xe(sa_manager->bo->tile);

	if (!sa_manager->bo->vmap.is_iomem)
		return;

	xe_map_memcpy_to(xe, &sa_manager->bo->vmap, drm_suballoc_soffset(sa_bo),
			 xe_sa_bo_cpu_addr(sa_bo),
			 drm_suballoc_size(sa_bo));
}

void xe_sa_bo_free(struct drm_suballoc *sa_bo,
		   struct dma_fence *fence)
{
	drm_suballoc_free(sa_bo, fence);
}
