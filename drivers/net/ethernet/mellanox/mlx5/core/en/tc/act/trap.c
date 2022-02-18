// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"

static bool
tc_act_can_offload_trap(struct mlx5e_tc_act_parse_state *parse_state,
			const struct flow_action_entry *act,
			int act_index)
{
	struct netlink_ext_ack *extack = parse_state->extack;

	if (parse_state->num_actions != 1) {
		NL_SET_ERR_MSG_MOD(extack, "action trap is supported as a sole action only");
		return false;
	}

	return true;
}

static int
tc_act_parse_trap(struct mlx5e_tc_act_parse_state *parse_state,
		  const struct flow_action_entry *act,
		  struct mlx5e_priv *priv,
		  struct mlx5_flow_attr *attr)
{
	attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			MLX5_FLOW_CONTEXT_ACTION_COUNT;
	attr->flags |= MLX5_ESW_ATTR_FLAG_SLOW_PATH;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_trap = {
	.can_offload = tc_act_can_offload_trap,
	.parse_action = tc_act_parse_trap,
};
