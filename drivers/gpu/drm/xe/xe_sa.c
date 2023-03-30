// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/kernel.h>
#include <drm/drm_managed.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_map.h"
#include "xe_sa.h"

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

int xe_sa_bo_manager_init(struct xe_gt *gt,
			  struct xe_sa_manager *sa_manager,
			  u32 size, u32 align)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 managed_size = size - SZ_4K;
	struct xe_bo *bo;

	sa_manager->bo = NULL;

	bo = xe_bo_create_pin_map(xe, gt, NULL, size, ttm_bo_type_kernel,
				  XE_BO_CREATE_VRAM_IF_DGFX(gt) |
				  XE_BO_CREATE_GGTT_BIT);
	if (IS_ERR(bo)) {
		drm_err(&xe->drm, "failed to allocate bo for sa manager: %ld\n",
			PTR_ERR(bo));
		return PTR_ERR(bo);
	}
	sa_manager->bo = bo;

	drm_suballoc_manager_init(&sa_manager->base, managed_size, align);
	sa_manager->gpu_addr = xe_bo_ggtt_addr(bo);

	if (bo->vmap.is_iomem) {
		sa_manager->cpu_ptr = kvzalloc(managed_size, GFP_KERNEL);
		if (!sa_manager->cpu_ptr) {
			xe_bo_unpin_map_no_vm(sa_manager->bo);
			sa_manager->bo = NULL;
			return -ENOMEM;
		}
	} else {
		sa_manager->cpu_ptr = bo->vmap.vaddr;
		memset(sa_manager->cpu_ptr, 0, bo->ttm.base.size);
	}

	return drmm_add_action_or_reset(&xe->drm, xe_sa_bo_manager_fini,
					sa_manager);
}

struct drm_suballoc *xe_sa_bo_new(struct xe_sa_manager *sa_manager,
				  unsigned size)
{
	return drm_suballoc_new(&sa_manager->base, size, GFP_KERNEL, true, 0);
}

void xe_sa_bo_flush_write(struct drm_suballoc *sa_bo)
{
	struct xe_sa_manager *sa_manager = to_xe_sa_manager(sa_bo->manager);
	struct xe_device *xe = gt_to_xe(sa_manager->bo->gt);

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
