/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _I915_GEM_STOLEN_H_
#define _I915_GEM_STOLEN_H_

#include "xe_ttm_stolen_mgr.h"
#include "xe_res_cursor.h"
#include "xe_validation.h"

struct xe_bo;

struct intel_stolen_node {
	struct xe_bo *bo;
};

static inline int i915_gem_stolen_insert_node_in_range(struct xe_device *xe,
						       struct intel_stolen_node *node,
						       u32 size, u32 align,
						       u32 start, u32 end)
{
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

static inline int i915_gem_stolen_insert_node(struct xe_device *xe,
					      struct intel_stolen_node *node,
					      u32 size, u32 align)
{
	/* Not used on xe */
	BUG_ON(1);
	return -ENODEV;
}

static inline void i915_gem_stolen_remove_node(struct xe_device *xe,
					       struct intel_stolen_node *node)
{
	xe_bo_unpin_map_no_vm(node->bo);
	node->bo = NULL;
}

static inline bool i915_gem_stolen_initialized(struct xe_device *xe)
{
	return ttm_manager_type(&xe->ttm, XE_PL_STOLEN);
}

static inline bool i915_gem_stolen_node_allocated(const struct intel_stolen_node *node)
{
	return node->bo;
}

static inline u32 i915_gem_stolen_node_offset(struct intel_stolen_node *node)
{
	struct xe_res_cursor res;

	xe_res_first(node->bo->ttm.resource, 0, 4096, &res);
	return res.start;
}

/* Used for < gen4. These are not supported by Xe */
static inline u64 i915_gem_stolen_area_address(const struct xe_device *xe)
{
	WARN_ON(1);

	return 0;
}

/* Used for gen9 specific WA. Gen9 is not supported by Xe */
static inline u64 i915_gem_stolen_area_size(const struct xe_device *xe)
{
	WARN_ON(1);

	return 0;
}

static inline u64 i915_gem_stolen_node_address(struct xe_device *xe,
					       struct intel_stolen_node *node)
{
	return xe_ttm_stolen_gpu_offset(xe) + i915_gem_stolen_node_offset(node);
}

static inline u64 i915_gem_stolen_node_size(const struct intel_stolen_node *node)
{
	return node->bo->ttm.base.size;
}

#endif
