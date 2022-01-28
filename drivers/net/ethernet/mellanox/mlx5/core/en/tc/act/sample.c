// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <net/psample.h>
#include "act.h"
#include "en/tc_priv.h"

static bool
tc_act_can_offload_sample(struct mlx5e_tc_act_parse_state *parse_state,
			  const struct flow_action_entry *act,
			  int act_index,
			  struct mlx5_flow_attr *attr)
{
	struct netlink_ext_ack *extack = parse_state->extack;

	if (flow_flag_test(parse_state->flow, CT)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Sample action with connection tracking is not supported");
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

struct mlx5e_tc_act mlx5e_tc_act_sample = {
	.can_offload = tc_act_can_offload_sample,
	.parse_action = tc_act_parse_sample,
};
