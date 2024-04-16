// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"

static bool
tc_act_can_offload_ptype(struct mlx5e_tc_act_parse_state *parse_state,
			 const struct flow_action_entry *act,
			 int act_index,
			 struct mlx5_flow_attr *attr)
{
	return true;
}

static int
tc_act_parse_ptype(struct mlx5e_tc_act_parse_state *parse_state,
		   const struct flow_action_entry *act,
		   struct mlx5e_priv *priv,
		   struct mlx5_flow_attr *attr)
{
	struct netlink_ext_ack *extack = parse_state->extack;

	if (act->ptype != PACKET_HOST) {
		NL_SET_ERR_MSG_MOD(extack, "skbedit ptype is only supported with type host");
		return -EOPNOTSUPP;
	}

	parse_state->ptype_host = true;
	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_ptype = {
	.can_offload = tc_act_can_offload_ptype,
	.parse_action = tc_act_parse_ptype,
};
