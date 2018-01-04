/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/if_vlan.h>
#include <net/flow_dissector.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_skbedit.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_vlan.h>

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_sriov.h"
#include "bnxt_tc.h"
#include "bnxt_vfr.h"

#ifdef CONFIG_BNXT_FLOWER_OFFLOAD

#define BNXT_FID_INVALID			0xffff
#define VLAN_TCI(vid, prio)	((vid) | ((prio) << VLAN_PRIO_SHIFT))

/* Return the dst fid of the func for flow forwarding
 * For PFs: src_fid is the fid of the PF
 * For VF-reps: src_fid the fid of the VF
 */
static u16 bnxt_flow_get_dst_fid(struct bnxt *pf_bp, struct net_device *dev)
{
	struct bnxt *bp;

	/* check if dev belongs to the same switch */
	if (!switchdev_port_same_parent_id(pf_bp->dev, dev)) {
		netdev_info(pf_bp->dev, "dev(ifindex=%d) not on same switch",
			    dev->ifindex);
		return BNXT_FID_INVALID;
	}

	/* Is dev a VF-rep? */
	if (dev != pf_bp->dev)
		return bnxt_vf_rep_get_fid(dev);

	bp = netdev_priv(dev);
	return bp->pf.fw_fid;
}

static int bnxt_tc_parse_redir(struct bnxt *bp,
			       struct bnxt_tc_actions *actions,
			       const struct tc_action *tc_act)
{
	int ifindex = tcf_mirred_ifindex(tc_act);
	struct net_device *dev;
	u16 dst_fid;

	dev = __dev_get_by_index(dev_net(bp->dev), ifindex);
	if (!dev) {
		netdev_info(bp->dev, "no dev for ifindex=%d", ifindex);
		return -EINVAL;
	}

	/* find the FID from dev */
	dst_fid = bnxt_flow_get_dst_fid(bp, dev);
	if (dst_fid == BNXT_FID_INVALID) {
		netdev_info(bp->dev, "can't get fid for ifindex=%d", ifindex);
		return -EINVAL;
	}

	actions->flags |= BNXT_TC_ACTION_FLAG_FWD;
	actions->dst_fid = dst_fid;
	actions->dst_dev = dev;
	return 0;
}

static void bnxt_tc_parse_vlan(struct bnxt *bp,
			       struct bnxt_tc_actions *actions,
			       const struct tc_action *tc_act)
{
	if (tcf_vlan_action(tc_act) == TCA_VLAN_ACT_POP) {
		actions->flags |= BNXT_TC_ACTION_FLAG_POP_VLAN;
	} else if (tcf_vlan_action(tc_act) == TCA_VLAN_ACT_PUSH) {
		actions->flags |= BNXT_TC_ACTION_FLAG_PUSH_VLAN;
		actions->push_vlan_tci = htons(tcf_vlan_push_vid(tc_act));
		actions->push_vlan_tpid = tcf_vlan_push_proto(tc_act);
	}
}

static int bnxt_tc_parse_actions(struct bnxt *bp,
				 struct bnxt_tc_actions *actions,
				 struct tcf_exts *tc_exts)
{
	const struct tc_action *tc_act;
	LIST_HEAD(tc_actions);
	int rc;

	if (!tcf_exts_has_actions(tc_exts)) {
		netdev_info(bp->dev, "no actions");
		return -EINVAL;
	}

	tcf_exts_to_list(tc_exts, &tc_actions);
	list_for_each_entry(tc_act, &tc_actions, list) {
		/* Drop action */
		if (is_tcf_gact_shot(tc_act)) {
			actions->flags |= BNXT_TC_ACTION_FLAG_DROP;
			return 0; /* don't bother with other actions */
		}

		/* Redirect action */
		if (is_tcf_mirred_egress_redirect(tc_act)) {
			rc = bnxt_tc_parse_redir(bp, actions, tc_act);
			if (rc)
				return rc;
			continue;
		}

		/* Push/pop VLAN */
		if (is_tcf_vlan(tc_act)) {
			bnxt_tc_parse_vlan(bp, actions, tc_act);
			continue;
		}
	}

	return 0;
}

#define GET_KEY(flow_cmd, key_type)					\
		skb_flow_dissector_target((flow_cmd)->dissector, key_type,\
					  (flow_cmd)->key)
#define GET_MASK(flow_cmd, key_type)					\
		skb_flow_dissector_target((flow_cmd)->dissector, key_type,\
					  (flow_cmd)->mask)

static int bnxt_tc_parse_flow(struct bnxt *bp,
			      struct tc_cls_flower_offload *tc_flow_cmd,
			      struct bnxt_tc_flow *flow)
{
	struct flow_dissector *dissector = tc_flow_cmd->dissector;
	u16 addr_type = 0;

	/* KEY_CONTROL and KEY_BASIC are needed for forming a meaningful key */
	if ((dissector->used_keys & BIT(FLOW_DISSECTOR_KEY_CONTROL)) == 0 ||
	    (dissector->used_keys & BIT(FLOW_DISSECTOR_KEY_BASIC)) == 0) {
		netdev_info(bp->dev, "cannot form TC key: used_keys = 0x%x",
			    dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_CONTROL);

		addr_type = key->addr_type;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_BASIC);
		struct flow_dissector_key_basic *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_BASIC);

		flow->l2_key.ether_type = key->n_proto;
		flow->l2_mask.ether_type = mask->n_proto;

		if (key->n_proto == htons(ETH_P_IP) ||
		    key->n_proto == htons(ETH_P_IPV6)) {
			flow->l4_key.ip_proto = key->ip_proto;
			flow->l4_mask.ip_proto = mask->ip_proto;
		}
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_dissector_key_eth_addrs *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_ETH_ADDRS);
		struct flow_dissector_key_eth_addrs *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_ETH_ADDRS);

		flow->flags |= BNXT_TC_FLOW_FLAGS_ETH_ADDRS;
		ether_addr_copy(flow->l2_key.dmac, key->dst);
		ether_addr_copy(flow->l2_mask.dmac, mask->dst);
		ether_addr_copy(flow->l2_key.smac, key->src);
		ether_addr_copy(flow->l2_mask.smac, mask->src);
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_dissector_key_vlan *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_VLAN);
		struct flow_dissector_key_vlan *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_VLAN);

		flow->l2_key.inner_vlan_tci =
		   cpu_to_be16(VLAN_TCI(key->vlan_id, key->vlan_priority));
		flow->l2_mask.inner_vlan_tci =
		   cpu_to_be16((VLAN_TCI(mask->vlan_id, mask->vlan_priority)));
		flow->l2_key.inner_vlan_tpid = htons(ETH_P_8021Q);
		flow->l2_mask.inner_vlan_tpid = htons(0xffff);
		flow->l2_key.num_vlans = 1;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_dissector_key_ipv4_addrs *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_IPV4_ADDRS);
		struct flow_dissector_key_ipv4_addrs *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_IPV4_ADDRS);

		flow->flags |= BNXT_TC_FLOW_FLAGS_IPV4_ADDRS;
		flow->l3_key.ipv4.daddr.s_addr = key->dst;
		flow->l3_mask.ipv4.daddr.s_addr = mask->dst;
		flow->l3_key.ipv4.saddr.s_addr = key->src;
		flow->l3_mask.ipv4.saddr.s_addr = mask->src;
	} else if (dissector_uses_key(dissector,
				      FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_dissector_key_ipv6_addrs *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_IPV6_ADDRS);
		struct flow_dissector_key_ipv6_addrs *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_IPV6_ADDRS);

		flow->flags |= BNXT_TC_FLOW_FLAGS_IPV6_ADDRS;
		flow->l3_key.ipv6.daddr = key->dst;
		flow->l3_mask.ipv6.daddr = mask->dst;
		flow->l3_key.ipv6.saddr = key->src;
		flow->l3_mask.ipv6.saddr = mask->src;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_dissector_key_ports *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_PORTS);
		struct flow_dissector_key_ports *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_PORTS);

		flow->flags |= BNXT_TC_FLOW_FLAGS_PORTS;
		flow->l4_key.ports.dport = key->dst;
		flow->l4_mask.ports.dport = mask->dst;
		flow->l4_key.ports.sport = key->src;
		flow->l4_mask.ports.sport = mask->src;
	}

	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_ICMP)) {
		struct flow_dissector_key_icmp *key =
			GET_KEY(tc_flow_cmd, FLOW_DISSECTOR_KEY_ICMP);
		struct flow_dissector_key_icmp *mask =
			GET_MASK(tc_flow_cmd, FLOW_DISSECTOR_KEY_ICMP);

		flow->flags |= BNXT_TC_FLOW_FLAGS_ICMP;
		flow->l4_key.icmp.type = key->type;
		flow->l4_key.icmp.code = key->code;
		flow->l4_mask.icmp.type = mask->type;
		flow->l4_mask.icmp.code = mask->code;
	}

	return bnxt_tc_parse_actions(bp, &flow->actions, tc_flow_cmd->exts);
}

static int bnxt_hwrm_cfa_flow_free(struct bnxt *bp, __le16 flow_handle)
{
	struct hwrm_cfa_flow_free_input req = { 0 };
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_FLOW_FREE, -1, -1);
	req.flow_handle = flow_handle;

	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		netdev_info(bp->dev, "Error: %s: flow_handle=0x%x rc=%d",
			    __func__, flow_handle, rc);
	return rc;
}

static int ipv6_mask_len(struct in6_addr *mask)
{
	int mask_len = 0, i;

	for (i = 0; i < 4; i++)
		mask_len += inet_mask_len(mask->s6_addr32[i]);

	return mask_len;
}

static bool is_wildcard(void *mask, int len)
{
	const u8 *p = mask;
	int i;

	for (i = 0; i < len; i++) {
		if (p[i] != 0)
			return false;
	}
	return true;
}

static int bnxt_hwrm_cfa_flow_alloc(struct bnxt *bp, struct bnxt_tc_flow *flow,
				    __le16 ref_flow_handle, __le16 *flow_handle)
{
	struct hwrm_cfa_flow_alloc_output *resp = bp->hwrm_cmd_resp_addr;
	struct bnxt_tc_actions *actions = &flow->actions;
	struct bnxt_tc_l3_key *l3_mask = &flow->l3_mask;
	struct bnxt_tc_l3_key *l3_key = &flow->l3_key;
	struct hwrm_cfa_flow_alloc_input req = { 0 };
	u16 flow_flags = 0, action_flags = 0;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_FLOW_ALLOC, -1, -1);

	req.src_fid = cpu_to_le16(flow->src_fid);
	req.ref_flow_handle = ref_flow_handle;
	req.ethertype = flow->l2_key.ether_type;
	req.ip_proto = flow->l4_key.ip_proto;

	if (flow->flags & BNXT_TC_FLOW_FLAGS_ETH_ADDRS) {
		memcpy(req.dmac, flow->l2_key.dmac, ETH_ALEN);
		memcpy(req.smac, flow->l2_key.smac, ETH_ALEN);
	}

	if (flow->l2_key.num_vlans > 0) {
		flow_flags |= CFA_FLOW_ALLOC_REQ_FLAGS_NUM_VLAN_ONE;
		/* FW expects the inner_vlan_tci value to be set
		 * in outer_vlan_tci when num_vlans is 1 (which is
		 * always the case in TC.)
		 */
		req.outer_vlan_tci = flow->l2_key.inner_vlan_tci;
	}

	/* If all IP and L4 fields are wildcarded then this is an L2 flow */
	if (is_wildcard(l3_mask, sizeof(*l3_mask)) &&
	    is_wildcard(&flow->l4_mask, sizeof(flow->l4_mask))) {
		flow_flags |= CFA_FLOW_ALLOC_REQ_FLAGS_FLOWTYPE_L2;
	} else {
		flow_flags |= flow->l2_key.ether_type == htons(ETH_P_IP) ?
				CFA_FLOW_ALLOC_REQ_FLAGS_FLOWTYPE_IPV4 :
				CFA_FLOW_ALLOC_REQ_FLAGS_FLOWTYPE_IPV6;

		if (flow->flags & BNXT_TC_FLOW_FLAGS_IPV4_ADDRS) {
			req.ip_dst[0] = l3_key->ipv4.daddr.s_addr;
			req.ip_dst_mask_len =
				inet_mask_len(l3_mask->ipv4.daddr.s_addr);
			req.ip_src[0] = l3_key->ipv4.saddr.s_addr;
			req.ip_src_mask_len =
				inet_mask_len(l3_mask->ipv4.saddr.s_addr);
		} else if (flow->flags & BNXT_TC_FLOW_FLAGS_IPV6_ADDRS) {
			memcpy(req.ip_dst, l3_key->ipv6.daddr.s6_addr32,
			       sizeof(req.ip_dst));
			req.ip_dst_mask_len =
					ipv6_mask_len(&l3_mask->ipv6.daddr);
			memcpy(req.ip_src, l3_key->ipv6.saddr.s6_addr32,
			       sizeof(req.ip_src));
			req.ip_src_mask_len =
					ipv6_mask_len(&l3_mask->ipv6.saddr);
		}
	}

	if (flow->flags & BNXT_TC_FLOW_FLAGS_PORTS) {
		req.l4_src_port = flow->l4_key.ports.sport;
		req.l4_src_port_mask = flow->l4_mask.ports.sport;
		req.l4_dst_port = flow->l4_key.ports.dport;
		req.l4_dst_port_mask = flow->l4_mask.ports.dport;
	} else if (flow->flags & BNXT_TC_FLOW_FLAGS_ICMP) {
		/* l4 ports serve as type/code when ip_proto is ICMP */
		req.l4_src_port = htons(flow->l4_key.icmp.type);
		req.l4_src_port_mask = htons(flow->l4_mask.icmp.type);
		req.l4_dst_port = htons(flow->l4_key.icmp.code);
		req.l4_dst_port_mask = htons(flow->l4_mask.icmp.code);
	}
	req.flags = cpu_to_le16(flow_flags);

	if (actions->flags & BNXT_TC_ACTION_FLAG_DROP) {
		action_flags |= CFA_FLOW_ALLOC_REQ_ACTION_FLAGS_DROP;
	} else {
		if (actions->flags & BNXT_TC_ACTION_FLAG_FWD) {
			action_flags |= CFA_FLOW_ALLOC_REQ_ACTION_FLAGS_FWD;
			req.dst_fid = cpu_to_le16(actions->dst_fid);
		}
		if (actions->flags & BNXT_TC_ACTION_FLAG_PUSH_VLAN) {
			action_flags |=
			    CFA_FLOW_ALLOC_REQ_ACTION_FLAGS_L2_HEADER_REWRITE;
			req.l2_rewrite_vlan_tpid = actions->push_vlan_tpid;
			req.l2_rewrite_vlan_tci = actions->push_vlan_tci;
			memcpy(&req.l2_rewrite_dmac, &req.dmac, ETH_ALEN);
			memcpy(&req.l2_rewrite_smac, &req.smac, ETH_ALEN);
		}
		if (actions->flags & BNXT_TC_ACTION_FLAG_POP_VLAN) {
			action_flags |=
			    CFA_FLOW_ALLOC_REQ_ACTION_FLAGS_L2_HEADER_REWRITE;
			/* Rewrite config with tpid = 0 implies vlan pop */
			req.l2_rewrite_vlan_tpid = 0;
			memcpy(&req.l2_rewrite_dmac, &req.dmac, ETH_ALEN);
			memcpy(&req.l2_rewrite_smac, &req.smac, ETH_ALEN);
		}
	}
	req.action_flags = cpu_to_le16(action_flags);

	mutex_lock(&bp->hwrm_cmd_lock);

	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		*flow_handle = resp->flow_handle;

	mutex_unlock(&bp->hwrm_cmd_lock);

	return rc;
}

/* Add val to accum while handling a possible wraparound
 * of val. Eventhough val is of type u64, its actual width
 * is denoted by mask and will wrap-around beyond that width.
 */
static void accumulate_val(u64 *accum, u64 val, u64 mask)
{
#define low_bits(x, mask)		((x) & (mask))
#define high_bits(x, mask)		((x) & ~(mask))
	bool wrapped = val < low_bits(*accum, mask);

	*accum = high_bits(*accum, mask) + val;
	if (wrapped)
		*accum += (mask + 1);
}

/* The HW counters' width is much less than 64bits.
 * Handle possible wrap-around while updating the stat counters
 */
static void bnxt_flow_stats_fix_wraparound(struct bnxt_tc_info *tc_info,
					   struct bnxt_tc_flow_stats *stats,
					   struct bnxt_tc_flow_stats *hw_stats)
{
	accumulate_val(&stats->bytes, hw_stats->bytes, tc_info->bytes_mask);
	accumulate_val(&stats->packets, hw_stats->packets,
		       tc_info->packets_mask);
}

/* Fix possible wraparound of the stats queried from HW, calculate
 * the delta from prev_stats, and also update the prev_stats.
 * The HW flow stats are fetched under the hwrm_cmd_lock mutex.
 * This routine is best called while under the mutex so that the
 * stats processing happens atomically.
 */
static void bnxt_flow_stats_calc(struct bnxt_tc_info *tc_info,
				 struct bnxt_tc_flow *flow,
				 struct bnxt_tc_flow_stats *stats)
{
	struct bnxt_tc_flow_stats *acc_stats, *prev_stats;

	acc_stats = &flow->stats;
	bnxt_flow_stats_fix_wraparound(tc_info, acc_stats, stats);

	prev_stats = &flow->prev_stats;
	stats->bytes = acc_stats->bytes - prev_stats->bytes;
	stats->packets = acc_stats->packets - prev_stats->packets;
	*prev_stats = *acc_stats;
}

static int bnxt_hwrm_cfa_flow_stats_get(struct bnxt *bp,
					__le16 flow_handle,
					struct bnxt_tc_flow *flow,
					struct bnxt_tc_flow_stats *stats)
{
	struct hwrm_cfa_flow_stats_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_cfa_flow_stats_input req = { 0 };
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_CFA_FLOW_STATS, -1, -1);
	req.num_flows = cpu_to_le16(1);
	req.flow_handle_0 = flow_handle;

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		stats->packets = le64_to_cpu(resp->packet_0);
		stats->bytes = le64_to_cpu(resp->byte_0);
		bnxt_flow_stats_calc(&bp->tc_info, flow, stats);
	} else {
		netdev_info(bp->dev, "error rc=%d", rc);
	}

	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_tc_put_l2_node(struct bnxt *bp,
			       struct bnxt_tc_flow_node *flow_node)
{
	struct bnxt_tc_l2_node *l2_node = flow_node->l2_node;
	struct bnxt_tc_info *tc_info = &bp->tc_info;
	int rc;

	/* remove flow_node from the L2 shared flow list */
	list_del(&flow_node->l2_list_node);
	if (--l2_node->refcount == 0) {
		rc =  rhashtable_remove_fast(&tc_info->l2_table, &l2_node->node,
					     tc_info->l2_ht_params);
		if (rc)
			netdev_err(bp->dev,
				   "Error: %s: rhashtable_remove_fast: %d",
				   __func__, rc);
		kfree_rcu(l2_node, rcu);
	}
	return 0;
}

static struct bnxt_tc_l2_node *
bnxt_tc_get_l2_node(struct bnxt *bp, struct rhashtable *l2_table,
		    struct rhashtable_params ht_params,
		    struct bnxt_tc_l2_key *l2_key)
{
	struct bnxt_tc_l2_node *l2_node;
	int rc;

	l2_node = rhashtable_lookup_fast(l2_table, l2_key, ht_params);
	if (!l2_node) {
		l2_node = kzalloc(sizeof(*l2_node), GFP_KERNEL);
		if (!l2_node) {
			rc = -ENOMEM;
			return NULL;
		}

		l2_node->key = *l2_key;
		rc = rhashtable_insert_fast(l2_table, &l2_node->node,
					    ht_params);
		if (rc) {
			kfree(l2_node);
			netdev_err(bp->dev,
				   "Error: %s: rhashtable_insert_fast: %d",
				   __func__, rc);
			return NULL;
		}
		INIT_LIST_HEAD(&l2_node->common_l2_flows);
	}
	return l2_node;
}

/* Get the ref_flow_handle for a flow by checking if there are any other
 * flows that share the same L2 key as this flow.
 */
static int
bnxt_tc_get_ref_flow_handle(struct bnxt *bp, struct bnxt_tc_flow *flow,
			    struct bnxt_tc_flow_node *flow_node,
			    __le16 *ref_flow_handle)
{
	struct bnxt_tc_info *tc_info = &bp->tc_info;
	struct bnxt_tc_flow_node *ref_flow_node;
	struct bnxt_tc_l2_node *l2_node;

	l2_node = bnxt_tc_get_l2_node(bp, &tc_info->l2_table,
				      tc_info->l2_ht_params,
				      &flow->l2_key);
	if (!l2_node)
		return -1;

	/* If any other flow is using this l2_node, use it's flow_handle
	 * as the ref_flow_handle
	 */
	if (l2_node->refcount > 0) {
		ref_flow_node = list_first_entry(&l2_node->common_l2_flows,
						 struct bnxt_tc_flow_node,
						 l2_list_node);
		*ref_flow_handle = ref_flow_node->flow_handle;
	} else {
		*ref_flow_handle = cpu_to_le16(0xffff);
	}

	/* Insert the l2_node into the flow_node so that subsequent flows
	 * with a matching l2 key can use the flow_handle of this flow
	 * as their ref_flow_handle
	 */
	flow_node->l2_node = l2_node;
	list_add(&flow_node->l2_list_node, &l2_node->common_l2_flows);
	l2_node->refcount++;
	return 0;
}

/* After the flow parsing is done, this routine is used for checking
 * if there are any aspects of the flow that prevent it from being
 * offloaded.
 */
static bool bnxt_tc_can_offload(struct bnxt *bp, struct bnxt_tc_flow *flow)
{
	/* If L4 ports are specified then ip_proto must be TCP or UDP */
	if ((flow->flags & BNXT_TC_FLOW_FLAGS_PORTS) &&
	    (flow->l4_key.ip_proto != IPPROTO_TCP &&
	     flow->l4_key.ip_proto != IPPROTO_UDP)) {
		netdev_info(bp->dev, "Cannot offload non-TCP/UDP (%d) ports",
			    flow->l4_key.ip_proto);
		return false;
	}

	return true;
}

static int __bnxt_tc_del_flow(struct bnxt *bp,
			      struct bnxt_tc_flow_node *flow_node)
{
	struct bnxt_tc_info *tc_info = &bp->tc_info;
	int rc;

	/* send HWRM cmd to free the flow-id */
	bnxt_hwrm_cfa_flow_free(bp, flow_node->flow_handle);

	mutex_lock(&tc_info->lock);

	/* release reference to l2 node */
	bnxt_tc_put_l2_node(bp, flow_node);

	mutex_unlock(&tc_info->lock);

	rc = rhashtable_remove_fast(&tc_info->flow_table, &flow_node->node,
				    tc_info->flow_ht_params);
	if (rc)
		netdev_err(bp->dev, "Error: %s: rhashtable_remove_fast rc=%d",
			   __func__, rc);

	kfree_rcu(flow_node, rcu);
	return 0;
}

/* Add a new flow or replace an existing flow.
 * Notes on locking:
 * There are essentially two critical sections here.
 * 1. while adding a new flow
 *    a) lookup l2-key
 *    b) issue HWRM cmd and get flow_handle
 *    c) link l2-key with flow
 * 2. while deleting a flow
 *    a) unlinking l2-key from flow
 * A lock is needed to protect these two critical sections.
 *
 * The hash-tables are already protected by the rhashtable API.
 */
static int bnxt_tc_add_flow(struct bnxt *bp, u16 src_fid,
			    struct tc_cls_flower_offload *tc_flow_cmd)
{
	struct bnxt_tc_flow_node *new_node, *old_node;
	struct bnxt_tc_info *tc_info = &bp->tc_info;
	struct bnxt_tc_flow *flow;
	__le16 ref_flow_handle;
	int rc;

	/* allocate memory for the new flow and it's node */
	new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node) {
		rc = -ENOMEM;
		goto done;
	}
	new_node->cookie = tc_flow_cmd->cookie;
	flow = &new_node->flow;

	rc = bnxt_tc_parse_flow(bp, tc_flow_cmd, flow);
	if (rc)
		goto free_node;
	flow->src_fid = src_fid;

	if (!bnxt_tc_can_offload(bp, flow)) {
		rc = -ENOSPC;
		goto free_node;
	}

	/* If a flow exists with the same cookie, delete it */
	old_node = rhashtable_lookup_fast(&tc_info->flow_table,
					  &tc_flow_cmd->cookie,
					  tc_info->flow_ht_params);
	if (old_node)
		__bnxt_tc_del_flow(bp, old_node);

	/* Check if the L2 part of the flow has been offloaded already.
	 * If so, bump up it's refcnt and get it's reference handle.
	 */
	mutex_lock(&tc_info->lock);
	rc = bnxt_tc_get_ref_flow_handle(bp, flow, new_node, &ref_flow_handle);
	if (rc)
		goto unlock;

	/* send HWRM cmd to alloc the flow */
	rc = bnxt_hwrm_cfa_flow_alloc(bp, flow, ref_flow_handle,
				      &new_node->flow_handle);
	if (rc)
		goto put_l2;

	/* add new flow to flow-table */
	rc = rhashtable_insert_fast(&tc_info->flow_table, &new_node->node,
				    tc_info->flow_ht_params);
	if (rc)
		goto hwrm_flow_free;

	mutex_unlock(&tc_info->lock);
	return 0;

hwrm_flow_free:
	bnxt_hwrm_cfa_flow_free(bp, new_node->flow_handle);
put_l2:
	bnxt_tc_put_l2_node(bp, new_node);
unlock:
	mutex_unlock(&tc_info->lock);
free_node:
	kfree(new_node);
done:
	netdev_err(bp->dev, "Error: %s: cookie=0x%lx error=%d",
		   __func__, tc_flow_cmd->cookie, rc);
	return rc;
}

static int bnxt_tc_del_flow(struct bnxt *bp,
			    struct tc_cls_flower_offload *tc_flow_cmd)
{
	struct bnxt_tc_info *tc_info = &bp->tc_info;
	struct bnxt_tc_flow_node *flow_node;

	flow_node = rhashtable_lookup_fast(&tc_info->flow_table,
					   &tc_flow_cmd->cookie,
					   tc_info->flow_ht_params);
	if (!flow_node) {
		netdev_info(bp->dev, "ERROR: no flow_node for cookie %lx",
			    tc_flow_cmd->cookie);
		return -EINVAL;
	}

	return __bnxt_tc_del_flow(bp, flow_node);
}

static int bnxt_tc_get_flow_stats(struct bnxt *bp,
				  struct tc_cls_flower_offload *tc_flow_cmd)
{
	struct bnxt_tc_info *tc_info = &bp->tc_info;
	struct bnxt_tc_flow_node *flow_node;
	struct bnxt_tc_flow_stats stats;
	int rc;

	flow_node = rhashtable_lookup_fast(&tc_info->flow_table,
					   &tc_flow_cmd->cookie,
					   tc_info->flow_ht_params);
	if (!flow_node) {
		netdev_info(bp->dev, "Error: no flow_node for cookie %lx",
			    tc_flow_cmd->cookie);
		return -1;
	}

	rc = bnxt_hwrm_cfa_flow_stats_get(bp, flow_node->flow_handle,
					  &flow_node->flow, &stats);
	if (rc)
		return rc;

	tcf_exts_stats_update(tc_flow_cmd->exts, stats.bytes, stats.packets, 0);
	return 0;
}

int bnxt_tc_setup_flower(struct bnxt *bp, u16 src_fid,
			 struct tc_cls_flower_offload *cls_flower)
{
	int rc = 0;

	if (!is_classid_clsact_ingress(cls_flower->common.classid) ||
	    cls_flower->common.chain_index)
		return -EOPNOTSUPP;

	switch (cls_flower->command) {
	case TC_CLSFLOWER_REPLACE:
		rc = bnxt_tc_add_flow(bp, src_fid, cls_flower);
		break;

	case TC_CLSFLOWER_DESTROY:
		rc = bnxt_tc_del_flow(bp, cls_flower);
		break;

	case TC_CLSFLOWER_STATS:
		rc = bnxt_tc_get_flow_stats(bp, cls_flower);
		break;
	}
	return rc;
}

static const struct rhashtable_params bnxt_tc_flow_ht_params = {
	.head_offset = offsetof(struct bnxt_tc_flow_node, node),
	.key_offset = offsetof(struct bnxt_tc_flow_node, cookie),
	.key_len = sizeof(((struct bnxt_tc_flow_node *)0)->cookie),
	.automatic_shrinking = true
};

static const struct rhashtable_params bnxt_tc_l2_ht_params = {
	.head_offset = offsetof(struct bnxt_tc_l2_node, node),
	.key_offset = offsetof(struct bnxt_tc_l2_node, key),
	.key_len = BNXT_TC_L2_KEY_LEN,
	.automatic_shrinking = true
};

/* convert counter width in bits to a mask */
#define mask(width)		((u64)~0 >> (64 - (width)))

int bnxt_init_tc(struct bnxt *bp)
{
	struct bnxt_tc_info *tc_info = &bp->tc_info;
	int rc;

	if (bp->hwrm_spec_code < 0x10800) {
		netdev_warn(bp->dev,
			    "Firmware does not support TC flower offload.\n");
		return -ENOTSUPP;
	}
	mutex_init(&tc_info->lock);

	/* Counter widths are programmed by FW */
	tc_info->bytes_mask = mask(36);
	tc_info->packets_mask = mask(28);

	tc_info->flow_ht_params = bnxt_tc_flow_ht_params;
	rc = rhashtable_init(&tc_info->flow_table, &tc_info->flow_ht_params);
	if (rc)
		return rc;

	tc_info->l2_ht_params = bnxt_tc_l2_ht_params;
	rc = rhashtable_init(&tc_info->l2_table, &tc_info->l2_ht_params);
	if (rc)
		goto destroy_flow_table;

	tc_info->enabled = true;
	bp->dev->hw_features |= NETIF_F_HW_TC;
	bp->dev->features |= NETIF_F_HW_TC;
	return 0;

destroy_flow_table:
	rhashtable_destroy(&tc_info->flow_table);
	return rc;
}

void bnxt_shutdown_tc(struct bnxt *bp)
{
	struct bnxt_tc_info *tc_info = &bp->tc_info;

	if (!tc_info->enabled)
		return;

	rhashtable_destroy(&tc_info->flow_table);
	rhashtable_destroy(&tc_info->l2_table);
}

#else
#endif
