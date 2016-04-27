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
#include "en.h"
#include "en_tc.h"

struct mlx5e_tc_flow {
	struct rhash_head	node;
	u64			cookie;
	struct mlx5_flow_rule	*rule;
};

#define MLX5E_TC_FLOW_TABLE_NUM_ENTRIES 1024
#define MLX5E_TC_FLOW_TABLE_NUM_GROUPS 4

static struct mlx5_flow_rule *mlx5e_tc_add_flow(struct mlx5e_priv *priv,
						u32 *match_c, u32 *match_v,
						u32 action, u32 flow_tag)
{
	struct mlx5_flow_destination dest = {
		.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE,
		{.ft = priv->fts.vlan.t},
	};
	struct mlx5_flow_rule *rule;
	bool table_created = false;

	if (IS_ERR_OR_NULL(priv->fts.tc.t)) {
		priv->fts.tc.t =
			mlx5_create_auto_grouped_flow_table(priv->fts.ns, 0,
							    MLX5E_TC_FLOW_TABLE_NUM_ENTRIES,
							    MLX5E_TC_FLOW_TABLE_NUM_GROUPS);
		if (IS_ERR(priv->fts.tc.t)) {
			netdev_err(priv->netdev,
				   "Failed to create tc offload table\n");
			return ERR_CAST(priv->fts.tc.t);
		}

		table_created = true;
	}

	rule = mlx5_add_flow_rule(priv->fts.tc.t, MLX5_MATCH_OUTER_HEADERS,
				  match_c, match_v,
				  action, flow_tag,
				  action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST ? &dest : NULL);

	if (IS_ERR(rule) && table_created) {
		mlx5_destroy_flow_table(priv->fts.tc.t);
		priv->fts.tc.t = NULL;
	}

	return rule;
}

static void mlx5e_tc_del_flow(struct mlx5e_priv *priv,
			      struct mlx5_flow_rule *rule)
{
	mlx5_del_flow_rule(rule);

	if (!mlx5e_tc_num_filters(priv)) {
		mlx5_destroy_flow_table(priv->fts.tc.t);
		priv->fts.tc.t = NULL;
	}
}

static int parse_cls_flower(struct mlx5e_priv *priv,
			    u32 *match_c, u32 *match_v,
			    struct tc_cls_flower_offload *f)
{
	void *headers_c = MLX5_ADDR_OF(fte_match_param, match_c, outer_headers);
	void *headers_v = MLX5_ADDR_OF(fte_match_param, match_v, outer_headers);
	u16 addr_type = 0;
	u8 ip_proto = 0;

	if (f->dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS))) {
		netdev_warn(priv->netdev, "Unsupported key used: 0x%x\n",
			    f->dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
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

static int parse_tc_actions(struct mlx5e_priv *priv, struct tcf_exts *exts,
			    u32 *action, u32 *flow_tag)
{
	const struct tc_action *a;

	if (tc_no_actions(exts))
		return -EINVAL;

	*flow_tag = MLX5_FS_DEFAULT_FLOW_TAG;
	*action = 0;

	tc_for_each_action(a, exts) {
		/* Only support a single action per rule */
		if (*action)
			return -EINVAL;

		if (is_tcf_gact_shot(a)) {
			*action |= MLX5_FLOW_CONTEXT_ACTION_DROP;
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

int mlx5e_configure_flower(struct mlx5e_priv *priv, __be16 protocol,
			   struct tc_cls_flower_offload *f)
{
	struct mlx5e_tc_flow_table *tc = &priv->fts.tc;
	u32 *match_c;
	u32 *match_v;
	int err = 0;
	u32 flow_tag;
	u32 action;
	struct mlx5e_tc_flow *flow;
	struct mlx5_flow_rule *old = NULL;

	flow = rhashtable_lookup_fast(&tc->ht, &f->cookie,
				      tc->ht_params);
	if (flow)
		old = flow->rule;
	else
		flow = kzalloc(sizeof(*flow), GFP_KERNEL);

	match_c = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	match_v = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param), GFP_KERNEL);
	if (!match_c || !match_v || !flow) {
		err = -ENOMEM;
		goto err_free;
	}

	flow->cookie = f->cookie;

	err = parse_cls_flower(priv, match_c, match_v, f);
	if (err < 0)
		goto err_free;

	err = parse_tc_actions(priv, f->exts, &action, &flow_tag);
	if (err < 0)
		goto err_free;

	err = rhashtable_insert_fast(&tc->ht, &flow->node,
				     tc->ht_params);
	if (err)
		goto err_free;

	flow->rule = mlx5e_tc_add_flow(priv, match_c, match_v, action,
				       flow_tag);
	if (IS_ERR(flow->rule)) {
		err = PTR_ERR(flow->rule);
		goto err_hash_del;
	}

	if (old)
		mlx5e_tc_del_flow(priv, old);

	goto out;

err_hash_del:
	rhashtable_remove_fast(&tc->ht, &flow->node, tc->ht_params);

err_free:
	if (!old)
		kfree(flow);
out:
	kfree(match_c);
	kfree(match_v);
	return err;
}

int mlx5e_delete_flower(struct mlx5e_priv *priv,
			struct tc_cls_flower_offload *f)
{
	struct mlx5e_tc_flow *flow;
	struct mlx5e_tc_flow_table *tc = &priv->fts.tc;

	flow = rhashtable_lookup_fast(&tc->ht, &f->cookie,
				      tc->ht_params);
	if (!flow)
		return -EINVAL;

	rhashtable_remove_fast(&tc->ht, &flow->node, tc->ht_params);

	mlx5e_tc_del_flow(priv, flow->rule);

	kfree(flow);

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
	struct mlx5e_tc_flow_table *tc = &priv->fts.tc;

	tc->ht_params = mlx5e_tc_flow_ht_params;
	return rhashtable_init(&tc->ht, &tc->ht_params);
}

static void _mlx5e_tc_del_flow(void *ptr, void *arg)
{
	struct mlx5e_tc_flow *flow = ptr;
	struct mlx5e_priv *priv = arg;

	mlx5e_tc_del_flow(priv, flow->rule);
	kfree(flow);
}

void mlx5e_tc_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_tc_flow_table *tc = &priv->fts.tc;

	rhashtable_free_and_destroy(&tc->ht, _mlx5e_tc_del_flow, priv);

	if (!IS_ERR_OR_NULL(priv->fts.tc.t)) {
		mlx5_destroy_flow_table(priv->fts.tc.t);
		priv->fts.tc.t = NULL;
	}
}
