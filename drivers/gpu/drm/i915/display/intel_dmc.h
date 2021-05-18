/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DMC_H__
#define __INTEL_DMC_H__

struct drm_i915_private;

#define DMC_VERSION(major, minor)	((major) << 16 | (minor))
#define DMC_VERSION_MAJOR(version)	((version) >> 16)
#define DMC_VERSION_MINOR(version)	((version) & 0xffff)

void intel_dmc_ucode_init(struct drm_i915_private *i915);
void intel_dmc_load_program(struct drm_i915_private *i915);
void intel_dmc_ucode_fini(struct drm_i915_private *i915);
void intel_dmc_ucode_suspend(struct drm_i915_private *i915);
void intel_dmc_ucode_resume(struct drm_i915_private *i915);

#endif /* __INTEL_DMC_H__ */
