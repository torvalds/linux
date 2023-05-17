// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"

static int
tc_act_parse_accept(struct mlx5e_tc_act_parse_state *parse_state,
		    const struct flow_action_entry *act,
		    struct mlx5e_priv *priv,
		    struct mlx5_flow_attr *attr)
{
	attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	attr->flags |= MLX5_ATTR_FLAG_ACCEPT;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_accept = {
	.parse_action = tc_act_parse_accept,
	.is_terminating_action = true,
};
