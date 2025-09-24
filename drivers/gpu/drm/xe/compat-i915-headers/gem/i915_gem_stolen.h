/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _I915_GEM_STOLEN_H_
#define _I915_GEM_STOLEN_H_

#include <linux/types.h>

struct drm_device;
struct intel_stolen_node;
struct xe_device;

int i915_gem_stolen_insert_node_in_range(struct intel_stolen_node *node,
					 u32 size, u32 align,
					 u32 start, u32 end);

int i915_gem_stolen_insert_node(struct intel_stolen_node *node, u32 size, u32 align);

void i915_gem_stolen_remove_node(struct intel_stolen_node *node);

bool i915_gem_stolen_initialized(struct xe_device *xe);

bool i915_gem_stolen_node_allocated(const struct intel_stolen_node *node);

u32 i915_gem_stolen_node_offset(struct intel_stolen_node *node);

u64 i915_gem_stolen_area_address(const struct xe_device *xe);

u64 i915_gem_stolen_area_size(const struct xe_device *xe);

u64 i915_gem_stolen_node_address(struct intel_stolen_node *node);

u64 i915_gem_stolen_node_size(const struct intel_stolen_node *node);

struct intel_stolen_node *i915_gem_stolen_node_alloc(struct drm_device *drm);

void i915_gem_stolen_node_free(const struct intel_stolen_node *node);

#endif
