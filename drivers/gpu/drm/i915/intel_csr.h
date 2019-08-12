/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CSR_H__
#define __INTEL_CSR_H__

struct drm_i915_private;

#define CSR_VERSION(major, minor)	((major) << 16 | (minor))
#define CSR_VERSION_MAJOR(version)	((version) >> 16)
#define CSR_VERSION_MINOR(version)	((version) & 0xffff)

void intel_csr_ucode_init(struct drm_i915_private *i915);
void intel_csr_load_program(struct drm_i915_private *i915);
void intel_csr_ucode_fini(struct drm_i915_private *i915);
void intel_csr_ucode_suspend(struct drm_i915_private *i915);
void intel_csr_ucode_resume(struct drm_i915_private *i915);

#endif /* __INTEL_CSR_H__ */
