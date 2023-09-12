/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_TV_H__
#define __INTEL_TV_H__

struct drm_i915_private;

#ifdef I915
void intel_tv_init(struct drm_i915_private *dev_priv);
#else
static inline void intel_tv_init(struct drm_i915_private *dev_priv)
{
}
#endif

#endif /* __INTEL_TV_H__ */
