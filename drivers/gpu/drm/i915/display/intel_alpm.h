/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2024 Intel Corporation
 */

#ifndef _INTEL_ALPM_H
#define _INTEL_ALPM_H

#include <linux/types.h>

struct intel_dp;
struct intel_crtc_state;
struct drm_connector_state;
struct intel_connector;
struct intel_atomic_state;
struct intel_crtc;

void intel_alpm_init(struct intel_dp *intel_dp);
bool intel_alpm_compute_params(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state);
void intel_alpm_lobf_compute_config(struct intel_dp *intel_dp,
				    struct intel_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state);
void intel_alpm_configure(struct intel_dp *intel_dp,
			  const struct intel_crtc_state *crtc_state);
void intel_alpm_enable_sink(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state);
void intel_alpm_pre_plane_update(struct intel_atomic_state *state,
				 struct intel_crtc *crtc);
void intel_alpm_port_configure(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state);
void intel_alpm_post_plane_update(struct intel_atomic_state *state,
				  struct intel_crtc *crtc);
void intel_alpm_lobf_debugfs_add(struct intel_connector *connector);
bool intel_alpm_aux_wake_supported(struct intel_dp *intel_dp);
bool intel_alpm_aux_less_wake_supported(struct intel_dp *intel_dp);
bool intel_alpm_is_alpm_aux_less(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state);
void intel_alpm_disable(struct intel_dp *intel_dp);
bool intel_alpm_get_error(struct intel_dp *intel_dp);
#endif
