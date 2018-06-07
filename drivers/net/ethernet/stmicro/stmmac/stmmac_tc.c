// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 * stmmac TC Handling (HW only)
 */

#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include "common.h"
#include "dwmac4.h"
#include "dwmac5.h"
#include "stmmac.h"

static void tc_fill_all_pass_entry(struct stmmac_tc_entry *entry)
{
	memset(entry, 0, sizeof(*entry));
	entry->in_use = true;
	entry->is_last = true;
	entry->is_frag = false;
	entry->prio = ~0x0;
	entry->handle = 0;
	entry->val.match_data = 0x0;
	entry->val.match_en = 0x0;
	entry->val.af = 1;
	entry->val.dma_ch_no = 0x0;
}

static struct stmmac_tc_entry *tc_find_entry(struct stmmac_priv *priv,
					     struct tc_cls_u32_offload *cls,
					     bool free)
{
	struct stmmac_tc_entry *entry, *first = NULL, *dup = NULL;
	u32 loc = cls->knode.handle;
	int i;

	for (i = 0; i < priv->tc_entries_max; i++) {
		entry = &priv->tc_entries[i];
		if (!entry->in_use && !first && free)
			first = entry;
		if (entry->handle == loc && !free)
			dup = entry;
	}

	if (dup)
		return dup;
	if (first) {
		first->handle = loc;
		first->in_use = true;

		/* Reset HW values */
		memset(&first->val, 0, sizeof(first->val));
	}

	return first;
}

static int tc_fill_actions(struct stmmac_tc_entry *entry,
			   struct stmmac_tc_entry *frag,
			   struct tc_cls_u32_offload *cls)
{
	struct stmmac_tc_entry *action_entry = entry;
	const struct tc_action *act;
	struct tcf_exts *exts;
	LIST_HEAD(actions);

	exts = cls->knode.exts;
	if (!tcf_exts_has_actions(exts))
		return -EINVAL;
	if (frag)
		action_entry = frag;

	tcf_exts_to_list(exts, &actions);
	list_for_each_entry(act, &actions, list) {
		/* Accept */
		if (is_tcf_gact_ok(act)) {
			action_entry->val.af = 1;
			break;
		}
		/* Drop */
		if (is_tcf_gact_shot(act)) {
			action_entry->val.rf = 1;
			break;
		}

		/* Unsupported */
		return -EINVAL;
	}

	return 0;
}

static int tc_fill_entry(struct stmmac_priv *priv,
			 struct tc_cls_u32_offload *cls)
{
	struct stmmac_tc_entry *entry, *frag = NULL;
	struct tc_u32_sel *sel = cls->knode.sel;
	u32 off, data, mask, real_off, rem;
	u32 prio = cls->common.prio;
	int ret;

	/* Only 1 match per entry */
	if (sel->nkeys <= 0 || sel->nkeys > 1)
		return -EINVAL;

	off = sel->keys[0].off << sel->offshift;
	data = sel->keys[0].val;
	mask = sel->keys[0].mask;

	switch (ntohs(cls->common.protocol)) {
	case ETH_P_ALL:
		break;
	case ETH_P_IP:
		off += ETH_HLEN;
		break;
	default:
		return -EINVAL;
	}

	if (off > priv->tc_off_max)
		return -EINVAL;

	real_off = off / 4;
	rem = off % 4;

	entry = tc_find_entry(priv, cls, true);
	if (!entry)
		return -EINVAL;

	if (rem) {
		frag = tc_find_entry(priv, cls, true);
		if (!frag) {
			ret = -EINVAL;
			goto err_unuse;
		}

		entry->frag_ptr = frag;
		entry->val.match_en = (mask << (rem * 8)) &
			GENMASK(31, rem * 8);
		entry->val.match_data = (data << (rem * 8)) &
			GENMASK(31, rem * 8);
		entry->val.frame_offset = real_off;
		entry->prio = prio;

		frag->val.match_en = (mask >> (rem * 8)) &
			GENMASK(rem * 8 - 1, 0);
		frag->val.match_data = (data >> (rem * 8)) &
			GENMASK(rem * 8 - 1, 0);
		frag->val.frame_offset = real_off + 1;
		frag->prio = prio;
		frag->is_frag = true;
	} else {
		entry->frag_ptr = NULL;
		entry->val.match_en = mask;
		entry->val.match_data = data;
		entry->val.frame_offset = real_off;
		entry->prio = prio;
	}

	ret = tc_fill_actions(entry, frag, cls);
	if (ret)
		goto err_unuse;

	return 0;

err_unuse:
	if (frag)
		frag->in_use = false;
	entry->in_use = false;
	return ret;
}

static void tc_unfill_entry(struct stmmac_priv *priv,
			    struct tc_cls_u32_offload *cls)
{
	struct stmmac_tc_entry *entry;

	entry = tc_find_entry(priv, cls, false);
	if (!entry)
		return;

	entry->in_use = false;
	if (entry->frag_ptr) {
		entry = entry->frag_ptr;
		entry->is_frag = false;
		entry->in_use = false;
	}
}

static int tc_config_knode(struct stmmac_priv *priv,
			   struct tc_cls_u32_offload *cls)
{
	int ret;

	ret = tc_fill_entry(priv, cls);
	if (ret)
		return ret;

	ret = stmmac_rxp_config(priv, priv->hw->pcsr, priv->tc_entries,
			priv->tc_entries_max);
	if (ret)
		goto err_unfill;

	return 0;

err_unfill:
	tc_unfill_entry(priv, cls);
	return ret;
}

static int tc_delete_knode(struct stmmac_priv *priv,
			   struct tc_cls_u32_offload *cls)
{
	int ret;

	/* Set entry and fragments as not used */
	tc_unfill_entry(priv, cls);

	ret = stmmac_rxp_config(priv, priv->hw->pcsr, priv->tc_entries,
			priv->tc_entries_max);
	if (ret)
		return ret;

	return 0;
}

static int tc_setup_cls_u32(struct stmmac_priv *priv,
			    struct tc_cls_u32_offload *cls)
{
	switch (cls->command) {
	case TC_CLSU32_REPLACE_KNODE:
		tc_unfill_entry(priv, cls);
		/* Fall through */
	case TC_CLSU32_NEW_KNODE:
		return tc_config_knode(priv, cls);
	case TC_CLSU32_DELETE_KNODE:
		return tc_delete_knode(priv, cls);
	default:
		return -EOPNOTSUPP;
	}
}

static int tc_init(struct stmmac_priv *priv)
{
	struct dma_features *dma_cap = &priv->dma_cap;
	unsigned int count;

	if (!dma_cap->frpsel)
		return -EINVAL;

	switch (dma_cap->frpbs) {
	case 0x0:
		priv->tc_off_max = 64;
		break;
	case 0x1:
		priv->tc_off_max = 128;
		break;
	case 0x2:
		priv->tc_off_max = 256;
		break;
	default:
		return -EINVAL;
	}

	switch (dma_cap->frpes) {
	case 0x0:
		count = 64;
		break;
	case 0x1:
		count = 128;
		break;
	case 0x2:
		count = 256;
		break;
	default:
		return -EINVAL;
	}

	/* Reserve one last filter which lets all pass */
	priv->tc_entries_max = count;
	priv->tc_entries = devm_kzalloc(priv->device,
			sizeof(*priv->tc_entries) * count, GFP_KERNEL);
	if (!priv->tc_entries)
		return -ENOMEM;

	tc_fill_all_pass_entry(&priv->tc_entries[count - 1]);

	dev_info(priv->device, "Enabling HW TC (entries=%d, max_off=%d)\n",
			priv->tc_entries_max, priv->tc_off_max);
	return 0;
}

const struct stmmac_tc_ops dwmac510_tc_ops = {
	.init = tc_init,
	.setup_cls_u32 = tc_setup_cls_u32,
};
