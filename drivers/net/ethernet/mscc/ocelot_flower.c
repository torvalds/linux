// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 * Copyright (c) 2019 Microsemi Corporation
 */

#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <soc/mscc/ocelot_vcap.h>
#include "ocelot_vcap.h"

/* Arbitrarily chosen constants for encoding the VCAP block and lookup number
 * into the chain number. This is UAPI.
 */
#define VCAP_BLOCK			10000
#define VCAP_LOOKUP			1000
#define VCAP_IS1_NUM_LOOKUPS		3
#define VCAP_IS2_NUM_LOOKUPS		2
#define VCAP_IS2_NUM_PAG		256
#define VCAP_IS1_CHAIN(lookup)		\
	(1 * VCAP_BLOCK + (lookup) * VCAP_LOOKUP)
#define VCAP_IS2_CHAIN(lookup, pag)	\
	(2 * VCAP_BLOCK + (lookup) * VCAP_LOOKUP + (pag))

static int ocelot_chain_to_block(int chain, bool ingress)
{
	int lookup, pag;

	if (!ingress) {
		if (chain == 0)
			return VCAP_ES0;
		return -EOPNOTSUPP;
	}

	/* Backwards compatibility with older, single-chain tc-flower
	 * offload support in Ocelot
	 */
	if (chain == 0)
		return VCAP_IS2;

	for (lookup = 0; lookup < VCAP_IS1_NUM_LOOKUPS; lookup++)
		if (chain == VCAP_IS1_CHAIN(lookup))
			return VCAP_IS1;

	for (lookup = 0; lookup < VCAP_IS2_NUM_LOOKUPS; lookup++)
		for (pag = 0; pag < VCAP_IS2_NUM_PAG; pag++)
			if (chain == VCAP_IS2_CHAIN(lookup, pag))
				return VCAP_IS2;

	return -EOPNOTSUPP;
}

/* Caller must ensure this is a valid IS1 or IS2 chain first,
 * by calling ocelot_chain_to_block.
 */
static int ocelot_chain_to_lookup(int chain)
{
	return (chain / VCAP_LOOKUP) % 10;
}

static bool ocelot_is_goto_target_valid(int goto_target, int chain,
					bool ingress)
{
	int pag;

	/* Can't offload GOTO in VCAP ES0 */
	if (!ingress)
		return (goto_target < 0);

	/* Non-optional GOTOs */
	if (chain == 0)
		/* VCAP IS1 can be skipped, either partially or completely */
		return (goto_target == VCAP_IS1_CHAIN(0) ||
			goto_target == VCAP_IS1_CHAIN(1) ||
			goto_target == VCAP_IS1_CHAIN(2) ||
			goto_target == VCAP_IS2_CHAIN(0, 0) ||
			goto_target == VCAP_IS2_CHAIN(1, 0));

	if (chain == VCAP_IS1_CHAIN(0))
		return (goto_target == VCAP_IS1_CHAIN(1));

	if (chain == VCAP_IS1_CHAIN(1))
		return (goto_target == VCAP_IS1_CHAIN(2));

	/* Lookup 2 of VCAP IS1 can really support non-optional GOTOs,
	 * using a Policy Association Group (PAG) value, which is an 8-bit
	 * value encoding a VCAP IS2 target chain.
	 */
	if (chain == VCAP_IS1_CHAIN(2)) {
		for (pag = 0; pag < VCAP_IS2_NUM_PAG; pag++)
			if (goto_target == VCAP_IS2_CHAIN(0, pag))
				return true;

		return false;
	}

	/* Non-optional GOTO from VCAP IS2 lookup 0 to lookup 1.
	 * We cannot change the PAG at this point.
	 */
	for (pag = 0; pag < VCAP_IS2_NUM_PAG; pag++)
		if (chain == VCAP_IS2_CHAIN(0, pag))
			return (goto_target == VCAP_IS2_CHAIN(1, pag));

	/* VCAP IS2 lookup 1 cannot jump anywhere */
	return false;
}

static struct ocelot_vcap_filter *
ocelot_find_vcap_filter_that_points_at(struct ocelot *ocelot, int chain)
{
	struct ocelot_vcap_filter *filter;
	struct ocelot_vcap_block *block;
	int block_id;

	block_id = ocelot_chain_to_block(chain, true);
	if (block_id < 0)
		return NULL;

	if (block_id == VCAP_IS2) {
		block = &ocelot->block[VCAP_IS1];

		list_for_each_entry(filter, &block->rules, list)
			if (filter->type == OCELOT_VCAP_FILTER_PAG &&
			    filter->goto_target == chain)
				return filter;
	}

	list_for_each_entry(filter, &ocelot->dummy_rules, list)
		if (filter->goto_target == chain)
			return filter;

	return NULL;
}

static int ocelot_flower_parse_action(struct flow_cls_offload *f, bool ingress,
				      struct ocelot_vcap_filter *filter)
{
	struct netlink_ext_ack *extack = f->common.extack;
	bool allow_missing_goto_target = false;
	const struct flow_action_entry *a;
	int i, chain;
	u64 rate;

	if (!flow_action_basic_hw_stats_check(&f->rule->action,
					      f->common.extack))
		return -EOPNOTSUPP;

	chain = f->common.chain_index;
	filter->block_id = ocelot_chain_to_block(chain, ingress);
	if (filter->block_id < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload to this chain");
		return -EOPNOTSUPP;
	}
	if (filter->block_id == VCAP_IS1 || filter->block_id == VCAP_IS2)
		filter->lookup = ocelot_chain_to_lookup(chain);

	filter->goto_target = -1;
	filter->type = OCELOT_VCAP_FILTER_DUMMY;

	flow_action_for_each(i, a, &f->rule->action) {
		switch (a->id) {
		case FLOW_ACTION_DROP:
			if (filter->block_id != VCAP_IS2) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Drop action can only be offloaded to VCAP IS2");
				return -EOPNOTSUPP;
			}
			if (filter->goto_target != -1) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Last action must be GOTO");
				return -EOPNOTSUPP;
			}
			filter->action.mask_mode = OCELOT_MASK_MODE_PERMIT_DENY;
			filter->action.port_mask = 0;
			filter->action.police_ena = true;
			filter->action.pol_ix = OCELOT_POLICER_DISCARD;
			filter->type = OCELOT_VCAP_FILTER_OFFLOAD;
			break;
		case FLOW_ACTION_TRAP:
			if (filter->block_id != VCAP_IS2) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Trap action can only be offloaded to VCAP IS2");
				return -EOPNOTSUPP;
			}
			if (filter->goto_target != -1) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Last action must be GOTO");
				return -EOPNOTSUPP;
			}
			filter->action.mask_mode = OCELOT_MASK_MODE_PERMIT_DENY;
			filter->action.port_mask = 0;
			filter->action.cpu_copy_ena = true;
			filter->action.cpu_qu_num = 0;
			filter->type = OCELOT_VCAP_FILTER_OFFLOAD;
			break;
		case FLOW_ACTION_POLICE:
			if (filter->block_id != VCAP_IS2) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Police action can only be offloaded to VCAP IS2");
				return -EOPNOTSUPP;
			}
			if (filter->goto_target != -1) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Last action must be GOTO");
				return -EOPNOTSUPP;
			}
			filter->action.police_ena = true;
			rate = a->police.rate_bytes_ps;
			filter->action.pol.rate = div_u64(rate, 1000) * 8;
			filter->action.pol.burst = a->police.burst;
			filter->type = OCELOT_VCAP_FILTER_OFFLOAD;
			break;
		case FLOW_ACTION_GOTO:
			filter->goto_target = a->chain_index;
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "Cannot offload action");
			return -EOPNOTSUPP;
		}
	}

	if (filter->goto_target == -1) {
		if ((filter->block_id == VCAP_IS2 && filter->lookup == 1) ||
		    chain == 0) {
			allow_missing_goto_target = true;
		} else {
			NL_SET_ERR_MSG_MOD(extack, "Missing GOTO action");
			return -EOPNOTSUPP;
		}
	}

	if (!ocelot_is_goto_target_valid(filter->goto_target, chain, ingress) &&
	    !allow_missing_goto_target) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot offload this GOTO target");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ocelot_flower_parse_key(struct flow_cls_offload *f, bool ingress,
				   struct ocelot_vcap_filter *filter)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_dissector *dissector = rule->match.dissector;
	u16 proto = ntohs(f->common.protocol);
	bool match_protocol = true;

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

		flow_rule_match_eth_addrs(rule, &match);
		filter->key_type = OCELOT_VCAP_KEY_ETYPE;
		ether_addr_copy(filter->key.etype.dmac.value,
				match.key->dst);
		ether_addr_copy(filter->key.etype.smac.value,
				match.key->src);
		ether_addr_copy(filter->key.etype.dmac.mask,
				match.mask->dst);
		ether_addr_copy(filter->key.etype.smac.mask,
				match.mask->src);
		goto finished_key_parsing;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		if (ntohs(match.key->n_proto) == ETH_P_IP) {
			filter->key_type = OCELOT_VCAP_KEY_IPV4;
			filter->key.ipv4.proto.value[0] =
				match.key->ip_proto;
			filter->key.ipv4.proto.mask[0] =
				match.mask->ip_proto;
			match_protocol = false;
		}
		if (ntohs(match.key->n_proto) == ETH_P_IPV6) {
			filter->key_type = OCELOT_VCAP_KEY_IPV6;
			filter->key.ipv6.proto.value[0] =
				match.key->ip_proto;
			filter->key.ipv6.proto.mask[0] =
				match.mask->ip_proto;
			match_protocol = false;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS) &&
	    proto == ETH_P_IP) {
		struct flow_match_ipv4_addrs match;
		u8 *tmp;

		flow_rule_match_ipv4_addrs(rule, &match);
		tmp = &filter->key.ipv4.sip.value.addr[0];
		memcpy(tmp, &match.key->src, 4);

		tmp = &filter->key.ipv4.sip.mask.addr[0];
		memcpy(tmp, &match.mask->src, 4);

		tmp = &filter->key.ipv4.dip.value.addr[0];
		memcpy(tmp, &match.key->dst, 4);

		tmp = &filter->key.ipv4.dip.mask.addr[0];
		memcpy(tmp, &match.mask->dst, 4);
		match_protocol = false;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS) &&
	    proto == ETH_P_IPV6) {
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		filter->key.ipv4.sport.value = ntohs(match.key->src);
		filter->key.ipv4.sport.mask = ntohs(match.mask->src);
		filter->key.ipv4.dport.value = ntohs(match.key->dst);
		filter->key.ipv4.dport.mask = ntohs(match.mask->dst);
		match_protocol = false;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		filter->key_type = OCELOT_VCAP_KEY_ANY;
		filter->vlan.vid.value = match.key->vlan_id;
		filter->vlan.vid.mask = match.mask->vlan_id;
		filter->vlan.pcp.value[0] = match.key->vlan_priority;
		filter->vlan.pcp.mask[0] = match.mask->vlan_priority;
		match_protocol = false;
	}

finished_key_parsing:
	if (match_protocol && proto != ETH_P_ALL) {
		/* TODO: support SNAP, LLC etc */
		if (proto < ETH_P_802_3_MIN)
			return -EOPNOTSUPP;
		filter->key_type = OCELOT_VCAP_KEY_ETYPE;
		*(__be16 *)filter->key.etype.etype.value = htons(proto);
		*(__be16 *)filter->key.etype.etype.mask = htons(0xffff);
	}
	/* else, a filter of type OCELOT_VCAP_KEY_ANY is implicitly added */

	return 0;
}

static int ocelot_flower_parse(struct flow_cls_offload *f, bool ingress,
			       struct ocelot_vcap_filter *filter)
{
	int ret;

	filter->prio = f->common.prio;
	filter->id = f->cookie;

	ret = ocelot_flower_parse_action(f, ingress, filter);
	if (ret)
		return ret;

	return ocelot_flower_parse_key(f, ingress, filter);
}

static struct ocelot_vcap_filter
*ocelot_vcap_filter_create(struct ocelot *ocelot, int port,
			 struct flow_cls_offload *f)
{
	struct ocelot_vcap_filter *filter;

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter)
		return NULL;

	filter->ingress_port_mask = BIT(port);
	return filter;
}

static int ocelot_vcap_dummy_filter_add(struct ocelot *ocelot,
					struct ocelot_vcap_filter *filter)
{
	list_add(&filter->list, &ocelot->dummy_rules);

	return 0;
}

static int ocelot_vcap_dummy_filter_del(struct ocelot *ocelot,
					struct ocelot_vcap_filter *filter)
{
	list_del(&filter->list);
	kfree(filter);

	return 0;
}

int ocelot_cls_flower_replace(struct ocelot *ocelot, int port,
			      struct flow_cls_offload *f, bool ingress)
{
	struct netlink_ext_ack *extack = f->common.extack;
	struct ocelot_vcap_filter *filter;
	int chain = f->common.chain_index;
	int ret;

	if (chain && !ocelot_find_vcap_filter_that_points_at(ocelot, chain)) {
		NL_SET_ERR_MSG_MOD(extack, "No default GOTO action points to this chain");
		return -EOPNOTSUPP;
	}

	filter = ocelot_vcap_filter_create(ocelot, port, f);
	if (!filter)
		return -ENOMEM;

	ret = ocelot_flower_parse(f, ingress, filter);
	if (ret) {
		kfree(filter);
		return ret;
	}

	/* The non-optional GOTOs for the TCAM skeleton don't need
	 * to be actually offloaded.
	 */
	if (filter->type == OCELOT_VCAP_FILTER_DUMMY)
		return ocelot_vcap_dummy_filter_add(ocelot, filter);

	return ocelot_vcap_filter_add(ocelot, filter, f->common.extack);
}
EXPORT_SYMBOL_GPL(ocelot_cls_flower_replace);

int ocelot_cls_flower_destroy(struct ocelot *ocelot, int port,
			      struct flow_cls_offload *f, bool ingress)
{
	struct ocelot_vcap_filter *filter;
	struct ocelot_vcap_block *block;
	int block_id;

	block_id = ocelot_chain_to_block(f->common.chain_index, ingress);
	if (block_id < 0)
		return 0;

	block = &ocelot->block[block_id];

	filter = ocelot_vcap_block_find_filter_by_id(block, f->cookie);
	if (!filter)
		return 0;

	if (filter->type == OCELOT_VCAP_FILTER_DUMMY)
		return ocelot_vcap_dummy_filter_del(ocelot, filter);

	return ocelot_vcap_filter_del(ocelot, filter);
}
EXPORT_SYMBOL_GPL(ocelot_cls_flower_destroy);

int ocelot_cls_flower_stats(struct ocelot *ocelot, int port,
			    struct flow_cls_offload *f, bool ingress)
{
	struct ocelot_vcap_filter *filter;
	struct ocelot_vcap_block *block;
	int block_id, ret;

	block_id = ocelot_chain_to_block(f->common.chain_index, ingress);
	if (block_id < 0)
		return 0;

	block = &ocelot->block[block_id];

	filter = ocelot_vcap_block_find_filter_by_id(block, f->cookie);
	if (!filter || filter->type == OCELOT_VCAP_FILTER_DUMMY)
		return 0;

	ret = ocelot_vcap_filter_stats_update(ocelot, filter);
	if (ret)
		return ret;

	flow_stats_update(&f->stats, 0x0, filter->stats.pkts, 0, 0x0,
			  FLOW_ACTION_HW_STATS_IMMEDIATE);
	return 0;
}
EXPORT_SYMBOL_GPL(ocelot_cls_flower_stats);
