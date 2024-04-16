/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_GEM_DOMAIN_H__
#define __I915_GEM_DOMAIN_H__

struct drm_i915_gem_object;
enum i915_cache_level;

int i915_gem_object_set_cache_level(struct drm_i915_gem_object *obj,
				    enum i915_cache_level cache_level);

#endif /* __I915_GEM_DOMAIN_H__ */
