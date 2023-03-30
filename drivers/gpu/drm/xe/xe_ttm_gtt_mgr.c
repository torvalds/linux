// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2022 Intel Corporation
 * Copyright (C) 2021-2002 Red Hat
 */

#include <drm/drm_managed.h>

#include <drm/ttm/ttm_range_manager.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#include "xe_bo.h"
#include "xe_gt.h"
#include "xe_ttm_gtt_mgr.h"

struct xe_ttm_gtt_node {
	struct ttm_buffer_object *tbo;
	struct ttm_range_mgr_node base;
};

static inline struct xe_ttm_gtt_mgr *
to_gtt_mgr(struct ttm_resource_manager *man)
{
	return container_of(man, struct xe_ttm_gtt_mgr, manager);
}

static inline struct xe_ttm_gtt_node *
to_xe_ttm_gtt_node(struct ttm_resource *res)
{
	return container_of(res, struct xe_ttm_gtt_node, base.base);
}

static int xe_ttm_gtt_mgr_new(struct ttm_resource_manager *man,
			      struct ttm_buffer_object *tbo,
			      const struct ttm_place *place,
			      struct ttm_resource **res)
{
	struct xe_ttm_gtt_node *node;
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

static void xe_ttm_gtt_mgr_del(struct ttm_resource_manager *man,
			       struct ttm_resource *res)
{
	struct xe_ttm_gtt_node *node = to_xe_ttm_gtt_node(res);

	ttm_resource_fini(man, res);
	kfree(node);
}

static void xe_ttm_gtt_mgr_debug(struct ttm_resource_manager *man,
				 struct drm_printer *printer)
{

}

static const struct ttm_resource_manager_func xe_ttm_gtt_mgr_func = {
	.alloc = xe_ttm_gtt_mgr_new,
	.free = xe_ttm_gtt_mgr_del,
	.debug = xe_ttm_gtt_mgr_debug
};

static void ttm_gtt_mgr_fini(struct drm_device *drm, void *arg)
{
	struct xe_ttm_gtt_mgr *mgr = arg;
	struct xe_device *xe = gt_to_xe(mgr->gt);
	struct ttm_resource_manager *man = &mgr->manager;
	int err;

	ttm_resource_manager_set_used(man, false);

	err = ttm_resource_manager_evict_all(&xe->ttm, man);
	if (err)
		return;

	ttm_resource_manager_cleanup(man);
	ttm_set_driver_manager(&xe->ttm, XE_PL_TT, NULL);
}

int xe_ttm_gtt_mgr_init(struct xe_gt *gt, struct xe_ttm_gtt_mgr *mgr,
			u64 gtt_size)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct ttm_resource_manager *man = &mgr->manager;
	int err;

	XE_BUG_ON(xe_gt_is_media_type(gt));

	mgr->gt = gt;
	man->use_tt = true;
	man->func = &xe_ttm_gtt_mgr_func;

	ttm_resource_manager_init(man, &xe->ttm, gtt_size >> PAGE_SHIFT);

	ttm_set_driver_manager(&xe->ttm, XE_PL_TT, &mgr->manager);
	ttm_resource_manager_set_used(man, true);

	err = drmm_add_action_or_reset(&xe->drm, ttm_gtt_mgr_fini, mgr);
	if (err)
		return err;

	return 0;
}
