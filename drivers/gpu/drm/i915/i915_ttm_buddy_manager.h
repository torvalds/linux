/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_TTM_BUDDY_MANAGER_H__
#define __I915_TTM_BUDDY_MANAGER_H__

#include <linux/list.h>
#include <linux/types.h>

#include <drm/ttm/ttm_resource.h>

struct ttm_device;
struct ttm_resource_manager;
struct drm_buddy;

/**
 * struct i915_ttm_buddy_resource
 *
 * @base: struct ttm_resource base class we extend
 * @blocks: the list of struct i915_buddy_block for this resource/allocation
 * @flags: DRM_BUDDY_*_ALLOCATION flags
 * @used_visible_size: How much of this resource, if any, uses the CPU visible
 * portion, in pages.
 * @mm: the struct i915_buddy_mm for this resource
 *
 * Extends the struct ttm_resource to manage an address space allocation with
 * one or more struct i915_buddy_block.
 */
struct i915_ttm_buddy_resource {
	struct ttm_resource base;
	struct list_head blocks;
	unsigned long flags;
	unsigned long used_visible_size;
	struct drm_buddy *mm;
};

/**
 * to_ttm_buddy_resource
 *
 * @res: the resource to upcast
 *
 * Upcast the struct ttm_resource object into a struct i915_ttm_buddy_resource.
 */
static inline struct i915_ttm_buddy_resource *
to_ttm_buddy_resource(struct ttm_resource *res)
{
	return container_of(res, struct i915_ttm_buddy_resource, base);
}

int i915_ttm_buddy_man_init(struct ttm_device *bdev,
			    unsigned type, bool use_tt,
			    u64 size, u64 visible_size,
			    u64 default_page_size, u64 chunk_size);
int i915_ttm_buddy_man_fini(struct ttm_device *bdev,
			    unsigned int type);

int i915_ttm_buddy_man_reserve(struct ttm_resource_manager *man,
			       u64 start, u64 size);

u64 i915_ttm_buddy_man_visible_size(struct ttm_resource_manager *man);

void i915_ttm_buddy_man_avail(struct ttm_resource_manager *man,
			      u64 *avail, u64 *avail_visible);

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
void i915_ttm_buddy_man_force_visible_size(struct ttm_resource_manager *man,
					   u64 size);
#endif

#endif
