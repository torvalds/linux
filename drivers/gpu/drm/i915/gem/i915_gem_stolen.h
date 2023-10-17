/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_GEM_STOLEN_H__
#define __I915_GEM_STOLEN_H__

#include <linux/types.h>

struct drm_i915_private;
struct drm_mm_node;
struct drm_i915_gem_object;

#define i915_stolen_fb drm_mm_node

int i915_gem_stolen_insert_node(struct drm_i915_private *dev_priv,
				struct drm_mm_node *node, u64 size,
				unsigned alignment);
int i915_gem_stolen_insert_node_in_range(struct drm_i915_private *dev_priv,
					 struct drm_mm_node *node, u64 size,
					 unsigned alignment, u64 start,
					 u64 end);
void i915_gem_stolen_remove_node(struct drm_i915_private *dev_priv,
				 struct drm_mm_node *node);
struct intel_memory_region *
i915_gem_stolen_smem_setup(struct drm_i915_private *i915, u16 type,
			   u16 instance);
struct intel_memory_region *
i915_gem_stolen_lmem_setup(struct drm_i915_private *i915, u16 type,
			   u16 instance);

struct drm_i915_gem_object *
i915_gem_object_create_stolen(struct drm_i915_private *dev_priv,
			      resource_size_t size);

bool i915_gem_object_is_stolen(const struct drm_i915_gem_object *obj);

#define I915_GEM_STOLEN_BIAS SZ_128K

bool i915_gem_stolen_initialized(const struct drm_i915_private *i915);
u64 i915_gem_stolen_area_address(const struct drm_i915_private *i915);
u64 i915_gem_stolen_area_size(const struct drm_i915_private *i915);

u64 i915_gem_stolen_node_address(const struct drm_i915_private *i915,
				 const struct drm_mm_node *node);

bool i915_gem_stolen_node_allocated(const struct drm_mm_node *node);
u64 i915_gem_stolen_node_offset(const struct drm_mm_node *node);
u64 i915_gem_stolen_node_size(const struct drm_mm_node *node);

#endif /* __I915_GEM_STOLEN_H__ */
