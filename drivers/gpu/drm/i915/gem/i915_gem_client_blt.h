/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */
#ifndef __I915_GEM_CLIENT_BLT_H__
#define __I915_GEM_CLIENT_BLT_H__

#include <linux/types.h>

struct drm_i915_gem_object;
struct i915_page_sizes;
struct intel_context;
struct sg_table;

int i915_gem_schedule_fill_pages_blt(struct drm_i915_gem_object *obj,
				     struct intel_context *ce,
				     struct sg_table *pages,
				     struct i915_page_sizes *page_sizes,
				     u32 value);

#endif
