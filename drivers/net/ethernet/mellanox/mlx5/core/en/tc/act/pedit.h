/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_TC_ACT_PEDIT_H__
#define __MLX5_EN_TC_ACT_PEDIT_H__

#include "en_tc.h"

struct pedit_headers {
	struct ethhdr   eth;
	struct vlan_hdr vlan;
	struct iphdr    ip4;
	struct ipv6hdr  ip6;
	struct tcphdr   tcp;
	struct udphdr   udp;
};

struct pedit_headers_action {
	struct pedit_headers vals;
	struct pedit_headers masks;
	u32 pedits;
};

int
mlx5e_tc_act_pedit_parse_action(struct mlx5e_priv *priv,
				const struct flow_action_entry *act, int namespace,
				struct mlx5e_tc_flow_parse_attr *parse_attr,
				struct pedit_headers_action *hdrs,
				struct mlx5e_tc_flow *flow,
				struct netlink_ext_ack *extack);

#endif /* __MLX5_EN_TC_ACT_PEDIT_H__ */
