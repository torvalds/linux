/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_HDCP_H__
#define __INTEL_HDCP_H__

#include <linux/types.h>

struct drm_connector;
struct drm_connector_state;
struct drm_i915_private;
struct intel_connector;
struct intel_crtc_state;
struct intel_encoder;
struct intel_hdcp_shim;
enum port;
enum transcoder;

void intel_hdcp_atomic_check(struct drm_connector *connector,
			     struct drm_connector_state *old_state,
			     struct drm_connector_state *new_state);
int intel_hdcp_init(struct intel_connector *connector,
		    const struct intel_hdcp_shim *hdcp_shim);
int intel_hdcp_enable(struct intel_connector *connector,
		      enum transcoder cpu_transcoder, u8 content_type);
int intel_hdcp_disable(struct intel_connector *connector);
void intel_hdcp_update_pipe(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state);
bool is_hdcp_supported(struct drm_i915_private *dev_priv, enum port port);
bool intel_hdcp_capable(struct intel_connector *connector);
bool intel_hdcp2_capable(struct intel_connector *connector);
void intel_hdcp_component_init(struct drm_i915_private *dev_priv);
void intel_hdcp_component_fini(struct drm_i915_private *dev_priv);
void intel_hdcp_cleanup(struct intel_connector *connector);
void intel_hdcp_handle_cp_irq(struct intel_connector *connector);

#endif /* __INTEL_HDCP_H__ */
