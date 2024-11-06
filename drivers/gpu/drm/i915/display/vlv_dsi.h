/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __VLV_DSI_H__
#define __VLV_DSI_H__

enum port;
struct drm_i915_private;
struct intel_dsi;

#ifdef I915
void vlv_dsi_wait_for_fifo_empty(struct intel_dsi *intel_dsi, enum port port);
void vlv_dsi_init(struct drm_i915_private *dev_priv);
#else
static inline void vlv_dsi_wait_for_fifo_empty(struct intel_dsi *intel_dsi, enum port port)
{
}
static inline void vlv_dsi_init(struct drm_i915_private *dev_priv)
{
}
#endif

#endif /* __VLV_DSI_H__ */
