/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022-2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_DRIVER_H__
#define __INTEL_DISPLAY_DRIVER_H__

#include <linux/types.h>

struct drm_atomic_state;
struct drm_i915_private;
struct drm_modeset_acquire_ctx;
struct pci_dev;

bool intel_display_driver_probe_defer(struct pci_dev *pdev);
void intel_display_driver_init_hw(struct drm_i915_private *i915);
void intel_display_driver_early_probe(struct drm_i915_private *i915);
int intel_display_driver_probe_noirq(struct drm_i915_private *i915);
int intel_display_driver_probe_nogem(struct drm_i915_private *i915);
int intel_display_driver_probe(struct drm_i915_private *i915);
void intel_display_driver_register(struct drm_i915_private *i915);
void intel_display_driver_remove(struct drm_i915_private *i915);
void intel_display_driver_remove_noirq(struct drm_i915_private *i915);
void intel_display_driver_remove_nogem(struct drm_i915_private *i915);
void intel_display_driver_unregister(struct drm_i915_private *i915);
int intel_display_driver_suspend(struct drm_i915_private *i915);
void intel_display_driver_resume(struct drm_i915_private *i915);

/* interface for intel_display_reset.c */
int __intel_display_driver_resume(struct drm_i915_private *i915,
				  struct drm_atomic_state *state,
				  struct drm_modeset_acquire_ctx *ctx);

void intel_display_driver_enable_user_access(struct drm_i915_private *i915);
void intel_display_driver_disable_user_access(struct drm_i915_private *i915);
void intel_display_driver_suspend_access(struct drm_i915_private *i915);
void intel_display_driver_resume_access(struct drm_i915_private *i915);
bool intel_display_driver_check_access(struct drm_i915_private *i915);

#endif /* __INTEL_DISPLAY_DRIVER_H__ */

