// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"
#include "en/tc_ct.h"

static bool
tc_act_can_offload_ct(struct mlx5e_tc_act_parse_state *parse_state,
		      const struct flow_action_entry *act,
		      int act_index)
{
	struct netlink_ext_ack *extack = parse_state->extack;

	if (flow_flag_test(parse_state->flow, SAMPLE)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Sample action with connection tracking is not supported");
		return false;
	}

	return true;
}

static int
tc_act_parse_ct(struct mlx5e_tc_act_parse_state *parse_state,
		const struct flow_action_entry *act,
		struct mlx5e_priv *priv,
		struct mlx5_flow_attr *attr)
{
	bool clear_action = act->ct.action & TCA_CT_ACT_CLEAR;
	int err;

	/* It's redundant to do ct clear more than once. */
	if (clear_action && parse_state->ct_clear)
		return 0;

	err = mlx5_tc_ct_parse_action(parse_state->ct_priv, attr,
				      &attr->parse_attr->mod_hdr_acts,
				      act, parse_state->extack);
	if (err)
		return err;

	flow_flag_set(parse_state->flow, CT);

	if (mlx5e_is_eswitch_flow(parse_state->flow))
		attr->esw_attr->split_count = attr->esw_attr->out_count;

	parse_state->ct_clear = clear_action;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_ct = {
	.can_offload = tc_act_can_offload_ct,
	.parse_action = tc_act_parse_ct,
};

