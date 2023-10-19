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
	if (act->police.notexceed.act_id != FLOW_ACTION_PIPE &&
	    act->police.notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(parse_state->extack,
				   "Offload not supported when conform action is not pipe or ok");
		return false;
	}
	if (mlx5e_policer_validate(parse_state->flow_action, act,
				   parse_state->extack))
		return false;

	return !!mlx5e_get_flow_meters(parse_state->flow->priv->mdev);
}

static int
fill_meter_params_from_act(const struct flow_action_entry *act,
			   struct mlx5e_flow_meter_params *params)
{
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

	return 0;
}

static int
tc_act_parse_police(struct mlx5e_tc_act_parse_state *parse_state,
		    const struct flow_action_entry *act,
		    struct mlx5e_priv *priv,
		    struct mlx5_flow_attr *attr)
{
	int err;

	err = fill_meter_params_from_act(act, &attr->meter_attr.params);
	if (err)
		return err;

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

static int
tc_act_police_offload(struct mlx5e_priv *priv,
		      struct flow_offload_action *fl_act,
		      struct flow_action_entry *act)
{
	struct mlx5e_flow_meter_params params = {};
	struct mlx5e_flow_meter_handle *meter;
	int err = 0;

	err = mlx5e_policer_validate(&fl_act->action, act, fl_act->extack);
	if (err)
		return err;

	err = fill_meter_params_from_act(act, &params);
	if (err)
		return err;

	meter = mlx5e_tc_meter_get(priv->mdev, &params);
	if (IS_ERR(meter) && PTR_ERR(meter) == -ENOENT) {
		meter = mlx5e_tc_meter_replace(priv->mdev, &params);
	} else if (!IS_ERR(meter)) {
		err = mlx5e_tc_meter_update(meter, &params);
		mlx5e_tc_meter_put(meter);
	}

	if (IS_ERR(meter)) {
		NL_SET_ERR_MSG_MOD(fl_act->extack, "Failed to get flow meter");
		mlx5_core_err(priv->mdev, "Failed to get flow meter %d\n", params.index);
		err = PTR_ERR(meter);
	}

	return err;
}

static int
tc_act_police_destroy(struct mlx5e_priv *priv,
		      struct flow_offload_action *fl_act)
{
	struct mlx5e_flow_meter_params params = {};
	struct mlx5e_flow_meter_handle *meter;

	params.index = fl_act->index;
	meter = mlx5e_tc_meter_get(priv->mdev, &params);
	if (IS_ERR(meter)) {
		NL_SET_ERR_MSG_MOD(fl_act->extack, "Failed to get flow meter");
		mlx5_core_err(priv->mdev, "Failed to get flow meter %d\n", params.index);
		return PTR_ERR(meter);
	}
	/* first put for the get and second for cleanup */
	mlx5e_tc_meter_put(meter);
	mlx5e_tc_meter_put(meter);
	return 0;
}

static int
tc_act_police_stats(struct mlx5e_priv *priv,
		    struct flow_offload_action *fl_act)
{
	struct mlx5e_flow_meter_params params = {};
	struct mlx5e_flow_meter_handle *meter;
	u64 bytes, packets, drops, lastuse;

	params.index = fl_act->index;
	meter = mlx5e_tc_meter_get(priv->mdev, &params);
	if (IS_ERR(meter)) {
		NL_SET_ERR_MSG_MOD(fl_act->extack, "Failed to get flow meter");
		mlx5_core_err(priv->mdev, "Failed to get flow meter %d\n", params.index);
		return PTR_ERR(meter);
	}

	mlx5e_tc_meter_get_stats(meter, &bytes, &packets, &drops, &lastuse);
	flow_stats_update(&fl_act->stats, bytes, packets, drops, lastuse,
			  FLOW_ACTION_HW_STATS_DELAYED);
	mlx5e_tc_meter_put(meter);
	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_police = {
	.can_offload = tc_act_can_offload_police,
	.parse_action = tc_act_parse_police,
	.is_multi_table_act = tc_act_is_multi_table_act_police,
	.offload_action = tc_act_police_offload,
	.destroy_action = tc_act_police_destroy,
	.stats_action = tc_act_police_stats,
};
