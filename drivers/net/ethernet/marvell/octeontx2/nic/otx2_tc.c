// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2021 Marvell.
 *
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

#include "cn10k.h"
#include "otx2_common.h"

/* Egress rate limiting definitions */
#define MAX_BURST_EXPONENT		0x0FULL
#define MAX_BURST_MANTISSA		0xFFULL
#define MAX_BURST_SIZE			130816ULL
#define MAX_RATE_DIVIDER_EXPONENT	12ULL
#define MAX_RATE_EXPONENT		0x0FULL
#define MAX_RATE_MANTISSA		0xFFULL

#define CN10K_MAX_BURST_MANTISSA	0x7FFFULL
#define CN10K_MAX_BURST_SIZE		8453888ULL

/* Bitfields in NIX_TLX_PIR register */
#define TLX_RATE_MANTISSA		GENMASK_ULL(8, 1)
#define TLX_RATE_EXPONENT		GENMASK_ULL(12, 9)
#define TLX_RATE_DIVIDER_EXPONENT	GENMASK_ULL(16, 13)
#define TLX_BURST_MANTISSA		GENMASK_ULL(36, 29)
#define TLX_BURST_EXPONENT		GENMASK_ULL(40, 37)

#define CN10K_TLX_BURST_MANTISSA	GENMASK_ULL(43, 29)
#define CN10K_TLX_BURST_EXPONENT	GENMASK_ULL(47, 44)

struct otx2_tc_flow_stats {
	u64 bytes;
	u64 pkts;
	u64 used;
};

struct otx2_tc_flow {
	struct rhash_head		node;
	unsigned long			cookie;
	unsigned int			bitpos;
	struct rcu_head			rcu;
	struct otx2_tc_flow_stats	stats;
	spinlock_t			lock; /* lock for stats */
	u16				rq;
	u16				entry;
	u16				leaf_profile;
	bool				is_act_police;
};

int otx2_tc_alloc_ent_bitmap(struct otx2_nic *nic)
{
	struct otx2_tc_info *tc = &nic->tc_info;

	if (!nic->flow_cfg->max_flows)
		return 0;

	/* Max flows changed, free the existing bitmap */
	kfree(tc->tc_entries_bitmap);

	tc->tc_entries_bitmap =
			kcalloc(BITS_TO_LONGS(nic->flow_cfg->max_flows),
				sizeof(long), GFP_KERNEL);
	if (!tc->tc_entries_bitmap) {
		netdev_err(nic->netdev,
			   "Unable to alloc TC flow entries bitmap\n");
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL(otx2_tc_alloc_ent_bitmap);

static void otx2_get_egress_burst_cfg(struct otx2_nic *nic, u32 burst,
				      u32 *burst_exp, u32 *burst_mantissa)
{
	int max_burst, max_mantissa;
	unsigned int tmp;

	if (is_dev_otx2(nic->pdev)) {
		max_burst = MAX_BURST_SIZE;
		max_mantissa = MAX_BURST_MANTISSA;
	} else {
		max_burst = CN10K_MAX_BURST_SIZE;
		max_mantissa = CN10K_MAX_BURST_MANTISSA;
	}

	/* Burst is calculated as
	 * ((256 + BURST_MANTISSA) << (1 + BURST_EXPONENT)) / 256
	 * Max supported burst size is 130,816 bytes.
	 */
	burst = min_t(u32, burst, max_burst);
	if (burst) {
		*burst_exp = ilog2(burst) ? ilog2(burst) - 1 : 0;
		tmp = burst - rounddown_pow_of_two(burst);
		if (burst < max_mantissa)
			*burst_mantissa = tmp * 2;
		else
			*burst_mantissa = tmp / (1ULL << (*burst_exp - 7));
	} else {
		*burst_exp = MAX_BURST_EXPONENT;
		*burst_mantissa = max_mantissa;
	}
}

static void otx2_get_egress_rate_cfg(u64 maxrate, u32 *exp,
				     u32 *mantissa, u32 *div_exp)
{
	u64 tmp;

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

static u64 otx2_get_txschq_rate_regval(struct otx2_nic *nic,
				       u64 maxrate, u32 burst)
{
	u32 burst_exp, burst_mantissa;
	u32 exp, mantissa, div_exp;
	u64 regval = 0;

	/* Get exponent and mantissa values from the desired rate */
	otx2_get_egress_burst_cfg(nic, burst, &burst_exp, &burst_mantissa);
	otx2_get_egress_rate_cfg(maxrate, &exp, &mantissa, &div_exp);

	if (is_dev_otx2(nic->pdev)) {
		regval = FIELD_PREP(TLX_BURST_EXPONENT, (u64)burst_exp) |
				FIELD_PREP(TLX_BURST_MANTISSA, (u64)burst_mantissa) |
				FIELD_PREP(TLX_RATE_DIVIDER_EXPONENT, div_exp) |
				FIELD_PREP(TLX_RATE_EXPONENT, exp) |
				FIELD_PREP(TLX_RATE_MANTISSA, mantissa) | BIT_ULL(0);
	} else {
		regval = FIELD_PREP(CN10K_TLX_BURST_EXPONENT, (u64)burst_exp) |
				FIELD_PREP(CN10K_TLX_BURST_MANTISSA, (u64)burst_mantissa) |
				FIELD_PREP(TLX_RATE_DIVIDER_EXPONENT, div_exp) |
				FIELD_PREP(TLX_RATE_EXPONENT, exp) |
				FIELD_PREP(TLX_RATE_MANTISSA, mantissa) | BIT_ULL(0);
	}

	return regval;
}

static int otx2_set_matchall_egress_rate(struct otx2_nic *nic,
					 u32 burst, u64 maxrate)
{
	struct otx2_hw *hw = &nic->hw;
	struct nix_txschq_config *req;
	int txschq, err;

	/* All SQs share the same TL4, so pick the first scheduler */
	txschq = hw->txschq_list[NIX_TXSCH_LVL_TL4][0];

	mutex_lock(&nic->mbox.lock);
	req = otx2_mbox_alloc_msg_nix_txschq_cfg(&nic->mbox);
	if (!req) {
		mutex_unlock(&nic->mbox.lock);
		return -ENOMEM;
	}

	req->lvl = NIX_TXSCH_LVL_TL4;
	req->num_regs = 1;
	req->reg[0] = NIX_AF_TL4X_PIR(txschq);
	req->regval[0] = otx2_get_txschq_rate_regval(nic, maxrate, burst);

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

static int otx2_policer_validate(const struct flow_action *action,
				 const struct flow_action_entry *act,
				 struct netlink_ext_ack *extack)
{
	if (act->police.exceed.act_id != FLOW_ACTION_DROP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when exceed action is not drop");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id != FLOW_ACTION_PIPE &&
	    act->police.notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is not pipe or ok");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id == FLOW_ACTION_ACCEPT &&
	    !flow_action_is_last_entry(action, act)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is ok, but action is not last");
		return -EOPNOTSUPP;
	}

	if (act->police.peakrate_bytes_ps ||
	    act->police.avrate || act->police.overhead) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when peakrate/avrate/overhead is configured");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int otx2_tc_egress_matchall_install(struct otx2_nic *nic,
					   struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct flow_action *actions = &cls->rule->action;
	struct flow_action_entry *entry;
	u64 rate;
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
		err = otx2_policer_validate(&cls->rule->action, entry, extack);
		if (err)
			return err;

		if (entry->police.rate_pkt_ps) {
			NL_SET_ERR_MSG_MOD(extack, "QoS offload not support packets per second");
			return -EOPNOTSUPP;
		}
		/* Convert bytes per second to Mbps */
		rate = entry->police.rate_bytes_ps * 8;
		rate = max_t(u64, rate / 1000000, 1);
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

static int otx2_tc_act_set_police(struct otx2_nic *nic,
				  struct otx2_tc_flow *node,
				  struct flow_cls_offload *f,
				  u64 rate, u32 burst, u32 mark,
				  struct npc_install_flow_req *req, bool pps)
{
	struct netlink_ext_ack *extack = f->common.extack;
	struct otx2_hw *hw = &nic->hw;
	int rq_idx, rc;

	rq_idx = find_first_zero_bit(&nic->rq_bmap, hw->rx_queues);
	if (rq_idx >= hw->rx_queues) {
		NL_SET_ERR_MSG_MOD(extack, "Police action rules exceeded");
		return -EINVAL;
	}

	mutex_lock(&nic->mbox.lock);

	rc = cn10k_alloc_leaf_profile(nic, &node->leaf_profile);
	if (rc) {
		mutex_unlock(&nic->mbox.lock);
		return rc;
	}

	rc = cn10k_set_ipolicer_rate(nic, node->leaf_profile, burst, rate, pps);
	if (rc)
		goto free_leaf;

	rc = cn10k_map_unmap_rq_policer(nic, rq_idx, node->leaf_profile, true);
	if (rc)
		goto free_leaf;

	mutex_unlock(&nic->mbox.lock);

	req->match_id = mark & 0xFFFFULL;
	req->index = rq_idx;
	req->op = NIX_RX_ACTIONOP_UCAST;
	set_bit(rq_idx, &nic->rq_bmap);
	node->is_act_police = true;
	node->rq = rq_idx;

	return 0;

free_leaf:
	if (cn10k_free_leaf_profile(nic, node->leaf_profile))
		netdev_err(nic->netdev,
			   "Unable to free leaf bandwidth profile(%d)\n",
			   node->leaf_profile);
	mutex_unlock(&nic->mbox.lock);
	return rc;
}

static int otx2_tc_parse_actions(struct otx2_nic *nic,
				 struct flow_action *flow_action,
				 struct npc_install_flow_req *req,
				 struct flow_cls_offload *f,
				 struct otx2_tc_flow *node)
{
	struct netlink_ext_ack *extack = f->common.extack;
	struct flow_action_entry *act;
	struct net_device *target;
	struct otx2_nic *priv;
	u32 burst, mark = 0;
	u8 nr_police = 0;
	bool pps = false;
	u64 rate;
	int err;
	int i;

	if (!flow_action_has_entries(flow_action)) {
		NL_SET_ERR_MSG_MOD(extack, "no tc actions specified");
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
				NL_SET_ERR_MSG_MOD(extack,
						   "can't redirect to other pf/vf");
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
		case FLOW_ACTION_POLICE:
			/* Ingress ratelimiting is not supported on OcteonTx2 */
			if (is_dev_otx2(nic->pdev)) {
				NL_SET_ERR_MSG_MOD(extack,
					"Ingress policing not supported on this platform");
				return -EOPNOTSUPP;
			}

			err = otx2_policer_validate(flow_action, act, extack);
			if (err)
				return err;

			if (act->police.rate_bytes_ps > 0) {
				rate = act->police.rate_bytes_ps * 8;
				burst = act->police.burst;
			} else if (act->police.rate_pkt_ps > 0) {
				/* The algorithm used to calculate rate
				 * mantissa, exponent values for a given token
				 * rate (token can be byte or packet) requires
				 * token rate to be mutiplied by 8.
				 */
				rate = act->police.rate_pkt_ps * 8;
				burst = act->police.burst_pkt;
				pps = true;
			}
			nr_police++;
			break;
		case FLOW_ACTION_MARK:
			mark = act->mark;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	if (nr_police > 1) {
		NL_SET_ERR_MSG_MOD(extack,
				   "rate limit police offload requires a single action");
		return -EOPNOTSUPP;
	}

	if (nr_police)
		return otx2_tc_act_set_police(nic, node, f, rate, burst,
					      mark, req, pps);

	return 0;
}

static int otx2_tc_prepare_flow(struct otx2_nic *nic, struct otx2_tc_flow *node,
				struct flow_cls_offload *f,
				struct npc_install_flow_req *req)
{
	struct netlink_ext_ack *extack = f->common.extack;
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
			NL_SET_ERR_MSG_MOD(extack, "src mac match not supported");
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
			NL_SET_ERR_MSG_MOD(extack, "tos not supported");
			return -EOPNOTSUPP;
		}
		if (match.mask->ttl) {
			NL_SET_ERR_MSG_MOD(extack, "ttl not supported");
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
					match.mask->vlan_dei << 12 |
					match.mask->vlan_priority << 13;

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
			NL_SET_ERR_MSG_MOD(extack,
					   "Flow matching IPv6 loopback addr not supported");
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

		if (flow_mask->dport) {
			if (ip_proto == IPPROTO_UDP)
				req->features |= BIT_ULL(NPC_DPORT_UDP);
			else if (ip_proto == IPPROTO_TCP)
				req->features |= BIT_ULL(NPC_DPORT_TCP);
			else if (ip_proto == IPPROTO_SCTP)
				req->features |= BIT_ULL(NPC_DPORT_SCTP);
		}

		flow_spec->sport = match.key->src;
		flow_mask->sport = match.mask->src;

		if (flow_mask->sport) {
			if (ip_proto == IPPROTO_UDP)
				req->features |= BIT_ULL(NPC_SPORT_UDP);
			else if (ip_proto == IPPROTO_TCP)
				req->features |= BIT_ULL(NPC_SPORT_TCP);
			else if (ip_proto == IPPROTO_SCTP)
				req->features |= BIT_ULL(NPC_SPORT_SCTP);
		}
	}

	return otx2_tc_parse_actions(nic, &rule->action, req, f, node);
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
	struct otx2_flow_config *flow_cfg = nic->flow_cfg;
	struct otx2_tc_info *tc_info = &nic->tc_info;
	struct otx2_tc_flow *flow_node;
	int err;

	flow_node = rhashtable_lookup_fast(&tc_info->flow_table,
					   &tc_flow_cmd->cookie,
					   tc_info->flow_ht_params);
	if (!flow_node) {
		netdev_err(nic->netdev, "tc flow not found for cookie 0x%lx\n",
			   tc_flow_cmd->cookie);
		return -EINVAL;
	}

	if (flow_node->is_act_police) {
		mutex_lock(&nic->mbox.lock);

		err = cn10k_map_unmap_rq_policer(nic, flow_node->rq,
						 flow_node->leaf_profile, false);
		if (err)
			netdev_err(nic->netdev,
				   "Unmapping RQ %d & profile %d failed\n",
				   flow_node->rq, flow_node->leaf_profile);

		err = cn10k_free_leaf_profile(nic, flow_node->leaf_profile);
		if (err)
			netdev_err(nic->netdev,
				   "Unable to free leaf bandwidth profile(%d)\n",
				   flow_node->leaf_profile);

		__clear_bit(flow_node->rq, &nic->rq_bmap);

		mutex_unlock(&nic->mbox.lock);
	}

	otx2_del_mcam_flow_entry(nic, flow_node->entry);

	WARN_ON(rhashtable_remove_fast(&nic->tc_info.flow_table,
				       &flow_node->node,
				       nic->tc_info.flow_ht_params));
	kfree_rcu(flow_node, rcu);

	clear_bit(flow_node->bitpos, tc_info->tc_entries_bitmap);
	flow_cfg->nr_flows--;

	return 0;
}

static int otx2_tc_add_flow(struct otx2_nic *nic,
			    struct flow_cls_offload *tc_flow_cmd)
{
	struct netlink_ext_ack *extack = tc_flow_cmd->common.extack;
	struct otx2_flow_config *flow_cfg = nic->flow_cfg;
	struct otx2_tc_info *tc_info = &nic->tc_info;
	struct otx2_tc_flow *new_node, *old_node;
	struct npc_install_flow_req *req, dummy;
	int rc, err;

	if (!(nic->flags & OTX2_FLAG_TC_FLOWER_SUPPORT))
		return -ENOMEM;

	if (bitmap_full(tc_info->tc_entries_bitmap, flow_cfg->max_flows)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Free MCAM entry not available to add the flow");
		return -ENOMEM;
	}

	/* allocate memory for the new flow and it's node */
	new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node)
		return -ENOMEM;
	spin_lock_init(&new_node->lock);
	new_node->cookie = tc_flow_cmd->cookie;

	memset(&dummy, 0, sizeof(struct npc_install_flow_req));

	rc = otx2_tc_prepare_flow(nic, new_node, tc_flow_cmd, &dummy);
	if (rc) {
		kfree_rcu(new_node, rcu);
		return rc;
	}

	/* If a flow exists with the same cookie, delete it */
	old_node = rhashtable_lookup_fast(&tc_info->flow_table,
					  &tc_flow_cmd->cookie,
					  tc_info->flow_ht_params);
	if (old_node)
		otx2_tc_del_flow(nic, tc_flow_cmd);

	mutex_lock(&nic->mbox.lock);
	req = otx2_mbox_alloc_msg_npc_install_flow(&nic->mbox);
	if (!req) {
		mutex_unlock(&nic->mbox.lock);
		rc = -ENOMEM;
		goto free_leaf;
	}

	memcpy(&dummy.hdr, &req->hdr, sizeof(struct mbox_msghdr));
	memcpy(req, &dummy, sizeof(struct npc_install_flow_req));

	new_node->bitpos = find_first_zero_bit(tc_info->tc_entries_bitmap,
					       flow_cfg->max_flows);
	req->channel = nic->hw.rx_chan_base;
	req->entry = flow_cfg->flow_ent[flow_cfg->max_flows - new_node->bitpos - 1];
	req->intf = NIX_INTF_RX;
	req->set_cntr = 1;
	new_node->entry = req->entry;

	/* Send message to AF */
	rc = otx2_sync_mbox_msg(&nic->mbox);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to install MCAM flow entry");
		mutex_unlock(&nic->mbox.lock);
		kfree_rcu(new_node, rcu);
		goto free_leaf;
	}
	mutex_unlock(&nic->mbox.lock);

	/* add new flow to flow-table */
	rc = rhashtable_insert_fast(&nic->tc_info.flow_table, &new_node->node,
				    nic->tc_info.flow_ht_params);
	if (rc) {
		otx2_del_mcam_flow_entry(nic, req->entry);
		kfree_rcu(new_node, rcu);
		goto free_leaf;
	}

	set_bit(new_node->bitpos, tc_info->tc_entries_bitmap);
	flow_cfg->nr_flows++;

	return 0;

free_leaf:
	if (new_node->is_act_police) {
		mutex_lock(&nic->mbox.lock);

		err = cn10k_map_unmap_rq_policer(nic, new_node->rq,
						 new_node->leaf_profile, false);
		if (err)
			netdev_err(nic->netdev,
				   "Unmapping RQ %d & profile %d failed\n",
				   new_node->rq, new_node->leaf_profile);
		err = cn10k_free_leaf_profile(nic, new_node->leaf_profile);
		if (err)
			netdev_err(nic->netdev,
				   "Unable to free leaf bandwidth profile(%d)\n",
				   new_node->leaf_profile);

		__clear_bit(new_node->rq, &nic->rq_bmap);

		mutex_unlock(&nic->mbox.lock);
	}

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

static int otx2_tc_ingress_matchall_install(struct otx2_nic *nic,
					    struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct flow_action *actions = &cls->rule->action;
	struct flow_action_entry *entry;
	u64 rate;
	int err;

	err = otx2_tc_validate_flow(nic, actions, extack);
	if (err)
		return err;

	if (nic->flags & OTX2_FLAG_TC_MATCHALL_INGRESS_ENABLED) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only one ingress MATCHALL ratelimitter can be offloaded");
		return -ENOMEM;
	}

	entry = &cls->rule->action.entries[0];
	switch (entry->id) {
	case FLOW_ACTION_POLICE:
		/* Ingress ratelimiting is not supported on OcteonTx2 */
		if (is_dev_otx2(nic->pdev)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Ingress policing not supported on this platform");
			return -EOPNOTSUPP;
		}

		err = cn10k_alloc_matchall_ipolicer(nic);
		if (err)
			return err;

		/* Convert to bits per second */
		rate = entry->police.rate_bytes_ps * 8;
		err = cn10k_set_matchall_ipolicer_rate(nic, entry->police.burst, rate);
		if (err)
			return err;
		nic->flags |= OTX2_FLAG_TC_MATCHALL_INGRESS_ENABLED;
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack,
				   "Only police action supported with Ingress MATCHALL offload");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int otx2_tc_ingress_matchall_delete(struct otx2_nic *nic,
					   struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	int err;

	if (nic->flags & OTX2_FLAG_INTF_DOWN) {
		NL_SET_ERR_MSG_MOD(extack, "Interface not initialized");
		return -EINVAL;
	}

	err = cn10k_free_matchall_ipolicer(nic);
	nic->flags &= ~OTX2_FLAG_TC_MATCHALL_INGRESS_ENABLED;
	return err;
}

static int otx2_setup_tc_ingress_matchall(struct otx2_nic *nic,
					  struct tc_cls_matchall_offload *cls_matchall)
{
	switch (cls_matchall->command) {
	case TC_CLSMATCHALL_REPLACE:
		return otx2_tc_ingress_matchall_install(nic, cls_matchall);
	case TC_CLSMATCHALL_DESTROY:
		return otx2_tc_ingress_matchall_delete(nic, cls_matchall);
	case TC_CLSMATCHALL_STATS:
	default:
		break;
	}

	return -EOPNOTSUPP;
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
	case TC_SETUP_CLSMATCHALL:
		return otx2_setup_tc_ingress_matchall(nic, type_data);
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
EXPORT_SYMBOL(otx2_setup_tc);

static const struct rhashtable_params tc_flow_ht_params = {
	.head_offset = offsetof(struct otx2_tc_flow, node),
	.key_offset = offsetof(struct otx2_tc_flow, cookie),
	.key_len = sizeof(((struct otx2_tc_flow *)0)->cookie),
	.automatic_shrinking = true,
};

int otx2_init_tc(struct otx2_nic *nic)
{
	struct otx2_tc_info *tc = &nic->tc_info;
	int err;

	/* Exclude receive queue 0 being used for police action */
	set_bit(0, &nic->rq_bmap);

	if (!nic->flow_cfg) {
		netdev_err(nic->netdev,
			   "Can't init TC, nic->flow_cfg is not setup\n");
		return -EINVAL;
	}

	err = otx2_tc_alloc_ent_bitmap(nic);
	if (err)
		return err;

	tc->flow_ht_params = tc_flow_ht_params;
	err = rhashtable_init(&tc->flow_table, &tc->flow_ht_params);
	if (err) {
		kfree(tc->tc_entries_bitmap);
		tc->tc_entries_bitmap = NULL;
	}
	return err;
}
EXPORT_SYMBOL(otx2_init_tc);

void otx2_shutdown_tc(struct otx2_nic *nic)
{
	struct otx2_tc_info *tc = &nic->tc_info;

	kfree(tc->tc_entries_bitmap);
	rhashtable_destroy(&tc->flow_table);
}
EXPORT_SYMBOL(otx2_shutdown_tc);
