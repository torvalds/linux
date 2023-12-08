/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_DEBUGFS_H__
#define __INTEL_DISPLAY_DEBUGFS_H__

struct drm_crtc;
struct drm_i915_private;
struct intel_connector;

#ifdef CONFIG_DEBUG_FS
void intel_display_debugfs_register(struct drm_i915_private *i915);
void intel_connector_debugfs_add(struct intel_connector *connector);
void intel_crtc_debugfs_add(struct drm_crtc *crtc);
#else
static inline void intel_display_debugfs_register(struct drm_i915_private *i915) {}
static inline void intel_connector_debugfs_add(struct intel_connector *connector) {}
static inline void intel_crtc_debugfs_add(struct drm_crtc *crtc) {}
#endif

#endif /* __INTEL_DISPLAY_DEBUGFS_H__ */
