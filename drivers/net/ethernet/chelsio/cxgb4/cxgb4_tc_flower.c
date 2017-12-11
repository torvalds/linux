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

struct ch_tc_pedit_fields pedits[] = {
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
	PEDIT_FIELDS(TCP_, SPORT, 2, nat_fport, 0),
	PEDIT_FIELDS(TCP_, DPORT, 2, nat_lport, 0),
	PEDIT_FIELDS(UDP_, SPORT, 2, nat_fport, 0),
	PEDIT_FIELDS(UDP_, DPORT, 2, nat_lport, 0),
};

static struct ch_tc_flower_entry *allocate_flower_entry(void)
{
	struct ch_tc_flower_entry *new = kzalloc(sizeof(*new), GFP_KERNEL);
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
				     struct tc_cls_flower_offload *cls,
				     struct ch_filter_specification *fs)
{
	u16 addr_type = 0;

	if (dissector_uses_key(cls->dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_CONTROL,
						  cls->key);

		addr_type = key->addr_type;
	}

	if (dissector_uses_key(cls->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *key =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  cls->key);
		struct flow_dissector_key_basic *mask =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  cls->mask);
		u16 ethtype_key = ntohs(key->n_proto);
		u16 ethtype_mask = ntohs(mask->n_proto);

		if (ethtype_key == ETH_P_ALL) {
			ethtype_key = 0;
			ethtype_mask = 0;
		}

		fs->val.ethtype = ethtype_key;
		fs->mask.ethtype = ethtype_mask;
		fs->val.proto = key->ip_proto;
		fs->mask.proto = mask->ip_proto;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_dissector_key_ipv4_addrs *key =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						  cls->key);
		struct flow_dissector_key_ipv4_addrs *mask =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						  cls->mask);
		fs->type = 0;
		memcpy(&fs->val.lip[0], &key->dst, sizeof(key->dst));
		memcpy(&fs->val.fip[0], &key->src, sizeof(key->src));
		memcpy(&fs->mask.lip[0], &mask->dst, sizeof(mask->dst));
		memcpy(&fs->mask.fip[0], &mask->src, sizeof(mask->src));

		/* also initialize nat_lip/fip to same values */
		memcpy(&fs->nat_lip[0], &key->dst, sizeof(key->dst));
		memcpy(&fs->nat_fip[0], &key->src, sizeof(key->src));

	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_dissector_key_ipv6_addrs *key =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						  cls->key);
		struct flow_dissector_key_ipv6_addrs *mask =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						  cls->mask);

		fs->type = 1;
		memcpy(&fs->val.lip[0], key->dst.s6_addr, sizeof(key->dst));
		memcpy(&fs->val.fip[0], key->src.s6_addr, sizeof(key->src));
		memcpy(&fs->mask.lip[0], mask->dst.s6_addr, sizeof(mask->dst));
		memcpy(&fs->mask.fip[0], mask->src.s6_addr, sizeof(mask->src));

		/* also initialize nat_lip/fip to same values */
		memcpy(&fs->nat_lip[0], key->dst.s6_addr, sizeof(key->dst));
		memcpy(&fs->nat_fip[0], key->src.s6_addr, sizeof(key->src));
	}

	if (dissector_uses_key(cls->dissector, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_dissector_key_ports *key, *mask;

		key = skb_flow_dissector_target(cls->dissector,
						FLOW_DISSECTOR_KEY_PORTS,
						cls->key);
		mask = skb_flow_dissector_target(cls->dissector,
						 FLOW_DISSECTOR_KEY_PORTS,
						 cls->mask);
		fs->val.lport = cpu_to_be16(key->dst);
		fs->mask.lport = cpu_to_be16(mask->dst);
		fs->val.fport = cpu_to_be16(key->src);
		fs->mask.fport = cpu_to_be16(mask->src);

		/* also initialize nat_lport/fport to same values */
		fs->nat_lport = cpu_to_be16(key->dst);
		fs->nat_fport = cpu_to_be16(key->src);
	}

	if (dissector_uses_key(cls->dissector, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_dissector_key_ip *key, *mask;

		key = skb_flow_dissector_target(cls->dissector,
						FLOW_DISSECTOR_KEY_IP,
						cls->key);
		mask = skb_flow_dissector_target(cls->dissector,
						 FLOW_DISSECTOR_KEY_IP,
						 cls->mask);
		fs->val.tos = key->tos;
		fs->mask.tos = mask->tos;
	}

	if (dissector_uses_key(cls->dissector, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_dissector_key_vlan *key, *mask;
		u16 vlan_tci, vlan_tci_mask;

		key = skb_flow_dissector_target(cls->dissector,
						FLOW_DISSECTOR_KEY_VLAN,
						cls->key);
		mask = skb_flow_dissector_target(cls->dissector,
						 FLOW_DISSECTOR_KEY_VLAN,
						 cls->mask);
		vlan_tci = key->vlan_id | (key->vlan_priority <<
					   VLAN_PRIO_SHIFT);
		vlan_tci_mask = mask->vlan_id | (mask->vlan_priority <<
						 VLAN_PRIO_SHIFT);
		fs->val.ivlan = cpu_to_be16(vlan_tci);
		fs->mask.ivlan = cpu_to_be16(vlan_tci_mask);

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
			fs->val.ivlan_vld = 1;
			fs->mask.ivlan_vld = 1;
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
				     struct tc_cls_flower_offload *cls)
{
	u16 ethtype_mask = 0;
	u16 ethtype_key = 0;

	if (cls->dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_IP))) {
		netdev_warn(dev, "Unsupported key used: 0x%x\n",
			    cls->dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if (dissector_uses_key(cls->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *key =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  cls->key);
		struct flow_dissector_key_basic *mask =
			skb_flow_dissector_target(cls->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  cls->mask);
		ethtype_key = ntohs(key->n_proto);
		ethtype_mask = ntohs(mask->n_proto);
	}

	if (dissector_uses_key(cls->dissector, FLOW_DISSECTOR_KEY_IP)) {
		u16 eth_ip_type = ethtype_key & ethtype_mask;
		struct flow_dissector_key_ip *mask;

		if (eth_ip_type != ETH_P_IP && eth_ip_type != ETH_P_IPV6) {
			netdev_err(dev, "IP Key supported only with IPv4/v6");
			return -EINVAL;
		}

		mask = skb_flow_dissector_target(cls->dissector,
						 FLOW_DISSECTOR_KEY_IP,
						 cls->mask);
		if (mask->ttl) {
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
				u32 mask, u32 offset, u8 htype)
{
	switch (htype) {
	case TCA_PEDIT_KEY_EX_HDR_TYPE_ETH:
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
	case TCA_PEDIT_KEY_EX_HDR_TYPE_IP4:
		switch (offset) {
		case PEDIT_IP4_SRC:
			offload_pedit(fs, val, mask, IP4_SRC);
			break;
		case PEDIT_IP4_DST:
			offload_pedit(fs, val, mask, IP4_DST);
		}
		fs->nat_mode = NAT_MODE_ALL;
		break;
	case TCA_PEDIT_KEY_EX_HDR_TYPE_IP6:
		switch (offset) {
		case PEDIT_IP6_SRC_31_0:
			offload_pedit(fs, val, mask, IP6_SRC_31_0);
			break;
		case PEDIT_IP6_SRC_63_32:
			offload_pedit(fs, val, mask, IP6_SRC_63_32);
			break;
		case PEDIT_IP6_SRC_95_64:
			offload_pedit(fs, val, mask, IP6_SRC_95_64);
			break;
		case PEDIT_IP6_SRC_127_96:
			offload_pedit(fs, val, mask, IP6_SRC_127_96);
			break;
		case PEDIT_IP6_DST_31_0:
			offload_pedit(fs, val, mask, IP6_DST_31_0);
			break;
		case PEDIT_IP6_DST_63_32:
			offload_pedit(fs, val, mask, IP6_DST_63_32);
			break;
		case PEDIT_IP6_DST_95_64:
			offload_pedit(fs, val, mask, IP6_DST_95_64);
			break;
		case PEDIT_IP6_DST_127_96:
			offload_pedit(fs, val, mask, IP6_DST_127_96);
		}
		fs->nat_mode = NAT_MODE_ALL;
		break;
	case TCA_PEDIT_KEY_EX_HDR_TYPE_TCP:
		switch (offset) {
		case PEDIT_TCP_SPORT_DPORT:
			if (~mask & PEDIT_TCP_UDP_SPORT_MASK)
				offload_pedit(fs, cpu_to_be32(val) >> 16,
					      cpu_to_be32(mask) >> 16,
					      TCP_SPORT);
			else
				offload_pedit(fs, cpu_to_be32(val),
					      cpu_to_be32(mask), TCP_DPORT);
		}
		fs->nat_mode = NAT_MODE_ALL;
		break;
	case TCA_PEDIT_KEY_EX_HDR_TYPE_UDP:
		switch (offset) {
		case PEDIT_UDP_SPORT_DPORT:
			if (~mask & PEDIT_TCP_UDP_SPORT_MASK)
				offload_pedit(fs, cpu_to_be32(val) >> 16,
					      cpu_to_be32(mask) >> 16,
					      UDP_SPORT);
			else
				offload_pedit(fs, cpu_to_be32(val),
					      cpu_to_be32(mask), UDP_DPORT);
		}
		fs->nat_mode = NAT_MODE_ALL;
	}
}

static void cxgb4_process_flow_actions(struct net_device *in,
				       struct tc_cls_flower_offload *cls,
				       struct ch_filter_specification *fs)
{
	const struct tc_action *a;
	LIST_HEAD(actions);

	tcf_exts_to_list(cls->exts, &actions);
	list_for_each_entry(a, &actions, list) {
		if (is_tcf_gact_ok(a)) {
			fs->action = FILTER_PASS;
		} else if (is_tcf_gact_shot(a)) {
			fs->action = FILTER_DROP;
		} else if (is_tcf_mirred_egress_redirect(a)) {
			struct net_device *out = tcf_mirred_dev(a);
			struct port_info *pi = netdev_priv(out);

			fs->action = FILTER_SWITCH;
			fs->eport = pi->port_id;
		} else if (is_tcf_vlan(a)) {
			u32 vlan_action = tcf_vlan_action(a);
			u8 prio = tcf_vlan_push_prio(a);
			u16 vid = tcf_vlan_push_vid(a);
			u16 vlan_tci = (prio << VLAN_PRIO_SHIFT) | vid;

			switch (vlan_action) {
			case TCA_VLAN_ACT_POP:
				fs->newvlan |= VLAN_REMOVE;
				break;
			case TCA_VLAN_ACT_PUSH:
				fs->newvlan |= VLAN_INSERT;
				fs->vlan = vlan_tci;
				break;
			case TCA_VLAN_ACT_MODIFY:
				fs->newvlan |= VLAN_REWRITE;
				fs->vlan = vlan_tci;
				break;
			default:
				break;
			}
		} else if (is_tcf_pedit(a)) {
			u32 mask, val, offset;
			int nkeys, i;
			u8 htype;

			nkeys = tcf_pedit_nkeys(a);
			for (i = 0; i < nkeys; i++) {
				htype = tcf_pedit_htype(a, i);
				mask = tcf_pedit_mask(a, i);
				val = tcf_pedit_val(a, i);
				offset = tcf_pedit_offset(a, i);

				process_pedit_field(fs, val, mask, offset,
						    htype);
			}
		}
	}
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
			       const struct tc_action *a)
{
	u32 mask, offset;
	u8 cmd, htype;
	int nkeys, i;

	nkeys = tcf_pedit_nkeys(a);
	for (i = 0; i < nkeys; i++) {
		htype = tcf_pedit_htype(a, i);
		cmd = tcf_pedit_cmd(a, i);
		mask = tcf_pedit_mask(a, i);
		offset = tcf_pedit_offset(a, i);

		if (cmd != TCA_PEDIT_KEY_EX_CMD_SET) {
			netdev_err(dev, "%s: Unsupported pedit cmd\n",
				   __func__);
			return false;
		}

		switch (htype) {
		case TCA_PEDIT_KEY_EX_HDR_TYPE_ETH:
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
		case TCA_PEDIT_KEY_EX_HDR_TYPE_IP4:
			switch (offset) {
			case PEDIT_IP4_SRC:
			case PEDIT_IP4_DST:
				break;
			default:
				netdev_err(dev, "%s: Unsupported pedit field\n",
					   __func__);
				return false;
			}
			break;
		case TCA_PEDIT_KEY_EX_HDR_TYPE_IP6:
			switch (offset) {
			case PEDIT_IP6_SRC_31_0:
			case PEDIT_IP6_SRC_63_32:
			case PEDIT_IP6_SRC_95_64:
			case PEDIT_IP6_SRC_127_96:
			case PEDIT_IP6_DST_31_0:
			case PEDIT_IP6_DST_63_32:
			case PEDIT_IP6_DST_95_64:
			case PEDIT_IP6_DST_127_96:
				break;
			default:
				netdev_err(dev, "%s: Unsupported pedit field\n",
					   __func__);
				return false;
			}
			break;
		case TCA_PEDIT_KEY_EX_HDR_TYPE_TCP:
			switch (offset) {
			case PEDIT_TCP_SPORT_DPORT:
				if (!valid_l4_mask(~mask)) {
					netdev_err(dev, "%s: Unsupported mask for TCP L4 ports\n",
						   __func__);
					return false;
				}
				break;
			default:
				netdev_err(dev, "%s: Unsupported pedit field\n",
					   __func__);
				return false;
			}
			break;
		case TCA_PEDIT_KEY_EX_HDR_TYPE_UDP:
			switch (offset) {
			case PEDIT_UDP_SPORT_DPORT:
				if (!valid_l4_mask(~mask)) {
					netdev_err(dev, "%s: Unsupported mask for UDP L4 ports\n",
						   __func__);
					return false;
				}
				break;
			default:
				netdev_err(dev, "%s: Unsupported pedit field\n",
					   __func__);
				return false;
			}
			break;
		default:
			netdev_err(dev, "%s: Unsupported pedit type\n",
				   __func__);
			return false;
		}
	}
	return true;
}

static int cxgb4_validate_flow_actions(struct net_device *dev,
				       struct tc_cls_flower_offload *cls)
{
	const struct tc_action *a;
	bool act_redir = false;
	bool act_pedit = false;
	bool act_vlan = false;
	LIST_HEAD(actions);

	tcf_exts_to_list(cls->exts, &actions);
	list_for_each_entry(a, &actions, list) {
		if (is_tcf_gact_ok(a)) {
			/* Do nothing */
		} else if (is_tcf_gact_shot(a)) {
			/* Do nothing */
		} else if (is_tcf_mirred_egress_redirect(a)) {
			struct adapter *adap = netdev2adap(dev);
			struct net_device *n_dev, *target_dev;
			unsigned int i;
			bool found = false;

			target_dev = tcf_mirred_dev(a);
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
		} else if (is_tcf_vlan(a)) {
			u16 proto = be16_to_cpu(tcf_vlan_push_proto(a));
			u32 vlan_action = tcf_vlan_action(a);

			switch (vlan_action) {
			case TCA_VLAN_ACT_POP:
				break;
			case TCA_VLAN_ACT_PUSH:
			case TCA_VLAN_ACT_MODIFY:
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
		} else if (is_tcf_pedit(a)) {
			bool pedit_valid = valid_pedit_action(dev, a);

			if (!pedit_valid)
				return -EOPNOTSUPP;
			act_pedit = true;
		} else {
			netdev_err(dev, "%s: Unsupported action\n", __func__);
			return -EOPNOTSUPP;
		}
	}

	if ((act_pedit || act_vlan) && !act_redir) {
		netdev_err(dev, "%s: pedit/vlan rewrite invalid without egress redirect\n",
			   __func__);
		return -EINVAL;
	}

	return 0;
}

int cxgb4_tc_flower_replace(struct net_device *dev,
			    struct tc_cls_flower_offload *cls)
{
	struct adapter *adap = netdev2adap(dev);
	struct ch_tc_flower_entry *ch_flower;
	struct ch_filter_specification *fs;
	struct filter_ctx ctx;
	int fidx;
	int ret;

	if (cxgb4_validate_flow_actions(dev, cls))
		return -EOPNOTSUPP;

	if (cxgb4_validate_flow_match(dev, cls))
		return -EOPNOTSUPP;

	ch_flower = allocate_flower_entry();
	if (!ch_flower) {
		netdev_err(dev, "%s: ch_flower alloc failed.\n", __func__);
		return -ENOMEM;
	}

	fs = &ch_flower->fs;
	fs->hitcnts = 1;
	cxgb4_process_flow_match(dev, cls, fs);
	cxgb4_process_flow_actions(dev, cls, fs);

	fs->hash = is_filter_exact_match(adap, fs);
	if (fs->hash) {
		fidx = 0;
	} else {
		fidx = cxgb4_get_free_ftid(dev, fs->type ? PF_INET6 : PF_INET);
		if (fidx < 0) {
			netdev_err(dev, "%s: No fidx for offload.\n", __func__);
			ret = -ENOMEM;
			goto free_entry;
		}
	}

	init_completion(&ctx.completion);
	ret = __cxgb4_set_filter(dev, fidx, fs, &ctx);
	if (ret) {
		netdev_err(dev, "%s: filter creation err %d\n",
			   __func__, ret);
		goto free_entry;
	}

	/* Wait for reply */
	ret = wait_for_completion_timeout(&ctx.completion, 10 * HZ);
	if (!ret) {
		ret = -ETIMEDOUT;
		goto free_entry;
	}

	ret = ctx.result;
	/* Check if hw returned error for filter creation */
	if (ret) {
		netdev_err(dev, "%s: filter creation err %d\n",
			   __func__, ret);
		goto free_entry;
	}

	ch_flower->tc_flower_cookie = cls->cookie;
	ch_flower->filter_id = ctx.tid;
	ret = rhashtable_insert_fast(&adap->flower_tbl, &ch_flower->node,
				     adap->flower_ht_params);
	if (ret)
		goto del_filter;

	return 0;

del_filter:
	cxgb4_del_filter(dev, ch_flower->filter_id, &ch_flower->fs);

free_entry:
	kfree(ch_flower);
	return ret;
}

int cxgb4_tc_flower_destroy(struct net_device *dev,
			    struct tc_cls_flower_offload *cls)
{
	struct adapter *adap = netdev2adap(dev);
	struct ch_tc_flower_entry *ch_flower;
	int ret;

	ch_flower = ch_flower_lookup(adap, cls->cookie);
	if (!ch_flower)
		return -ENOENT;

	ret = cxgb4_del_filter(dev, ch_flower->filter_id, &ch_flower->fs);
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
			  struct tc_cls_flower_offload *cls)
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
		tcf_exts_stats_update(cls->exts, bytes - ofld_stats->byte_count,
				      packets - ofld_stats->packet_count,
				      ofld_stats->last_used);

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

	adap->flower_ht_params = cxgb4_tc_flower_ht_params;
	ret = rhashtable_init(&adap->flower_tbl, &adap->flower_ht_params);
	if (ret)
		return ret;

	INIT_WORK(&adap->flower_stats_work, ch_flower_stats_handler);
	timer_setup(&adap->flower_stats_timer, ch_flower_stats_cb, 0);
	mod_timer(&adap->flower_stats_timer, jiffies + STATS_CHECK_PERIOD);
	return 0;
}

void cxgb4_cleanup_tc_flower(struct adapter *adap)
{
	if (adap->flower_stats_timer.function)
		del_timer_sync(&adap->flower_stats_timer);
	cancel_work_sync(&adap->flower_stats_work);
	rhashtable_destroy(&adap->flower_tbl);
}
