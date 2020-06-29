// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_labels.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <uapi/linux/tc_act/tc_pedit.h>
#include <net/tc_act/tc_ct.h>
#include <net/flow_offload.h>
#include <net/netfilter/nf_flow_table.h>
#include <linux/workqueue.h>
#include <linux/xarray.h>

#include "esw/chains.h"
#include "en/tc_ct.h"
#include "en.h"
#include "en_tc.h"
#include "en_rep.h"

#define MLX5_CT_ZONE_BITS (mlx5e_tc_attr_to_reg_mappings[ZONE_TO_REG].mlen * 8)
#define MLX5_CT_ZONE_MASK GENMASK(MLX5_CT_ZONE_BITS - 1, 0)
#define MLX5_CT_STATE_ESTABLISHED_BIT BIT(1)
#define MLX5_CT_STATE_TRK_BIT BIT(2)
#define MLX5_CT_STATE_NAT_BIT BIT(3)

#define MLX5_FTE_ID_BITS (mlx5e_tc_attr_to_reg_mappings[FTEID_TO_REG].mlen * 8)
#define MLX5_FTE_ID_MAX GENMASK(MLX5_FTE_ID_BITS - 1, 0)
#define MLX5_FTE_ID_MASK MLX5_FTE_ID_MAX

#define ct_dbg(fmt, args...)\
	netdev_dbg(ct_priv->netdev, "ct_debug: " fmt "\n", ##args)

struct mlx5_tc_ct_priv {
	struct mlx5_eswitch *esw;
	const struct net_device *netdev;
	struct idr fte_ids;
	struct xarray tuple_ids;
	struct rhashtable zone_ht;
	struct mlx5_flow_table *ct;
	struct mlx5_flow_table *ct_nat;
	struct mlx5_flow_table *post_ct;
	struct mutex control_lock; /* guards parallel adds/dels */
};

struct mlx5_ct_flow {
	struct mlx5_esw_flow_attr pre_ct_attr;
	struct mlx5_esw_flow_attr post_ct_attr;
	struct mlx5_flow_handle *pre_ct_rule;
	struct mlx5_flow_handle *post_ct_rule;
	struct mlx5_ct_ft *ft;
	u32 fte_id;
	u32 chain_mapping;
};

struct mlx5_ct_zone_rule {
	struct mlx5_flow_handle *rule;
	struct mlx5_esw_flow_attr attr;
	int tupleid;
	bool nat;
};

struct mlx5_tc_ct_pre {
	struct mlx5_flow_table *fdb;
	struct mlx5_flow_group *flow_grp;
	struct mlx5_flow_group *miss_grp;
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_flow_handle *miss_rule;
	struct mlx5_modify_hdr *modify_hdr;
};

struct mlx5_ct_ft {
	struct rhash_head node;
	u16 zone;
	refcount_t refcount;
	struct nf_flowtable *nf_ft;
	struct mlx5_tc_ct_priv *ct_priv;
	struct rhashtable ct_entries_ht;
	struct mlx5_tc_ct_pre pre_ct;
	struct mlx5_tc_ct_pre pre_ct_nat;
};

struct mlx5_ct_entry {
	u16 zone;
	struct rhash_head node;
	struct mlx5_fc *counter;
	unsigned long cookie;
	unsigned long restore_cookie;
	struct mlx5_ct_zone_rule zone_rules[2];
};

static const struct rhashtable_params cts_ht_params = {
	.head_offset = offsetof(struct mlx5_ct_entry, node),
	.key_offset = offsetof(struct mlx5_ct_entry, cookie),
	.key_len = sizeof(((struct mlx5_ct_entry *)0)->cookie),
	.automatic_shrinking = true,
	.min_size = 16 * 1024,
};

static const struct rhashtable_params zone_params = {
	.head_offset = offsetof(struct mlx5_ct_ft, node),
	.key_offset = offsetof(struct mlx5_ct_ft, zone),
	.key_len = sizeof(((struct mlx5_ct_ft *)0)->zone),
	.automatic_shrinking = true,
};

static struct mlx5_tc_ct_priv *
mlx5_tc_ct_get_ct_priv(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *uplink_rpriv;

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &uplink_rpriv->uplink_priv;
	return uplink_priv->ct_priv;
}

static int
mlx5_tc_ct_set_tuple_match(struct mlx5e_priv *priv, struct mlx5_flow_spec *spec,
			   struct flow_rule *rule)
{
	void *headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				       outer_headers);
	void *headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				       outer_headers);
	u16 addr_type = 0;
	u8 ip_proto = 0;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);

		mlx5e_tc_set_ethertype(priv->mdev, &match, true, headers_c,
				       headers_v);
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_protocol,
			 match.mask->ip_proto);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol,
			 match.key->ip_proto);

		ip_proto = match.key->ip_proto;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		addr_type = match.key->addr_type;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &match.mask->src, sizeof(match.mask->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &match.key->src, sizeof(match.key->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &match.mask->dst, sizeof(match.mask->dst));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &match.key->dst, sizeof(match.key->dst));
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &match.mask->src, sizeof(match.mask->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &match.key->src, sizeof(match.key->src));

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &match.mask->dst, sizeof(match.mask->dst));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &match.key->dst, sizeof(match.key->dst));
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		switch (ip_proto) {
		case IPPROTO_TCP:
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 tcp_sport, ntohs(match.mask->src));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 tcp_sport, ntohs(match.key->src));

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 tcp_dport, ntohs(match.mask->dst));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 tcp_dport, ntohs(match.key->dst));
			break;

		case IPPROTO_UDP:
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 udp_sport, ntohs(match.mask->src));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 udp_sport, ntohs(match.key->src));

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 udp_dport, ntohs(match.mask->dst));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 udp_dport, ntohs(match.key->dst));
			break;
		default:
			break;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_match_tcp match;

		flow_rule_match_tcp(rule, &match);
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, tcp_flags,
			 ntohs(match.mask->flags));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, tcp_flags,
			 ntohs(match.key->flags));
	}

	return 0;
}

static void
mlx5_tc_ct_entry_del_rule(struct mlx5_tc_ct_priv *ct_priv,
			  struct mlx5_ct_entry *entry,
			  bool nat)
{
	struct mlx5_ct_zone_rule *zone_rule = &entry->zone_rules[nat];
	struct mlx5_esw_flow_attr *attr = &zone_rule->attr;
	struct mlx5_eswitch *esw = ct_priv->esw;

	ct_dbg("Deleting ct entry rule in zone %d", entry->zone);

	mlx5_eswitch_del_offloaded_rule(esw, zone_rule->rule, attr);
	mlx5_modify_header_dealloc(esw->dev, attr->modify_hdr);
	xa_erase(&ct_priv->tuple_ids, zone_rule->tupleid);
}

static void
mlx5_tc_ct_entry_del_rules(struct mlx5_tc_ct_priv *ct_priv,
			   struct mlx5_ct_entry *entry)
{
	mlx5_tc_ct_entry_del_rule(ct_priv, entry, true);
	mlx5_tc_ct_entry_del_rule(ct_priv, entry, false);

	mlx5_fc_destroy(ct_priv->esw->dev, entry->counter);
}

static struct flow_action_entry *
mlx5_tc_ct_get_ct_metadata_action(struct flow_rule *flow_rule)
{
	struct flow_action *flow_action = &flow_rule->action;
	struct flow_action_entry *act;
	int i;

	flow_action_for_each(i, act, flow_action) {
		if (act->id == FLOW_ACTION_CT_METADATA)
			return act;
	}

	return NULL;
}

static int
mlx5_tc_ct_entry_set_registers(struct mlx5_tc_ct_priv *ct_priv,
			       struct mlx5e_tc_mod_hdr_acts *mod_acts,
			       u8 ct_state,
			       u32 mark,
			       u32 label,
			       u32 tupleid)
{
	struct mlx5_eswitch *esw = ct_priv->esw;
	int err;

	err = mlx5e_tc_match_to_reg_set(esw->dev, mod_acts,
					CTSTATE_TO_REG, ct_state);
	if (err)
		return err;

	err = mlx5e_tc_match_to_reg_set(esw->dev, mod_acts,
					MARK_TO_REG, mark);
	if (err)
		return err;

	err = mlx5e_tc_match_to_reg_set(esw->dev, mod_acts,
					LABELS_TO_REG, label);
	if (err)
		return err;

	err = mlx5e_tc_match_to_reg_set(esw->dev, mod_acts,
					TUPLEID_TO_REG, tupleid);
	if (err)
		return err;

	return 0;
}

static int
mlx5_tc_ct_parse_mangle_to_mod_act(struct flow_action_entry *act,
				   char *modact)
{
	u32 offset = act->mangle.offset, field;

	switch (act->mangle.htype) {
	case FLOW_ACT_MANGLE_HDR_TYPE_IP4:
		MLX5_SET(set_action_in, modact, length, 0);
		if (offset == offsetof(struct iphdr, saddr))
			field = MLX5_ACTION_IN_FIELD_OUT_SIPV4;
		else if (offset == offsetof(struct iphdr, daddr))
			field = MLX5_ACTION_IN_FIELD_OUT_DIPV4;
		else
			return -EOPNOTSUPP;
		break;

	case FLOW_ACT_MANGLE_HDR_TYPE_IP6:
		MLX5_SET(set_action_in, modact, length, 0);
		if (offset == offsetof(struct ipv6hdr, saddr) + 12)
			field = MLX5_ACTION_IN_FIELD_OUT_SIPV6_31_0;
		else if (offset == offsetof(struct ipv6hdr, saddr) + 8)
			field = MLX5_ACTION_IN_FIELD_OUT_SIPV6_63_32;
		else if (offset == offsetof(struct ipv6hdr, saddr) + 4)
			field = MLX5_ACTION_IN_FIELD_OUT_SIPV6_95_64;
		else if (offset == offsetof(struct ipv6hdr, saddr))
			field = MLX5_ACTION_IN_FIELD_OUT_SIPV6_127_96;
		else if (offset == offsetof(struct ipv6hdr, daddr) + 12)
			field = MLX5_ACTION_IN_FIELD_OUT_DIPV6_31_0;
		else if (offset == offsetof(struct ipv6hdr, daddr) + 8)
			field = MLX5_ACTION_IN_FIELD_OUT_DIPV6_63_32;
		else if (offset == offsetof(struct ipv6hdr, daddr) + 4)
			field = MLX5_ACTION_IN_FIELD_OUT_DIPV6_95_64;
		else if (offset == offsetof(struct ipv6hdr, daddr))
			field = MLX5_ACTION_IN_FIELD_OUT_DIPV6_127_96;
		else
			return -EOPNOTSUPP;
		break;

	case FLOW_ACT_MANGLE_HDR_TYPE_TCP:
		MLX5_SET(set_action_in, modact, length, 16);
		if (offset == offsetof(struct tcphdr, source))
			field = MLX5_ACTION_IN_FIELD_OUT_TCP_SPORT;
		else if (offset == offsetof(struct tcphdr, dest))
			field = MLX5_ACTION_IN_FIELD_OUT_TCP_DPORT;
		else
			return -EOPNOTSUPP;
		break;

	case FLOW_ACT_MANGLE_HDR_TYPE_UDP:
		MLX5_SET(set_action_in, modact, length, 16);
		if (offset == offsetof(struct udphdr, source))
			field = MLX5_ACTION_IN_FIELD_OUT_UDP_SPORT;
		else if (offset == offsetof(struct udphdr, dest))
			field = MLX5_ACTION_IN_FIELD_OUT_UDP_DPORT;
		else
			return -EOPNOTSUPP;
		break;

	default:
		return -EOPNOTSUPP;
	}

	MLX5_SET(set_action_in, modact, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, modact, offset, 0);
	MLX5_SET(set_action_in, modact, field, field);
	MLX5_SET(set_action_in, modact, data, act->mangle.val);

	return 0;
}

static int
mlx5_tc_ct_entry_create_nat(struct mlx5_tc_ct_priv *ct_priv,
			    struct flow_rule *flow_rule,
			    struct mlx5e_tc_mod_hdr_acts *mod_acts)
{
	struct flow_action *flow_action = &flow_rule->action;
	struct mlx5_core_dev *mdev = ct_priv->esw->dev;
	struct flow_action_entry *act;
	size_t action_size;
	char *modact;
	int err, i;

	action_size = MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto);

	flow_action_for_each(i, act, flow_action) {
		switch (act->id) {
		case FLOW_ACTION_MANGLE: {
			err = alloc_mod_hdr_actions(mdev,
						    MLX5_FLOW_NAMESPACE_FDB,
						    mod_acts);
			if (err)
				return err;

			modact = mod_acts->actions +
				 mod_acts->num_actions * action_size;

			err = mlx5_tc_ct_parse_mangle_to_mod_act(act, modact);
			if (err)
				return err;

			mod_acts->num_actions++;
		}
		break;

		case FLOW_ACTION_CT_METADATA:
			/* Handled earlier */
			continue;
		default:
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int
mlx5_tc_ct_entry_create_mod_hdr(struct mlx5_tc_ct_priv *ct_priv,
				struct mlx5_esw_flow_attr *attr,
				struct flow_rule *flow_rule,
				u32 tupleid,
				bool nat)
{
	struct mlx5e_tc_mod_hdr_acts mod_acts = {};
	struct mlx5_eswitch *esw = ct_priv->esw;
	struct mlx5_modify_hdr *mod_hdr;
	struct flow_action_entry *meta;
	u16 ct_state = 0;
	int err;

	meta = mlx5_tc_ct_get_ct_metadata_action(flow_rule);
	if (!meta)
		return -EOPNOTSUPP;

	if (meta->ct_metadata.labels[1] ||
	    meta->ct_metadata.labels[2] ||
	    meta->ct_metadata.labels[3]) {
		ct_dbg("Failed to offload ct entry due to unsupported label");
		return -EOPNOTSUPP;
	}

	if (nat) {
		err = mlx5_tc_ct_entry_create_nat(ct_priv, flow_rule,
						  &mod_acts);
		if (err)
			goto err_mapping;

		ct_state |= MLX5_CT_STATE_NAT_BIT;
	}

	ct_state |= MLX5_CT_STATE_ESTABLISHED_BIT | MLX5_CT_STATE_TRK_BIT;
	err = mlx5_tc_ct_entry_set_registers(ct_priv, &mod_acts,
					     ct_state,
					     meta->ct_metadata.mark,
					     meta->ct_metadata.labels[0],
					     tupleid);
	if (err)
		goto err_mapping;

	mod_hdr = mlx5_modify_header_alloc(esw->dev, MLX5_FLOW_NAMESPACE_FDB,
					   mod_acts.num_actions,
					   mod_acts.actions);
	if (IS_ERR(mod_hdr)) {
		err = PTR_ERR(mod_hdr);
		goto err_mapping;
	}
	attr->modify_hdr = mod_hdr;

	dealloc_mod_hdr_actions(&mod_acts);
	return 0;

err_mapping:
	dealloc_mod_hdr_actions(&mod_acts);
	return err;
}

static int
mlx5_tc_ct_entry_add_rule(struct mlx5_tc_ct_priv *ct_priv,
			  struct flow_rule *flow_rule,
			  struct mlx5_ct_entry *entry,
			  bool nat)
{
	struct mlx5_ct_zone_rule *zone_rule = &entry->zone_rules[nat];
	struct mlx5_esw_flow_attr *attr = &zone_rule->attr;
	struct mlx5_eswitch *esw = ct_priv->esw;
	struct mlx5_flow_spec *spec = NULL;
	u32 tupleid;
	int err;

	zone_rule->nat = nat;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	/* Get tuple unique id */
	err = xa_alloc(&ct_priv->tuple_ids, &tupleid, zone_rule,
		       XA_LIMIT(1, TUPLE_ID_MAX), GFP_KERNEL);
	if (err) {
		netdev_warn(ct_priv->netdev,
			    "Failed to allocate tuple id, err: %d\n", err);
		goto err_xa_alloc;
	}
	zone_rule->tupleid = tupleid;

	err = mlx5_tc_ct_entry_create_mod_hdr(ct_priv, attr, flow_rule,
					      tupleid, nat);
	if (err) {
		ct_dbg("Failed to create ct entry mod hdr");
		goto err_mod_hdr;
	}

	attr->action = MLX5_FLOW_CONTEXT_ACTION_MOD_HDR |
		       MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
		       MLX5_FLOW_CONTEXT_ACTION_COUNT;
	attr->dest_chain = 0;
	attr->dest_ft = ct_priv->post_ct;
	attr->fdb = nat ? ct_priv->ct_nat : ct_priv->ct;
	attr->outer_match_level = MLX5_MATCH_L4;
	attr->counter = entry->counter;
	attr->flags |= MLX5_ESW_ATTR_FLAG_NO_IN_PORT;

	mlx5_tc_ct_set_tuple_match(netdev_priv(ct_priv->netdev), spec, flow_rule);
	mlx5e_tc_match_to_reg_match(spec, ZONE_TO_REG,
				    entry->zone & MLX5_CT_ZONE_MASK,
				    MLX5_CT_ZONE_MASK);

	zone_rule->rule = mlx5_eswitch_add_offloaded_rule(esw, spec, attr);
	if (IS_ERR(zone_rule->rule)) {
		err = PTR_ERR(zone_rule->rule);
		ct_dbg("Failed to add ct entry rule, nat: %d", nat);
		goto err_rule;
	}

	kfree(spec);
	ct_dbg("Offloaded ct entry rule in zone %d", entry->zone);

	return 0;

err_rule:
	mlx5_modify_header_dealloc(esw->dev, attr->modify_hdr);
err_mod_hdr:
	xa_erase(&ct_priv->tuple_ids, zone_rule->tupleid);
err_xa_alloc:
	kfree(spec);
	return err;
}

static int
mlx5_tc_ct_entry_add_rules(struct mlx5_tc_ct_priv *ct_priv,
			   struct flow_rule *flow_rule,
			   struct mlx5_ct_entry *entry)
{
	struct mlx5_eswitch *esw = ct_priv->esw;
	int err;

	entry->counter = mlx5_fc_create(esw->dev, true);
	if (IS_ERR(entry->counter)) {
		err = PTR_ERR(entry->counter);
		ct_dbg("Failed to create counter for ct entry");
		return err;
	}

	err = mlx5_tc_ct_entry_add_rule(ct_priv, flow_rule, entry, false);
	if (err)
		goto err_orig;

	err = mlx5_tc_ct_entry_add_rule(ct_priv, flow_rule, entry, true);
	if (err)
		goto err_nat;

	return 0;

err_nat:
	mlx5_tc_ct_entry_del_rule(ct_priv, entry, false);
err_orig:
	mlx5_fc_destroy(esw->dev, entry->counter);
	return err;
}

static int
mlx5_tc_ct_block_flow_offload_add(struct mlx5_ct_ft *ft,
				  struct flow_cls_offload *flow)
{
	struct flow_rule *flow_rule = flow_cls_offload_flow_rule(flow);
	struct mlx5_tc_ct_priv *ct_priv = ft->ct_priv;
	struct flow_action_entry *meta_action;
	unsigned long cookie = flow->cookie;
	struct mlx5_ct_entry *entry;
	int err;

	meta_action = mlx5_tc_ct_get_ct_metadata_action(flow_rule);
	if (!meta_action)
		return -EOPNOTSUPP;

	entry = rhashtable_lookup_fast(&ft->ct_entries_ht, &cookie,
				       cts_ht_params);
	if (entry)
		return 0;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->zone = ft->zone;
	entry->cookie = flow->cookie;
	entry->restore_cookie = meta_action->ct_metadata.cookie;

	err = mlx5_tc_ct_entry_add_rules(ct_priv, flow_rule, entry);
	if (err)
		goto err_rules;

	err = rhashtable_insert_fast(&ft->ct_entries_ht, &entry->node,
				     cts_ht_params);
	if (err)
		goto err_insert;

	return 0;

err_insert:
	mlx5_tc_ct_entry_del_rules(ct_priv, entry);
err_rules:
	kfree(entry);
	netdev_warn(ct_priv->netdev,
		    "Failed to offload ct entry, err: %d\n", err);
	return err;
}

static int
mlx5_tc_ct_block_flow_offload_del(struct mlx5_ct_ft *ft,
				  struct flow_cls_offload *flow)
{
	unsigned long cookie = flow->cookie;
	struct mlx5_ct_entry *entry;

	entry = rhashtable_lookup_fast(&ft->ct_entries_ht, &cookie,
				       cts_ht_params);
	if (!entry)
		return -ENOENT;

	mlx5_tc_ct_entry_del_rules(ft->ct_priv, entry);
	WARN_ON(rhashtable_remove_fast(&ft->ct_entries_ht,
				       &entry->node,
				       cts_ht_params));
	kfree(entry);

	return 0;
}

static int
mlx5_tc_ct_block_flow_offload_stats(struct mlx5_ct_ft *ft,
				    struct flow_cls_offload *f)
{
	unsigned long cookie = f->cookie;
	struct mlx5_ct_entry *entry;
	u64 lastuse, packets, bytes;

	entry = rhashtable_lookup_fast(&ft->ct_entries_ht, &cookie,
				       cts_ht_params);
	if (!entry)
		return -ENOENT;

	mlx5_fc_query_cached(entry->counter, &bytes, &packets, &lastuse);
	flow_stats_update(&f->stats, bytes, packets, lastuse,
			  FLOW_ACTION_HW_STATS_DELAYED);

	return 0;
}

static int
mlx5_tc_ct_block_flow_offload(enum tc_setup_type type, void *type_data,
			      void *cb_priv)
{
	struct flow_cls_offload *f = type_data;
	struct mlx5_ct_ft *ft = cb_priv;

	if (type != TC_SETUP_CLSFLOWER)
		return -EOPNOTSUPP;

	switch (f->command) {
	case FLOW_CLS_REPLACE:
		return mlx5_tc_ct_block_flow_offload_add(ft, f);
	case FLOW_CLS_DESTROY:
		return mlx5_tc_ct_block_flow_offload_del(ft, f);
	case FLOW_CLS_STATS:
		return mlx5_tc_ct_block_flow_offload_stats(ft, f);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

int
mlx5_tc_ct_parse_match(struct mlx5e_priv *priv,
		       struct mlx5_flow_spec *spec,
		       struct flow_cls_offload *f,
		       struct netlink_ext_ack *extack)
{
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_dissector_key_ct *mask, *key;
	bool trk, est, untrk, unest, new;
	u32 ctstate = 0, ctstate_mask = 0;
	u16 ct_state_on, ct_state_off;
	u16 ct_state, ct_state_mask;
	struct flow_match_ct match;

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CT))
		return 0;

	if (!ct_priv) {
		NL_SET_ERR_MSG_MOD(extack,
				   "offload of ct matching isn't available");
		return -EOPNOTSUPP;
	}

	flow_rule_match_ct(rule, &match);

	key = match.key;
	mask = match.mask;

	ct_state = key->ct_state;
	ct_state_mask = mask->ct_state;

	if (ct_state_mask & ~(TCA_FLOWER_KEY_CT_FLAGS_TRACKED |
			      TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED |
			      TCA_FLOWER_KEY_CT_FLAGS_NEW)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "only ct_state trk, est and new are supported for offload");
		return -EOPNOTSUPP;
	}

	if (mask->ct_labels[1] || mask->ct_labels[2] || mask->ct_labels[3]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "only lower 32bits of ct_labels are supported for offload");
		return -EOPNOTSUPP;
	}

	ct_state_on = ct_state & ct_state_mask;
	ct_state_off = (ct_state & ct_state_mask) ^ ct_state_mask;
	trk = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_TRACKED;
	new = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_NEW;
	est = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED;
	untrk = ct_state_off & TCA_FLOWER_KEY_CT_FLAGS_TRACKED;
	unest = ct_state_off & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED;

	ctstate |= trk ? MLX5_CT_STATE_TRK_BIT : 0;
	ctstate |= est ? MLX5_CT_STATE_ESTABLISHED_BIT : 0;
	ctstate_mask |= (untrk || trk) ? MLX5_CT_STATE_TRK_BIT : 0;
	ctstate_mask |= (unest || est) ? MLX5_CT_STATE_ESTABLISHED_BIT : 0;

	if (new) {
		NL_SET_ERR_MSG_MOD(extack,
				   "matching on ct_state +new isn't supported");
		return -EOPNOTSUPP;
	}

	if (mask->ct_zone)
		mlx5e_tc_match_to_reg_match(spec, ZONE_TO_REG,
					    key->ct_zone, MLX5_CT_ZONE_MASK);
	if (ctstate_mask)
		mlx5e_tc_match_to_reg_match(spec, CTSTATE_TO_REG,
					    ctstate, ctstate_mask);
	if (mask->ct_mark)
		mlx5e_tc_match_to_reg_match(spec, MARK_TO_REG,
					    key->ct_mark, mask->ct_mark);
	if (mask->ct_labels[0])
		mlx5e_tc_match_to_reg_match(spec, LABELS_TO_REG,
					    key->ct_labels[0],
					    mask->ct_labels[0]);

	return 0;
}

int
mlx5_tc_ct_parse_action(struct mlx5e_priv *priv,
			struct mlx5_esw_flow_attr *attr,
			const struct flow_action_entry *act,
			struct netlink_ext_ack *extack)
{
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);

	if (!ct_priv) {
		NL_SET_ERR_MSG_MOD(extack,
				   "offload of ct action isn't available");
		return -EOPNOTSUPP;
	}

	attr->ct_attr.zone = act->ct.zone;
	attr->ct_attr.ct_action = act->ct.action;
	attr->ct_attr.nf_ft = act->ct.flow_table;

	return 0;
}

static int tc_ct_pre_ct_add_rules(struct mlx5_ct_ft *ct_ft,
				  struct mlx5_tc_ct_pre *pre_ct,
				  bool nat)
{
	struct mlx5_tc_ct_priv *ct_priv = ct_ft->ct_priv;
	struct mlx5e_tc_mod_hdr_acts pre_mod_acts = {};
	struct mlx5_core_dev *dev = ct_priv->esw->dev;
	struct mlx5_flow_table *fdb = pre_ct->fdb;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_modify_hdr *mod_hdr;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	u32 ctstate;
	u16 zone;
	int err;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	zone = ct_ft->zone & MLX5_CT_ZONE_MASK;
	err = mlx5e_tc_match_to_reg_set(dev, &pre_mod_acts, ZONE_TO_REG, zone);
	if (err) {
		ct_dbg("Failed to set zone register mapping");
		goto err_mapping;
	}

	mod_hdr = mlx5_modify_header_alloc(dev,
					   MLX5_FLOW_NAMESPACE_FDB,
					   pre_mod_acts.num_actions,
					   pre_mod_acts.actions);

	if (IS_ERR(mod_hdr)) {
		err = PTR_ERR(mod_hdr);
		ct_dbg("Failed to create pre ct mod hdr");
		goto err_mapping;
	}
	pre_ct->modify_hdr = mod_hdr;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			  MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	flow_act.flags |= FLOW_ACT_IGNORE_FLOW_LEVEL;
	flow_act.modify_hdr = mod_hdr;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;

	/* add flow rule */
	mlx5e_tc_match_to_reg_match(spec, ZONE_TO_REG,
				    zone, MLX5_CT_ZONE_MASK);
	ctstate = MLX5_CT_STATE_TRK_BIT;
	if (nat)
		ctstate |= MLX5_CT_STATE_NAT_BIT;
	mlx5e_tc_match_to_reg_match(spec, CTSTATE_TO_REG, ctstate, ctstate);

	dest.ft = ct_priv->post_ct;
	rule = mlx5_add_flow_rules(fdb, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		ct_dbg("Failed to add pre ct flow rule zone %d", zone);
		goto err_flow_rule;
	}
	pre_ct->flow_rule = rule;

	/* add miss rule */
	memset(spec, 0, sizeof(*spec));
	dest.ft = nat ? ct_priv->ct_nat : ct_priv->ct;
	rule = mlx5_add_flow_rules(fdb, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		ct_dbg("Failed to add pre ct miss rule zone %d", zone);
		goto err_miss_rule;
	}
	pre_ct->miss_rule = rule;

	dealloc_mod_hdr_actions(&pre_mod_acts);
	kvfree(spec);
	return 0;

err_miss_rule:
	mlx5_del_flow_rules(pre_ct->flow_rule);
err_flow_rule:
	mlx5_modify_header_dealloc(dev, pre_ct->modify_hdr);
err_mapping:
	dealloc_mod_hdr_actions(&pre_mod_acts);
	kvfree(spec);
	return err;
}

static void
tc_ct_pre_ct_del_rules(struct mlx5_ct_ft *ct_ft,
		       struct mlx5_tc_ct_pre *pre_ct)
{
	struct mlx5_tc_ct_priv *ct_priv = ct_ft->ct_priv;
	struct mlx5_core_dev *dev = ct_priv->esw->dev;

	mlx5_del_flow_rules(pre_ct->flow_rule);
	mlx5_del_flow_rules(pre_ct->miss_rule);
	mlx5_modify_header_dealloc(dev, pre_ct->modify_hdr);
}

static int
mlx5_tc_ct_alloc_pre_ct(struct mlx5_ct_ft *ct_ft,
			struct mlx5_tc_ct_pre *pre_ct,
			bool nat)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_tc_ct_priv *ct_priv = ct_ft->ct_priv;
	struct mlx5_core_dev *dev = ct_priv->esw->dev;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *g;
	u32 metadata_reg_c_2_mask;
	u32 *flow_group_in;
	void *misc;
	int err;

	ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!ns) {
		err = -EOPNOTSUPP;
		ct_dbg("Failed to get FDB flow namespace");
		return err;
	}

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	ft_attr.flags = MLX5_FLOW_TABLE_UNMANAGED;
	ft_attr.prio = FDB_TC_OFFLOAD;
	ft_attr.max_fte = 2;
	ft_attr.level = 1;
	ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		ct_dbg("Failed to create pre ct table");
		goto out_free;
	}
	pre_ct->fdb = ft;

	/* create flow group */
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS_2);

	misc = MLX5_ADDR_OF(create_flow_group_in, flow_group_in,
			    match_criteria.misc_parameters_2);

	metadata_reg_c_2_mask = MLX5_CT_ZONE_MASK;
	metadata_reg_c_2_mask |= (MLX5_CT_STATE_TRK_BIT << 16);
	if (nat)
		metadata_reg_c_2_mask |= (MLX5_CT_STATE_NAT_BIT << 16);

	MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_2,
		 metadata_reg_c_2_mask);

	g = mlx5_create_flow_group(ft, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		ct_dbg("Failed to create pre ct group");
		goto err_flow_grp;
	}
	pre_ct->flow_grp = g;

	/* create miss group */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, 1);
	g = mlx5_create_flow_group(ft, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		ct_dbg("Failed to create pre ct miss group");
		goto err_miss_grp;
	}
	pre_ct->miss_grp = g;

	err = tc_ct_pre_ct_add_rules(ct_ft, pre_ct, nat);
	if (err)
		goto err_add_rules;

	kvfree(flow_group_in);
	return 0;

err_add_rules:
	mlx5_destroy_flow_group(pre_ct->miss_grp);
err_miss_grp:
	mlx5_destroy_flow_group(pre_ct->flow_grp);
err_flow_grp:
	mlx5_destroy_flow_table(ft);
out_free:
	kvfree(flow_group_in);
	return err;
}

static void
mlx5_tc_ct_free_pre_ct(struct mlx5_ct_ft *ct_ft,
		       struct mlx5_tc_ct_pre *pre_ct)
{
	tc_ct_pre_ct_del_rules(ct_ft, pre_ct);
	mlx5_destroy_flow_group(pre_ct->miss_grp);
	mlx5_destroy_flow_group(pre_ct->flow_grp);
	mlx5_destroy_flow_table(pre_ct->fdb);
}

static int
mlx5_tc_ct_alloc_pre_ct_tables(struct mlx5_ct_ft *ft)
{
	int err;

	err = mlx5_tc_ct_alloc_pre_ct(ft, &ft->pre_ct, false);
	if (err)
		return err;

	err = mlx5_tc_ct_alloc_pre_ct(ft, &ft->pre_ct_nat, true);
	if (err)
		goto err_pre_ct_nat;

	return 0;

err_pre_ct_nat:
	mlx5_tc_ct_free_pre_ct(ft, &ft->pre_ct);
	return err;
}

static void
mlx5_tc_ct_free_pre_ct_tables(struct mlx5_ct_ft *ft)
{
	mlx5_tc_ct_free_pre_ct(ft, &ft->pre_ct_nat);
	mlx5_tc_ct_free_pre_ct(ft, &ft->pre_ct);
}

static struct mlx5_ct_ft *
mlx5_tc_ct_add_ft_cb(struct mlx5_tc_ct_priv *ct_priv, u16 zone,
		     struct nf_flowtable *nf_ft)
{
	struct mlx5_ct_ft *ft;
	int err;

	ft = rhashtable_lookup_fast(&ct_priv->zone_ht, &zone, zone_params);
	if (ft) {
		refcount_inc(&ft->refcount);
		return ft;
	}

	ft = kzalloc(sizeof(*ft), GFP_KERNEL);
	if (!ft)
		return ERR_PTR(-ENOMEM);

	ft->zone = zone;
	ft->nf_ft = nf_ft;
	ft->ct_priv = ct_priv;
	refcount_set(&ft->refcount, 1);

	err = mlx5_tc_ct_alloc_pre_ct_tables(ft);
	if (err)
		goto err_alloc_pre_ct;

	err = rhashtable_init(&ft->ct_entries_ht, &cts_ht_params);
	if (err)
		goto err_init;

	err = rhashtable_insert_fast(&ct_priv->zone_ht, &ft->node,
				     zone_params);
	if (err)
		goto err_insert;

	err = nf_flow_table_offload_add_cb(ft->nf_ft,
					   mlx5_tc_ct_block_flow_offload, ft);
	if (err)
		goto err_add_cb;

	return ft;

err_add_cb:
	rhashtable_remove_fast(&ct_priv->zone_ht, &ft->node, zone_params);
err_insert:
	rhashtable_destroy(&ft->ct_entries_ht);
err_init:
	mlx5_tc_ct_free_pre_ct_tables(ft);
err_alloc_pre_ct:
	kfree(ft);
	return ERR_PTR(err);
}

static void
mlx5_tc_ct_flush_ft_entry(void *ptr, void *arg)
{
	struct mlx5_tc_ct_priv *ct_priv = arg;
	struct mlx5_ct_entry *entry = ptr;

	mlx5_tc_ct_entry_del_rules(ct_priv, entry);
}

static void
mlx5_tc_ct_del_ft_cb(struct mlx5_tc_ct_priv *ct_priv, struct mlx5_ct_ft *ft)
{
	if (!refcount_dec_and_test(&ft->refcount))
		return;

	nf_flow_table_offload_del_cb(ft->nf_ft,
				     mlx5_tc_ct_block_flow_offload, ft);
	rhashtable_remove_fast(&ct_priv->zone_ht, &ft->node, zone_params);
	rhashtable_free_and_destroy(&ft->ct_entries_ht,
				    mlx5_tc_ct_flush_ft_entry,
				    ct_priv);
	mlx5_tc_ct_free_pre_ct_tables(ft);
	kfree(ft);
}

/* We translate the tc filter with CT action to the following HW model:
 *
 * +---------------------+
 * + fdb prio (tc chain) +
 * + original match      +
 * +---------------------+
 *      | set chain miss mapping
 *      | set fte_id
 *      | set tunnel_id
 *      | do decap
 *      v
 * +---------------------+
 * + pre_ct/pre_ct_nat   +  if matches     +---------------------+
 * + zone+nat match      +---------------->+ post_ct (see below) +
 * +---------------------+  set zone       +---------------------+
 *      | set zone
 *      v
 * +--------------------+
 * + CT (nat or no nat) +
 * + tuple + zone match +
 * +--------------------+
 *      | set mark
 *      | set label
 *      | set established
 *      | do nat (if needed)
 *      v
 * +--------------+
 * + post_ct      + original filter actions
 * + fte_id match +------------------------>
 * +--------------+
 */
static int
__mlx5_tc_ct_flow_offload(struct mlx5e_priv *priv,
			  struct mlx5e_tc_flow *flow,
			  struct mlx5_flow_spec *orig_spec,
			  struct mlx5_esw_flow_attr *attr,
			  struct mlx5_flow_handle **flow_rule)
{
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);
	bool nat = attr->ct_attr.ct_action & TCA_CT_ACT_NAT;
	struct mlx5e_tc_mod_hdr_acts pre_mod_acts = {};
	struct mlx5_flow_spec *post_ct_spec = NULL;
	struct mlx5_eswitch *esw = ct_priv->esw;
	struct mlx5_esw_flow_attr *pre_ct_attr;
	struct mlx5_modify_hdr *mod_hdr;
	struct mlx5_flow_handle *rule;
	struct mlx5_ct_flow *ct_flow;
	int chain_mapping = 0, err;
	struct mlx5_ct_ft *ft;
	u32 fte_id = 1;

	post_ct_spec = kzalloc(sizeof(*post_ct_spec), GFP_KERNEL);
	ct_flow = kzalloc(sizeof(*ct_flow), GFP_KERNEL);
	if (!post_ct_spec || !ct_flow) {
		kfree(post_ct_spec);
		kfree(ct_flow);
		return -ENOMEM;
	}

	/* Register for CT established events */
	ft = mlx5_tc_ct_add_ft_cb(ct_priv, attr->ct_attr.zone,
				  attr->ct_attr.nf_ft);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		ct_dbg("Failed to register to ft callback");
		goto err_ft;
	}
	ct_flow->ft = ft;

	err = idr_alloc_u32(&ct_priv->fte_ids, ct_flow, &fte_id,
			    MLX5_FTE_ID_MAX, GFP_KERNEL);
	if (err) {
		netdev_warn(priv->netdev,
			    "Failed to allocate fte id, err: %d\n", err);
		goto err_idr;
	}
	ct_flow->fte_id = fte_id;

	/* Base esw attributes of both rules on original rule attribute */
	pre_ct_attr = &ct_flow->pre_ct_attr;
	memcpy(pre_ct_attr, attr, sizeof(*attr));
	memcpy(&ct_flow->post_ct_attr, attr, sizeof(*attr));

	/* Modify the original rule's action to fwd and modify, leave decap */
	pre_ct_attr->action = attr->action & MLX5_FLOW_CONTEXT_ACTION_DECAP;
	pre_ct_attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			       MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	/* Write chain miss tag for miss in ct table as we
	 * don't go though all prios of this chain as normal tc rules
	 * miss.
	 */
	err = mlx5_esw_chains_get_chain_mapping(esw, attr->chain,
						&chain_mapping);
	if (err) {
		ct_dbg("Failed to get chain register mapping for chain");
		goto err_get_chain;
	}
	ct_flow->chain_mapping = chain_mapping;

	err = mlx5e_tc_match_to_reg_set(esw->dev, &pre_mod_acts,
					CHAIN_TO_REG, chain_mapping);
	if (err) {
		ct_dbg("Failed to set chain register mapping");
		goto err_mapping;
	}

	err = mlx5e_tc_match_to_reg_set(esw->dev, &pre_mod_acts,
					FTEID_TO_REG, fte_id);
	if (err) {
		ct_dbg("Failed to set fte_id register mapping");
		goto err_mapping;
	}

	/* If original flow is decap, we do it before going into ct table
	 * so add a rewrite for the tunnel match_id.
	 */
	if ((pre_ct_attr->action & MLX5_FLOW_CONTEXT_ACTION_DECAP) &&
	    attr->chain == 0) {
		u32 tun_id = mlx5e_tc_get_flow_tun_id(flow);

		err = mlx5e_tc_match_to_reg_set(esw->dev, &pre_mod_acts,
						TUNNEL_TO_REG,
						tun_id);
		if (err) {
			ct_dbg("Failed to set tunnel register mapping");
			goto err_mapping;
		}
	}

	mod_hdr = mlx5_modify_header_alloc(esw->dev,
					   MLX5_FLOW_NAMESPACE_FDB,
					   pre_mod_acts.num_actions,
					   pre_mod_acts.actions);
	if (IS_ERR(mod_hdr)) {
		err = PTR_ERR(mod_hdr);
		ct_dbg("Failed to create pre ct mod hdr");
		goto err_mapping;
	}
	pre_ct_attr->modify_hdr = mod_hdr;

	/* Post ct rule matches on fte_id and executes original rule's
	 * tc rule action
	 */
	mlx5e_tc_match_to_reg_match(post_ct_spec, FTEID_TO_REG,
				    fte_id, MLX5_FTE_ID_MASK);

	/* Put post_ct rule on post_ct fdb */
	ct_flow->post_ct_attr.chain = 0;
	ct_flow->post_ct_attr.prio = 0;
	ct_flow->post_ct_attr.fdb = ct_priv->post_ct;

	ct_flow->post_ct_attr.inner_match_level = MLX5_MATCH_NONE;
	ct_flow->post_ct_attr.outer_match_level = MLX5_MATCH_NONE;
	ct_flow->post_ct_attr.action &= ~(MLX5_FLOW_CONTEXT_ACTION_DECAP);
	rule = mlx5_eswitch_add_offloaded_rule(esw, post_ct_spec,
					       &ct_flow->post_ct_attr);
	ct_flow->post_ct_rule = rule;
	if (IS_ERR(ct_flow->post_ct_rule)) {
		err = PTR_ERR(ct_flow->post_ct_rule);
		ct_dbg("Failed to add post ct rule");
		goto err_insert_post_ct;
	}

	/* Change original rule point to ct table */
	pre_ct_attr->dest_chain = 0;
	pre_ct_attr->dest_ft = nat ? ft->pre_ct_nat.fdb : ft->pre_ct.fdb;
	ct_flow->pre_ct_rule = mlx5_eswitch_add_offloaded_rule(esw,
							       orig_spec,
							       pre_ct_attr);
	if (IS_ERR(ct_flow->pre_ct_rule)) {
		err = PTR_ERR(ct_flow->pre_ct_rule);
		ct_dbg("Failed to add pre ct rule");
		goto err_insert_orig;
	}

	attr->ct_attr.ct_flow = ct_flow;
	*flow_rule = ct_flow->post_ct_rule;
	dealloc_mod_hdr_actions(&pre_mod_acts);
	kfree(post_ct_spec);

	return 0;

err_insert_orig:
	mlx5_eswitch_del_offloaded_rule(ct_priv->esw, ct_flow->post_ct_rule,
					&ct_flow->post_ct_attr);
err_insert_post_ct:
	mlx5_modify_header_dealloc(priv->mdev, pre_ct_attr->modify_hdr);
err_mapping:
	dealloc_mod_hdr_actions(&pre_mod_acts);
	mlx5_esw_chains_put_chain_mapping(esw, ct_flow->chain_mapping);
err_get_chain:
	idr_remove(&ct_priv->fte_ids, fte_id);
err_idr:
	mlx5_tc_ct_del_ft_cb(ct_priv, ft);
err_ft:
	kfree(post_ct_spec);
	kfree(ct_flow);
	netdev_warn(priv->netdev, "Failed to offload ct flow, err %d\n", err);
	return err;
}

static int
__mlx5_tc_ct_flow_offload_clear(struct mlx5e_priv *priv,
				struct mlx5e_tc_flow *flow,
				struct mlx5_flow_spec *orig_spec,
				struct mlx5_esw_flow_attr *attr,
				struct mlx5e_tc_mod_hdr_acts *mod_acts,
				struct mlx5_flow_handle **flow_rule)
{
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);
	struct mlx5_eswitch *esw = ct_priv->esw;
	struct mlx5_esw_flow_attr *pre_ct_attr;
	struct mlx5_modify_hdr *mod_hdr;
	struct mlx5_flow_handle *rule;
	struct mlx5_ct_flow *ct_flow;
	int err;

	ct_flow = kzalloc(sizeof(*ct_flow), GFP_KERNEL);
	if (!ct_flow)
		return -ENOMEM;

	/* Base esw attributes on original rule attribute */
	pre_ct_attr = &ct_flow->pre_ct_attr;
	memcpy(pre_ct_attr, attr, sizeof(*attr));

	err = mlx5_tc_ct_entry_set_registers(ct_priv, mod_acts, 0, 0, 0, 0);
	if (err) {
		ct_dbg("Failed to set register for ct clear");
		goto err_set_registers;
	}

	mod_hdr = mlx5_modify_header_alloc(esw->dev,
					   MLX5_FLOW_NAMESPACE_FDB,
					   mod_acts->num_actions,
					   mod_acts->actions);
	if (IS_ERR(mod_hdr)) {
		err = PTR_ERR(mod_hdr);
		ct_dbg("Failed to add create ct clear mod hdr");
		goto err_set_registers;
	}

	dealloc_mod_hdr_actions(mod_acts);
	pre_ct_attr->modify_hdr = mod_hdr;
	pre_ct_attr->action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	rule = mlx5_eswitch_add_offloaded_rule(esw, orig_spec, pre_ct_attr);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		ct_dbg("Failed to add ct clear rule");
		goto err_insert;
	}

	attr->ct_attr.ct_flow = ct_flow;
	ct_flow->pre_ct_rule = rule;
	*flow_rule = rule;

	return 0;

err_insert:
	mlx5_modify_header_dealloc(priv->mdev, mod_hdr);
err_set_registers:
	netdev_warn(priv->netdev,
		    "Failed to offload ct clear flow, err %d\n", err);
	return err;
}

struct mlx5_flow_handle *
mlx5_tc_ct_flow_offload(struct mlx5e_priv *priv,
			struct mlx5e_tc_flow *flow,
			struct mlx5_flow_spec *spec,
			struct mlx5_esw_flow_attr *attr,
			struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts)
{
	bool clear_action = attr->ct_attr.ct_action & TCA_CT_ACT_CLEAR;
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);
	struct mlx5_flow_handle *rule = ERR_PTR(-EINVAL);
	int err;

	if (!ct_priv)
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&ct_priv->control_lock);
	if (clear_action)
		err = __mlx5_tc_ct_flow_offload_clear(priv, flow, spec, attr,
						      mod_hdr_acts, &rule);
	else
		err = __mlx5_tc_ct_flow_offload(priv, flow, spec, attr,
						&rule);
	mutex_unlock(&ct_priv->control_lock);
	if (err)
		return ERR_PTR(err);

	return rule;
}

static void
__mlx5_tc_ct_delete_flow(struct mlx5_tc_ct_priv *ct_priv,
			 struct mlx5_ct_flow *ct_flow)
{
	struct mlx5_esw_flow_attr *pre_ct_attr = &ct_flow->pre_ct_attr;
	struct mlx5_eswitch *esw = ct_priv->esw;

	mlx5_eswitch_del_offloaded_rule(esw, ct_flow->pre_ct_rule,
					pre_ct_attr);
	mlx5_modify_header_dealloc(esw->dev, pre_ct_attr->modify_hdr);

	if (ct_flow->post_ct_rule) {
		mlx5_eswitch_del_offloaded_rule(esw, ct_flow->post_ct_rule,
						&ct_flow->post_ct_attr);
		mlx5_esw_chains_put_chain_mapping(esw, ct_flow->chain_mapping);
		idr_remove(&ct_priv->fte_ids, ct_flow->fte_id);
		mlx5_tc_ct_del_ft_cb(ct_priv, ct_flow->ft);
	}

	kfree(ct_flow);
}

void
mlx5_tc_ct_delete_flow(struct mlx5e_priv *priv, struct mlx5e_tc_flow *flow,
		       struct mlx5_esw_flow_attr *attr)
{
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);
	struct mlx5_ct_flow *ct_flow = attr->ct_attr.ct_flow;

	/* We are called on error to clean up stuff from parsing
	 * but we don't have anything for now
	 */
	if (!ct_flow)
		return;

	mutex_lock(&ct_priv->control_lock);
	__mlx5_tc_ct_delete_flow(ct_priv, ct_flow);
	mutex_unlock(&ct_priv->control_lock);
}

static int
mlx5_tc_ct_init_check_support(struct mlx5_eswitch *esw,
			      const char **err_msg)
{
#if !IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
	/* cannot restore chain ID on HW miss */

	*err_msg = "tc skb extension missing";
	return -EOPNOTSUPP;
#endif

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, ignore_flow_level)) {
		*err_msg = "firmware level support is missing";
		return -EOPNOTSUPP;
	}

	if (!mlx5_eswitch_vlan_actions_supported(esw->dev, 1)) {
		/* vlan workaround should be avoided for multi chain rules.
		 * This is just a sanity check as pop vlan action should
		 * be supported by any FW that supports ignore_flow_level
		 */

		*err_msg = "firmware vlan actions support is missing";
		return -EOPNOTSUPP;
	}

	if (!MLX5_CAP_ESW_FLOWTABLE(esw->dev,
				    fdb_modify_header_fwd_to_table)) {
		/* CT always writes to registers which are mod header actions.
		 * Therefore, mod header and goto is required
		 */

		*err_msg = "firmware fwd and modify support is missing";
		return -EOPNOTSUPP;
	}

	if (!mlx5_eswitch_reg_c1_loopback_enabled(esw)) {
		*err_msg = "register loopback isn't supported";
		return -EOPNOTSUPP;
	}

	return 0;
}

static void
mlx5_tc_ct_init_err(struct mlx5e_rep_priv *rpriv, const char *msg, int err)
{
	if (msg)
		netdev_warn(rpriv->netdev,
			    "tc ct offload not supported, %s, err: %d\n",
			    msg, err);
	else
		netdev_warn(rpriv->netdev,
			    "tc ct offload not supported, err: %d\n",
			    err);
}

int
mlx5_tc_ct_init(struct mlx5_rep_uplink_priv *uplink_priv)
{
	struct mlx5_tc_ct_priv *ct_priv;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5_eswitch *esw;
	struct mlx5e_priv *priv;
	const char *msg;
	int err;

	rpriv = container_of(uplink_priv, struct mlx5e_rep_priv, uplink_priv);
	priv = netdev_priv(rpriv->netdev);
	esw = priv->mdev->priv.eswitch;

	err = mlx5_tc_ct_init_check_support(esw, &msg);
	if (err) {
		mlx5_tc_ct_init_err(rpriv, msg, err);
		goto err_support;
	}

	ct_priv = kzalloc(sizeof(*ct_priv), GFP_KERNEL);
	if (!ct_priv) {
		mlx5_tc_ct_init_err(rpriv, NULL, -ENOMEM);
		goto err_alloc;
	}

	ct_priv->esw = esw;
	ct_priv->netdev = rpriv->netdev;
	ct_priv->ct = mlx5_esw_chains_create_global_table(esw);
	if (IS_ERR(ct_priv->ct)) {
		err = PTR_ERR(ct_priv->ct);
		mlx5_tc_ct_init_err(rpriv, "failed to create ct table", err);
		goto err_ct_tbl;
	}

	ct_priv->ct_nat = mlx5_esw_chains_create_global_table(esw);
	if (IS_ERR(ct_priv->ct_nat)) {
		err = PTR_ERR(ct_priv->ct_nat);
		mlx5_tc_ct_init_err(rpriv, "failed to create ct nat table",
				    err);
		goto err_ct_nat_tbl;
	}

	ct_priv->post_ct = mlx5_esw_chains_create_global_table(esw);
	if (IS_ERR(ct_priv->post_ct)) {
		err = PTR_ERR(ct_priv->post_ct);
		mlx5_tc_ct_init_err(rpriv, "failed to create post ct table",
				    err);
		goto err_post_ct_tbl;
	}

	idr_init(&ct_priv->fte_ids);
	xa_init_flags(&ct_priv->tuple_ids, XA_FLAGS_ALLOC1);
	mutex_init(&ct_priv->control_lock);
	rhashtable_init(&ct_priv->zone_ht, &zone_params);

	/* Done, set ct_priv to know it initializted */
	uplink_priv->ct_priv = ct_priv;

	return 0;

err_post_ct_tbl:
	mlx5_esw_chains_destroy_global_table(esw, ct_priv->ct_nat);
err_ct_nat_tbl:
	mlx5_esw_chains_destroy_global_table(esw, ct_priv->ct);
err_ct_tbl:
	kfree(ct_priv);
err_alloc:
err_support:

	return 0;
}

void
mlx5_tc_ct_clean(struct mlx5_rep_uplink_priv *uplink_priv)
{
	struct mlx5_tc_ct_priv *ct_priv = uplink_priv->ct_priv;

	if (!ct_priv)
		return;

	mlx5_esw_chains_destroy_global_table(ct_priv->esw, ct_priv->post_ct);
	mlx5_esw_chains_destroy_global_table(ct_priv->esw, ct_priv->ct_nat);
	mlx5_esw_chains_destroy_global_table(ct_priv->esw, ct_priv->ct);

	rhashtable_destroy(&ct_priv->zone_ht);
	mutex_destroy(&ct_priv->control_lock);
	xa_destroy(&ct_priv->tuple_ids);
	idr_destroy(&ct_priv->fte_ids);
	kfree(ct_priv);

	uplink_priv->ct_priv = NULL;
}

bool
mlx5e_tc_ct_restore_flow(struct mlx5_rep_uplink_priv *uplink_priv,
			 struct sk_buff *skb, u32 tupleid)
{
	struct mlx5_tc_ct_priv *ct_priv = uplink_priv->ct_priv;
	struct mlx5_ct_zone_rule *zone_rule;
	struct mlx5_ct_entry *entry;

	if (!ct_priv || !tupleid)
		return true;

	zone_rule = xa_load(&ct_priv->tuple_ids, tupleid);
	if (!zone_rule)
		return false;

	entry = container_of(zone_rule, struct mlx5_ct_entry,
			     zone_rules[zone_rule->nat]);
	tcf_ct_flow_table_restore_skb(skb, entry->restore_cookie);

	return true;
}
