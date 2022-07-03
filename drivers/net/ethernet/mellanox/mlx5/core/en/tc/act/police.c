// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"

static bool
tc_act_can_offload_police(struct mlx5e_tc_act_parse_state *parse_state,
			  const struct flow_action_entry *act,
			  int act_index,
			  struct mlx5_flow_attr *attr)
{
	if (mlx5e_policer_validate(parse_state->flow_action, act,
				   parse_state->extack))
		return false;

	return !!mlx5e_get_flow_meters(parse_state->flow->priv->mdev);
}

static int
tc_act_parse_police(struct mlx5e_tc_act_parse_state *parse_state,
		    const struct flow_action_entry *act,
		    struct mlx5e_priv *priv,
		    struct mlx5_flow_attr *attr)
{
	struct mlx5e_flow_meter_params *params;

	params = &attr->meter_attr.params;
	params->index = act->hw_index;
	if (act->police.rate_bytes_ps) {
		params->mode = MLX5_RATE_LIMIT_BPS;
		/* change rate to bits per second */
		params->rate = act->police.rate_bytes_ps << 3;
		params->burst = act->police.burst;
	} else if (act->police.rate_pkt_ps) {
		params->mode = MLX5_RATE_LIMIT_PPS;
		params->rate = act->police.rate_pkt_ps;
		params->burst = act->police.burst_pkt;
	} else {
		return -EOPNOTSUPP;
	}

	attr->action |= MLX5_FLOW_CONTEXT_ACTION_EXECUTE_ASO;
	attr->exe_aso_type = MLX5_EXE_ASO_FLOW_METER;

	return 0;
}

static bool
tc_act_is_multi_table_act_police(struct mlx5e_priv *priv,
				 const struct flow_action_entry *act,
				 struct mlx5_flow_attr *attr)
{
	return true;
}

struct mlx5e_tc_act mlx5e_tc_act_police = {
	.can_offload = tc_act_can_offload_police,
	.parse_action = tc_act_parse_police,
	.is_multi_table_act = tc_act_is_multi_table_act_police,
};
