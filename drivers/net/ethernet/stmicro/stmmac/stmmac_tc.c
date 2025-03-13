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
	/* Set entry and fragments as not used */
	tc_unfill_entry(priv, cls);

	return stmmac_rxp_config(priv, priv->hw->pcsr, priv->tc_entries,
				 priv->tc_entries_max);
}

static int tc_setup_cls_u32(struct stmmac_priv *priv,
			    struct tc_cls_u32_offload *cls)
{
	switch (cls->command) {
	case TC_CLSU32_REPLACE_KNODE:
		tc_unfill_entry(priv, cls);
		fallthrough;
	case TC_CLSU32_NEW_KNODE:
		return tc_config_knode(priv, cls);
	case TC_CLSU32_DELETE_KNODE:
		return tc_delete_knode(priv, cls);
	default:
		return -EOPNOTSUPP;
	}
}

static int tc_rfs_init(struct stmmac_priv *priv)
{
	int i;

	priv->rfs_entries_max[STMMAC_RFS_T_VLAN] = 8;
	priv->rfs_entries_max[STMMAC_RFS_T_LLDP] = 1;
	priv->rfs_entries_max[STMMAC_RFS_T_1588] = 1;

	for (i = 0; i < STMMAC_RFS_T_MAX; i++)
		priv->rfs_entries_total += priv->rfs_entries_max[i];

	priv->rfs_entries = devm_kcalloc(priv->device,
					 priv->rfs_entries_total,
					 sizeof(*priv->rfs_entries),
					 GFP_KERNEL);
	if (!priv->rfs_entries)
		return -ENOMEM;

	dev_info(priv->device, "Enabled RFS Flow TC (entries=%d)\n",
		 priv->rfs_entries_total);

	return 0;
}

static int tc_init(struct stmmac_priv *priv)
{
	struct dma_features *dma_cap = &priv->dma_cap;
	unsigned int count;
	int ret, i;

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

		dev_info(priv->device, "Enabled L3L4 Flow TC (entries=%d)\n",
			 priv->flow_entries_max);
	}

	ret = tc_rfs_init(priv);
	if (ret)
		return -ENOMEM;

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
	s64 port_transmit_rate_kbps;
	u32 queue = qopt->queue;
	u32 mode_to_use;
	u64 value;
	u32 ptr;
	int ret;

	/* Queue 0 is not AVB capable */
	if (queue <= 0 || queue >= tx_queues_count)
		return -EINVAL;
	if (!priv->dma_cap.av)
		return -EOPNOTSUPP;

	port_transmit_rate_kbps = qopt->idleslope - qopt->sendslope;

	if (qopt->enable) {
		/* Port Transmit Rate and Speed Divider */
		switch (div_s64(port_transmit_rate_kbps, 1000)) {
		case SPEED_10000:
		case SPEED_5000:
			ptr = 32;
			break;
		case SPEED_2500:
		case SPEED_1000:
			ptr = 8;
			break;
		case SPEED_100:
			ptr = 4;
			break;
		default:
			netdev_err(priv->dev,
				   "Invalid portTransmitRate %lld (idleSlope - sendSlope)\n",
				   port_transmit_rate_kbps);
			return -EINVAL;
		}
	} else {
		ptr = 0;
	}

	mode_to_use = priv->plat->tx_queues_cfg[queue].mode_to_use;
	if (mode_to_use == MTL_QUEUE_DCB && qopt->enable) {
		ret = stmmac_dma_qmode(priv, priv->ioaddr, queue, MTL_QUEUE_AVB);
		if (ret)
			return ret;

		priv->plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_AVB;
	} else if (!qopt->enable) {
		ret = stmmac_dma_qmode(priv, priv->ioaddr, queue,
				       MTL_QUEUE_DCB);
		if (ret)
			return ret;

		priv->plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_DCB;
		return 0;
	}

	/* Final adjustments for HW */
	value = div_s64(qopt->idleslope * 1024ll * ptr, port_transmit_rate_kbps);
	priv->plat->tx_queues_cfg[queue].idle_slope = value & GENMASK(31, 0);

	value = div_s64(-qopt->sendslope * 1024ll * ptr, port_transmit_rate_kbps);
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
				 struct stmmac_flow_entry *entry,
				 struct netlink_ext_ack *extack)
{
	struct flow_action_entry *act;
	int i;

	if (!flow_action_has_entries(action))
		return -EINVAL;

	if (!flow_action_basic_hw_stats_check(action, extack))
		return -EOPNOTSUPP;

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

#define ETHER_TYPE_FULL_MASK	cpu_to_be16(~0)

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

	ret = tc_parse_flow_actions(priv, &rule->action, entry,
				    cls->common.extack);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(tc_flow_parsers); i++) {
		ret = tc_flow_parsers[i].fn(priv, cls, entry);
		if (!ret)
			entry->in_use = true;
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

static struct stmmac_rfs_entry *tc_find_rfs(struct stmmac_priv *priv,
					    struct flow_cls_offload *cls,
					    bool get_free)
{
	int i;

	for (i = 0; i < priv->rfs_entries_total; i++) {
		struct stmmac_rfs_entry *entry = &priv->rfs_entries[i];

		if (entry->cookie == cls->cookie)
			return entry;
		if (get_free && entry->in_use == false)
			return entry;
	}

	return NULL;
}

#define VLAN_PRIO_FULL_MASK (0x07)

static int tc_add_vlan_flow(struct stmmac_priv *priv,
			    struct flow_cls_offload *cls)
{
	struct stmmac_rfs_entry *entry = tc_find_rfs(priv, cls, false);
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	int tc = tc_classid_to_hwtc(priv->dev, cls->classid);
	struct flow_match_vlan match;

	if (!entry) {
		entry = tc_find_rfs(priv, cls, true);
		if (!entry)
			return -ENOENT;
	}

	if (priv->rfs_entries_cnt[STMMAC_RFS_T_VLAN] >=
	    priv->rfs_entries_max[STMMAC_RFS_T_VLAN])
		return -ENOENT;

	/* Nothing to do here */
	if (!dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_VLAN))
		return -EINVAL;

	if (tc < 0) {
		netdev_err(priv->dev, "Invalid traffic class\n");
		return -EINVAL;
	}

	flow_rule_match_vlan(rule, &match);

	if (match.mask->vlan_priority) {
		u32 prio;

		if (match.mask->vlan_priority != VLAN_PRIO_FULL_MASK) {
			netdev_err(priv->dev, "Only full mask is supported for VLAN priority");
			return -EINVAL;
		}

		prio = BIT(match.key->vlan_priority);
		stmmac_rx_queue_prio(priv, priv->hw, prio, tc);

		entry->in_use = true;
		entry->cookie = cls->cookie;
		entry->tc = tc;
		entry->type = STMMAC_RFS_T_VLAN;
		priv->rfs_entries_cnt[STMMAC_RFS_T_VLAN]++;
	}

	return 0;
}

static int tc_del_vlan_flow(struct stmmac_priv *priv,
			    struct flow_cls_offload *cls)
{
	struct stmmac_rfs_entry *entry = tc_find_rfs(priv, cls, false);

	if (!entry || !entry->in_use || entry->type != STMMAC_RFS_T_VLAN)
		return -ENOENT;

	stmmac_rx_queue_prio(priv, priv->hw, 0, entry->tc);

	entry->in_use = false;
	entry->cookie = 0;
	entry->tc = 0;
	entry->type = 0;

	priv->rfs_entries_cnt[STMMAC_RFS_T_VLAN]--;

	return 0;
}

static int tc_add_ethtype_flow(struct stmmac_priv *priv,
			       struct flow_cls_offload *cls)
{
	struct stmmac_rfs_entry *entry = tc_find_rfs(priv, cls, false);
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	int tc = tc_classid_to_hwtc(priv->dev, cls->classid);
	struct flow_match_basic match;

	if (!entry) {
		entry = tc_find_rfs(priv, cls, true);
		if (!entry)
			return -ENOENT;
	}

	/* Nothing to do here */
	if (!dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_BASIC))
		return -EINVAL;

	if (tc < 0) {
		netdev_err(priv->dev, "Invalid traffic class\n");
		return -EINVAL;
	}

	flow_rule_match_basic(rule, &match);

	if (match.mask->n_proto) {
		u16 etype = ntohs(match.key->n_proto);

		if (match.mask->n_proto != ETHER_TYPE_FULL_MASK) {
			netdev_err(priv->dev, "Only full mask is supported for EthType filter");
			return -EINVAL;
		}
		switch (etype) {
		case ETH_P_LLDP:
			if (priv->rfs_entries_cnt[STMMAC_RFS_T_LLDP] >=
			    priv->rfs_entries_max[STMMAC_RFS_T_LLDP])
				return -ENOENT;

			entry->type = STMMAC_RFS_T_LLDP;
			priv->rfs_entries_cnt[STMMAC_RFS_T_LLDP]++;

			stmmac_rx_queue_routing(priv, priv->hw,
						PACKET_DCBCPQ, tc);
			break;
		case ETH_P_1588:
			if (priv->rfs_entries_cnt[STMMAC_RFS_T_1588] >=
			    priv->rfs_entries_max[STMMAC_RFS_T_1588])
				return -ENOENT;

			entry->type = STMMAC_RFS_T_1588;
			priv->rfs_entries_cnt[STMMAC_RFS_T_1588]++;

			stmmac_rx_queue_routing(priv, priv->hw,
						PACKET_PTPQ, tc);
			break;
		default:
			netdev_err(priv->dev, "EthType(0x%x) is not supported", etype);
			return -EINVAL;
		}

		entry->in_use = true;
		entry->cookie = cls->cookie;
		entry->tc = tc;
		entry->etype = etype;

		return 0;
	}

	return -EINVAL;
}

static int tc_del_ethtype_flow(struct stmmac_priv *priv,
			       struct flow_cls_offload *cls)
{
	struct stmmac_rfs_entry *entry = tc_find_rfs(priv, cls, false);

	if (!entry || !entry->in_use ||
	    entry->type < STMMAC_RFS_T_LLDP ||
	    entry->type > STMMAC_RFS_T_1588)
		return -ENOENT;

	switch (entry->etype) {
	case ETH_P_LLDP:
		stmmac_rx_queue_routing(priv, priv->hw,
					PACKET_DCBCPQ, 0);
		priv->rfs_entries_cnt[STMMAC_RFS_T_LLDP]--;
		break;
	case ETH_P_1588:
		stmmac_rx_queue_routing(priv, priv->hw,
					PACKET_PTPQ, 0);
		priv->rfs_entries_cnt[STMMAC_RFS_T_1588]--;
		break;
	default:
		netdev_err(priv->dev, "EthType(0x%x) is not supported",
			   entry->etype);
		return -EINVAL;
	}

	entry->in_use = false;
	entry->cookie = 0;
	entry->tc = 0;
	entry->etype = 0;
	entry->type = 0;

	return 0;
}

static int tc_add_flow_cls(struct stmmac_priv *priv,
			   struct flow_cls_offload *cls)
{
	int ret;

	ret = tc_add_flow(priv, cls);
	if (!ret)
		return ret;

	ret = tc_add_ethtype_flow(priv, cls);
	if (!ret)
		return ret;

	return tc_add_vlan_flow(priv, cls);
}

static int tc_del_flow_cls(struct stmmac_priv *priv,
			   struct flow_cls_offload *cls)
{
	int ret;

	ret = tc_del_flow(priv, cls);
	if (!ret)
		return ret;

	ret = tc_del_ethtype_flow(priv, cls);
	if (!ret)
		return ret;

	return tc_del_vlan_flow(priv, cls);
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
		ret = tc_add_flow_cls(priv, cls);
		break;
	case FLOW_CLS_DESTROY:
		ret = tc_del_flow_cls(priv, cls);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

struct timespec64 stmmac_calc_tas_basetime(ktime_t old_base_time,
					   ktime_t current_time,
					   u64 cycle_time)
{
	struct timespec64 time;

	if (ktime_after(old_base_time, current_time)) {
		time = ktime_to_timespec64(old_base_time);
	} else {
		s64 n;
		ktime_t base_time;

		n = div64_s64(ktime_sub_ns(current_time, old_base_time),
			      cycle_time);
		base_time = ktime_add_ns(old_base_time,
					 (n + 1) * cycle_time);

		time = ktime_to_timespec64(base_time);
	}

	return time;
}

static void tc_taprio_map_maxsdu_txq(struct stmmac_priv *priv,
				     struct tc_taprio_qopt_offload *qopt)
{
	u32 num_tc = qopt->mqprio.qopt.num_tc;
	u32 offset, count, i, j;

	/* QueueMaxSDU received from the driver corresponds to the Linux traffic
	 * class. Map queueMaxSDU per Linux traffic class to DWMAC Tx queues.
	 */
	for (i = 0; i < num_tc; i++) {
		if (!qopt->max_sdu[i])
			continue;

		offset = qopt->mqprio.qopt.offset[i];
		count = qopt->mqprio.qopt.count[i];

		for (j = offset; j < offset + count; j++)
			priv->est->max_sdu[j] = qopt->max_sdu[i] + ETH_HLEN - ETH_TLEN;
	}
}

static int tc_taprio_configure(struct stmmac_priv *priv,
			       struct tc_taprio_qopt_offload *qopt)
{
	u32 size, wid = priv->dma_cap.estwid, dep = priv->dma_cap.estdep;
	struct netlink_ext_ack *extack = qopt->mqprio.extack;
	struct timespec64 time, current_time, qopt_time;
	ktime_t current_time_ns;
	int i, ret = 0;
	u64 ctr;

	if (qopt->base_time < 0)
		return -ERANGE;

	if (!priv->dma_cap.estsel)
		return -EOPNOTSUPP;

	switch (wid) {
	case 0x1:
		wid = 16;
		break;
	case 0x2:
		wid = 20;
		break;
	case 0x3:
		wid = 24;
		break;
	default:
		return -EOPNOTSUPP;
	}

	switch (dep) {
	case 0x1:
		dep = 64;
		break;
	case 0x2:
		dep = 128;
		break;
	case 0x3:
		dep = 256;
		break;
	case 0x4:
		dep = 512;
		break;
	case 0x5:
		dep = 1024;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (qopt->cmd == TAPRIO_CMD_DESTROY)
		goto disable;

	if (qopt->num_entries >= dep)
		return -EINVAL;
	if (!qopt->cycle_time)
		return -ERANGE;
	if (qopt->cycle_time_extension >= BIT(wid + 7))
		return -ERANGE;

	if (!priv->est) {
		priv->est = devm_kzalloc(priv->device, sizeof(*priv->est),
					 GFP_KERNEL);
		if (!priv->est)
			return -ENOMEM;

		mutex_init(&priv->est_lock);
	} else {
		mutex_lock(&priv->est_lock);
		memset(priv->est, 0, sizeof(*priv->est));
		mutex_unlock(&priv->est_lock);
	}

	size = qopt->num_entries;

	mutex_lock(&priv->est_lock);
	priv->est->gcl_size = size;
	priv->est->enable = qopt->cmd == TAPRIO_CMD_REPLACE;
	mutex_unlock(&priv->est_lock);

	for (i = 0; i < size; i++) {
		s64 delta_ns = qopt->entries[i].interval;
		u32 gates = qopt->entries[i].gate_mask;

		if (delta_ns > GENMASK(wid, 0))
			return -ERANGE;
		if (gates > GENMASK(31 - wid, 0))
			return -ERANGE;

		switch (qopt->entries[i].command) {
		case TC_TAPRIO_CMD_SET_GATES:
			break;
		case TC_TAPRIO_CMD_SET_AND_HOLD:
			gates |= BIT(0);
			break;
		case TC_TAPRIO_CMD_SET_AND_RELEASE:
			gates &= ~BIT(0);
			break;
		default:
			return -EOPNOTSUPP;
		}

		priv->est->gcl[i] = delta_ns | (gates << wid);
	}

	mutex_lock(&priv->est_lock);
	/* Adjust for real system time */
	priv->ptp_clock_ops.gettime64(&priv->ptp_clock_ops, &current_time);
	current_time_ns = timespec64_to_ktime(current_time);
	time = stmmac_calc_tas_basetime(qopt->base_time, current_time_ns,
					qopt->cycle_time);

	priv->est->btr[0] = (u32)time.tv_nsec;
	priv->est->btr[1] = (u32)time.tv_sec;

	qopt_time = ktime_to_timespec64(qopt->base_time);
	priv->est->btr_reserve[0] = (u32)qopt_time.tv_nsec;
	priv->est->btr_reserve[1] = (u32)qopt_time.tv_sec;

	ctr = qopt->cycle_time;
	priv->est->ctr[0] = do_div(ctr, NSEC_PER_SEC);
	priv->est->ctr[1] = (u32)ctr;

	priv->est->ter = qopt->cycle_time_extension;

	tc_taprio_map_maxsdu_txq(priv, qopt);

	ret = stmmac_est_configure(priv, priv, priv->est,
				   priv->plat->clk_ptp_rate);
	mutex_unlock(&priv->est_lock);
	if (ret) {
		netdev_err(priv->dev, "failed to configure EST\n");
		goto disable;
	}

	ret = stmmac_fpe_map_preemption_class(priv, priv->dev, extack,
					      qopt->mqprio.preemptible_tcs);
	if (ret)
		goto disable;

	return 0;

disable:
	if (priv->est) {
		mutex_lock(&priv->est_lock);
		priv->est->enable = false;
		stmmac_est_configure(priv, priv, priv->est,
				     priv->plat->clk_ptp_rate);
		/* Reset taprio status */
		for (i = 0; i < priv->plat->tx_queues_to_use; i++) {
			priv->xstats.max_sdu_txq_drop[i] = 0;
			priv->xstats.mtl_est_txq_hlbf[i] = 0;
		}
		mutex_unlock(&priv->est_lock);
	}

	stmmac_fpe_map_preemption_class(priv, priv->dev, extack, 0);

	return ret;
}

static void tc_taprio_stats(struct stmmac_priv *priv,
			    struct tc_taprio_qopt_offload *qopt)
{
	u64 window_drops = 0;
	int i = 0;

	for (i = 0; i < priv->plat->tx_queues_to_use; i++)
		window_drops += priv->xstats.max_sdu_txq_drop[i] +
				priv->xstats.mtl_est_txq_hlbf[i];
	qopt->stats.window_drops = window_drops;

	/* Transmission overrun doesn't happen for stmmac, hence always 0 */
	qopt->stats.tx_overruns = 0;
}

static void tc_taprio_queue_stats(struct stmmac_priv *priv,
				  struct tc_taprio_qopt_offload *qopt)
{
	struct tc_taprio_qopt_queue_stats *q_stats = &qopt->queue_stats;
	int queue = qopt->queue_stats.queue;

	q_stats->stats.window_drops = priv->xstats.max_sdu_txq_drop[queue] +
				      priv->xstats.mtl_est_txq_hlbf[queue];

	/* Transmission overrun doesn't happen for stmmac, hence always 0 */
	q_stats->stats.tx_overruns = 0;
}

static int tc_setup_taprio(struct stmmac_priv *priv,
			   struct tc_taprio_qopt_offload *qopt)
{
	int err = 0;

	switch (qopt->cmd) {
	case TAPRIO_CMD_REPLACE:
	case TAPRIO_CMD_DESTROY:
		err = tc_taprio_configure(priv, qopt);
		break;
	case TAPRIO_CMD_STATS:
		tc_taprio_stats(priv, qopt);
		break;
	case TAPRIO_CMD_QUEUE_STATS:
		tc_taprio_queue_stats(priv, qopt);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

static int tc_setup_taprio_without_fpe(struct stmmac_priv *priv,
				       struct tc_taprio_qopt_offload *qopt)
{
	if (!qopt->mqprio.preemptible_tcs)
		return tc_setup_taprio(priv, qopt);

	NL_SET_ERR_MSG_MOD(qopt->mqprio.extack,
			   "taprio with FPE is not implemented for this MAC");

	return -EOPNOTSUPP;
}

static int tc_setup_etf(struct stmmac_priv *priv,
			struct tc_etf_qopt_offload *qopt)
{
	if (!priv->dma_cap.tbssel)
		return -EOPNOTSUPP;
	if (qopt->queue >= priv->plat->tx_queues_to_use)
		return -EINVAL;
	if (!(priv->dma_conf.tx_queue[qopt->queue].tbs & STMMAC_TBS_AVAIL))
		return -EINVAL;

	if (qopt->enable)
		priv->dma_conf.tx_queue[qopt->queue].tbs |= STMMAC_TBS_EN;
	else
		priv->dma_conf.tx_queue[qopt->queue].tbs &= ~STMMAC_TBS_EN;

	netdev_info(priv->dev, "%s ETF for Queue %d\n",
		    qopt->enable ? "enabled" : "disabled", qopt->queue);
	return 0;
}

static int tc_query_caps(struct stmmac_priv *priv,
			 struct tc_query_caps_base *base)
{
	switch (base->type) {
	case TC_SETUP_QDISC_MQPRIO: {
		struct tc_mqprio_caps *caps = base->caps;

		caps->validate_queue_counts = true;

		return 0;
	}
	case TC_SETUP_QDISC_TAPRIO: {
		struct tc_taprio_caps *caps = base->caps;

		if (!priv->dma_cap.estsel)
			return -EOPNOTSUPP;

		caps->gate_mask_per_txq = true;
		caps->supports_queue_max_sdu = true;

		return 0;
	}
	default:
		return -EOPNOTSUPP;
	}
}

static void stmmac_reset_tc_mqprio(struct net_device *ndev,
				   struct netlink_ext_ack *extack)
{
	struct stmmac_priv *priv = netdev_priv(ndev);

	netdev_reset_tc(ndev);
	netif_set_real_num_tx_queues(ndev, priv->plat->tx_queues_to_use);
	stmmac_fpe_map_preemption_class(priv, ndev, extack, 0);
}

static int tc_setup_dwmac510_mqprio(struct stmmac_priv *priv,
				    struct tc_mqprio_qopt_offload *mqprio)
{
	struct netlink_ext_ack *extack = mqprio->extack;
	struct tc_mqprio_qopt *qopt = &mqprio->qopt;
	u32 offset, count, num_stack_tx_queues = 0;
	struct net_device *ndev = priv->dev;
	u32 num_tc = qopt->num_tc;
	int err;

	if (!num_tc) {
		stmmac_reset_tc_mqprio(ndev, extack);
		return 0;
	}

	err = netdev_set_num_tc(ndev, num_tc);
	if (err)
		return err;

	for (u32 tc = 0; tc < num_tc; tc++) {
		offset = qopt->offset[tc];
		count = qopt->count[tc];
		num_stack_tx_queues += count;

		err = netdev_set_tc_queue(ndev, tc, count, offset);
		if (err)
			goto err_reset_tc;
	}

	err = netif_set_real_num_tx_queues(ndev, num_stack_tx_queues);
	if (err)
		goto err_reset_tc;

	err = stmmac_fpe_map_preemption_class(priv, ndev, extack,
					      mqprio->preemptible_tcs);
	if (err)
		goto err_reset_tc;

	return 0;

err_reset_tc:
	stmmac_reset_tc_mqprio(ndev, extack);

	return err;
}

static int tc_setup_mqprio_unimplemented(struct stmmac_priv *priv,
					 struct tc_mqprio_qopt_offload *mqprio)
{
	NL_SET_ERR_MSG_MOD(mqprio->extack,
			   "mqprio HW offload is not implemented for this MAC");
	return -EOPNOTSUPP;
}

const struct stmmac_tc_ops dwmac4_tc_ops = {
	.init = tc_init,
	.setup_cls_u32 = tc_setup_cls_u32,
	.setup_cbs = tc_setup_cbs,
	.setup_cls = tc_setup_cls,
	.setup_taprio = tc_setup_taprio_without_fpe,
	.setup_etf = tc_setup_etf,
	.query_caps = tc_query_caps,
	.setup_mqprio = tc_setup_mqprio_unimplemented,
};

const struct stmmac_tc_ops dwmac510_tc_ops = {
	.init = tc_init,
	.setup_cls_u32 = tc_setup_cls_u32,
	.setup_cbs = tc_setup_cbs,
	.setup_cls = tc_setup_cls,
	.setup_taprio = tc_setup_taprio,
	.setup_etf = tc_setup_etf,
	.query_caps = tc_query_caps,
	.setup_mqprio = tc_setup_dwmac510_mqprio,
};
