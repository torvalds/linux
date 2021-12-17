/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DP_MST_H__
#define __INTEL_DP_MST_H__

#include <linux/types.h>

struct intel_crtc_state;
struct intel_digital_port;
struct intel_dp;

int intel_dp_mst_encoder_init(struct intel_digital_port *dig_port, int conn_id);
void intel_dp_mst_encoder_cleanup(struct intel_digital_port *dig_port);
int intel_dp_mst_encoder_active_links(struct intel_digital_port *dig_port);
bool intel_dp_mst_is_master_trans(const struct intel_crtc_state *crtc_state);
bool intel_dp_mst_is_slave_trans(const struct intel_crtc_state *crtc_state);
bool intel_dp_mst_source_support(struct intel_dp *intel_dp);

#endif /* __INTEL_DP_MST_H__ */
