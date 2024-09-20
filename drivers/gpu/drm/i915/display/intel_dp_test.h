/* SPDX-License-Identifier: MIT */
/* Copyright © 2024 Intel Corporation */

#ifndef __INTEL_DP_TEST_H__
#define __INTEL_DP_TEST_H__

struct intel_crtc_state;
struct intel_dp;
struct intel_encoder;
struct link_config_limits;

void intel_dp_handle_test_request(struct intel_dp *intel_dp);
void intel_dp_adjust_compliance_config(struct intel_dp *intel_dp,
				       struct intel_crtc_state *pipe_config,
				       struct link_config_limits *limits);
void intel_dp_phy_test(struct intel_encoder *encoder);

#endif /* __INTEL_DP_TEST_H__ */
