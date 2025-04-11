/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_DEBUGFS_H__
#define __INTEL_DISPLAY_DEBUGFS_H__

struct intel_connector;
struct intel_crtc;
struct intel_display;

#ifdef CONFIG_DEBUG_FS
void intel_display_debugfs_register(struct intel_display *display);
void intel_connector_debugfs_add(struct intel_connector *connector);
void intel_crtc_debugfs_add(struct intel_crtc *crtc);
#else
static inline void intel_display_debugfs_register(struct intel_display *display) {}
static inline void intel_connector_debugfs_add(struct intel_connector *connector) {}
static inline void intel_crtc_debugfs_add(struct intel_crtc *crtc) {}
#endif

#endif /* __INTEL_DISPLAY_DEBUGFS_H__ */
