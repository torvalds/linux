/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_HDCP_H__
#define __INTEL_HDCP_H__

#include <linux/types.h>

#include <drm/i915_drm.h>

struct drm_connector;
struct drm_connector_state;
struct drm_i915_private;
struct intel_connector;
struct intel_hdcp_shim;

void intel_hdcp_atomic_check(struct drm_connector *connector,
			     struct drm_connector_state *old_state,
			     struct drm_connector_state *new_state);
int intel_hdcp_init(struct intel_connector *connector,
		    const struct intel_hdcp_shim *hdcp_shim);
int intel_hdcp_enable(struct intel_connector *connector);
int intel_hdcp_disable(struct intel_connector *connector);
bool is_hdcp_supported(struct drm_i915_private *dev_priv, enum port port);
bool intel_hdcp_capable(struct intel_connector *connector);
bool intel_hdcp2_capable(struct intel_connector *connector);
void intel_hdcp_component_init(struct drm_i915_private *dev_priv);
void intel_hdcp_component_fini(struct drm_i915_private *dev_priv);
void intel_hdcp_cleanup(struct intel_connector *connector);
void intel_hdcp_handle_cp_irq(struct intel_connector *connector);

#endif /* __INTEL_HDCP_H__ */
