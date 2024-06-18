// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"
#include "fs_core.h"

static bool police_act_validate_control(enum flow_action_id act_id,
					struct netlink_ext_ack *extack)
{
	if (act_id != FLOW_ACTION_PIPE &&
	    act_id != FLOW_ACTION_ACCEPT &&
	    act_id != FLOW_ACTION_JUMP &&
	    act_id != FLOW_ACTION_DROP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform-exceed action is not pipe, ok, jump or drop");
		return false;
	}

	return true;
}

static int police_act_validate(const struct flow_action_entry *act,
			       struct netlink_ext_ack *extack)
{
	if (!police_act_validate_control(act->police.exceed.act_id, extack) ||
	    !police_act_validate_control(act->police.notexceed.act_id, extack))
		return -EOPNOTSUPP;

	if (act->police.peakrate_bytes_ps ||
	    act->police.avrate || act->police.overhead) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when peakrate/avrate/overhead is configured");
		return -EOPNOTSUPP;
	}

	return 0;
}

static bool
tc_act_can_offload_police(struct mlx5e_tc_act_parse_state *parse_state,
			  const struct flow_action_entry *act,
			  int act_index,
			  struct mlx5_flow_attr *attr)
{
	int err;

	err = police_act_validate(act, parse_state->extack);
	if (err)
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
	} else if (act->police.mtu) {
		params->mtu = act->police.mtu;
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
	enum mlx5_flow_namespace_type ns =  mlx5e_get_flow_namespace(parse_state->flow);
	struct mlx5e_flow_meter_params *params = &attr->meter_attr.params;
	int err;

	err = fill_meter_params_from_act(act, params);
	if (err)
		return err;

	if (params->mtu) {
		if (!(mlx5_fs_get_capabilities(priv->mdev, ns) &
		      MLX5_FLOW_STEERING_CAP_MATCH_RANGES))
			return -EOPNOTSUPP;

		attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
		attr->flags |= MLX5_ATTR_FLAG_MTU;
	} else {
		attr->action |= MLX5_FLOW_CONTEXT_ACTION_EXECUTE_ASO;
		attr->exe_aso_type = MLX5_EXE_ASO_FLOW_METER;
	}

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

	err = police_act_validate(act, fl_act->extack);
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
		return PTR_ERR(meter);
	}

	mlx5e_tc_meter_get_stats(meter, &bytes, &packets, &drops, &lastuse);
	flow_stats_update(&fl_act->stats, bytes, packets, drops, lastuse,
			  FLOW_ACTION_HW_STATS_DELAYED);
	mlx5e_tc_meter_put(meter);
	return 0;
}

static bool
tc_act_police_get_branch_ctrl(const struct flow_action_entry *act,
			      struct mlx5e_tc_act_branch_ctrl *cond_true,
			      struct mlx5e_tc_act_branch_ctrl *cond_false)
{
	cond_true->act_id = act->police.notexceed.act_id;
	cond_true->extval = act->police.notexceed.extval;

	cond_false->act_id = act->police.exceed.act_id;
	cond_false->extval = act->police.exceed.extval;
	return true;
}

struct mlx5e_tc_act mlx5e_tc_act_police = {
	.can_offload = tc_act_can_offload_police,
	.parse_action = tc_act_parse_police,
	.is_multi_table_act = tc_act_is_multi_table_act_police,
	.offload_action = tc_act_police_offload,
	.destroy_action = tc_act_police_destroy,
	.stats_action = tc_act_police_stats,
	.get_branch_ctrl = tc_act_police_get_branch_ctrl,
};
