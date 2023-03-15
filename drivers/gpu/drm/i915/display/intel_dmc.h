/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DMC_H__
#define __INTEL_DMC_H__

#include <linux/types.h>

struct drm_i915_error_state_buf;
struct drm_i915_private;
enum pipe;

void intel_dmc_init(struct drm_i915_private *i915);
void intel_dmc_load_program(struct drm_i915_private *i915);
void intel_dmc_disable_program(struct drm_i915_private *i915);
void intel_dmc_enable_pipe(struct drm_i915_private *i915, enum pipe pipe);
void intel_dmc_disable_pipe(struct drm_i915_private *i915, enum pipe pipe);
void intel_dmc_fini(struct drm_i915_private *i915);
void intel_dmc_suspend(struct drm_i915_private *i915);
void intel_dmc_resume(struct drm_i915_private *i915);
bool intel_dmc_has_payload(struct drm_i915_private *i915);
void intel_dmc_debugfs_register(struct drm_i915_private *i915);
void intel_dmc_print_error_state(struct drm_i915_error_state_buf *m,
				 struct drm_i915_private *i915);

void assert_dmc_loaded(struct drm_i915_private *i915);

#endif /* __INTEL_DMC_H__ */
