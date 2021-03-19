// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Physcial Function ethernet driver
 *
 * Copyright (C) 2021 Marvell.
 */
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/rhashtable.h>
#include <linux/bitfield.h>
#include <net/flow_dissector.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_vlan.h>
#include <net/ipv6.h>

#include "otx2_common.h"

/* Egress rate limiting definitions */
#define MAX_BURST_EXPONENT		0x0FULL
#define MAX_BURST_MANTISSA		0xFFULL
#define MAX_BURST_SIZE			130816ULL
#define MAX_RATE_DIVIDER_EXPONENT	12ULL
#define MAX_RATE_EXPONENT		0x0FULL
#define MAX_RATE_MANTISSA		0xFFULL

/* Bitfields in NIX_TLX_PIR register */
#define TLX_RATE_MANTISSA		GENMASK_ULL(8, 1)
#define TLX_RATE_EXPONENT		GENMASK_ULL(12, 9)
#define TLX_RATE_DIVIDER_EXPONENT	GENMASK_ULL(16, 13)
#define TLX_BURST_MANTISSA		GENMASK_ULL(36, 29)
#define TLX_BURST_EXPONENT		GENMASK_ULL(40, 37)

struct otx2_tc_flow_stats {
	u64 bytes;
	u64 pkts;
	u64 used;
};

struct otx2_tc_flow {
	struct rhash_head		node;
	unsigned long			cookie;
	u16				entry;
	unsigned int			bitpos;
	struct rcu_head			rcu;
	struct otx2_tc_flow_stats	stats;
	spinlock_t			lock; /* lock for stats */
};

static void otx2_get_egress_burst_cfg(u32 burst, u32 *burst_exp,
				      u32 *burst_mantissa)
{
	unsigned int tmp;

	/* Burst is calculated as
	 * ((256 + BURST_MANTISSA) << (1 + BURST_EXPONENT)) / 256
	 * Max supported burst size is 130,816 bytes.
	 */
	burst = min_t(u32, burst, MAX_BURST_SIZE);
	if (burst) {
		*burst_exp = ilog2(burst) ? ilog2(burst) - 1 : 0;
		tmp = burst - rounddown_pow_of_two(burst);
		if (burst < MAX_BURST_MANTISSA)
			*burst_mantissa = tmp * 2;
		else
			*burst_mantissa = tmp / (1ULL << (*burst_exp - 7));
	} else {
		*burst_exp = MAX_BURST_EXPONENT;
		*burst_mantissa = MAX_BURST_MANTISSA;
	}
}

static void otx2_get_egress_rate_cfg(u32 maxrate, u32 *exp,
				     u32 *mantissa, u32 *div_exp)
{
	unsigned int tmp;

	/* Rate calculation by hardware
	 *
	 * PIR_ADD = ((256 + mantissa) << exp) / 256
	 * rate = (2 * PIR_ADD) / ( 1 << div_exp)
	 * The resultant rate is in Mbps.
	 */

	/* 2Mbps to 100Gbps can be expressed with div_exp = 0.
	 * Setting this to '0' will ease the calculation of
	 * exponent and mantissa.
	 */
	*div_exp = 0;

	if (maxrate) {
		*exp = ilog2(maxrate) ? ilog2(maxrate) - 1 : 0;
		tmp = maxrate - rounddown_pow_of_two(maxrate);
		if (maxrate < MAX_RATE_MANTISSA)
			*mantissa = tmp * 2;
		else
			*mantissa = tmp / (1ULL << (*exp - 7));
	} else {
		/* Instead of disabling rate limiting, set all values to max */
		*exp = MAX_RATE_EXPONENT;
		*mantissa = MAX_RATE_MANTISSA;
	}
}

static int otx2_set_matchall_egress_rate(struct otx2_nic *nic, u32 burst, u32 maxrate)
{
	struct otx2_hw *hw = &nic->hw;
	struct nix_txschq_config *req;
	u32 burst_exp, burst_mantissa;
	u32 exp, mantissa, div_exp;
	int txschq, err;

	/* All SQs share the same TL4, so pick the first scheduler */
	txschq = hw->txschq_list[NIX_TXSCH_LVL_TL4][0];

	/* Get exponent and mantissa values from the desired rate */
	otx2_get_egress_burst_cfg(burst, &burst_exp, &burst_mantissa);
	otx2_get_egress_rate_cfg(maxrate, &exp, &mantissa, &div_exp);

	mutex_lock(&nic->mbox.lock);
	req = otx2_mbox_alloc_msg_nix_txschq_cfg(&nic->mbox);
	if (!req) {
		mutex_unlock(&nic->mbox.lock);
		return -ENOMEM;
	}

	req->lvl = NIX_TXSCH_LVL_TL4;
	req->num_regs = 1;
	req->reg[0] = NIX_AF_TL4X_PIR(txschq);
	req->regval[0] = FIELD_PREP(TLX_BURST_EXPONENT, burst_exp) |
			 FIELD_PREP(TLX_BURST_MANTISSA, burst_mantissa) |
			 FIELD_PREP(TLX_RATE_DIVIDER_EXPONENT, div_exp) |
			 FIELD_PREP(TLX_RATE_EXPONENT, exp) |
			 FIELD_PREP(TLX_RATE_MANTISSA, mantissa) | BIT_ULL(0);

	err = otx2_sync_mbox_msg(&nic->mbox);
	mutex_unlock(&nic->mbox.lock);
	return err;
}

static int otx2_tc_validate_flow(struct otx2_nic *nic,
				 struct flow_action *actions,
				 struct netlink_ext_ack *extack)
{
	if (nic->flags & OTX2_FLAG_INTF_DOWN) {
		NL_SET_ERR_MSG_MOD(extack, "Interface not initialized");
		return -EINVAL;
	}

	if (!flow_action_has_entries(actions)) {
		NL_SET_ERR_MSG_MOD(extack, "MATCHALL offload called with no action");
		return -EINVAL;
	}

	if (!flow_offload_has_one_action(actions)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Egress MATCHALL offload supports only 1 policing action");
		return -EINVAL;
	}
	return 0;
}

static int otx2_tc_egress_matchall_install(struct otx2_nic *nic,
					   struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct flow_action *actions = &cls->rule->action;
	struct flow_action_entry *entry;
	u32 rate;
	int err;

	err = otx2_tc_validate_flow(nic, actions, extack);
	if (err)
		return err;

	if (nic->flags & OTX2_FLAG_TC_MATCHALL_EGRESS_ENABLED) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only one Egress MATCHALL ratelimiter can be offloaded");
		return -ENOMEM;
	}

	entry = &cls->rule->action.entries[0];
	switch (entry->id) {
	case FLOW_ACTION_POLICE:
		if (entry->police.rate_pkt_ps) {
			NL_SET_ERR_MSG_MOD(extack, "QoS offload not support packets per second");
			return -EOPNOTSUPP;
		}
		/* Convert bytes per second to Mbps */
		rate = entry->police.rate_bytes_ps * 8;
		rate = max_t(u32, rate / 1000000, 1);
		err = otx2_set_matchall_egress_rate(nic, entry->police.burst, rate);
		if (err)
			return err;
		nic->flags |= OTX2_FLAG_TC_MATCHALL_EGRESS_ENABLED;
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack,
				   "Only police action is supported with Egress MATCHALL offload");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int otx2_tc_egress_matchall_delete(struct otx2_nic *nic,
					  struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	int err;

	if (nic->flags & OTX2_FLAG_INTF_DOWN) {
		NL_SET_ERR_MSG_MOD(extack, "Interface not initialized");
		return -EINVAL;
	}

	err = otx2_set_matchall_egress_rate(nic, 0, 0);
	nic->flags &= ~OTX2_FLAG_TC_MATCHALL_EGRESS_ENABLED;
	return err;
}

static int otx2_tc_parse_actions(struct otx2_nic *nic,
				 struct flow_action *flow_action,
				 struct npc_install_flow_req *req)
{
	struct flow_action_entry *act;
	struct net_device *target;
	struct otx2_nic *priv;
	int i;

	if (!flow_action_has_entries(flow_action)) {
		netdev_info(nic->netdev, "no tc actions specified");
		return -EINVAL;
	}

	flow_action_for_each(i, act, flow_action) {
		switch (act->id) {
		case FLOW_ACTION_DROP:
			req->op = NIX_RX_ACTIONOP_DROP;
			return 0;
		case FLOW_ACTION_ACCEPT:
			req->op = NIX_RX_ACTION_DEFAULT;
			return 0;
		case FLOW_ACTION_REDIRECT_INGRESS:
			target = act->dev;
			priv = netdev_priv(target);
			/* npc_install_flow_req doesn't support passing a target pcifunc */
			if (rvu_get_pf(nic->pcifunc) != rvu_get_pf(priv->pcifunc)) {
				netdev_info(nic->netdev,
					    "can't redirect to other pf/vf\n");
				return -EOPNOTSUPP;
			}
			req->vf = priv->pcifunc & RVU_PFVF_FUNC_MASK;
			req->op = NIX_RX_ACTION_DEFAULT;
			return 0;
		case FLOW_ACTION_VLAN_POP:
			req->vtag0_valid = true;
			/* use RX_VTAG_TYPE7 which is initialized to strip vlan tag */
			req->vtag0_type = NIX_AF_LFX_RX_VTAG_TYPE7;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int otx2_tc_prepare_flow(struct otx2_nic *nic,
				struct flow_cls_offload *f,
				struct npc_install_flow_req *req)
{
	struct flow_msg *flow_spec = &req->packet;
	struct flow_msg *flow_mask = &req->mask;
	struct flow_dissector *dissector;
	struct flow_rule *rule;
	u8 ip_proto = 0;

	rule = flow_cls_offload_flow_rule(f);
	dissector = rule->match.dissector;

	if ((dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_IP))))  {
		netdev_info(nic->netdev, "unsupported flow used key 0x%x",
			    dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);

		/* All EtherTypes can be matched, no hw limitation */
		flow_spec->etype = match.key->n_proto;
		flow_mask->etype = match.mask->n_proto;
		req->features |= BIT_ULL(NPC_ETYPE);

		if (match.mask->ip_proto &&
		    (match.key->ip_proto != IPPROTO_TCP &&
		     match.key->ip_proto != IPPROTO_UDP &&
		     match.key->ip_proto != IPPROTO_SCTP &&
		     match.key->ip_proto != IPPROTO_ICMP &&
		     match.key->ip_proto != IPPROTO_ICMPV6)) {
			netdev_info(nic->netdev,
				    "ip_proto=0x%x not supported\n",
				    match.key->ip_proto);
			return -EOPNOTSUPP;
		}
		if (match.mask->ip_proto)
			ip_proto = match.key->ip_proto;

		if (ip_proto == IPPROTO_UDP)
			req->features |= BIT_ULL(NPC_IPPROTO_UDP);
		else if (ip_proto == IPPROTO_TCP)
			req->features |= BIT_ULL(NPC_IPPROTO_TCP);
		else if (ip_proto == IPPROTO_SCTP)
			req->features |= BIT_ULL(NPC_IPPROTO_SCTP);
		else if (ip_proto == IPPROTO_ICMP)
			req->features |= BIT_ULL(NPC_IPPROTO_ICMP);
		else if (ip_proto == IPPROTO_ICMPV6)
			req->features |= BIT_ULL(NPC_IPPROTO_ICMP6);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);
		if (!is_zero_ether_addr(match.mask->src)) {
			netdev_err(nic->netdev, "src mac match not supported\n");
			return -EOPNOTSUPP;
		}

		if (!is_zero_ether_addr(match.mask->dst)) {
			ether_addr_copy(flow_spec->dmac, (u8 *)&match.key->dst);
			ether_addr_copy(flow_mask->dmac,
					(u8 *)&match.mask->dst);
			req->features |= BIT_ULL(NPC_DMAC);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;

		flow_rule_match_ip(rule, &match);
		if ((ntohs(flow_spec->etype) != ETH_P_IP) &&
		    match.mask->tos) {
			netdev_err(nic->netdev, "tos not supported\n");
			return -EOPNOTSUPP;
		}
		if (match.mask->ttl) {
			netdev_err(nic->netdev, "ttl not supported\n");
			return -EOPNOTSUPP;
		}
		flow_spec->tos = match.key->tos;
		flow_mask->tos = match.mask->tos;
		req->features |= BIT_ULL(NPC_TOS);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;
		u16 vlan_tci, vlan_tci_mask;

		flow_rule_match_vlan(rule, &match);

		if (ntohs(match.key->vlan_tpid) != ETH_P_8021Q) {
			netdev_err(nic->netdev, "vlan tpid 0x%x not supported\n",
				   ntohs(match.key->vlan_tpid));
			return -EOPNOTSUPP;
		}

		if (match.mask->vlan_id ||
		    match.mask->vlan_dei ||
		    match.mask->vlan_priority) {
			vlan_tci = match.key->vlan_id |
				   match.key->vlan_dei << 12 |
				   match.key->vlan_priority << 13;

			vlan_tci_mask = match.mask->vlan_id |
					match.key->vlan_dei << 12 |
					match.key->vlan_priority << 13;

			flow_spec->vlan_tci = htons(vlan_tci);
			flow_mask->vlan_tci = htons(vlan_tci_mask);
			req->features |= BIT_ULL(NPC_OUTER_VID);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);

		flow_spec->ip4dst = match.key->dst;
		flow_mask->ip4dst = match.mask->dst;
		req->features |= BIT_ULL(NPC_DIP_IPV4);

		flow_spec->ip4src = match.key->src;
		flow_mask->ip4src = match.mask->src;
		req->features |= BIT_ULL(NPC_SIP_IPV4);
	} else if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);

		if (ipv6_addr_loopback(&match.key->dst) ||
		    ipv6_addr_loopback(&match.key->src)) {
			netdev_err(nic->netdev,
				   "Flow matching on IPv6 loopback addr is not supported\n");
			return -EOPNOTSUPP;
		}

		if (!ipv6_addr_any(&match.mask->dst)) {
			memcpy(&flow_spec->ip6dst,
			       (struct in6_addr *)&match.key->dst,
			       sizeof(flow_spec->ip6dst));
			memcpy(&flow_mask->ip6dst,
			       (struct in6_addr *)&match.mask->dst,
			       sizeof(flow_spec->ip6dst));
			req->features |= BIT_ULL(NPC_DIP_IPV6);
		}

		if (!ipv6_addr_any(&match.mask->src)) {
			memcpy(&flow_spec->ip6src,
			       (struct in6_addr *)&match.key->src,
			       sizeof(flow_spec->ip6src));
			memcpy(&flow_mask->ip6src,
			       (struct in6_addr *)&match.mask->src,
			       sizeof(flow_spec->ip6src));
			req->features |= BIT_ULL(NPC_SIP_IPV6);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);

		flow_spec->dport = match.key->dst;
		flow_mask->dport = match.mask->dst;
		if (ip_proto == IPPROTO_UDP)
			req->features |= BIT_ULL(NPC_DPORT_UDP);
		else if (ip_proto == IPPROTO_TCP)
			req->features |= BIT_ULL(NPC_DPORT_TCP);
		else if (ip_proto == IPPROTO_SCTP)
			req->features |= BIT_ULL(NPC_DPORT_SCTP);

		flow_spec->sport = match.key->src;
		flow_mask->sport = match.mask->src;
		if (ip_proto == IPPROTO_UDP)
			req->features |= BIT_ULL(NPC_SPORT_UDP);
		else if (ip_proto == IPPROTO_TCP)
			req->features |= BIT_ULL(NPC_SPORT_TCP);
		else if (ip_proto == IPPROTO_SCTP)
			req->features |= BIT_ULL(NPC_SPORT_SCTP);
	}

	return otx2_tc_parse_actions(nic, &rule->action, req);
}

static int otx2_del_mcam_flow_entry(struct otx2_nic *nic, u16 entry)
{
	struct npc_delete_flow_req *req;
	int err;

	mutex_lock(&nic->mbox.lock);
	req = otx2_mbox_alloc_msg_npc_delete_flow(&nic->mbox);
	if (!req) {
		mutex_unlock(&nic->mbox.lock);
		return -ENOMEM;
	}

	req->entry = entry;

	/* Send message to AF */
	err = otx2_sync_mbox_msg(&nic->mbox);
	if (err) {
		netdev_err(nic->netdev, "Failed to delete MCAM flow entry %d\n",
			   entry);
		mutex_unlock(&nic->mbox.lock);
		return -EFAULT;
	}
	mutex_unlock(&nic->mbox.lock);

	return 0;
}

static int otx2_tc_del_flow(struct otx2_nic *nic,
			    struct flow_cls_offload *tc_flow_cmd)
{
	struct otx2_tc_info *tc_info = &nic->tc_info;
	struct otx2_tc_flow *flow_node;

	flow_node = rhashtable_lookup_fast(&tc_info->flow_table,
					   &tc_flow_cmd->cookie,
					   tc_info->flow_ht_params);
	if (!flow_node) {
		netdev_err(nic->netdev, "tc flow not found for cookie 0x%lx\n",
			   tc_flow_cmd->cookie);
		return -EINVAL;
	}

	otx2_del_mcam_flow_entry(nic, flow_node->entry);

	WARN_ON(rhashtable_remove_fast(&nic->tc_info.flow_table,
				       &flow_node->node,
				       nic->tc_info.flow_ht_params));
	kfree_rcu(flow_node, rcu);

	clear_bit(flow_node->bitpos, tc_info->tc_entries_bitmap);
	tc_info->num_entries--;

	return 0;
}

static int otx2_tc_add_flow(struct otx2_nic *nic,
			    struct flow_cls_offload *tc_flow_cmd)
{
	struct otx2_tc_info *tc_info = &nic->tc_info;
	struct otx2_tc_flow *new_node, *old_node;
	struct npc_install_flow_req *req;
	int rc;

	if (!(nic->flags & OTX2_FLAG_TC_FLOWER_SUPPORT))
		return -ENOMEM;

	/* allocate memory for the new flow and it's node */
	new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node)
		return -ENOMEM;
	spin_lock_init(&new_node->lock);
	new_node->cookie = tc_flow_cmd->cookie;

	mutex_lock(&nic->mbox.lock);
	req = otx2_mbox_alloc_msg_npc_install_flow(&nic->mbox);
	if (!req) {
		mutex_unlock(&nic->mbox.lock);
		return -ENOMEM;
	}

	rc = otx2_tc_prepare_flow(nic, tc_flow_cmd, req);
	if (rc) {
		otx2_mbox_reset(&nic->mbox.mbox, 0);
		mutex_unlock(&nic->mbox.lock);
		return rc;
	}

	/* If a flow exists with the same cookie, delete it */
	old_node = rhashtable_lookup_fast(&tc_info->flow_table,
					  &tc_flow_cmd->cookie,
					  tc_info->flow_ht_params);
	if (old_node)
		otx2_tc_del_flow(nic, tc_flow_cmd);

	if (bitmap_full(tc_info->tc_entries_bitmap, nic->flow_cfg->tc_max_flows)) {
		netdev_err(nic->netdev, "Not enough MCAM space to add the flow\n");
		otx2_mbox_reset(&nic->mbox.mbox, 0);
		mutex_unlock(&nic->mbox.lock);
		return -ENOMEM;
	}

	new_node->bitpos = find_first_zero_bit(tc_info->tc_entries_bitmap,
					       nic->flow_cfg->tc_max_flows);
	req->channel = nic->hw.rx_chan_base;
	req->entry = nic->flow_cfg->entry[nic->flow_cfg->tc_flower_offset +
					  nic->flow_cfg->tc_max_flows - new_node->bitpos];
	req->intf = NIX_INTF_RX;
	req->set_cntr = 1;
	new_node->entry = req->entry;

	/* Send message to AF */
	rc = otx2_sync_mbox_msg(&nic->mbox);
	if (rc) {
		netdev_err(nic->netdev, "Failed to install MCAM flow entry\n");
		mutex_unlock(&nic->mbox.lock);
		goto out;
	}
	mutex_unlock(&nic->mbox.lock);

	/* add new flow to flow-table */
	rc = rhashtable_insert_fast(&nic->tc_info.flow_table, &new_node->node,
				    nic->tc_info.flow_ht_params);
	if (rc) {
		otx2_del_mcam_flow_entry(nic, req->entry);
		kfree_rcu(new_node, rcu);
		goto out;
	}

	set_bit(new_node->bitpos, tc_info->tc_entries_bitmap);
	tc_info->num_entries++;
out:
	return rc;
}

static int otx2_tc_get_flow_stats(struct otx2_nic *nic,
				  struct flow_cls_offload *tc_flow_cmd)
{
	struct otx2_tc_info *tc_info = &nic->tc_info;
	struct npc_mcam_get_stats_req *req;
	struct npc_mcam_get_stats_rsp *rsp;
	struct otx2_tc_flow_stats *stats;
	struct otx2_tc_flow *flow_node;
	int err;

	flow_node = rhashtable_lookup_fast(&tc_info->flow_table,
					   &tc_flow_cmd->cookie,
					   tc_info->flow_ht_params);
	if (!flow_node) {
		netdev_info(nic->netdev, "tc flow not found for cookie %lx",
			    tc_flow_cmd->cookie);
		return -EINVAL;
	}

	mutex_lock(&nic->mbox.lock);

	req = otx2_mbox_alloc_msg_npc_mcam_entry_stats(&nic->mbox);
	if (!req) {
		mutex_unlock(&nic->mbox.lock);
		return -ENOMEM;
	}

	req->entry = flow_node->entry;

	err = otx2_sync_mbox_msg(&nic->mbox);
	if (err) {
		netdev_err(nic->netdev, "Failed to get stats for MCAM flow entry %d\n",
			   req->entry);
		mutex_unlock(&nic->mbox.lock);
		return -EFAULT;
	}

	rsp = (struct npc_mcam_get_stats_rsp *)otx2_mbox_get_rsp
		(&nic->mbox.mbox, 0, &req->hdr);
	if (IS_ERR(rsp)) {
		mutex_unlock(&nic->mbox.lock);
		return PTR_ERR(rsp);
	}

	mutex_unlock(&nic->mbox.lock);

	if (!rsp->stat_ena)
		return -EINVAL;

	stats = &flow_node->stats;

	spin_lock(&flow_node->lock);
	flow_stats_update(&tc_flow_cmd->stats, 0x0, rsp->stat - stats->pkts, 0x0, 0x0,
			  FLOW_ACTION_HW_STATS_IMMEDIATE);
	stats->pkts = rsp->stat;
	spin_unlock(&flow_node->lock);

	return 0;
}

static int otx2_setup_tc_cls_flower(struct otx2_nic *nic,
				    struct flow_cls_offload *cls_flower)
{
	switch (cls_flower->command) {
	case FLOW_CLS_REPLACE:
		return otx2_tc_add_flow(nic, cls_flower);
	case FLOW_CLS_DESTROY:
		return otx2_tc_del_flow(nic, cls_flower);
	case FLOW_CLS_STATS:
		return otx2_tc_get_flow_stats(nic, cls_flower);
	default:
		return -EOPNOTSUPP;
	}
}

static int otx2_setup_tc_block_ingress_cb(enum tc_setup_type type,
					  void *type_data, void *cb_priv)
{
	struct otx2_nic *nic = cb_priv;

	if (!tc_cls_can_offload_and_chain0(nic->netdev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return otx2_setup_tc_cls_flower(nic, type_data);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int otx2_setup_tc_egress_matchall(struct otx2_nic *nic,
					 struct tc_cls_matchall_offload *cls_matchall)
{
	switch (cls_matchall->command) {
	case TC_CLSMATCHALL_REPLACE:
		return otx2_tc_egress_matchall_install(nic, cls_matchall);
	case TC_CLSMATCHALL_DESTROY:
		return otx2_tc_egress_matchall_delete(nic, cls_matchall);
	case TC_CLSMATCHALL_STATS:
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int otx2_setup_tc_block_egress_cb(enum tc_setup_type type,
					 void *type_data, void *cb_priv)
{
	struct otx2_nic *nic = cb_priv;

	if (!tc_cls_can_offload_and_chain0(nic->netdev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSMATCHALL:
		return otx2_setup_tc_egress_matchall(nic, type_data);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static LIST_HEAD(otx2_block_cb_list);

static int otx2_setup_tc_block(struct net_device *netdev,
			       struct flow_block_offload *f)
{
	struct otx2_nic *nic = netdev_priv(netdev);
	flow_setup_cb_t *cb;
	bool ingress;

	if (f->block_shared)
		return -EOPNOTSUPP;

	if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS) {
		cb = otx2_setup_tc_block_ingress_cb;
		ingress = true;
	} else if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS) {
		cb = otx2_setup_tc_block_egress_cb;
		ingress = false;
	} else {
		return -EOPNOTSUPP;
	}

	return flow_block_cb_setup_simple(f, &otx2_block_cb_list, cb,
					  nic, nic, ingress);
}

int otx2_setup_tc(struct net_device *netdev, enum tc_setup_type type,
		  void *type_data)
{
	switch (type) {
	case TC_SETUP_BLOCK:
		return otx2_setup_tc_block(netdev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct rhashtable_params tc_flow_ht_params = {
	.head_offset = offsetof(struct otx2_tc_flow, node),
	.key_offset = offsetof(struct otx2_tc_flow, cookie),
	.key_len = sizeof(((struct otx2_tc_flow *)0)->cookie),
	.automatic_shrinking = true,
};

int otx2_init_tc(struct otx2_nic *nic)
{
	struct otx2_tc_info *tc = &nic->tc_info;

	tc->flow_ht_params = tc_flow_ht_params;
	return rhashtable_init(&tc->flow_table, &tc->flow_ht_params);
}

void otx2_shutdown_tc(struct otx2_nic *nic)
{
	struct otx2_tc_info *tc = &nic->tc_info;

	rhashtable_destroy(&tc->flow_table);
}
