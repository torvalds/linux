// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 * Copyright (c) 2019 Microsemi Corporation
 */

#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>

#include "ocelot_ace.h"

struct ocelot_port_block {
	struct ocelot_acl_block *block;
	struct ocelot_port *port;
};

static u16 get_prio(u32 prio)
{
	/* prio starts from 0x1000 while the ids starts from 0 */
	return prio >> 16;
}

static int ocelot_flower_parse_action(struct tc_cls_flower_offload *f,
				      struct ocelot_ace_rule *rule)
{
	const struct flow_action_entry *a;
	int i;

	if (f->rule->action.num_entries != 1)
		return -EOPNOTSUPP;

	flow_action_for_each(i, a, &f->rule->action) {
		switch (a->id) {
		case FLOW_ACTION_DROP:
			rule->action = OCELOT_ACL_ACTION_DROP;
			break;
		case FLOW_ACTION_TRAP:
			rule->action = OCELOT_ACL_ACTION_TRAP;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int ocelot_flower_parse(struct tc_cls_flower_offload *f,
			       struct ocelot_ace_rule *ocelot_rule)
{
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(f);
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
		ocelot_rule->type = OCELOT_ACE_TYPE_ETYPE;
		ether_addr_copy(ocelot_rule->frame.etype.dmac.value,
				match.key->dst);
		ether_addr_copy(ocelot_rule->frame.etype.smac.value,
				match.key->src);
		ether_addr_copy(ocelot_rule->frame.etype.dmac.mask,
				match.mask->dst);
		ether_addr_copy(ocelot_rule->frame.etype.smac.mask,
				match.mask->src);
		goto finished_key_parsing;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		if (ntohs(match.key->n_proto) == ETH_P_IP) {
			ocelot_rule->type = OCELOT_ACE_TYPE_IPV4;
			ocelot_rule->frame.ipv4.proto.value[0] =
				match.key->ip_proto;
			ocelot_rule->frame.ipv4.proto.mask[0] =
				match.mask->ip_proto;
		}
		if (ntohs(match.key->n_proto) == ETH_P_IPV6) {
			ocelot_rule->type = OCELOT_ACE_TYPE_IPV6;
			ocelot_rule->frame.ipv6.proto.value[0] =
				match.key->ip_proto;
			ocelot_rule->frame.ipv6.proto.mask[0] =
				match.mask->ip_proto;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS) &&
	    ntohs(f->common.protocol) == ETH_P_IP) {
		struct flow_match_ipv4_addrs match;
		u8 *tmp;

		flow_rule_match_ipv4_addrs(rule, &match);
		tmp = &ocelot_rule->frame.ipv4.sip.value.addr[0];
		memcpy(tmp, &match.key->src, 4);

		tmp = &ocelot_rule->frame.ipv4.sip.mask.addr[0];
		memcpy(tmp, &match.mask->src, 4);

		tmp = &ocelot_rule->frame.ipv4.dip.value.addr[0];
		memcpy(tmp, &match.key->dst, 4);

		tmp = &ocelot_rule->frame.ipv4.dip.mask.addr[0];
		memcpy(tmp, &match.mask->dst, 4);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS) &&
	    ntohs(f->common.protocol) == ETH_P_IPV6) {
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		ocelot_rule->frame.ipv4.sport.value = ntohs(match.key->src);
		ocelot_rule->frame.ipv4.sport.mask = ntohs(match.mask->src);
		ocelot_rule->frame.ipv4.dport.value = ntohs(match.key->dst);
		ocelot_rule->frame.ipv4.dport.mask = ntohs(match.mask->dst);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		ocelot_rule->type = OCELOT_ACE_TYPE_ANY;
		ocelot_rule->vlan.vid.value = match.key->vlan_id;
		ocelot_rule->vlan.vid.mask = match.mask->vlan_id;
		ocelot_rule->vlan.pcp.value[0] = match.key->vlan_priority;
		ocelot_rule->vlan.pcp.mask[0] = match.mask->vlan_priority;
	}

finished_key_parsing:
	ocelot_rule->prio = get_prio(f->common.prio);
	ocelot_rule->id = f->cookie;
	return ocelot_flower_parse_action(f, ocelot_rule);
}

static
struct ocelot_ace_rule *ocelot_ace_rule_create(struct tc_cls_flower_offload *f,
					       struct ocelot_port_block *block)
{
	struct ocelot_ace_rule *rule;

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return NULL;

	rule->port = block->port;
	rule->chip_port = block->port->chip_port;
	return rule;
}

static int ocelot_flower_replace(struct tc_cls_flower_offload *f,
				 struct ocelot_port_block *port_block)
{
	struct ocelot_ace_rule *rule;
	int ret;

	rule = ocelot_ace_rule_create(f, port_block);
	if (!rule)
		return -ENOMEM;

	ret = ocelot_flower_parse(f, rule);
	if (ret) {
		kfree(rule);
		return ret;
	}

	ret = ocelot_ace_rule_offload_add(rule);
	if (ret)
		return ret;

	port_block->port->tc.offload_cnt++;
	return 0;
}

static int ocelot_flower_destroy(struct tc_cls_flower_offload *f,
				 struct ocelot_port_block *port_block)
{
	struct ocelot_ace_rule rule;
	int ret;

	rule.prio = get_prio(f->common.prio);
	rule.port = port_block->port;
	rule.id = f->cookie;

	ret = ocelot_ace_rule_offload_del(&rule);
	if (ret)
		return ret;

	port_block->port->tc.offload_cnt--;
	return 0;
}

static int ocelot_flower_stats_update(struct tc_cls_flower_offload *f,
				      struct ocelot_port_block *port_block)
{
	struct ocelot_ace_rule rule;
	int ret;

	rule.prio = get_prio(f->common.prio);
	rule.port = port_block->port;
	rule.id = f->cookie;
	ret = ocelot_ace_rule_stats_update(&rule);
	if (ret)
		return ret;

	flow_stats_update(&f->stats, 0x0, rule.stats.pkts, 0x0);
	return 0;
}

static int ocelot_setup_tc_cls_flower(struct tc_cls_flower_offload *f,
				      struct ocelot_port_block *port_block)
{
	switch (f->command) {
	case TC_CLSFLOWER_REPLACE:
		return ocelot_flower_replace(f, port_block);
	case TC_CLSFLOWER_DESTROY:
		return ocelot_flower_destroy(f, port_block);
	case TC_CLSFLOWER_STATS:
		return ocelot_flower_stats_update(f, port_block);
	default:
		return -EOPNOTSUPP;
	}
}

static int ocelot_setup_tc_block_cb_flower(enum tc_setup_type type,
					   void *type_data, void *cb_priv)
{
	struct ocelot_port_block *port_block = cb_priv;

	if (!tc_cls_can_offload_and_chain0(port_block->port->dev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return ocelot_setup_tc_cls_flower(type_data, cb_priv);
	case TC_SETUP_CLSMATCHALL:
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static struct ocelot_port_block*
ocelot_port_block_create(struct ocelot_port *port)
{
	struct ocelot_port_block *port_block;

	port_block = kzalloc(sizeof(*port_block), GFP_KERNEL);
	if (!port_block)
		return NULL;

	port_block->port = port;

	return port_block;
}

static void ocelot_port_block_destroy(struct ocelot_port_block *block)
{
	kfree(block);
}

static void ocelot_tc_block_unbind(void *cb_priv)
{
	struct ocelot_port_block *port_block = cb_priv;

	ocelot_port_block_destroy(port_block);
}

int ocelot_setup_tc_block_flower_bind(struct ocelot_port *port,
				      struct flow_block_offload *f)
{
	struct ocelot_port_block *port_block;
	struct flow_block_cb *block_cb;
	int ret;

	if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS)
		return -EOPNOTSUPP;

	block_cb = flow_block_cb_lookup(f, ocelot_setup_tc_block_cb_flower,
					port);
	if (!block_cb) {
		port_block = ocelot_port_block_create(port);
		if (!port_block)
			return -ENOMEM;

		block_cb = flow_block_cb_alloc(f->net,
					       ocelot_setup_tc_block_cb_flower,
					       port, port_block,
					       ocelot_tc_block_unbind);
		if (IS_ERR(block_cb)) {
			ret = PTR_ERR(block_cb);
			goto err_cb_register;
		}
		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, f->driver_block_list);
	} else {
		port_block = flow_block_cb_priv(block_cb);
	}

	flow_block_cb_incref(block_cb);
	return 0;

err_cb_register:
	ocelot_port_block_destroy(port_block);

	return ret;
}

void ocelot_setup_tc_block_flower_unbind(struct ocelot_port *port,
					 struct flow_block_offload *f)
{
	struct flow_block_cb *block_cb;

	block_cb = flow_block_cb_lookup(f, ocelot_setup_tc_block_cb_flower,
					port);
	if (!block_cb)
		return;

	if (!flow_block_cb_decref(block_cb)) {
		flow_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
	}
}
