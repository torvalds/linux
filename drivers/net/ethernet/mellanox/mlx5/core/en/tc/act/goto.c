// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"
#include "eswitch.h"

static int
validate_goto_chain(struct mlx5e_priv *priv,
		    struct mlx5e_tc_flow *flow,
		    const struct flow_action_entry *act,
		    struct netlink_ext_ack *extack)
{
	bool is_esw = mlx5e_is_eswitch_flow(flow);
	bool ft_flow = mlx5e_is_ft_flow(flow);
	u32 dest_chain = act->chain_index;
	struct mlx5_fs_chains *chains;
	struct mlx5_eswitch *esw;
	u32 reformat_and_fwd;
	u32 max_chain;

	esw = priv->mdev->priv.eswitch;
	chains = is_esw ? esw_chains(esw) : mlx5e_nic_chains(priv);
	max_chain = mlx5_chains_get_chain_range(chains);
	reformat_and_fwd = is_esw ?
			   MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev, reformat_and_fwd_to_table) :
			   MLX5_CAP_FLOWTABLE_NIC_RX(priv->mdev, reformat_and_fwd_to_table);

	if (ft_flow) {
		NL_SET_ERR_MSG_MOD(extack, "Goto action is not supported");
		return -EOPNOTSUPP;
	}

	if (!mlx5_chains_backwards_supported(chains) &&
	    dest_chain <= flow->attr->chain) {
		NL_SET_ERR_MSG_MOD(extack, "Goto lower numbered chain isn't supported");
		return -EOPNOTSUPP;
	}

	if (dest_chain > max_chain) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Requested destination chain is out of supported range");
		return -EOPNOTSUPP;
	}

	if (flow->attr->action & (MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT |
				  MLX5_FLOW_CONTEXT_ACTION_DECAP) &&
	    !reformat_and_fwd) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Goto chain is not allowed if action has reformat or decap");
		return -EOPNOTSUPP;
	}

	return 0;
}

static bool
tc_act_can_offload_goto(struct mlx5e_tc_act_parse_state *parse_state,
			const struct flow_action_entry *act,
			int act_index)
{
	struct netlink_ext_ack *extack = parse_state->extack;
	struct mlx5e_tc_flow *flow = parse_state->flow;

	if (validate_goto_chain(flow->priv, flow, act, extack))
		return false;

	return true;
}

static int
tc_act_parse_goto(struct mlx5e_tc_act_parse_state *parse_state,
		  const struct flow_action_entry *act,
		  struct mlx5e_priv *priv,
		  struct mlx5_flow_attr *attr)
{
	attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			MLX5_FLOW_CONTEXT_ACTION_COUNT;
	attr->dest_chain = act->chain_index;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_goto = {
	.can_offload = tc_act_can_offload_goto,
	.parse_action = tc_act_parse_goto,
};
