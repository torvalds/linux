/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_CLOCK_GATING_H__
#define __INTEL_CLOCK_GATING_H__

struct drm_device;

static inline void intel_clock_gating_init(struct drm_device *drm) {}
static inline void intel_clock_gating_hooks_init(struct drm_device *drm) {}

#endif /* __INTEL_CLOCK_GATING_H__ */
