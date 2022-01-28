// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"

static bool
tc_act_can_offload_drop(struct mlx5e_tc_act_parse_state *parse_state,
			const struct flow_action_entry *act,
			int act_index,
			struct mlx5_flow_attr *attr)
{
	return true;
}

static int
tc_act_parse_drop(struct mlx5e_tc_act_parse_state *parse_state,
		  const struct flow_action_entry *act,
		  struct mlx5e_priv *priv,
		  struct mlx5_flow_attr *attr)
{
	attr->action |= MLX5_FLOW_CONTEXT_ACTION_DROP |
			MLX5_FLOW_CONTEXT_ACTION_COUNT;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_drop = {
	.can_offload = tc_act_can_offload_drop,
	.parse_action = tc_act_parse_drop,
};
