// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc. */

#include <linux/bitfield.h>
#include <net/pkt_cls.h>

#include "../nfpcore/nfp_cpp.h"
#include "../nfp_app.h"
#include "../nfp_net_repr.h"
#include "main.h"

struct nfp_abm_u32_match {
	u32 handle;
	u32 band;
	u8 mask;
	u8 val;
	struct list_head list;
};

static bool
nfp_abm_u32_check_knode(struct nfp_abm *abm, struct tc_cls_u32_knode *knode,
			__be16 proto, struct netlink_ext_ack *extack)
{
	struct tc_u32_key *k;
	unsigned int tos_off;

	if (knode->exts && tcf_exts_has_actions(knode->exts)) {
		NL_SET_ERR_MSG_MOD(extack, "action offload not supported");
		return false;
	}
	if (knode->link_handle) {
		NL_SET_ERR_MSG_MOD(extack, "linking not supported");
		return false;
	}
	if (knode->sel->flags != TC_U32_TERMINAL) {
		NL_SET_ERR_MSG_MOD(extack,
				   "flags must be equal to TC_U32_TERMINAL");
		return false;
	}
	if (knode->sel->off || knode->sel->offshift || knode->sel->offmask ||
	    knode->sel->offoff || knode->fshift) {
		NL_SET_ERR_MSG_MOD(extack, "variable offsetting not supported");
		return false;
	}
	if (knode->sel->hoff || knode->sel->hmask) {
		NL_SET_ERR_MSG_MOD(extack, "hashing not supported");
		return false;
	}
	if (knode->val || knode->mask) {
		NL_SET_ERR_MSG_MOD(extack, "matching on mark not supported");
		return false;
	}
	if (knode->res && knode->res->class) {
		NL_SET_ERR_MSG_MOD(extack, "setting non-0 class not supported");
		return false;
	}
	if (knode->res && knode->res->classid >= abm->num_bands) {
		NL_SET_ERR_MSG_MOD(extack,
				   "classid higher than number of bands");
		return false;
	}
	if (knode->sel->nkeys != 1) {
		NL_SET_ERR_MSG_MOD(extack, "exactly one key required");
		return false;
	}

	switch (proto) {
	case htons(ETH_P_IP):
		tos_off = 16;
		break;
	case htons(ETH_P_IPV6):
		tos_off = 20;
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "only IP and IPv6 supported as filter protocol");
		return false;
	}

	k = &knode->sel->keys[0];
	if (k->offmask) {
		NL_SET_ERR_MSG_MOD(extack, "offset mask - variable offsetting not supported");
		return false;
	}
	if (k->off) {
		NL_SET_ERR_MSG_MOD(extack, "only DSCP fields can be matched");
		return false;
	}
	if (k->val & ~k->mask) {
		NL_SET_ERR_MSG_MOD(extack, "mask does not cover the key");
		return false;
	}
	if (be32_to_cpu(k->mask) >> tos_off & ~abm->dscp_mask) {
		NL_SET_ERR_MSG_MOD(extack, "only high DSCP class selector bits can be used");
		nfp_err(abm->app->cpp,
			"u32 offload: requested mask %x FW can support only %x\n",
			be32_to_cpu(k->mask) >> tos_off, abm->dscp_mask);
		return false;
	}

	return true;
}

/* This filter list -> map conversion is O(n * m), we expect single digit or
 * low double digit number of prios and likewise for the filters.  Also u32
 * doesn't report stats, so it's really only setup time cost.
 */
static unsigned int
nfp_abm_find_band_for_prio(struct nfp_abm_link *alink, unsigned int prio)
{
	struct nfp_abm_u32_match *iter;

	list_for_each_entry(iter, &alink->dscp_map, list)
		if ((prio & iter->mask) == iter->val)
			return iter->band;

	return alink->def_band;
}

static int nfp_abm_update_band_map(struct nfp_abm_link *alink)
{
	unsigned int i, bits_per_prio, prios_per_word, base_shift;
	struct nfp_abm *abm = alink->abm;
	u32 field_mask;

	alink->has_prio = !list_empty(&alink->dscp_map);

	bits_per_prio = roundup_pow_of_two(order_base_2(abm->num_bands));
	field_mask = (1 << bits_per_prio) - 1;
	prios_per_word = sizeof(u32) * BITS_PER_BYTE / bits_per_prio;

	/* FW mask applies from top bits */
	base_shift = 8 - order_base_2(abm->num_prios);

	for (i = 0; i < abm->num_prios; i++) {
		unsigned int offset;
		u32 *word;
		u8 band;

		word = &alink->prio_map[i / prios_per_word];
		offset = (i % prios_per_word) * bits_per_prio;

		band = nfp_abm_find_band_for_prio(alink, i << base_shift);

		*word &= ~(field_mask << offset);
		*word |= band << offset;
	}

	/* Qdisc offload status may change if has_prio changed */
	nfp_abm_qdisc_offload_update(alink);

	return nfp_abm_ctrl_prio_map_update(alink, alink->prio_map);
}

static void
nfp_abm_u32_knode_delete(struct nfp_abm_link *alink,
			 struct tc_cls_u32_knode *knode)
{
	struct nfp_abm_u32_match *iter;

	list_for_each_entry(iter, &alink->dscp_map, list)
		if (iter->handle == knode->handle) {
			list_del(&iter->list);
			kfree(iter);
			nfp_abm_update_band_map(alink);
			return;
		}
}

static int
nfp_abm_u32_knode_replace(struct nfp_abm_link *alink,
			  struct tc_cls_u32_knode *knode,
			  __be16 proto, struct netlink_ext_ack *extack)
{
	struct nfp_abm_u32_match *match = NULL, *iter;
	unsigned int tos_off;
	u8 mask, val;
	int err;

	if (!nfp_abm_u32_check_knode(alink->abm, knode, proto, extack)) {
		err = -EOPNOTSUPP;
		goto err_delete;
	}

	tos_off = proto == htons(ETH_P_IP) ? 16 : 20;

	/* Extract the DSCP Class Selector bits */
	val = be32_to_cpu(knode->sel->keys[0].val) >> tos_off & 0xff;
	mask = be32_to_cpu(knode->sel->keys[0].mask) >> tos_off & 0xff;

	/* Check if there is no conflicting mapping and find match by handle */
	list_for_each_entry(iter, &alink->dscp_map, list) {
		u32 cmask;

		if (iter->handle == knode->handle) {
			match = iter;
			continue;
		}

		cmask = iter->mask & mask;
		if ((iter->val & cmask) == (val & cmask) &&
		    iter->band != knode->res->classid) {
			NL_SET_ERR_MSG_MOD(extack, "conflict with already offloaded filter");
			err = -EOPNOTSUPP;
			goto err_delete;
		}
	}

	if (!match) {
		match = kzalloc(sizeof(*match), GFP_KERNEL);
		if (!match) {
			err = -ENOMEM;
			goto err_delete;
		}

		list_add(&match->list, &alink->dscp_map);
	}
	match->handle = knode->handle;
	match->band = knode->res->classid;
	match->mask = mask;
	match->val = val;

	err = nfp_abm_update_band_map(alink);
	if (err)
		goto err_delete;

	return 0;

err_delete:
	nfp_abm_u32_knode_delete(alink, knode);
	return err;
}

static int nfp_abm_setup_tc_block_cb(enum tc_setup_type type,
				     void *type_data, void *cb_priv)
{
	struct tc_cls_u32_offload *cls_u32 = type_data;
	struct nfp_repr *repr = cb_priv;
	struct nfp_abm_link *alink;

	alink = repr->app_priv;

	if (type != TC_SETUP_CLSU32) {
		NL_SET_ERR_MSG_MOD(cls_u32->common.extack,
				   "only offload of u32 classifier supported");
		return -EOPNOTSUPP;
	}
	if (!tc_cls_can_offload_and_chain0(repr->netdev, &cls_u32->common))
		return -EOPNOTSUPP;

	if (cls_u32->common.protocol != htons(ETH_P_IP) &&
	    cls_u32->common.protocol != htons(ETH_P_IPV6)) {
		NL_SET_ERR_MSG_MOD(cls_u32->common.extack,
				   "only IP and IPv6 supported as filter protocol");
		return -EOPNOTSUPP;
	}

	switch (cls_u32->command) {
	case TC_CLSU32_NEW_KNODE:
	case TC_CLSU32_REPLACE_KNODE:
		return nfp_abm_u32_knode_replace(alink, &cls_u32->knode,
						 cls_u32->common.protocol,
						 cls_u32->common.extack);
	case TC_CLSU32_DELETE_KNODE:
		nfp_abm_u32_knode_delete(alink, &cls_u32->knode);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(nfp_abm_block_cb_list);

int nfp_abm_setup_cls_block(struct net_device *netdev, struct nfp_repr *repr,
			    struct flow_block_offload *f)
{
	return flow_block_cb_setup_simple(f, &nfp_abm_block_cb_list,
					  nfp_abm_setup_tc_block_cb,
					  repr, repr, true);
}
