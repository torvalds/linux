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
#include <linux/refcount.h>
#include <linux/xarray.h>

#include "lib/fs_chains.h"
#include "en/tc_ct.h"
#include "en/mod_hdr.h"
#include "en/mapping.h"
#include "en/tc/post_act.h"
#include "en.h"
#include "en_tc.h"
#include "en_rep.h"

#define MLX5_CT_ZONE_BITS (mlx5e_tc_attr_to_reg_mappings[ZONE_TO_REG].mlen)
#define MLX5_CT_ZONE_MASK GENMASK(MLX5_CT_ZONE_BITS - 1, 0)
#define MLX5_CT_STATE_ESTABLISHED_BIT BIT(1)
#define MLX5_CT_STATE_TRK_BIT BIT(2)
#define MLX5_CT_STATE_NAT_BIT BIT(3)
#define MLX5_CT_STATE_REPLY_BIT BIT(4)
#define MLX5_CT_STATE_RELATED_BIT BIT(5)
#define MLX5_CT_STATE_INVALID_BIT BIT(6)

#define MLX5_CT_LABELS_BITS (mlx5e_tc_attr_to_reg_mappings[LABELS_TO_REG].mlen)
#define MLX5_CT_LABELS_MASK GENMASK(MLX5_CT_LABELS_BITS - 1, 0)

#define ct_dbg(fmt, args...)\
	netdev_dbg(ct_priv->netdev, "ct_debug: " fmt "\n", ##args)

struct mlx5_tc_ct_priv {
	struct mlx5_core_dev *dev;
	const struct net_device *netdev;
	struct mod_hdr_tbl *mod_hdr_tbl;
	struct xarray tuple_ids;
	struct rhashtable zone_ht;
	struct rhashtable ct_tuples_ht;
	struct rhashtable ct_tuples_nat_ht;
	struct mlx5_flow_table *ct;
	struct mlx5_flow_table *ct_nat;
	struct mlx5e_post_act *post_act;
	struct mutex control_lock; /* guards parallel adds/dels */
	struct mapping_ctx *zone_mapping;
	struct mapping_ctx *labels_mapping;
	enum mlx5_flow_namespace_type ns_type;
	struct mlx5_fs_chains *chains;
	spinlock_t ht_lock; /* protects ft entries */
};

struct mlx5_ct_flow {
	struct mlx5_flow_attr *pre_ct_attr;
	struct mlx5_flow_handle *pre_ct_rule;
	struct mlx5e_post_act_handle *post_act_handle;
	struct mlx5_ct_ft *ft;
	u32 chain_mapping;
};

struct mlx5_ct_zone_rule {
	struct mlx5_flow_handle *rule;
	struct mlx5e_mod_hdr_handle *mh;
	struct mlx5_flow_attr *attr;
	bool nat;
};

struct mlx5_tc_ct_pre {
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *flow_grp;
	struct mlx5_flow_group *miss_grp;
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_flow_handle *miss_rule;
	struct mlx5_modify_hdr *modify_hdr;
};

struct mlx5_ct_ft {
	struct rhash_head node;
	u16 zone;
	u32 zone_restore_id;
	refcount_t refcount;
	struct nf_flowtable *nf_ft;
	struct mlx5_tc_ct_priv *ct_priv;
	struct rhashtable ct_entries_ht;
	struct mlx5_tc_ct_pre pre_ct;
	struct mlx5_tc_ct_pre pre_ct_nat;
};

struct mlx5_ct_tuple {
	u16 addr_type;
	__be16 n_proto;
	u8 ip_proto;
	struct {
		union {
			__be32 src_v4;
			struct in6_addr src_v6;
		};
		union {
			__be32 dst_v4;
			struct in6_addr dst_v6;
		};
	} ip;
	struct {
		__be16 src;
		__be16 dst;
	} port;

	u16 zone;
};

struct mlx5_ct_counter {
	struct mlx5_fc *counter;
	refcount_t refcount;
	bool is_shared;
};

enum {
	MLX5_CT_ENTRY_FLAG_VALID,
};

struct mlx5_ct_entry {
	struct rhash_head node;
	struct rhash_head tuple_node;
	struct rhash_head tuple_nat_node;
	struct mlx5_ct_counter *counter;
	unsigned long cookie;
	unsigned long restore_cookie;
	struct mlx5_ct_tuple tuple;
	struct mlx5_ct_tuple tuple_nat;
	struct mlx5_ct_zone_rule zone_rules[2];

	struct mlx5_tc_ct_priv *ct_priv;
	struct work_struct work;

	refcount_t refcnt;
	unsigned long flags;
};

static void
mlx5_tc_ct_entry_destroy_mod_hdr(struct mlx5_tc_ct_priv *ct_priv,
				 struct mlx5_flow_attr *attr,
				 struct mlx5e_mod_hdr_handle *mh);

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

static const struct rhashtable_params tuples_ht_params = {
	.head_offset = offsetof(struct mlx5_ct_entry, tuple_node),
	.key_offset = offsetof(struct mlx5_ct_entry, tuple),
	.key_len = sizeof(((struct mlx5_ct_entry *)0)->tuple),
	.automatic_shrinking = true,
	.min_size = 16 * 1024,
};

static const struct rhashtable_params tuples_nat_ht_params = {
	.head_offset = offsetof(struct mlx5_ct_entry, tuple_nat_node),
	.key_offset = offsetof(struct mlx5_ct_entry, tuple_nat),
	.key_len = sizeof(((struct mlx5_ct_entry *)0)->tuple_nat),
	.automatic_shrinking = true,
	.min_size = 16 * 1024,
};

static bool
mlx5_tc_ct_entry_has_nat(struct mlx5_ct_entry *entry)
{
	return !!(entry->tuple_nat_node.next);
}

static int
mlx5_get_label_mapping(struct mlx5_tc_ct_priv *ct_priv,
		       u32 *labels, u32 *id)
{
	if (!memchr_inv(labels, 0, sizeof(u32) * 4)) {
		*id = 0;
		return 0;
	}

	if (mapping_add(ct_priv->labels_mapping, labels, id))
		return -EOPNOTSUPP;

	return 0;
}

static void
mlx5_put_label_mapping(struct mlx5_tc_ct_priv *ct_priv, u32 id)
{
	if (id)
		mapping_remove(ct_priv->labels_mapping, id);
}

static int
mlx5_tc_ct_rule_to_tuple(struct mlx5_ct_tuple *tuple, struct flow_rule *rule)
{
	struct flow_match_control control;
	struct flow_match_basic basic;

	flow_rule_match_basic(rule, &basic);
	flow_rule_match_control(rule, &control);

	tuple->n_proto = basic.key->n_proto;
	tuple->ip_proto = basic.key->ip_proto;
	tuple->addr_type = control.key->addr_type;

	if (tuple->addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);
		tuple->ip.src_v4 = match.key->src;
		tuple->ip.dst_v4 = match.key->dst;
	} else if (tuple->addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);
		tuple->ip.src_v6 = match.key->src;
		tuple->ip.dst_v6 = match.key->dst;
	} else {
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		switch (tuple->ip_proto) {
		case IPPROTO_TCP:
		case IPPROTO_UDP:
			tuple->port.src = match.key->src;
			tuple->port.dst = match.key->dst;
			break;
		default:
			return -EOPNOTSUPP;
		}
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
mlx5_tc_ct_rule_to_tuple_nat(struct mlx5_ct_tuple *tuple,
			     struct flow_rule *rule)
{
	struct flow_action *flow_action = &rule->action;
	struct flow_action_entry *act;
	u32 offset, val, ip6_offset;
	int i;

	flow_action_for_each(i, act, flow_action) {
		if (act->id != FLOW_ACTION_MANGLE)
			continue;

		offset = act->mangle.offset;
		val = act->mangle.val;
		switch (act->mangle.htype) {
		case FLOW_ACT_MANGLE_HDR_TYPE_IP4:
			if (offset == offsetof(struct iphdr, saddr))
				tuple->ip.src_v4 = cpu_to_be32(val);
			else if (offset == offsetof(struct iphdr, daddr))
				tuple->ip.dst_v4 = cpu_to_be32(val);
			else
				return -EOPNOTSUPP;
			break;

		case FLOW_ACT_MANGLE_HDR_TYPE_IP6:
			ip6_offset = (offset - offsetof(struct ipv6hdr, saddr));
			ip6_offset /= 4;
			if (ip6_offset < 4)
				tuple->ip.src_v6.s6_addr32[ip6_offset] = cpu_to_be32(val);
			else if (ip6_offset < 8)
				tuple->ip.dst_v6.s6_addr32[ip6_offset - 4] = cpu_to_be32(val);
			else
				return -EOPNOTSUPP;
			break;

		case FLOW_ACT_MANGLE_HDR_TYPE_TCP:
			if (offset == offsetof(struct tcphdr, source))
				tuple->port.src = cpu_to_be16(val);
			else if (offset == offsetof(struct tcphdr, dest))
				tuple->port.dst = cpu_to_be16(val);
			else
				return -EOPNOTSUPP;
			break;

		case FLOW_ACT_MANGLE_HDR_TYPE_UDP:
			if (offset == offsetof(struct udphdr, source))
				tuple->port.src = cpu_to_be16(val);
			else if (offset == offsetof(struct udphdr, dest))
				tuple->port.dst = cpu_to_be16(val);
			else
				return -EOPNOTSUPP;
			break;

		default:
			return -EOPNOTSUPP;
		}
	}

	return 0;
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
mlx5_tc_ct_counter_put(struct mlx5_tc_ct_priv *ct_priv, struct mlx5_ct_entry *entry)
{
	if (entry->counter->is_shared &&
	    !refcount_dec_and_test(&entry->counter->refcount))
		return;

	mlx5_fc_destroy(ct_priv->dev, entry->counter->counter);
	kfree(entry->counter);
}

static void
mlx5_tc_ct_entry_del_rule(struct mlx5_tc_ct_priv *ct_priv,
			  struct mlx5_ct_entry *entry,
			  bool nat)
{
	struct mlx5_ct_zone_rule *zone_rule = &entry->zone_rules[nat];
	struct mlx5_flow_attr *attr = zone_rule->attr;

	ct_dbg("Deleting ct entry rule in zone %d", entry->tuple.zone);

	mlx5_tc_rule_delete(netdev_priv(ct_priv->netdev), zone_rule->rule, attr);
	mlx5_tc_ct_entry_destroy_mod_hdr(ct_priv, zone_rule->attr, zone_rule->mh);
	mlx5_put_label_mapping(ct_priv, attr->ct_attr.ct_labels_id);
	kfree(attr);
}

static void
mlx5_tc_ct_entry_del_rules(struct mlx5_tc_ct_priv *ct_priv,
			   struct mlx5_ct_entry *entry)
{
	mlx5_tc_ct_entry_del_rule(ct_priv, entry, true);
	mlx5_tc_ct_entry_del_rule(ct_priv, entry, false);
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
			       u32 labels_id,
			       u8 zone_restore_id)
{
	enum mlx5_flow_namespace_type ns = ct_priv->ns_type;
	struct mlx5_core_dev *dev = ct_priv->dev;
	int err;

	err = mlx5e_tc_match_to_reg_set(dev, mod_acts, ns,
					CTSTATE_TO_REG, ct_state);
	if (err)
		return err;

	err = mlx5e_tc_match_to_reg_set(dev, mod_acts, ns,
					MARK_TO_REG, mark);
	if (err)
		return err;

	err = mlx5e_tc_match_to_reg_set(dev, mod_acts, ns,
					LABELS_TO_REG, labels_id);
	if (err)
		return err;

	err = mlx5e_tc_match_to_reg_set(dev, mod_acts, ns,
					ZONE_RESTORE_TO_REG, zone_restore_id);
	if (err)
		return err;

	/* Make another copy of zone id in reg_b for
	 * NIC rx flows since we don't copy reg_c1 to
	 * reg_b upon miss.
	 */
	if (ns != MLX5_FLOW_NAMESPACE_FDB) {
		err = mlx5e_tc_match_to_reg_set(dev, mod_acts, ns,
						NIC_ZONE_RESTORE_TO_REG, zone_restore_id);
		if (err)
			return err;
	}
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
	struct mlx5_core_dev *mdev = ct_priv->dev;
	struct flow_action_entry *act;
	size_t action_size;
	char *modact;
	int err, i;

	action_size = MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto);

	flow_action_for_each(i, act, flow_action) {
		switch (act->id) {
		case FLOW_ACTION_MANGLE: {
			err = alloc_mod_hdr_actions(mdev, ct_priv->ns_type,
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
				struct mlx5_flow_attr *attr,
				struct flow_rule *flow_rule,
				struct mlx5e_mod_hdr_handle **mh,
				u8 zone_restore_id, bool nat)
{
	struct mlx5e_tc_mod_hdr_acts mod_acts = {};
	struct flow_action_entry *meta;
	u16 ct_state = 0;
	int err;

	meta = mlx5_tc_ct_get_ct_metadata_action(flow_rule);
	if (!meta)
		return -EOPNOTSUPP;

	err = mlx5_get_label_mapping(ct_priv, meta->ct_metadata.labels,
				     &attr->ct_attr.ct_labels_id);
	if (err)
		return -EOPNOTSUPP;
	if (nat) {
		err = mlx5_tc_ct_entry_create_nat(ct_priv, flow_rule,
						  &mod_acts);
		if (err)
			goto err_mapping;

		ct_state |= MLX5_CT_STATE_NAT_BIT;
	}

	ct_state |= MLX5_CT_STATE_ESTABLISHED_BIT | MLX5_CT_STATE_TRK_BIT;
	ct_state |= meta->ct_metadata.orig_dir ? 0 : MLX5_CT_STATE_REPLY_BIT;
	err = mlx5_tc_ct_entry_set_registers(ct_priv, &mod_acts,
					     ct_state,
					     meta->ct_metadata.mark,
					     attr->ct_attr.ct_labels_id,
					     zone_restore_id);
	if (err)
		goto err_mapping;

	if (nat) {
		attr->modify_hdr = mlx5_modify_header_alloc(ct_priv->dev, ct_priv->ns_type,
							    mod_acts.num_actions,
							    mod_acts.actions);
		if (IS_ERR(attr->modify_hdr)) {
			err = PTR_ERR(attr->modify_hdr);
			goto err_mapping;
		}

		*mh = NULL;
	} else {
		*mh = mlx5e_mod_hdr_attach(ct_priv->dev,
					   ct_priv->mod_hdr_tbl,
					   ct_priv->ns_type,
					   &mod_acts);
		if (IS_ERR(*mh)) {
			err = PTR_ERR(*mh);
			goto err_mapping;
		}
		attr->modify_hdr = mlx5e_mod_hdr_get(*mh);
	}

	dealloc_mod_hdr_actions(&mod_acts);
	return 0;

err_mapping:
	dealloc_mod_hdr_actions(&mod_acts);
	mlx5_put_label_mapping(ct_priv, attr->ct_attr.ct_labels_id);
	return err;
}

static void
mlx5_tc_ct_entry_destroy_mod_hdr(struct mlx5_tc_ct_priv *ct_priv,
				 struct mlx5_flow_attr *attr,
				 struct mlx5e_mod_hdr_handle *mh)
{
	if (mh)
		mlx5e_mod_hdr_detach(ct_priv->dev, ct_priv->mod_hdr_tbl, mh);
	else
		mlx5_modify_header_dealloc(ct_priv->dev, attr->modify_hdr);
}

static int
mlx5_tc_ct_entry_add_rule(struct mlx5_tc_ct_priv *ct_priv,
			  struct flow_rule *flow_rule,
			  struct mlx5_ct_entry *entry,
			  bool nat, u8 zone_restore_id)
{
	struct mlx5_ct_zone_rule *zone_rule = &entry->zone_rules[nat];
	struct mlx5e_priv *priv = netdev_priv(ct_priv->netdev);
	struct mlx5_flow_spec *spec = NULL;
	struct mlx5_flow_attr *attr;
	int err;

	zone_rule->nat = nat;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	attr = mlx5_alloc_flow_attr(ct_priv->ns_type);
	if (!attr) {
		err = -ENOMEM;
		goto err_attr;
	}

	err = mlx5_tc_ct_entry_create_mod_hdr(ct_priv, attr, flow_rule,
					      &zone_rule->mh,
					      zone_restore_id, nat);
	if (err) {
		ct_dbg("Failed to create ct entry mod hdr");
		goto err_mod_hdr;
	}

	attr->action = MLX5_FLOW_CONTEXT_ACTION_MOD_HDR |
		       MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
		       MLX5_FLOW_CONTEXT_ACTION_COUNT;
	attr->dest_chain = 0;
	attr->dest_ft = mlx5e_tc_post_act_get_ft(ct_priv->post_act);
	attr->ft = nat ? ct_priv->ct_nat : ct_priv->ct;
	attr->outer_match_level = MLX5_MATCH_L4;
	attr->counter = entry->counter->counter;
	attr->flags |= MLX5_ESW_ATTR_FLAG_NO_IN_PORT;
	if (ct_priv->ns_type == MLX5_FLOW_NAMESPACE_FDB)
		attr->esw_attr->in_mdev = priv->mdev;

	mlx5_tc_ct_set_tuple_match(netdev_priv(ct_priv->netdev), spec, flow_rule);
	mlx5e_tc_match_to_reg_match(spec, ZONE_TO_REG, entry->tuple.zone, MLX5_CT_ZONE_MASK);

	zone_rule->rule = mlx5_tc_rule_insert(priv, spec, attr);
	if (IS_ERR(zone_rule->rule)) {
		err = PTR_ERR(zone_rule->rule);
		ct_dbg("Failed to add ct entry rule, nat: %d", nat);
		goto err_rule;
	}

	zone_rule->attr = attr;

	kvfree(spec);
	ct_dbg("Offloaded ct entry rule in zone %d", entry->tuple.zone);

	return 0;

err_rule:
	mlx5_tc_ct_entry_destroy_mod_hdr(ct_priv, zone_rule->attr, zone_rule->mh);
	mlx5_put_label_mapping(ct_priv, attr->ct_attr.ct_labels_id);
err_mod_hdr:
	kfree(attr);
err_attr:
	kvfree(spec);
	return err;
}

static bool
mlx5_tc_ct_entry_valid(struct mlx5_ct_entry *entry)
{
	return test_bit(MLX5_CT_ENTRY_FLAG_VALID, &entry->flags);
}

static struct mlx5_ct_entry *
mlx5_tc_ct_entry_get(struct mlx5_tc_ct_priv *ct_priv, struct mlx5_ct_tuple *tuple)
{
	struct mlx5_ct_entry *entry;

	entry = rhashtable_lookup_fast(&ct_priv->ct_tuples_ht, tuple,
				       tuples_ht_params);
	if (entry && mlx5_tc_ct_entry_valid(entry) &&
	    refcount_inc_not_zero(&entry->refcnt)) {
		return entry;
	} else if (!entry) {
		entry = rhashtable_lookup_fast(&ct_priv->ct_tuples_nat_ht,
					       tuple, tuples_nat_ht_params);
		if (entry && mlx5_tc_ct_entry_valid(entry) &&
		    refcount_inc_not_zero(&entry->refcnt))
			return entry;
	}

	return entry ? ERR_PTR(-EINVAL) : NULL;
}

static void mlx5_tc_ct_entry_remove_from_tuples(struct mlx5_ct_entry *entry)
{
	struct mlx5_tc_ct_priv *ct_priv = entry->ct_priv;

	rhashtable_remove_fast(&ct_priv->ct_tuples_nat_ht,
			       &entry->tuple_nat_node,
			       tuples_nat_ht_params);
	rhashtable_remove_fast(&ct_priv->ct_tuples_ht, &entry->tuple_node,
			       tuples_ht_params);
}

static void mlx5_tc_ct_entry_del(struct mlx5_ct_entry *entry)
{
	struct mlx5_tc_ct_priv *ct_priv = entry->ct_priv;

	mlx5_tc_ct_entry_del_rules(ct_priv, entry);

	spin_lock_bh(&ct_priv->ht_lock);
	mlx5_tc_ct_entry_remove_from_tuples(entry);
	spin_unlock_bh(&ct_priv->ht_lock);

	mlx5_tc_ct_counter_put(ct_priv, entry);
	kfree(entry);
}

static void
mlx5_tc_ct_entry_put(struct mlx5_ct_entry *entry)
{
	if (!refcount_dec_and_test(&entry->refcnt))
		return;

	mlx5_tc_ct_entry_del(entry);
}

static void mlx5_tc_ct_entry_del_work(struct work_struct *work)
{
	struct mlx5_ct_entry *entry = container_of(work, struct mlx5_ct_entry, work);

	mlx5_tc_ct_entry_del(entry);
}

static void
__mlx5_tc_ct_entry_put(struct mlx5_ct_entry *entry)
{
	struct mlx5e_priv *priv;

	if (!refcount_dec_and_test(&entry->refcnt))
		return;

	priv = netdev_priv(entry->ct_priv->netdev);
	INIT_WORK(&entry->work, mlx5_tc_ct_entry_del_work);
	queue_work(priv->wq, &entry->work);
}

static struct mlx5_ct_counter *
mlx5_tc_ct_counter_create(struct mlx5_tc_ct_priv *ct_priv)
{
	struct mlx5_ct_counter *counter;
	int ret;

	counter = kzalloc(sizeof(*counter), GFP_KERNEL);
	if (!counter)
		return ERR_PTR(-ENOMEM);

	counter->is_shared = false;
	counter->counter = mlx5_fc_create(ct_priv->dev, true);
	if (IS_ERR(counter->counter)) {
		ct_dbg("Failed to create counter for ct entry");
		ret = PTR_ERR(counter->counter);
		kfree(counter);
		return ERR_PTR(ret);
	}

	return counter;
}

static struct mlx5_ct_counter *
mlx5_tc_ct_shared_counter_get(struct mlx5_tc_ct_priv *ct_priv,
			      struct mlx5_ct_entry *entry)
{
	struct mlx5_ct_tuple rev_tuple = entry->tuple;
	struct mlx5_ct_counter *shared_counter;
	struct mlx5_ct_entry *rev_entry;
	__be16 tmp_port;

	/* get the reversed tuple */
	tmp_port = rev_tuple.port.src;
	rev_tuple.port.src = rev_tuple.port.dst;
	rev_tuple.port.dst = tmp_port;

	if (rev_tuple.addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		__be32 tmp_addr = rev_tuple.ip.src_v4;

		rev_tuple.ip.src_v4 = rev_tuple.ip.dst_v4;
		rev_tuple.ip.dst_v4 = tmp_addr;
	} else if (rev_tuple.addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct in6_addr tmp_addr = rev_tuple.ip.src_v6;

		rev_tuple.ip.src_v6 = rev_tuple.ip.dst_v6;
		rev_tuple.ip.dst_v6 = tmp_addr;
	} else {
		return ERR_PTR(-EOPNOTSUPP);
	}

	/* Use the same counter as the reverse direction */
	spin_lock_bh(&ct_priv->ht_lock);
	rev_entry = mlx5_tc_ct_entry_get(ct_priv, &rev_tuple);

	if (IS_ERR(rev_entry)) {
		spin_unlock_bh(&ct_priv->ht_lock);
		goto create_counter;
	}

	if (rev_entry && refcount_inc_not_zero(&rev_entry->counter->refcount)) {
		ct_dbg("Using shared counter entry=0x%p rev=0x%p", entry, rev_entry);
		shared_counter = rev_entry->counter;
		spin_unlock_bh(&ct_priv->ht_lock);

		mlx5_tc_ct_entry_put(rev_entry);
		return shared_counter;
	}

	spin_unlock_bh(&ct_priv->ht_lock);

create_counter:

	shared_counter = mlx5_tc_ct_counter_create(ct_priv);
	if (IS_ERR(shared_counter))
		return shared_counter;

	shared_counter->is_shared = true;
	refcount_set(&shared_counter->refcount, 1);
	return shared_counter;
}

static int
mlx5_tc_ct_entry_add_rules(struct mlx5_tc_ct_priv *ct_priv,
			   struct flow_rule *flow_rule,
			   struct mlx5_ct_entry *entry,
			   u8 zone_restore_id)
{
	int err;

	if (nf_ct_acct_enabled(dev_net(ct_priv->netdev)))
		entry->counter = mlx5_tc_ct_counter_create(ct_priv);
	else
		entry->counter = mlx5_tc_ct_shared_counter_get(ct_priv, entry);

	if (IS_ERR(entry->counter)) {
		err = PTR_ERR(entry->counter);
		return err;
	}

	err = mlx5_tc_ct_entry_add_rule(ct_priv, flow_rule, entry, false,
					zone_restore_id);
	if (err)
		goto err_orig;

	err = mlx5_tc_ct_entry_add_rule(ct_priv, flow_rule, entry, true,
					zone_restore_id);
	if (err)
		goto err_nat;

	return 0;

err_nat:
	mlx5_tc_ct_entry_del_rule(ct_priv, entry, false);
err_orig:
	mlx5_tc_ct_counter_put(ct_priv, entry);
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

	spin_lock_bh(&ct_priv->ht_lock);
	entry = rhashtable_lookup_fast(&ft->ct_entries_ht, &cookie, cts_ht_params);
	if (entry && refcount_inc_not_zero(&entry->refcnt)) {
		spin_unlock_bh(&ct_priv->ht_lock);
		mlx5_tc_ct_entry_put(entry);
		return -EEXIST;
	}
	spin_unlock_bh(&ct_priv->ht_lock);

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->tuple.zone = ft->zone;
	entry->cookie = flow->cookie;
	entry->restore_cookie = meta_action->ct_metadata.cookie;
	refcount_set(&entry->refcnt, 2);
	entry->ct_priv = ct_priv;

	err = mlx5_tc_ct_rule_to_tuple(&entry->tuple, flow_rule);
	if (err)
		goto err_set;

	memcpy(&entry->tuple_nat, &entry->tuple, sizeof(entry->tuple));
	err = mlx5_tc_ct_rule_to_tuple_nat(&entry->tuple_nat, flow_rule);
	if (err)
		goto err_set;

	spin_lock_bh(&ct_priv->ht_lock);

	err = rhashtable_lookup_insert_fast(&ft->ct_entries_ht, &entry->node,
					    cts_ht_params);
	if (err)
		goto err_entries;

	err = rhashtable_lookup_insert_fast(&ct_priv->ct_tuples_ht,
					    &entry->tuple_node,
					    tuples_ht_params);
	if (err)
		goto err_tuple;

	if (memcmp(&entry->tuple, &entry->tuple_nat, sizeof(entry->tuple))) {
		err = rhashtable_lookup_insert_fast(&ct_priv->ct_tuples_nat_ht,
						    &entry->tuple_nat_node,
						    tuples_nat_ht_params);
		if (err)
			goto err_tuple_nat;
	}
	spin_unlock_bh(&ct_priv->ht_lock);

	err = mlx5_tc_ct_entry_add_rules(ct_priv, flow_rule, entry,
					 ft->zone_restore_id);
	if (err)
		goto err_rules;

	set_bit(MLX5_CT_ENTRY_FLAG_VALID, &entry->flags);
	mlx5_tc_ct_entry_put(entry); /* this function reference */

	return 0;

err_rules:
	spin_lock_bh(&ct_priv->ht_lock);
	if (mlx5_tc_ct_entry_has_nat(entry))
		rhashtable_remove_fast(&ct_priv->ct_tuples_nat_ht,
				       &entry->tuple_nat_node, tuples_nat_ht_params);
err_tuple_nat:
	rhashtable_remove_fast(&ct_priv->ct_tuples_ht,
			       &entry->tuple_node,
			       tuples_ht_params);
err_tuple:
	rhashtable_remove_fast(&ft->ct_entries_ht,
			       &entry->node,
			       cts_ht_params);
err_entries:
	spin_unlock_bh(&ct_priv->ht_lock);
err_set:
	kfree(entry);
	if (err != -EEXIST)
		netdev_warn(ct_priv->netdev, "Failed to offload ct entry, err: %d\n", err);
	return err;
}

static int
mlx5_tc_ct_block_flow_offload_del(struct mlx5_ct_ft *ft,
				  struct flow_cls_offload *flow)
{
	struct mlx5_tc_ct_priv *ct_priv = ft->ct_priv;
	unsigned long cookie = flow->cookie;
	struct mlx5_ct_entry *entry;

	spin_lock_bh(&ct_priv->ht_lock);
	entry = rhashtable_lookup_fast(&ft->ct_entries_ht, &cookie, cts_ht_params);
	if (!entry) {
		spin_unlock_bh(&ct_priv->ht_lock);
		return -ENOENT;
	}

	if (!mlx5_tc_ct_entry_valid(entry)) {
		spin_unlock_bh(&ct_priv->ht_lock);
		return -EINVAL;
	}

	rhashtable_remove_fast(&ft->ct_entries_ht, &entry->node, cts_ht_params);
	mlx5_tc_ct_entry_remove_from_tuples(entry);
	spin_unlock_bh(&ct_priv->ht_lock);

	mlx5_tc_ct_entry_put(entry);

	return 0;
}

static int
mlx5_tc_ct_block_flow_offload_stats(struct mlx5_ct_ft *ft,
				    struct flow_cls_offload *f)
{
	struct mlx5_tc_ct_priv *ct_priv = ft->ct_priv;
	unsigned long cookie = f->cookie;
	struct mlx5_ct_entry *entry;
	u64 lastuse, packets, bytes;

	spin_lock_bh(&ct_priv->ht_lock);
	entry = rhashtable_lookup_fast(&ft->ct_entries_ht, &cookie, cts_ht_params);
	if (!entry) {
		spin_unlock_bh(&ct_priv->ht_lock);
		return -ENOENT;
	}

	if (!mlx5_tc_ct_entry_valid(entry) || !refcount_inc_not_zero(&entry->refcnt)) {
		spin_unlock_bh(&ct_priv->ht_lock);
		return -EINVAL;
	}

	spin_unlock_bh(&ct_priv->ht_lock);

	mlx5_fc_query_cached(entry->counter->counter, &bytes, &packets, &lastuse);
	flow_stats_update(&f->stats, bytes, packets, 0, lastuse,
			  FLOW_ACTION_HW_STATS_DELAYED);

	mlx5_tc_ct_entry_put(entry);
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

static bool
mlx5_tc_ct_skb_to_tuple(struct sk_buff *skb, struct mlx5_ct_tuple *tuple,
			u16 zone)
{
	struct flow_keys flow_keys;

	skb_reset_network_header(skb);
	skb_flow_dissect_flow_keys(skb, &flow_keys, 0);

	tuple->zone = zone;

	if (flow_keys.basic.ip_proto != IPPROTO_TCP &&
	    flow_keys.basic.ip_proto != IPPROTO_UDP)
		return false;

	tuple->port.src = flow_keys.ports.src;
	tuple->port.dst = flow_keys.ports.dst;
	tuple->n_proto = flow_keys.basic.n_proto;
	tuple->ip_proto = flow_keys.basic.ip_proto;

	switch (flow_keys.basic.n_proto) {
	case htons(ETH_P_IP):
		tuple->addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
		tuple->ip.src_v4 = flow_keys.addrs.v4addrs.src;
		tuple->ip.dst_v4 = flow_keys.addrs.v4addrs.dst;
		break;

	case htons(ETH_P_IPV6):
		tuple->addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
		tuple->ip.src_v6 = flow_keys.addrs.v6addrs.src;
		tuple->ip.dst_v6 = flow_keys.addrs.v6addrs.dst;
		break;
	default:
		goto out;
	}

	return true;

out:
	return false;
}

int mlx5_tc_ct_add_no_trk_match(struct mlx5_flow_spec *spec)
{
	u32 ctstate = 0, ctstate_mask = 0;

	mlx5e_tc_match_to_reg_get_match(spec, CTSTATE_TO_REG,
					&ctstate, &ctstate_mask);

	if ((ctstate & ctstate_mask) == MLX5_CT_STATE_TRK_BIT)
		return -EOPNOTSUPP;

	ctstate_mask |= MLX5_CT_STATE_TRK_BIT;
	mlx5e_tc_match_to_reg_match(spec, CTSTATE_TO_REG,
				    ctstate, ctstate_mask);

	return 0;
}

void mlx5_tc_ct_match_del(struct mlx5_tc_ct_priv *priv, struct mlx5_ct_attr *ct_attr)
{
	if (!priv || !ct_attr->ct_labels_id)
		return;

	mlx5_put_label_mapping(priv, ct_attr->ct_labels_id);
}

int
mlx5_tc_ct_match_add(struct mlx5_tc_ct_priv *priv,
		     struct mlx5_flow_spec *spec,
		     struct flow_cls_offload *f,
		     struct mlx5_ct_attr *ct_attr,
		     struct netlink_ext_ack *extack)
{
	bool trk, est, untrk, unest, new, rpl, unrpl, rel, unrel, inv, uninv;
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_dissector_key_ct *mask, *key;
	u32 ctstate = 0, ctstate_mask = 0;
	u16 ct_state_on, ct_state_off;
	u16 ct_state, ct_state_mask;
	struct flow_match_ct match;
	u32 ct_labels[4];

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CT))
		return 0;

	if (!priv) {
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
			      TCA_FLOWER_KEY_CT_FLAGS_NEW |
			      TCA_FLOWER_KEY_CT_FLAGS_REPLY |
			      TCA_FLOWER_KEY_CT_FLAGS_RELATED |
			      TCA_FLOWER_KEY_CT_FLAGS_INVALID)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "only ct_state trk, est, new and rpl are supported for offload");
		return -EOPNOTSUPP;
	}

	ct_state_on = ct_state & ct_state_mask;
	ct_state_off = (ct_state & ct_state_mask) ^ ct_state_mask;
	trk = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_TRACKED;
	new = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_NEW;
	est = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED;
	rpl = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_REPLY;
	rel = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_RELATED;
	inv = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_INVALID;
	untrk = ct_state_off & TCA_FLOWER_KEY_CT_FLAGS_TRACKED;
	unest = ct_state_off & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED;
	unrpl = ct_state_off & TCA_FLOWER_KEY_CT_FLAGS_REPLY;
	unrel = ct_state_off & TCA_FLOWER_KEY_CT_FLAGS_RELATED;
	uninv = ct_state_off & TCA_FLOWER_KEY_CT_FLAGS_INVALID;

	ctstate |= trk ? MLX5_CT_STATE_TRK_BIT : 0;
	ctstate |= est ? MLX5_CT_STATE_ESTABLISHED_BIT : 0;
	ctstate |= rpl ? MLX5_CT_STATE_REPLY_BIT : 0;
	ctstate_mask |= (untrk || trk) ? MLX5_CT_STATE_TRK_BIT : 0;
	ctstate_mask |= (unest || est) ? MLX5_CT_STATE_ESTABLISHED_BIT : 0;
	ctstate_mask |= (unrpl || rpl) ? MLX5_CT_STATE_REPLY_BIT : 0;
	ctstate_mask |= unrel ? MLX5_CT_STATE_RELATED_BIT : 0;
	ctstate_mask |= uninv ? MLX5_CT_STATE_INVALID_BIT : 0;

	if (rel) {
		NL_SET_ERR_MSG_MOD(extack,
				   "matching on ct_state +rel isn't supported");
		return -EOPNOTSUPP;
	}

	if (inv) {
		NL_SET_ERR_MSG_MOD(extack,
				   "matching on ct_state +inv isn't supported");
		return -EOPNOTSUPP;
	}

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
	if (mask->ct_labels[0] || mask->ct_labels[1] || mask->ct_labels[2] ||
	    mask->ct_labels[3]) {
		ct_labels[0] = key->ct_labels[0] & mask->ct_labels[0];
		ct_labels[1] = key->ct_labels[1] & mask->ct_labels[1];
		ct_labels[2] = key->ct_labels[2] & mask->ct_labels[2];
		ct_labels[3] = key->ct_labels[3] & mask->ct_labels[3];
		if (mlx5_get_label_mapping(priv, ct_labels, &ct_attr->ct_labels_id))
			return -EOPNOTSUPP;
		mlx5e_tc_match_to_reg_match(spec, LABELS_TO_REG, ct_attr->ct_labels_id,
					    MLX5_CT_LABELS_MASK);
	}

	return 0;
}

int
mlx5_tc_ct_parse_action(struct mlx5_tc_ct_priv *priv,
			struct mlx5_flow_attr *attr,
			const struct flow_action_entry *act,
			struct netlink_ext_ack *extack)
{
	if (!priv) {
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
	struct mlx5_core_dev *dev = ct_priv->dev;
	struct mlx5_flow_table *ft = pre_ct->ft;
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
	err = mlx5e_tc_match_to_reg_set(dev, &pre_mod_acts, ct_priv->ns_type,
					ZONE_TO_REG, zone);
	if (err) {
		ct_dbg("Failed to set zone register mapping");
		goto err_mapping;
	}

	mod_hdr = mlx5_modify_header_alloc(dev, ct_priv->ns_type,
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

	dest.ft = mlx5e_tc_post_act_get_ft(ct_priv->post_act);
	rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		ct_dbg("Failed to add pre ct flow rule zone %d", zone);
		goto err_flow_rule;
	}
	pre_ct->flow_rule = rule;

	/* add miss rule */
	dest.ft = nat ? ct_priv->ct_nat : ct_priv->ct;
	rule = mlx5_add_flow_rules(ft, NULL, &flow_act, &dest, 1);
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
	struct mlx5_core_dev *dev = ct_priv->dev;

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
	struct mlx5_core_dev *dev = ct_priv->dev;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *g;
	u32 metadata_reg_c_2_mask;
	u32 *flow_group_in;
	void *misc;
	int err;

	ns = mlx5_get_flow_namespace(dev, ct_priv->ns_type);
	if (!ns) {
		err = -EOPNOTSUPP;
		ct_dbg("Failed to get flow namespace");
		return err;
	}

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	ft_attr.flags = MLX5_FLOW_TABLE_UNMANAGED;
	ft_attr.prio =  ct_priv->ns_type ==  MLX5_FLOW_NAMESPACE_FDB ?
			FDB_TC_OFFLOAD : MLX5E_TC_PRIO;
	ft_attr.max_fte = 2;
	ft_attr.level = 1;
	ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		ct_dbg("Failed to create pre ct table");
		goto out_free;
	}
	pre_ct->ft = ft;

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
	mlx5_destroy_flow_table(pre_ct->ft);
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

/* To avoid false lock dependency warning set the ct_entries_ht lock
 * class different than the lock class of the ht being used when deleting
 * last flow from a group and then deleting a group, we get into del_sw_flow_group()
 * which call rhashtable_destroy on fg->ftes_hash which will take ht->mutex but
 * it's different than the ht->mutex here.
 */
static struct lock_class_key ct_entries_ht_lock_key;

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

	err = mapping_add(ct_priv->zone_mapping, &zone, &ft->zone_restore_id);
	if (err)
		goto err_mapping;

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

	lockdep_set_class(&ft->ct_entries_ht.mutex, &ct_entries_ht_lock_key);

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
	mapping_remove(ct_priv->zone_mapping, ft->zone_restore_id);
err_mapping:
	kfree(ft);
	return ERR_PTR(err);
}

static void
mlx5_tc_ct_flush_ft_entry(void *ptr, void *arg)
{
	struct mlx5_ct_entry *entry = ptr;

	mlx5_tc_ct_entry_put(entry);
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
	mapping_remove(ct_priv->zone_mapping, ft->zone_restore_id);
	kfree(ft);
}

/* We translate the tc filter with CT action to the following HW model:
 *
 * +---------------------+
 * + ft prio (tc chain) +
 * + original match      +
 * +---------------------+
 *      | set chain miss mapping
 *      | set fte_id
 *      | set tunnel_id
 *      | do decap
 *      v
 * +---------------------+
 * + pre_ct/pre_ct_nat   +  if matches     +-------------------------+
 * + zone+nat match      +---------------->+ post_act (see below) +
 * +---------------------+  set zone       +-------------------------+
 *      | set zone
 *      v
 * +--------------------+
 * + CT (nat or no nat) +
 * + tuple + zone match +
 * +--------------------+
 *      | set mark
 *      | set labels_id
 *      | set established
 *	| set zone_restore
 *      | do nat (if needed)
 *      v
 * +--------------+
 * + post_act  + original filter actions
 * + fte_id match +------------------------>
 * +--------------+
 */
static struct mlx5_flow_handle *
__mlx5_tc_ct_flow_offload(struct mlx5_tc_ct_priv *ct_priv,
			  struct mlx5e_tc_flow *flow,
			  struct mlx5_flow_spec *orig_spec,
			  struct mlx5_flow_attr *attr)
{
	bool nat = attr->ct_attr.ct_action & TCA_CT_ACT_NAT;
	struct mlx5e_priv *priv = netdev_priv(ct_priv->netdev);
	struct mlx5e_tc_mod_hdr_acts pre_mod_acts = {};
	u32 attr_sz = ns_to_attr_sz(ct_priv->ns_type);
	struct mlx5e_post_act_handle *handle;
	struct mlx5_flow_attr *pre_ct_attr;
	struct mlx5_modify_hdr *mod_hdr;
	struct mlx5_ct_flow *ct_flow;
	int chain_mapping = 0, err;
	struct mlx5_ct_ft *ft;

	ct_flow = kzalloc(sizeof(*ct_flow), GFP_KERNEL);
	if (!ct_flow) {
		kfree(ct_flow);
		return ERR_PTR(-ENOMEM);
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

	handle = mlx5e_tc_post_act_add(ct_priv->post_act, attr);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		ct_dbg("Failed to allocate post action handle");
		goto err_post_act_handle;
	}
	ct_flow->post_act_handle = handle;

	/* Base flow attributes of both rules on original rule attribute */
	ct_flow->pre_ct_attr = mlx5_alloc_flow_attr(ct_priv->ns_type);
	if (!ct_flow->pre_ct_attr) {
		err = -ENOMEM;
		goto err_alloc_pre;
	}

	pre_ct_attr = ct_flow->pre_ct_attr;
	memcpy(pre_ct_attr, attr, attr_sz);

	/* Modify the original rule's action to fwd and modify, leave decap */
	pre_ct_attr->action = attr->action & MLX5_FLOW_CONTEXT_ACTION_DECAP;
	pre_ct_attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			       MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	/* Write chain miss tag for miss in ct table as we
	 * don't go though all prios of this chain as normal tc rules
	 * miss.
	 */
	err = mlx5_chains_get_chain_mapping(ct_priv->chains, attr->chain,
					    &chain_mapping);
	if (err) {
		ct_dbg("Failed to get chain register mapping for chain");
		goto err_get_chain;
	}
	ct_flow->chain_mapping = chain_mapping;

	err = mlx5e_tc_match_to_reg_set(priv->mdev, &pre_mod_acts, ct_priv->ns_type,
					CHAIN_TO_REG, chain_mapping);
	if (err) {
		ct_dbg("Failed to set chain register mapping");
		goto err_mapping;
	}

	err = mlx5e_tc_post_act_set_handle(priv->mdev, handle, &pre_mod_acts);
	if (err) {
		ct_dbg("Failed to set post action handle");
		goto err_mapping;
	}

	/* If original flow is decap, we do it before going into ct table
	 * so add a rewrite for the tunnel match_id.
	 */
	if ((pre_ct_attr->action & MLX5_FLOW_CONTEXT_ACTION_DECAP) &&
	    attr->chain == 0) {
		u32 tun_id = mlx5e_tc_get_flow_tun_id(flow);

		err = mlx5e_tc_match_to_reg_set(priv->mdev, &pre_mod_acts,
						ct_priv->ns_type,
						TUNNEL_TO_REG,
						tun_id);
		if (err) {
			ct_dbg("Failed to set tunnel register mapping");
			goto err_mapping;
		}
	}

	mod_hdr = mlx5_modify_header_alloc(priv->mdev, ct_priv->ns_type,
					   pre_mod_acts.num_actions,
					   pre_mod_acts.actions);
	if (IS_ERR(mod_hdr)) {
		err = PTR_ERR(mod_hdr);
		ct_dbg("Failed to create pre ct mod hdr");
		goto err_mapping;
	}
	pre_ct_attr->modify_hdr = mod_hdr;

	/* Change original rule point to ct table */
	pre_ct_attr->dest_chain = 0;
	pre_ct_attr->dest_ft = nat ? ft->pre_ct_nat.ft : ft->pre_ct.ft;
	ct_flow->pre_ct_rule = mlx5_tc_rule_insert(priv, orig_spec,
						   pre_ct_attr);
	if (IS_ERR(ct_flow->pre_ct_rule)) {
		err = PTR_ERR(ct_flow->pre_ct_rule);
		ct_dbg("Failed to add pre ct rule");
		goto err_insert_orig;
	}

	attr->ct_attr.ct_flow = ct_flow;
	dealloc_mod_hdr_actions(&pre_mod_acts);

	return ct_flow->pre_ct_rule;

err_insert_orig:
	mlx5_modify_header_dealloc(priv->mdev, pre_ct_attr->modify_hdr);
err_mapping:
	dealloc_mod_hdr_actions(&pre_mod_acts);
	mlx5_chains_put_chain_mapping(ct_priv->chains, ct_flow->chain_mapping);
err_get_chain:
	kfree(ct_flow->pre_ct_attr);
err_alloc_pre:
	mlx5e_tc_post_act_del(ct_priv->post_act, handle);
err_post_act_handle:
	mlx5_tc_ct_del_ft_cb(ct_priv, ft);
err_ft:
	kfree(ct_flow);
	netdev_warn(priv->netdev, "Failed to offload ct flow, err %d\n", err);
	return ERR_PTR(err);
}

static struct mlx5_flow_handle *
__mlx5_tc_ct_flow_offload_clear(struct mlx5_tc_ct_priv *ct_priv,
				struct mlx5_flow_spec *orig_spec,
				struct mlx5_flow_attr *attr,
				struct mlx5e_tc_mod_hdr_acts *mod_acts)
{
	struct mlx5e_priv *priv = netdev_priv(ct_priv->netdev);
	u32 attr_sz = ns_to_attr_sz(ct_priv->ns_type);
	struct mlx5_flow_attr *pre_ct_attr;
	struct mlx5_modify_hdr *mod_hdr;
	struct mlx5_flow_handle *rule;
	struct mlx5_ct_flow *ct_flow;
	int err;

	ct_flow = kzalloc(sizeof(*ct_flow), GFP_KERNEL);
	if (!ct_flow)
		return ERR_PTR(-ENOMEM);

	/* Base esw attributes on original rule attribute */
	pre_ct_attr = mlx5_alloc_flow_attr(ct_priv->ns_type);
	if (!pre_ct_attr) {
		err = -ENOMEM;
		goto err_attr;
	}

	memcpy(pre_ct_attr, attr, attr_sz);

	err = mlx5_tc_ct_entry_set_registers(ct_priv, mod_acts, 0, 0, 0, 0);
	if (err) {
		ct_dbg("Failed to set register for ct clear");
		goto err_set_registers;
	}

	mod_hdr = mlx5_modify_header_alloc(priv->mdev, ct_priv->ns_type,
					   mod_acts->num_actions,
					   mod_acts->actions);
	if (IS_ERR(mod_hdr)) {
		err = PTR_ERR(mod_hdr);
		ct_dbg("Failed to add create ct clear mod hdr");
		goto err_set_registers;
	}

	pre_ct_attr->modify_hdr = mod_hdr;
	pre_ct_attr->action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	rule = mlx5_tc_rule_insert(priv, orig_spec, pre_ct_attr);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		ct_dbg("Failed to add ct clear rule");
		goto err_insert;
	}

	attr->ct_attr.ct_flow = ct_flow;
	ct_flow->pre_ct_attr = pre_ct_attr;
	ct_flow->pre_ct_rule = rule;
	return rule;

err_insert:
	mlx5_modify_header_dealloc(priv->mdev, mod_hdr);
err_set_registers:
	netdev_warn(priv->netdev,
		    "Failed to offload ct clear flow, err %d\n", err);
	kfree(pre_ct_attr);
err_attr:
	kfree(ct_flow);

	return ERR_PTR(err);
}

struct mlx5_flow_handle *
mlx5_tc_ct_flow_offload(struct mlx5_tc_ct_priv *priv,
			struct mlx5e_tc_flow *flow,
			struct mlx5_flow_spec *spec,
			struct mlx5_flow_attr *attr,
			struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts)
{
	bool clear_action = attr->ct_attr.ct_action & TCA_CT_ACT_CLEAR;
	struct mlx5_flow_handle *rule;

	if (!priv)
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&priv->control_lock);

	if (clear_action)
		rule = __mlx5_tc_ct_flow_offload_clear(priv, spec, attr, mod_hdr_acts);
	else
		rule = __mlx5_tc_ct_flow_offload(priv, flow, spec, attr);
	mutex_unlock(&priv->control_lock);

	return rule;
}

static void
__mlx5_tc_ct_delete_flow(struct mlx5_tc_ct_priv *ct_priv,
			 struct mlx5e_tc_flow *flow,
			 struct mlx5_ct_flow *ct_flow)
{
	struct mlx5_flow_attr *pre_ct_attr = ct_flow->pre_ct_attr;
	struct mlx5e_priv *priv = netdev_priv(ct_priv->netdev);

	mlx5_tc_rule_delete(priv, ct_flow->pre_ct_rule,
			    pre_ct_attr);
	mlx5_modify_header_dealloc(priv->mdev, pre_ct_attr->modify_hdr);

	if (ct_flow->post_act_handle) {
		mlx5_chains_put_chain_mapping(ct_priv->chains, ct_flow->chain_mapping);
		mlx5e_tc_post_act_del(ct_priv->post_act, ct_flow->post_act_handle);
		mlx5_tc_ct_del_ft_cb(ct_priv, ct_flow->ft);
	}

	kfree(ct_flow->pre_ct_attr);
	kfree(ct_flow);
}

void
mlx5_tc_ct_delete_flow(struct mlx5_tc_ct_priv *priv,
		       struct mlx5e_tc_flow *flow,
		       struct mlx5_flow_attr *attr)
{
	struct mlx5_ct_flow *ct_flow = attr->ct_attr.ct_flow;

	/* We are called on error to clean up stuff from parsing
	 * but we don't have anything for now
	 */
	if (!ct_flow)
		return;

	mutex_lock(&priv->control_lock);
	__mlx5_tc_ct_delete_flow(priv, flow, ct_flow);
	mutex_unlock(&priv->control_lock);
}

static int
mlx5_tc_ct_init_check_esw_support(struct mlx5_eswitch *esw,
				  const char **err_msg)
{
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

static int
mlx5_tc_ct_init_check_support(struct mlx5e_priv *priv,
			      enum mlx5_flow_namespace_type ns_type,
			      struct mlx5e_post_act *post_act,
			      const char **err_msg)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

#if !IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
	/* cannot restore chain ID on HW miss */

	*err_msg = "tc skb extension missing";
	return -EOPNOTSUPP;
#endif
	if (IS_ERR_OR_NULL(post_act)) {
		*err_msg = "tc ct offload not supported, post action is missing";
		return -EOPNOTSUPP;
	}

	if (ns_type == MLX5_FLOW_NAMESPACE_FDB)
		return mlx5_tc_ct_init_check_esw_support(esw, err_msg);
	return 0;
}

#define INIT_ERR_PREFIX "tc ct offload init failed"

struct mlx5_tc_ct_priv *
mlx5_tc_ct_init(struct mlx5e_priv *priv, struct mlx5_fs_chains *chains,
		struct mod_hdr_tbl *mod_hdr,
		enum mlx5_flow_namespace_type ns_type,
		struct mlx5e_post_act *post_act)
{
	struct mlx5_tc_ct_priv *ct_priv;
	struct mlx5_core_dev *dev;
	const char *msg;
	u64 mapping_id;
	int err;

	dev = priv->mdev;
	err = mlx5_tc_ct_init_check_support(priv, ns_type, post_act, &msg);
	if (err) {
		mlx5_core_warn(dev, "tc ct offload not supported, %s\n", msg);
		goto err_support;
	}

	ct_priv = kzalloc(sizeof(*ct_priv), GFP_KERNEL);
	if (!ct_priv)
		goto err_alloc;

	mapping_id = mlx5_query_nic_system_image_guid(dev);

	ct_priv->zone_mapping = mapping_create_for_id(mapping_id, MAPPING_TYPE_ZONE,
						      sizeof(u16), 0, true);
	if (IS_ERR(ct_priv->zone_mapping)) {
		err = PTR_ERR(ct_priv->zone_mapping);
		goto err_mapping_zone;
	}

	ct_priv->labels_mapping = mapping_create_for_id(mapping_id, MAPPING_TYPE_LABELS,
							sizeof(u32) * 4, 0, true);
	if (IS_ERR(ct_priv->labels_mapping)) {
		err = PTR_ERR(ct_priv->labels_mapping);
		goto err_mapping_labels;
	}

	spin_lock_init(&ct_priv->ht_lock);
	ct_priv->ns_type = ns_type;
	ct_priv->chains = chains;
	ct_priv->netdev = priv->netdev;
	ct_priv->dev = priv->mdev;
	ct_priv->mod_hdr_tbl = mod_hdr;
	ct_priv->ct = mlx5_chains_create_global_table(chains);
	if (IS_ERR(ct_priv->ct)) {
		err = PTR_ERR(ct_priv->ct);
		mlx5_core_warn(dev,
			       "%s, failed to create ct table err: %d\n",
			       INIT_ERR_PREFIX, err);
		goto err_ct_tbl;
	}

	ct_priv->ct_nat = mlx5_chains_create_global_table(chains);
	if (IS_ERR(ct_priv->ct_nat)) {
		err = PTR_ERR(ct_priv->ct_nat);
		mlx5_core_warn(dev,
			       "%s, failed to create ct nat table err: %d\n",
			       INIT_ERR_PREFIX, err);
		goto err_ct_nat_tbl;
	}

	ct_priv->post_act = post_act;
	mutex_init(&ct_priv->control_lock);
	if (rhashtable_init(&ct_priv->zone_ht, &zone_params))
		goto err_ct_zone_ht;
	if (rhashtable_init(&ct_priv->ct_tuples_ht, &tuples_ht_params))
		goto err_ct_tuples_ht;
	if (rhashtable_init(&ct_priv->ct_tuples_nat_ht, &tuples_nat_ht_params))
		goto err_ct_tuples_nat_ht;

	return ct_priv;

err_ct_tuples_nat_ht:
	rhashtable_destroy(&ct_priv->ct_tuples_ht);
err_ct_tuples_ht:
	rhashtable_destroy(&ct_priv->zone_ht);
err_ct_zone_ht:
	mlx5_chains_destroy_global_table(chains, ct_priv->ct_nat);
err_ct_nat_tbl:
	mlx5_chains_destroy_global_table(chains, ct_priv->ct);
err_ct_tbl:
	mapping_destroy(ct_priv->labels_mapping);
err_mapping_labels:
	mapping_destroy(ct_priv->zone_mapping);
err_mapping_zone:
	kfree(ct_priv);
err_alloc:
err_support:

	return NULL;
}

void
mlx5_tc_ct_clean(struct mlx5_tc_ct_priv *ct_priv)
{
	struct mlx5_fs_chains *chains;

	if (!ct_priv)
		return;

	chains = ct_priv->chains;

	mlx5_chains_destroy_global_table(chains, ct_priv->ct_nat);
	mlx5_chains_destroy_global_table(chains, ct_priv->ct);
	mapping_destroy(ct_priv->zone_mapping);
	mapping_destroy(ct_priv->labels_mapping);

	rhashtable_destroy(&ct_priv->ct_tuples_ht);
	rhashtable_destroy(&ct_priv->ct_tuples_nat_ht);
	rhashtable_destroy(&ct_priv->zone_ht);
	mutex_destroy(&ct_priv->control_lock);
	kfree(ct_priv);
}

bool
mlx5e_tc_ct_restore_flow(struct mlx5_tc_ct_priv *ct_priv,
			 struct sk_buff *skb, u8 zone_restore_id)
{
	struct mlx5_ct_tuple tuple = {};
	struct mlx5_ct_entry *entry;
	u16 zone;

	if (!ct_priv || !zone_restore_id)
		return true;

	if (mapping_find(ct_priv->zone_mapping, zone_restore_id, &zone))
		return false;

	if (!mlx5_tc_ct_skb_to_tuple(skb, &tuple, zone))
		return false;

	spin_lock(&ct_priv->ht_lock);

	entry = mlx5_tc_ct_entry_get(ct_priv, &tuple);
	if (!entry) {
		spin_unlock(&ct_priv->ht_lock);
		return false;
	}

	if (IS_ERR(entry)) {
		spin_unlock(&ct_priv->ht_lock);
		return false;
	}
	spin_unlock(&ct_priv->ht_lock);

	tcf_ct_flow_table_restore_skb(skb, entry->restore_cookie);
	__mlx5_tc_ct_entry_put(entry);

	return true;
}
