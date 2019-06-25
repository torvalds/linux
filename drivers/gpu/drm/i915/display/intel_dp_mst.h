/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DP_MST_H__
#define __INTEL_DP_MST_H__

struct intel_digital_port;

int intel_dp_mst_encoder_init(struct intel_digital_port *intel_dig_port, int conn_id);
void intel_dp_mst_encoder_cleanup(struct intel_digital_port *intel_dig_port);

#endif /* __INTEL_DP_MST_H__ */
