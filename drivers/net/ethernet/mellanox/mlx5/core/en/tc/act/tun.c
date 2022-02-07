// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_tun_encap.h"
#include "en/tc_priv.h"

static bool
tc_act_can_offload_tun_encap(struct mlx5e_tc_act_parse_state *parse_state,
			     const struct flow_action_entry *act,
			     int act_index)
{
	if (!act->tunnel) {
		NL_SET_ERR_MSG_MOD(parse_state->extack,
				   "Zero tunnel attributes is not supported");
		return false;
	}

	return true;
}

static int
tc_act_parse_tun_encap(struct mlx5e_tc_act_parse_state *parse_state,
		       const struct flow_action_entry *act,
		       struct mlx5e_priv *priv,
		       struct mlx5_flow_attr *attr)
{
	parse_state->tun_info = act->tunnel;
	parse_state->encap = true;

	return 0;
}

static bool
tc_act_can_offload_tun_decap(struct mlx5e_tc_act_parse_state *parse_state,
			     const struct flow_action_entry *act,
			     int act_index)
{
	return true;
}

static int
tc_act_parse_tun_decap(struct mlx5e_tc_act_parse_state *parse_state,
		       const struct flow_action_entry *act,
		       struct mlx5e_priv *priv,
		       struct mlx5_flow_attr *attr)
{
	parse_state->decap = true;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_tun_encap = {
	.can_offload = tc_act_can_offload_tun_encap,
	.parse_action = tc_act_parse_tun_encap,
};

struct mlx5e_tc_act mlx5e_tc_act_tun_decap = {
	.can_offload = tc_act_can_offload_tun_decap,
	.parse_action = tc_act_parse_tun_decap,
};
