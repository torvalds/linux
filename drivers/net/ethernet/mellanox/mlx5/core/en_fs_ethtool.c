/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#include <linux/mlx5/fs.h>
#include "en.h"

struct mlx5e_ethtool_rule {
	struct list_head             list;
	struct ethtool_rx_flow_spec  flow_spec;
	struct mlx5_flow_rule        *rule;
	struct mlx5e_ethtool_table   *eth_ft;
};

static void put_flow_table(struct mlx5e_ethtool_table *eth_ft)
{
	if (!--eth_ft->num_rules) {
		mlx5_destroy_flow_table(eth_ft->ft);
		eth_ft->ft = NULL;
	}
}

#define MLX5E_ETHTOOL_L3_L4_PRIO 0
#define MLX5E_ETHTOOL_L2_PRIO (MLX5E_ETHTOOL_L3_L4_PRIO + ETHTOOL_NUM_L3_L4_FTS)
#define MLX5E_ETHTOOL_NUM_ENTRIES 64000
#define MLX5E_ETHTOOL_NUM_GROUPS  10
static struct mlx5e_ethtool_table *get_flow_table(struct mlx5e_priv *priv,
						  struct ethtool_rx_flow_spec *fs,
						  int num_tuples)
{
	struct mlx5e_ethtool_table *eth_ft;
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *ft;
	int max_tuples;
	int table_size;
	int prio;

	switch (fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT)) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		max_tuples = ETHTOOL_NUM_L3_L4_FTS;
		prio = MLX5E_ETHTOOL_L3_L4_PRIO + (max_tuples - num_tuples);
		eth_ft = &priv->fs.ethtool.l3_l4_ft[prio];
		break;
	case IP_USER_FLOW:
		max_tuples = ETHTOOL_NUM_L3_L4_FTS;
		prio = MLX5E_ETHTOOL_L3_L4_PRIO + (max_tuples - num_tuples);
		eth_ft = &priv->fs.ethtool.l3_l4_ft[prio];
		break;
	case ETHER_FLOW:
		max_tuples = ETHTOOL_NUM_L2_FTS;
		prio = max_tuples - num_tuples;
		eth_ft = &priv->fs.ethtool.l2_ft[prio];
		prio += MLX5E_ETHTOOL_L2_PRIO;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	eth_ft->num_rules++;
	if (eth_ft->ft)
		return eth_ft;

	ns = mlx5_get_flow_namespace(priv->mdev,
				     MLX5_FLOW_NAMESPACE_ETHTOOL);
	if (!ns)
		return ERR_PTR(-ENOTSUPP);

	table_size = min_t(u32, BIT(MLX5_CAP_FLOWTABLE(priv->mdev,
						       flow_table_properties_nic_receive.log_max_ft_size)),
			   MLX5E_ETHTOOL_NUM_ENTRIES);
	ft = mlx5_create_auto_grouped_flow_table(ns, prio,
						 table_size,
						 MLX5E_ETHTOOL_NUM_GROUPS, 0);
	if (IS_ERR(ft))
		return (void *)ft;

	eth_ft->ft = ft;
	return eth_ft;
}

static void mask_spec(u8 *mask, u8 *val, size_t size)
{
	unsigned int i;

	for (i = 0; i < size; i++, mask++, val++)
		*((u8 *)val) = *((u8 *)mask) & *((u8 *)val);
}

static void set_ips(void *outer_headers_v, void *outer_headers_c, __be32 ip4src_m,
		    __be32 ip4src_v, __be32 ip4dst_m, __be32 ip4dst_v)
{
	if (ip4src_m) {
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, outer_headers_v,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &ip4src_v, sizeof(ip4src_v));
		memset(MLX5_ADDR_OF(fte_match_set_lyr_2_4, outer_headers_c,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       0xff, sizeof(ip4src_m));
	}
	if (ip4dst_m) {
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, outer_headers_v,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &ip4dst_v, sizeof(ip4dst_v));
		memset(MLX5_ADDR_OF(fte_match_set_lyr_2_4, outer_headers_c,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       0xff, sizeof(ip4dst_m));
	}
	MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v,
		 ethertype, ETH_P_IP);
	MLX5_SET(fte_match_set_lyr_2_4, outer_headers_c,
		 ethertype, 0xffff);
}

static int set_flow_attrs(u32 *match_c, u32 *match_v,
			  struct ethtool_rx_flow_spec *fs)
{
	void *outer_headers_c = MLX5_ADDR_OF(fte_match_param, match_c,
					     outer_headers);
	void *outer_headers_v = MLX5_ADDR_OF(fte_match_param, match_v,
					     outer_headers);
	u32 flow_type = fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT);
	struct ethtool_tcpip4_spec *l4_mask;
	struct ethtool_tcpip4_spec *l4_val;
	struct ethtool_usrip4_spec *l3_mask;
	struct ethtool_usrip4_spec *l3_val;
	struct ethhdr *eth_val;
	struct ethhdr *eth_mask;

	switch (flow_type) {
	case TCP_V4_FLOW:
		l4_mask = &fs->m_u.tcp_ip4_spec;
		l4_val = &fs->h_u.tcp_ip4_spec;
		set_ips(outer_headers_v, outer_headers_c, l4_mask->ip4src,
			l4_val->ip4src, l4_mask->ip4dst, l4_val->ip4dst);

		if (l4_mask->psrc) {
			MLX5_SET(fte_match_set_lyr_2_4, outer_headers_c, tcp_sport,
				 0xffff);
			MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v, tcp_sport,
				 ntohs(l4_val->psrc));
		}
		if (l4_mask->pdst) {
			MLX5_SET(fte_match_set_lyr_2_4, outer_headers_c, tcp_dport,
				 0xffff);
			MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v, tcp_dport,
				 ntohs(l4_val->pdst));
		}
		MLX5_SET(fte_match_set_lyr_2_4, outer_headers_c, ip_protocol,
			 0xffff);
		MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v, ip_protocol,
			 IPPROTO_TCP);
		break;
	case UDP_V4_FLOW:
		l4_mask = &fs->m_u.tcp_ip4_spec;
		l4_val = &fs->h_u.tcp_ip4_spec;
		set_ips(outer_headers_v, outer_headers_c, l4_mask->ip4src,
			l4_val->ip4src, l4_mask->ip4dst, l4_val->ip4dst);

		if (l4_mask->psrc) {
			MLX5_SET(fte_match_set_lyr_2_4, outer_headers_c, udp_sport,
				 0xffff);
			MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v, udp_sport,
				 ntohs(l4_val->psrc));
		}
		if (l4_mask->pdst) {
			MLX5_SET(fte_match_set_lyr_2_4, outer_headers_c, udp_dport,
				 0xffff);
			MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v, udp_dport,
				 ntohs(l4_val->pdst));
		}
		MLX5_SET(fte_match_set_lyr_2_4, outer_headers_c, ip_protocol,
			 0xffff);
		MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v, ip_protocol,
			 IPPROTO_UDP);
		break;
	case IP_USER_FLOW:
		l3_mask = &fs->m_u.usr_ip4_spec;
		l3_val = &fs->h_u.usr_ip4_spec;
		set_ips(outer_headers_v, outer_headers_c, l3_mask->ip4src,
			l3_val->ip4src, l3_mask->ip4dst, l3_val->ip4dst);
		break;
	case ETHER_FLOW:
		eth_mask = &fs->m_u.ether_spec;
		eth_val = &fs->h_u.ether_spec;

		mask_spec((u8 *)eth_mask, (u8 *)eth_val, sizeof(*eth_mask));
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4,
					     outer_headers_c, smac_47_16),
				eth_mask->h_source);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4,
					     outer_headers_v, smac_47_16),
				eth_val->h_source);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4,
					     outer_headers_c, dmac_47_16),
				eth_mask->h_dest);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4,
					     outer_headers_v, dmac_47_16),
				eth_val->h_dest);
		MLX5_SET(fte_match_set_lyr_2_4, outer_headers_c, ethertype,
			 ntohs(eth_mask->h_proto));
		MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v, ethertype,
			 ntohs(eth_val->h_proto));
		break;
	default:
		return -EINVAL;
	}

	if ((fs->flow_type & FLOW_EXT) &&
	    (fs->m_ext.vlan_tci & cpu_to_be16(VLAN_VID_MASK))) {
		MLX5_SET(fte_match_set_lyr_2_4, outer_headers_c,
			 vlan_tag, 1);
		MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v,
			 vlan_tag, 1);
		MLX5_SET(fte_match_set_lyr_2_4, outer_headers_c,
			 first_vid, 0xfff);
		MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v,
			 first_vid, ntohs(fs->h_ext.vlan_tci));
	}
	if (fs->flow_type & FLOW_MAC_EXT &&
	    !is_zero_ether_addr(fs->m_ext.h_dest)) {
		mask_spec(fs->m_ext.h_dest, fs->h_ext.h_dest, ETH_ALEN);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4,
					     outer_headers_c, dmac_47_16),
				fs->m_ext.h_dest);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4,
					     outer_headers_v, dmac_47_16),
				fs->h_ext.h_dest);
	}

	return 0;
}

static void add_rule_to_list(struct mlx5e_priv *priv,
			     struct mlx5e_ethtool_rule *rule)
{
	struct mlx5e_ethtool_rule *iter;
	struct list_head *head = &priv->fs.ethtool.rules;

	list_for_each_entry(iter, &priv->fs.ethtool.rules, list) {
		if (iter->flow_spec.location > rule->flow_spec.location)
			break;
		head = &iter->list;
	}
	priv->fs.ethtool.tot_num_rules++;
	list_add(&rule->list, head);
}

static bool outer_header_zero(u32 *match_criteria)
{
	int size = MLX5_FLD_SZ_BYTES(fte_match_param, outer_headers);
	char *outer_headers_c = MLX5_ADDR_OF(fte_match_param, match_criteria,
					     outer_headers);

	return outer_headers_c[0] == 0 && !memcmp(outer_headers_c,
						  outer_headers_c + 1,
						  size - 1);
}

static struct mlx5_flow_rule *add_ethtool_flow_rule(struct mlx5e_priv *priv,
						    struct mlx5_flow_table *ft,
						    struct ethtool_rx_flow_spec *fs)
{
	struct mlx5_flow_destination *dst = NULL;
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_rule *rule;
	int err = 0;
	u32 action;

	spec = mlx5_vzalloc(sizeof(*spec));
	if (!spec)
		return ERR_PTR(-ENOMEM);
	err = set_flow_attrs(spec->match_criteria, spec->match_value,
			     fs);
	if (err)
		goto free;

	if (fs->ring_cookie == RX_CLS_FLOW_DISC) {
		action = MLX5_FLOW_CONTEXT_ACTION_DROP;
	} else {
		dst = kzalloc(sizeof(*dst), GFP_KERNEL);
		if (!dst) {
			err = -ENOMEM;
			goto free;
		}

		dst->type = MLX5_FLOW_DESTINATION_TYPE_TIR;
		dst->tir_num = priv->direct_tir[fs->ring_cookie].tirn;
		action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	}

	spec->match_criteria_enable = (!outer_header_zero(spec->match_criteria));
	rule = mlx5_add_flow_rule(ft, spec, action,
				  MLX5_FS_DEFAULT_FLOW_TAG, dst);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(priv->netdev, "%s: failed to add ethtool steering rule: %d\n",
			   __func__, err);
		goto free;
	}
free:
	kvfree(spec);
	kfree(dst);
	return err ? ERR_PTR(err) : rule;
}

static void del_ethtool_rule(struct mlx5e_priv *priv,
			     struct mlx5e_ethtool_rule *eth_rule)
{
	if (eth_rule->rule)
		mlx5_del_flow_rule(eth_rule->rule);
	list_del(&eth_rule->list);
	priv->fs.ethtool.tot_num_rules--;
	put_flow_table(eth_rule->eth_ft);
	kfree(eth_rule);
}

static struct mlx5e_ethtool_rule *find_ethtool_rule(struct mlx5e_priv *priv,
						    int location)
{
	struct mlx5e_ethtool_rule *iter;

	list_for_each_entry(iter, &priv->fs.ethtool.rules, list) {
		if (iter->flow_spec.location == location)
			return iter;
	}
	return NULL;
}

static struct mlx5e_ethtool_rule *get_ethtool_rule(struct mlx5e_priv *priv,
						   int location)
{
	struct mlx5e_ethtool_rule *eth_rule;

	eth_rule = find_ethtool_rule(priv, location);
	if (eth_rule)
		del_ethtool_rule(priv, eth_rule);

	eth_rule = kzalloc(sizeof(*eth_rule), GFP_KERNEL);
	if (!eth_rule)
		return ERR_PTR(-ENOMEM);

	add_rule_to_list(priv, eth_rule);
	return eth_rule;
}

#define MAX_NUM_OF_ETHTOOL_RULES BIT(10)

#define all_ones(field) (field == (__force typeof(field))-1)
#define all_zeros_or_all_ones(field)		\
	((field) == 0 || (field) == (__force typeof(field))-1)

static int validate_flow(struct mlx5e_priv *priv,
			 struct ethtool_rx_flow_spec *fs)
{
	struct ethtool_tcpip4_spec *l4_mask;
	struct ethtool_usrip4_spec *l3_mask;
	struct ethhdr *eth_mask;
	int num_tuples = 0;

	if (fs->location >= MAX_NUM_OF_ETHTOOL_RULES)
		return -EINVAL;

	if (fs->ring_cookie >= priv->params.num_channels &&
	    fs->ring_cookie != RX_CLS_FLOW_DISC)
		return -EINVAL;

	switch (fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT)) {
	case ETHER_FLOW:
		eth_mask = &fs->m_u.ether_spec;
		if (!is_zero_ether_addr(eth_mask->h_dest))
			num_tuples++;
		if (!is_zero_ether_addr(eth_mask->h_source))
			num_tuples++;
		if (eth_mask->h_proto)
			num_tuples++;
		break;
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		if (fs->m_u.tcp_ip4_spec.tos)
			return -EINVAL;
		l4_mask = &fs->m_u.tcp_ip4_spec;
		if (l4_mask->ip4src) {
			if (!all_ones(l4_mask->ip4src))
				return -EINVAL;
			num_tuples++;
		}
		if (l4_mask->ip4dst) {
			if (!all_ones(l4_mask->ip4dst))
				return -EINVAL;
			num_tuples++;
		}
		if (l4_mask->psrc) {
			if (!all_ones(l4_mask->psrc))
				return -EINVAL;
			num_tuples++;
		}
		if (l4_mask->pdst) {
			if (!all_ones(l4_mask->pdst))
				return -EINVAL;
			num_tuples++;
		}
		/* Flow is TCP/UDP */
		num_tuples++;
		break;
	case IP_USER_FLOW:
		l3_mask = &fs->m_u.usr_ip4_spec;
		if (l3_mask->l4_4_bytes || l3_mask->tos || l3_mask->proto ||
		    fs->h_u.usr_ip4_spec.ip_ver != ETH_RX_NFC_IP4)
			return -EINVAL;
		if (l3_mask->ip4src) {
			if (!all_ones(l3_mask->ip4src))
				return -EINVAL;
			num_tuples++;
		}
		if (l3_mask->ip4dst) {
			if (!all_ones(l3_mask->ip4dst))
				return -EINVAL;
			num_tuples++;
		}
		/* Flow is IPv4 */
		num_tuples++;
		break;
	default:
		return -EINVAL;
	}
	if ((fs->flow_type & FLOW_EXT)) {
		if (fs->m_ext.vlan_etype ||
		    (fs->m_ext.vlan_tci != cpu_to_be16(VLAN_VID_MASK)))
			return -EINVAL;

		if (fs->m_ext.vlan_tci) {
			if (be16_to_cpu(fs->h_ext.vlan_tci) >= VLAN_N_VID)
				return -EINVAL;
		}
		num_tuples++;
	}

	if (fs->flow_type & FLOW_MAC_EXT &&
	    !is_zero_ether_addr(fs->m_ext.h_dest))
		num_tuples++;

	return num_tuples;
}

int mlx5e_ethtool_flow_replace(struct mlx5e_priv *priv,
			       struct ethtool_rx_flow_spec *fs)
{
	struct mlx5e_ethtool_table *eth_ft;
	struct mlx5e_ethtool_rule *eth_rule;
	struct mlx5_flow_rule *rule;
	int num_tuples;
	int err;

	num_tuples = validate_flow(priv, fs);
	if (num_tuples <= 0) {
		netdev_warn(priv->netdev, "%s: flow is not valid\n",  __func__);
		return -EINVAL;
	}

	eth_ft = get_flow_table(priv, fs, num_tuples);
	if (IS_ERR(eth_ft))
		return PTR_ERR(eth_ft);

	eth_rule = get_ethtool_rule(priv, fs->location);
	if (IS_ERR(eth_rule)) {
		put_flow_table(eth_ft);
		return PTR_ERR(eth_rule);
	}

	eth_rule->flow_spec = *fs;
	eth_rule->eth_ft = eth_ft;
	if (!eth_ft->ft) {
		err = -EINVAL;
		goto del_ethtool_rule;
	}
	rule = add_ethtool_flow_rule(priv, eth_ft->ft, fs);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		goto del_ethtool_rule;
	}

	eth_rule->rule = rule;

	return 0;

del_ethtool_rule:
	del_ethtool_rule(priv, eth_rule);

	return err;
}

int mlx5e_ethtool_flow_remove(struct mlx5e_priv *priv,
			      int location)
{
	struct mlx5e_ethtool_rule *eth_rule;
	int err = 0;

	if (location >= MAX_NUM_OF_ETHTOOL_RULES)
		return -ENOSPC;

	eth_rule = find_ethtool_rule(priv, location);
	if (!eth_rule) {
		err =  -ENOENT;
		goto out;
	}

	del_ethtool_rule(priv, eth_rule);
out:
	return err;
}

int mlx5e_ethtool_get_flow(struct mlx5e_priv *priv, struct ethtool_rxnfc *info,
			   int location)
{
	struct mlx5e_ethtool_rule *eth_rule;

	if (location < 0 || location >= MAX_NUM_OF_ETHTOOL_RULES)
		return -EINVAL;

	list_for_each_entry(eth_rule, &priv->fs.ethtool.rules, list) {
		if (eth_rule->flow_spec.location == location) {
			info->fs = eth_rule->flow_spec;
			return 0;
		}
	}

	return -ENOENT;
}

int mlx5e_ethtool_get_all_flows(struct mlx5e_priv *priv, struct ethtool_rxnfc *info,
				u32 *rule_locs)
{
	int location = 0;
	int idx = 0;
	int err = 0;

	info->data = MAX_NUM_OF_ETHTOOL_RULES;
	while ((!err || err == -ENOENT) && idx < info->rule_cnt) {
		err = mlx5e_ethtool_get_flow(priv, info, location);
		if (!err)
			rule_locs[idx++] = location;
		location++;
	}
	return err;
}

void mlx5e_ethtool_cleanup_steering(struct mlx5e_priv *priv)
{
	struct mlx5e_ethtool_rule *iter;
	struct mlx5e_ethtool_rule *temp;

	list_for_each_entry_safe(iter, temp, &priv->fs.ethtool.rules, list)
		del_ethtool_rule(priv, iter);
}

void mlx5e_ethtool_init_steering(struct mlx5e_priv *priv)
{
	INIT_LIST_HEAD(&priv->fs.ethtool.rules);
}
