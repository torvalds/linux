/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_TC_ACT_H__
#define __MLX5_EN_TC_ACT_H__

#include <net/tc_act/tc_pedit.h>
#include <net/flow_offload.h>
#include <linux/netlink.h>
#include "eswitch.h"
#include "pedit.h"

struct mlx5_flow_attr;

struct mlx5e_tc_act_parse_state {
	unsigned int num_actions;
	struct mlx5e_tc_flow *flow;
	struct netlink_ext_ack *extack;
	bool encap;
	bool decap;
	bool mpls_push;
	const struct ip_tunnel_info *tun_info;
	struct pedit_headers_action hdrs[__PEDIT_CMD_MAX];
};

struct mlx5e_tc_act {
	bool (*can_offload)(struct mlx5e_tc_act_parse_state *parse_state,
			    const struct flow_action_entry *act,
			    int act_index);

	int (*parse_action)(struct mlx5e_tc_act_parse_state *parse_state,
			    const struct flow_action_entry *act,
			    struct mlx5e_priv *priv,
			    struct mlx5_flow_attr *attr);
};

extern struct mlx5e_tc_act mlx5e_tc_act_drop;
extern struct mlx5e_tc_act mlx5e_tc_act_trap;
extern struct mlx5e_tc_act mlx5e_tc_act_accept;
extern struct mlx5e_tc_act mlx5e_tc_act_mark;
extern struct mlx5e_tc_act mlx5e_tc_act_goto;
extern struct mlx5e_tc_act mlx5e_tc_act_tun_encap;
extern struct mlx5e_tc_act mlx5e_tc_act_tun_decap;
extern struct mlx5e_tc_act mlx5e_tc_act_csum;
extern struct mlx5e_tc_act mlx5e_tc_act_pedit;
extern struct mlx5e_tc_act mlx5e_tc_act_vlan;
extern struct mlx5e_tc_act mlx5e_tc_act_vlan_mangle;
extern struct mlx5e_tc_act mlx5e_tc_act_mpls_push;
extern struct mlx5e_tc_act mlx5e_tc_act_mpls_pop;

struct mlx5e_tc_act *
mlx5e_tc_act_get(enum flow_action_id act_id,
		 enum mlx5_flow_namespace_type ns_type);

void
mlx5e_tc_act_init_parse_state(struct mlx5e_tc_act_parse_state *parse_state,
			      struct mlx5e_tc_flow *flow,
			      struct flow_action *flow_action,
			      struct netlink_ext_ack *extack);

#endif /* __MLX5_EN_TC_ACT_H__ */
