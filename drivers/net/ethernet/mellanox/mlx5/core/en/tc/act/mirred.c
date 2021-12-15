// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/if_macvlan.h>
#include <linux/if_vlan.h>
#include <net/bareudp.h>
#include <net/bonding.h>
#include "act.h"
#include "vlan.h"
#include "en/tc_tun_encap.h"
#include "en/tc_priv.h"
#include "en_rep.h"

static bool
same_vf_reps(struct mlx5e_priv *priv, struct net_device *out_dev)
{
	return mlx5e_eswitch_vf_rep(priv->netdev) &&
	       priv->netdev == out_dev;
}

static int
verify_uplink_forwarding(struct mlx5e_priv *priv,
			 struct mlx5_flow_attr *attr,
			 struct net_device *out_dev,
			 struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *rep_priv;

	/* Forwarding non encapsulated traffic between
	 * uplink ports is allowed only if
	 * termination_table_raw_traffic cap is set.
	 *
	 * Input vport was stored attr->in_rep.
	 * In LAG case, *priv* is the private data of
	 * uplink which may be not the input vport.
	 */
	rep_priv = mlx5e_rep_to_rep_priv(attr->esw_attr->in_rep);

	if (!(mlx5e_eswitch_uplink_rep(rep_priv->netdev) &&
	      mlx5e_eswitch_uplink_rep(out_dev)))
		return 0;

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev,
					termination_table_raw_traffic)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "devices are both uplink, can't offload forwarding");
			pr_err("devices %s %s are both uplink, can't offload forwarding\n",
			       priv->netdev->name, out_dev->name);
			return -EOPNOTSUPP;
	} else if (out_dev != rep_priv->netdev) {
		NL_SET_ERR_MSG_MOD(extack,
				   "devices are not the same uplink, can't offload forwarding");
		pr_err("devices %s %s are both uplink but not the same, can't offload forwarding\n",
		       priv->netdev->name, out_dev->name);
		return -EOPNOTSUPP;
	}
	return 0;
}

static bool
is_duplicated_output_device(struct net_device *dev,
			    struct net_device *out_dev,
			    int *ifindexes, int if_count,
			    struct netlink_ext_ack *extack)
{
	int i;

	for (i = 0; i < if_count; i++) {
		if (ifindexes[i] == out_dev->ifindex) {
			NL_SET_ERR_MSG_MOD(extack, "can't duplicate output to same device");
			netdev_err(dev, "can't duplicate output to same device: %s\n",
				   out_dev->name);
			return true;
		}
	}

	return false;
}

static struct net_device *
get_fdb_out_dev(struct net_device *uplink_dev, struct net_device *out_dev)
{
	struct net_device *fdb_out_dev = out_dev;
	struct net_device *uplink_upper;

	rcu_read_lock();
	uplink_upper = netdev_master_upper_dev_get_rcu(uplink_dev);
	if (uplink_upper && netif_is_lag_master(uplink_upper) &&
	    uplink_upper == out_dev) {
		fdb_out_dev = uplink_dev;
	} else if (netif_is_lag_master(out_dev)) {
		fdb_out_dev = bond_option_active_slave_get_rcu(netdev_priv(out_dev));
		if (fdb_out_dev &&
		    (!mlx5e_eswitch_rep(fdb_out_dev) ||
		     !netdev_port_same_parent_id(fdb_out_dev, uplink_dev)))
			fdb_out_dev = NULL;
	}
	rcu_read_unlock();
	return fdb_out_dev;
}

static bool
tc_act_can_offload_mirred(struct mlx5e_tc_act_parse_state *parse_state,
			  const struct flow_action_entry *act,
			  int act_index)
{
	struct netlink_ext_ack *extack = parse_state->extack;
	struct mlx5e_tc_flow *flow = parse_state->flow;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct net_device *out_dev = act->dev;
	struct mlx5e_priv *priv = flow->priv;
	struct mlx5_esw_flow_attr *esw_attr;

	parse_attr = flow->attr->parse_attr;
	esw_attr = flow->attr->esw_attr;

	if (!out_dev) {
		/* out_dev is NULL when filters with
		 * non-existing mirred device are replayed to
		 * the driver.
		 */
		return false;
	}

	if (parse_state->mpls_push && !netif_is_bareudp(out_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "mpls is supported only through a bareudp device");
		return false;
	}

	if (mlx5e_is_ft_flow(flow) && out_dev == priv->netdev) {
		/* Ignore forward to self rules generated
		 * by adding both mlx5 devs to the flow table
		 * block on a normal nft offload setup.
		 */
		return false;
	}

	if (esw_attr->out_count >= MLX5_MAX_FLOW_FWD_VPORTS) {
		NL_SET_ERR_MSG_MOD(extack,
				   "can't support more output ports, can't offload forwarding");
		netdev_warn(priv->netdev,
			    "can't support more than %d output ports, can't offload forwarding\n",
			    esw_attr->out_count);
		return false;
	}

	if (parse_state->encap ||
	    netdev_port_same_parent_id(priv->netdev, out_dev) ||
	    netif_is_ovs_master(out_dev))
		return true;

	if (parse_attr->filter_dev != priv->netdev) {
		/* All mlx5 devices are called to configure
		 * high level device filters. Therefore, the
		 * *attempt* to  install a filter on invalid
		 * eswitch should not trigger an explicit error
		 */
		return false;
	}

	NL_SET_ERR_MSG_MOD(extack, "devices are not on same switch HW, can't offload forwarding");
	netdev_warn(priv->netdev,
		    "devices %s %s not on same switch HW, can't offload forwarding\n",
		    netdev_name(priv->netdev),
		    out_dev->name);

	return false;
}

static int
parse_mirred_encap(struct mlx5e_tc_act_parse_state *parse_state,
		   const struct flow_action_entry *act,
		   struct mlx5_flow_attr *attr)
{
	struct mlx5e_tc_flow_parse_attr *parse_attr = attr->parse_attr;
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct net_device *out_dev = act->dev;

	parse_attr->mirred_ifindex[esw_attr->out_count] = out_dev->ifindex;
	parse_attr->tun_info[esw_attr->out_count] =
		mlx5e_dup_tun_info(parse_state->tun_info);

	if (!parse_attr->tun_info[esw_attr->out_count])
		return -ENOMEM;

	parse_state->encap = false;
	esw_attr->dests[esw_attr->out_count].flags |= MLX5_ESW_DEST_ENCAP;
	esw_attr->out_count++;
	/* attr->dests[].rep is resolved when we handle encap */

	return 0;
}

static int
parse_mirred(struct mlx5e_tc_act_parse_state *parse_state,
	     const struct flow_action_entry *act,
	     struct mlx5e_priv *priv,
	     struct mlx5_flow_attr *attr)
{
	struct mlx5e_tc_flow_parse_attr *parse_attr = attr->parse_attr;
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct netlink_ext_ack *extack = parse_state->extack;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct net_device *out_dev = act->dev;
	struct net_device *uplink_dev;
	struct mlx5e_priv *out_priv;
	struct mlx5_eswitch *esw;
	int *ifindexes;
	int if_count;
	int err;

	esw = priv->mdev->priv.eswitch;
	uplink_dev = mlx5_eswitch_uplink_get_proto_dev(esw, REP_ETH);
	ifindexes = parse_state->ifindexes;
	if_count = parse_state->if_count;

	if (is_duplicated_output_device(priv->netdev, out_dev, ifindexes, if_count, extack))
		return -EOPNOTSUPP;

	parse_state->ifindexes[if_count] = out_dev->ifindex;
	parse_state->if_count++;

	out_dev = get_fdb_out_dev(uplink_dev, out_dev);
	if (!out_dev)
		return -ENODEV;

	if (is_vlan_dev(out_dev)) {
		err = mlx5e_tc_act_vlan_add_push_action(priv, attr, &out_dev, extack);
		if (err)
			return err;
	}

	if (is_vlan_dev(parse_attr->filter_dev)) {
		err = mlx5e_tc_act_vlan_add_pop_action(priv, attr, extack);
		if (err)
			return err;
	}

	if (netif_is_macvlan(out_dev))
		out_dev = macvlan_dev_real_dev(out_dev);

	err = verify_uplink_forwarding(priv, attr, out_dev, extack);
	if (err)
		return err;

	if (!mlx5e_is_valid_eswitch_fwd_dev(priv, out_dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "devices are not on same switch HW, can't offload forwarding");
		return -EOPNOTSUPP;
	}

	if (same_vf_reps(priv, out_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "can't forward from a VF to itself");
		return -EOPNOTSUPP;
	}

	out_priv = netdev_priv(out_dev);
	rpriv = out_priv->ppriv;
	esw_attr->dests[esw_attr->out_count].rep = rpriv->rep;
	esw_attr->dests[esw_attr->out_count].mdev = out_priv->mdev;
	esw_attr->out_count++;

	return 0;
}

static int
parse_mirred_ovs_master(struct mlx5e_tc_act_parse_state *parse_state,
			const struct flow_action_entry *act,
			struct mlx5e_priv *priv,
			struct mlx5_flow_attr *attr)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct net_device *out_dev = act->dev;
	int err;

	err = mlx5e_set_fwd_to_int_port_actions(priv, attr, out_dev->ifindex,
						MLX5E_TC_INT_PORT_EGRESS,
						&attr->action, esw_attr->out_count);
	if (err)
		return err;

	esw_attr->out_count++;
	return 0;
}

static int
tc_act_parse_mirred(struct mlx5e_tc_act_parse_state *parse_state,
		    const struct flow_action_entry *act,
		    struct mlx5e_priv *priv,
		    struct mlx5_flow_attr *attr)
{
	struct net_device *out_dev = act->dev;
	int err = -EOPNOTSUPP;

	if (parse_state->encap)
		err = parse_mirred_encap(parse_state, act, attr);
	else if (netdev_port_same_parent_id(priv->netdev, out_dev))
		err = parse_mirred(parse_state, act, priv, attr);
	else if (netif_is_ovs_master(out_dev))
		err = parse_mirred_ovs_master(parse_state, act, priv, attr);

	if (err)
		return err;

	attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			MLX5_FLOW_CONTEXT_ACTION_COUNT;

	return 0;
}

struct mlx5e_tc_act mlx5e_tc_act_mirred = {
	.can_offload = tc_act_can_offload_mirred,
	.parse_action = tc_act_parse_mirred,
};
