// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include "gem/i915_gem_stolen.h"
#include "xe_res_cursor.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_validation.h"

struct intel_stolen_node {
	struct xe_device *xe;
	struct xe_bo *bo;
};

int i915_gem_stolen_insert_node_in_range(struct intel_stolen_node *node, u64 size,
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

int i915_gem_stolen_insert_node(struct intel_stolen_node *node, u64 size, unsigned int align)
{
	/* Not used on xe */
	WARN_ON(1);

	return -ENODEV;
}

void i915_gem_stolen_remove_node(struct intel_stolen_node *node)
{
	xe_bo_unpin_map_no_vm(node->bo);
	node->bo = NULL;
}

bool i915_gem_stolen_initialized(struct drm_device *drm)
{
	struct xe_device *xe = to_xe_device(drm);

	return ttm_manager_type(&xe->ttm, XE_PL_STOLEN);
}

bool i915_gem_stolen_node_allocated(const struct intel_stolen_node *node)
{
	return node->bo;
}

u32 i915_gem_stolen_node_offset(struct intel_stolen_node *node)
{
	struct xe_res_cursor res;

	xe_res_first(node->bo->ttm.resource, 0, 4096, &res);
	return res.start;
}

/* Used for < gen4. These are not supported by Xe */
u64 i915_gem_stolen_area_address(struct drm_device *drm)
{
	WARN_ON(1);

	return 0;
}

/* Used for gen9 specific WA. Gen9 is not supported by Xe */
u64 i915_gem_stolen_area_size(struct drm_device *drm)
{
	WARN_ON(1);

	return 0;
}

u64 i915_gem_stolen_node_address(struct intel_stolen_node *node)
{
	struct xe_device *xe = node->xe;

	return xe_ttm_stolen_gpu_offset(xe) + i915_gem_stolen_node_offset(node);
}

u64 i915_gem_stolen_node_size(const struct intel_stolen_node *node)
{
	return node->bo->ttm.base.size;
}

struct intel_stolen_node *i915_gem_stolen_node_alloc(struct drm_device *drm)
{
	struct xe_device *xe = to_xe_device(drm);
	struct intel_stolen_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;

	node->xe = xe;

	return node;
}

void i915_gem_stolen_node_free(const struct intel_stolen_node *node)
{
	kfree(node);
}
