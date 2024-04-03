/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_GMCH_H__
#define __INTEL_GMCH_H__

#include <linux/types.h>

struct pci_dev;
struct drm_i915_private;

int intel_gmch_bridge_setup(struct drm_i915_private *i915);
void intel_gmch_bar_setup(struct drm_i915_private *i915);
void intel_gmch_bar_teardown(struct drm_i915_private *i915);
int intel_gmch_vga_set_state(struct drm_i915_private *i915, bool enable_decode);
unsigned int intel_gmch_vga_set_decode(struct pci_dev *pdev, bool enable_decode);

#endif /* __INTEL_GMCH_H__ */
