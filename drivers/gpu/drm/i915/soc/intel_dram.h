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
	enum intel_dram_type {
		INTEL_DRAM_UNKNOWN,
		INTEL_DRAM_DDR2,
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
	unsigned int fsb_freq;
	unsigned int mem_freq;
	u8 num_channels;
	u8 num_qgv_points;
	u8 num_psf_gv_points;
	bool symmetric_memory;
	bool has_16gb_dimms;
};

void intel_dram_edram_detect(struct drm_i915_private *i915);
int intel_dram_detect(struct drm_i915_private *i915);
unsigned int intel_fsb_freq(struct drm_i915_private *i915);
unsigned int intel_mem_freq(struct drm_i915_private *i915);
const struct dram_info *intel_dram_info(struct drm_device *drm);
const char *intel_dram_type_str(enum intel_dram_type type);

#endif /* __INTEL_DRAM_H__ */
