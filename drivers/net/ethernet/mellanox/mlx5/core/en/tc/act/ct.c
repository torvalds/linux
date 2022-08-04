// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"
#include "en/tc_ct.h"

static bool
tc_act_can_offload_ct(struct mlx5e_tc_act_parse_state *parse_state,
		      const struct flow_action_entry *act,
		      int act_index,
		      struct mlx5_flow_attr *attr)
{
	bool clear_action = act->ct.action & TCA_CT_ACT_CLEAR;
	struct netlink_ext_ack *extack = parse_state->extack;

	if (parse_state->ct && !clear_action) {
		NL_SET_ERR_MSG_MOD(extack, "Multiple CT actions are not supported");
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


	if (mlx5e_is_eswitch_flow(parse_state->flow))
		attr->esw_attr->split_count = attr->esw_attr->out_count;

	if (clear_action) {
		parse_state->ct_clear = true;
	} else {
		attr->flags |= MLX5_ATTR_FLAG_CT;
		flow_flag_set(parse_state->flow, CT);
		parse_state->ct = true;
	}

	return 0;
}

static int
tc_act_post_parse_ct(struct mlx5e_tc_act_parse_state *parse_state,
		     struct mlx5e_priv *priv,
		     struct mlx5_flow_attr *attr)
{
	struct mlx5e_tc_mod_hdr_acts *mod_acts = &attr->parse_attr->mod_hdr_acts;
	int err;

	/* If ct action exist, we can ignore previous ct_clear actions */
	if (parse_state->ct)
		return 0;

	if (parse_state->ct_clear) {
		err = mlx5_tc_ct_set_ct_clear_regs(parse_state->ct_priv, mod_acts);
		if (err) {
			NL_SET_ERR_MSG_MOD(parse_state->extack,
					   "Failed to set registers for ct clear");
			return err;
		}
		attr->action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

		/* Prevent handling of additional, redundant clear actions */
		parse_state->ct_clear = false;
	}

	return 0;
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

struct mlx5e_tc_act mlx5e_tc_act_ct = {
	.can_offload = tc_act_can_offload_ct,
	.parse_action = tc_act_parse_ct,
	.is_multi_table_act = tc_act_is_multi_table_act_ct,
	.post_parse = tc_act_post_parse_ct,
};

