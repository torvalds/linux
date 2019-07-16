/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_DEBUGFS_H__
#define __I915_DEBUGFS_H__

struct drm_i915_private;
struct drm_connector;

#ifdef CONFIG_DEBUG_FS
int i915_debugfs_register(struct drm_i915_private *dev_priv);
int i915_debugfs_connector_add(struct drm_connector *connector);
#else
static inline int i915_debugfs_register(struct drm_i915_private *dev_priv) { return 0; }
static inline int i915_debugfs_connector_add(struct drm_connector *connector) { return 0; }
#endif

#endif /* __I915_DEBUGFS_H__ */
