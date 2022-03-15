// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <net/bareudp.h>
#include "act.h"
#include "en/tc_priv.h"

static bool
tc_act_can_offload_mpls_push(struct mlx5e_tc_act_parse_state *parse_state,
			     const struct flow_action_entry *act,
			     int act_index)
{
	struct netlink_ext_ack *extack = parse_state->extack;
	struct mlx5e_priv *priv = parse_state->flow->priv;

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev, reformat_l2_to_l3_tunnel) ||
	    act->mpls_push.proto != htons(ETH_P_MPLS_UC)) {
		NL_SET_ERR_MSG_MOD(extack, "mpls push is supported only for mpls_uc protocol");
		return false;
	}

	return true;
}

static void
copy_mpls_info(struct mlx5e_mpls_info *mpls_info,
	       const struct flow_action_entry *act)
{
	mpls_info->label = act->mpls_push.label;
	mpls_info->tc = act->mpls_push.tc;
	mpls_info->bos = act->mpls_push.bos;
	mpls_info->ttl = act->mpls_push.ttl;
}

static int
tc_act_parse_mpls_push(struct mlx5e_tc_act_parse_state *parse_state,
		       const struct flow_action_entry *act,
		       struct mlx5e_priv *priv,
		       struct mlx5_flow_attr *attr)
{
	parse_state->mpls_push = true;
	copy_mpls_info(&parse_state->mpls_info, act);

	return 0;
}

static bool
tc_act_can_offload_mpls_pop(struct mlx5e_tc_act_parse_state *parse_state,
			    const struct flow_action_entry *act,
			    int act_index)
{
	struct netlink_ext_ack *extack = parse_state->extack;
	struct mlx5e_tc_flow *flow = parse_state->flow;
	struct net_device *filter_dev;

	filter_dev = flow->attr->parse_attr->filter_dev;

	/* we only support mpls pop if it is the first action
	 * and the filter net device is bareudp. Subsequent
	 * actions can be pedit and the last can be mirred
	 * egress redirect.
	 */
	if (act_index) {
		NL_SET_ERR_MSG_MOD(extack, "mpls pop supported only as first action");
		return false;
	}

	if (!netif_is_bareudp(filter_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "mpls pop supported only on bareudp devices");
		return false;
	}

	return true;
}

static int
tc_act_parse_mpls_pop(struct mlx5e_tc_act_parse_state *parse_state,
		      const struct flow_action_entry *act,
		      struct mlx5e_priv *priv,
		      struct mlx5_flow_attr *attr)
{
	attr->parse_attr->eth.h_proto = act->mpls_pop.proto;
	attr->action |= MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
	flow_flag_set(parse_state->flow, L3_TO_L2_DECAP);

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_mpls_push = {
	.can_offload = tc_act_can_offload_mpls_push,
	.parse_action = tc_act_parse_mpls_push,
};

struct mlx5e_tc_act mlx5e_tc_act_mpls_pop = {
	.can_offload = tc_act_can_offload_mpls_pop,
	.parse_action = tc_act_parse_mpls_pop,
};
