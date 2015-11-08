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
#include <linux/mlx5/flow_table.h>
#include "en.h"

enum {
	MLX5E_FULLMATCH = 0,
	MLX5E_ALLMULTI  = 1,
	MLX5E_PROMISC   = 2,
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

struct mlx5e_eth_addr_hash_node {
	struct hlist_node          hlist;
	u8                         action;
	struct mlx5e_eth_addr_info ai;
};

static inline int mlx5e_hash_eth_addr(u8 *addr)
{
	return addr[5];
}

static void mlx5e_add_eth_addr_to_hash(struct hlist_head *hash, u8 *addr)
{
	struct mlx5e_eth_addr_hash_node *hn;
	int ix = mlx5e_hash_eth_addr(addr);
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

static void mlx5e_del_eth_addr_from_hash(struct mlx5e_eth_addr_hash_node *hn)
{
	hlist_del(&hn->hlist);
	kfree(hn);
}

static void mlx5e_del_eth_addr_from_flow_table(struct mlx5e_priv *priv,
					       struct mlx5e_eth_addr_info *ai)
{
	void *ft = priv->ft.main;

	if (ai->tt_vec & BIT(MLX5E_TT_IPV6_IPSEC_ESP))
		mlx5_del_flow_table_entry(ft,
					  ai->ft_ix[MLX5E_TT_IPV6_IPSEC_ESP]);

	if (ai->tt_vec & BIT(MLX5E_TT_IPV4_IPSEC_ESP))
		mlx5_del_flow_table_entry(ft,
					  ai->ft_ix[MLX5E_TT_IPV4_IPSEC_ESP]);

	if (ai->tt_vec & BIT(MLX5E_TT_IPV6_IPSEC_AH))
		mlx5_del_flow_table_entry(ft,
					  ai->ft_ix[MLX5E_TT_IPV6_IPSEC_AH]);

	if (ai->tt_vec & BIT(MLX5E_TT_IPV4_IPSEC_AH))
		mlx5_del_flow_table_entry(ft,
					  ai->ft_ix[MLX5E_TT_IPV4_IPSEC_AH]);

	if (ai->tt_vec & BIT(MLX5E_TT_IPV6_TCP))
		mlx5_del_flow_table_entry(ft, ai->ft_ix[MLX5E_TT_IPV6_TCP]);

	if (ai->tt_vec & BIT(MLX5E_TT_IPV4_TCP))
		mlx5_del_flow_table_entry(ft, ai->ft_ix[MLX5E_TT_IPV4_TCP]);

	if (ai->tt_vec & BIT(MLX5E_TT_IPV6_UDP))
		mlx5_del_flow_table_entry(ft, ai->ft_ix[MLX5E_TT_IPV6_UDP]);

	if (ai->tt_vec & BIT(MLX5E_TT_IPV4_UDP))
		mlx5_del_flow_table_entry(ft, ai->ft_ix[MLX5E_TT_IPV4_UDP]);

	if (ai->tt_vec & BIT(MLX5E_TT_IPV6))
		mlx5_del_flow_table_entry(ft, ai->ft_ix[MLX5E_TT_IPV6]);

	if (ai->tt_vec & BIT(MLX5E_TT_IPV4))
		mlx5_del_flow_table_entry(ft, ai->ft_ix[MLX5E_TT_IPV4]);

	if (ai->tt_vec & BIT(MLX5E_TT_ANY))
		mlx5_del_flow_table_entry(ft, ai->ft_ix[MLX5E_TT_ANY]);
}

static int mlx5e_get_eth_addr_type(u8 *addr)
{
	if (is_unicast_ether_addr(addr))
		return MLX5E_UC;

	if ((addr[0] == 0x01) &&
	    (addr[1] == 0x00) &&
	    (addr[2] == 0x5e) &&
	   !(addr[3] &  0x80))
		return MLX5E_MC_IPV4;

	if ((addr[0] == 0x33) &&
	    (addr[1] == 0x33))
		return MLX5E_MC_IPV6;

	return MLX5E_MC_OTHER;
}

static u32 mlx5e_get_tt_vec(struct mlx5e_eth_addr_info *ai, int type)
{
	int eth_addr_type;
	u32 ret;

	switch (type) {
	case MLX5E_FULLMATCH:
		eth_addr_type = mlx5e_get_eth_addr_type(ai->addr);
		switch (eth_addr_type) {
		case MLX5E_UC:
			ret =
				BIT(MLX5E_TT_IPV4_TCP)       |
				BIT(MLX5E_TT_IPV6_TCP)       |
				BIT(MLX5E_TT_IPV4_UDP)       |
				BIT(MLX5E_TT_IPV6_UDP)       |
				BIT(MLX5E_TT_IPV4_IPSEC_AH)  |
				BIT(MLX5E_TT_IPV6_IPSEC_AH)  |
				BIT(MLX5E_TT_IPV4_IPSEC_ESP) |
				BIT(MLX5E_TT_IPV6_IPSEC_ESP) |
				BIT(MLX5E_TT_IPV4)           |
				BIT(MLX5E_TT_IPV6)           |
				BIT(MLX5E_TT_ANY)            |
				0;
			break;

		case MLX5E_MC_IPV4:
			ret =
				BIT(MLX5E_TT_IPV4_UDP)       |
				BIT(MLX5E_TT_IPV4)           |
				0;
			break;

		case MLX5E_MC_IPV6:
			ret =
				BIT(MLX5E_TT_IPV6_UDP)       |
				BIT(MLX5E_TT_IPV6)           |
				0;
			break;

		case MLX5E_MC_OTHER:
			ret =
				BIT(MLX5E_TT_ANY)            |
				0;
			break;
		}

		break;

	case MLX5E_ALLMULTI:
		ret =
			BIT(MLX5E_TT_IPV4_UDP) |
			BIT(MLX5E_TT_IPV6_UDP) |
			BIT(MLX5E_TT_IPV4)     |
			BIT(MLX5E_TT_IPV6)     |
			BIT(MLX5E_TT_ANY)      |
			0;
		break;

	default: /* MLX5E_PROMISC */
		ret =
			BIT(MLX5E_TT_IPV4_TCP)       |
			BIT(MLX5E_TT_IPV6_TCP)       |
			BIT(MLX5E_TT_IPV4_UDP)       |
			BIT(MLX5E_TT_IPV6_UDP)       |
			BIT(MLX5E_TT_IPV4_IPSEC_AH)  |
			BIT(MLX5E_TT_IPV6_IPSEC_AH)  |
			BIT(MLX5E_TT_IPV4_IPSEC_ESP) |
			BIT(MLX5E_TT_IPV6_IPSEC_ESP) |
			BIT(MLX5E_TT_IPV4)           |
			BIT(MLX5E_TT_IPV6)           |
			BIT(MLX5E_TT_ANY)            |
			0;
		break;
	}

	return ret;
}

static int __mlx5e_add_eth_addr_rule(struct mlx5e_priv *priv,
				     struct mlx5e_eth_addr_info *ai, int type,
				     void *flow_context, void *match_criteria)
{
	u8 match_criteria_enable = 0;
	void *match_value;
	void *dest;
	u8   *dmac;
	u8   *match_criteria_dmac;
	void *ft   = priv->ft.main;
	u32  *tirn = priv->tirn;
	u32  *ft_ix;
	u32  tt_vec;
	int  err;

	match_value = MLX5_ADDR_OF(flow_context, flow_context, match_value);
	dmac = MLX5_ADDR_OF(fte_match_param, match_value,
			    outer_headers.dmac_47_16);
	match_criteria_dmac = MLX5_ADDR_OF(fte_match_param, match_criteria,
					   outer_headers.dmac_47_16);
	dest = MLX5_ADDR_OF(flow_context, flow_context, destination);

	MLX5_SET(flow_context, flow_context, action,
		 MLX5_FLOW_CONTEXT_ACTION_FWD_DEST);
	MLX5_SET(flow_context, flow_context, destination_list_size, 1);
	MLX5_SET(dest_format_struct, dest, destination_type,
		 MLX5_FLOW_CONTEXT_DEST_TYPE_TIR);

	switch (type) {
	case MLX5E_FULLMATCH:
		match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		memset(match_criteria_dmac, 0xff, ETH_ALEN);
		ether_addr_copy(dmac, ai->addr);
		break;

	case MLX5E_ALLMULTI:
		match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		match_criteria_dmac[0] = 0x01;
		dmac[0] = 0x01;
		break;

	case MLX5E_PROMISC:
		break;
	}

	tt_vec = mlx5e_get_tt_vec(ai, type);

	ft_ix = &ai->ft_ix[MLX5E_TT_ANY];
	if (tt_vec & BIT(MLX5E_TT_ANY)) {
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_ANY]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_ANY);
	}

	match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, match_criteria,
			 outer_headers.ethertype);

	ft_ix = &ai->ft_ix[MLX5E_TT_IPV4];
	if (tt_vec & BIT(MLX5E_TT_IPV4)) {
		MLX5_SET(fte_match_param, match_value, outer_headers.ethertype,
			 ETH_P_IP);
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_IPV4]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV4);
	}

	ft_ix = &ai->ft_ix[MLX5E_TT_IPV6];
	if (tt_vec & BIT(MLX5E_TT_IPV6)) {
		MLX5_SET(fte_match_param, match_value, outer_headers.ethertype,
			 ETH_P_IPV6);
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_IPV6]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV6);
	}

	MLX5_SET_TO_ONES(fte_match_param, match_criteria,
			 outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, match_value, outer_headers.ip_protocol,
		 IPPROTO_UDP);

	ft_ix = &ai->ft_ix[MLX5E_TT_IPV4_UDP];
	if (tt_vec & BIT(MLX5E_TT_IPV4_UDP)) {
		MLX5_SET(fte_match_param, match_value, outer_headers.ethertype,
			 ETH_P_IP);
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_IPV4_UDP]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV4_UDP);
	}

	ft_ix = &ai->ft_ix[MLX5E_TT_IPV6_UDP];
	if (tt_vec & BIT(MLX5E_TT_IPV6_UDP)) {
		MLX5_SET(fte_match_param, match_value, outer_headers.ethertype,
			 ETH_P_IPV6);
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_IPV6_UDP]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV6_UDP);
	}

	MLX5_SET(fte_match_param, match_value, outer_headers.ip_protocol,
		 IPPROTO_TCP);

	ft_ix = &ai->ft_ix[MLX5E_TT_IPV4_TCP];
	if (tt_vec & BIT(MLX5E_TT_IPV4_TCP)) {
		MLX5_SET(fte_match_param, match_value, outer_headers.ethertype,
			 ETH_P_IP);
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_IPV4_TCP]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV4_TCP);
	}

	ft_ix = &ai->ft_ix[MLX5E_TT_IPV6_TCP];
	if (tt_vec & BIT(MLX5E_TT_IPV6_TCP)) {
		MLX5_SET(fte_match_param, match_value, outer_headers.ethertype,
			 ETH_P_IPV6);
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_IPV6_TCP]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV6_TCP);
	}

	MLX5_SET(fte_match_param, match_value, outer_headers.ip_protocol,
		 IPPROTO_AH);

	ft_ix = &ai->ft_ix[MLX5E_TT_IPV4_IPSEC_AH];
	if (tt_vec & BIT(MLX5E_TT_IPV4_IPSEC_AH)) {
		MLX5_SET(fte_match_param, match_value, outer_headers.ethertype,
			 ETH_P_IP);
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_IPV4_IPSEC_AH]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV4_IPSEC_AH);
	}

	ft_ix = &ai->ft_ix[MLX5E_TT_IPV6_IPSEC_AH];
	if (tt_vec & BIT(MLX5E_TT_IPV6_IPSEC_AH)) {
		MLX5_SET(fte_match_param, match_value, outer_headers.ethertype,
			 ETH_P_IPV6);
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_IPV6_IPSEC_AH]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV6_IPSEC_AH);
	}

	MLX5_SET(fte_match_param, match_value, outer_headers.ip_protocol,
		 IPPROTO_ESP);

	ft_ix = &ai->ft_ix[MLX5E_TT_IPV4_IPSEC_ESP];
	if (tt_vec & BIT(MLX5E_TT_IPV4_IPSEC_ESP)) {
		MLX5_SET(fte_match_param, match_value, outer_headers.ethertype,
			 ETH_P_IP);
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_IPV4_IPSEC_ESP]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV4_IPSEC_ESP);
	}

	ft_ix = &ai->ft_ix[MLX5E_TT_IPV6_IPSEC_ESP];
	if (tt_vec & BIT(MLX5E_TT_IPV6_IPSEC_ESP)) {
		MLX5_SET(fte_match_param, match_value, outer_headers.ethertype,
			 ETH_P_IPV6);
		MLX5_SET(dest_format_struct, dest, destination_id,
			 tirn[MLX5E_TT_IPV6_IPSEC_ESP]);
		err = mlx5_add_flow_table_entry(ft, match_criteria_enable,
						match_criteria, flow_context,
						ft_ix);
		if (err)
			goto err_del_ai;

		ai->tt_vec |= BIT(MLX5E_TT_IPV6_IPSEC_ESP);
	}

	return 0;

err_del_ai:
	mlx5e_del_eth_addr_from_flow_table(priv, ai);

	return err;
}

static int mlx5e_add_eth_addr_rule(struct mlx5e_priv *priv,
				   struct mlx5e_eth_addr_info *ai, int type)
{
	u32 *flow_context;
	u32 *match_criteria;
	int err;

	flow_context   = mlx5_vzalloc(MLX5_ST_SZ_BYTES(flow_context) +
				      MLX5_ST_SZ_BYTES(dest_format_struct));
	match_criteria = mlx5_vzalloc(MLX5_ST_SZ_BYTES(fte_match_param));
	if (!flow_context || !match_criteria) {
		netdev_err(priv->netdev, "%s: alloc failed\n", __func__);
		err = -ENOMEM;
		goto add_eth_addr_rule_out;
	}

	err = __mlx5e_add_eth_addr_rule(priv, ai, type, flow_context,
					match_criteria);
	if (err)
		netdev_err(priv->netdev, "%s: failed\n", __func__);

add_eth_addr_rule_out:
	kvfree(match_criteria);
	kvfree(flow_context);
	return err;
}

enum mlx5e_vlan_rule_type {
	MLX5E_VLAN_RULE_TYPE_UNTAGGED,
	MLX5E_VLAN_RULE_TYPE_ANY_VID,
	MLX5E_VLAN_RULE_TYPE_MATCH_VID,
};

static int mlx5e_add_vlan_rule(struct mlx5e_priv *priv,
			       enum mlx5e_vlan_rule_type rule_type, u16 vid)
{
	u8 match_criteria_enable = 0;
	u32 *flow_context;
	void *match_value;
	void *dest;
	u32 *match_criteria;
	u32 *ft_ix;
	int err;

	flow_context   = mlx5_vzalloc(MLX5_ST_SZ_BYTES(flow_context) +
				      MLX5_ST_SZ_BYTES(dest_format_struct));
	match_criteria = mlx5_vzalloc(MLX5_ST_SZ_BYTES(fte_match_param));
	if (!flow_context || !match_criteria) {
		netdev_err(priv->netdev, "%s: alloc failed\n", __func__);
		err = -ENOMEM;
		goto add_vlan_rule_out;
	}
	match_value = MLX5_ADDR_OF(flow_context, flow_context, match_value);
	dest = MLX5_ADDR_OF(flow_context, flow_context, destination);

	MLX5_SET(flow_context, flow_context, action,
		 MLX5_FLOW_CONTEXT_ACTION_FWD_DEST);
	MLX5_SET(flow_context, flow_context, destination_list_size, 1);
	MLX5_SET(dest_format_struct, dest, destination_type,
		 MLX5_FLOW_CONTEXT_DEST_TYPE_FLOW_TABLE);
	MLX5_SET(dest_format_struct, dest, destination_id,
		 mlx5_get_flow_table_id(priv->ft.main));

	match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, match_criteria,
			 outer_headers.vlan_tag);

	switch (rule_type) {
	case MLX5E_VLAN_RULE_TYPE_UNTAGGED:
		ft_ix = &priv->vlan.untagged_rule_ft_ix;
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_VID:
		ft_ix = &priv->vlan.any_vlan_rule_ft_ix;
		MLX5_SET(fte_match_param, match_value, outer_headers.vlan_tag,
			 1);
		break;
	default: /* MLX5E_VLAN_RULE_TYPE_MATCH_VID */
		ft_ix = &priv->vlan.active_vlans_ft_ix[vid];
		MLX5_SET(fte_match_param, match_value, outer_headers.vlan_tag,
			 1);
		MLX5_SET_TO_ONES(fte_match_param, match_criteria,
				 outer_headers.first_vid);
		MLX5_SET(fte_match_param, match_value, outer_headers.first_vid,
			 vid);
		break;
	}

	err = mlx5_add_flow_table_entry(priv->ft.vlan, match_criteria_enable,
					match_criteria, flow_context, ft_ix);
	if (err)
		netdev_err(priv->netdev, "%s: failed\n", __func__);

add_vlan_rule_out:
	kvfree(match_criteria);
	kvfree(flow_context);
	return err;
}

static void mlx5e_del_vlan_rule(struct mlx5e_priv *priv,
				enum mlx5e_vlan_rule_type rule_type, u16 vid)
{
	switch (rule_type) {
	case MLX5E_VLAN_RULE_TYPE_UNTAGGED:
		mlx5_del_flow_table_entry(priv->ft.vlan,
					  priv->vlan.untagged_rule_ft_ix);
		break;
	case MLX5E_VLAN_RULE_TYPE_ANY_VID:
		mlx5_del_flow_table_entry(priv->ft.vlan,
					  priv->vlan.any_vlan_rule_ft_ix);
		break;
	case MLX5E_VLAN_RULE_TYPE_MATCH_VID:
		mlx5_del_flow_table_entry(priv->ft.vlan,
					  priv->vlan.active_vlans_ft_ix[vid]);
		break;
	}
}

void mlx5e_enable_vlan_filter(struct mlx5e_priv *priv)
{
	if (!priv->vlan.filter_disabled)
		return;

	priv->vlan.filter_disabled = false;
	if (priv->netdev->flags & IFF_PROMISC)
		return;
	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_VID, 0);
}

void mlx5e_disable_vlan_filter(struct mlx5e_priv *priv)
{
	if (priv->vlan.filter_disabled)
		return;

	priv->vlan.filter_disabled = true;
	if (priv->netdev->flags & IFF_PROMISC)
		return;
	mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_VID, 0);
}

int mlx5e_vlan_rx_add_vid(struct net_device *dev, __always_unused __be16 proto,
			  u16 vid)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_VID, vid);
}

int mlx5e_vlan_rx_kill_vid(struct net_device *dev, __always_unused __be16 proto,
			   u16 vid)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_MATCH_VID, vid);

	return 0;
}

#define mlx5e_for_each_hash_node(hn, tmp, hash, i) \
	for (i = 0; i < MLX5E_ETH_ADDR_HASH_SIZE; i++) \
		hlist_for_each_entry_safe(hn, tmp, &hash[i], hlist)

static void mlx5e_execute_action(struct mlx5e_priv *priv,
				 struct mlx5e_eth_addr_hash_node *hn)
{
	switch (hn->action) {
	case MLX5E_ACTION_ADD:
		mlx5e_add_eth_addr_rule(priv, &hn->ai, MLX5E_FULLMATCH);
		hn->action = MLX5E_ACTION_NONE;
		break;

	case MLX5E_ACTION_DEL:
		mlx5e_del_eth_addr_from_flow_table(priv, &hn->ai);
		mlx5e_del_eth_addr_from_hash(hn);
		break;
	}
}

static void mlx5e_sync_netdev_addr(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct netdev_hw_addr *ha;

	netif_addr_lock_bh(netdev);

	mlx5e_add_eth_addr_to_hash(priv->eth_addr.netdev_uc,
				   priv->netdev->dev_addr);

	netdev_for_each_uc_addr(ha, netdev)
		mlx5e_add_eth_addr_to_hash(priv->eth_addr.netdev_uc, ha->addr);

	netdev_for_each_mc_addr(ha, netdev)
		mlx5e_add_eth_addr_to_hash(priv->eth_addr.netdev_mc, ha->addr);

	netif_addr_unlock_bh(netdev);
}

static void mlx5e_apply_netdev_addr(struct mlx5e_priv *priv)
{
	struct mlx5e_eth_addr_hash_node *hn;
	struct hlist_node *tmp;
	int i;

	mlx5e_for_each_hash_node(hn, tmp, priv->eth_addr.netdev_uc, i)
		mlx5e_execute_action(priv, hn);

	mlx5e_for_each_hash_node(hn, tmp, priv->eth_addr.netdev_mc, i)
		mlx5e_execute_action(priv, hn);
}

static void mlx5e_handle_netdev_addr(struct mlx5e_priv *priv)
{
	struct mlx5e_eth_addr_hash_node *hn;
	struct hlist_node *tmp;
	int i;

	mlx5e_for_each_hash_node(hn, tmp, priv->eth_addr.netdev_uc, i)
		hn->action = MLX5E_ACTION_DEL;
	mlx5e_for_each_hash_node(hn, tmp, priv->eth_addr.netdev_mc, i)
		hn->action = MLX5E_ACTION_DEL;

	if (!test_bit(MLX5E_STATE_DESTROYING, &priv->state))
		mlx5e_sync_netdev_addr(priv);

	mlx5e_apply_netdev_addr(priv);
}

void mlx5e_set_rx_mode_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       set_rx_mode_work);

	struct mlx5e_eth_addr_db *ea = &priv->eth_addr;
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

	if (enable_promisc) {
		mlx5e_add_eth_addr_rule(priv, &ea->promisc, MLX5E_PROMISC);
		if (!priv->vlan.filter_disabled)
			mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_VID,
					    0);
	}
	if (enable_allmulti)
		mlx5e_add_eth_addr_rule(priv, &ea->allmulti, MLX5E_ALLMULTI);
	if (enable_broadcast)
		mlx5e_add_eth_addr_rule(priv, &ea->broadcast, MLX5E_FULLMATCH);

	mlx5e_handle_netdev_addr(priv);

	if (disable_broadcast)
		mlx5e_del_eth_addr_from_flow_table(priv, &ea->broadcast);
	if (disable_allmulti)
		mlx5e_del_eth_addr_from_flow_table(priv, &ea->allmulti);
	if (disable_promisc) {
		if (!priv->vlan.filter_disabled)
			mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_ANY_VID,
					    0);
		mlx5e_del_eth_addr_from_flow_table(priv, &ea->promisc);
	}

	ea->promisc_enabled   = promisc_enabled;
	ea->allmulti_enabled  = allmulti_enabled;
	ea->broadcast_enabled = broadcast_enabled;
}

void mlx5e_init_eth_addr(struct mlx5e_priv *priv)
{
	ether_addr_copy(priv->eth_addr.broadcast.addr, priv->netdev->broadcast);
}

static int mlx5e_create_main_flow_table(struct mlx5e_priv *priv)
{
	struct mlx5_flow_table_group *g;
	u8 *dmac;

	g = kcalloc(9, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;

	g[0].log_sz = 3;
	g[0].match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, g[0].match_criteria,
			 outer_headers.ethertype);
	MLX5_SET_TO_ONES(fte_match_param, g[0].match_criteria,
			 outer_headers.ip_protocol);

	g[1].log_sz = 1;
	g[1].match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, g[1].match_criteria,
			 outer_headers.ethertype);

	g[2].log_sz = 0;

	g[3].log_sz = 14;
	g[3].match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	dmac = MLX5_ADDR_OF(fte_match_param, g[3].match_criteria,
			    outer_headers.dmac_47_16);
	memset(dmac, 0xff, ETH_ALEN);
	MLX5_SET_TO_ONES(fte_match_param, g[3].match_criteria,
			 outer_headers.ethertype);
	MLX5_SET_TO_ONES(fte_match_param, g[3].match_criteria,
			 outer_headers.ip_protocol);

	g[4].log_sz = 13;
	g[4].match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	dmac = MLX5_ADDR_OF(fte_match_param, g[4].match_criteria,
			    outer_headers.dmac_47_16);
	memset(dmac, 0xff, ETH_ALEN);
	MLX5_SET_TO_ONES(fte_match_param, g[4].match_criteria,
			 outer_headers.ethertype);

	g[5].log_sz = 11;
	g[5].match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	dmac = MLX5_ADDR_OF(fte_match_param, g[5].match_criteria,
			    outer_headers.dmac_47_16);
	memset(dmac, 0xff, ETH_ALEN);

	g[6].log_sz = 2;
	g[6].match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	dmac = MLX5_ADDR_OF(fte_match_param, g[6].match_criteria,
			    outer_headers.dmac_47_16);
	dmac[0] = 0x01;
	MLX5_SET_TO_ONES(fte_match_param, g[6].match_criteria,
			 outer_headers.ethertype);
	MLX5_SET_TO_ONES(fte_match_param, g[6].match_criteria,
			 outer_headers.ip_protocol);

	g[7].log_sz = 1;
	g[7].match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	dmac = MLX5_ADDR_OF(fte_match_param, g[7].match_criteria,
			    outer_headers.dmac_47_16);
	dmac[0] = 0x01;
	MLX5_SET_TO_ONES(fte_match_param, g[7].match_criteria,
			 outer_headers.ethertype);

	g[8].log_sz = 0;
	g[8].match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	dmac = MLX5_ADDR_OF(fte_match_param, g[8].match_criteria,
			    outer_headers.dmac_47_16);
	dmac[0] = 0x01;
	priv->ft.main = mlx5_create_flow_table(priv->mdev, 1,
					       MLX5_FLOW_TABLE_TYPE_NIC_RCV,
					       9, g);
	kfree(g);

	return priv->ft.main ? 0 : -ENOMEM;
}

static void mlx5e_destroy_main_flow_table(struct mlx5e_priv *priv)
{
	mlx5_destroy_flow_table(priv->ft.main);
}

static int mlx5e_create_vlan_flow_table(struct mlx5e_priv *priv)
{
	struct mlx5_flow_table_group *g;

	g = kcalloc(2, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;

	g[0].log_sz = 12;
	g[0].match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, g[0].match_criteria,
			 outer_headers.vlan_tag);
	MLX5_SET_TO_ONES(fte_match_param, g[0].match_criteria,
			 outer_headers.first_vid);

	/* untagged + any vlan id */
	g[1].log_sz = 1;
	g[1].match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, g[1].match_criteria,
			 outer_headers.vlan_tag);

	priv->ft.vlan = mlx5_create_flow_table(priv->mdev, 0,
					       MLX5_FLOW_TABLE_TYPE_NIC_RCV,
					       2, g);

	kfree(g);
	return priv->ft.vlan ? 0 : -ENOMEM;
}

static void mlx5e_destroy_vlan_flow_table(struct mlx5e_priv *priv)
{
	mlx5_destroy_flow_table(priv->ft.vlan);
}

int mlx5e_create_flow_tables(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_create_main_flow_table(priv);
	if (err)
		return err;

	err = mlx5e_create_vlan_flow_table(priv);
	if (err)
		goto err_destroy_main_flow_table;

	err = mlx5e_add_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_UNTAGGED, 0);
	if (err)
		goto err_destroy_vlan_flow_table;

	return 0;

err_destroy_vlan_flow_table:
	mlx5e_destroy_vlan_flow_table(priv);

err_destroy_main_flow_table:
	mlx5e_destroy_main_flow_table(priv);

	return err;
}

void mlx5e_destroy_flow_tables(struct mlx5e_priv *priv)
{
	mlx5e_del_vlan_rule(priv, MLX5E_VLAN_RULE_TYPE_UNTAGGED, 0);
	mlx5e_destroy_vlan_flow_table(priv);
	mlx5e_destroy_main_flow_table(priv);
}
