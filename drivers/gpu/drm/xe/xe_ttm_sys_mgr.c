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

struct xe_ttm_sys_node {
	struct ttm_buffer_object *tbo;
	struct ttm_range_mgr_node base;
};

static inline struct xe_ttm_sys_node *
to_xe_ttm_sys_node(struct ttm_resource *res)
{
	return container_of(res, struct xe_ttm_sys_node, base.base);
}

static int xe_ttm_sys_mgr_new(struct ttm_resource_manager *man,
			      struct ttm_buffer_object *tbo,
			      const struct ttm_place *place,
			      struct ttm_resource **res)
{
	struct xe_ttm_sys_node *node;
	int r;

	node = kzalloc(struct_size(node, base.mm_nodes, 1), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->tbo = tbo;
	ttm_resource_init(tbo, place, &node->base.base);

	if (!(place->flags & TTM_PL_FLAG_TEMPORARY) &&
	    ttm_resource_manager_usage(man) > (man->size << PAGE_SHIFT)) {
		r = -ENOSPC;
		goto err_fini;
	}

	node->base.mm_nodes[0].start = 0;
	node->base.mm_nodes[0].size = PFN_UP(node->base.base.size);
	node->base.base.start = XE_BO_INVALID_OFFSET;

	*res = &node->base.base;

	return 0;

err_fini:
	ttm_resource_fini(man, &node->base.base);
	kfree(node);
	return r;
}

static void xe_ttm_sys_mgr_del(struct ttm_resource_manager *man,
			       struct ttm_resource *res)
{
	struct xe_ttm_sys_node *node = to_xe_ttm_sys_node(res);

	ttm_resource_fini(man, res);
	kfree(node);
}

static void xe_ttm_sys_mgr_debug(struct ttm_resource_manager *man,
				 struct drm_printer *printer)
{
	/*
	 * This function is called by debugfs entry and would require
	 * pm_runtime_{get,put} wrappers around any operation.
	 */
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
	/* Potentially restrict amount of TT memory here. */
	gtt_size = (u64)si.totalram * si.mem_unit;

	man->use_tt = true;
	man->func = &xe_ttm_sys_mgr_func;
	ttm_resource_manager_init(man, &xe->ttm, gtt_size >> PAGE_SHIFT);
	ttm_set_driver_manager(&xe->ttm, XE_PL_TT, man);
	ttm_resource_manager_set_used(man, true);
	return drmm_add_action_or_reset(&xe->drm, ttm_sys_mgr_fini, xe);
}
