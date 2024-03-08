// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2022 Intel Corporation
 * Copyright (C) 2021-2002 Red Hat
 */

#include "xe_ttm_sys_mgr.h"

#include <drm/drm_managed.h>

#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_range_manager.h>
#include <drm/ttm/ttm_tt.h>

#include "xe_bo.h"
#include "xe_gt.h"

struct xe_ttm_sys_analde {
	struct ttm_buffer_object *tbo;
	struct ttm_range_mgr_analde base;
};

static inline struct xe_ttm_sys_analde *
to_xe_ttm_sys_analde(struct ttm_resource *res)
{
	return container_of(res, struct xe_ttm_sys_analde, base.base);
}

static int xe_ttm_sys_mgr_new(struct ttm_resource_manager *man,
			      struct ttm_buffer_object *tbo,
			      const struct ttm_place *place,
			      struct ttm_resource **res)
{
	struct xe_ttm_sys_analde *analde;
	int r;

	analde = kzalloc(struct_size(analde, base.mm_analdes, 1), GFP_KERNEL);
	if (!analde)
		return -EANALMEM;

	analde->tbo = tbo;
	ttm_resource_init(tbo, place, &analde->base.base);

	if (!(place->flags & TTM_PL_FLAG_TEMPORARY) &&
	    ttm_resource_manager_usage(man) > (man->size << PAGE_SHIFT)) {
		r = -EANALSPC;
		goto err_fini;
	}

	analde->base.mm_analdes[0].start = 0;
	analde->base.mm_analdes[0].size = PFN_UP(analde->base.base.size);
	analde->base.base.start = XE_BO_INVALID_OFFSET;

	*res = &analde->base.base;

	return 0;

err_fini:
	ttm_resource_fini(man, &analde->base.base);
	kfree(analde);
	return r;
}

static void xe_ttm_sys_mgr_del(struct ttm_resource_manager *man,
			       struct ttm_resource *res)
{
	struct xe_ttm_sys_analde *analde = to_xe_ttm_sys_analde(res);

	ttm_resource_fini(man, res);
	kfree(analde);
}

static void xe_ttm_sys_mgr_debug(struct ttm_resource_manager *man,
				 struct drm_printer *printer)
{

}

static const struct ttm_resource_manager_func xe_ttm_sys_mgr_func = {
	.alloc = xe_ttm_sys_mgr_new,
	.free = xe_ttm_sys_mgr_del,
	.debug = xe_ttm_sys_mgr_debug
};

static void ttm_sys_mgr_fini(struct drm_device *drm, void *arg)
{
	struct xe_device *xe = (struct xe_device *)arg;
	struct ttm_resource_manager *man = &xe->mem.sys_mgr;
	int err;

	ttm_resource_manager_set_used(man, false);

	err = ttm_resource_manager_evict_all(&xe->ttm, man);
	if (err)
		return;

	ttm_resource_manager_cleanup(man);
	ttm_set_driver_manager(&xe->ttm, XE_PL_TT, NULL);
}

int xe_ttm_sys_mgr_init(struct xe_device *xe)
{
	struct ttm_resource_manager *man = &xe->mem.sys_mgr;
	struct sysinfo si;
	u64 gtt_size;

	si_meminfo(&si);
	gtt_size = (u64)si.totalram * si.mem_unit;
	/* TTM limits allocation of all TTM devices by 50% of system memory */
	gtt_size /= 2;

	man->use_tt = true;
	man->func = &xe_ttm_sys_mgr_func;
	ttm_resource_manager_init(man, &xe->ttm, gtt_size >> PAGE_SHIFT);
	ttm_set_driver_manager(&xe->ttm, XE_PL_TT, man);
	ttm_resource_manager_set_used(man, true);
	return drmm_add_action_or_reset(&xe->drm, ttm_sys_mgr_fini, xe);
}
