// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"
#include "en/tc_ct.h"

static int
tc_act_parse_ct(struct mlx5e_tc_act_parse_state *parse_state,
		const struct flow_action_entry *act,
		struct mlx5e_priv *priv,
		struct mlx5_flow_attr *attr)
{
	int err;

	err = mlx5_tc_ct_parse_action(parse_state->ct_priv, attr, act, parse_state->extack);
	if (err)
		return err;

	if (mlx5e_is_eswitch_flow(parse_state->flow)) {
		attr->esw_attr->split_count = attr->esw_attr->out_count;
		parse_state->if_count = 0;
	}

	attr->flags |= MLX5_ATTR_FLAG_CT;

	return 0;
}

static int
tc_act_post_parse_ct(struct mlx5e_tc_act_parse_state *parse_state,
		     struct mlx5e_priv *priv,
		     struct mlx5_flow_attr *attr)
{
	if (!(attr->flags & MLX5_ATTR_FLAG_CT))
		return 0;

	return mlx5_tc_ct_flow_offload(parse_state->ct_priv, attr);
}

static bool
tc_act_is_multi_table_act_ct(struct mlx5e_priv *priv,
			     const struct flow_action_entry *act,
			     struct mlx5_flow_attr *attr)
{
	if (act->ct.action & TCA_CT_ACT_CLEAR)
		return false;

	return true;
}

static bool
tc_act_is_missable_ct(const struct flow_action_entry *act)
{
	return !(act->ct.action & TCA_CT_ACT_CLEAR);
}

struct mlx5e_tc_act mlx5e_tc_act_ct = {
	.parse_action = tc_act_parse_ct,
	.post_parse = tc_act_post_parse_ct,
	.is_multi_table_act = tc_act_is_multi_table_act_ct,
	.is_missable = tc_act_is_missable_ct,
};

