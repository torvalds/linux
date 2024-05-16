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

	if (bo->vmap.is_iomem)
		kvfree(sa_manager->cpu_ptr);

	xe_bo_unpin_map_no_vm(bo);
	sa_manager->bo = NULL;
}

struct xe_sa_manager *xe_sa_bo_manager_init(struct xe_tile *tile, u32 size, u32 align)
{
	struct xe_device *xe = tile_to_xe(tile);
	u32 managed_size = size - SZ_4K;
	struct xe_bo *bo;
	int ret;

	struct xe_sa_manager *sa_manager = drmm_kzalloc(&tile_to_xe(tile)->drm,
							sizeof(*sa_manager),
							GFP_KERNEL);
	if (!sa_manager)
		return ERR_PTR(-ENOMEM);

	sa_manager->bo = NULL;

	bo = xe_bo_create_pin_map(xe, tile, NULL, size, ttm_bo_type_kernel,
				  XE_BO_FLAG_VRAM_IF_DGFX(tile) |
				  XE_BO_FLAG_GGTT |
				  XE_BO_FLAG_GGTT_INVALIDATE);
	if (IS_ERR(bo)) {
		drm_err(&xe->drm, "failed to allocate bo for sa manager: %ld\n",
			PTR_ERR(bo));
		return (struct xe_sa_manager *)bo;
	}
	sa_manager->bo = bo;

	drm_suballoc_manager_init(&sa_manager->base, managed_size, align);
	sa_manager->gpu_addr = xe_bo_ggtt_addr(bo);

	if (bo->vmap.is_iomem) {
		sa_manager->cpu_ptr = kvzalloc(managed_size, GFP_KERNEL);
		if (!sa_manager->cpu_ptr) {
			xe_bo_unpin_map_no_vm(sa_manager->bo);
			sa_manager->bo = NULL;
			return ERR_PTR(-ENOMEM);
		}
	} else {
		sa_manager->cpu_ptr = bo->vmap.vaddr;
		memset(sa_manager->cpu_ptr, 0, bo->ttm.base.size);
	}

	ret = drmm_add_action_or_reset(&xe->drm, xe_sa_bo_manager_fini,
				       sa_manager);
	if (ret)
		return ERR_PTR(ret);

	return sa_manager;
}

struct drm_suballoc *xe_sa_bo_new(struct xe_sa_manager *sa_manager,
				  unsigned int size)
{
	return drm_suballoc_new(&sa_manager->base, size, GFP_KERNEL, true, 0);
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
