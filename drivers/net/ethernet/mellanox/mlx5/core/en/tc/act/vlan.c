// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/if_vlan.h>
#include "act.h"
#include "vlan.h"
#include "en/tc_priv.h"

static int
add_vlan_prio_tag_rewrite_action(struct mlx5e_priv *priv,
				 struct mlx5e_tc_flow_parse_attr *parse_attr,
				 u32 *action, struct netlink_ext_ack *extack)
{
	const struct flow_action_entry prio_tag_act = {
		.vlan.vid = 0,
		.vlan.prio =
			MLX5_GET(fte_match_set_lyr_2_4,
				 mlx5e_get_match_headers_value(*action,
							       &parse_attr->spec),
				 first_prio) &
			MLX5_GET(fte_match_set_lyr_2_4,
				 mlx5e_get_match_headers_criteria(*action,
								  &parse_attr->spec),
				 first_prio),
	};

	return mlx5e_tc_act_vlan_add_rewrite_action(priv, MLX5_FLOW_NAMESPACE_FDB,
						    &prio_tag_act, parse_attr, action,
						    extack);
}

static int
parse_tc_vlan_action(struct mlx5e_priv *priv,
		     const struct flow_action_entry *act,
		     struct mlx5_esw_flow_attr *attr,
		     u32 *action,
		     struct netlink_ext_ack *extack,
		     struct mlx5e_tc_act_parse_state *parse_state)
{
	u8 vlan_idx = attr->total_vlan;

	if (vlan_idx >= MLX5_FS_VLAN_DEPTH) {
		NL_SET_ERR_MSG_MOD(extack, "Total vlans used is greater than supported");
		return -EOPNOTSUPP;
	}

	if (!mlx5_eswitch_vlan_actions_supported(priv->mdev, vlan_idx)) {
		NL_SET_ERR_MSG_MOD(extack, "firmware vlan actions is not supported");
		return -EOPNOTSUPP;
	}

	switch (act->id) {
	case FLOW_ACTION_VLAN_POP:
		if (vlan_idx)
			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_POP_2;
		else
			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_POP;
		break;
	case FLOW_ACTION_VLAN_PUSH:
		attr->vlan_vid[vlan_idx] = act->vlan.vid;
		attr->vlan_prio[vlan_idx] = act->vlan.prio;
		attr->vlan_proto[vlan_idx] = act->vlan.proto;
		if (!attr->vlan_proto[vlan_idx])
			attr->vlan_proto[vlan_idx] = htons(ETH_P_8021Q);

		if (vlan_idx)
			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2;
		else
			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH;
		break;
	case FLOW_ACTION_VLAN_POP_ETH:
		parse_state->eth_pop = true;
		break;
	case FLOW_ACTION_VLAN_PUSH_ETH:
		if (!flow_flag_test(parse_state->flow, L3_TO_L2_DECAP))
			return -EOPNOTSUPP;
		parse_state->eth_push = true;
		memcpy(attr->eth.h_dest, act->vlan_push_eth.dst, ETH_ALEN);
		memcpy(attr->eth.h_source, act->vlan_push_eth.src, ETH_ALEN);
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unexpected action id for VLAN");
		return -EINVAL;
	}

	attr->total_vlan = vlan_idx + 1;

	return 0;
}

int
mlx5e_tc_act_vlan_add_push_action(struct mlx5e_priv *priv,
				  struct mlx5_flow_attr *attr,
				  struct net_device **out_dev,
				  struct netlink_ext_ack *extack)
{
	struct net_device *vlan_dev = *out_dev;
	struct flow_action_entry vlan_act = {
		.id = FLOW_ACTION_VLAN_PUSH,
		.vlan.vid = vlan_dev_vlan_id(vlan_dev),
		.vlan.proto = vlan_dev_vlan_proto(vlan_dev),
		.vlan.prio = 0,
	};
	int err;

	err = parse_tc_vlan_action(priv, &vlan_act, attr->esw_attr, &attr->action, extack, NULL);
	if (err)
		return err;

	rcu_read_lock();
	*out_dev = dev_get_by_index_rcu(dev_net(vlan_dev), dev_get_iflink(vlan_dev));
	rcu_read_unlock();
	if (!*out_dev)
		return -ENODEV;

	if (is_vlan_dev(*out_dev))
		err = mlx5e_tc_act_vlan_add_push_action(priv, attr, out_dev, extack);

	return err;
}

int
mlx5e_tc_act_vlan_add_pop_action(struct mlx5e_priv *priv,
				 struct mlx5_flow_attr *attr,
				 struct netlink_ext_ack *extack)
{
	struct flow_action_entry vlan_act = {
		.id = FLOW_ACTION_VLAN_POP,
	};
	int nest_level, err = 0;

	nest_level = attr->parse_attr->filter_dev->lower_level -
						priv->netdev->lower_level;
	while (nest_level--) {
		err = parse_tc_vlan_action(priv, &vlan_act, attr->esw_attr, &attr->action,
					   extack, NULL);
		if (err)
			return err;
	}

	return err;
}

static bool
tc_act_can_offload_vlan(struct mlx5e_tc_act_parse_state *parse_state,
			const struct flow_action_entry *act,
			int act_index,
			struct mlx5_flow_attr *attr)
{
	return true;
}

static int
tc_act_parse_vlan(struct mlx5e_tc_act_parse_state *parse_state,
		  const struct flow_action_entry *act,
		  struct mlx5e_priv *priv,
		  struct mlx5_flow_attr *attr)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	int err;

	if (act->id == FLOW_ACTION_VLAN_PUSH &&
	    (attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP)) {
		/* Replace vlan pop+push with vlan modify */
		attr->action &= ~MLX5_FLOW_CONTEXT_ACTION_VLAN_POP;
		err = mlx5e_tc_act_vlan_add_rewrite_action(priv, MLX5_FLOW_NAMESPACE_FDB, act,
							   attr->parse_attr, &attr->action,
							   parse_state->extack);
	} else {
		err = parse_tc_vlan_action(priv, act, esw_attr, &attr->action,
					   parse_state->extack, parse_state);
	}

	if (err)
		return err;

	esw_attr->split_count = esw_attr->out_count;

	return 0;
}

static int
tc_act_post_parse_vlan(struct mlx5e_tc_act_parse_state *parse_state,
		       struct mlx5e_priv *priv,
		       struct mlx5_flow_attr *attr)
{
	struct mlx5e_tc_flow_parse_attr *parse_attr = attr->parse_attr;
	struct netlink_ext_ack *extack = parse_state->extack;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	int err;

	if (MLX5_CAP_GEN(esw->dev, prio_tag_required) &&
	    attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP) {
		/* For prio tag mode, replace vlan pop with rewrite vlan prio
		 * tag rewrite.
		 */
		attr->action &= ~MLX5_FLOW_CONTEXT_ACTION_VLAN_POP;
		err = add_vlan_prio_tag_rewrite_action(priv, parse_attr,
						       &attr->action, extack);
		if (err)
			return err;
	}

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_vlan = {
	.can_offload = tc_act_can_offload_vlan,
	.parse_action = tc_act_parse_vlan,
	.post_parse = tc_act_post_parse_vlan,
};
