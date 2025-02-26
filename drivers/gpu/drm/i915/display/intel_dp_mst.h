/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DP_MST_H__
#define __INTEL_DP_MST_H__

#include <linux/types.h>

struct drm_connector_state;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_digital_port;
struct intel_dp;
struct intel_link_bw_limits;

int intel_dp_mst_encoder_init(struct intel_digital_port *dig_port, int conn_id);
void intel_dp_mst_encoder_cleanup(struct intel_digital_port *dig_port);
int intel_dp_mst_encoder_active_links(struct intel_digital_port *dig_port);
bool intel_dp_mst_is_master_trans(const struct intel_crtc_state *crtc_state);
bool intel_dp_mst_is_slave_trans(const struct intel_crtc_state *crtc_state);
bool intel_dp_mst_source_support(struct intel_dp *intel_dp);
int intel_dp_mst_add_topology_state_for_crtc(struct intel_atomic_state *state,
					     struct intel_crtc *crtc);
int intel_dp_mst_atomic_check_link(struct intel_atomic_state *state,
				   struct intel_link_bw_limits *limits);
bool intel_dp_mst_crtc_needs_modeset(struct intel_atomic_state *state,
				     struct intel_crtc *crtc);
void intel_dp_mst_prepare_probe(struct intel_dp *intel_dp);
bool intel_dp_mst_verify_dpcd_state(struct intel_dp *intel_dp);

int intel_dp_mtp_tu_compute_config(struct intel_dp *intel_dp,
				   struct intel_crtc_state *crtc_state,
				   int max_bpp, int min_bpp,
				   struct drm_connector_state *conn_state,
				   int step, bool dsc);

#endif /* __INTEL_DP_MST_H__ */
