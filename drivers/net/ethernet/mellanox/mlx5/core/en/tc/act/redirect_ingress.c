// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"

static bool
tc_act_can_offload_redirect_ingress(struct mlx5e_tc_act_parse_state *parse_state,
				    const struct flow_action_entry *act,
				    int act_index,
				    struct mlx5_flow_attr *attr)
{
	struct netlink_ext_ack *extack = parse_state->extack;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct net_device *out_dev = act->dev;
	struct mlx5_esw_flow_attr *esw_attr;

	parse_attr = attr->parse_attr;
	esw_attr = attr->esw_attr;

	if (!out_dev)
		return false;

	if (!netif_is_ovs_master(out_dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "redirect to ingress is supported only for OVS internal ports");
		return false;
	}

	if (netif_is_ovs_master(parse_attr->filter_dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "redirect to ingress is not supported from internal port");
		return false;
	}

	if (!parse_state->ptype_host) {
		NL_SET_ERR_MSG_MOD(extack,
				   "redirect to int port ingress requires ptype=host action");
		return false;
	}

	if (esw_attr->out_count) {
		NL_SET_ERR_MSG_MOD(extack,
				   "redirect to int port ingress is supported only as single destination");
		return false;
	}

	return true;
}

static int
tc_act_parse_redirect_ingress(struct mlx5e_tc_act_parse_state *parse_state,
			      const struct flow_action_entry *act,
			      struct mlx5e_priv *priv,
			      struct mlx5_flow_attr *attr)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct net_device *out_dev = act->dev;
	int err;

	attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;

	err = mlx5e_set_fwd_to_int_port_actions(priv, attr, out_dev->ifindex,
						MLX5E_TC_INT_PORT_INGRESS,
						&attr->action, esw_attr->out_count);
	if (err)
		return err;

	parse_state->if_count = 0;
	esw_attr->out_count++;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_redirect_ingress = {
	.can_offload = tc_act_can_offload_redirect_ingress,
	.parse_action = tc_act_parse_redirect_ingress,
};

