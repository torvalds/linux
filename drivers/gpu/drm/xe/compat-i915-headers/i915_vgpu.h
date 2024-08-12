/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _I915_VGPU_H_
#define _I915_VGPU_H_

#include <linux/types.h>

struct drm_i915_private;

static inline bool intel_vgpu_active(struct drm_i915_private *i915)
{
	return false;
}

#endif /* _I915_VGPU_H_ */
