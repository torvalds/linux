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

	/* init the various hash tables and lists*/
	INIT_LIST_HEAD(&zt->pre_ct_list);
	INIT_LIST_HEAD(&zt->post_ct_list);

	err = rhashtable_init(&zt->tc_merge_tb, &nfp_tc_ct_merge_params);
	if (err)
		goto err_tc_merge_tb_init;

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
	rhashtable_destroy(&zt->tc_merge_tb);
err_tc_merge_tb_init:
	kfree(zt);
	return ERR_PTR(err);
}

static struct
nfp_fl_ct_flow_entry *nfp_fl_ct_add_flow(struct nfp_fl_ct_zone_entry *zt,
					 struct net_device *netdev,
					 struct flow_cls_offload *flow,
					 struct netlink_ext_ack *extack)
{
	struct nfp_fl_ct_flow_entry *entry;
	struct nfp_fl_ct_map_entry *map;
	struct flow_action_entry *act;
	int err, i;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	entry->zt = zt;
	entry->netdev = netdev;
	entry->cookie = flow->cookie;
	entry->rule = flow_rule_alloc(flow->rule->action.num_entries);
	if (!entry->rule) {
		err = -ENOMEM;
		goto err_pre_ct_act;
	}
	entry->rule->match.dissector = flow->rule->match.dissector;
	entry->rule->match.mask = flow->rule->match.mask;
	entry->rule->match.key = flow->rule->match.key;
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
	kfree(entry->rule);
err_pre_ct_act:
	kfree(entry);
	return ERR_PTR(err);
}

static void nfp_free_tc_merge_children(struct nfp_fl_ct_flow_entry *entry)
{
}

static void nfp_free_nft_merge_children(void *entry, bool is_nft_flow)
{
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
	kfree(entry->rule);
	kfree(entry);
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
	struct flow_action_entry *ct_act, *ct_goto;
	struct nfp_fl_ct_flow_entry *ct_entry;
	struct nfp_fl_ct_zone_entry *zt;

	ct_act = get_flow_act(flow, FLOW_ACTION_CT);
	if (!ct_act) {
		NL_SET_ERR_MSG_MOD(extack,
				   "unsupported offload: Conntrack action empty in conntrack offload");
		return -EOPNOTSUPP;
	}

	ct_goto = get_flow_act(flow, FLOW_ACTION_GOTO);
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

	if (!zt->nft)
		zt->nft = ct_act->ct.flow_table;

	/* Add entry to pre_ct_list */
	ct_entry = nfp_fl_ct_add_flow(zt, netdev, flow, extack);
	if (IS_ERR(ct_entry))
		return PTR_ERR(ct_entry);
	ct_entry->type = CT_TYPE_PRE_CT;
	ct_entry->chain_index = ct_goto->chain_index;
	list_add(&ct_entry->list_node, &zt->pre_ct_list);
	zt->pre_ct_count++;

	NL_SET_ERR_MSG_MOD(extack, "unsupported offload: Conntrack action not supported");
	nfp_fl_ct_clean_flow_entry(ct_entry);
	return -EOPNOTSUPP;
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
	ct_entry = nfp_fl_ct_add_flow(zt, netdev, flow, extack);
	if (IS_ERR(ct_entry))
		return PTR_ERR(ct_entry);

	ct_entry->type = CT_TYPE_POST_CT;
	ct_entry->chain_index = flow->common.chain_index;
	list_add(&ct_entry->list_node, &zt->post_ct_list);
	zt->post_ct_count++;

	NL_SET_ERR_MSG_MOD(extack, "unsupported offload: Conntrack match not supported");
	nfp_fl_ct_clean_flow_entry(ct_entry);
	return -EOPNOTSUPP;
}
