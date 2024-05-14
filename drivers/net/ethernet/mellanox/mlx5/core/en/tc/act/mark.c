// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en_tc.h"

static bool
tc_act_can_offload_mark(struct mlx5e_tc_act_parse_state *parse_state,
			const struct flow_action_entry *act,
			int act_index,
			struct mlx5_flow_attr *attr)
{
	if (act->mark & ~MLX5E_TC_FLOW_ID_MASK) {
		NL_SET_ERR_MSG_MOD(parse_state->extack, "Bad flow mark, only 16 bit supported");
		return false;
	}

	return true;
}

static int
tc_act_parse_mark(struct mlx5e_tc_act_parse_state *parse_state,
		  const struct flow_action_entry *act,
		  struct mlx5e_priv *priv,
		  struct mlx5_flow_attr *attr)
{
	attr->nic_attr->flow_tag = act->mark;
	attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_mark = {
	.can_offload = tc_act_can_offload_mark,
	.parse_action = tc_act_parse_mark,
};
