/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 Intel Corporation */

#ifndef __INTEL_DP_LINK_CAPS_H__
#define __INTEL_DP_LINK_CAPS_H__

#include <linux/types.h>

struct intel_connector;
struct intel_dp;
struct intel_dp_link_caps;
struct intel_dp_link_config;

int intel_dp_common_len_rate_limit(struct intel_dp_link_caps *link_caps,
				   int max_rate);
int intel_dp_common_rate(struct intel_dp_link_caps *link_caps, int index);
int intel_dp_link_caps_common_rate_idx(struct intel_dp_link_caps *link_caps, int rate);
int intel_dp_max_common_rate(struct intel_dp_link_caps *link_caps);
int intel_dp_link_caps_num_common_rates(struct intel_dp_link_caps *link_caps);
int intel_dp_link_caps_max_common_lane_count(struct intel_dp_link_caps *link_caps);

void intel_dp_link_caps_print_common_rates(struct intel_dp_link_caps *link_caps);

void intel_dp_link_caps_get_forced_params(struct intel_dp_link_caps *link_caps,
					  struct intel_dp_link_config *forced_params);

int intel_dp_link_config_index(struct intel_dp_link_caps *link_caps,
			       int link_rate, int lane_count);
void intel_dp_link_config_get(struct intel_dp_link_caps *link_caps,
			      int idx, int *link_rate, int *lane_count);

void intel_dp_link_caps_get_max_limits(struct intel_dp_link_caps *link_caps,
				       struct intel_dp_link_config *max_link_limits);
bool intel_dp_link_caps_set_max_limits(struct intel_dp_link_caps *link_caps,
				       const struct intel_dp_link_config *max_link_limits);
void intel_dp_link_caps_reset_max_limits(struct intel_dp_link_caps *link_caps);

bool intel_dp_link_caps_update(struct intel_dp_link_caps *link_caps,
			       const int *rates, int num_rates, int max_lane_count);
void intel_dp_link_caps_reset(struct intel_dp_link_caps *link_caps);

void intel_dp_link_caps_debugfs_add(struct intel_connector *connector);

struct intel_dp_link_caps *intel_dp_link_caps_init(struct intel_dp *intel_dp);
void intel_dp_link_caps_cleanup(struct intel_dp_link_caps *link_caps);

#endif /* __INTEL_DP_LINK_CAPS_H__ */
