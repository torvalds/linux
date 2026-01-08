// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/intel/display_parent_interface.h>

#include "xe_res_cursor.h"
#include "xe_stolen.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_validation.h"

struct intel_stolen_node {
	struct xe_device *xe;
	struct xe_bo *bo;
};

static int xe_stolen_insert_node_in_range(struct intel_stolen_node *node, u64 size,
					  unsigned int align, u64 start, u64 end)
{
	struct xe_device *xe = node->xe;

	struct xe_bo *bo;
	int err = 0;
	u32 flags = XE_BO_FLAG_PINNED | XE_BO_FLAG_STOLEN;

	if (start < SZ_4K)
		start = SZ_4K;

	if (align) {
		size = ALIGN(size, align);
		start = ALIGN(start, align);
	}

	bo = xe_bo_create_pin_range_novm(xe, xe_device_get_root_tile(xe),
					 size, start, end, ttm_bo_type_kernel, flags);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		bo = NULL;
		return err;
	}

	node->bo = bo;

	return err;
}

static void xe_stolen_remove_node(struct intel_stolen_node *node)
{
	xe_bo_unpin_map_no_vm(node->bo);
	node->bo = NULL;
}

static bool xe_stolen_initialized(struct drm_device *drm)
{
	struct xe_device *xe = to_xe_device(drm);

	return ttm_manager_type(&xe->ttm, XE_PL_STOLEN);
}

static bool xe_stolen_node_allocated(const struct intel_stolen_node *node)
{
	return node->bo;
}

static u64 xe_stolen_node_offset(const struct intel_stolen_node *node)
{
	struct xe_res_cursor res;

	xe_res_first(node->bo->ttm.resource, 0, 4096, &res);
	return res.start;
}

static u64 xe_stolen_node_address(const struct intel_stolen_node *node)
{
	struct xe_device *xe = node->xe;

	return xe_ttm_stolen_gpu_offset(xe) + xe_stolen_node_offset(node);
}

static u64 xe_stolen_node_size(const struct intel_stolen_node *node)
{
	return xe_bo_size(node->bo);
}

static struct intel_stolen_node *xe_stolen_node_alloc(struct drm_device *drm)
{
	struct xe_device *xe = to_xe_device(drm);
	struct intel_stolen_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;

	node->xe = xe;

	return node;
}

static void xe_stolen_node_free(const struct intel_stolen_node *node)
{
	kfree(node);
}

const struct intel_display_stolen_interface xe_display_stolen_interface = {
	.insert_node_in_range = xe_stolen_insert_node_in_range,
	.remove_node = xe_stolen_remove_node,
	.initialized = xe_stolen_initialized,
	.node_allocated = xe_stolen_node_allocated,
	.node_offset = xe_stolen_node_offset,
	.node_address = xe_stolen_node_address,
	.node_size = xe_stolen_node_size,
	.node_alloc = xe_stolen_node_alloc,
	.node_free = xe_stolen_node_free,
};
