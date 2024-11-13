/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef __I9XX_DISPLAY_SR_H__
#define __I9XX_DISPLAY_SR_H__

struct drm_i915_private;

void i9xx_display_sr_save(struct drm_i915_private *i915);
void i9xx_display_sr_restore(struct drm_i915_private *i915);

#endif
