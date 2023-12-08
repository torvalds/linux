/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_GEM_EVICT_H__
#define __I915_GEM_EVICT_H__

#include <linux/types.h>

struct drm_mm_node;
struct i915_address_space;
struct i915_gem_ww_ctx;
struct drm_i915_gem_object;

int __must_check i915_gem_evict_something(struct i915_address_space *vm,
					  struct i915_gem_ww_ctx *ww,
					  u64 min_size, u64 alignment,
					  unsigned long color,
					  u64 start, u64 end,
					  unsigned flags);
int __must_check i915_gem_evict_for_node(struct i915_address_space *vm,
					 struct i915_gem_ww_ctx *ww,
					 struct drm_mm_node *node,
					 unsigned int flags);
int i915_gem_evict_vm(struct i915_address_space *vm,
		      struct i915_gem_ww_ctx *ww,
		      struct drm_i915_gem_object **busy_bo);

#endif /* __I915_GEM_EVICT_H__ */
