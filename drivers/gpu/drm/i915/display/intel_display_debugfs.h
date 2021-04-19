/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_DEBUGFS_H__
#define __INTEL_DISPLAY_DEBUGFS_H__

struct drm_connector;
struct drm_crtc;
struct drm_i915_private;

#ifdef CONFIG_DEBUG_FS
void intel_display_debugfs_register(struct drm_i915_private *i915);
int intel_connector_debugfs_add(struct drm_connector *connector);
int intel_crtc_debugfs_add(struct drm_crtc *crtc);
#else
static inline void intel_display_debugfs_register(struct drm_i915_private *i915) {}
static inline int intel_connector_debugfs_add(struct drm_connector *connector) { return 0; }
static inline int intel_crtc_debugfs_add(struct drm_crtc *crtc) { return 0; }
#endif

#endif /* __INTEL_DISPLAY_DEBUGFS_H__ */
