// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"
#include "eswitch.h"

static int
tc_act_parse_trap(struct mlx5e_tc_act_parse_state *parse_state,
		  const struct flow_action_entry *act,
		  struct mlx5e_priv *priv,
		  struct mlx5_flow_attr *attr)
{
	attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	attr->dest_ft = mlx5_eswitch_get_slow_fdb(priv->mdev->priv.eswitch);

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_trap = {
	.parse_action = tc_act_parse_trap,
};
