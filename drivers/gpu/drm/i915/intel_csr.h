/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CSR_H__
#define __INTEL_CSR_H__

struct drm_i915_private;

void intel_csr_ucode_init(struct drm_i915_private *i915);
void intel_csr_load_program(struct drm_i915_private *i915);
void intel_csr_ucode_fini(struct drm_i915_private *i915);
void intel_csr_ucode_suspend(struct drm_i915_private *i915);
void intel_csr_ucode_resume(struct drm_i915_private *i915);

#endif /* __INTEL_CSR_H__ */
