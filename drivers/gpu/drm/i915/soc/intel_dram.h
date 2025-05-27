/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_DRAM_H__
#define __INTEL_DRAM_H__

struct drm_i915_private;
struct drm_device;
struct dram_info;

void intel_dram_edram_detect(struct drm_i915_private *i915);
void intel_dram_detect(struct drm_i915_private *i915);
unsigned int i9xx_fsb_freq(struct drm_i915_private *i915);
const struct dram_info *intel_dram_info(struct drm_device *drm);

#endif /* __INTEL_DRAM_H__ */
