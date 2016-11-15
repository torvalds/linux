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

#include <net/flow_dissector.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_skbedit.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/device.h>
#include <linux/rhashtable.h>
#include <net/switchdev.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_vlan.h>
#include <net/tc_act/tc_tunnel_key.h>
#include <net/vxlan.h>
#include "en.h"
#include "en_tc.h"
#include "eswitch.h"
#include "vxlan.h"

struct mlx5e_tc_flow {
	struct rhash_head	node;
	u64			cookie;
	struct mlx5_flow_handle *rule;
	struct list_head	encap; /* flows sharing the same encap */
	struct mlx5_esw_flow_attr *attr;
};

enum {
	MLX5_HEADER_TYPE_VXLAN = 0x0,
	MLX5_HEADER_TYPE_NVGRE = 0x1,
};

#define MLX5E_TC_TABLE_NUM_ENTRIES 1024
#define MLX5E_TC_TABLE_NUM_GROUPS 4

static struct mlx5_flow_handle *
mlx5e_tc_add_nic_flow(struct mlx5e_priv *priv,
		      struct mlx5_flow_spec *spec,
		      u32 action, u32 flow_tag)
{
	struct mlx5_core_dev *dev = priv->mdev;
	struct mlx5_flow_destination dest = { 0 };
	struct mlx5_flow_act flow_act = {
		.action = action,
		.flow_tag = flow_tag,
		.encap_id = 0,
	};
	struct mlx5_fc *counter = NULL;
	struct mlx5_flow_handle *rule;
	bool table_created = false;

	if (action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
		dest.ft = priv->fs.vlan.ft.t;
	} else if (action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		counter = mlx5_fc_create(dev, true);
		if (IS_ERR(counter))
			return ERR_CAST(counter);

		dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dest.counter = counter;
	}

	if (IS_ERR_OR_NULL(priv->fs.tc.t)) {
		priv->fs.tc.t =
			mlx5_create_auto_grouped_flow_table(priv->fs.ns,
							    MLX5E_TC_PRIO,
							    MLX5E_TC_TABLE_NUM_ENTRIES,
							    MLX5E_TC_TABLE_NUM_GROUPS,
							    0, 0);
		if (IS_ERR(priv->fs.tc.t)) {
			netdev_err(priv->netdev,
				   "Failed to create tc offload table\n");
			rule = ERR_CAST(priv->fs.tc.t);
			goto err_create_ft;
		}

		table_created = true;
	}

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	rule = mlx5_add_flow_rules(priv->fs.tc.t, spec, &flow_act, &dest, 1);

	if (IS_ERR(rule))
		goto err_add_rule;

	return rule;

err_add_rule:
	if (table_created) {
		mlx5_destroy_flow_table(priv->fs.tc.t);
		priv->fs.tc.t = NULL;
	}
err_create_ft:
	mlx5_fc_destroy(dev, counter);

	return rule;
}

static struct mlx5_flow_handle *
mlx5e_tc_add_fdb_flow(struct mlx5e_priv *priv,
		      struct mlx5_flow_spec *spec,
		      struct mlx5_esw_flow_attr *attr)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	int err;

	err = mlx5_eswitch_add_vlan_action(esw, attr);
	if (err)
		return ERR_PTR(err);

	return mlx5_eswitch_add_offloaded_rule(esw, spec, attr);
}

static void mlx5e_tc_del_flow(struct mlx5e_priv *priv,
			      struct mlx5_flow_handle *rule,
			      struct mlx5_esw_flow_attr *attr)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_fc *counter = NULL;

	counter = mlx5_flow_rule_counter(rule);

	if (esw && esw->mode == SRIOV_OFFLOADS)
		mlx5_eswitch_del_vlan_action(esw, attr);

	mlx5_del_flow_rules(rule);

	mlx5_fc_destroy(priv->mdev, counter);

	if (!mlx5e_tc_num_filters(priv) && (priv->fs.tc.t)) {
		mlx5_destroy_flow_table(priv->fs.tc.t);
		priv->fs.tc.t = NULL;
	}
}

static void parse_vxlan_attr(struct mlx5_flow_spec *spec,
			     struct tc_cls_flower_offload *f)
{
	void *headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				       outer_headers);
	void *headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				       outer_headers);
	void *misc_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    misc_parameters);
	void *misc_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    misc_parameters);

	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol, IPPROTO_UDP);

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_dissector_key_keyid *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_KEYID,
						  f->key);
		struct flow_dissector_key_keyid *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_KEYID,
						  f->mask);
		MLX5_SET(fte_match_set_misc, misc_c, vxlan_vni,
			 be32_to_cpu(mask->keyid));
		MLX5_SET(fte_match_set_misc, misc_v, vxlan_vni,
			 be32_to_cpu(key->keyid));
	}
}

static int parse_tunnel_attr(struct mlx5e_priv *priv,
			     struct mlx5_flow_spec *spec,
			     struct tc_cls_flower_offload *f)
{
	void *headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				       outer_headers);
	void *headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				       outer_headers);

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_PORTS)) {
		struct flow_dissector_key_ports *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_PORTS,
						  f->key);
		struct flow_dissector_key_ports *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_PORTS,
						  f->mask);

		/* Full udp dst port must be given */
		if (memchr_inv(&mask->dst, 0xff, sizeof(mask->dst)))
			return -EOPNOTSUPP;

		/* udp src port isn't supported */
		if (memchr_inv(&mask->src, 0, sizeof(mask->src)))
			return -EOPNOTSUPP;

		if (mlx5e_vxlan_lookup_port(priv, be16_to_cpu(key->dst)) &&
		    MLX5_CAP_ESW(priv->mdev, vxlan_encap_decap))
			parse_vxlan_attr(spec, f);
		else
			return -EOPNOTSUPP;

		MLX5_SET(fte_match_set_lyr_2_4, headers_c,
			 udp_dport, ntohs(mask->dst));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v,
			 udp_dport, ntohs(key->dst));

	} else { /* udp dst port must be given */
			return -EOPNOTSUPP;
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS)) {
		struct flow_dissector_key_ipv4_addrs *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
						  f->key);
		struct flow_dissector_key_ipv4_addrs *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
						  f->mask);
		MLX5_SET(fte_match_set_lyr_2_4, headers_c,
			 src_ipv4_src_ipv6.ipv4_layout.ipv4,
			 ntohl(mask->src));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v,
			 src_ipv4_src_ipv6.ipv4_layout.ipv4,
			 ntohl(key->src));

		MLX5_SET(fte_match_set_lyr_2_4, headers_c,
			 dst_ipv4_dst_ipv6.ipv4_layout.ipv4,
			 ntohl(mask->dst));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v,
			 dst_ipv4_dst_ipv6.ipv4_layout.ipv4,
			 ntohl(key->dst));
	}

	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ethertype);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, ethertype, ETH_P_IP);

	/* Enforce DMAC when offloading incoming tunneled flows.
	 * Flow counters require a match on the DMAC.
	 */
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, dmac_47_16);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, dmac_15_0);
	ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				     dmac_47_16), priv->netdev->dev_addr);

	/* let software handle IP fragments */
	MLX5_SET(fte_match_set_lyr_2_4, headers_c, frag, 1);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, frag, 0);

	return 0;
}

static int parse_cls_flower(struct mlx5e_priv *priv, struct mlx5_flow_spec *spec,
			    struct tc_cls_flower_offload *f)
{
	void *headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				       outer_headers);
	void *headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				       outer_headers);
	u16 addr_type = 0;
	u8 ip_proto = 0;

	if (f->dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_KEYID) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_PORTS)	|
	      BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL))) {
		netdev_warn(priv->netdev, "Unsupported key used: 0x%x\n",
			    f->dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if ((dissector_uses_key(f->dissector,
				FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) ||
	     dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_KEYID) ||
	     dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_PORTS)) &&
	    dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_CONTROL)) {
		struct flow_dissector_key_control *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_CONTROL,
						  f->key);
		switch (key->addr_type) {
		case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
			if (parse_tunnel_attr(priv, spec, f))
				return -EOPNOTSUPP;
			break;
		default:
			return -EOPNOTSUPP;
		}

		/* In decap flow, header pointers should point to the inner
		 * headers, outer header were already set by parse_tunnel_attr
		 */
		headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
					 inner_headers);
		headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
					 inner_headers);
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_CONTROL,
						  f->key);
		addr_type = key->addr_type;
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  f->key);
		struct flow_dissector_key_basic *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  f->mask);
		ip_proto = key->ip_proto;

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ethertype,
			 ntohs(mask->n_proto));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ethertype,
			 ntohs(key->n_proto));

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_protocol,
			 mask->ip_proto);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol,
			 key->ip_proto);
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_dissector_key_eth_addrs *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ETH_ADDRS,
						  f->key);
		struct flow_dissector_key_eth_addrs *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ETH_ADDRS,
						  f->mask);

		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
					     dmac_47_16),
				mask->dst);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					     dmac_47_16),
				key->dst);

		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
					     smac_47_16),
				mask->src);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					     smac_47_16),
				key->src);
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_dissector_key_vlan *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_VLAN,
						  f->key);
		struct flow_dissector_key_vlan *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_VLAN,
						  f->mask);
		if (mask->vlan_id || mask->vlan_priority) {
			MLX5_SET(fte_match_set_lyr_2_4, headers_c, vlan_tag, 1);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v, vlan_tag, 1);

			MLX5_SET(fte_match_set_lyr_2_4, headers_c, first_vid, mask->vlan_id);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v, first_vid, key->vlan_id);

			MLX5_SET(fte_match_set_lyr_2_4, headers_c, first_prio, mask->vlan_priority);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v, first_prio, key->vlan_priority);
		}
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_dissector_key_ipv4_addrs *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						  f->key);
		struct flow_dissector_key_ipv4_addrs *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						  f->mask);

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &mask->src, sizeof(mask->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &key->src, sizeof(key->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &mask->dst, sizeof(mask->dst));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &key->dst, sizeof(key->dst));
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_dissector_key_ipv6_addrs *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						  f->key);
		struct flow_dissector_key_ipv6_addrs *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						  f->mask);

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &mask->src, sizeof(mask->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &key->src, sizeof(key->src));

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &mask->dst, sizeof(mask->dst));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &key->dst, sizeof(key->dst));
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_dissector_key_ports *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_PORTS,
						  f->key);
		struct flow_dissector_key_ports *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_PORTS,
						  f->mask);
		switch (ip_proto) {
		case IPPROTO_TCP:
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 tcp_sport, ntohs(mask->src));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 tcp_sport, ntohs(key->src));

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 tcp_dport, ntohs(mask->dst));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 tcp_dport, ntohs(key->dst));
			break;

		case IPPROTO_UDP:
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 udp_sport, ntohs(mask->src));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 udp_sport, ntohs(key->src));

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 udp_dport, ntohs(mask->dst));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 udp_dport, ntohs(key->dst));
			break;
		default:
			netdev_err(priv->netdev,
				   "Only UDP and TCP transport are supported\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int parse_tc_nic_actions(struct mlx5e_priv *priv, struct tcf_exts *exts,
				u32 *action, u32 *flow_tag)
{
	const struct tc_action *a;
	LIST_HEAD(actions);

	if (tc_no_actions(exts))
		return -EINVAL;

	*flow_tag = MLX5_FS_DEFAULT_FLOW_TAG;
	*action = 0;

	tcf_exts_to_list(exts, &actions);
	list_for_each_entry(a, &actions, list) {
		/* Only support a single action per rule */
		if (*action)
			return -EINVAL;

		if (is_tcf_gact_shot(a)) {
			*action |= MLX5_FLOW_CONTEXT_ACTION_DROP;
			if (MLX5_CAP_FLOWTABLE(priv->mdev,
					       flow_table_properties_nic_receive.flow_counter))
				*action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
			continue;
		}

		if (is_tcf_skbedit_mark(a)) {
			u32 mark = tcf_skbedit_mark(a);

			if (mark & ~MLX5E_TC_FLOW_ID_MASK) {
				netdev_warn(priv->netdev, "Bad flow mark - only 16 bit is supported: 0x%x\n",
					    mark);
				return -EINVAL;
			}

			*flow_tag = mark;
			*action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
			continue;
		}

		return -EINVAL;
	}

	return 0;
}

static inline int cmp_encap_info(struct mlx5_encap_info *a,
				 struct mlx5_encap_info *b)
{
	return memcmp(a, b, sizeof(*a));
}

static inline int hash_encap_info(struct mlx5_encap_info *info)
{
	return jhash(info, sizeof(*info), 0);
}

static int mlx5e_route_lookup_ipv4(struct mlx5e_priv *priv,
				   struct net_device *mirred_dev,
				   struct net_device **out_dev,
				   struct flowi4 *fl4,
				   struct neighbour **out_n,
				   __be32 *saddr,
				   int *out_ttl)
{
	struct rtable *rt;
	struct neighbour *n = NULL;
	int ttl;

#if IS_ENABLED(CONFIG_INET)
	rt = ip_route_output_key(dev_net(mirred_dev), fl4);
	if (IS_ERR(rt)) {
		pr_warn("%s: no route to %pI4\n", __func__, &fl4->daddr);
		return -EOPNOTSUPP;
	}
#else
	return -EOPNOTSUPP;
#endif

	if (!switchdev_port_same_parent_id(priv->netdev, rt->dst.dev)) {
		pr_warn("%s: Can't offload the flow, netdevices aren't on the same HW e-switch\n",
			__func__);
		ip_rt_put(rt);
		return -EOPNOTSUPP;
	}

	ttl = ip4_dst_hoplimit(&rt->dst);
	n = dst_neigh_lookup(&rt->dst, &fl4->daddr);
	ip_rt_put(rt);
	if (!n)
		return -ENOMEM;

	*out_n = n;
	*saddr = fl4->saddr;
	*out_ttl = ttl;
	*out_dev = rt->dst.dev;

	return 0;
}

static int gen_vxlan_header_ipv4(struct net_device *out_dev,
				 char buf[],
				 unsigned char h_dest[ETH_ALEN],
				 int ttl,
				 __be32 daddr,
				 __be32 saddr,
				 __be16 udp_dst_port,
				 __be32 vx_vni)
{
	int encap_size = VXLAN_HLEN + sizeof(struct iphdr) + ETH_HLEN;
	struct ethhdr *eth = (struct ethhdr *)buf;
	struct iphdr  *ip = (struct iphdr *)((char *)eth + sizeof(struct ethhdr));
	struct udphdr *udp = (struct udphdr *)((char *)ip + sizeof(struct iphdr));
	struct vxlanhdr *vxh = (struct vxlanhdr *)((char *)udp + sizeof(struct udphdr));

	memset(buf, 0, encap_size);

	ether_addr_copy(eth->h_dest, h_dest);
	ether_addr_copy(eth->h_source, out_dev->dev_addr);
	eth->h_proto = htons(ETH_P_IP);

	ip->daddr = daddr;
	ip->saddr = saddr;

	ip->ttl = ttl;
	ip->protocol = IPPROTO_UDP;
	ip->version = 0x4;
	ip->ihl = 0x5;

	udp->dest = udp_dst_port;
	vxh->vx_flags = VXLAN_HF_VNI;
	vxh->vx_vni = vxlan_vni_field(vx_vni);

	return encap_size;
}

static int mlx5e_create_encap_header_ipv4(struct mlx5e_priv *priv,
					  struct net_device *mirred_dev,
					  struct mlx5_encap_entry *e,
					  struct net_device **out_dev)
{
	int max_encap_size = MLX5_CAP_ESW(priv->mdev, max_encap_header_size);
	struct flowi4 fl4 = {};
	struct neighbour *n;
	char *encap_header;
	int encap_size;
	__be32 saddr;
	int ttl;
	int err;

	encap_header = kzalloc(max_encap_size, GFP_KERNEL);
	if (!encap_header)
		return -ENOMEM;

	switch (e->tunnel_type) {
	case MLX5_HEADER_TYPE_VXLAN:
		fl4.flowi4_proto = IPPROTO_UDP;
		fl4.fl4_dport = e->tun_info.tp_dst;
		break;
	default:
		err = -EOPNOTSUPP;
		goto out;
	}
	fl4.daddr = e->tun_info.daddr;

	err = mlx5e_route_lookup_ipv4(priv, mirred_dev, out_dev,
				      &fl4, &n, &saddr, &ttl);
	if (err)
		goto out;

	e->n = n;
	e->out_dev = *out_dev;

	if (!(n->nud_state & NUD_VALID)) {
		err = -ENOTSUPP;
		goto out;
	}

	neigh_ha_snapshot(e->h_dest, n, *out_dev);

	switch (e->tunnel_type) {
	case MLX5_HEADER_TYPE_VXLAN:
		encap_size = gen_vxlan_header_ipv4(*out_dev, encap_header,
						   e->h_dest, ttl,
						   e->tun_info.daddr,
						   saddr, e->tun_info.tp_dst,
						   e->tun_info.tun_id);
		break;
	default:
		err = -EOPNOTSUPP;
		goto out;
	}

	err = mlx5_encap_alloc(priv->mdev, e->tunnel_type,
			       encap_size, encap_header, &e->encap_id);
out:
	kfree(encap_header);
	return err;
}

static int mlx5e_attach_encap(struct mlx5e_priv *priv,
			      struct ip_tunnel_info *tun_info,
			      struct net_device *mirred_dev,
			      struct mlx5_esw_flow_attr *attr)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	unsigned short family = ip_tunnel_info_af(tun_info);
	struct ip_tunnel_key *key = &tun_info->key;
	struct mlx5_encap_info info;
	struct mlx5_encap_entry *e;
	struct net_device *out_dev;
	uintptr_t hash_key;
	bool found = false;
	int tunnel_type;
	int err;

	/* udp dst port must be given */
	if (!memchr_inv(&key->tp_dst, 0, sizeof(key->tp_dst)))
		return -EOPNOTSUPP;

	if (mlx5e_vxlan_lookup_port(priv, be16_to_cpu(key->tp_dst)) &&
	    MLX5_CAP_ESW(priv->mdev, vxlan_encap_decap)) {
		info.tp_dst = key->tp_dst;
		info.tun_id = tunnel_id_to_key32(key->tun_id);
		tunnel_type = MLX5_HEADER_TYPE_VXLAN;
	} else {
		return -EOPNOTSUPP;
	}

	switch (family) {
	case AF_INET:
		info.daddr = key->u.ipv4.dst;
		break;
	default:
		return -EOPNOTSUPP;
	}

	hash_key = hash_encap_info(&info);

	hash_for_each_possible_rcu(esw->offloads.encap_tbl, e,
				   encap_hlist, hash_key) {
		if (!cmp_encap_info(&e->tun_info, &info)) {
			found = true;
			break;
		}
	}

	if (found) {
		attr->encap = e;
		return 0;
	}

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->tun_info = info;
	e->tunnel_type = tunnel_type;
	INIT_LIST_HEAD(&e->flows);

	err = mlx5e_create_encap_header_ipv4(priv, mirred_dev, e, &out_dev);
	if (err)
		goto out_err;

	attr->encap = e;
	hash_add_rcu(esw->offloads.encap_tbl, &e->encap_hlist, hash_key);

	return err;

out_err:
	kfree(e);
	return err;
}

static int parse_tc_fdb_actions(struct mlx5e_priv *priv, struct tcf_exts *exts,
				struct mlx5e_tc_flow *flow)
{
	struct mlx5_esw_flow_attr *attr = flow->attr;
	struct ip_tunnel_info *info = NULL;
	const struct tc_action *a;
	LIST_HEAD(actions);
	bool encap = false;
	int err;

	if (tc_no_actions(exts))
		return -EINVAL;

	memset(attr, 0, sizeof(*attr));
	attr->in_rep = priv->ppriv;

	tcf_exts_to_list(exts, &actions);
	list_for_each_entry(a, &actions, list) {
		if (is_tcf_gact_shot(a)) {
			attr->action |= MLX5_FLOW_CONTEXT_ACTION_DROP |
					MLX5_FLOW_CONTEXT_ACTION_COUNT;
			continue;
		}

		if (is_tcf_mirred_egress_redirect(a)) {
			int ifindex = tcf_mirred_ifindex(a);
			struct net_device *out_dev;
			struct mlx5e_priv *out_priv;

			out_dev = __dev_get_by_index(dev_net(priv->netdev), ifindex);

			if (switchdev_port_same_parent_id(priv->netdev,
							  out_dev)) {
				attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
					MLX5_FLOW_CONTEXT_ACTION_COUNT;
				out_priv = netdev_priv(out_dev);
				attr->out_rep = out_priv->ppriv;
			} else if (encap) {
				err = mlx5e_attach_encap(priv, info,
							 out_dev, attr);
				if (err)
					return err;
				list_add(&flow->encap, &attr->encap->flows);
				attr->action |= MLX5_FLOW_CONTEXT_ACTION_ENCAP |
					MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
					MLX5_FLOW_CONTEXT_ACTION_COUNT;
				out_priv = netdev_priv(attr->encap->out_dev);
				attr->out_rep = out_priv->ppriv;
			} else {
				pr_err("devices %s %s not on same switch HW, can't offload forwarding\n",
				       priv->netdev->name, out_dev->name);
				return -EINVAL;
			}
			continue;
		}

		if (is_tcf_tunnel_set(a)) {
			info = tcf_tunnel_info(a);
			if (info)
				encap = true;
			else
				return -EOPNOTSUPP;
			continue;
		}

		if (is_tcf_vlan(a)) {
			if (tcf_vlan_action(a) == VLAN_F_POP) {
				attr->action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_POP;
			} else if (tcf_vlan_action(a) == VLAN_F_PUSH) {
				if (tcf_vlan_push_proto(a) != htons(ETH_P_8021Q))
					return -EOPNOTSUPP;

				attr->action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH;
				attr->vlan = tcf_vlan_push_vid(a);
			}
			continue;
		}

		if (is_tcf_tunnel_release(a)) {
			attr->action |= MLX5_FLOW_CONTEXT_ACTION_DECAP;
			continue;
		}

		return -EINVAL;
	}
	return 0;
}

int mlx5e_configure_flower(struct mlx5e_priv *priv, __be16 protocol,
			   struct tc_cls_flower_offload *f)
{
	struct mlx5e_tc_table *tc = &priv->fs.tc;
	int err = 0;
	bool fdb_flow = false;
	u32 flow_tag, action;
	struct mlx5e_tc_flow *flow;
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_handle *old = NULL;
	struct mlx5_esw_flow_attr *old_attr = NULL;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	if (esw && esw->mode == SRIOV_OFFLOADS)
		fdb_flow = true;

	flow = rhashtable_lookup_fast(&tc->ht, &f->cookie,
				      tc->ht_params);
	if (flow) {
		old = flow->rule;
		old_attr = flow->attr;
	} else {
		if (fdb_flow)
			flow = kzalloc(sizeof(*flow) + sizeof(struct mlx5_esw_flow_attr),
				       GFP_KERNEL);
		else
			flow = kzalloc(sizeof(*flow), GFP_KERNEL);
	}

	spec = mlx5_vzalloc(sizeof(*spec));
	if (!spec || !flow) {
		err = -ENOMEM;
		goto err_free;
	}

	flow->cookie = f->cookie;

	err = parse_cls_flower(priv, spec, f);
	if (err < 0)
		goto err_free;

	if (fdb_flow) {
		flow->attr  = (struct mlx5_esw_flow_attr *)(flow + 1);
		err = parse_tc_fdb_actions(priv, f->exts, flow);
		if (err < 0)
			goto err_free;
		flow->rule = mlx5e_tc_add_fdb_flow(priv, spec, flow->attr);
	} else {
		err = parse_tc_nic_actions(priv, f->exts, &action, &flow_tag);
		if (err < 0)
			goto err_free;
		flow->rule = mlx5e_tc_add_nic_flow(priv, spec, action, flow_tag);
	}

	if (IS_ERR(flow->rule)) {
		err = PTR_ERR(flow->rule);
		goto err_free;
	}

	err = rhashtable_insert_fast(&tc->ht, &flow->node,
				     tc->ht_params);
	if (err)
		goto err_del_rule;

	if (old)
		mlx5e_tc_del_flow(priv, old, old_attr);

	goto out;

err_del_rule:
	mlx5_del_flow_rules(flow->rule);

err_free:
	if (!old)
		kfree(flow);
out:
	kvfree(spec);
	return err;
}

static void mlx5e_detach_encap(struct mlx5e_priv *priv,
			       struct mlx5e_tc_flow *flow) {
	struct list_head *next = flow->encap.next;

	list_del(&flow->encap);
	if (list_empty(next)) {
		struct mlx5_encap_entry *e;

		e = list_entry(next, struct mlx5_encap_entry, flows);
		if (e->n) {
			mlx5_encap_dealloc(priv->mdev, e->encap_id);
			neigh_release(e->n);
		}
		hlist_del_rcu(&e->encap_hlist);
		kfree(e);
	}
}

int mlx5e_delete_flower(struct mlx5e_priv *priv,
			struct tc_cls_flower_offload *f)
{
	struct mlx5e_tc_flow *flow;
	struct mlx5e_tc_table *tc = &priv->fs.tc;

	flow = rhashtable_lookup_fast(&tc->ht, &f->cookie,
				      tc->ht_params);
	if (!flow)
		return -EINVAL;

	rhashtable_remove_fast(&tc->ht, &flow->node, tc->ht_params);

	mlx5e_tc_del_flow(priv, flow->rule, flow->attr);

	if (flow->attr->action & MLX5_FLOW_CONTEXT_ACTION_ENCAP)
		mlx5e_detach_encap(priv, flow);

	kfree(flow);

	return 0;
}

int mlx5e_stats_flower(struct mlx5e_priv *priv,
		       struct tc_cls_flower_offload *f)
{
	struct mlx5e_tc_table *tc = &priv->fs.tc;
	struct mlx5e_tc_flow *flow;
	struct tc_action *a;
	struct mlx5_fc *counter;
	LIST_HEAD(actions);
	u64 bytes;
	u64 packets;
	u64 lastuse;

	flow = rhashtable_lookup_fast(&tc->ht, &f->cookie,
				      tc->ht_params);
	if (!flow)
		return -EINVAL;

	counter = mlx5_flow_rule_counter(flow->rule);
	if (!counter)
		return 0;

	mlx5_fc_query_cached(counter, &bytes, &packets, &lastuse);

	tcf_exts_to_list(f->exts, &actions);
	list_for_each_entry(a, &actions, list)
		tcf_action_stats_update(a, bytes, packets, lastuse);

	return 0;
}

static const struct rhashtable_params mlx5e_tc_flow_ht_params = {
	.head_offset = offsetof(struct mlx5e_tc_flow, node),
	.key_offset = offsetof(struct mlx5e_tc_flow, cookie),
	.key_len = sizeof(((struct mlx5e_tc_flow *)0)->cookie),
	.automatic_shrinking = true,
};

int mlx5e_tc_init(struct mlx5e_priv *priv)
{
	struct mlx5e_tc_table *tc = &priv->fs.tc;

	tc->ht_params = mlx5e_tc_flow_ht_params;
	return rhashtable_init(&tc->ht, &tc->ht_params);
}

static void _mlx5e_tc_del_flow(void *ptr, void *arg)
{
	struct mlx5e_tc_flow *flow = ptr;
	struct mlx5e_priv *priv = arg;

	mlx5e_tc_del_flow(priv, flow->rule, flow->attr);
	kfree(flow);
}

void mlx5e_tc_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_tc_table *tc = &priv->fs.tc;

	rhashtable_free_and_destroy(&tc->ht, _mlx5e_tc_del_flow, priv);

	if (!IS_ERR_OR_NULL(tc->t)) {
		mlx5_destroy_flow_table(tc->t);
		tc->t = NULL;
	}
}
