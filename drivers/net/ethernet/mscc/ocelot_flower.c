// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 * Copyright (c) 2019 Microsemi Corporation
 */

#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>

#include "ocelot_ace.h"

static int ocelot_flower_parse_action(struct flow_cls_offload *f,
				      struct ocelot_ace_rule *ace)
{
	const struct flow_action_entry *a;
	int i;

	if (f->rule->action.num_entries != 1)
		return -EOPNOTSUPP;

	flow_action_for_each(i, a, &f->rule->action) {
		switch (a->id) {
		case FLOW_ACTION_DROP:
			ace->action = OCELOT_ACL_ACTION_DROP;
			break;
		case FLOW_ACTION_TRAP:
			ace->action = OCELOT_ACL_ACTION_TRAP;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int ocelot_flower_parse(struct flow_cls_offload *f,
			       struct ocelot_ace_rule *ace)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_dissector *dissector = rule->match.dissector;

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS))) {
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;
		u16 proto = ntohs(f->common.protocol);

		/* The hw support mac matches only for MAC_ETYPE key,
		 * therefore if other matches(port, tcp flags, etc) are added
		 * then just bail out
		 */
		if ((dissector->used_keys &
		    (BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
		     BIT(FLOW_DISSECTOR_KEY_BASIC) |
		     BIT(FLOW_DISSECTOR_KEY_CONTROL))) !=
		    (BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
		     BIT(FLOW_DISSECTOR_KEY_BASIC) |
		     BIT(FLOW_DISSECTOR_KEY_CONTROL)))
			return -EOPNOTSUPP;

		if (proto == ETH_P_IP ||
		    proto == ETH_P_IPV6 ||
		    proto == ETH_P_ARP)
			return -EOPNOTSUPP;

		flow_rule_match_eth_addrs(rule, &match);
		ace->type = OCELOT_ACE_TYPE_ETYPE;
		ether_addr_copy(ace->frame.etype.dmac.value,
				match.key->dst);
		ether_addr_copy(ace->frame.etype.smac.value,
				match.key->src);
		ether_addr_copy(ace->frame.etype.dmac.mask,
				match.mask->dst);
		ether_addr_copy(ace->frame.etype.smac.mask,
				match.mask->src);
		goto finished_key_parsing;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		if (ntohs(match.key->n_proto) == ETH_P_IP) {
			ace->type = OCELOT_ACE_TYPE_IPV4;
			ace->frame.ipv4.proto.value[0] =
				match.key->ip_proto;
			ace->frame.ipv4.proto.mask[0] =
				match.mask->ip_proto;
		}
		if (ntohs(match.key->n_proto) == ETH_P_IPV6) {
			ace->type = OCELOT_ACE_TYPE_IPV6;
			ace->frame.ipv6.proto.value[0] =
				match.key->ip_proto;
			ace->frame.ipv6.proto.mask[0] =
				match.mask->ip_proto;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS) &&
	    ntohs(f->common.protocol) == ETH_P_IP) {
		struct flow_match_ipv4_addrs match;
		u8 *tmp;

		flow_rule_match_ipv4_addrs(rule, &match);
		tmp = &ace->frame.ipv4.sip.value.addr[0];
		memcpy(tmp, &match.key->src, 4);

		tmp = &ace->frame.ipv4.sip.mask.addr[0];
		memcpy(tmp, &match.mask->src, 4);

		tmp = &ace->frame.ipv4.dip.value.addr[0];
		memcpy(tmp, &match.key->dst, 4);

		tmp = &ace->frame.ipv4.dip.mask.addr[0];
		memcpy(tmp, &match.mask->dst, 4);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS) &&
	    ntohs(f->common.protocol) == ETH_P_IPV6) {
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		ace->frame.ipv4.sport.value = ntohs(match.key->src);
		ace->frame.ipv4.sport.mask = ntohs(match.mask->src);
		ace->frame.ipv4.dport.value = ntohs(match.key->dst);
		ace->frame.ipv4.dport.mask = ntohs(match.mask->dst);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		ace->type = OCELOT_ACE_TYPE_ANY;
		ace->vlan.vid.value = match.key->vlan_id;
		ace->vlan.vid.mask = match.mask->vlan_id;
		ace->vlan.pcp.value[0] = match.key->vlan_priority;
		ace->vlan.pcp.mask[0] = match.mask->vlan_priority;
	}

finished_key_parsing:
	ace->prio = f->common.prio;
	ace->id = f->cookie;
	return ocelot_flower_parse_action(f, ace);
}

static
struct ocelot_ace_rule *ocelot_ace_rule_create(struct ocelot *ocelot, int port,
					       struct flow_cls_offload *f)
{
	struct ocelot_ace_rule *ace;

	ace = kzalloc(sizeof(*ace), GFP_KERNEL);
	if (!ace)
		return NULL;

	ace->ingress_port_mask = BIT(port);
	return ace;
}

int ocelot_cls_flower_replace(struct ocelot *ocelot, int port,
			      struct flow_cls_offload *f, bool ingress)
{
	struct ocelot_ace_rule *ace;
	int ret;

	ace = ocelot_ace_rule_create(ocelot, port, f);
	if (!ace)
		return -ENOMEM;

	ret = ocelot_flower_parse(f, ace);
	if (ret) {
		kfree(ace);
		return ret;
	}

	return ocelot_ace_rule_offload_add(ocelot, ace);
}
EXPORT_SYMBOL_GPL(ocelot_cls_flower_replace);

int ocelot_cls_flower_destroy(struct ocelot *ocelot, int port,
			      struct flow_cls_offload *f, bool ingress)
{
	struct ocelot_ace_rule ace;

	ace.prio = f->common.prio;
	ace.id = f->cookie;

	return ocelot_ace_rule_offload_del(ocelot, &ace);
}
EXPORT_SYMBOL_GPL(ocelot_cls_flower_destroy);

int ocelot_cls_flower_stats(struct ocelot *ocelot, int port,
			    struct flow_cls_offload *f, bool ingress)
{
	struct ocelot_ace_rule ace;
	int ret;

	ace.prio = f->common.prio;
	ace.id = f->cookie;
	ret = ocelot_ace_rule_stats_update(ocelot, &ace);
	if (ret)
		return ret;

	flow_stats_update(&f->stats, 0x0, ace.stats.pkts, 0x0);
	return 0;
}
EXPORT_SYMBOL_GPL(ocelot_cls_flower_stats);

int ocelot_setup_tc_cls_flower(struct ocelot_port_private *priv,
			       struct flow_cls_offload *f,
			       bool ingress)
{
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	if (!ingress)
		return -EOPNOTSUPP;

	switch (f->command) {
	case FLOW_CLS_REPLACE:
		return ocelot_cls_flower_replace(ocelot, port, f, ingress);
	case FLOW_CLS_DESTROY:
		return ocelot_cls_flower_destroy(ocelot, port, f, ingress);
	case FLOW_CLS_STATS:
		return ocelot_cls_flower_stats(ocelot, port, f, ingress);
	default:
		return -EOPNOTSUPP;
	}
}
