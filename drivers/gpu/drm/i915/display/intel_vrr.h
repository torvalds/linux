/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_VRR_H__
#define __INTEL_VRR_H__

#include <linux/types.h>

struct drm_connector;
struct drm_connector_state;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_dp;
struct intel_encoder;

bool intel_vrr_is_capable(struct drm_connector *connector);
void intel_vrr_check_modeset(struct intel_atomic_state *state);
void intel_vrr_compute_config(struct intel_crtc_state *crtc_state,
			      struct drm_connector_state *conn_state);
void intel_vrr_enable(struct intel_encoder *encoder,
		      const struct intel_crtc_state *crtc_state);
void intel_vrr_send_push(const struct intel_crtc_state *crtc_state);
void intel_vrr_disable(const struct intel_crtc_state *old_crtc_state);
void intel_vrr_get_config(struct intel_crtc *crtc,
			  struct intel_crtc_state *crtc_state);
int intel_vrr_vmax_vblank_start(const struct intel_crtc_state *crtc_state);
int intel_vrr_vmin_vblank_start(const struct intel_crtc_state *crtc_state);

#endif /* __INTEL_VRR_H__ */
