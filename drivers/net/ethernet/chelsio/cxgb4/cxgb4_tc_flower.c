/*
 * This file is part of the Chelsio T4/T5/T6 Ethernet driver for Linux.
 *
 * Copyright (c) 2017 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_pedit.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_vlan.h>

#include "cxgb4.h"
#include "cxgb4_filter.h"
#include "cxgb4_tc_flower.h"

#define STATS_CHECK_PERIOD (HZ / 2)

static struct ch_tc_pedit_fields pedits[] = {
	PEDIT_FIELDS(ETH_, DMAC_31_0, 4, dmac, 0),
	PEDIT_FIELDS(ETH_, DMAC_47_32, 2, dmac, 4),
	PEDIT_FIELDS(ETH_, SMAC_15_0, 2, smac, 0),
	PEDIT_FIELDS(ETH_, SMAC_47_16, 4, smac, 2),
	PEDIT_FIELDS(IP4_, SRC, 4, nat_fip, 0),
	PEDIT_FIELDS(IP4_, DST, 4, nat_lip, 0),
	PEDIT_FIELDS(IP6_, SRC_31_0, 4, nat_fip, 0),
	PEDIT_FIELDS(IP6_, SRC_63_32, 4, nat_fip, 4),
	PEDIT_FIELDS(IP6_, SRC_95_64, 4, nat_fip, 8),
	PEDIT_FIELDS(IP6_, SRC_127_96, 4, nat_fip, 12),
	PEDIT_FIELDS(IP6_, DST_31_0, 4, nat_lip, 0),
	PEDIT_FIELDS(IP6_, DST_63_32, 4, nat_lip, 4),
	PEDIT_FIELDS(IP6_, DST_95_64, 4, nat_lip, 8),
	PEDIT_FIELDS(IP6_, DST_127_96, 4, nat_lip, 12),
};

static const struct cxgb4_natmode_config cxgb4_natmode_config_array[] = {
	/* Default supported NAT modes */
	{
		.chip = CHELSIO_T5,
		.flags = CXGB4_ACTION_NATMODE_NONE,
		.natmode = NAT_MODE_NONE,
	},
	{
		.chip = CHELSIO_T5,
		.flags = CXGB4_ACTION_NATMODE_DIP,
		.natmode = NAT_MODE_DIP,
	},
	{
		.chip = CHELSIO_T5,
		.flags = CXGB4_ACTION_NATMODE_DIP | CXGB4_ACTION_NATMODE_DPORT,
		.natmode = NAT_MODE_DIP_DP,
	},
	{
		.chip = CHELSIO_T5,
		.flags = CXGB4_ACTION_NATMODE_DIP | CXGB4_ACTION_NATMODE_DPORT |
			 CXGB4_ACTION_NATMODE_SIP,
		.natmode = NAT_MODE_DIP_DP_SIP,
	},
	{
		.chip = CHELSIO_T5,
		.flags = CXGB4_ACTION_NATMODE_DIP | CXGB4_ACTION_NATMODE_DPORT |
			 CXGB4_ACTION_NATMODE_SPORT,
		.natmode = NAT_MODE_DIP_DP_SP,
	},
	{
		.chip = CHELSIO_T5,
		.flags = CXGB4_ACTION_NATMODE_SIP | CXGB4_ACTION_NATMODE_SPORT,
		.natmode = NAT_MODE_SIP_SP,
	},
	{
		.chip = CHELSIO_T5,
		.flags = CXGB4_ACTION_NATMODE_DIP | CXGB4_ACTION_NATMODE_SIP |
			 CXGB4_ACTION_NATMODE_SPORT,
		.natmode = NAT_MODE_DIP_SIP_SP,
	},
	{
		.chip = CHELSIO_T5,
		.flags = CXGB4_ACTION_NATMODE_DIP | CXGB4_ACTION_NATMODE_SIP |
			 CXGB4_ACTION_NATMODE_DPORT |
			 CXGB4_ACTION_NATMODE_SPORT,
		.natmode = NAT_MODE_ALL,
	},
	/* T6+ can ignore L4 ports when they're disabled. */
	{
		.chip = CHELSIO_T6,
		.flags = CXGB4_ACTION_NATMODE_SIP,
		.natmode = NAT_MODE_SIP_SP,
	},
	{
		.chip = CHELSIO_T6,
		.flags = CXGB4_ACTION_NATMODE_DIP | CXGB4_ACTION_NATMODE_SPORT,
		.natmode = NAT_MODE_DIP_DP_SP,
	},
	{
		.chip = CHELSIO_T6,
		.flags = CXGB4_ACTION_NATMODE_DIP | CXGB4_ACTION_NATMODE_SIP,
		.natmode = NAT_MODE_ALL,
	},
};

static void cxgb4_action_natmode_tweak(struct ch_filter_specification *fs,
				       u8 natmode_flags)
{
	u8 i = 0;

	/* Translate the enabled NAT 4-tuple fields to one of the
	 * hardware supported NAT mode configurations. This ensures
	 * that we pick a valid combination, where the disabled fields
	 * do not get overwritten to 0.
	 */
	for (i = 0; i < ARRAY_SIZE(cxgb4_natmode_config_array); i++) {
		if (cxgb4_natmode_config_array[i].flags == natmode_flags) {
			fs->nat_mode = cxgb4_natmode_config_array[i].natmode;
			return;
		}
	}
}

static struct ch_tc_flower_entry *allocate_flower_entry(void)
{
	struct ch_tc_flower_entry *new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (new)
		spin_lock_init(&new->lock);
	return new;
}

/* Must be called with either RTNL or rcu_read_lock */
static struct ch_tc_flower_entry *ch_flower_lookup(struct adapter *adap,
						   unsigned long flower_cookie)
{
	return rhashtable_lookup_fast(&adap->flower_tbl, &flower_cookie,
				      adap->flower_ht_params);
}

static void cxgb4_process_flow_match(struct net_device *dev,
				     struct flow_rule *rule,
				     struct ch_filter_specification *fs)
{
	u16 addr_type = 0;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		addr_type = match.key->addr_type;
	} else if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
	} else if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;
		u16 ethtype_key, ethtype_mask;

		flow_rule_match_basic(rule, &match);
		ethtype_key = ntohs(match.key->n_proto);
		ethtype_mask = ntohs(match.mask->n_proto);

		if (ethtype_key == ETH_P_ALL) {
			ethtype_key = 0;
			ethtype_mask = 0;
		}

		if (ethtype_key == ETH_P_IPV6)
			fs->type = 1;

		fs->val.ethtype = ethtype_key;
		fs->mask.ethtype = ethtype_mask;
		fs->val.proto = match.key->ip_proto;
		fs->mask.proto = match.mask->ip_proto;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);
		fs->type = 0;
		memcpy(&fs->val.lip[0], &match.key->dst, sizeof(match.key->dst));
		memcpy(&fs->val.fip[0], &match.key->src, sizeof(match.key->src));
		memcpy(&fs->mask.lip[0], &match.mask->dst, sizeof(match.mask->dst));
		memcpy(&fs->mask.fip[0], &match.mask->src, sizeof(match.mask->src));

		/* also initialize nat_lip/fip to same values */
		memcpy(&fs->nat_lip[0], &match.key->dst, sizeof(match.key->dst));
		memcpy(&fs->nat_fip[0], &match.key->src, sizeof(match.key->src));
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);
		fs->type = 1;
		memcpy(&fs->val.lip[0], match.key->dst.s6_addr,
		       sizeof(match.key->dst));
		memcpy(&fs->val.fip[0], match.key->src.s6_addr,
		       sizeof(match.key->src));
		memcpy(&fs->mask.lip[0], match.mask->dst.s6_addr,
		       sizeof(match.mask->dst));
		memcpy(&fs->mask.fip[0], match.mask->src.s6_addr,
		       sizeof(match.mask->src));

		/* also initialize nat_lip/fip to same values */
		memcpy(&fs->nat_lip[0], match.key->dst.s6_addr,
		       sizeof(match.key->dst));
		memcpy(&fs->nat_fip[0], match.key->src.s6_addr,
		       sizeof(match.key->src));
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		fs->val.lport = be16_to_cpu(match.key->dst);
		fs->mask.lport = be16_to_cpu(match.mask->dst);
		fs->val.fport = be16_to_cpu(match.key->src);
		fs->mask.fport = be16_to_cpu(match.mask->src);

		/* also initialize nat_lport/fport to same values */
		fs->nat_lport = fs->val.lport;
		fs->nat_fport = fs->val.fport;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;

		flow_rule_match_ip(rule, &match);
		fs->val.tos = match.key->tos;
		fs->mask.tos = match.mask->tos;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid match;

		flow_rule_match_enc_keyid(rule, &match);
		fs->val.vni = be32_to_cpu(match.key->keyid);
		fs->mask.vni = be32_to_cpu(match.mask->keyid);
		if (fs->mask.vni) {
			fs->val.encap_vld = 1;
			fs->mask.encap_vld = 1;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;
		u16 vlan_tci, vlan_tci_mask;

		flow_rule_match_vlan(rule, &match);
		vlan_tci = match.key->vlan_id | (match.key->vlan_priority <<
					       VLAN_PRIO_SHIFT);
		vlan_tci_mask = match.mask->vlan_id | (match.mask->vlan_priority <<
						     VLAN_PRIO_SHIFT);
		fs->val.ivlan = vlan_tci;
		fs->mask.ivlan = vlan_tci_mask;

		fs->val.ivlan_vld = 1;
		fs->mask.ivlan_vld = 1;

		/* Chelsio adapters use ivlan_vld bit to match vlan packets
		 * as 802.1Q. Also, when vlan tag is present in packets,
		 * ethtype match is used then to match on ethtype of inner
		 * header ie. the header following the vlan header.
		 * So, set the ivlan_vld based on ethtype info supplied by
		 * TC for vlan packets if its 802.1Q. And then reset the
		 * ethtype value else, hw will try to match the supplied
		 * ethtype value with ethtype of inner header.
		 */
		if (fs->val.ethtype == ETH_P_8021Q) {
			fs->val.ethtype = 0;
			fs->mask.ethtype = 0;
		}
	}

	/* Match only packets coming from the ingress port where this
	 * filter will be created.
	 */
	fs->val.iport = netdev2pinfo(dev)->port_id;
	fs->mask.iport = ~0;
}

static int cxgb4_validate_flow_match(struct net_device *dev,
				     struct flow_rule *rule)
{
	struct flow_dissector *dissector = rule->match.dissector;
	u16 ethtype_mask = 0;
	u16 ethtype_key = 0;

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_KEYID) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_IP))) {
		netdev_warn(dev, "Unsupported key used: 0x%x\n",
			    dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		ethtype_key = ntohs(match.key->n_proto);
		ethtype_mask = ntohs(match.mask->n_proto);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		u16 eth_ip_type = ethtype_key & ethtype_mask;
		struct flow_match_ip match;

		if (eth_ip_type != ETH_P_IP && eth_ip_type != ETH_P_IPV6) {
			netdev_err(dev, "IP Key supported only with IPv4/v6");
			return -EINVAL;
		}

		flow_rule_match_ip(rule, &match);
		if (match.mask->ttl) {
			netdev_warn(dev, "ttl match unsupported for offload");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static void offload_pedit(struct ch_filter_specification *fs, u32 val, u32 mask,
			  u8 field)
{
	u32 set_val = val & ~mask;
	u32 offset = 0;
	u8 size = 1;
	int i;

	for (i = 0; i < ARRAY_SIZE(pedits); i++) {
		if (pedits[i].field == field) {
			offset = pedits[i].offset;
			size = pedits[i].size;
			break;
		}
	}
	memcpy((u8 *)fs + offset, &set_val, size);
}

static void process_pedit_field(struct ch_filter_specification *fs, u32 val,
				u32 mask, u32 offset, u8 htype,
				u8 *natmode_flags)
{
	switch (htype) {
	case FLOW_ACT_MANGLE_HDR_TYPE_ETH:
		switch (offset) {
		case PEDIT_ETH_DMAC_31_0:
			fs->newdmac = 1;
			offload_pedit(fs, val, mask, ETH_DMAC_31_0);
			break;
		case PEDIT_ETH_DMAC_47_32_SMAC_15_0:
			if (~mask & PEDIT_ETH_DMAC_MASK)
				offload_pedit(fs, val, mask, ETH_DMAC_47_32);
			else
				offload_pedit(fs, val >> 16, mask >> 16,
					      ETH_SMAC_15_0);
			break;
		case PEDIT_ETH_SMAC_47_16:
			fs->newsmac = 1;
			offload_pedit(fs, val, mask, ETH_SMAC_47_16);
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_IP4:
		switch (offset) {
		case PEDIT_IP4_SRC:
			offload_pedit(fs, val, mask, IP4_SRC);
			*natmode_flags |= CXGB4_ACTION_NATMODE_SIP;
			break;
		case PEDIT_IP4_DST:
			offload_pedit(fs, val, mask, IP4_DST);
			*natmode_flags |= CXGB4_ACTION_NATMODE_DIP;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_IP6:
		switch (offset) {
		case PEDIT_IP6_SRC_31_0:
			offload_pedit(fs, val, mask, IP6_SRC_31_0);
			*natmode_flags |= CXGB4_ACTION_NATMODE_SIP;
			break;
		case PEDIT_IP6_SRC_63_32:
			offload_pedit(fs, val, mask, IP6_SRC_63_32);
			*natmode_flags |=  CXGB4_ACTION_NATMODE_SIP;
			break;
		case PEDIT_IP6_SRC_95_64:
			offload_pedit(fs, val, mask, IP6_SRC_95_64);
			*natmode_flags |= CXGB4_ACTION_NATMODE_SIP;
			break;
		case PEDIT_IP6_SRC_127_96:
			offload_pedit(fs, val, mask, IP6_SRC_127_96);
			*natmode_flags |= CXGB4_ACTION_NATMODE_SIP;
			break;
		case PEDIT_IP6_DST_31_0:
			offload_pedit(fs, val, mask, IP6_DST_31_0);
			*natmode_flags |= CXGB4_ACTION_NATMODE_DIP;
			break;
		case PEDIT_IP6_DST_63_32:
			offload_pedit(fs, val, mask, IP6_DST_63_32);
			*natmode_flags |= CXGB4_ACTION_NATMODE_DIP;
			break;
		case PEDIT_IP6_DST_95_64:
			offload_pedit(fs, val, mask, IP6_DST_95_64);
			*natmode_flags |= CXGB4_ACTION_NATMODE_DIP;
			break;
		case PEDIT_IP6_DST_127_96:
			offload_pedit(fs, val, mask, IP6_DST_127_96);
			*natmode_flags |= CXGB4_ACTION_NATMODE_DIP;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_TCP:
		switch (offset) {
		case PEDIT_TCP_SPORT_DPORT:
			if (~mask & PEDIT_TCP_UDP_SPORT_MASK) {
				fs->nat_fport = val;
				*natmode_flags |= CXGB4_ACTION_NATMODE_SPORT;
			} else {
				fs->nat_lport = val >> 16;
				*natmode_flags |= CXGB4_ACTION_NATMODE_DPORT;
			}
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_UDP:
		switch (offset) {
		case PEDIT_UDP_SPORT_DPORT:
			if (~mask & PEDIT_TCP_UDP_SPORT_MASK) {
				fs->nat_fport = val;
				*natmode_flags |= CXGB4_ACTION_NATMODE_SPORT;
			} else {
				fs->nat_lport = val >> 16;
				*natmode_flags |= CXGB4_ACTION_NATMODE_DPORT;
			}
		}
		break;
	}
}

static int cxgb4_action_natmode_validate(struct adapter *adap, u8 natmode_flags,
					 struct netlink_ext_ack *extack)
{
	u8 i = 0;

	/* Extract the NAT mode to enable based on what 4-tuple fields
	 * are enabled to be overwritten. This ensures that the
	 * disabled fields don't get overwritten to 0.
	 */
	for (i = 0; i < ARRAY_SIZE(cxgb4_natmode_config_array); i++) {
		const struct cxgb4_natmode_config *c;

		c = &cxgb4_natmode_config_array[i];
		if (CHELSIO_CHIP_VERSION(adap->params.chip) >= c->chip &&
		    natmode_flags == c->flags)
			return 0;
	}
	NL_SET_ERR_MSG_MOD(extack, "Unsupported NAT mode 4-tuple combination");
	return -EOPNOTSUPP;
}

void cxgb4_process_flow_actions(struct net_device *in,
				struct flow_action *actions,
				struct ch_filter_specification *fs)
{
	struct flow_action_entry *act;
	u8 natmode_flags = 0;
	int i;

	flow_action_for_each(i, act, actions) {
		switch (act->id) {
		case FLOW_ACTION_ACCEPT:
			fs->action = FILTER_PASS;
			break;
		case FLOW_ACTION_DROP:
			fs->action = FILTER_DROP;
			break;
		case FLOW_ACTION_MIRRED:
		case FLOW_ACTION_REDIRECT: {
			struct net_device *out = act->dev;
			struct port_info *pi = netdev_priv(out);

			fs->action = FILTER_SWITCH;
			fs->eport = pi->port_id;
			}
			break;
		case FLOW_ACTION_VLAN_POP:
		case FLOW_ACTION_VLAN_PUSH:
		case FLOW_ACTION_VLAN_MANGLE: {
			u8 prio = act->vlan.prio;
			u16 vid = act->vlan.vid;
			u16 vlan_tci = (prio << VLAN_PRIO_SHIFT) | vid;
			switch (act->id) {
			case FLOW_ACTION_VLAN_POP:
				fs->newvlan |= VLAN_REMOVE;
				break;
			case FLOW_ACTION_VLAN_PUSH:
				fs->newvlan |= VLAN_INSERT;
				fs->vlan = vlan_tci;
				break;
			case FLOW_ACTION_VLAN_MANGLE:
				fs->newvlan |= VLAN_REWRITE;
				fs->vlan = vlan_tci;
				break;
			default:
				break;
			}
			}
			break;
		case FLOW_ACTION_MANGLE: {
			u32 mask, val, offset;
			u8 htype;

			htype = act->mangle.htype;
			mask = act->mangle.mask;
			val = act->mangle.val;
			offset = act->mangle.offset;

			process_pedit_field(fs, val, mask, offset, htype,
					    &natmode_flags);
			}
			break;
		case FLOW_ACTION_QUEUE:
			fs->action = FILTER_PASS;
			fs->dirsteer = 1;
			fs->iq = act->queue.index;
			break;
		default:
			break;
		}
	}
	if (natmode_flags)
		cxgb4_action_natmode_tweak(fs, natmode_flags);

}

static bool valid_l4_mask(u32 mask)
{
	u16 hi, lo;

	/* Either the upper 16-bits (SPORT) OR the lower
	 * 16-bits (DPORT) can be set, but NOT BOTH.
	 */
	hi = (mask >> 16) & 0xFFFF;
	lo = mask & 0xFFFF;

	return hi && lo ? false : true;
}

static bool valid_pedit_action(struct net_device *dev,
			       const struct flow_action_entry *act,
			       u8 *natmode_flags)
{
	u32 mask, offset;
	u8 htype;

	htype = act->mangle.htype;
	mask = act->mangle.mask;
	offset = act->mangle.offset;

	switch (htype) {
	case FLOW_ACT_MANGLE_HDR_TYPE_ETH:
		switch (offset) {
		case PEDIT_ETH_DMAC_31_0:
		case PEDIT_ETH_DMAC_47_32_SMAC_15_0:
		case PEDIT_ETH_SMAC_47_16:
			break;
		default:
			netdev_err(dev, "%s: Unsupported pedit field\n",
				   __func__);
			return false;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_IP4:
		switch (offset) {
		case PEDIT_IP4_SRC:
			*natmode_flags |= CXGB4_ACTION_NATMODE_SIP;
			break;
		case PEDIT_IP4_DST:
			*natmode_flags |= CXGB4_ACTION_NATMODE_DIP;
			break;
		default:
			netdev_err(dev, "%s: Unsupported pedit field\n",
				   __func__);
			return false;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_IP6:
		switch (offset) {
		case PEDIT_IP6_SRC_31_0:
		case PEDIT_IP6_SRC_63_32:
		case PEDIT_IP6_SRC_95_64:
		case PEDIT_IP6_SRC_127_96:
			*natmode_flags |= CXGB4_ACTION_NATMODE_SIP;
			break;
		case PEDIT_IP6_DST_31_0:
		case PEDIT_IP6_DST_63_32:
		case PEDIT_IP6_DST_95_64:
		case PEDIT_IP6_DST_127_96:
			*natmode_flags |= CXGB4_ACTION_NATMODE_DIP;
			break;
		default:
			netdev_err(dev, "%s: Unsupported pedit field\n",
				   __func__);
			return false;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_TCP:
		switch (offset) {
		case PEDIT_TCP_SPORT_DPORT:
			if (!valid_l4_mask(~mask)) {
				netdev_err(dev, "%s: Unsupported mask for TCP L4 ports\n",
					   __func__);
				return false;
			}
			if (~mask & PEDIT_TCP_UDP_SPORT_MASK)
				*natmode_flags |= CXGB4_ACTION_NATMODE_SPORT;
			else
				*natmode_flags |= CXGB4_ACTION_NATMODE_DPORT;
			break;
		default:
			netdev_err(dev, "%s: Unsupported pedit field\n",
				   __func__);
			return false;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_UDP:
		switch (offset) {
		case PEDIT_UDP_SPORT_DPORT:
			if (!valid_l4_mask(~mask)) {
				netdev_err(dev, "%s: Unsupported mask for UDP L4 ports\n",
					   __func__);
				return false;
			}
			if (~mask & PEDIT_TCP_UDP_SPORT_MASK)
				*natmode_flags |= CXGB4_ACTION_NATMODE_SPORT;
			else
				*natmode_flags |= CXGB4_ACTION_NATMODE_DPORT;
			break;
		default:
			netdev_err(dev, "%s: Unsupported pedit field\n",
				   __func__);
			return false;
		}
		break;
	default:
		netdev_err(dev, "%s: Unsupported pedit type\n", __func__);
		return false;
	}
	return true;
}

int cxgb4_validate_flow_actions(struct net_device *dev,
				struct flow_action *actions,
				struct netlink_ext_ack *extack,
				u8 matchall_filter)
{
	struct adapter *adap = netdev2adap(dev);
	struct flow_action_entry *act;
	bool act_redir = false;
	bool act_pedit = false;
	bool act_vlan = false;
	u8 natmode_flags = 0;
	int i;

	if (!flow_action_basic_hw_stats_check(actions, extack))
		return -EOPNOTSUPP;

	flow_action_for_each(i, act, actions) {
		switch (act->id) {
		case FLOW_ACTION_ACCEPT:
		case FLOW_ACTION_DROP:
			/* Do nothing */
			break;
		case FLOW_ACTION_MIRRED:
		case FLOW_ACTION_REDIRECT: {
			struct net_device *n_dev, *target_dev;
			bool found = false;
			unsigned int i;

			if (act->id == FLOW_ACTION_MIRRED &&
			    !matchall_filter) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Egress mirror action is only supported for tc-matchall");
				return -EOPNOTSUPP;
			}

			target_dev = act->dev;
			for_each_port(adap, i) {
				n_dev = adap->port[i];
				if (target_dev == n_dev) {
					found = true;
					break;
				}
			}

			/* If interface doesn't belong to our hw, then
			 * the provided output port is not valid
			 */
			if (!found) {
				netdev_err(dev, "%s: Out port invalid\n",
					   __func__);
				return -EINVAL;
			}
			act_redir = true;
			}
			break;
		case FLOW_ACTION_VLAN_POP:
		case FLOW_ACTION_VLAN_PUSH:
		case FLOW_ACTION_VLAN_MANGLE: {
			u16 proto = be16_to_cpu(act->vlan.proto);

			switch (act->id) {
			case FLOW_ACTION_VLAN_POP:
				break;
			case FLOW_ACTION_VLAN_PUSH:
			case FLOW_ACTION_VLAN_MANGLE:
				if (proto != ETH_P_8021Q) {
					netdev_err(dev, "%s: Unsupported vlan proto\n",
						   __func__);
					return -EOPNOTSUPP;
				}
				break;
			default:
				netdev_err(dev, "%s: Unsupported vlan action\n",
					   __func__);
				return -EOPNOTSUPP;
			}
			act_vlan = true;
			}
			break;
		case FLOW_ACTION_MANGLE: {
			bool pedit_valid = valid_pedit_action(dev, act,
							      &natmode_flags);

			if (!pedit_valid)
				return -EOPNOTSUPP;
			act_pedit = true;
			}
			break;
		case FLOW_ACTION_QUEUE:
			/* Do nothing. cxgb4_set_filter will validate */
			break;
		default:
			netdev_err(dev, "%s: Unsupported action\n", __func__);
			return -EOPNOTSUPP;
		}
	}

	if ((act_pedit || act_vlan) && !act_redir) {
		netdev_err(dev, "%s: pedit/vlan rewrite invalid without egress redirect\n",
			   __func__);
		return -EINVAL;
	}

	if (act_pedit) {
		int ret;

		ret = cxgb4_action_natmode_validate(adap, natmode_flags,
						    extack);
		if (ret)
			return ret;
	}

	return 0;
}

static void cxgb4_tc_flower_hash_prio_add(struct adapter *adap, u32 tc_prio)
{
	spin_lock_bh(&adap->tids.ftid_lock);
	if (adap->tids.tc_hash_tids_max_prio < tc_prio)
		adap->tids.tc_hash_tids_max_prio = tc_prio;
	spin_unlock_bh(&adap->tids.ftid_lock);
}

static void cxgb4_tc_flower_hash_prio_del(struct adapter *adap, u32 tc_prio)
{
	struct tid_info *t = &adap->tids;
	struct ch_tc_flower_entry *fe;
	struct rhashtable_iter iter;
	u32 found = 0;

	spin_lock_bh(&t->ftid_lock);
	/* Bail if the current rule is not the one with the max
	 * prio.
	 */
	if (t->tc_hash_tids_max_prio != tc_prio)
		goto out_unlock;

	/* Search for the next rule having the same or next lower
	 * max prio.
	 */
	rhashtable_walk_enter(&adap->flower_tbl, &iter);
	do {
		rhashtable_walk_start(&iter);

		fe = rhashtable_walk_next(&iter);
		while (!IS_ERR_OR_NULL(fe)) {
			if (fe->fs.hash &&
			    fe->fs.tc_prio <= t->tc_hash_tids_max_prio) {
				t->tc_hash_tids_max_prio = fe->fs.tc_prio;
				found++;

				/* Bail if we found another rule
				 * having the same prio as the
				 * current max one.
				 */
				if (fe->fs.tc_prio == tc_prio)
					break;
			}

			fe = rhashtable_walk_next(&iter);
		}

		rhashtable_walk_stop(&iter);
	} while (fe == ERR_PTR(-EAGAIN));
	rhashtable_walk_exit(&iter);

	if (!found)
		t->tc_hash_tids_max_prio = 0;

out_unlock:
	spin_unlock_bh(&t->ftid_lock);
}

int cxgb4_flow_rule_replace(struct net_device *dev, struct flow_rule *rule,
			    u32 tc_prio, struct netlink_ext_ack *extack,
			    struct ch_filter_specification *fs, u32 *tid)
{
	struct adapter *adap = netdev2adap(dev);
	struct filter_ctx ctx;
	u8 inet_family;
	int fidx, ret;

	if (cxgb4_validate_flow_actions(dev, &rule->action, extack, 0))
		return -EOPNOTSUPP;

	if (cxgb4_validate_flow_match(dev, rule))
		return -EOPNOTSUPP;

	cxgb4_process_flow_match(dev, rule, fs);
	cxgb4_process_flow_actions(dev, &rule->action, fs);

	fs->hash = is_filter_exact_match(adap, fs);
	inet_family = fs->type ? PF_INET6 : PF_INET;

	/* Get a free filter entry TID, where we can insert this new
	 * rule. Only insert rule if its prio doesn't conflict with
	 * existing rules.
	 */
	fidx = cxgb4_get_free_ftid(dev, inet_family, fs->hash,
				   tc_prio);
	if (fidx < 0) {
		NL_SET_ERR_MSG_MOD(extack,
				   "No free LETCAM index available");
		return -ENOMEM;
	}

	if (fidx < adap->tids.nhpftids) {
		fs->prio = 1;
		fs->hash = 0;
	}

	/* If the rule can be inserted into HASH region, then ignore
	 * the index to normal FILTER region.
	 */
	if (fs->hash)
		fidx = 0;

	fs->tc_prio = tc_prio;

	init_completion(&ctx.completion);
	ret = __cxgb4_set_filter(dev, fidx, fs, &ctx);
	if (ret) {
		netdev_err(dev, "%s: filter creation err %d\n",
			   __func__, ret);
		return ret;
	}

	/* Wait for reply */
	ret = wait_for_completion_timeout(&ctx.completion, 10 * HZ);
	if (!ret)
		return -ETIMEDOUT;

	/* Check if hw returned error for filter creation */
	if (ctx.result)
		return ctx.result;

	*tid = ctx.tid;

	if (fs->hash)
		cxgb4_tc_flower_hash_prio_add(adap, tc_prio);

	return 0;
}

int cxgb4_tc_flower_replace(struct net_device *dev,
			    struct flow_cls_offload *cls)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	struct adapter *adap = netdev2adap(dev);
	struct ch_tc_flower_entry *ch_flower;
	struct ch_filter_specification *fs;
	int ret;

	ch_flower = allocate_flower_entry();
	if (!ch_flower) {
		netdev_err(dev, "%s: ch_flower alloc failed.\n", __func__);
		return -ENOMEM;
	}

	fs = &ch_flower->fs;
	fs->hitcnts = 1;
	fs->tc_cookie = cls->cookie;

	ret = cxgb4_flow_rule_replace(dev, rule, cls->common.prio, extack, fs,
				      &ch_flower->filter_id);
	if (ret)
		goto free_entry;

	ch_flower->tc_flower_cookie = cls->cookie;
	ret = rhashtable_insert_fast(&adap->flower_tbl, &ch_flower->node,
				     adap->flower_ht_params);
	if (ret)
		goto del_filter;

	return 0;

del_filter:
	if (fs->hash)
		cxgb4_tc_flower_hash_prio_del(adap, cls->common.prio);

	cxgb4_del_filter(dev, ch_flower->filter_id, &ch_flower->fs);

free_entry:
	kfree(ch_flower);
	return ret;
}

int cxgb4_flow_rule_destroy(struct net_device *dev, u32 tc_prio,
			    struct ch_filter_specification *fs, int tid)
{
	struct adapter *adap = netdev2adap(dev);
	u8 hash;
	int ret;

	hash = fs->hash;

	ret = cxgb4_del_filter(dev, tid, fs);
	if (ret)
		return ret;

	if (hash)
		cxgb4_tc_flower_hash_prio_del(adap, tc_prio);

	return ret;
}

int cxgb4_tc_flower_destroy(struct net_device *dev,
			    struct flow_cls_offload *cls)
{
	struct adapter *adap = netdev2adap(dev);
	struct ch_tc_flower_entry *ch_flower;
	int ret;

	ch_flower = ch_flower_lookup(adap, cls->cookie);
	if (!ch_flower)
		return -ENOENT;

	ret = cxgb4_flow_rule_destroy(dev, ch_flower->fs.tc_prio,
				      &ch_flower->fs, ch_flower->filter_id);
	if (ret)
		goto err;

	ret = rhashtable_remove_fast(&adap->flower_tbl, &ch_flower->node,
				     adap->flower_ht_params);
	if (ret) {
		netdev_err(dev, "Flow remove from rhashtable failed");
		goto err;
	}
	kfree_rcu(ch_flower, rcu);

err:
	return ret;
}

static void ch_flower_stats_handler(struct work_struct *work)
{
	struct adapter *adap = container_of(work, struct adapter,
					    flower_stats_work);
	struct ch_tc_flower_entry *flower_entry;
	struct ch_tc_flower_stats *ofld_stats;
	struct rhashtable_iter iter;
	u64 packets;
	u64 bytes;
	int ret;

	rhashtable_walk_enter(&adap->flower_tbl, &iter);
	do {
		rhashtable_walk_start(&iter);

		while ((flower_entry = rhashtable_walk_next(&iter)) &&
		       !IS_ERR(flower_entry)) {
			ret = cxgb4_get_filter_counters(adap->port[0],
							flower_entry->filter_id,
							&packets, &bytes,
							flower_entry->fs.hash);
			if (!ret) {
				spin_lock(&flower_entry->lock);
				ofld_stats = &flower_entry->stats;

				if (ofld_stats->prev_packet_count != packets) {
					ofld_stats->prev_packet_count = packets;
					ofld_stats->last_used = jiffies;
				}
				spin_unlock(&flower_entry->lock);
			}
		}

		rhashtable_walk_stop(&iter);

	} while (flower_entry == ERR_PTR(-EAGAIN));
	rhashtable_walk_exit(&iter);
	mod_timer(&adap->flower_stats_timer, jiffies + STATS_CHECK_PERIOD);
}

static void ch_flower_stats_cb(struct timer_list *t)
{
	struct adapter *adap = from_timer(adap, t, flower_stats_timer);

	schedule_work(&adap->flower_stats_work);
}

int cxgb4_tc_flower_stats(struct net_device *dev,
			  struct flow_cls_offload *cls)
{
	struct adapter *adap = netdev2adap(dev);
	struct ch_tc_flower_stats *ofld_stats;
	struct ch_tc_flower_entry *ch_flower;
	u64 packets;
	u64 bytes;
	int ret;

	ch_flower = ch_flower_lookup(adap, cls->cookie);
	if (!ch_flower) {
		ret = -ENOENT;
		goto err;
	}

	ret = cxgb4_get_filter_counters(dev, ch_flower->filter_id,
					&packets, &bytes,
					ch_flower->fs.hash);
	if (ret < 0)
		goto err;

	spin_lock_bh(&ch_flower->lock);
	ofld_stats = &ch_flower->stats;
	if (ofld_stats->packet_count != packets) {
		if (ofld_stats->prev_packet_count != packets)
			ofld_stats->last_used = jiffies;
		flow_stats_update(&cls->stats, bytes - ofld_stats->byte_count,
				  packets - ofld_stats->packet_count, 0,
				  ofld_stats->last_used,
				  FLOW_ACTION_HW_STATS_IMMEDIATE);

		ofld_stats->packet_count = packets;
		ofld_stats->byte_count = bytes;
		ofld_stats->prev_packet_count = packets;
	}
	spin_unlock_bh(&ch_flower->lock);
	return 0;

err:
	return ret;
}

static const struct rhashtable_params cxgb4_tc_flower_ht_params = {
	.nelem_hint = 384,
	.head_offset = offsetof(struct ch_tc_flower_entry, node),
	.key_offset = offsetof(struct ch_tc_flower_entry, tc_flower_cookie),
	.key_len = sizeof(((struct ch_tc_flower_entry *)0)->tc_flower_cookie),
	.max_size = 524288,
	.min_size = 512,
	.automatic_shrinking = true
};

int cxgb4_init_tc_flower(struct adapter *adap)
{
	int ret;

	if (adap->tc_flower_initialized)
		return -EEXIST;

	adap->flower_ht_params = cxgb4_tc_flower_ht_params;
	ret = rhashtable_init(&adap->flower_tbl, &adap->flower_ht_params);
	if (ret)
		return ret;

	INIT_WORK(&adap->flower_stats_work, ch_flower_stats_handler);
	timer_setup(&adap->flower_stats_timer, ch_flower_stats_cb, 0);
	mod_timer(&adap->flower_stats_timer, jiffies + STATS_CHECK_PERIOD);
	adap->tc_flower_initialized = true;
	return 0;
}

void cxgb4_cleanup_tc_flower(struct adapter *adap)
{
	if (!adap->tc_flower_initialized)
		return;

	if (adap->flower_stats_timer.function)
		del_timer_sync(&adap->flower_stats_timer);
	cancel_work_sync(&adap->flower_stats_work);
	rhashtable_destroy(&adap->flower_tbl);
	adap->tc_flower_initialized = false;
}
