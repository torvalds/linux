/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/list.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/mpfs.h>
#include "en.h"
#include "en_rep.h"
#include "lib/mpfs.h"
#include "en/ptp.h"

static int mlx5e_add_l2_flow_rule(struct mlx5e_priv *priv,
				  struct mlx5e_l2_rule *ai, int type);
static void mlx5e_del_l2_flow_rule(struct mlx5e_priv *priv,
				   struct mlx5e_l2_rule *ai);

enum {
	MLX5E_FULLMATCH = 0,
	MLX5E_ALLMULTI  = 1,
};

enum {
	MLX5E_UC        = 0,
	MLX5E_MC_IPV4   = 1,
	MLX5E_MC_IPV6   = 2,
	MLX5E_MC_OTHER  = 3,
};

enum {
	MLX5E_ACTION_NONE = 0,
	MLX5E_ACTION_ADD  = 1,
	MLX5E_ACTION_DEL  = 2,
};

struct mlx5e_l2_hash_node {
	struct hlist_node          hlist;
	u8                         action;
	struct mlx5e_l2_rule ai;
	bool   mpfs;
};

static inline int mlx5e_hash_l2(u8 *addr)
{
	return addr[5];
}

static void mlx5e_add_l2_to_hash(struct hlist_head *hash, u8 *addr)
{
	struct mlx5e_l2_hash_node *hn;
	int ix = mlx5e_hash_l2(addr);
	int found = 0;

	hlist_for_each_entry(hn, &hash[ix], hlist)
		if (ether_addr_equal_64bits(hn->ai.addr, addr)) {
			found = 1;
			break;
		}

	if (found) {
		hn->action = MLX5E_ACTION_NONE;
		return;
	}

	hn = kzalloc(sizeof(*hn), GFP_ATOMIC);
	if (!hn)
		return;

	ether_addr_copy(hn->ai.addr, addr);
	hn->action = MLX5E_ACTION_ADD;

	hlist_add_head(&hn->hlist, &hash[ix]);
}

static void mlx5e_del_l2_from_hash(struct mlx5e_l2_hash_node *hn)
{
	hlist_del(&hn->hlist);
	kfree(hn);
}

struct mlx5e_vlan_table {
	struct mlx5e_flow_table		ft;
	DECLARE_BITMAP(active_cvlans, VLAN_N_VID);
	DECLARE_BITMAP(active_svlans, VLAN_N_VID);
	struct mlx5_flow_handle	*active_cvlans_rule[VLAN_N_VID];
	struct mlx5_flow_handle	*active_svlans_rule[VLAN_N_VID];
	struct mlx5_flow_handle	*untagged_rule;
	struct mlx5_flow_handle	*any_cvlan_rule;
	struct mlx5_flow_handle	*any_svlan_rule;
	struct mlx5_flow_handle	*trap_rule;
	bool			cvlan_filter_disabled;
};

unsigned long *mlx5e_vlan_get_active_svlans(struct mlx5e_vlan_table *vlan)
{
	return vlan->active_svlans;
}

struct mlx5_flow_table *mlx5e_vlan_get_flowtable(struct mlx5e_vlan_table *vlan)
{
	return vlan->ft.t;
}

static int mlx5e_vport_context_update_vlans(struct mlx5e_priv *priv)
{
	struct net_device *ndev = priv->netdev;
	int max_list_size;
	int list_size;
	u16 *vlans;
	int vlan;
	int err;
	int i;

	list_size = 0;
	for_each_set_bit(vlan, priv->fs.vlan->active_cvlans, VLAN_N_VID)
		list_size++;

	max_list_size = 1 << MLX5_CAP_GEN(priv->mdev, log_max_vlan_list);

	if (list_size > max_list_size) {
		netdev_warn(ndev,
			    "netdev vlans list size (%d) > (%d) max vport list size, some vlans will be dropped\n",
			    list_size, max_list_size);
		list_size = max_list_size;
	}

	vlans = kcalloc(list_size, sizeof(*vlans), GFP_KERNEL);
	if (!vlans)
		return -ENOMEM;

	i = 0;
	for_each_set_bit(vlan, priv->fs.vlan->active_cvlans, VLAN_N_VID) {
		if (i >= list_size)
			break;
		vlans[i++] = vlan;
	}

	err = mlx5_modify_nic_vport_vlans(priv->mdev, vlans, list_size);
	if (err)
		netdev_err(ndev, "Failed to modify vport vlans list err(%d)\n",
			   err);

	kfree(vlans);
	return err;
}

enum mlx5e_vlan_rule_type {
	MLX5E_VLAN_RULE_TYPE_UNTAGGED,
	MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID,
	MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID,
	MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID,
	MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID,
};

static int __mlx5e_add_vlan_rule(struct mlx5e_priv *priv,
				 enum mlx5e_vlan_rule_type rule_type,
				 u16 vid, struct mlx5_flow_spec *spec)
{
	struct mlx5_flow_table *ft = priv->fs.vlan->ft.t;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_handle **rule_p;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	int err = 0;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = priv->fs.l2.ft.t;

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	switch (rule_type) {
	case MLX5E_VLAN_RULE_TYPE_UNTAGGED:
		/* cvlan_tag enabled in match criteria and
		 * disabled in match value means both S & C tags
		 * don't exist (untagged of both)
		 */
		rule_p = &priv->fs.vlan->untagged_rule;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.cvlan_tag);
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID:
		rule_p = &priv->fs.vlan->any_cvlan_rule;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.cvlan_tag);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.cvlan_tag, 1);
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID:
		rule_p = &priv->fs.vlan->any_svlan_rule;
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.svlan_tag);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.svlan_tag, 1);
		break;
	case MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID:
		rule_p = &priv->fs.vlan->active_svlans_rule[vid];
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.svlan_tag);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.svlan_tag, 1);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.first_vid);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.first_vid,
			 vid);
		break;
	default: /* MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID */
		rule_p = &priv->fs.vlan->active_cvlans_rule[vid];
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.cvlan_tag);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.cvlan_tag, 1);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.first_vid);
		MLX5_SET(fte_match_param, spec->match_value, outer_headers.first_vid,
			 vid);
		break;
	}

	if (WARN_ONCE(*rule_p, "VLAN rule already exists type %d", rule_type))
		return 0;

	*rule_p = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);

	if (IS_ERR(*rule_p)) {
		err = PTR_ERR(*rule_p);
		*rule_p = NULL;
		netdev_err(priv->netdev, "%s: add rule failed\n", __func__);
	}

	return err;
}

static int mlx5e_add_vlan_rule(struct mlx5e_priv *priv,
			       enum mlx5e_vlan_rule_type rule_type, u16 vid)
{
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	if (rule_type == MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID)
		mlx5e_vport_context_update_vlans(priv);

	err = __mlx5e_add_vlan_rule(priv, rule_type, vid, spec);

	kvfree(spec);

	return err;
}

static void mlx5e_del_vlan_rule(struct mlx5e_priv *priv,
				enum mlx5e_vlan_rule_type rule_type, u16 vid)
{
	switch (rule_type) {
	case MLX5E_VLAN_RULE_TYPE_UNTAGGED:
		if (priv->fs.vlan->untagged_rule) {
			mlx5_del_flow_rules(priv->fs.vlan->untagged_rule);
			priv->fs.vlan->untagged_rule = NULL;
		}
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID:
		if (priv->fs.vlan->any_cvlan_rule) {
			mlx5_del_flow_rules(priv->fs.vlan->any_cvlan_rule);
			priv->fs.vlan->any_cvlan_rule = NULL;
		}
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID:
		if (priv->fs.vlan->any_svlan_rule) {
			mlx5_del_flow_rules(priv->fs.vlan->any_svlan_rule);
			priv->fs.vlan->any_svlan_rule = NULL;
		}
		break;
	case MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID:
		if (priv->fs.vlan->active_svlans_rule[vid]) {
			mlx5_del_flow_rules(priv->fs.vlan->active_svlans_rule[vid]);
			priv->fs.vlan->active_svlans_rule[vid] = NULL;
		}
		break;
	case MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID:
		if (priv->fs.vlan->active_cvlans_rule[vid]) {
			mlx5_del_flow_rules(priv->fs.vlan->active_cvlans_rule[vid]);
			priv->fs.vlan->active_cvlans_rule[vid] = NULL;
		}
		mlx5e_vport_context_update_vlans(priv);
		break;
	}
}

static void mlx5e_del_any_vid_rules(struct mlx5e_priv *priv)
{
	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID, 0);
	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID, 0);
}

static int mlx5e_add_any_vid_rules(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID, 0);
	if (err)
		return err;

	return mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_STAG_VID, 0);
}

static struct mlx5_flow_handle *
mlx5e_add_trap_rule(struct mlx5_flow_table *ft, int trap_id, int tir_num)
{
	struct mlx5_flow_destination dest = {};
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);
	spec->flow_context.flags |= FLOW_CONTEXT_HAS_TAG;
	spec->flow_context.flow_tag = trap_id;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dest.tir_num = tir_num;

	rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	kvfree(spec);
	return rule;
}

int mlx5e_add_vlan_trap(struct mlx5e_priv *priv, int trap_id, int tir_num)
{
	struct mlx5_flow_table *ft = priv->fs.vlan->ft.t;
	struct mlx5_flow_handle *rule;
	int err;

	rule = mlx5e_add_trap_rule(ft, trap_id, tir_num);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		priv->fs.vlan->trap_rule = NULL;
		netdev_err(priv->netdev, "%s: add VLAN trap rule failed, err %d\n",
			   __func__, err);
		return err;
	}
	priv->fs.vlan->trap_rule = rule;
	return 0;
}

void mlx5e_remove_vlan_trap(struct mlx5e_priv *priv)
{
	if (priv->fs.vlan->trap_rule) {
		mlx5_del_flow_rules(priv->fs.vlan->trap_rule);
		priv->fs.vlan->trap_rule = NULL;
	}
}

int mlx5e_add_mac_trap(struct mlx5e_priv *priv, int trap_id, int tir_num)
{
	struct mlx5_flow_table *ft = priv->fs.l2.ft.t;
	struct mlx5_flow_handle *rule;
	int err;

	rule = mlx5e_add_trap_rule(ft, trap_id, tir_num);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		priv->fs.l2.trap_rule = NULL;
		netdev_err(priv->netdev, "%s: add MAC trap rule failed, err %d\n",
			   __func__, err);
		return err;
	}
	priv->fs.l2.trap_rule = rule;
	return 0;
}

void mlx5e_remove_mac_trap(struct mlx5e_priv *priv)
{
	if (priv->fs.l2.trap_rule) {
		mlx5_del_flow_rules(priv->fs.l2.trap_rule);
		priv->fs.l2.trap_rule = NULL;
	}
}

void mlx5e_enable_cvlan_filter(struct mlx5e_priv *priv)
{
	if (!priv->fs.vlan->cvlan_filter_disabled)
		return;

	priv->fs.vlan->cvlan_filter_disabled = false;
	if (priv->netdev->flags & IFF_PROMISC)
		return;
	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID, 0);
}

void mlx5e_disable_cvlan_filter(struct mlx5e_priv *priv)
{
	if (priv->fs.vlan->cvlan_filter_disabled)
		return;

	priv->fs.vlan->cvlan_filter_disabled = true;
	if (priv->netdev->flags & IFF_PROMISC)
		return;
	mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_CTAG_VID, 0);
}

static int mlx5e_vlan_rx_add_cvid(struct mlx5e_priv *priv, u16 vid)
{
	int err;

	set_bit(vid, priv->fs.vlan->active_cvlans);

	err = mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID, vid);
	if (err)
		clear_bit(vid, priv->fs.vlan->active_cvlans);

	return err;
}

static int mlx5e_vlan_rx_add_svid(struct mlx5e_priv *priv, u16 vid)
{
	struct net_device *netdev = priv->netdev;
	int err;

	set_bit(vid, priv->fs.vlan->active_svlans);

	err = mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID, vid);
	if (err) {
		clear_bit(vid, priv->fs.vlan->active_svlans);
		return err;
	}

	/* Need to fix some features.. */
	netdev_update_features(netdev);
	return err;
}

int mlx5e_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	if (mlx5e_is_uplink_rep(priv))
		return 0; /* no vlan table for uplink rep */

	if (be16_to_cpu(proto) == ETH_P_8021Q)
		return mlx5e_vlan_rx_add_cvid(priv, vid);
	else if (be16_to_cpu(proto) == ETH_P_8021AD)
		return mlx5e_vlan_rx_add_svid(priv, vid);

	return -EOPNOTSUPP;
}

int mlx5e_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	if (mlx5e_is_uplink_rep(priv))
		return 0; /* no vlan table for uplink rep */

	if (be16_to_cpu(proto) == ETH_P_8021Q) {
		clear_bit(vid, priv->fs.vlan->active_cvlans);
		mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID, vid);
	} else if (be16_to_cpu(proto) == ETH_P_8021AD) {
		clear_bit(vid, priv->fs.vlan->active_svlans);
		mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID, vid);
		netdev_update_features(dev);
	}

	return 0;
}

static void mlx5e_add_vlan_rules(struct mlx5e_priv *priv)
{
	int i;

	mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_UNTAGGED, 0);

	for_each_set_bit(i, priv->fs.vlan->active_cvlans, VLAN_N_VID) {
		mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID, i);
	}

	for_each_set_bit(i, priv->fs.vlan->active_svlans, VLAN_N_VID)
		mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID, i);

	if (priv->fs.vlan->cvlan_filter_disabled)
		mlx5e_add_any_vid_rules(priv);
}

static void mlx5e_del_vlan_rules(struct mlx5e_priv *priv)
{
	int i;

	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_UNTAGGED, 0);

	for_each_set_bit(i, priv->fs.vlan->active_cvlans, VLAN_N_VID) {
		mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_CTAG_VID, i);
	}

	for_each_set_bit(i, priv->fs.vlan->active_svlans, VLAN_N_VID)
		mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_STAG_VID, i);

	WARN_ON_ONCE(!(test_bit(MLX5E_STATE_DESTROYING, &priv->state)));

	mlx5e_remove_vlan_trap(priv);

	/* must be called after DESTROY bit is set and
	 * set_rx_mode is called and flushed
	 */
	if (priv->fs.vlan->cvlan_filter_disabled)
		mlx5e_del_any_vid_rules(priv);
}

#define mlx5e_for_each_hash_node(hn, tmp, hash, i) \
	for (i = 0; i < MLX5E_L2_ADDR_HASH_SIZE; i++) \
		hlist_for_each_entry_safe(hn, tmp, &hash[i], hlist)

static void mlx5e_execute_l2_action(struct mlx5e_priv *priv,
				    struct mlx5e_l2_hash_node *hn)
{
	u8 action = hn->action;
	u8 mac_addr[ETH_ALEN];
	int l2_err = 0;

	ether_addr_copy(mac_addr, hn->ai.addr);

	switch (action) {
	case MLX5E_ACTION_ADD:
		mlx5e_add_l2_flow_rule(priv, &hn->ai, MLX5E_FULLMATCH);
		if (!is_multicast_ether_addr(mac_addr)) {
			l2_err = mlx5_mpfs_add_mac(priv->mdev, mac_addr);
			hn->mpfs = !l2_err;
		}
		hn->action = MLX5E_ACTION_NONE;
		break;

	case MLX5E_ACTION_DEL:
		if (!is_multicast_ether_addr(mac_addr) && hn->mpfs)
			l2_err = mlx5_mpfs_del_mac(priv->mdev, mac_addr);
		mlx5e_del_l2_flow_rule(priv, &hn->ai);
		mlx5e_del_l2_from_hash(hn);
		break;
	}

	if (l2_err)
		netdev_warn(priv->netdev, "MPFS, failed to %s mac %pM, err(%d)\n",
			    action == MLX5E_ACTION_ADD ? "add" : "del", mac_addr, l2_err);
}

static void mlx5e_sync_netdev_addr(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct netdev_hw_addr *ha;

	netif_addr_lock_bh(netdev);

	mlx5e_add_l2_to_hash(priv->fs.l2.netdev_uc,
			     priv->netdev->dev_addr);

	netdev_for_each_uc_addr(ha, netdev)
		mlx5e_add_l2_to_hash(priv->fs.l2.netdev_uc, ha->addr);

	netdev_for_each_mc_addr(ha, netdev)
		mlx5e_add_l2_to_hash(priv->fs.l2.netdev_mc, ha->addr);

	netif_addr_unlock_bh(netdev);
}

static void mlx5e_fill_addr_array(struct mlx5e_priv *priv, int list_type,
				  u8 addr_array[][ETH_ALEN], int size)
{
	bool is_uc = (list_type == MLX5_NVPRT_LIST_TYPE_UC);
	struct net_device *ndev = priv->netdev;
	struct mlx5e_l2_hash_node *hn;
	struct hlist_head *addr_list;
	struct hlist_node *tmp;
	int i = 0;
	int hi;

	addr_list = is_uc ? priv->fs.l2.netdev_uc : priv->fs.l2.netdev_mc;

	if (is_uc) /* Make sure our own address is pushed first */
		ether_addr_copy(addr_array[i++], ndev->dev_addr);
	else if (priv->fs.l2.broadcast_enabled)
		ether_addr_copy(addr_array[i++], ndev->broadcast);

	mlx5e_for_each_hash_node(hn, tmp, addr_list, hi) {
		if (ether_addr_equal(ndev->dev_addr, hn->ai.addr))
			continue;
		if (i >= size)
			break;
		ether_addr_copy(addr_array[i++], hn->ai.addr);
	}
}

static void mlx5e_vport_context_update_addr_list(struct mlx5e_priv *priv,
						 int list_type)
{
	bool is_uc = (list_type == MLX5_NVPRT_LIST_TYPE_UC);
	struct mlx5e_l2_hash_node *hn;
	u8 (*addr_array)[ETH_ALEN] = NULL;
	struct hlist_head *addr_list;
	struct hlist_node *tmp;
	int max_size;
	int size;
	int err;
	int hi;

	size = is_uc ? 0 : (priv->fs.l2.broadcast_enabled ? 1 : 0);
	max_size = is_uc ?
		1 << MLX5_CAP_GEN(priv->mdev, log_max_current_uc_list) :
		1 << MLX5_CAP_GEN(priv->mdev, log_max_current_mc_list);

	addr_list = is_uc ? priv->fs.l2.netdev_uc : priv->fs.l2.netdev_mc;
	mlx5e_for_each_hash_node(hn, tmp, addr_list, hi)
		size++;

	if (size > max_size) {
		netdev_warn(priv->netdev,
			    "netdev %s list size (%d) > (%d) max vport list size, some addresses will be dropped\n",
			    is_uc ? "UC" : "MC", size, max_size);
		size = max_size;
	}

	if (size) {
		addr_array = kcalloc(size, ETH_ALEN, GFP_KERNEL);
		if (!addr_array) {
			err = -ENOMEM;
			goto out;
		}
		mlx5e_fill_addr_array(priv, list_type, addr_array, size);
	}

	err = mlx5_modify_nic_vport_mac_list(priv->mdev, list_type, addr_array, size);
out:
	if (err)
		netdev_err(priv->netdev,
			   "Failed to modify vport %s list err(%d)\n",
			   is_uc ? "UC" : "MC", err);
	kfree(addr_array);
}

static void mlx5e_vport_context_update(struct mlx5e_priv *priv)
{
	struct mlx5e_l2_table *ea = &priv->fs.l2;

	mlx5e_vport_context_update_addr_list(priv, MLX5_NVPRT_LIST_TYPE_UC);
	mlx5e_vport_context_update_addr_list(priv, MLX5_NVPRT_LIST_TYPE_MC);
	mlx5_modify_nic_vport_promisc(priv->mdev, 0,
				      ea->allmulti_enabled,
				      ea->promisc_enabled);
}

static void mlx5e_apply_netdev_addr(struct mlx5e_priv *priv)
{
	struct mlx5e_l2_hash_node *hn;
	struct hlist_node *tmp;
	int i;

	mlx5e_for_each_hash_node(hn, tmp, priv->fs.l2.netdev_uc, i)
		mlx5e_execute_l2_action(priv, hn);

	mlx5e_for_each_hash_node(hn, tmp, priv->fs.l2.netdev_mc, i)
		mlx5e_execute_l2_action(priv, hn);
}

static void mlx5e_handle_netdev_addr(struct mlx5e_priv *priv)
{
	struct mlx5e_l2_hash_node *hn;
	struct hlist_node *tmp;
	int i;

	mlx5e_for_each_hash_node(hn, tmp, priv->fs.l2.netdev_uc, i)
		hn->action = MLX5E_ACTION_DEL;
	mlx5e_for_each_hash_node(hn, tmp, priv->fs.l2.netdev_mc, i)
		hn->action = MLX5E_ACTION_DEL;

	if (!test_bit(MLX5E_STATE_DESTROYING, &priv->state))
		mlx5e_sync_netdev_addr(priv);

	mlx5e_apply_netdev_addr(priv);
}

#define MLX5E_PROMISC_GROUP0_SIZE BIT(0)
#define MLX5E_PROMISC_TABLE_SIZE MLX5E_PROMISC_GROUP0_SIZE

static int mlx5e_add_promisc_rule(struct mlx5e_priv *priv)
{
	struct mlx5_flow_table *ft = priv->fs.promisc.ft.t;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_handle **rule_p;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = priv->fs.ttc.t;

	rule_p = &priv->fs.promisc.rule;
	*rule_p = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(*rule_p)) {
		err = PTR_ERR(*rule_p);
		*rule_p = NULL;
		netdev_err(priv->netdev, "%s: add promiscuous rule failed\n", __func__);
	}
	kvfree(spec);
	return err;
}

static int mlx5e_create_promisc_table(struct mlx5e_priv *priv)
{
	struct mlx5e_flow_table *ft = &priv->fs.promisc.ft;
	struct mlx5_flow_table_attr ft_attr = {};
	int err;

	ft_attr.max_fte = MLX5E_PROMISC_TABLE_SIZE;
	ft_attr.autogroup.max_num_groups = 1;
	ft_attr.level = MLX5E_PROMISC_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;

	ft->t = mlx5_create_auto_grouped_flow_table(priv->fs.ns, &ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		netdev_err(priv->netdev, "fail to create promisc table err=%d\n", err);
		return err;
	}

	err = mlx5e_add_promisc_rule(priv);
	if (err)
		goto err_destroy_promisc_table;

	return 0;

err_destroy_promisc_table:
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;

	return err;
}

static void mlx5e_del_promisc_rule(struct mlx5e_priv *priv)
{
	if (WARN(!priv->fs.promisc.rule, "Trying to remove non-existing promiscuous rule"))
		return;
	mlx5_del_flow_rules(priv->fs.promisc.rule);
	priv->fs.promisc.rule = NULL;
}

static void mlx5e_destroy_promisc_table(struct mlx5e_priv *priv)
{
	if (WARN(!priv->fs.promisc.ft.t, "Trying to remove non-existing promiscuous table"))
		return;
	mlx5e_del_promisc_rule(priv);
	mlx5_destroy_flow_table(priv->fs.promisc.ft.t);
	priv->fs.promisc.ft.t = NULL;
}

void mlx5e_set_rx_mode_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       set_rx_mode_work);

	struct mlx5e_l2_table *ea = &priv->fs.l2;
	struct net_device *ndev = priv->netdev;

	bool rx_mode_enable   = !test_bit(MLX5E_STATE_DESTROYING, &priv->state);
	bool promisc_enabled   = rx_mode_enable && (ndev->flags & IFF_PROMISC);
	bool allmulti_enabled  = rx_mode_enable && (ndev->flags & IFF_ALLMULTI);
	bool broadcast_enabled = rx_mode_enable;

	bool enable_promisc    = !ea->promisc_enabled   &&  promisc_enabled;
	bool disable_promisc   =  ea->promisc_enabled   && !promisc_enabled;
	bool enable_allmulti   = !ea->allmulti_enabled  &&  allmulti_enabled;
	bool disable_allmulti  =  ea->allmulti_enabled  && !allmulti_enabled;
	bool enable_broadcast  = !ea->broadcast_enabled &&  broadcast_enabled;
	bool disable_broadcast =  ea->broadcast_enabled && !broadcast_enabled;
	int err;

	if (enable_promisc) {
		err = mlx5e_create_promisc_table(priv);
		if (err)
			enable_promisc = false;
		if (!priv->channels.params.vlan_strip_disable && !err)
			netdev_warn_once(ndev,
					 "S-tagged traffic will be dropped while C-tag vlan stripping is enabled\n");
	}
	if (enable_allmulti)
		mlx5e_add_l2_flow_rule(priv, &ea->allmulti, MLX5E_ALLMULTI);
	if (enable_broadcast)
		mlx5e_add_l2_flow_rule(priv, &ea->broadcast, MLX5E_FULLMATCH);

	mlx5e_handle_netdev_addr(priv);

	if (disable_broadcast)
		mlx5e_del_l2_flow_rule(priv, &ea->broadcast);
	if (disable_allmulti)
		mlx5e_del_l2_flow_rule(priv, &ea->allmulti);
	if (disable_promisc)
		mlx5e_destroy_promisc_table(priv);

	ea->promisc_enabled   = promisc_enabled;
	ea->allmulti_enabled  = allmulti_enabled;
	ea->broadcast_enabled = broadcast_enabled;

	mlx5e_vport_context_update(priv);
}

static void mlx5e_destroy_groups(struct mlx5e_flow_table *ft)
{
	int i;

	for (i = ft->num_groups - 1; i >= 0; i--) {
		if (!IS_ERR_OR_NULL(ft->g[i]))
			mlx5_destroy_flow_group(ft->g[i]);
		ft->g[i] = NULL;
	}
	ft->num_groups = 0;
}

void mlx5e_init_l2_addr(struct mlx5e_priv *priv)
{
	ether_addr_copy(priv->fs.l2.broadcast.addr, priv->netdev->broadcast);
}

void mlx5e_destroy_flow_table(struct mlx5e_flow_table *ft)
{
	mlx5e_destroy_groups(ft);
	kfree(ft->g);
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;
}

static void mlx5e_set_inner_ttc_params(struct mlx5e_priv *priv,
				       struct ttc_params *ttc_params)
{
	struct mlx5_flow_table_attr *ft_attr = &ttc_params->ft_attr;
	int tt;

	memset(ttc_params, 0, sizeof(*ttc_params));
	ttc_params->ns = mlx5_get_flow_namespace(priv->mdev,
						 MLX5_FLOW_NAMESPACE_KERNEL);
	ft_attr->level = MLX5E_INNER_TTC_FT_LEVEL;
	ft_attr->prio = MLX5E_NIC_PRIO;

	for (tt = 0; tt < MLX5_NUM_TT; tt++) {
		ttc_params->dests[tt].type = MLX5_FLOW_DESTINATION_TYPE_TIR;
		ttc_params->dests[tt].tir_num =
			tt == MLX5_TT_ANY ?
				mlx5e_rx_res_get_tirn_direct(priv->rx_res, 0) :
				mlx5e_rx_res_get_tirn_rss_inner(priv->rx_res,
								tt);
	}
}

void mlx5e_set_ttc_params(struct mlx5e_priv *priv,
			  struct ttc_params *ttc_params, bool tunnel)

{
	struct mlx5_flow_table_attr *ft_attr = &ttc_params->ft_attr;
	int tt;

	memset(ttc_params, 0, sizeof(*ttc_params));
	ttc_params->ns = mlx5_get_flow_namespace(priv->mdev,
						 MLX5_FLOW_NAMESPACE_KERNEL);
	ft_attr->level = MLX5E_TTC_FT_LEVEL;
	ft_attr->prio = MLX5E_NIC_PRIO;

	for (tt = 0; tt < MLX5_NUM_TT; tt++) {
		ttc_params->dests[tt].type = MLX5_FLOW_DESTINATION_TYPE_TIR;
		ttc_params->dests[tt].tir_num =
			tt == MLX5_TT_ANY ?
				mlx5e_rx_res_get_tirn_direct(priv->rx_res, 0) :
				mlx5e_rx_res_get_tirn_rss(priv->rx_res, tt);
	}

	ttc_params->inner_ttc = tunnel;
	if (!tunnel || !mlx5_tunnel_inner_ft_supported(priv->mdev))
		return;

	for (tt = 0; tt < MLX5_NUM_TUNNEL_TT; tt++) {
		ttc_params->tunnel_dests[tt].type =
			MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
		ttc_params->tunnel_dests[tt].ft = priv->fs.inner_ttc.t;
	}
}

static void mlx5e_del_l2_flow_rule(struct mlx5e_priv *priv,
				   struct mlx5e_l2_rule *ai)
{
	if (!IS_ERR_OR_NULL(ai->rule)) {
		mlx5_del_flow_rules(ai->rule);
		ai->rule = NULL;
	}
}

static int mlx5e_add_l2_flow_rule(struct mlx5e_priv *priv,
				  struct mlx5e_l2_rule *ai, int type)
{
	struct mlx5_flow_table *ft = priv->fs.l2.ft.t;
	struct mlx5_flow_destination dest = {};
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_spec *spec;
	int err = 0;
	u8 *mc_dmac;
	u8 *mv_dmac;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	mc_dmac = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			       outer_headers.dmac_47_16);
	mv_dmac = MLX5_ADDR_OF(fte_match_param, spec->match_value,
			       outer_headers.dmac_47_16);

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = priv->fs.ttc.t;

	switch (type) {
	case MLX5E_FULLMATCH:
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		eth_broadcast_addr(mc_dmac);
		ether_addr_copy(mv_dmac, ai->addr);
		break;

	case MLX5E_ALLMULTI:
		spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		mc_dmac[0] = 0x01;
		mv_dmac[0] = 0x01;
		break;
	}

	ai->rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(ai->rule)) {
		netdev_err(priv->netdev, "%s: add l2 rule(mac:%pM) failed\n",
			   __func__, mv_dmac);
		err = PTR_ERR(ai->rule);
		ai->rule = NULL;
	}

	kvfree(spec);

	return err;
}

#define MLX5E_NUM_L2_GROUPS	   3
#define MLX5E_L2_GROUP1_SIZE	   BIT(15)
#define MLX5E_L2_GROUP2_SIZE	   BIT(0)
#define MLX5E_L2_GROUP_TRAP_SIZE   BIT(0) /* must be last */
#define MLX5E_L2_TABLE_SIZE	   (MLX5E_L2_GROUP1_SIZE +\
				    MLX5E_L2_GROUP2_SIZE +\
				    MLX5E_L2_GROUP_TRAP_SIZE)
static int mlx5e_create_l2_table_groups(struct mlx5e_l2_table *l2_table)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5e_flow_table *ft = &l2_table->ft;
	int ix = 0;
	u8 *mc_dmac;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(MLX5E_NUM_L2_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g)
		return -ENOMEM;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		kfree(ft->g);
		return -ENOMEM;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	mc_dmac = MLX5_ADDR_OF(fte_match_param, mc,
			       outer_headers.dmac_47_16);
	/* Flow Group for full match */
	eth_broadcast_addr(mc_dmac);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_L2_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	/* Flow Group for allmulti */
	eth_zero_addr(mc_dmac);
	mc_dmac[0] = 0x01;
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_L2_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	/* Flow Group for l2 traps */
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_L2_GROUP_TRAP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	kvfree(in);
	return 0;

err_destroy_groups:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	mlx5e_destroy_groups(ft);
	kvfree(in);
	kfree(ft->g);

	return err;
}

static void mlx5e_destroy_l2_table(struct mlx5e_priv *priv)
{
	mlx5e_destroy_flow_table(&priv->fs.l2.ft);
}

static int mlx5e_create_l2_table(struct mlx5e_priv *priv)
{
	struct mlx5e_l2_table *l2_table = &priv->fs.l2;
	struct mlx5e_flow_table *ft = &l2_table->ft;
	struct mlx5_flow_table_attr ft_attr = {};
	int err;

	ft->num_groups = 0;

	ft_attr.max_fte = MLX5E_L2_TABLE_SIZE;
	ft_attr.level = MLX5E_L2_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;

	ft->t = mlx5_create_flow_table(priv->fs.ns, &ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}

	err = mlx5e_create_l2_table_groups(l2_table);
	if (err)
		goto err_destroy_flow_table;

	return 0;

err_destroy_flow_table:
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;

	return err;
}

#define MLX5E_NUM_VLAN_GROUPS	5
#define MLX5E_VLAN_GROUP0_SIZE	BIT(12)
#define MLX5E_VLAN_GROUP1_SIZE	BIT(12)
#define MLX5E_VLAN_GROUP2_SIZE	BIT(1)
#define MLX5E_VLAN_GROUP3_SIZE	BIT(0)
#define MLX5E_VLAN_GROUP_TRAP_SIZE BIT(0) /* must be last */
#define MLX5E_VLAN_TABLE_SIZE	(MLX5E_VLAN_GROUP0_SIZE +\
				 MLX5E_VLAN_GROUP1_SIZE +\
				 MLX5E_VLAN_GROUP2_SIZE +\
				 MLX5E_VLAN_GROUP3_SIZE +\
				 MLX5E_VLAN_GROUP_TRAP_SIZE)

static int __mlx5e_create_vlan_table_groups(struct mlx5e_flow_table *ft, u32 *in,
					    int inlen)
{
	int err;
	int ix = 0;
	u8 *mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.cvlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.first_vid);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP0_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.svlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.first_vid);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.cvlan_tag);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.svlan_tag);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP3_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_VLAN_GROUP_TRAP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err_destroy_groups;
	ft->num_groups++;

	return 0;

err_destroy_groups:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	mlx5e_destroy_groups(ft);

	return err;
}

static int mlx5e_create_vlan_table_groups(struct mlx5e_flow_table *ft)
{
	u32 *in;
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	err = __mlx5e_create_vlan_table_groups(ft, in, inlen);

	kvfree(in);
	return err;
}

static int mlx5e_create_vlan_table(struct mlx5e_priv *priv)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5e_flow_table *ft;
	int err;

	priv->fs.vlan = kvzalloc(sizeof(*priv->fs.vlan), GFP_KERNEL);
	if (!priv->fs.vlan)
		return -ENOMEM;

	ft = &priv->fs.vlan->ft;
	ft->num_groups = 0;

	ft_attr.max_fte = MLX5E_VLAN_TABLE_SIZE;
	ft_attr.level = MLX5E_VLAN_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;

	ft->t = mlx5_create_flow_table(priv->fs.ns, &ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		goto err_free_t;
	}

	ft->g = kcalloc(MLX5E_NUM_VLAN_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g) {
		err = -ENOMEM;
		goto err_destroy_vlan_table;
	}

	err = mlx5e_create_vlan_table_groups(ft);
	if (err)
		goto err_free_g;

	mlx5e_add_vlan_rules(priv);

	return 0;

err_free_g:
	kfree(ft->g);
err_destroy_vlan_table:
	mlx5_destroy_flow_table(ft->t);
err_free_t:
	kvfree(priv->fs.vlan);
	priv->fs.vlan = NULL;

	return err;
}

static void mlx5e_destroy_vlan_table(struct mlx5e_priv *priv)
{
	mlx5e_del_vlan_rules(priv);
	mlx5e_destroy_flow_table(&priv->fs.vlan->ft);
	kvfree(priv->fs.vlan);
}

int mlx5e_create_flow_steering(struct mlx5e_priv *priv)
{
	struct ttc_params ttc_params = {};
	int err;

	priv->fs.ns = mlx5_get_flow_namespace(priv->mdev,
					       MLX5_FLOW_NAMESPACE_KERNEL);

	if (!priv->fs.ns)
		return -EOPNOTSUPP;

	err = mlx5e_arfs_create_tables(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to create arfs tables, err=%d\n",
			   err);
		priv->netdev->hw_features &= ~NETIF_F_NTUPLE;
	}

	if (mlx5_tunnel_inner_ft_supported(priv->mdev)) {
		mlx5e_set_inner_ttc_params(priv, &ttc_params);
		err = mlx5_create_inner_ttc_table(priv->mdev, &ttc_params,
						  &priv->fs.inner_ttc);
		if (err) {
			netdev_err(priv->netdev,
				   "Failed to create inner ttc table, err=%d\n",
				   err);
			goto err_destroy_arfs_tables;
		}
	}

	mlx5e_set_ttc_params(priv, &ttc_params, true);
	err = mlx5_create_ttc_table(priv->mdev, &ttc_params, &priv->fs.ttc);
	if (err) {
		netdev_err(priv->netdev, "Failed to create ttc table, err=%d\n",
			   err);
		goto err_destroy_inner_ttc_table;
	}

	err = mlx5e_create_l2_table(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to create l2 table, err=%d\n",
			   err);
		goto err_destroy_ttc_table;
	}

	err = mlx5e_create_vlan_table(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to create vlan table, err=%d\n",
			   err);
		goto err_destroy_l2_table;
	}

	err = mlx5e_ptp_alloc_rx_fs(priv);
	if (err)
		goto err_destory_vlan_table;

	mlx5e_ethtool_init_steering(priv);

	return 0;

err_destory_vlan_table:
	mlx5e_destroy_vlan_table(priv);
err_destroy_l2_table:
	mlx5e_destroy_l2_table(priv);
err_destroy_ttc_table:
	mlx5_destroy_ttc_table(&priv->fs.ttc);
err_destroy_inner_ttc_table:
	if (mlx5_tunnel_inner_ft_supported(priv->mdev))
		mlx5_destroy_inner_ttc_table(&priv->fs.inner_ttc);
err_destroy_arfs_tables:
	mlx5e_arfs_destroy_tables(priv);

	return err;
}

void mlx5e_destroy_flow_steering(struct mlx5e_priv *priv)
{
	mlx5e_ptp_free_rx_fs(priv);
	mlx5e_destroy_vlan_table(priv);
	mlx5e_destroy_l2_table(priv);
	mlx5_destroy_ttc_table(&priv->fs.ttc);
	if (mlx5_tunnel_inner_ft_supported(priv->mdev))
		mlx5_destroy_inner_ttc_table(&priv->fs.inner_ttc);
	mlx5e_arfs_destroy_tables(priv);
	mlx5e_ethtool_cleanup_steering(priv);
}
