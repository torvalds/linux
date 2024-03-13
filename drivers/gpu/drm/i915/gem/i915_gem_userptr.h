/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_GEM_USERPTR_H__
#define __I915_GEM_USERPTR_H__

struct drm_i915_private;

int i915_gem_init_userptr(struct drm_i915_private *dev_priv);
void i915_gem_cleanup_userptr(struct drm_i915_private *dev_priv);

#endif /* __I915_GEM_USERPTR_H__ */
