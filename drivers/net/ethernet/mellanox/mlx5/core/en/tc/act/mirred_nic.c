// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"

static bool
tc_act_can_offload_mirred_nic(struct mlx5e_tc_act_parse_state *parse_state,
			      const struct flow_action_entry *act,
			      int act_index,
			      struct mlx5_flow_attr *attr)
{
	struct netlink_ext_ack *extack = parse_state->extack;
	struct mlx5e_tc_flow *flow = parse_state->flow;
	struct net_device *out_dev = act->dev;
	struct mlx5e_priv *priv = flow->priv;

	if (act->id != FLOW_ACTION_REDIRECT)
		return false;

	if (priv->netdev->netdev_ops != out_dev->netdev_ops ||
	    !mlx5e_same_hw_devs(priv, netdev_priv(out_dev))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "devices are not on same switch HW, can't offload forwarding");
		netdev_warn(priv->netdev,
			    "devices %s %s not on same switch HW, can't offload forwarding\n",
			    netdev_name(priv->netdev),
			    out_dev->name);
		return false;
	}

	return true;
}

static int
tc_act_parse_mirred_nic(struct mlx5e_tc_act_parse_state *parse_state,
			const struct flow_action_entry *act,
			struct mlx5e_priv *priv,
			struct mlx5_flow_attr *attr)
{
	attr->parse_attr->mirred_ifindex[0] = act->dev->ifindex;
	flow_flag_set(parse_state->flow, HAIRPIN);
	attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_mirred_nic = {
	.can_offload = tc_act_can_offload_mirred_nic,
	.parse_action = tc_act_parse_mirred_nic,
};
