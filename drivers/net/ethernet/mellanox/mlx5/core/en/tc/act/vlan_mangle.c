// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/if_vlan.h>
#include "act.h"
#include "vlan.h"
#include "en/tc_priv.h"

struct pedit_headers_action;

int
mlx5e_tc_act_vlan_add_rewrite_action(struct mlx5e_priv *priv, int namespace,
				     const struct flow_action_entry *act,
				     struct mlx5e_tc_flow_parse_attr *parse_attr,
				     u32 *action, struct netlink_ext_ack *extack)
{
	u16 mask16 = VLAN_VID_MASK;
	u16 val16 = act->vlan.vid & VLAN_VID_MASK;
	const struct flow_action_entry pedit_act = {
		.id = FLOW_ACTION_MANGLE,
		.mangle.htype = FLOW_ACT_MANGLE_HDR_TYPE_ETH,
		.mangle.offset = offsetof(struct vlan_ethhdr, h_vlan_TCI),
		.mangle.mask = ~(u32)be16_to_cpu(*(__be16 *)&mask16),
		.mangle.val = (u32)be16_to_cpu(*(__be16 *)&val16),
	};
	u8 match_prio_mask, match_prio_val;
	void *headers_c, *headers_v;
	int err;

	headers_c = mlx5e_get_match_headers_criteria(*action, &parse_attr->spec);
	headers_v = mlx5e_get_match_headers_value(*action, &parse_attr->spec);

	if (!(MLX5_GET(fte_match_set_lyr_2_4, headers_c, cvlan_tag) &&
	      MLX5_GET(fte_match_set_lyr_2_4, headers_v, cvlan_tag))) {
		NL_SET_ERR_MSG_MOD(extack, "VLAN rewrite action must have VLAN protocol match");
		return -EOPNOTSUPP;
	}

	match_prio_mask = MLX5_GET(fte_match_set_lyr_2_4, headers_c, first_prio);
	match_prio_val = MLX5_GET(fte_match_set_lyr_2_4, headers_v, first_prio);
	if (act->vlan.prio != (match_prio_val & match_prio_mask)) {
		NL_SET_ERR_MSG_MOD(extack, "Changing VLAN prio is not supported");
		return -EOPNOTSUPP;
	}

	err = mlx5e_tc_act_pedit_parse_action(priv, &pedit_act, namespace, parse_attr,
					      NULL, extack);
	*action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	return err;
}

static bool
tc_act_can_offload_vlan_mangle(struct mlx5e_tc_act_parse_state *parse_state,
			       const struct flow_action_entry *act,
			       int act_index)
{
	return true;
}

static int
tc_act_parse_vlan_mangle(struct mlx5e_tc_act_parse_state *parse_state,
			 const struct flow_action_entry *act,
			 struct mlx5e_priv *priv,
			 struct mlx5_flow_attr *attr)
{
	enum mlx5_flow_namespace_type ns_type;
	int err;

	ns_type = mlx5e_get_flow_namespace(parse_state->flow);
	err = mlx5e_tc_act_vlan_add_rewrite_action(priv, ns_type, act, attr->parse_attr,
						   &attr->action, parse_state->extack);
	if (err)
		return err;

	if (ns_type == MLX5_FLOW_NAMESPACE_FDB)
		attr->esw_attr->split_count = attr->esw_attr->out_count;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_vlan_mangle = {
	.can_offload = tc_act_can_offload_vlan_mangle,
	.parse_action = tc_act_parse_vlan_mangle,
};
