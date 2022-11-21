/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DMC_H__
#define __INTEL_DMC_H__

#include "i915_reg_defs.h"
#include "intel_wakeref.h"
#include <linux/workqueue.h>

struct drm_i915_private;

#define DMC_VERSION(major, minor)	((major) << 16 | (minor))
#define DMC_VERSION_MAJOR(version)	((version) >> 16)
#define DMC_VERSION_MINOR(version)	((version) & 0xffff)

enum {
	DMC_FW_MAIN = 0,
	DMC_FW_PIPEA,
	DMC_FW_PIPEB,
	DMC_FW_PIPEC,
	DMC_FW_PIPED,
	DMC_FW_MAX
};

struct intel_dmc {
	struct work_struct work;
	const char *fw_path;
	u32 required_version;
	u32 max_fw_size; /* bytes */
	u32 version;
	struct dmc_fw_info {
		u32 mmio_count;
		i915_reg_t mmioaddr[20];
		u32 mmiodata[20];
		u32 dmc_offset;
		u32 start_mmioaddr;
		u32 dmc_fw_size; /*dwords */
		u32 *payload;
		bool present;
	} dmc_info[DMC_FW_MAX];

	u32 dc_state;
	u32 target_dc_state;
	u32 allowed_dc_mask;
	intel_wakeref_t wakeref;
};

void intel_dmc_ucode_init(struct drm_i915_private *i915);
void intel_dmc_load_program(struct drm_i915_private *i915);
void intel_dmc_ucode_fini(struct drm_i915_private *i915);
void intel_dmc_ucode_suspend(struct drm_i915_private *i915);
void intel_dmc_ucode_resume(struct drm_i915_private *i915);
bool intel_dmc_has_payload(struct drm_i915_private *i915);

#endif /* __INTEL_DMC_H__ */
