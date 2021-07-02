// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2021 Corigine, Inc. */

#include "conntrack.h"

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

static int nfp_ct_merge_check(struct nfp_fl_ct_flow_entry *entry1,
			      struct nfp_fl_ct_flow_entry *entry2)
{
	unsigned int ovlp_keys = entry1->rule->match.dissector->used_keys &
				 entry2->rule->match.dissector->used_keys;
	bool out;

	/* check the overlapped fields one by one, the unmasked part
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
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match1, match2;

		flow_rule_match_ipv4_addrs(entry1->rule, &match1);
		flow_rule_match_ipv4_addrs(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match1, match2;

		flow_rule_match_ipv6_addrs(entry1->rule, &match1);
		flow_rule_match_ipv6_addrs(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match1, match2;

		flow_rule_match_ports(entry1->rule, &match1);
		flow_rule_match_ports(entry2->rule, &match2);
		COMPARE_UNMASKED_FIELDS(match1, match2, &out);
		if (out)
			goto check_failed;
	}

	if (ovlp_keys & BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match1, match2;

		flow_rule_match_eth_addrs(entry1->rule, &match1);
		flow_rule_match_eth_addrs(entry2->rule, &match2);
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

static int nfp_ct_check_mangle_merge(struct flow_action_entry *a_in,
				     struct flow_rule *rule)
{
	enum flow_action_mangle_base htype = a_in->mangle.htype;
	u32 offset = a_in->mangle.offset;

	switch (htype) {
	case FLOW_ACT_MANGLE_HDR_TYPE_ETH:
		if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS))
			return -EOPNOTSUPP;
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_IP4:
		if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
			struct flow_match_ip match;

			flow_rule_match_ip(rule, &match);
			if (offset == offsetof(struct iphdr, ttl) &&
			    match.mask->ttl)
				return -EOPNOTSUPP;
			if (offset == round_down(offsetof(struct iphdr, tos), 4) &&
			    match.mask->tos)
				return -EOPNOTSUPP;
		}
		if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
			struct flow_match_ipv4_addrs match;

			flow_rule_match_ipv4_addrs(rule, &match);
			if (offset == offsetof(struct iphdr, saddr) &&
			    match.mask->src)
				return -EOPNOTSUPP;
			if (offset == offsetof(struct iphdr, daddr) &&
			    match.mask->dst)
				return -EOPNOTSUPP;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_IP6:
		if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
			struct flow_match_ip match;

			flow_rule_match_ip(rule, &match);
			if (offset == round_down(offsetof(struct ipv6hdr, hop_limit), 4) &&
			    match.mask->ttl)
				return -EOPNOTSUPP;
			/* for ipv6, tos and flow_lbl are in the same word */
			if (offset == round_down(offsetof(struct ipv6hdr, flow_lbl), 4) &&
			    match.mask->tos)
				return -EOPNOTSUPP;
		}
		if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
			struct flow_match_ipv6_addrs match;

			flow_rule_match_ipv6_addrs(rule, &match);
			if (offset >= offsetof(struct ipv6hdr, saddr) &&
			    offset < offsetof(struct ipv6hdr, daddr) &&
			    memchr_inv(&match.mask->src, 0, sizeof(match.mask->src)))
				return -EOPNOTSUPP;
			if (offset >= offsetof(struct ipv6hdr, daddr) &&
			    offset < sizeof(struct ipv6hdr) &&
			    memchr_inv(&match.mask->dst, 0, sizeof(match.mask->dst)))
				return -EOPNOTSUPP;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_TCP:
	case FLOW_ACT_MANGLE_HDR_TYPE_UDP:
		/* currently only can modify ports */
		if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS))
			return -EOPNOTSUPP;
		break;
	default:
		break;
	}
	return 0;
}

static int nfp_ct_merge_act_check(struct nfp_fl_ct_flow_entry *pre_ct_entry,
				  struct nfp_fl_ct_flow_entry *post_ct_entry,
				  struct nfp_fl_ct_flow_entry *nft_entry)
{
	struct flow_action_entry *act;
	int err, i;

	/* Check for pre_ct->action conflicts */
	flow_action_for_each(i, act, &pre_ct_entry->rule->action) {
		switch (act->id) {
		case FLOW_ACTION_MANGLE:
			err = nfp_ct_check_mangle_merge(act, nft_entry->rule);
			if (err)
				return err;
			err = nfp_ct_check_mangle_merge(act, post_ct_entry->rule);
			if (err)
				return err;
			break;
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
		case FLOW_ACTION_MANGLE:
			err = nfp_ct_check_mangle_merge(act, post_ct_entry->rule);
			if (err)
				return err;
			break;
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

static int nfp_fl_ct_add_offload(struct nfp_fl_nft_tc_merge *m_entry)
{
	return 0;
}

static int nfp_fl_ct_del_offload(struct nfp_app *app, unsigned long cookie,
				 struct net_device *netdev)
{
	return 0;
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
	 * The nft entry does not have a netdev or chain populated, so
	 * skip this check.
	 */
	err = nfp_ct_merge_check(pre_ct_entry, nft_entry);
	if (err)
		return err;
	err = nfp_ct_merge_check(post_ct_entry, nft_entry);
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
	/* Copy the netdev from one the pre_ct entry. When the tc_m_entry was created
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

	if (post_ct_entry->netdev != pre_ct_entry->netdev)
		return -EINVAL;
	/* Checks that the chain_index of the filter matches the
	 * chain_index of the GOTO action.
	 */
	if (post_ct_entry->chain_index != pre_ct_entry->chain_index)
		return -EINVAL;

	err = nfp_ct_merge_check(post_ct_entry, pre_ct_entry);
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

	/* init the various hash tables and lists*/
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
		/* Need to iterate through list of nft_flow entries*/
		struct nfp_fl_ct_flow_entry *ct_entry = entry;

		list_for_each_entry_safe(m_entry, tmp, &ct_entry->children,
					 nft_flow_list) {
			cleanup_nft_merge_entry(m_entry);
		}
	} else {
		/* Need to iterate through list of tc_merged_flow entries*/
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
		return 0;
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
	default:
		break;
	}

	return 0;
}
