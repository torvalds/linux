// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2021 Corigine, Inc. */

#include "conntrack.h"
#include "../nfp_port.h"

const struct rhashtable_params nfp_tc_ct_merge_params = {
	.head_offset		= offsetof(struct nfp_fl_ct_tc_merge,
					   hash_node),
	.key_len		= sizeof(unsigned long) * 2,
	.key_offset		= offsetof(struct nfp_fl_ct_tc_merge, cookie),
	.automatic_shrinking	= true,
};

const struct rhashtable_params nfp_nft_ct_merge_params = {
	.head_offset		= offsetof(struct nfp_fl_nft_tc_merge,
					   hash_node),
	.key_len		= sizeof(unsigned long) * 3,
	.key_offset		= offsetof(struct nfp_fl_nft_tc_merge, cookie),
	.automatic_shrinking	= true,
};

static struct flow_action_entry *get_flow_act(struct flow_rule *rule,
					      enum flow_action_id act_id);

/**
 * get_hashentry() - Wrapper around hashtable lookup.
 * @ht:		hashtable where entry could be found
 * @key:	key to lookup
 * @params:	hashtable params
 * @size:	size of entry to allocate if not in table
 *
 * Returns an entry from a hashtable. If entry does not exist
 * yet allocate the memory for it and return the new entry.
 */
static void *get_hashentry(struct rhashtable *ht, void *key,
			   const struct rhashtable_params params, size_t size)
{
	void *result;

	result = rhashtable_lookup_fast(ht, key, params);

	if (result)
		return result;

	result = kzalloc(size, GFP_KERNEL);
	if (!result)
		return ERR_PTR(-ENOMEM);

	return result;
}

bool is_pre_ct_flow(struct flow_cls_offload *flow)
{
	struct flow_action_entry *act;
	int i;

	flow_action_for_each(i, act, &flow->rule->action) {
		if (act->id == FLOW_ACTION_CT && !act->ct.action)
			return true;
	}
	return false;
}

bool is_post_ct_flow(struct flow_cls_offload *flow)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(flow);
	struct flow_dissector *dissector = rule->match.dissector;
	struct flow_match_ct ct;

	if (dissector->used_keys & BIT(FLOW_DISSECTOR_KEY_CT)) {
		flow_rule_match_ct(rule, &ct);
		if (ct.key->ct_state & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED)
			return true;
	}
	return false;
}

/**
 * get_mangled_key() - Mangle the key if mangle act exists
 * @rule:	rule that carries the actions
 * @buf:	pointer to key to be mangled
 * @offset:	used to adjust mangled offset in L2/L3/L4 header
 * @key_sz:	key size
 * @htype:	mangling type
 *
 * Returns buf where the mangled key stores.
 */
static void *get_mangled_key(struct flow_rule *rule, void *buf,
			     u32 offset, size_t key_sz,
			     enum flow_action_mangle_base htype)
{
	struct flow_action_entry *act;
	u32 *val = (u32 *)buf;
	u32 off, msk, key;
	int i;

	flow_action_for_each(i, act, &rule->action) {
		if (act->id == FLOW_ACTION_MANGLE &&
		    act->mangle.htype == htype) {
			off = act->mangle.offset - offset;
			msk = act->mangle.mask;
			key = act->mangle.val;

			/* Mangling is supposed to be u32 aligned */
			if (off % 4 || off >= key_sz)
				continue;

			val[off >> 2] &= msk;
			val[off >> 2] |= key;
		}
	}

	return buf;
}

/* Only tos and ttl are involved in flow_match_ip structure, which
 * doesn't conform to the layout of ip/ipv6 header definition. So
 * they need particular process here: fill them into the ip/ipv6
 * header, so that mangling actions can work directly.
 */
#define NFP_IPV4_TOS_MASK	GENMASK(23, 16)
#define NFP_IPV4_TTL_MASK	GENMASK(31, 24)
#define NFP_IPV6_TCLASS_MASK	GENMASK(27, 20)
#define NFP_IPV6_HLIMIT_MASK	GENMASK(7, 0)
static void *get_mangled_tos_ttl(struct flow_rule *rule, void *buf,
				 bool is_v6)
{
	struct flow_match_ip match;
	/* IPv4's ttl field is in third dword. */
	__be32 ip_hdr[3];
	u32 tmp, hdr_len;

	flow_rule_match_ip(rule, &match);

	if (is_v6) {
		tmp = FIELD_PREP(NFP_IPV6_TCLASS_MASK, match.key->tos);
		ip_hdr[0] = cpu_to_be32(tmp);
		tmp = FIELD_PREP(NFP_IPV6_HLIMIT_MASK, match.key->ttl);
		ip_hdr[1] = cpu_to_be32(tmp);
		hdr_len = 2 * sizeof(__be32);
	} else {
		tmp = FIELD_PREP(NFP_IPV4_TOS_MASK, match.key->tos);
		ip_hdr[0] = cpu_to_be32(tmp);
		tmp = FIELD_PREP(NFP_IPV4_TTL_MASK, match.key->ttl);
		ip_hdr[2] = cpu_to_be32(tmp);
		hdr_len = 3 * sizeof(__be32);
	}

	get_mangled_key(rule, ip_hdr, 0, hdr_len,
			is_v6 ? FLOW_ACT_MANGLE_HDR_TYPE_IP6 :
				FLOW_ACT_MANGLE_HDR_TYPE_IP4);

	match.key = buf;

	if (is_v6) {
		tmp = be32_to_cpu(ip_hdr[0]);
		match.key->tos = FIELD_GET(NFP_IPV6_TCLASS_MASK, tmp);
		tmp = be32_to_cpu(ip_hdr[1]);
		match.key->ttl = FIELD_GET(NFP_IPV6_HLIMIT_MASK, tmp);
	} else {
		tmp = be32_to_cpu(ip_hdr[0]);
		match.key->tos = FIELD_GET(NFP_IPV4_TOS_MASK, tmp);
		tmp = be32_to_cpu(ip_hdr[2]);
		match.key->ttl = FIELD_GET(NFP_IPV4_TTL_MASK, tmp);
	}

	return buf;
}

/* Note entry1 and entry2 are not swappable, entry1 should be
 * the former flow whose mangle action need be taken into account
 * if existed, and entry2 should be the latter flow whose action
 * we don't care.
 */
static int nfp_ct_merge_check(struct nfp_fl_ct_flow_entry *entry1,
			      struct nfp_fl_ct_flow_entry *entry2)
{
	unsigned int ovlp_keys = entry1->rule->match.dissector->used_keys &
				 entry2->rule->match.dissector->used_keys;
	bool out, is_v6 = false;
	u8 ip_proto = 0;
	/* Temporary buffer for mangling keys, 64 is enough to cover max
	 * struct size of key in various fields that may be mangled.
	 * Supported fields to mangle:
	 * mac_src/mac_dst(struct flow_match_eth_addrs, 12B)
	 * nw_tos/nw_ttl(struct flow_match_ip, 2B)
	 * nw_src/nw_dst(struct flow_match_ipv4/6_addrs, 32B)
	 * tp_src/tp_dst(struct flow_match_ports, 4B)
	 */
	char buf[64];

	if (entry1->netdev && entry2->netdev &&
	    entry1->netdev != entry2->netdev)
		return -EINVAL;

	/* Check the overlapped fields one by one, the unmasked part
	 * should not conflict with each other.
	 */
	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match1, match2;

		flow_rule_match_control(entry1->rule, &match1);
		flow_rule_match_control(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match1, match2;

		flow_rule_match_basic(entry1->rule, &match1);
		flow_rule_match_basic(entry2->rule, &match2);

		/* n_proto field is a must in ct-related flows,
		 * it should be either ipv4 or ipv6.
		 */
		is_v6 = match1.key->n_proto == htons(ETH_P_IPV6);
		/* ip_proto field is a must when port field is cared */
		ip_proto = match1.key->ip_proto;

		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match1, match2;

		flow_rule_match_ipv4_addrs(entry1->rule, &match1);
		flow_rule_match_ipv4_addrs(entry2->rule, &match2);

		memcpy(buf, match1.key, sizeof(*match1.key));
		match1.key = get_mangled_key(entry1->rule, buf,
					     offsetof(struct iphdr, saddr),
					     sizeof(*match1.key),
					     FLOW_ACT_MANGLE_HDR_TYPE_IP4);

		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match1, match2;

		flow_rule_match_ipv6_addrs(entry1->rule, &match1);
		flow_rule_match_ipv6_addrs(entry2->rule, &match2);

		memcpy(buf, match1.key, sizeof(*match1.key));
		match1.key = get_mangled_key(entry1->rule, buf,
					     offsetof(struct ipv6hdr, saddr),
					     sizeof(*match1.key),
					     FLOW_ACT_MANGLE_HDR_TYPE_IP6);

		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_PORTS)) {
		enum flow_action_mangle_base htype = FLOW_ACT_MANGLE_UNSPEC;
		struct flow_match_ports match1, match2;

		flow_rule_match_ports(entry1->rule, &match1);
		flow_rule_match_ports(entry2->rule, &match2);

		if (ip_proto == IPPROTO_UDP)
			htype = FLOW_ACT_MANGLE_HDR_TYPE_UDP;
		else if (ip_proto == IPPROTO_TCP)
			htype = FLOW_ACT_MANGLE_HDR_TYPE_TCP;

		memcpy(buf, match1.key, sizeof(*match1.key));
		match1.key = get_mangled_key(entry1->rule, buf, 0,
					     sizeof(*match1.key), htype);

		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match1, match2;

		flow_rule_match_eth_addrs(entry1->rule, &match1);
		flow_rule_match_eth_addrs(entry2->rule, &match2);

		memcpy(buf, match1.key, sizeof(*match1.key));
		match1.key = get_mangled_key(entry1->rule, buf, 0,
					     sizeof(*match1.key),
					     FLOW_ACT_MANGLE_HDR_TYPE_ETH);

		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match1, match2;

		flow_rule_match_vlan(entry1->rule, &match1);
		flow_rule_match_vlan(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_MPLS)) {
		struct flow_match_mpls match1, match2;

		flow_rule_match_mpls(entry1->rule, &match1);
		flow_rule_match_mpls(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_match_tcp match1, match2;

		flow_rule_match_tcp(entry1->rule, &match1);
		flow_rule_match_tcp(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match1, match2;

		flow_rule_match_ip(entry1->rule, &match1);
		flow_rule_match_ip(entry2->rule, &match2);

		match1.key = get_mangled_tos_ttl(entry1->rule, buf, is_v6);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid match1, match2;

		flow_rule_match_enc_keyid(entry1->rule, &match1);
		flow_rule_match_enc_keyid(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match1, match2;

		flow_rule_match_enc_ipv4_addrs(entry1->rule, &match1);
		flow_rule_match_enc_ipv4_addrs(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match1, match2;

		flow_rule_match_enc_ipv6_addrs(entry1->rule, &match1);
		flow_rule_match_enc_ipv6_addrs(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL)) {
		struct flow_match_control match1, match2;

		flow_rule_match_enc_control(entry1->rule, &match1);
		flow_rule_match_enc_control(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_ENC_IP)) {
		struct flow_match_ip match1, match2;

		flow_rule_match_enc_ip(entry1->rule, &match1);
		flow_rule_match_enc_ip(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_ENC_OPTS)) {
		struct flow_match_enc_opts match1, match2;

		flow_rule_match_enc_opts(entry1->rule, &match1);
		flow_rule_match_enc_opts(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	return 0;

check_failed:
	return -EINVAL;
}

static int nfp_ct_merge_act_check(struct nfp_fl_ct_flow_entry *pre_ct_entry,
				  struct nfp_fl_ct_flow_entry *post_ct_entry,
				  struct nfp_fl_ct_flow_entry *nft_entry)
{
	struct flow_action_entry *act;
	int i;

	/* Check for pre_ct->action conflicts */
	flow_action_for_each(i, act, &pre_ct_entry->rule->action) {
		switch (act->id) {
		case FLOW_ACTION_VLAN_PUSH:
		case FLOW_ACTION_VLAN_POP:
		case FLOW_ACTION_VLAN_MANGLE:
		case FLOW_ACTION_MPLS_PUSH:
		case FLOW_ACTION_MPLS_POP:
		case FLOW_ACTION_MPLS_MANGLE:
			return -EOPNOTSUPP;
		default:
			break;
		}
	}

	/* Check for nft->action conflicts */
	flow_action_for_each(i, act, &nft_entry->rule->action) {
		switch (act->id) {
		case FLOW_ACTION_VLAN_PUSH:
		case FLOW_ACTION_VLAN_POP:
		case FLOW_ACTION_VLAN_MANGLE:
		case FLOW_ACTION_MPLS_PUSH:
		case FLOW_ACTION_MPLS_POP:
		case FLOW_ACTION_MPLS_MANGLE:
			return -EOPNOTSUPP;
		default:
			break;
		}
	}
	return 0;
}

static int nfp_ct_check_meta(struct nfp_fl_ct_flow_entry *post_ct_entry,
			     struct nfp_fl_ct_flow_entry *nft_entry)
{
	struct flow_dissector *dissector = post_ct_entry->rule->match.dissector;
	struct flow_action_entry *ct_met;
	struct flow_match_ct ct;
	int i;

	ct_met = get_flow_act(nft_entry->rule, FLOW_ACTION_CT_METADATA);
	if (ct_met && (dissector->used_keys & BIT(FLOW_DISSECTOR_KEY_CT))) {
		u32 *act_lbl;

		act_lbl = ct_met->ct_metadata.labels;
		flow_rule_match_ct(post_ct_entry->rule, &ct);
		for (i = 0; i < 4; i++) {
			if ((ct.key->ct_labels[i] & ct.mask->ct_labels[i]) ^
			    (act_lbl[i] & ct.mask->ct_labels[i]))
				return -EINVAL;
		}

		if ((ct.key->ct_mark & ct.mask->ct_mark) ^
		    (ct_met->ct_metadata.mark & ct.mask->ct_mark))
			return -EINVAL;

		return 0;
	}

	return -EINVAL;
}

static int
nfp_fl_calc_key_layers_sz(struct nfp_fl_key_ls in_key_ls, uint16_t *map)
{
	int key_size;

	/* This field must always be present */
	key_size = sizeof(struct nfp_flower_meta_tci);
	map[FLOW_PAY_META_TCI] = 0;

	if (in_key_ls.key_layer & NFP_FLOWER_LAYER_EXT_META) {
		map[FLOW_PAY_EXT_META] = key_size;
		key_size += sizeof(struct nfp_flower_ext_meta);
	}
	if (in_key_ls.key_layer & NFP_FLOWER_LAYER_PORT) {
		map[FLOW_PAY_INPORT] = key_size;
		key_size += sizeof(struct nfp_flower_in_port);
	}
	if (in_key_ls.key_layer & NFP_FLOWER_LAYER_MAC) {
		map[FLOW_PAY_MAC_MPLS] = key_size;
		key_size += sizeof(struct nfp_flower_mac_mpls);
	}
	if (in_key_ls.key_layer & NFP_FLOWER_LAYER_TP) {
		map[FLOW_PAY_L4] = key_size;
		key_size += sizeof(struct nfp_flower_tp_ports);
	}
	if (in_key_ls.key_layer & NFP_FLOWER_LAYER_IPV4) {
		map[FLOW_PAY_IPV4] = key_size;
		key_size += sizeof(struct nfp_flower_ipv4);
	}
	if (in_key_ls.key_layer & NFP_FLOWER_LAYER_IPV6) {
		map[FLOW_PAY_IPV6] = key_size;
		key_size += sizeof(struct nfp_flower_ipv6);
	}

	if (in_key_ls.key_layer_two & NFP_FLOWER_LAYER2_QINQ) {
		map[FLOW_PAY_QINQ] = key_size;
		key_size += sizeof(struct nfp_flower_vlan);
	}

	if (in_key_ls.key_layer_two & NFP_FLOWER_LAYER2_GRE) {
		map[FLOW_PAY_GRE] = key_size;
		if (in_key_ls.key_layer_two & NFP_FLOWER_LAYER2_TUN_IPV6)
			key_size += sizeof(struct nfp_flower_ipv6_gre_tun);
		else
			key_size += sizeof(struct nfp_flower_ipv4_gre_tun);
	}

	if ((in_key_ls.key_layer & NFP_FLOWER_LAYER_VXLAN) ||
	    (in_key_ls.key_layer_two & NFP_FLOWER_LAYER2_GENEVE)) {
		map[FLOW_PAY_UDP_TUN] = key_size;
		if (in_key_ls.key_layer_two & NFP_FLOWER_LAYER2_TUN_IPV6)
			key_size += sizeof(struct nfp_flower_ipv6_udp_tun);
		else
			key_size += sizeof(struct nfp_flower_ipv4_udp_tun);
	}

	if (in_key_ls.key_layer_two & NFP_FLOWER_LAYER2_GENEVE_OP) {
		map[FLOW_PAY_GENEVE_OPT] = key_size;
		key_size += sizeof(struct nfp_flower_geneve_options);
	}

	return key_size;
}

static int nfp_fl_merge_actions_offload(struct flow_rule **rules,
					struct nfp_flower_priv *priv,
					struct net_device *netdev,
					struct nfp_fl_payload *flow_pay)
{
	struct flow_action_entry *a_in;
	int i, j, num_actions, id;
	struct flow_rule *a_rule;
	int err = 0, offset = 0;

	num_actions = rules[CT_TYPE_PRE_CT]->action.num_entries +
		      rules[CT_TYPE_NFT]->action.num_entries +
		      rules[CT_TYPE_POST_CT]->action.num_entries;

	a_rule = flow_rule_alloc(num_actions);
	if (!a_rule)
		return -ENOMEM;

	/* Actions need a BASIC dissector. */
	a_rule->match = rules[CT_TYPE_PRE_CT]->match;

	/* Copy actions */
	for (j = 0; j < _CT_TYPE_MAX; j++) {
		if (flow_rule_match_key(rules[j], FLOW_DISSECTOR_KEY_BASIC)) {
			struct flow_match_basic match;

			/* ip_proto is the only field that is needed in later compile_action,
			 * needed to set the correct checksum flags. It doesn't really matter
			 * which input rule's ip_proto field we take as the earlier merge checks
			 * would have made sure that they don't conflict. We do not know which
			 * of the subflows would have the ip_proto filled in, so we need to iterate
			 * through the subflows and assign the proper subflow to a_rule
			 */
			flow_rule_match_basic(rules[j], &match);
			if (match.mask->ip_proto)
				a_rule->match = rules[j]->match;
		}

		for (i = 0; i < rules[j]->action.num_entries; i++) {
			a_in = &rules[j]->action.entries[i];
			id = a_in->id;

			/* Ignore CT related actions as these would already have
			 * been taken care of by previous checks, and we do not send
			 * any CT actions to the firmware.
			 */
			switch (id) {
			case FLOW_ACTION_CT:
			case FLOW_ACTION_GOTO:
			case FLOW_ACTION_CT_METADATA:
				continue;
			default:
				memcpy(&a_rule->action.entries[offset++],
				       a_in, sizeof(struct flow_action_entry));
				break;
			}
		}
	}

	/* Some actions would have been ignored, so update the num_entries field */
	a_rule->action.num_entries = offset;
	err = nfp_flower_compile_action(priv->app, a_rule, netdev, flow_pay, NULL);
	kfree(a_rule);

	return err;
}

static int nfp_fl_ct_add_offload(struct nfp_fl_nft_tc_merge *m_entry)
{
	enum nfp_flower_tun_type tun_type = NFP_FL_TUNNEL_NONE;
	struct nfp_fl_ct_zone_entry *zt = m_entry->zt;
	struct nfp_fl_key_ls key_layer, tmp_layer;
	struct nfp_flower_priv *priv = zt->priv;
	u16 key_map[_FLOW_PAY_LAYERS_MAX];
	struct nfp_fl_payload *flow_pay;

	struct flow_rule *rules[_CT_TYPE_MAX];
	u8 *key, *msk, *kdata, *mdata;
	struct nfp_port *port = NULL;
	struct net_device *netdev;
	bool qinq_sup;
	u32 port_id;
	u16 offset;
	int i, err;

	netdev = m_entry->netdev;
	qinq_sup = !!(priv->flower_ext_feats & NFP_FL_FEATS_VLAN_QINQ);

	rules[CT_TYPE_PRE_CT] = m_entry->tc_m_parent->pre_ct_parent->rule;
	rules[CT_TYPE_NFT] = m_entry->nft_parent->rule;
	rules[CT_TYPE_POST_CT] = m_entry->tc_m_parent->post_ct_parent->rule;

	memset(&key_layer, 0, sizeof(struct nfp_fl_key_ls));
	memset(&key_map, 0, sizeof(key_map));

	/* Calculate the resultant key layer and size for offload */
	for (i = 0; i < _CT_TYPE_MAX; i++) {
		err = nfp_flower_calculate_key_layers(priv->app,
						      m_entry->netdev,
						      &tmp_layer, rules[i],
						      &tun_type, NULL);
		if (err)
			return err;

		key_layer.key_layer |= tmp_layer.key_layer;
		key_layer.key_layer_two |= tmp_layer.key_layer_two;
	}
	key_layer.key_size = nfp_fl_calc_key_layers_sz(key_layer, key_map);

	flow_pay = nfp_flower_allocate_new(&key_layer);
	if (!flow_pay)
		return -ENOMEM;

	memset(flow_pay->unmasked_data, 0, key_layer.key_size);
	memset(flow_pay->mask_data, 0, key_layer.key_size);

	kdata = flow_pay->unmasked_data;
	mdata = flow_pay->mask_data;

	offset = key_map[FLOW_PAY_META_TCI];
	key = kdata + offset;
	msk = mdata + offset;
	nfp_flower_compile_meta((struct nfp_flower_meta_tci *)key,
				(struct nfp_flower_meta_tci *)msk,
				key_layer.key_layer);

	if (NFP_FLOWER_LAYER_EXT_META & key_layer.key_layer) {
		offset =  key_map[FLOW_PAY_EXT_META];
		key = kdata + offset;
		msk = mdata + offset;
		nfp_flower_compile_ext_meta((struct nfp_flower_ext_meta *)key,
					    key_layer.key_layer_two);
		nfp_flower_compile_ext_meta((struct nfp_flower_ext_meta *)msk,
					    key_layer.key_layer_two);
	}

	/* Using in_port from the -trk rule. The tc merge checks should already
	 * be checking that the ingress netdevs are the same
	 */
	port_id = nfp_flower_get_port_id_from_netdev(priv->app, netdev);
	offset = key_map[FLOW_PAY_INPORT];
	key = kdata + offset;
	msk = mdata + offset;
	err = nfp_flower_compile_port((struct nfp_flower_in_port *)key,
				      port_id, false, tun_type, NULL);
	if (err)
		goto ct_offload_err;
	err = nfp_flower_compile_port((struct nfp_flower_in_port *)msk,
				      port_id, true, tun_type, NULL);
	if (err)
		goto ct_offload_err;

	/* This following part works on the assumption that previous checks has
	 * already filtered out flows that has different values for the different
	 * layers. Here we iterate through all three rules and merge their respective
	 * masked value(cared bits), basic method is:
	 * final_key = (r1_key & r1_mask) | (r2_key & r2_mask) | (r3_key & r3_mask)
	 * final_mask = r1_mask | r2_mask | r3_mask
	 * If none of the rules contains a match that is also fine, that simply means
	 * that the layer is not present.
	 */
	if (!qinq_sup) {
		for (i = 0; i < _CT_TYPE_MAX; i++) {
			offset = key_map[FLOW_PAY_META_TCI];
			key = kdata + offset;
			msk = mdata + offset;
			nfp_flower_compile_tci((struct nfp_flower_meta_tci *)key,
					       (struct nfp_flower_meta_tci *)msk,
					       rules[i]);
		}
	}

	if (NFP_FLOWER_LAYER_MAC & key_layer.key_layer) {
		offset = key_map[FLOW_PAY_MAC_MPLS];
		key = kdata + offset;
		msk = mdata + offset;
		for (i = 0; i < _CT_TYPE_MAX; i++) {
			nfp_flower_compile_mac((struct nfp_flower_mac_mpls *)key,
					       (struct nfp_flower_mac_mpls *)msk,
					       rules[i]);
			err = nfp_flower_compile_mpls((struct nfp_flower_mac_mpls *)key,
						      (struct nfp_flower_mac_mpls *)msk,
						      rules[i], NULL);
			if (err)
				goto ct_offload_err;
		}
	}

	if (NFP_FLOWER_LAYER_IPV4 & key_layer.key_layer) {
		offset = key_map[FLOW_PAY_IPV4];
		key = kdata + offset;
		msk = mdata + offset;
		for (i = 0; i < _CT_TYPE_MAX; i++) {
			nfp_flower_compile_ipv4((struct nfp_flower_ipv4 *)key,
						(struct nfp_flower_ipv4 *)msk,
						rules[i]);
		}
	}

	if (NFP_FLOWER_LAYER_IPV6 & key_layer.key_layer) {
		offset = key_map[FLOW_PAY_IPV6];
		key = kdata + offset;
		msk = mdata + offset;
		for (i = 0; i < _CT_TYPE_MAX; i++) {
			nfp_flower_compile_ipv6((struct nfp_flower_ipv6 *)key,
						(struct nfp_flower_ipv6 *)msk,
						rules[i]);
		}
	}

	if (NFP_FLOWER_LAYER_TP & key_layer.key_layer) {
		offset = key_map[FLOW_PAY_L4];
		key = kdata + offset;
		msk = mdata + offset;
		for (i = 0; i < _CT_TYPE_MAX; i++) {
			nfp_flower_compile_tport((struct nfp_flower_tp_ports *)key,
						 (struct nfp_flower_tp_ports *)msk,
						 rules[i]);
		}
	}

	if (NFP_FLOWER_LAYER2_QINQ & key_layer.key_layer_two) {
		offset = key_map[FLOW_PAY_QINQ];
		key = kdata + offset;
		msk = mdata + offset;
		for (i = 0; i < _CT_TYPE_MAX; i++) {
			nfp_flower_compile_vlan((struct nfp_flower_vlan *)key,
						(struct nfp_flower_vlan *)msk,
						rules[i]);
		}
	}

	if (key_layer.key_layer_two & NFP_FLOWER_LAYER2_GRE) {
		offset = key_map[FLOW_PAY_GRE];
		key = kdata + offset;
		msk = mdata + offset;
		if (key_layer.key_layer_two & NFP_FLOWER_LAYER2_TUN_IPV6) {
			struct nfp_flower_ipv6_gre_tun *gre_match;
			struct nfp_ipv6_addr_entry *entry;
			struct in6_addr *dst;

			for (i = 0; i < _CT_TYPE_MAX; i++) {
				nfp_flower_compile_ipv6_gre_tun((void *)key,
								(void *)msk, rules[i]);
			}
			gre_match = (struct nfp_flower_ipv6_gre_tun *)key;
			dst = &gre_match->ipv6.dst;

			entry = nfp_tunnel_add_ipv6_off(priv->app, dst);
			if (!entry) {
				err = -ENOMEM;
				goto ct_offload_err;
			}

			flow_pay->nfp_tun_ipv6 = entry;
		} else {
			__be32 dst;

			for (i = 0; i < _CT_TYPE_MAX; i++) {
				nfp_flower_compile_ipv4_gre_tun((void *)key,
								(void *)msk, rules[i]);
			}
			dst = ((struct nfp_flower_ipv4_gre_tun *)key)->ipv4.dst;

			/* Store the tunnel destination in the rule data.
			 * This must be present and be an exact match.
			 */
			flow_pay->nfp_tun_ipv4_addr = dst;
			nfp_tunnel_add_ipv4_off(priv->app, dst);
		}
	}

	if (key_layer.key_layer & NFP_FLOWER_LAYER_VXLAN ||
	    key_layer.key_layer_two & NFP_FLOWER_LAYER2_GENEVE) {
		offset = key_map[FLOW_PAY_UDP_TUN];
		key = kdata + offset;
		msk = mdata + offset;
		if (key_layer.key_layer_two & NFP_FLOWER_LAYER2_TUN_IPV6) {
			struct nfp_flower_ipv6_udp_tun *udp_match;
			struct nfp_ipv6_addr_entry *entry;
			struct in6_addr *dst;

			for (i = 0; i < _CT_TYPE_MAX; i++) {
				nfp_flower_compile_ipv6_udp_tun((void *)key,
								(void *)msk, rules[i]);
			}
			udp_match = (struct nfp_flower_ipv6_udp_tun *)key;
			dst = &udp_match->ipv6.dst;

			entry = nfp_tunnel_add_ipv6_off(priv->app, dst);
			if (!entry) {
				err = -ENOMEM;
				goto ct_offload_err;
			}

			flow_pay->nfp_tun_ipv6 = entry;
		} else {
			__be32 dst;

			for (i = 0; i < _CT_TYPE_MAX; i++) {
				nfp_flower_compile_ipv4_udp_tun((void *)key,
								(void *)msk, rules[i]);
			}
			dst = ((struct nfp_flower_ipv4_udp_tun *)key)->ipv4.dst;

			/* Store the tunnel destination in the rule data.
			 * This must be present and be an exact match.
			 */
			flow_pay->nfp_tun_ipv4_addr = dst;
			nfp_tunnel_add_ipv4_off(priv->app, dst);
		}

		if (key_layer.key_layer_two & NFP_FLOWER_LAYER2_GENEVE_OP) {
			offset = key_map[FLOW_PAY_GENEVE_OPT];
			key = kdata + offset;
			msk = mdata + offset;
			for (i = 0; i < _CT_TYPE_MAX; i++)
				nfp_flower_compile_geneve_opt(key, msk, rules[i]);
		}
	}

	/* Merge actions into flow_pay */
	err = nfp_fl_merge_actions_offload(rules, priv, netdev, flow_pay);
	if (err)
		goto ct_offload_err;

	/* Use the pointer address as the cookie, but set the last bit to 1.
	 * This is to avoid the 'is_merge_flow' check from detecting this as
	 * an already merged flow. This works since address alignment means
	 * that the last bit for pointer addresses will be 0.
	 */
	flow_pay->tc_flower_cookie = ((unsigned long)flow_pay) | 0x1;
	err = nfp_compile_flow_metadata(priv->app, flow_pay->tc_flower_cookie,
					flow_pay, netdev, NULL);
	if (err)
		goto ct_offload_err;

	if (nfp_netdev_is_nfp_repr(netdev))
		port = nfp_port_from_netdev(netdev);

	err = rhashtable_insert_fast(&priv->flow_table, &flow_pay->fl_node,
				     nfp_flower_table_params);
	if (err)
		goto ct_release_offload_meta_err;

	err = nfp_flower_xmit_flow(priv->app, flow_pay,
				   NFP_FLOWER_CMSG_TYPE_FLOW_ADD);
	if (err)
		goto ct_remove_rhash_err;

	m_entry->tc_flower_cookie = flow_pay->tc_flower_cookie;
	m_entry->flow_pay = flow_pay;

	if (port)
		port->tc_offload_cnt++;

	return err;

ct_remove_rhash_err:
	WARN_ON_ONCE(rhashtable_remove_fast(&priv->flow_table,
					    &flow_pay->fl_node,
					    nfp_flower_table_params));
ct_release_offload_meta_err:
	nfp_modify_flow_metadata(priv->app, flow_pay);
ct_offload_err:
	if (flow_pay->nfp_tun_ipv4_addr)
		nfp_tunnel_del_ipv4_off(priv->app, flow_pay->nfp_tun_ipv4_addr);
	if (flow_pay->nfp_tun_ipv6)
		nfp_tunnel_put_ipv6_off(priv->app, flow_pay->nfp_tun_ipv6);
	kfree(flow_pay->action_data);
	kfree(flow_pay->mask_data);
	kfree(flow_pay->unmasked_data);
	kfree(flow_pay);
	return err;
}

static int nfp_fl_ct_del_offload(struct nfp_app *app, unsigned long cookie,
				 struct net_device *netdev)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_payload *flow_pay;
	struct nfp_port *port = NULL;
	int err = 0;

	if (nfp_netdev_is_nfp_repr(netdev))
		port = nfp_port_from_netdev(netdev);

	flow_pay = nfp_flower_search_fl_table(app, cookie, netdev);
	if (!flow_pay)
		return -ENOENT;

	err = nfp_modify_flow_metadata(app, flow_pay);
	if (err)
		goto err_free_merge_flow;

	if (flow_pay->nfp_tun_ipv4_addr)
		nfp_tunnel_del_ipv4_off(app, flow_pay->nfp_tun_ipv4_addr);

	if (flow_pay->nfp_tun_ipv6)
		nfp_tunnel_put_ipv6_off(app, flow_pay->nfp_tun_ipv6);

	if (!flow_pay->in_hw) {
		err = 0;
		goto err_free_merge_flow;
	}

	err = nfp_flower_xmit_flow(app, flow_pay,
				   NFP_FLOWER_CMSG_TYPE_FLOW_DEL);

err_free_merge_flow:
	nfp_flower_del_linked_merge_flows(app, flow_pay);
	if (port)
		port->tc_offload_cnt--;
	kfree(flow_pay->action_data);
	kfree(flow_pay->mask_data);
	kfree(flow_pay->unmasked_data);
	WARN_ON_ONCE(rhashtable_remove_fast(&priv->flow_table,
					    &flow_pay->fl_node,
					    nfp_flower_table_params));
	kfree_rcu(flow_pay, rcu);
	return err;
}

static int nfp_ct_do_nft_merge(struct nfp_fl_ct_zone_entry *zt,
			       struct nfp_fl_ct_flow_entry *nft_entry,
			       struct nfp_fl_ct_tc_merge *tc_m_entry)
{
	struct nfp_fl_ct_flow_entry *post_ct_entry, *pre_ct_entry;
	struct nfp_fl_nft_tc_merge *nft_m_entry;
	unsigned long new_cookie[3];
	int err;

	pre_ct_entry = tc_m_entry->pre_ct_parent;
	post_ct_entry = tc_m_entry->post_ct_parent;

	err = nfp_ct_merge_act_check(pre_ct_entry, post_ct_entry, nft_entry);
	if (err)
		return err;

	/* Check that the two tc flows are also compatible with
	 * the nft entry. No need to check the pre_ct and post_ct
	 * entries as that was already done during pre_merge.
	 * The nft entry does not have a chain populated, so
	 * skip this check.
	 */
	err = nfp_ct_merge_check(pre_ct_entry, nft_entry);
	if (err)
		return err;
	err = nfp_ct_merge_check(nft_entry, post_ct_entry);
	if (err)
		return err;
	err = nfp_ct_check_meta(post_ct_entry, nft_entry);
	if (err)
		return err;

	/* Combine tc_merge and nft cookies for this cookie. */
	new_cookie[0] = tc_m_entry->cookie[0];
	new_cookie[1] = tc_m_entry->cookie[1];
	new_cookie[2] = nft_entry->cookie;
	nft_m_entry = get_hashentry(&zt->nft_merge_tb,
				    &new_cookie,
				    nfp_nft_ct_merge_params,
				    sizeof(*nft_m_entry));

	if (IS_ERR(nft_m_entry))
		return PTR_ERR(nft_m_entry);

	/* nft_m_entry already present, not merging again */
	if (!memcmp(&new_cookie, nft_m_entry->cookie, sizeof(new_cookie)))
		return 0;

	memcpy(&nft_m_entry->cookie, &new_cookie, sizeof(new_cookie));
	nft_m_entry->zt = zt;
	nft_m_entry->tc_m_parent = tc_m_entry;
	nft_m_entry->nft_parent = nft_entry;
	nft_m_entry->tc_flower_cookie = 0;
	/* Copy the netdev from the pre_ct entry. When the tc_m_entry was created
	 * it only combined them if the netdevs were the same, so can use any of them.
	 */
	nft_m_entry->netdev = pre_ct_entry->netdev;

	/* Add this entry to the tc_m_list and nft_flow lists */
	list_add(&nft_m_entry->tc_merge_list, &tc_m_entry->children);
	list_add(&nft_m_entry->nft_flow_list, &nft_entry->children);

	/* Generate offload structure and send to nfp */
	err = nfp_fl_ct_add_offload(nft_m_entry);
	if (err)
		goto err_nft_ct_offload;

	err = rhashtable_insert_fast(&zt->nft_merge_tb, &nft_m_entry->hash_node,
				     nfp_nft_ct_merge_params);
	if (err)
		goto err_nft_ct_merge_insert;

	zt->nft_merge_count++;

	return err;

err_nft_ct_merge_insert:
	nfp_fl_ct_del_offload(zt->priv->app, nft_m_entry->tc_flower_cookie,
			      nft_m_entry->netdev);
err_nft_ct_offload:
	list_del(&nft_m_entry->tc_merge_list);
	list_del(&nft_m_entry->nft_flow_list);
	kfree(nft_m_entry);
	return err;
}

static int nfp_ct_do_tc_merge(struct nfp_fl_ct_zone_entry *zt,
			      struct nfp_fl_ct_flow_entry *ct_entry1,
			      struct nfp_fl_ct_flow_entry *ct_entry2)
{
	struct nfp_fl_ct_flow_entry *post_ct_entry, *pre_ct_entry;
	struct nfp_fl_ct_flow_entry *nft_entry, *nft_tmp;
	struct nfp_fl_ct_tc_merge *m_entry;
	unsigned long new_cookie[2];
	int err;

	if (ct_entry1->type == CT_TYPE_PRE_CT) {
		pre_ct_entry = ct_entry1;
		post_ct_entry = ct_entry2;
	} else {
		post_ct_entry = ct_entry1;
		pre_ct_entry = ct_entry2;
	}

	/* Checks that the chain_index of the filter matches the
	 * chain_index of the GOTO action.
	 */
	if (post_ct_entry->chain_index != pre_ct_entry->chain_index)
		return -EINVAL;

	err = nfp_ct_merge_check(pre_ct_entry, post_ct_entry);
	if (err)
		return err;

	new_cookie[0] = pre_ct_entry->cookie;
	new_cookie[1] = post_ct_entry->cookie;
	m_entry = get_hashentry(&zt->tc_merge_tb, &new_cookie,
				nfp_tc_ct_merge_params, sizeof(*m_entry));
	if (IS_ERR(m_entry))
		return PTR_ERR(m_entry);

	/* m_entry already present, not merging again */
	if (!memcmp(&new_cookie, m_entry->cookie, sizeof(new_cookie)))
		return 0;

	memcpy(&m_entry->cookie, &new_cookie, sizeof(new_cookie));
	m_entry->zt = zt;
	m_entry->post_ct_parent = post_ct_entry;
	m_entry->pre_ct_parent = pre_ct_entry;

	/* Add this entry to the pre_ct and post_ct lists */
	list_add(&m_entry->post_ct_list, &post_ct_entry->children);
	list_add(&m_entry->pre_ct_list, &pre_ct_entry->children);
	INIT_LIST_HEAD(&m_entry->children);

	err = rhashtable_insert_fast(&zt->tc_merge_tb, &m_entry->hash_node,
				     nfp_tc_ct_merge_params);
	if (err)
		goto err_ct_tc_merge_insert;
	zt->tc_merge_count++;

	/* Merge with existing nft flows */
	list_for_each_entry_safe(nft_entry, nft_tmp, &zt->nft_flows_list,
				 list_node) {
		nfp_ct_do_nft_merge(zt, nft_entry, m_entry);
	}

	return 0;

err_ct_tc_merge_insert:
	list_del(&m_entry->post_ct_list);
	list_del(&m_entry->pre_ct_list);
	kfree(m_entry);
	return err;
}

static struct
nfp_fl_ct_zone_entry *get_nfp_zone_entry(struct nfp_flower_priv *priv,
					 u16 zone, bool wildcarded)
{
	struct nfp_fl_ct_zone_entry *zt;
	int err;

	if (wildcarded && priv->ct_zone_wc)
		return priv->ct_zone_wc;

	if (!wildcarded) {
		zt = get_hashentry(&priv->ct_zone_table, &zone,
				   nfp_zone_table_params, sizeof(*zt));

		/* If priv is set this is an existing entry, just return it */
		if (IS_ERR(zt) || zt->priv)
			return zt;
	} else {
		zt = kzalloc(sizeof(*zt), GFP_KERNEL);
		if (!zt)
			return ERR_PTR(-ENOMEM);
	}

	zt->zone = zone;
	zt->priv = priv;
	zt->nft = NULL;

	/* init the various hash tables and lists */
	INIT_LIST_HEAD(&zt->pre_ct_list);
	INIT_LIST_HEAD(&zt->post_ct_list);
	INIT_LIST_HEAD(&zt->nft_flows_list);

	err = rhashtable_init(&zt->tc_merge_tb, &nfp_tc_ct_merge_params);
	if (err)
		goto err_tc_merge_tb_init;

	err = rhashtable_init(&zt->nft_merge_tb, &nfp_nft_ct_merge_params);
	if (err)
		goto err_nft_merge_tb_init;

	if (wildcarded) {
		priv->ct_zone_wc = zt;
	} else {
		err = rhashtable_insert_fast(&priv->ct_zone_table,
					     &zt->hash_node,
					     nfp_zone_table_params);
		if (err)
			goto err_zone_insert;
	}

	return zt;

err_zone_insert:
	rhashtable_destroy(&zt->nft_merge_tb);
err_nft_merge_tb_init:
	rhashtable_destroy(&zt->tc_merge_tb);
err_tc_merge_tb_init:
	kfree(zt);
	return ERR_PTR(err);
}

static struct net_device *get_netdev_from_rule(struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_META)) {
		struct flow_match_meta match;

		flow_rule_match_meta(rule, &match);
		if (match.key->ingress_ifindex & match.mask->ingress_ifindex)
			return __dev_get_by_index(&init_net,
						  match.key->ingress_ifindex);
	}

	return NULL;
}

static struct
nfp_fl_ct_flow_entry *nfp_fl_ct_add_flow(struct nfp_fl_ct_zone_entry *zt,
					 struct net_device *netdev,
					 struct flow_cls_offload *flow,
					 bool is_nft, struct netlink_ext_ack *extack)
{
	struct nf_flow_match *nft_match = NULL;
	struct nfp_fl_ct_flow_entry *entry;
	struct nfp_fl_ct_map_entry *map;
	struct flow_action_entry *act;
	int err, i;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	entry->rule = flow_rule_alloc(flow->rule->action.num_entries);
	if (!entry->rule) {
		err = -ENOMEM;
		goto err_pre_ct_rule;
	}

	/* nft flows gets destroyed after callback return, so need
	 * to do a full copy instead of just a reference.
	 */
	if (is_nft) {
		nft_match = kzalloc(sizeof(*nft_match), GFP_KERNEL);
		if (!nft_match) {
			err = -ENOMEM;
			goto err_pre_ct_act;
		}
		memcpy(&nft_match->dissector, flow->rule->match.dissector,
		       sizeof(nft_match->dissector));
		memcpy(&nft_match->mask, flow->rule->match.mask,
		       sizeof(nft_match->mask));
		memcpy(&nft_match->key, flow->rule->match.key,
		       sizeof(nft_match->key));
		entry->rule->match.dissector = &nft_match->dissector;
		entry->rule->match.mask = &nft_match->mask;
		entry->rule->match.key = &nft_match->key;

		if (!netdev)
			netdev = get_netdev_from_rule(entry->rule);
	} else {
		entry->rule->match.dissector = flow->rule->match.dissector;
		entry->rule->match.mask = flow->rule->match.mask;
		entry->rule->match.key = flow->rule->match.key;
	}

	entry->zt = zt;
	entry->netdev = netdev;
	entry->cookie = flow->cookie;
	entry->chain_index = flow->common.chain_index;
	entry->tun_offset = NFP_FL_CT_NO_TUN;

	/* Copy over action data. Unfortunately we do not get a handle to the
	 * original tcf_action data, and the flow objects gets destroyed, so we
	 * cannot just save a pointer to this either, so need to copy over the
	 * data unfortunately.
	 */
	entry->rule->action.num_entries = flow->rule->action.num_entries;
	flow_action_for_each(i, act, &flow->rule->action) {
		struct flow_action_entry *new_act;

		new_act = &entry->rule->action.entries[i];
		memcpy(new_act, act, sizeof(struct flow_action_entry));
		/* Entunnel is a special case, need to allocate and copy
		 * tunnel info.
		 */
		if (act->id == FLOW_ACTION_TUNNEL_ENCAP) {
			struct ip_tunnel_info *tun = act->tunnel;
			size_t tun_size = sizeof(*tun) + tun->options_len;

			new_act->tunnel = kmemdup(tun, tun_size, GFP_ATOMIC);
			if (!new_act->tunnel) {
				err = -ENOMEM;
				goto err_pre_ct_tun_cp;
			}
			entry->tun_offset = i;
		}
	}

	INIT_LIST_HEAD(&entry->children);

	/* Now add a ct map entry to flower-priv */
	map = get_hashentry(&zt->priv->ct_map_table, &flow->cookie,
			    nfp_ct_map_params, sizeof(*map));
	if (IS_ERR(map)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "offload error: ct map entry creation failed");
		err = -ENOMEM;
		goto err_ct_flow_insert;
	}
	map->cookie = flow->cookie;
	map->ct_entry = entry;
	err = rhashtable_insert_fast(&zt->priv->ct_map_table,
				     &map->hash_node,
				     nfp_ct_map_params);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "offload error: ct map entry table add failed");
		goto err_map_insert;
	}

	return entry;

err_map_insert:
	kfree(map);
err_ct_flow_insert:
	if (entry->tun_offset != NFP_FL_CT_NO_TUN)
		kfree(entry->rule->action.entries[entry->tun_offset].tunnel);
err_pre_ct_tun_cp:
	kfree(nft_match);
err_pre_ct_act:
	kfree(entry->rule);
err_pre_ct_rule:
	kfree(entry);
	return ERR_PTR(err);
}

static void cleanup_nft_merge_entry(struct nfp_fl_nft_tc_merge *m_entry)
{
	struct nfp_fl_ct_zone_entry *zt;
	int err;

	zt = m_entry->zt;

	/* Flow is in HW, need to delete */
	if (m_entry->tc_flower_cookie) {
		err = nfp_fl_ct_del_offload(zt->priv->app, m_entry->tc_flower_cookie,
					    m_entry->netdev);
		if (err)
			return;
	}

	WARN_ON_ONCE(rhashtable_remove_fast(&zt->nft_merge_tb,
					    &m_entry->hash_node,
					    nfp_nft_ct_merge_params));
	zt->nft_merge_count--;
	list_del(&m_entry->tc_merge_list);
	list_del(&m_entry->nft_flow_list);

	kfree(m_entry);
}

static void nfp_free_nft_merge_children(void *entry, bool is_nft_flow)
{
	struct nfp_fl_nft_tc_merge *m_entry, *tmp;

	/* These post entries are parts of two lists, one is a list of nft_entries
	 * and the other is of from a list of tc_merge structures. Iterate
	 * through the relevant list and cleanup the entries.
	 */

	if (is_nft_flow) {
		/* Need to iterate through list of nft_flow entries */
		struct nfp_fl_ct_flow_entry *ct_entry = entry;

		list_for_each_entry_safe(m_entry, tmp, &ct_entry->children,
					 nft_flow_list) {
			cleanup_nft_merge_entry(m_entry);
		}
	} else {
		/* Need to iterate through list of tc_merged_flow entries */
		struct nfp_fl_ct_tc_merge *ct_entry = entry;

		list_for_each_entry_safe(m_entry, tmp, &ct_entry->children,
					 tc_merge_list) {
			cleanup_nft_merge_entry(m_entry);
		}
	}
}

static void nfp_del_tc_merge_entry(struct nfp_fl_ct_tc_merge *m_ent)
{
	struct nfp_fl_ct_zone_entry *zt;
	int err;

	zt = m_ent->zt;
	err = rhashtable_remove_fast(&zt->tc_merge_tb,
				     &m_ent->hash_node,
				     nfp_tc_ct_merge_params);
	if (err)
		pr_warn("WARNING: could not remove merge_entry from hashtable\n");
	zt->tc_merge_count--;
	list_del(&m_ent->post_ct_list);
	list_del(&m_ent->pre_ct_list);

	if (!list_empty(&m_ent->children))
		nfp_free_nft_merge_children(m_ent, false);
	kfree(m_ent);
}

static void nfp_free_tc_merge_children(struct nfp_fl_ct_flow_entry *entry)
{
	struct nfp_fl_ct_tc_merge *m_ent, *tmp;

	switch (entry->type) {
	case CT_TYPE_PRE_CT:
		list_for_each_entry_safe(m_ent, tmp, &entry->children, pre_ct_list) {
			nfp_del_tc_merge_entry(m_ent);
		}
		break;
	case CT_TYPE_POST_CT:
		list_for_each_entry_safe(m_ent, tmp, &entry->children, post_ct_list) {
			nfp_del_tc_merge_entry(m_ent);
		}
		break;
	default:
		break;
	}
}

void nfp_fl_ct_clean_flow_entry(struct nfp_fl_ct_flow_entry *entry)
{
	list_del(&entry->list_node);

	if (!list_empty(&entry->children)) {
		if (entry->type == CT_TYPE_NFT)
			nfp_free_nft_merge_children(entry, true);
		else
			nfp_free_tc_merge_children(entry);
	}

	if (entry->tun_offset != NFP_FL_CT_NO_TUN)
		kfree(entry->rule->action.entries[entry->tun_offset].tunnel);

	if (entry->type == CT_TYPE_NFT) {
		struct nf_flow_match *nft_match;

		nft_match = container_of(entry->rule->match.dissector,
					 struct nf_flow_match, dissector);
		kfree(nft_match);
	}

	kfree(entry->rule);
	kfree(entry);
}

static struct flow_action_entry *get_flow_act(struct flow_rule *rule,
					      enum flow_action_id act_id)
{
	struct flow_action_entry *act = NULL;
	int i;

	flow_action_for_each(i, act, &rule->action) {
		if (act->id == act_id)
			return act;
	}
	return NULL;
}

static void
nfp_ct_merge_tc_entries(struct nfp_fl_ct_flow_entry *ct_entry1,
			struct nfp_fl_ct_zone_entry *zt_src,
			struct nfp_fl_ct_zone_entry *zt_dst)
{
	struct nfp_fl_ct_flow_entry *ct_entry2, *ct_tmp;
	struct list_head *ct_list;

	if (ct_entry1->type == CT_TYPE_PRE_CT)
		ct_list = &zt_src->post_ct_list;
	else if (ct_entry1->type == CT_TYPE_POST_CT)
		ct_list = &zt_src->pre_ct_list;
	else
		return;

	list_for_each_entry_safe(ct_entry2, ct_tmp, ct_list,
				 list_node) {
		nfp_ct_do_tc_merge(zt_dst, ct_entry2, ct_entry1);
	}
}

static void
nfp_ct_merge_nft_with_tc(struct nfp_fl_ct_flow_entry *nft_entry,
			 struct nfp_fl_ct_zone_entry *zt)
{
	struct nfp_fl_ct_tc_merge *tc_merge_entry;
	struct rhashtable_iter iter;

	rhashtable_walk_enter(&zt->tc_merge_tb, &iter);
	rhashtable_walk_start(&iter);
	while ((tc_merge_entry = rhashtable_walk_next(&iter)) != NULL) {
		if (IS_ERR(tc_merge_entry))
			continue;
		rhashtable_walk_stop(&iter);
		nfp_ct_do_nft_merge(zt, nft_entry, tc_merge_entry);
		rhashtable_walk_start(&iter);
	}
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

int nfp_fl_ct_handle_pre_ct(struct nfp_flower_priv *priv,
			    struct net_device *netdev,
			    struct flow_cls_offload *flow,
			    struct netlink_ext_ack *extack)
{
	struct flow_action_entry *ct_act, *ct_goto;
	struct nfp_fl_ct_flow_entry *ct_entry;
	struct nfp_fl_ct_zone_entry *zt;
	int err;

	ct_act = get_flow_act(flow->rule, FLOW_ACTION_CT);
	if (!ct_act) {
		NL_SET_ERR_MSG_MOD(extack,
				   "unsupported offload: Conntrack action empty in conntrack offload");
		return -EOPNOTSUPP;
	}

	ct_goto = get_flow_act(flow->rule, FLOW_ACTION_GOTO);
	if (!ct_goto) {
		NL_SET_ERR_MSG_MOD(extack,
				   "unsupported offload: Conntrack requires ACTION_GOTO");
		return -EOPNOTSUPP;
	}

	zt = get_nfp_zone_entry(priv, ct_act->ct.zone, false);
	if (IS_ERR(zt)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "offload error: Could not create zone table entry");
		return PTR_ERR(zt);
	}

	if (!zt->nft) {
		zt->nft = ct_act->ct.flow_table;
		err = nf_flow_table_offload_add_cb(zt->nft, nfp_fl_ct_handle_nft_flow, zt);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "offload error: Could not register nft_callback");
			return err;
		}
	}

	/* Add entry to pre_ct_list */
	ct_entry = nfp_fl_ct_add_flow(zt, netdev, flow, false, extack);
	if (IS_ERR(ct_entry))
		return PTR_ERR(ct_entry);
	ct_entry->type = CT_TYPE_PRE_CT;
	ct_entry->chain_index = ct_goto->chain_index;
	list_add(&ct_entry->list_node, &zt->pre_ct_list);
	zt->pre_ct_count++;

	nfp_ct_merge_tc_entries(ct_entry, zt, zt);

	/* Need to check and merge with tables in the wc_zone as well */
	if (priv->ct_zone_wc)
		nfp_ct_merge_tc_entries(ct_entry, priv->ct_zone_wc, zt);

	return 0;
}

int nfp_fl_ct_handle_post_ct(struct nfp_flower_priv *priv,
			     struct net_device *netdev,
			     struct flow_cls_offload *flow,
			     struct netlink_ext_ack *extack)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(flow);
	struct nfp_fl_ct_flow_entry *ct_entry;
	struct nfp_fl_ct_zone_entry *zt;
	bool wildcarded = false;
	struct flow_match_ct ct;

	flow_rule_match_ct(rule, &ct);
	if (!ct.mask->ct_zone) {
		wildcarded = true;
	} else if (ct.mask->ct_zone != U16_MAX) {
		NL_SET_ERR_MSG_MOD(extack,
				   "unsupported offload: partially wildcarded ct_zone is not supported");
		return -EOPNOTSUPP;
	}

	zt = get_nfp_zone_entry(priv, ct.key->ct_zone, wildcarded);
	if (IS_ERR(zt)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "offload error: Could not create zone table entry");
		return PTR_ERR(zt);
	}

	/* Add entry to post_ct_list */
	ct_entry = nfp_fl_ct_add_flow(zt, netdev, flow, false, extack);
	if (IS_ERR(ct_entry))
		return PTR_ERR(ct_entry);

	ct_entry->type = CT_TYPE_POST_CT;
	ct_entry->chain_index = flow->common.chain_index;
	list_add(&ct_entry->list_node, &zt->post_ct_list);
	zt->post_ct_count++;

	if (wildcarded) {
		/* Iterate through all zone tables if not empty, look for merges with
		 * pre_ct entries and merge them.
		 */
		struct rhashtable_iter iter;
		struct nfp_fl_ct_zone_entry *zone_table;

		rhashtable_walk_enter(&priv->ct_zone_table, &iter);
		rhashtable_walk_start(&iter);
		while ((zone_table = rhashtable_walk_next(&iter)) != NULL) {
			if (IS_ERR(zone_table))
				continue;
			rhashtable_walk_stop(&iter);
			nfp_ct_merge_tc_entries(ct_entry, zone_table, zone_table);
			rhashtable_walk_start(&iter);
		}
		rhashtable_walk_stop(&iter);
		rhashtable_walk_exit(&iter);
	} else {
		nfp_ct_merge_tc_entries(ct_entry, zt, zt);
	}

	return 0;
}

static void
nfp_fl_ct_sub_stats(struct nfp_fl_nft_tc_merge *nft_merge,
		    enum ct_entry_type type, u64 *m_pkts,
		    u64 *m_bytes, u64 *m_used)
{
	struct nfp_flower_priv *priv = nft_merge->zt->priv;
	struct nfp_fl_payload *nfp_flow;
	u32 ctx_id;

	nfp_flow = nft_merge->flow_pay;
	if (!nfp_flow)
		return;

	ctx_id = be32_to_cpu(nfp_flow->meta.host_ctx_id);
	*m_pkts += priv->stats[ctx_id].pkts;
	*m_bytes += priv->stats[ctx_id].bytes;
	*m_used = max_t(u64, *m_used, priv->stats[ctx_id].used);

	/* If request is for a sub_flow which is part of a tunnel merged
	 * flow then update stats from tunnel merged flows first.
	 */
	if (!list_empty(&nfp_flow->linked_flows))
		nfp_flower_update_merge_stats(priv->app, nfp_flow);

	if (type != CT_TYPE_NFT) {
		/* Update nft cached stats */
		flow_stats_update(&nft_merge->nft_parent->stats,
				  priv->stats[ctx_id].bytes,
				  priv->stats[ctx_id].pkts,
				  0, priv->stats[ctx_id].used,
				  FLOW_ACTION_HW_STATS_DELAYED);
	} else {
		/* Update pre_ct cached stats */
		flow_stats_update(&nft_merge->tc_m_parent->pre_ct_parent->stats,
				  priv->stats[ctx_id].bytes,
				  priv->stats[ctx_id].pkts,
				  0, priv->stats[ctx_id].used,
				  FLOW_ACTION_HW_STATS_DELAYED);
		/* Update post_ct cached stats */
		flow_stats_update(&nft_merge->tc_m_parent->post_ct_parent->stats,
				  priv->stats[ctx_id].bytes,
				  priv->stats[ctx_id].pkts,
				  0, priv->stats[ctx_id].used,
				  FLOW_ACTION_HW_STATS_DELAYED);
	}
	/* Reset stats from the nfp */
	priv->stats[ctx_id].pkts = 0;
	priv->stats[ctx_id].bytes = 0;
}

int nfp_fl_ct_stats(struct flow_cls_offload *flow,
		    struct nfp_fl_ct_map_entry *ct_map_ent)
{
	struct nfp_fl_ct_flow_entry *ct_entry = ct_map_ent->ct_entry;
	struct nfp_fl_nft_tc_merge *nft_merge, *nft_m_tmp;
	struct nfp_fl_ct_tc_merge *tc_merge, *tc_m_tmp;

	u64 pkts = 0, bytes = 0, used = 0;
	u64 m_pkts, m_bytes, m_used;

	spin_lock_bh(&ct_entry->zt->priv->stats_lock);

	if (ct_entry->type == CT_TYPE_PRE_CT) {
		/* Iterate tc_merge entries associated with this flow */
		list_for_each_entry_safe(tc_merge, tc_m_tmp, &ct_entry->children,
					 pre_ct_list) {
			m_pkts = 0;
			m_bytes = 0;
			m_used = 0;
			/* Iterate nft_merge entries associated with this tc_merge flow */
			list_for_each_entry_safe(nft_merge, nft_m_tmp, &tc_merge->children,
						 tc_merge_list) {
				nfp_fl_ct_sub_stats(nft_merge, CT_TYPE_PRE_CT,
						    &m_pkts, &m_bytes, &m_used);
			}
			pkts += m_pkts;
			bytes += m_bytes;
			used = max_t(u64, used, m_used);
			/* Update post_ct partner */
			flow_stats_update(&tc_merge->post_ct_parent->stats,
					  m_bytes, m_pkts, 0, m_used,
					  FLOW_ACTION_HW_STATS_DELAYED);
		}
	} else if (ct_entry->type == CT_TYPE_POST_CT) {
		/* Iterate tc_merge entries associated with this flow */
		list_for_each_entry_safe(tc_merge, tc_m_tmp, &ct_entry->children,
					 post_ct_list) {
			m_pkts = 0;
			m_bytes = 0;
			m_used = 0;
			/* Iterate nft_merge entries associated with this tc_merge flow */
			list_for_each_entry_safe(nft_merge, nft_m_tmp, &tc_merge->children,
						 tc_merge_list) {
				nfp_fl_ct_sub_stats(nft_merge, CT_TYPE_POST_CT,
						    &m_pkts, &m_bytes, &m_used);
			}
			pkts += m_pkts;
			bytes += m_bytes;
			used = max_t(u64, used, m_used);
			/* Update pre_ct partner */
			flow_stats_update(&tc_merge->pre_ct_parent->stats,
					  m_bytes, m_pkts, 0, m_used,
					  FLOW_ACTION_HW_STATS_DELAYED);
		}
	} else  {
		/* Iterate nft_merge entries associated with this nft flow */
		list_for_each_entry_safe(nft_merge, nft_m_tmp, &ct_entry->children,
					 nft_flow_list) {
			nfp_fl_ct_sub_stats(nft_merge, CT_TYPE_NFT,
					    &pkts, &bytes, &used);
		}
	}

	/* Add stats from this request to stats potentially cached by
	 * previous requests.
	 */
	flow_stats_update(&ct_entry->stats, bytes, pkts, 0, used,
			  FLOW_ACTION_HW_STATS_DELAYED);
	/* Finally update the flow stats from the original stats request */
	flow_stats_update(&flow->stats, ct_entry->stats.bytes,
			  ct_entry->stats.pkts, 0,
			  ct_entry->stats.lastused,
			  FLOW_ACTION_HW_STATS_DELAYED);
	/* Stats has been synced to original flow, can now clear
	 * the cache.
	 */
	ct_entry->stats.pkts = 0;
	ct_entry->stats.bytes = 0;
	spin_unlock_bh(&ct_entry->zt->priv->stats_lock);

	return 0;
}

static int
nfp_fl_ct_offload_nft_flow(struct nfp_fl_ct_zone_entry *zt, struct flow_cls_offload *flow)
{
	struct nfp_fl_ct_map_entry *ct_map_ent;
	struct nfp_fl_ct_flow_entry *ct_entry;
	struct netlink_ext_ack *extack = NULL;

	ASSERT_RTNL();

	extack = flow->common.extack;
	switch (flow->command) {
	case FLOW_CLS_REPLACE:
		/* Netfilter can request offload multiple times for the same
		 * flow - protect against adding duplicates.
		 */
		ct_map_ent = rhashtable_lookup_fast(&zt->priv->ct_map_table, &flow->cookie,
						    nfp_ct_map_params);
		if (!ct_map_ent) {
			ct_entry = nfp_fl_ct_add_flow(zt, NULL, flow, true, extack);
			if (IS_ERR(ct_entry))
				return PTR_ERR(ct_entry);
			ct_entry->type = CT_TYPE_NFT;
			list_add(&ct_entry->list_node, &zt->nft_flows_list);
			zt->nft_flows_count++;
			nfp_ct_merge_nft_with_tc(ct_entry, zt);
		}
		return 0;
	case FLOW_CLS_DESTROY:
		ct_map_ent = rhashtable_lookup_fast(&zt->priv->ct_map_table, &flow->cookie,
						    nfp_ct_map_params);
		return nfp_fl_ct_del_flow(ct_map_ent);
	case FLOW_CLS_STATS:
		ct_map_ent = rhashtable_lookup_fast(&zt->priv->ct_map_table, &flow->cookie,
						    nfp_ct_map_params);
		if (ct_map_ent)
			return nfp_fl_ct_stats(flow, ct_map_ent);
		break;
	default:
		break;
	}
	return -EINVAL;
}

int nfp_fl_ct_handle_nft_flow(enum tc_setup_type type, void *type_data, void *cb_priv)
{
	struct flow_cls_offload *flow = type_data;
	struct nfp_fl_ct_zone_entry *zt = cb_priv;
	int err = -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		rtnl_lock();
		err = nfp_fl_ct_offload_nft_flow(zt, flow);
		rtnl_unlock();
		break;
	default:
		return -EOPNOTSUPP;
	}
	return err;
}

static void
nfp_fl_ct_clean_nft_entries(struct nfp_fl_ct_zone_entry *zt)
{
	struct nfp_fl_ct_flow_entry *nft_entry, *ct_tmp;
	struct nfp_fl_ct_map_entry *ct_map_ent;

	list_for_each_entry_safe(nft_entry, ct_tmp, &zt->nft_flows_list,
				 list_node) {
		ct_map_ent = rhashtable_lookup_fast(&zt->priv->ct_map_table,
						    &nft_entry->cookie,
						    nfp_ct_map_params);
		nfp_fl_ct_del_flow(ct_map_ent);
	}
}

int nfp_fl_ct_del_flow(struct nfp_fl_ct_map_entry *ct_map_ent)
{
	struct nfp_fl_ct_flow_entry *ct_entry;
	struct nfp_fl_ct_zone_entry *zt;
	struct rhashtable *m_table;

	if (!ct_map_ent)
		return -ENOENT;

	zt = ct_map_ent->ct_entry->zt;
	ct_entry = ct_map_ent->ct_entry;
	m_table = &zt->priv->ct_map_table;

	switch (ct_entry->type) {
	case CT_TYPE_PRE_CT:
		zt->pre_ct_count--;
		rhashtable_remove_fast(m_table, &ct_map_ent->hash_node,
				       nfp_ct_map_params);
		nfp_fl_ct_clean_flow_entry(ct_entry);
		kfree(ct_map_ent);

		if (!zt->pre_ct_count) {
			zt->nft = NULL;
			nfp_fl_ct_clean_nft_entries(zt);
		}
		break;
	case CT_TYPE_POST_CT:
		zt->post_ct_count--;
		rhashtable_remove_fast(m_table, &ct_map_ent->hash_node,
				       nfp_ct_map_params);
		nfp_fl_ct_clean_flow_entry(ct_entry);
		kfree(ct_map_ent);
		break;
	case CT_TYPE_NFT:
		zt->nft_flows_count--;
		rhashtable_remove_fast(m_table, &ct_map_ent->hash_node,
				       nfp_ct_map_params);
		nfp_fl_ct_clean_flow_entry(ct_map_ent->ct_entry);
		kfree(ct_map_ent);
		break;
	default:
		break;
	}

	return 0;
}
