/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017 Intel Corporation
 */

#ifndef __I915_GEMFS_H__
#define __I915_GEMFS_H__

struct drm_i915_private;

void i915_gemfs_init(struct drm_i915_private *i915);
void i915_gemfs_fini(struct drm_i915_private *i915);

#endif
