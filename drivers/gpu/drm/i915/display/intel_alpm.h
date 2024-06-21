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

void intel_alpm_init_dpcd(struct intel_dp *intel_dp);
bool intel_alpm_compute_params(struct intel_dp *intel_dp,
			       struct intel_crtc_state *crtc_state);
void intel_alpm_lobf_compute_config(struct intel_dp *intel_dp,
				    struct intel_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state);
void intel_alpm_configure(struct intel_dp *intel_dp,
			  const struct intel_crtc_state *crtc_state);
void intel_alpm_lobf_debugfs_add(struct intel_connector *connector);
bool intel_alpm_aux_wake_supported(struct intel_dp *intel_dp);
bool intel_alpm_aux_less_wake_supported(struct intel_dp *intel_dp);
#endif
