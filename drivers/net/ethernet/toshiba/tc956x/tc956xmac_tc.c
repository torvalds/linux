/*
 * TC956X ethernet driver.
 *
 * tc956xmac_tc.c
 *
 * Copyright (C) 2018 Synopsys, Inc. and/or its affiliates.
 * Copyright (C) 2023 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *  10 Nov 2023 : 1. Kernel 6.1 Porting changes
 *  VERSION     : 01-02-59
 *
 *  26 Dec 2023 : 1. Kernel 6.6 Porting changes
 *              : 2. Added the support for TC commands taprio and flower
 *  VERSION     : 01-03-59
 */

#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include "common.h"
#include "tc956xmac.h"
#include "dwxgmac2.h"

#ifdef TC956X_SRIOV_PF
static void tc_fill_all_pass_entry(struct tc956xmac_tc_entry *entry)
#elif defined TC956X_SRIOV_VF
static void tc_fill_all_pass_entry(struct tc956xmac_tc_entry *entry, struct tc956xmac_priv *priv)
#endif
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
#ifdef TC956X_SRIOV_PF
	entry->val.dma_ch_no = TC956X_TC_DEFAULT_DMA_CH;
#elif defined TC956X_SRIOV_VF
	entry->val.dma_ch_no = (1 << priv->plat->best_effort_ch_no);
#endif
}

static struct tc956xmac_tc_entry *tc_find_entry(struct tc956xmac_priv *priv,
					     struct tc_cls_u32_offload *cls,
					     bool free)
{
	struct tc956xmac_tc_entry *entry, *first = NULL, *dup = NULL;
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

static int tc_fill_actions(struct tc956xmac_tc_entry *entry,
			   struct tc956xmac_tc_entry *frag,
			   struct tc_cls_u32_offload *cls)
{
	struct tc956xmac_tc_entry *action_entry = entry;
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
		/* return -EINVAL; */
	}

	return 0;
}

static int tc_fill_entry(struct tc956xmac_priv *priv,
			 struct tc_cls_u32_offload *cls)
{
	struct tc956xmac_tc_entry *entry, *frag = NULL;
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
#ifdef TC956X_SRIOV_PF
	entry->val.dma_ch_no = TC956X_TC_DEFAULT_DMA_CH;
#elif defined TC956X_SRIOV_VF
	entry->val.dma_ch_no = (1 << priv->plat->best_effort_ch_no);
#endif
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

static void tc_unfill_entry(struct tc956xmac_priv *priv,
			    struct tc_cls_u32_offload *cls)
{
	struct tc956xmac_tc_entry *entry;

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

static int tc_config_knode(struct tc956xmac_priv *priv,
			   struct tc_cls_u32_offload *cls)
{
	int ret;
	u32 val, i = 0;
	void __iomem *ioaddr = priv->hw->pcsr;
	struct tc956xmac_rx_parser_cfg *cfg = &priv->plat->rxp_cfg;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif

	ret = tc_fill_entry(priv, cls);
	if (ret)
		return ret;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.frp, flags);
#endif
	ret = tc956xmac_rxp_config(priv, priv->hw->pcsr, priv->tc_entries,
			priv->tc_entries_max);
	if (ret)
		goto err_unfill;

	val = readl(ioaddr + XGMAC_MTL_RXP_CONTROL_STATUS);
	cfg->nve = val & XGMAC_NVE;
	cfg->npe = val & XGMAC_NVE;
	for (i = 0 ; i < cfg->nve ; i++)
		memcpy(&cfg->entries[i], &priv->tc_entries[i].val, sizeof(*cfg->entries));

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.frp, flags);
#endif
	return 0;

err_unfill:
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.frp, flags);
#endif
	tc_unfill_entry(priv, cls);
	return ret;
}

static int tc_delete_knode(struct tc956xmac_priv *priv,
			   struct tc_cls_u32_offload *cls)
{
	int ret;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	/* Set entry and fragments as not used */
	tc_unfill_entry(priv, cls);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.frp, flags);
#endif

	ret = tc956xmac_rxp_config(priv, priv->hw->pcsr, priv->tc_entries,
			priv->tc_entries_max);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.frp, flags);
#endif
	if (ret)
		return ret;

	return 0;
}

static int tc_setup_cls_u32(struct tc956xmac_priv *priv,
			    struct tc_cls_u32_offload *cls)
{
#ifdef TC956X_SRIOV_VF
	/* This is feature not supported from VF as this will impact
	 * the entire MAC
	 * */
	return -EOPNOTSUPP;
#endif
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

static int tc_rfs_init(struct tc956xmac_priv *priv)
{
	int i;

	priv->rfs_entries_max[TC956X_RFS_T_VLAN] = 8;
	priv->rfs_entries_max[TC956X_RFS_T_LLDP] = 1;
	priv->rfs_entries_max[TC956X_RFS_T_1588] = 1;

	for (i = 0; i < TC956X_RFS_T_MAX; i++)
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

static int tc_init(struct tc956xmac_priv *priv, void *data)
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

		dev_info(priv->device, "Enabled Flow TC (entries=%d)\n",
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
		priv->tc_off_max = SIXTY_FOUR;
		break;
	case 0x1:
		priv->tc_off_max = ONE_TWENTY_EIGHT;
		break;
	case 0x2:
		priv->tc_off_max = TWO_FIFTY_SIX;
		break;
	default:
		return -EINVAL;
	}

	count = dma_cap->frpes;

	/* Reserve one last filter which lets all pass */
	priv->tc_entries_max = count;
	priv->tc_entries = devm_kcalloc(priv->device,
			count, sizeof(*priv->tc_entries), GFP_KERNEL);
	if (!priv->tc_entries)
		return -ENOMEM;

#ifdef TC956X_SRIOV_PF
	tc_fill_all_pass_entry(&priv->tc_entries[count - 1]);
#elif defined TC956X_SRIOV_VF
	tc_fill_all_pass_entry(&priv->tc_entries[count - 1], priv);
#endif

	dev_info(priv->device, "Enabling HW TC (entries=%d, max_off=%d)\n",
			priv->tc_entries_max, priv->tc_off_max);
	return 0;
}

static int tc_setup_cbs(struct tc956xmac_priv *priv,
			struct tc_cbs_qopt_offload *qopt)
{
	u32 tx_queues_count = priv->plat->tx_queues_to_use;
	u32 queue = qopt->queue;
	u32 ptr, speed_div;
	u32 mode_to_use;
	u64 value;
	int ret;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
#endif
	/* Queue 0 is not AVB capable */
	if (queue <= 0 || queue >= tx_queues_count)
		return -EINVAL;
#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
	/* Only allows if Queue is allocated to PF */
	if (priv->plat->tx_queues_cfg[qopt->queue].mode_to_use ==
							MTL_QUEUE_DISABLE)
		return -EINVAL;
#endif
	if (!priv->dma_cap.av)
		return -EOPNOTSUPP;

	mode_to_use = priv->plat->tx_queues_cfg[queue].mode_to_use;
	/* Return when mode is other than AVB */
	if (mode_to_use != MTL_QUEUE_AVB) {
		DBGPR_FUNC(priv->device, "--> %s (mode_to_use != MTL_QUEUE_AVB) mode_to_use %d\n", __func__, mode_to_use);
		return -EINVAL;
	}

#ifndef TC956X
	if (mode_to_use == MTL_QUEUE_DCB && qopt->enable) {
		ret = tc956xmac_dma_qmode(priv, priv->ioaddr, queue, MTL_QUEUE_AVB);
		if (ret)
			return ret;

		priv->plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_AVB;
	} else if (!qopt->enable) {
		/* Removed : when called "tc del" code Changing property
		 * such as mode from AVB to DCB and reset register values
		 */

		ret = tc956xmac_dma_qmode(priv, priv->ioaddr, queue, MTL_QUEUE_DCB);
		if (ret)
			return ret;

		priv->plat->tx_queues_cfg[queue].mode_to_use = MTL_QUEUE_DCB;

		return 0;
	}
#endif

	/* Port Transmit Rate and Speed Divider */
	switch (priv->speed) {
	case SPEED_10000:
		ptr = 32;
		speed_div = SPD_DIV_10G;
		break;
	case SPEED_5000:
		ptr = 32;
		speed_div = SPD_DIV_5G;
		break;
	case SPEED_2500:
		ptr = 8;
		speed_div = SPD_DIV_2_5G;
		break;
	case SPEED_1000:
		ptr = 8;
		speed_div = SPD_DIV_1G;
		break;
	case SPEED_100:
		ptr = 4;
		speed_div = SPD_DIV_100M;
		break;
	default:
		return -EOPNOTSUPP;
	}

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_lock_irqsave(&priv->spn_lock.cbs, flags);
#endif
	/* Final adjustments for HW */
	value = div_s64(qopt->idleslope * 1024ll * ptr, speed_div);
	priv->plat->tx_queues_cfg[queue].idle_slope = value & GENMASK(20, 0);

	value = div_s64(-qopt->sendslope * 1024ll * ptr, speed_div);
	priv->plat->tx_queues_cfg[queue].send_slope = value & GENMASK(15, 0);

	value = qopt->hicredit * 1024ll * 8;
	priv->plat->tx_queues_cfg[queue].high_credit = value & GENMASK(28, 0);

	value = qopt->locredit * 1024ll * 8;
	priv->plat->tx_queues_cfg[queue].low_credit = value & GENMASK(28, 0);

#ifndef TC956X_SRIOV_VF
	ret = tc956xmac_config_cbs(priv, priv->hw,
				priv->plat->tx_queues_cfg[queue].send_slope,
				priv->plat->tx_queues_cfg[queue].idle_slope,
				priv->plat->tx_queues_cfg[queue].high_credit,
				priv->plat->tx_queues_cfg[queue].low_credit,
				queue);
#elif (defined TC956X_SRIOV_VF)
	ret = tc956xmac_config_cbs(priv,
			priv->plat->tx_queues_cfg[queue].send_slope,
			priv->plat->tx_queues_cfg[queue].idle_slope,
			priv->plat->tx_queues_cfg[queue].high_credit,
			priv->plat->tx_queues_cfg[queue].low_credit,
			queue);
#endif

	if (ret) {
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
		spin_unlock_irqrestore(&priv->spn_lock.cbs, flags);
#endif
		return ret;
	}

	if (priv->speed == SPEED_100) {
		priv->cbs_speed100_cfg[queue].send_slope =
			priv->plat->tx_queues_cfg[queue].send_slope;
		priv->cbs_speed100_cfg[queue].idle_slope =
			priv->plat->tx_queues_cfg[queue].idle_slope;
		priv->cbs_speed100_cfg[queue].high_credit =
			priv->plat->tx_queues_cfg[queue].high_credit;
		priv->cbs_speed100_cfg[queue].low_credit =
			priv->plat->tx_queues_cfg[queue].low_credit;
	} else if (priv->speed == SPEED_1000) {
		priv->cbs_speed1000_cfg[queue].send_slope =
			priv->plat->tx_queues_cfg[queue].send_slope;
		priv->cbs_speed1000_cfg[queue].idle_slope =
			priv->plat->tx_queues_cfg[queue].idle_slope;
		priv->cbs_speed1000_cfg[queue].high_credit =
			priv->plat->tx_queues_cfg[queue].high_credit;
		priv->cbs_speed1000_cfg[queue].low_credit =
			priv->plat->tx_queues_cfg[queue].low_credit;
	} else if (priv->speed == SPEED_10000) {
		priv->cbs_speed10000_cfg[queue].send_slope =
			priv->plat->tx_queues_cfg[queue].send_slope;
		priv->cbs_speed10000_cfg[queue].idle_slope =
			priv->plat->tx_queues_cfg[queue].idle_slope;
		priv->cbs_speed10000_cfg[queue].high_credit =
			priv->plat->tx_queues_cfg[queue].high_credit;
		priv->cbs_speed10000_cfg[queue].low_credit =
			priv->plat->tx_queues_cfg[queue].low_credit;
	} else if (priv->speed == SPEED_2500) {
		priv->cbs_speed2500_cfg[queue].send_slope =
			priv->plat->tx_queues_cfg[queue].send_slope;
		priv->cbs_speed2500_cfg[queue].idle_slope =
			priv->plat->tx_queues_cfg[queue].idle_slope;
		priv->cbs_speed2500_cfg[queue].high_credit =
			priv->plat->tx_queues_cfg[queue].high_credit;
		priv->cbs_speed2500_cfg[queue].low_credit =
			priv->plat->tx_queues_cfg[queue].low_credit;
	} else if (priv->speed == SPEED_5000) {
		priv->cbs_speed5000_cfg[queue].send_slope =
			priv->plat->tx_queues_cfg[queue].send_slope;
		priv->cbs_speed5000_cfg[queue].idle_slope =
			priv->plat->tx_queues_cfg[queue].idle_slope;
		priv->cbs_speed5000_cfg[queue].high_credit =
			priv->plat->tx_queues_cfg[queue].high_credit;
		priv->cbs_speed5000_cfg[queue].low_credit =
			priv->plat->tx_queues_cfg[queue].low_credit;
	} else {
	}

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.cbs, flags);
#endif
	dev_info(priv->device, "CBS queue %d: send %d, idle %d, hi %d, lo %d\n",
			queue, qopt->sendslope, qopt->idleslope,
			qopt->hicredit, qopt->locredit);
	return 0;
}

static int tc_parse_flow_actions(struct tc956xmac_priv *priv,
				 struct flow_action *action,
				 struct tc956xmac_flow_entry *entry)
{
	struct flow_action_entry *act;
	int i;

	if (!flow_action_has_entries(action))
		return -EINVAL;

	flow_action_for_each(i, act, action) {
		switch (act->id) {
		case FLOW_ACTION_DROP:
			entry->action |= TC956XMAC_FLOW_ACTION_DROP;
			return 0;
		default:
			break;
		}
	}

	/* Nothing to do, maybe inverse filter ? */
	return 0;
}

static int tc_add_basic_flow(struct tc956xmac_priv *priv,
			     struct flow_cls_offload *cls,
			     struct tc956xmac_flow_entry *entry)
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

static int tc_add_ip4_flow(struct tc956xmac_priv *priv,
			   struct flow_cls_offload *cls,
			   struct tc956xmac_flow_entry *entry)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	bool inv = entry->action & TC956XMAC_FLOW_ACTION_DROP;
	struct flow_match_ipv4_addrs match;
	u32 hw_match;
	int ret;

	/* Nothing to do here */
	if (!dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_IPV4_ADDRS))
		return -EINVAL;

	flow_rule_match_ipv4_addrs(rule, &match);
	hw_match = ntohl(match.key->src) & ntohl(match.mask->src);
	if (hw_match) {
		ret = tc956xmac_config_l3_filter(priv, priv->hw, entry->idx, true,
					      false, true, inv, hw_match);
		if (ret)
			return ret;
	}

	hw_match = ntohl(match.key->dst) & ntohl(match.mask->dst);
	if (hw_match) {
		ret = tc956xmac_config_l3_filter(priv, priv->hw, entry->idx, true,
					      false, false, inv, hw_match);
		if (ret)
			return ret;
	}

	return 0;
}

static int tc_add_ports_flow(struct tc956xmac_priv *priv,
			     struct flow_cls_offload *cls,
			     struct tc956xmac_flow_entry *entry)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	bool inv = entry->action & TC956XMAC_FLOW_ACTION_DROP;
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
		ret = tc956xmac_config_l4_filter(priv, priv->hw, entry->idx, true,
					      is_udp, true, inv, hw_match);
		if (ret)
			return ret;
	}

	hw_match = ntohs(match.key->dst) & ntohs(match.mask->dst);
	if (hw_match) {
		ret = tc956xmac_config_l4_filter(priv, priv->hw, entry->idx, true,
					      is_udp, false, inv, hw_match);
		if (ret)
			return ret;
	}

	entry->is_l4 = true;
	return 0;
}

static struct tc956xmac_flow_entry *tc_find_flow(struct tc956xmac_priv *priv,
					      struct flow_cls_offload *cls,
					      bool get_free)
{
	int i;

	for (i = 0; i < priv->flow_entries_max; i++) {
		struct tc956xmac_flow_entry *entry = &priv->flow_entries[i];

		if (entry->cookie == cls->cookie)
			return entry;
		if (get_free && (entry->in_use == false))
			return entry;
	}

	return NULL;
}

static struct {
	int (*fn)(struct tc956xmac_priv *priv, struct flow_cls_offload *cls,
		  struct tc956xmac_flow_entry *entry);
} tc_flow_parsers[] = {
	{ .fn = tc_add_basic_flow },
	{ .fn = tc_add_ip4_flow },
	{ .fn = tc_add_ports_flow },
};

static struct tc956xmac_rfs_entry *tc_find_rfs(struct tc956xmac_priv *priv,
					    struct flow_cls_offload *cls,
					    bool get_free)
{
	int i;

	for (i = 0; i < priv->rfs_entries_total; i++) {
		struct tc956xmac_rfs_entry *entry = &priv->rfs_entries[i];

		if (entry->cookie == cls->cookie)
			return entry;
		if (get_free && entry->in_use == false)
			return entry;
	}

	return NULL;
}

#define ETHER_TYPE_FULL_MASK	cpu_to_be16(~0)

static int tc_add_ethtype_flow(struct tc956xmac_priv *priv,
			       struct flow_cls_offload *cls)
{
	struct tc956xmac_rfs_entry *entry = tc_find_rfs(priv, cls, false);
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
			if (priv->rfs_entries_cnt[TC956X_RFS_T_LLDP] >=
			    priv->rfs_entries_max[TC956X_RFS_T_LLDP])
				return -ENOENT;

			entry->type = TC956X_RFS_T_LLDP;
			priv->rfs_entries_cnt[TC956X_RFS_T_LLDP]++;

			tc956xmac_rx_queue_routing(priv, priv->hw,
						PACKET_DCBCPQ, tc);
			break;
		case ETH_P_1588:
			if (priv->rfs_entries_cnt[TC956X_RFS_T_1588] >=
			    priv->rfs_entries_max[TC956X_RFS_T_1588])
				return -ENOENT;

			entry->type = TC956X_RFS_T_1588;
			priv->rfs_entries_cnt[TC956X_RFS_T_1588]++;

			tc956xmac_rx_queue_routing(priv, priv->hw,
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

#define VLAN_PRIO_FULL_MASK (0x07)

static int tc_add_vlan_flow(struct tc956xmac_priv *priv,
			    struct flow_cls_offload *cls)
{
	struct tc956xmac_rfs_entry *entry = tc_find_rfs(priv, cls, false);
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	int tc = tc_classid_to_hwtc(priv->dev, cls->classid);
	struct flow_match_vlan match;

	if (!entry) {
		entry = tc_find_rfs(priv, cls, true);
		if (!entry)
			return -ENOENT;
	}

	if (priv->rfs_entries_cnt[TC956X_RFS_T_VLAN] >=
	    priv->rfs_entries_max[TC956X_RFS_T_VLAN])
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
		tc956xmac_rx_queue_prio(priv, priv->hw, prio, tc);

		entry->in_use = true;
		entry->cookie = cls->cookie;
		entry->tc = tc;
		entry->type = TC956X_RFS_T_VLAN;
		priv->rfs_entries_cnt[TC956X_RFS_T_VLAN]++;
	}
	return 0;
}

static int tc_add_flow(struct tc956xmac_priv *priv,
		       struct flow_cls_offload *cls)
{
	struct tc956xmac_flow_entry *entry = tc_find_flow(priv, cls, false);
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

static int tc_del_flow(struct tc956xmac_priv *priv,
		       struct flow_cls_offload *cls)
{
	struct tc956xmac_flow_entry *entry = tc_find_flow(priv, cls, false);
	int ret;

	if (!entry || !entry->in_use)
		return -ENOENT;

	if (entry->is_l4) {
		ret = tc956xmac_config_l4_filter(priv, priv->hw, entry->idx, false,
					      false, false, false, 0);
	} else {
		ret = tc956xmac_config_l3_filter(priv, priv->hw, entry->idx, false,
					      false, false, false, 0);
	}

	entry->in_use = false;
	entry->cookie = 0;
	entry->is_l4 = false;
	return ret;
}

static int tc_del_ethtype_flow(struct tc956xmac_priv *priv,
			       struct flow_cls_offload *cls)
{
	struct tc956xmac_rfs_entry *entry = tc_find_rfs(priv, cls, false);

	if (!entry || !entry->in_use ||
	    entry->type < TC956X_RFS_T_LLDP ||
	    entry->type > TC956X_RFS_T_1588)
		return -ENOENT;

	switch (entry->etype) {
	case ETH_P_LLDP:
		tc956xmac_rx_queue_routing(priv, priv->hw,
					PACKET_DCBCPQ, 0);
		priv->rfs_entries_cnt[TC956X_RFS_T_LLDP]--;
		break;
	case ETH_P_1588:
		tc956xmac_rx_queue_routing(priv, priv->hw,
					PACKET_PTPQ, 0);
		priv->rfs_entries_cnt[TC956X_RFS_T_1588]--;
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

static int tc_del_vlan_flow(struct tc956xmac_priv *priv,
			    struct flow_cls_offload *cls)
{
	struct tc956xmac_rfs_entry *entry = tc_find_rfs(priv, cls, false);

	if (!entry || !entry->in_use || entry->type != TC956X_RFS_T_VLAN)
		return -ENOENT;

	tc956xmac_rx_queue_prio(priv, priv->hw, 0, entry->tc);

	entry->in_use = false;
	entry->cookie = 0;
	entry->tc = 0;
	entry->type = 0;

	priv->rfs_entries_cnt[TC956X_RFS_T_VLAN]--;

	return 0;
}

static int tc_add_flow_cls(struct tc956xmac_priv *priv,
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

static int tc_del_flow_cls(struct tc956xmac_priv *priv,
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

static int tc_setup_cls(struct tc956xmac_priv *priv,
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

#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 5, 0))
struct timespec64 tc956x_calc_basetime(ktime_t old_base_time,
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

static int tc_setup_taprio(struct tc956xmac_priv *priv,
			   struct tc_taprio_qopt_offload *qopt)
{
	u32 size, wid = priv->dma_cap.estwid, dep = priv->dma_cap.estdep;
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
	u64 quotient;
	u32 reminder;
#endif
	struct plat_tc956xmacenet_data *plat = priv->plat;
	struct timespec64 time;
	bool fpe = false;
	int i, ret = 0;

	u64 system_time;
	ktime_t current_time_ns;

	u32 tx_channels_count = priv->plat->tx_queues_to_use;
	u32 chan = 0;
	unsigned long gates;

	if (!priv->dma_cap.estsel)
		return -EOPNOTSUPP;

	switch (wid) {
	case 0x1:
		wid = SIXTEEN;
		break;
	case 0x2:
		wid = TWENTY;
		break;
	case 0x3:
		wid = TWENTY_FOUR;
		break;
	default:
		return -EOPNOTSUPP;
	}

	switch (dep) {
	case 0x1:
		dep = SIXTY_FOUR;
		break;
	case 0x2:
		dep = ONE_TWENTY_EIGHT;
		break;
	case 0x3:
		dep = TWO_FIFTY_SIX;
		break;
	case 0x4:
		dep = FIVE_HUNDRED_TWELVE;
		break;
	case 0x5:
		dep = THOUSAND_TWENTY_FOUR;
		break;
	default:
		return -EOPNOTSUPP;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 5))
	if (!qopt->enable)
		goto disable;
#else
	if (qopt->cmd == TAPRIO_CMD_DESTROY)
		goto disable;
	else if (qopt->cmd != TAPRIO_CMD_REPLACE)
		return -EOPNOTSUPP;
#endif
	if (qopt->num_entries >= dep)
		return -EINVAL;
	if (!qopt->base_time)
		return -ERANGE;
	if (!qopt->cycle_time)
		return -ERANGE;

	if (!plat->est) {
		plat->est = devm_kzalloc(priv->device, sizeof(*plat->est),
					 GFP_KERNEL);
		if (!plat->est)
			return -ENOMEM;
	} else {
		memset(plat->est, 0, sizeof(*plat->est));
	}

	size = qopt->num_entries;

	priv->plat->est->gcl_size = size;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 5))
	priv->plat->est->enable = qopt->enable;
#else
	priv->plat->est->enable = qopt->cmd == TAPRIO_CMD_REPLACE;
#endif
	for (i = 0; i < size; i++) {
		s64 delta_ns = qopt->entries[i].interval;
		unsigned long gate_list = qopt->entries[i].gate_mask;

		gates = 0;
		for (chan = 0; chan < tx_channels_count; chan++) {
#ifdef TC956X_SRIOV_PF
			if (priv->plat->tx_q_in_use[chan] == TC956X_DISABLE_QUEUE)
				continue;
#endif

			if (priv->plat->gate_mask == 1) { /* kernel returns gate_mask with tc_map_to_queue_mask. so queue mask to tc map to be done */
				if (test_bit(chan, &gate_list))
					set_bit(priv->plat->tx_queues_cfg[chan].traffic_class, &gates);
			}
		}

		if (priv->plat->gate_mask == 0) /* If tc_map_to_queue_mask disabled, then no need of queue mask to tc map */
			gates = gate_list;

		if (delta_ns > GENMASK(wid, 0))
			return -ERANGE;
		if (gates > GENMASK(31 - wid, 0))
			return -ERANGE;

		switch (qopt->entries[i].command) {
		case TC_TAPRIO_CMD_SET_GATES:
			if (fpe)
				return -EINVAL;
			break;
		case TC_TAPRIO_CMD_SET_AND_HOLD:
			gates |= BIT(0);
			fpe = true;
			break;
		case TC_TAPRIO_CMD_SET_AND_RELEASE:
			gates &= ~BIT(0);
			fpe = true;
			break;
		default:
			return -EOPNOTSUPP;
		}

		priv->plat->est->gcl[i] = delta_ns | (gates << wid);
	}

	/* Adjust for real system time */

	tc956xmac_get_systime(priv, priv->ptpaddr, &system_time);
	current_time_ns = ns_to_ktime(system_time);
	time = tc956x_calc_basetime(qopt->base_time, current_time_ns, qopt->cycle_time);

	tc956xmac_get_systime(priv, priv->ptpaddr, &system_time);

	priv->plat->est->btr[0] = (u32)time.tv_nsec;
	priv->plat->est->btr[1] = (u32)time.tv_sec;
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
	quotient = div_u64_rem(qopt->cycle_time, NSEC_PER_SEC, &reminder);
	priv->plat->est->ctr[0] = reminder;
	priv->plat->est->ctr[1] = (u32) quotient;
#else
	priv->plat->est->ctr[0] = (u32)(qopt->cycle_time % NSEC_PER_SEC);
	priv->plat->est->ctr[1] = (u32)(qopt->cycle_time / NSEC_PER_SEC);
#endif

	if (fpe && !priv->dma_cap.fpesel)
		return -EOPNOTSUPP;
#ifdef TC956X_UNSUPPORTED_UNTESTED
	ret = tc956xmac_fpe_configure(priv, priv->ioaddr,
				   priv->plat->tx_queues_to_use,
				   priv->plat->rx_queues_to_use, fpe);
	if (ret && fpe) {
		netdev_err(priv->dev, "failed to enable Frame Preemption\n");
		return ret;
	}
#endif
	ret = tc956xmac_est_configure(priv, priv->ioaddr, priv->plat->est,
				   priv->plat->clk_ptp_rate);
	if (ret) {
		netdev_err(priv->dev, "failed to configure EST\n");
		goto disable;
	}

	netdev_info(priv->dev, "configured EST\n");
	return 0;

disable:
	priv->plat->est->enable = false;
	tc956xmac_est_configure(priv, priv->ioaddr, priv->plat->est,
			     priv->plat->clk_ptp_rate);
	return ret;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(6, 2, 16))
static int tc_query_caps(struct tc956xmac_priv *priv,
			 struct tc_query_caps_base *base)
{
	switch (base->type) {
	case TC_SETUP_QDISC_TAPRIO: {
		struct tc_taprio_caps *caps = base->caps;

		if (!priv->dma_cap.estsel)
			return -EOPNOTSUPP;

		caps->gate_mask_per_txq = true;
		if (caps->gate_mask_per_txq)
			priv->plat->gate_mask = 1;
		else
			priv->plat->gate_mask = 0;

		return 0;
	}
	default:
		return -EOPNOTSUPP;
	}
}
#endif
#endif

static int tc_setup_etf(struct tc956xmac_priv *priv,
			struct tc_etf_qopt_offload *qopt)
{
#ifndef TC956X_SRIOV_VF
	u8 vf;
#endif
	bool is_pf_ch = false;

	if (!priv->dma_cap.tbssel)
		return -EOPNOTSUPP;
	if (qopt->queue >= priv->plat->tx_queues_to_use)
		return -EINVAL;
#ifdef TC956X_SRIOV_VF
	/* Only allows if Queue is allocated to VF */
	if (priv->plat->tx_queues_cfg[qopt->queue].mode_to_use ==
							MTL_QUEUE_DISABLE)
		return -EINVAL;
#else
#ifdef TC956X_AUTOMOTIVE_CONFIG
	vf = priv->dma_vf_map[qopt->queue];
	if (vf == 0)
		is_pf_ch = true;

#else
	vf = priv->dma_vf_map[qopt->queue];
	if (vf == 3)
		is_pf_ch = true;
#endif
#endif
	if (!(priv->tx_queue[qopt->queue].tbs & TC956XMAC_TBS_AVAIL)) {
		if (is_pf_ch == false && priv->fn_id_info.vf_no != 0)
			return -EINVAL;
		else if (is_pf_ch == true)
			return -EINVAL;
	}

	if (qopt->enable)
		priv->tx_queue[qopt->queue].tbs |= TC956XMAC_TBS_EN;
	else
		priv->tx_queue[qopt->queue].tbs &= ~TC956XMAC_TBS_EN;

#ifndef TC956X_SRIOV_VF
	if (is_pf_ch == false && priv->fn_id_info.vf_no == 0)
		tc956x_mbx_wrap_setup_etf(priv, qopt->queue, vf);
#endif

	netdev_info(priv->dev, "%s ETF for Queue %d\n",
		    qopt->enable ? "enabled" : "disabled", qopt->queue);
	return 0;
}

const struct tc956xmac_tc_ops dwmac510_tc_ops = {
	.init = tc_init,
	.setup_cls_u32 = tc_setup_cls_u32,
	.setup_cbs = tc_setup_cbs,
	.setup_cls = tc_setup_cls,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 5, 0))
	.setup_taprio = tc_setup_taprio,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(6, 2, 16))
	.query_caps = tc_query_caps,
#endif
#endif
	.setup_etf = tc_setup_etf,
};
