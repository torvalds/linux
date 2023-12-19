/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DVO_H__
#define __INTEL_DVO_H__

struct drm_i915_private;

#ifdef I915
void intel_dvo_init(struct drm_i915_private *dev_priv);
#else
static inline void intel_dvo_init(struct drm_i915_private *dev_priv)
{
}
#endif

#endif /* __INTEL_DVO_H__ */
