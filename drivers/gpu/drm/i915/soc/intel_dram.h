/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_DRAM_H__
#define __INTEL_DRAM_H__

#include <linux/types.h>

struct drm_i915_private;
struct drm_device;

struct dram_info {
	bool wm_lv_0_adjust_needed;
	u8 num_channels;
	bool symmetric_memory;
	enum intel_dram_type {
		INTEL_DRAM_UNKNOWN,
		INTEL_DRAM_DDR3,
		INTEL_DRAM_DDR4,
		INTEL_DRAM_LPDDR3,
		INTEL_DRAM_LPDDR4,
		INTEL_DRAM_DDR5,
		INTEL_DRAM_LPDDR5,
		INTEL_DRAM_GDDR,
		INTEL_DRAM_GDDR_ECC,
		__INTEL_DRAM_TYPE_MAX,
	} type;
	u8 num_qgv_points;
	u8 num_psf_gv_points;
};

void intel_dram_edram_detect(struct drm_i915_private *i915);
int intel_dram_detect(struct drm_i915_private *i915);
unsigned int i9xx_fsb_freq(struct drm_i915_private *i915);
const struct dram_info *intel_dram_info(struct drm_device *drm);

#endif /* __INTEL_DRAM_H__ */
