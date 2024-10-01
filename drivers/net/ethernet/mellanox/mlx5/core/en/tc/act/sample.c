// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <net/psample.h>
#include "act.h"
#include "en/tc_priv.h"
#include "en/tc/act/sample.h"

static bool
tc_act_can_offload_sample(struct mlx5e_tc_act_parse_state *parse_state,
			  const struct flow_action_entry *act,
			  int act_index,
			  struct mlx5_flow_attr *attr)
{
	struct netlink_ext_ack *extack = parse_state->extack;
	bool ct_nat;

	ct_nat = attr->ct_attr.ct_action & TCA_CT_ACT_NAT;

	if (flow_flag_test(parse_state->flow, CT) && ct_nat) {
		NL_SET_ERR_MSG_MOD(extack, "Sample action with CT NAT is not supported");
		return false;
	}

	return true;
}

static int
tc_act_parse_sample(struct mlx5e_tc_act_parse_state *parse_state,
		    const struct flow_action_entry *act,
		    struct mlx5e_priv *priv,
		    struct mlx5_flow_attr *attr)
{
	struct mlx5e_sample_attr *sample_attr = &attr->sample_attr;

	sample_attr->rate = act->sample.rate;
	sample_attr->group_num = act->sample.psample_group->group_num;

	if (act->sample.truncate)
		sample_attr->trunc_size = act->sample.trunc_size;

	attr->flags |= MLX5_ATTR_FLAG_SAMPLE;
	flow_flag_set(parse_state->flow, SAMPLE);

	return 0;
}

bool
mlx5e_tc_act_sample_is_multi_table(struct mlx5_core_dev *mdev,
				   struct mlx5_flow_attr *attr)
{
	if (MLX5_CAP_GEN(mdev, reg_c_preserve) ||
	    attr->action & MLX5_FLOW_CONTEXT_ACTION_DECAP)
		return true;

	return false;
}

static bool
tc_act_is_multi_table_act_sample(struct mlx5e_priv *priv,
				 const struct flow_action_entry *act,
				 struct mlx5_flow_attr *attr)
{
	return mlx5e_tc_act_sample_is_multi_table(priv->mdev, attr);
}

struct mlx5e_tc_act mlx5e_tc_act_sample = {
	.can_offload = tc_act_can_offload_sample,
	.parse_action = tc_act_parse_sample,
	.is_multi_table_act = tc_act_is_multi_table_act_sample,
};
