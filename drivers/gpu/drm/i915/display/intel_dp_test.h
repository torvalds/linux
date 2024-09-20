/* SPDX-License-Identifier: MIT */
/* Copyright © 2024 Intel Corporation */

#ifndef __INTEL_DP_TEST_H__
#define __INTEL_DP_TEST_H__

#include <linux/types.h>

struct intel_crtc_state;
struct intel_dp;
struct link_config_limits;

void intel_dp_test_request(struct intel_dp *intel_dp);
void intel_dp_test_compute_config(struct intel_dp *intel_dp,
				  struct intel_crtc_state *pipe_config,
				  struct link_config_limits *limits);
bool intel_dp_test_phy(struct intel_dp *intel_dp);

#endif /* __INTEL_DP_TEST_H__ */
