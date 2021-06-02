// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2021 Corigine, Inc. */

#include "conntrack.h"

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
	kfree(zt);
	return ERR_PTR(err);
}

static struct flow_action_entry *get_flow_act(struct flow_cls_offload *flow,
					      enum flow_action_id act_id)
{
	struct flow_action_entry *act = NULL;
	int i;

	flow_action_for_each(i, act, &flow->rule->action) {
		if (act->id == act_id)
			return act;
	}
	return NULL;
}

int nfp_fl_ct_handle_pre_ct(struct nfp_flower_priv *priv,
			    struct net_device *netdev,
			    struct flow_cls_offload *flow,
			    struct netlink_ext_ack *extack)
{
	struct flow_action_entry *ct_act;
	struct nfp_fl_ct_zone_entry *zt;

	ct_act = get_flow_act(flow, FLOW_ACTION_CT);
	if (!ct_act) {
		NL_SET_ERR_MSG_MOD(extack,
				   "unsupported offload: Conntrack action empty in conntrack offload");
		return -EOPNOTSUPP;
	}

	zt = get_nfp_zone_entry(priv, ct_act->ct.zone, false);
	if (IS_ERR(zt)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "offload error: Could not create zone table entry");
		return PTR_ERR(zt);
	}

	if (!zt->nft)
		zt->nft = ct_act->ct.flow_table;

	NL_SET_ERR_MSG_MOD(extack, "unsupported offload: Conntrack action not supported");
	return -EOPNOTSUPP;
}

int nfp_fl_ct_handle_post_ct(struct nfp_flower_priv *priv,
			     struct net_device *netdev,
			     struct flow_cls_offload *flow,
			     struct netlink_ext_ack *extack)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(flow);
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

	NL_SET_ERR_MSG_MOD(extack, "unsupported offload: Conntrack match not supported");
	return -EOPNOTSUPP;
}
