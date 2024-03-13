/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_DP_HDCP___
#define __INTEL_DP_HDCP___

struct intel_connector;
struct intel_digital_port;

int intel_dp_hdcp_init(struct intel_digital_port *dig_port,
		       struct intel_connector *intel_connector);

#endif /* __INTEL_DP_HDCP___ */
