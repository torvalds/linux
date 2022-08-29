/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_QUIRKS_H__
#define __INTEL_QUIRKS_H__

#include <linux/types.h>

struct drm_i915_private;

void intel_init_quirks(struct drm_i915_private *i915);
bool intel_has_quirk(struct drm_i915_private *i915, unsigned long quirk);

#endif /* __INTEL_QUIRKS_H__ */
