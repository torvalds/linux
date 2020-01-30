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
		if ((entry->handle == loc) && !free && !entry->is_frag)
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
	int i;

	exts = cls->knode.exts;
	if (!tcf_exts_has_actions(exts))
		return -EINVAL;
	if (frag)
		action_entry = frag;

	tcf_exts_for_each_action(i, act, exts) {
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
	u32 prio = cls->common.prio << 16;
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
	int i;

	if (dma_cap->l3l4fnum) {
		priv->flow_entries_max = dma_cap->l3l4fnum;
		priv->flow_entries = devm_kcalloc(priv->device,
						  dma_cap->l3l4fnum,
						  sizeof(*priv->flow_entries),
						  GFP_KERNEL);
		if (!priv->flow_entries)
			return -ENOMEM;

		for (i = 0; i < priv->flow_entries_max; i++)
			priv->flow_entries[i].idx = i;

		dev_info(priv->device, "Enabled Flow TC (entries=%d)\n",
			 priv->flow_entries_max);
	}

	/* Fail silently as we can still use remaining features, e.g. CBS */
	if (!dma_cap->frpsel)
		return 0;

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
	priv->tc_entries = devm_kcalloc(priv->device,
			count, sizeof(*priv->tc_entries), GFP_KERNEL);
	if (!priv->tc_entries)
		return -ENOMEM;

	tc_fill_all_pass_entry(&priv->tc_entries[count - 1]);

	dev_info(priv->device, "Enabling HW TC (entries=%d, max_off=%d)\n",
			priv->tc_entries_max, priv->tc_off_max);
	return 0;
}

static int tc_setup_cbs(struct stmmac_priv *priv,
			struct tc_cbs_qopt_offload *qopt)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 queue = qopt->queue;
	u32 ptr, speed_div;
	u32 mode_to_use;
	u64 value;
	int ret;

	/* Queue 0 is not AVB capable */
	if (queue <= 0 || queue >= tx_queues_count)
		return -EINVAL;
	if (!priv->dma_cap.av)
		return -EOPNOTSUPP;

	mode_to_use = priv->plat->tx_queues_cfg[queue].mode_to_use;
	if (mode_to_use == MTL_QUEUE_DCB && qopt->enable) {
		ret = stmmac_dma_qmode(priv, priv->ioaddr, queue, MTL_QUEUE_AVB);
		if (ret)
			return ret;

		priv->plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_AVB;
	} else if (!qopt->enable) {
		return stmmac_dma_qmode(priv, priv->ioaddr, queue, MTL_QUEUE_DCB);
	}

	/* Port Transmit Rate and Speed Divider */
	ptr = (priv->speed == SPEED_100) ? 4 : 8;
	speed_div = (priv->speed == SPEED_100) ? 100000 : 1000000;

	/* Final adjustments for HW */
	value = div_s64(qopt->idleslope * 1024ll * ptr, speed_div);
	priv->plat->tx_queues_cfg[queue].idle_slope = value & GENMASK(31, 0);

	value = div_s64(-qopt->sendslope * 1024ll * ptr, speed_div);
	priv->plat->tx_queues_cfg[queue].send_slope = value & GENMASK(31, 0);

	value = qopt->hicredit * 1024ll * 8;
	priv->plat->tx_queues_cfg[queue].high_credit = value & GENMASK(31, 0);

	value = qopt->locredit * 1024ll * 8;
	priv->plat->tx_queues_cfg[queue].low_credit = value & GENMASK(31, 0);

	ret = stmmac_config_cbs(priv, priv->hw,
				priv->plat->tx_queues_cfg[queue].send_slope,
				priv->plat->tx_queues_cfg[queue].idle_slope,
				priv->plat->tx_queues_cfg[queue].high_credit,
				priv->plat->tx_queues_cfg[queue].low_credit,
				queue);
	if (ret)
		return ret;

	dev_info(priv->device, "CBS queue %d: send %d, idle %d, hi %d, lo %d\n",
			queue, qopt->sendslope, qopt->idleslope,
			qopt->hicredit, qopt->locredit);
	return 0;
}

static int tc_parse_flow_actions(struct stmmac_priv *priv,
				 struct flow_action *action,
				 struct stmmac_flow_entry *entry)
{
	struct flow_action_entry *act;
	int i;

	if (!flow_action_has_entries(action))
		return -EINVAL;

	flow_action_for_each(i, act, action) {
		switch (act->id) {
		case FLOW_ACTION_DROP:
			entry->action |= STMMAC_FLOW_ACTION_DROP;
			return 0;
		default:
			break;
		}
	}

	/* Nothing to do, maybe inverse filter ? */
	return 0;
}

static int tc_add_basic_flow(struct stmmac_priv *priv,
			     struct flow_cls_offload *cls,
			     struct stmmac_flow_entry *entry)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	struct flow_match_basic match;

	/* Nothing to do here */
	if (!dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_BASIC))
		return -EINVAL;

	flow_rule_match_basic(rule, &match);
	entry->ip_proto = match.key->ip_proto;
	return 0;
}

static int tc_add_ip4_flow(struct stmmac_priv *priv,
			   struct flow_cls_offload *cls,
			   struct stmmac_flow_entry *entry)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	bool inv = entry->action & STMMAC_FLOW_ACTION_DROP;
	struct flow_match_ipv4_addrs match;
	u32 hw_match;
	int ret;

	/* Nothing to do here */
	if (!dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_IPV4_ADDRS))
		return -EINVAL;

	flow_rule_match_ipv4_addrs(rule, &match);
	hw_match = ntohl(match.key->src) & ntohl(match.mask->src);
	if (hw_match) {
		ret = stmmac_config_l3_filter(priv, priv->hw, entry->idx, true,
					      false, true, inv, hw_match);
		if (ret)
			return ret;
	}

	hw_match = ntohl(match.key->dst) & ntohl(match.mask->dst);
	if (hw_match) {
		ret = stmmac_config_l3_filter(priv, priv->hw, entry->idx, true,
					      false, false, inv, hw_match);
		if (ret)
			return ret;
	}

	return 0;
}

static int tc_add_ports_flow(struct stmmac_priv *priv,
			     struct flow_cls_offload *cls,
			     struct stmmac_flow_entry *entry)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	bool inv = entry->action & STMMAC_FLOW_ACTION_DROP;
	struct flow_match_ports match;
	u32 hw_match;
	bool is_udp;
	int ret;

	/* Nothing to do here */
	if (!dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_PORTS))
		return -EINVAL;

	switch (entry->ip_proto) {
	case IPPROTO_TCP:
		is_udp = false;
		break;
	case IPPROTO_UDP:
		is_udp = true;
		break;
	default:
		return -EINVAL;
	}

	flow_rule_match_ports(rule, &match);

	hw_match = ntohs(match.key->src) & ntohs(match.mask->src);
	if (hw_match) {
		ret = stmmac_config_l4_filter(priv, priv->hw, entry->idx, true,
					      is_udp, true, inv, hw_match);
		if (ret)
			return ret;
	}

	hw_match = ntohs(match.key->dst) & ntohs(match.mask->dst);
	if (hw_match) {
		ret = stmmac_config_l4_filter(priv, priv->hw, entry->idx, true,
					      is_udp, false, inv, hw_match);
		if (ret)
			return ret;
	}

	entry->is_l4 = true;
	return 0;
}

static struct stmmac_flow_entry *tc_find_flow(struct stmmac_priv *priv,
					      struct flow_cls_offload *cls,
					      bool get_free)
{
	int i;

	for (i = 0; i < priv->flow_entries_max; i++) {
		struct stmmac_flow_entry *entry = &priv->flow_entries[i];

		if (entry->cookie == cls->cookie)
			return entry;
		if (get_free && (entry->in_use == false))
			return entry;
	}

	return NULL;
}

static struct {
	int (*fn)(struct stmmac_priv *priv, struct flow_cls_offload *cls,
		  struct stmmac_flow_entry *entry);
} tc_flow_parsers[] = {
	{ .fn = tc_add_basic_flow },
	{ .fn = tc_add_ip4_flow },
	{ .fn = tc_add_ports_flow },
};

static int tc_add_flow(struct stmmac_priv *priv,
		       struct flow_cls_offload *cls)
{
	struct stmmac_flow_entry *entry = tc_find_flow(priv, cls, false);
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	int i, ret;

	if (!entry) {
		entry = tc_find_flow(priv, cls, true);
		if (!entry)
			return -ENOENT;
	}

	ret = tc_parse_flow_actions(priv, &rule->action, entry);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(tc_flow_parsers); i++) {
		ret = tc_flow_parsers[i].fn(priv, cls, entry);
		if (!ret) {
			entry->in_use = true;
			continue;
		}
	}

	if (!entry->in_use)
		return -EINVAL;

	entry->cookie = cls->cookie;
	return 0;
}

static int tc_del_flow(struct stmmac_priv *priv,
		       struct flow_cls_offload *cls)
{
	struct stmmac_flow_entry *entry = tc_find_flow(priv, cls, false);
	int ret;

	if (!entry || !entry->in_use)
		return -ENOENT;

	if (entry->is_l4) {
		ret = stmmac_config_l4_filter(priv, priv->hw, entry->idx, false,
					      false, false, false, 0);
	} else {
		ret = stmmac_config_l3_filter(priv, priv->hw, entry->idx, false,
					      false, false, false, 0);
	}

	entry->in_use = false;
	entry->cookie = 0;
	entry->is_l4 = false;
	return ret;
}

static int tc_setup_cls(struct stmmac_priv *priv,
			struct flow_cls_offload *cls)
{
	int ret = 0;

	/* When RSS is enabled, the filtering will be bypassed */
	if (priv->rss.enable)
		return -EBUSY;

	switch (cls->command) {
	case FLOW_CLS_REPLACE:
		ret = tc_add_flow(priv, cls);
		break;
	case FLOW_CLS_DESTROY:
		ret = tc_del_flow(priv, cls);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

const struct stmmac_tc_ops dwmac510_tc_ops = {
	.init = tc_init,
	.setup_cls_u32 = tc_setup_cls_u32,
	.setup_cbs = tc_setup_cbs,
	.setup_cls = tc_setup_cls,
};
