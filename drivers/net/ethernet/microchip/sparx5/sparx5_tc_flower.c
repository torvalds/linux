// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP API
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/tcp.h>

#include "sparx5_tc.h"
#include "vcap_api.h"
#include "vcap_api_client.h"
#include "sparx5_main.h"
#include "sparx5_vcap_impl.h"

#define SPX5_MAX_RULE_SIZE 13 /* allows X1, X2, X4, X6 and X12 rules */

/* Collect keysets and type ids for multiple rules per size */
struct sparx5_wildcard_rule {
	bool selected;
	u8 value;
	u8 mask;
	enum vcap_keyfield_set keyset;
};

struct sparx5_multiple_rules {
	struct sparx5_wildcard_rule rule[SPX5_MAX_RULE_SIZE];
};

struct sparx5_tc_flower_parse_usage {
	struct flow_cls_offload *fco;
	struct flow_rule *frule;
	struct vcap_rule *vrule;
	struct vcap_admin *admin;
	u16 l3_proto;
	u8 l4_proto;
	unsigned int used_keys;
};

enum sparx5_is2_arp_opcode {
	SPX5_IS2_ARP_REQUEST,
	SPX5_IS2_ARP_REPLY,
	SPX5_IS2_RARP_REQUEST,
	SPX5_IS2_RARP_REPLY,
};

enum tc_arp_opcode {
	TC_ARP_OP_RESERVED,
	TC_ARP_OP_REQUEST,
	TC_ARP_OP_REPLY,
};

static int sparx5_tc_flower_handler_ethaddr_usage(struct sparx5_tc_flower_parse_usage *st)
{
	enum vcap_key_field smac_key = VCAP_KF_L2_SMAC;
	enum vcap_key_field dmac_key = VCAP_KF_L2_DMAC;
	struct flow_match_eth_addrs match;
	struct vcap_u48_key smac, dmac;
	int err = 0;

	flow_rule_match_eth_addrs(st->frule, &match);

	if (!is_zero_ether_addr(match.mask->src)) {
		vcap_netbytes_copy(smac.value, match.key->src, ETH_ALEN);
		vcap_netbytes_copy(smac.mask, match.mask->src, ETH_ALEN);
		err = vcap_rule_add_key_u48(st->vrule, smac_key, &smac);
		if (err)
			goto out;
	}

	if (!is_zero_ether_addr(match.mask->dst)) {
		vcap_netbytes_copy(dmac.value, match.key->dst, ETH_ALEN);
		vcap_netbytes_copy(dmac.mask, match.mask->dst, ETH_ALEN);
		err = vcap_rule_add_key_u48(st->vrule, dmac_key, &dmac);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS);

	return err;

out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "eth_addr parse error");
	return err;
}

static int
sparx5_tc_flower_handler_ipv4_usage(struct sparx5_tc_flower_parse_usage *st)
{
	int err = 0;

	if (st->l3_proto == ETH_P_IP) {
		struct flow_match_ipv4_addrs mt;

		flow_rule_match_ipv4_addrs(st->frule, &mt);
		if (mt.mask->src) {
			err = vcap_rule_add_key_u32(st->vrule,
						    VCAP_KF_L3_IP4_SIP,
						    be32_to_cpu(mt.key->src),
						    be32_to_cpu(mt.mask->src));
			if (err)
				goto out;
		}
		if (mt.mask->dst) {
			err = vcap_rule_add_key_u32(st->vrule,
						    VCAP_KF_L3_IP4_DIP,
						    be32_to_cpu(mt.key->dst),
						    be32_to_cpu(mt.mask->dst));
			if (err)
				goto out;
		}
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS);

	return err;

out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "ipv4_addr parse error");
	return err;
}

static int
sparx5_tc_flower_handler_ipv6_usage(struct sparx5_tc_flower_parse_usage *st)
{
	int err = 0;

	if (st->l3_proto == ETH_P_IPV6) {
		struct flow_match_ipv6_addrs mt;
		struct vcap_u128_key sip;
		struct vcap_u128_key dip;

		flow_rule_match_ipv6_addrs(st->frule, &mt);
		/* Check if address masks are non-zero */
		if (!ipv6_addr_any(&mt.mask->src)) {
			vcap_netbytes_copy(sip.value, mt.key->src.s6_addr, 16);
			vcap_netbytes_copy(sip.mask, mt.mask->src.s6_addr, 16);
			err = vcap_rule_add_key_u128(st->vrule,
						     VCAP_KF_L3_IP6_SIP, &sip);
			if (err)
				goto out;
		}
		if (!ipv6_addr_any(&mt.mask->dst)) {
			vcap_netbytes_copy(dip.value, mt.key->dst.s6_addr, 16);
			vcap_netbytes_copy(dip.mask, mt.mask->dst.s6_addr, 16);
			err = vcap_rule_add_key_u128(st->vrule,
						     VCAP_KF_L3_IP6_DIP, &dip);
			if (err)
				goto out;
		}
	}
	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS);
	return err;
out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "ipv6_addr parse error");
	return err;
}

static int
sparx5_tc_flower_handler_control_usage(struct sparx5_tc_flower_parse_usage *st)
{
	struct flow_match_control mt;
	u32 value, mask;
	int err = 0;

	flow_rule_match_control(st->frule, &mt);

	if (mt.mask->flags) {
		if (mt.mask->flags & FLOW_DIS_FIRST_FRAG) {
			if (mt.key->flags & FLOW_DIS_FIRST_FRAG) {
				value = 1; /* initial fragment */
				mask = 0x3;
			} else {
				if (mt.mask->flags & FLOW_DIS_IS_FRAGMENT) {
					value = 3; /* follow up fragment */
					mask = 0x3;
				} else {
					value = 0; /* no fragment */
					mask = 0x3;
				}
			}
		} else {
			if (mt.mask->flags & FLOW_DIS_IS_FRAGMENT) {
				value = 3; /* follow up fragment */
				mask = 0x3;
			} else {
				value = 0; /* no fragment */
				mask = 0x3;
			}
		}

		err = vcap_rule_add_key_u32(st->vrule,
					    VCAP_KF_L3_FRAGMENT_TYPE,
					    value, mask);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_CONTROL);

	return err;

out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "ip_frag parse error");
	return err;
}

static int
sparx5_tc_flower_handler_portnum_usage(struct sparx5_tc_flower_parse_usage *st)
{
	struct flow_match_ports mt;
	u16 value, mask;
	int err = 0;

	flow_rule_match_ports(st->frule, &mt);

	if (mt.mask->src) {
		value = be16_to_cpu(mt.key->src);
		mask = be16_to_cpu(mt.mask->src);
		err = vcap_rule_add_key_u32(st->vrule, VCAP_KF_L4_SPORT, value,
					    mask);
		if (err)
			goto out;
	}

	if (mt.mask->dst) {
		value = be16_to_cpu(mt.key->dst);
		mask = be16_to_cpu(mt.mask->dst);
		err = vcap_rule_add_key_u32(st->vrule, VCAP_KF_L4_DPORT, value,
					    mask);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_PORTS);

	return err;

out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "port parse error");
	return err;
}

static int
sparx5_tc_flower_handler_basic_usage(struct sparx5_tc_flower_parse_usage *st)
{
	struct flow_match_basic mt;
	int err = 0;

	flow_rule_match_basic(st->frule, &mt);

	if (mt.mask->n_proto) {
		st->l3_proto = be16_to_cpu(mt.key->n_proto);
		if (!sparx5_vcap_is_known_etype(st->admin, st->l3_proto)) {
			err = vcap_rule_add_key_u32(st->vrule, VCAP_KF_ETYPE,
						    st->l3_proto, ~0);
			if (err)
				goto out;
		} else if (st->l3_proto == ETH_P_IP) {
			err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_IP4_IS,
						    VCAP_BIT_1);
			if (err)
				goto out;
		} else if (st->l3_proto == ETH_P_IPV6) {
			err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_IP4_IS,
						    VCAP_BIT_0);
			if (err)
				goto out;
			if (st->admin->vtype == VCAP_TYPE_IS0) {
				err = vcap_rule_add_key_bit(st->vrule,
							    VCAP_KF_IP_SNAP_IS,
							    VCAP_BIT_1);
				if (err)
					goto out;
			}

		}
	}

	if (mt.mask->ip_proto) {
		st->l4_proto = mt.key->ip_proto;
		if (st->l4_proto == IPPROTO_TCP) {
			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_TCP_IS,
						    VCAP_BIT_1);
			if (err)
				goto out;
		} else if (st->l4_proto == IPPROTO_UDP) {
			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_TCP_IS,
						    VCAP_BIT_0);
			if (err)
				goto out;
			if (st->admin->vtype == VCAP_TYPE_IS0) {
				err = vcap_rule_add_key_bit(st->vrule,
							    VCAP_KF_TCP_UDP_IS,
							    VCAP_BIT_1);
				if (err)
					goto out;
			}
		} else {
			err = vcap_rule_add_key_u32(st->vrule,
						    VCAP_KF_L3_IP_PROTO,
						    st->l4_proto, ~0);
			if (err)
				goto out;
		}
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_BASIC);

	return err;

out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "ip_proto parse error");
	return err;
}

static int
sparx5_tc_flower_handler_cvlan_usage(struct sparx5_tc_flower_parse_usage *st)
{
	enum vcap_key_field vid_key = VCAP_KF_8021Q_VID0;
	enum vcap_key_field pcp_key = VCAP_KF_8021Q_PCP0;
	struct flow_match_vlan mt;
	u16 tpid;
	int err;

	if (st->admin->vtype != VCAP_TYPE_IS0) {
		NL_SET_ERR_MSG_MOD(st->fco->common.extack,
				   "cvlan not supported in this VCAP");
		return -EINVAL;
	}

	flow_rule_match_cvlan(st->frule, &mt);

	tpid = be16_to_cpu(mt.key->vlan_tpid);

	if (tpid == ETH_P_8021Q) {
		vid_key = VCAP_KF_8021Q_VID1;
		pcp_key = VCAP_KF_8021Q_PCP1;
	}

	if (mt.mask->vlan_id) {
		err = vcap_rule_add_key_u32(st->vrule, vid_key,
					    mt.key->vlan_id,
					    mt.mask->vlan_id);
		if (err)
			goto out;
	}

	if (mt.mask->vlan_priority) {
		err = vcap_rule_add_key_u32(st->vrule, pcp_key,
					    mt.key->vlan_priority,
					    mt.mask->vlan_priority);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_CVLAN);

	return 0;
out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "cvlan parse error");
	return err;
}

static int
sparx5_tc_flower_handler_vlan_usage(struct sparx5_tc_flower_parse_usage *st)
{
	enum vcap_key_field vid_key = VCAP_KF_8021Q_VID_CLS;
	enum vcap_key_field pcp_key = VCAP_KF_8021Q_PCP_CLS;
	struct flow_match_vlan mt;
	int err;

	flow_rule_match_vlan(st->frule, &mt);

	if (st->admin->vtype == VCAP_TYPE_IS0) {
		vid_key = VCAP_KF_8021Q_VID0;
		pcp_key = VCAP_KF_8021Q_PCP0;
	}

	if (mt.mask->vlan_id) {
		err = vcap_rule_add_key_u32(st->vrule, vid_key,
					    mt.key->vlan_id,
					    mt.mask->vlan_id);
		if (err)
			goto out;
	}

	if (mt.mask->vlan_priority) {
		err = vcap_rule_add_key_u32(st->vrule, pcp_key,
					    mt.key->vlan_priority,
					    mt.mask->vlan_priority);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_VLAN);

	return 0;
out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "vlan parse error");
	return err;
}

static int
sparx5_tc_flower_handler_tcp_usage(struct sparx5_tc_flower_parse_usage *st)
{
	struct flow_match_tcp mt;
	u16 tcp_flags_mask;
	u16 tcp_flags_key;
	enum vcap_bit val;
	int err = 0;

	flow_rule_match_tcp(st->frule, &mt);
	tcp_flags_key = be16_to_cpu(mt.key->flags);
	tcp_flags_mask = be16_to_cpu(mt.mask->flags);

	if (tcp_flags_mask & TCPHDR_FIN) {
		val = VCAP_BIT_0;
		if (tcp_flags_key & TCPHDR_FIN)
			val = VCAP_BIT_1;
		err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_L4_FIN, val);
		if (err)
			goto out;
	}

	if (tcp_flags_mask & TCPHDR_SYN) {
		val = VCAP_BIT_0;
		if (tcp_flags_key & TCPHDR_SYN)
			val = VCAP_BIT_1;
		err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_L4_SYN, val);
		if (err)
			goto out;
	}

	if (tcp_flags_mask & TCPHDR_RST) {
		val = VCAP_BIT_0;
		if (tcp_flags_key & TCPHDR_RST)
			val = VCAP_BIT_1;
		err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_L4_RST, val);
		if (err)
			goto out;
	}

	if (tcp_flags_mask & TCPHDR_PSH) {
		val = VCAP_BIT_0;
		if (tcp_flags_key & TCPHDR_PSH)
			val = VCAP_BIT_1;
		err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_L4_PSH, val);
		if (err)
			goto out;
	}

	if (tcp_flags_mask & TCPHDR_ACK) {
		val = VCAP_BIT_0;
		if (tcp_flags_key & TCPHDR_ACK)
			val = VCAP_BIT_1;
		err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_L4_ACK, val);
		if (err)
			goto out;
	}

	if (tcp_flags_mask & TCPHDR_URG) {
		val = VCAP_BIT_0;
		if (tcp_flags_key & TCPHDR_URG)
			val = VCAP_BIT_1;
		err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_L4_URG, val);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_TCP);

	return err;

out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "tcp_flags parse error");
	return err;
}

static int
sparx5_tc_flower_handler_arp_usage(struct sparx5_tc_flower_parse_usage *st)
{
	struct flow_match_arp mt;
	u16 value, mask;
	u32 ipval, ipmsk;
	int err;

	flow_rule_match_arp(st->frule, &mt);

	if (mt.mask->op) {
		mask = 0x3;
		if (st->l3_proto == ETH_P_ARP) {
			value = mt.key->op == TC_ARP_OP_REQUEST ?
					SPX5_IS2_ARP_REQUEST :
					SPX5_IS2_ARP_REPLY;
		} else { /* RARP */
			value = mt.key->op == TC_ARP_OP_REQUEST ?
					SPX5_IS2_RARP_REQUEST :
					SPX5_IS2_RARP_REPLY;
		}
		err = vcap_rule_add_key_u32(st->vrule, VCAP_KF_ARP_OPCODE,
					    value, mask);
		if (err)
			goto out;
	}

	/* The IS2 ARP keyset does not support ARP hardware addresses */
	if (!is_zero_ether_addr(mt.mask->sha) ||
	    !is_zero_ether_addr(mt.mask->tha)) {
		err = -EINVAL;
		goto out;
	}

	if (mt.mask->sip) {
		ipval = be32_to_cpu((__force __be32)mt.key->sip);
		ipmsk = be32_to_cpu((__force __be32)mt.mask->sip);

		err = vcap_rule_add_key_u32(st->vrule, VCAP_KF_L3_IP4_SIP,
					    ipval, ipmsk);
		if (err)
			goto out;
	}

	if (mt.mask->tip) {
		ipval = be32_to_cpu((__force __be32)mt.key->tip);
		ipmsk = be32_to_cpu((__force __be32)mt.mask->tip);

		err = vcap_rule_add_key_u32(st->vrule, VCAP_KF_L3_IP4_DIP,
					    ipval, ipmsk);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_ARP);

	return 0;

out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "arp parse error");
	return err;
}

static int
sparx5_tc_flower_handler_ip_usage(struct sparx5_tc_flower_parse_usage *st)
{
	struct flow_match_ip mt;
	int err = 0;

	flow_rule_match_ip(st->frule, &mt);

	if (mt.mask->tos) {
		err = vcap_rule_add_key_u32(st->vrule, VCAP_KF_L3_TOS,
					    mt.key->tos,
					    mt.mask->tos);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_IP);

	return err;

out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "ip_tos parse error");
	return err;
}

static int (*sparx5_tc_flower_usage_handlers[])(struct sparx5_tc_flower_parse_usage *st) = {
	[FLOW_DISSECTOR_KEY_ETH_ADDRS] = sparx5_tc_flower_handler_ethaddr_usage,
	[FLOW_DISSECTOR_KEY_IPV4_ADDRS] = sparx5_tc_flower_handler_ipv4_usage,
	[FLOW_DISSECTOR_KEY_IPV6_ADDRS] = sparx5_tc_flower_handler_ipv6_usage,
	[FLOW_DISSECTOR_KEY_CONTROL] = sparx5_tc_flower_handler_control_usage,
	[FLOW_DISSECTOR_KEY_PORTS] = sparx5_tc_flower_handler_portnum_usage,
	[FLOW_DISSECTOR_KEY_BASIC] = sparx5_tc_flower_handler_basic_usage,
	[FLOW_DISSECTOR_KEY_CVLAN] = sparx5_tc_flower_handler_cvlan_usage,
	[FLOW_DISSECTOR_KEY_VLAN] = sparx5_tc_flower_handler_vlan_usage,
	[FLOW_DISSECTOR_KEY_TCP] = sparx5_tc_flower_handler_tcp_usage,
	[FLOW_DISSECTOR_KEY_ARP] = sparx5_tc_flower_handler_arp_usage,
	[FLOW_DISSECTOR_KEY_IP] = sparx5_tc_flower_handler_ip_usage,
};

static int sparx5_tc_use_dissectors(struct flow_cls_offload *fco,
				    struct vcap_admin *admin,
				    struct vcap_rule *vrule,
				    u16 *l3_proto)
{
	struct sparx5_tc_flower_parse_usage state = {
		.fco = fco,
		.vrule = vrule,
		.l3_proto = ETH_P_ALL,
		.admin = admin,
	};
	int idx, err = 0;

	state.frule = flow_cls_offload_flow_rule(fco);
	for (idx = 0; idx < ARRAY_SIZE(sparx5_tc_flower_usage_handlers); ++idx) {
		if (!flow_rule_match_key(state.frule, idx))
			continue;
		if (!sparx5_tc_flower_usage_handlers[idx])
			continue;
		err = sparx5_tc_flower_usage_handlers[idx](&state);
		if (err)
			return err;
	}

	if (state.frule->match.dissector->used_keys ^ state.used_keys) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Unsupported match item");
		return -ENOENT;
	}

	if (l3_proto)
		*l3_proto = state.l3_proto;
	return err;
}

static int sparx5_tc_flower_action_check(struct vcap_control *vctrl,
					 struct net_device *ndev,
					 struct flow_cls_offload *fco,
					 bool ingress)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(fco);
	struct flow_action_entry *actent, *last_actent = NULL;
	struct flow_action *act = &rule->action;
	u64 action_mask = 0;
	int idx;

	if (!flow_action_has_entries(act)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack, "No actions");
		return -EINVAL;
	}

	if (!flow_action_basic_hw_stats_check(act, fco->common.extack))
		return -EOPNOTSUPP;

	flow_action_for_each(idx, actent, act) {
		if (action_mask & BIT(actent->id)) {
			NL_SET_ERR_MSG_MOD(fco->common.extack,
					   "More actions of the same type");
			return -EINVAL;
		}
		action_mask |= BIT(actent->id);
		last_actent = actent; /* Save last action for later check */
	}

	/* Check if last action is a goto
	 * The last chain/lookup does not need to have a goto action
	 */
	if (last_actent->id == FLOW_ACTION_GOTO) {
		/* Check if the destination chain is in one of the VCAPs */
		if (!vcap_is_next_lookup(vctrl, fco->common.chain_index,
					 last_actent->chain_index)) {
			NL_SET_ERR_MSG_MOD(fco->common.extack,
					   "Invalid goto chain");
			return -EINVAL;
		}
	} else if (!vcap_is_last_chain(vctrl, fco->common.chain_index,
				       ingress)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Last action must be 'goto'");
		return -EINVAL;
	}

	/* Catch unsupported combinations of actions */
	if (action_mask & BIT(FLOW_ACTION_TRAP) &&
	    action_mask & BIT(FLOW_ACTION_ACCEPT)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Cannot combine pass and trap action");
		return -EOPNOTSUPP;
	}

	return 0;
}

/* Add a rule counter action */
static int sparx5_tc_add_rule_counter(struct vcap_admin *admin,
				      struct vcap_rule *vrule)
{
	int err;

	if (admin->vtype == VCAP_TYPE_IS2 || admin->vtype == VCAP_TYPE_ES2) {
		err = vcap_rule_mod_action_u32(vrule, VCAP_AF_CNT_ID,
					       vrule->id);
		if (err)
			return err;
		vcap_rule_set_counter_id(vrule, vrule->id);
	}

	return 0;
}

/* Collect all port keysets and apply the first of them, possibly wildcarded */
static int sparx5_tc_select_protocol_keyset(struct net_device *ndev,
					    struct vcap_rule *vrule,
					    struct vcap_admin *admin,
					    u16 l3_proto,
					    struct sparx5_multiple_rules *multi)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_keyset_list portkeysetlist = {};
	enum vcap_keyfield_set portkeysets[10] = {};
	struct vcap_keyset_list matches = {};
	enum vcap_keyfield_set keysets[10];
	int idx, jdx, err = 0, count = 0;
	struct sparx5_wildcard_rule *mru;
	const struct vcap_set *kinfo;
	struct vcap_control *vctrl;

	vctrl = port->sparx5->vcap_ctrl;

	/* Find the keysets that the rule can use */
	matches.keysets = keysets;
	matches.max = ARRAY_SIZE(keysets);
	if (vcap_rule_find_keysets(vrule, &matches) == 0)
		return -EINVAL;

	/* Find the keysets that the port configuration supports */
	portkeysetlist.max = ARRAY_SIZE(portkeysets);
	portkeysetlist.keysets = portkeysets;
	err = sparx5_vcap_get_port_keyset(ndev,
					  admin, vrule->vcap_chain_id,
					  l3_proto,
					  &portkeysetlist);
	if (err)
		return err;

	/* Find the intersection of the two sets of keyset */
	for (idx = 0; idx < portkeysetlist.cnt; ++idx) {
		kinfo = vcap_keyfieldset(vctrl, admin->vtype,
					 portkeysetlist.keysets[idx]);
		if (!kinfo)
			continue;

		/* Find a port keyset that matches the required keys
		 * If there are multiple keysets then compose a type id mask
		 */
		for (jdx = 0; jdx < matches.cnt; ++jdx) {
			if (portkeysetlist.keysets[idx] != matches.keysets[jdx])
				continue;

			mru = &multi->rule[kinfo->sw_per_item];
			if (!mru->selected) {
				mru->selected = true;
				mru->keyset = portkeysetlist.keysets[idx];
				mru->value = kinfo->type_id;
			}
			mru->value &= kinfo->type_id;
			mru->mask |= kinfo->type_id;
			++count;
		}
	}
	if (count == 0)
		return -EPROTO;

	if (l3_proto == ETH_P_ALL && count < portkeysetlist.cnt)
		return -ENOENT;

	for (idx = 0; idx < SPX5_MAX_RULE_SIZE; ++idx) {
		mru = &multi->rule[idx];
		if (!mru->selected)
			continue;

		/* Align the mask to the combined value */
		mru->mask ^= mru->value;
	}

	/* Set the chosen keyset on the rule and set a wildcarded type if there
	 * are more than one keyset
	 */
	for (idx = 0; idx < SPX5_MAX_RULE_SIZE; ++idx) {
		mru = &multi->rule[idx];
		if (!mru->selected)
			continue;

		vcap_set_rule_set_keyset(vrule, mru->keyset);
		if (count > 1)
			/* Some keysets do not have a type field */
			vcap_rule_mod_key_u32(vrule, VCAP_KF_TYPE,
					      mru->value,
					      ~mru->mask);
		mru->selected = false; /* mark as done */
		break; /* Stop here and add more rules later */
	}
	return err;
}

static int sparx5_tc_add_rule_copy(struct vcap_control *vctrl,
				   struct flow_cls_offload *fco,
				   struct vcap_rule *erule,
				   struct vcap_admin *admin,
				   struct sparx5_wildcard_rule *rule)
{
	enum vcap_key_field keylist[] = {
		VCAP_KF_IF_IGR_PORT_MASK,
		VCAP_KF_IF_IGR_PORT_MASK_SEL,
		VCAP_KF_IF_IGR_PORT_MASK_RNG,
		VCAP_KF_LOOKUP_FIRST_IS,
		VCAP_KF_TYPE,
	};
	struct vcap_rule *vrule;
	int err;

	/* Add an extra rule with a special user and the new keyset */
	erule->user = VCAP_USER_TC_EXTRA;
	vrule = vcap_copy_rule(erule);
	if (IS_ERR(vrule))
		return PTR_ERR(vrule);

	/* Link the new rule to the existing rule with the cookie */
	vrule->cookie = erule->cookie;
	vcap_filter_rule_keys(vrule, keylist, ARRAY_SIZE(keylist), true);
	err = vcap_set_rule_set_keyset(vrule, rule->keyset);
	if (err) {
		pr_err("%s:%d: could not set keyset %s in rule: %u\n",
		       __func__, __LINE__,
		       vcap_keyset_name(vctrl, rule->keyset),
		       vrule->id);
		goto out;
	}

	/* Some keysets do not have a type field, so ignore return value */
	vcap_rule_mod_key_u32(vrule, VCAP_KF_TYPE, rule->value, ~rule->mask);

	err = vcap_set_rule_set_actionset(vrule, erule->actionset);
	if (err)
		goto out;

	err = sparx5_tc_add_rule_counter(admin, vrule);
	if (err)
		goto out;

	err = vcap_val_rule(vrule, ETH_P_ALL);
	if (err) {
		pr_err("%s:%d: could not validate rule: %u\n",
		       __func__, __LINE__, vrule->id);
		vcap_set_tc_exterr(fco, vrule);
		goto out;
	}
	err = vcap_add_rule(vrule);
	if (err) {
		pr_err("%s:%d: could not add rule: %u\n",
		       __func__, __LINE__, vrule->id);
		goto out;
	}
out:
	vcap_free_rule(vrule);
	return err;
}

static int sparx5_tc_add_remaining_rules(struct vcap_control *vctrl,
					 struct flow_cls_offload *fco,
					 struct vcap_rule *erule,
					 struct vcap_admin *admin,
					 struct sparx5_multiple_rules *multi)
{
	int idx, err = 0;

	for (idx = 0; idx < SPX5_MAX_RULE_SIZE; ++idx) {
		if (!multi->rule[idx].selected)
			continue;

		err = sparx5_tc_add_rule_copy(vctrl, fco, erule, admin,
					      &multi->rule[idx]);
		if (err)
			break;
	}
	return err;
}

/* Add the actionset that is the default for the VCAP type */
static int sparx5_tc_set_actionset(struct vcap_admin *admin,
				   struct vcap_rule *vrule)
{
	enum vcap_actionfield_set aset;
	int err = 0;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		aset = VCAP_AFS_CLASSIFICATION;
		break;
	case VCAP_TYPE_IS2:
		aset = VCAP_AFS_BASE_TYPE;
		break;
	case VCAP_TYPE_ES2:
		aset = VCAP_AFS_BASE_TYPE;
		break;
	default:
		return -EINVAL;
	}
	/* Do not overwrite any current actionset */
	if (vrule->actionset == VCAP_AFS_NO_VALUE)
		err = vcap_set_rule_set_actionset(vrule, aset);
	return err;
}

/* Add the VCAP key to match on for a rule target value */
static int sparx5_tc_add_rule_link_target(struct vcap_admin *admin,
					  struct vcap_rule *vrule,
					  int target_cid)
{
	int link_val = target_cid % VCAP_CID_LOOKUP_SIZE;
	int err;

	if (!link_val)
		return 0;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		/* Add NXT_IDX key for chaining rules between IS0 instances */
		err = vcap_rule_add_key_u32(vrule, VCAP_KF_LOOKUP_GEN_IDX_SEL,
					    1, /* enable */
					    ~0);
		if (err)
			return err;
		return vcap_rule_add_key_u32(vrule, VCAP_KF_LOOKUP_GEN_IDX,
					     link_val, /* target */
					     ~0);
	case VCAP_TYPE_IS2:
		/* Add PAG key for chaining rules from IS0 */
		return vcap_rule_add_key_u32(vrule, VCAP_KF_LOOKUP_PAG,
					     link_val, /* target */
					     ~0);
	case VCAP_TYPE_ES2:
		/* Add ISDX key for chaining rules from IS0 */
		return vcap_rule_add_key_u32(vrule, VCAP_KF_ISDX_CLS, link_val,
					     ~0);
	default:
		break;
	}
	return 0;
}

/* Add the VCAP action that adds a target value to a rule */
static int sparx5_tc_add_rule_link(struct vcap_control *vctrl,
				   struct vcap_admin *admin,
				   struct vcap_rule *vrule,
				   int from_cid, int to_cid)
{
	struct vcap_admin *to_admin = vcap_find_admin(vctrl, to_cid);
	int diff, err = 0;

	diff = vcap_chain_offset(vctrl, from_cid, to_cid);
	if (!(to_admin && diff > 0)) {
		pr_err("%s:%d: unsupported chain direction: %d\n",
		       __func__, __LINE__, to_cid);
		return -EINVAL;
	}
	if (admin->vtype == VCAP_TYPE_IS0 &&
	    to_admin->vtype == VCAP_TYPE_IS0) {
		/* Between IS0 instances the G_IDX value is used */
		err = vcap_rule_add_action_u32(vrule, VCAP_AF_NXT_IDX, diff);
		if (err)
			goto out;
		err = vcap_rule_add_action_u32(vrule, VCAP_AF_NXT_IDX_CTRL,
					       1); /* Replace */
		if (err)
			goto out;
	} else if (admin->vtype == VCAP_TYPE_IS0 &&
		   to_admin->vtype == VCAP_TYPE_IS2) {
		/* Between IS0 and IS2 the PAG value is used */
		err = vcap_rule_add_action_u32(vrule, VCAP_AF_PAG_VAL, diff);
		if (err)
			goto out;
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_PAG_OVERRIDE_MASK,
					       0xff);
		if (err)
			goto out;
	} else if (admin->vtype == VCAP_TYPE_IS0 &&
		   to_admin->vtype == VCAP_TYPE_ES2) {
		/* Between IS0 and ES2 the ISDX value is used */
		err = vcap_rule_add_action_u32(vrule, VCAP_AF_ISDX_VAL,
					       diff);
		if (err)
			goto out;
		err = vcap_rule_add_action_bit(vrule,
					       VCAP_AF_ISDX_ADD_REPLACE_SEL,
					       VCAP_BIT_1);
		if (err)
			goto out;
	} else {
		pr_err("%s:%d: unsupported chain destination: %d\n",
		       __func__, __LINE__, to_cid);
		err = -EOPNOTSUPP;
	}
out:
	return err;
}

static int sparx5_tc_flower_replace(struct net_device *ndev,
				    struct flow_cls_offload *fco,
				    struct vcap_admin *admin,
				    bool ingress)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5_multiple_rules multi = {};
	struct flow_action_entry *act;
	struct vcap_control *vctrl;
	struct flow_rule *frule;
	struct vcap_rule *vrule;
	u16 l3_proto;
	int err, idx;

	vctrl = port->sparx5->vcap_ctrl;

	err = sparx5_tc_flower_action_check(vctrl, ndev, fco, ingress);
	if (err)
		return err;

	vrule = vcap_alloc_rule(vctrl, ndev, fco->common.chain_index, VCAP_USER_TC,
				fco->common.prio, 0);
	if (IS_ERR(vrule))
		return PTR_ERR(vrule);

	vrule->cookie = fco->cookie;

	l3_proto = ETH_P_ALL;
	err = sparx5_tc_use_dissectors(fco, admin, vrule, &l3_proto);
	if (err)
		goto out;

	err = sparx5_tc_add_rule_counter(admin, vrule);
	if (err)
		goto out;

	err = sparx5_tc_add_rule_link_target(admin, vrule,
					     fco->common.chain_index);
	if (err)
		goto out;

	frule = flow_cls_offload_flow_rule(fco);
	flow_action_for_each(idx, act, &frule->action) {
		switch (act->id) {
		case FLOW_ACTION_TRAP:
			if (admin->vtype != VCAP_TYPE_IS2 &&
			    admin->vtype != VCAP_TYPE_ES2) {
				NL_SET_ERR_MSG_MOD(fco->common.extack,
						   "Trap action not supported in this VCAP");
				err = -EOPNOTSUPP;
				goto out;
			}
			err = vcap_rule_add_action_bit(vrule,
						       VCAP_AF_CPU_COPY_ENA,
						       VCAP_BIT_1);
			if (err)
				goto out;
			err = vcap_rule_add_action_u32(vrule,
						       VCAP_AF_CPU_QUEUE_NUM, 0);
			if (err)
				goto out;
			if (admin->vtype != VCAP_TYPE_IS2)
				break;
			err = vcap_rule_add_action_u32(vrule,
						       VCAP_AF_MASK_MODE,
				SPX5_PMM_REPLACE_ALL);
			if (err)
				goto out;
			break;
		case FLOW_ACTION_ACCEPT:
			err = sparx5_tc_set_actionset(admin, vrule);
			if (err)
				goto out;
			break;
		case FLOW_ACTION_GOTO:
			err = sparx5_tc_set_actionset(admin, vrule);
			if (err)
				goto out;
			sparx5_tc_add_rule_link(vctrl, admin, vrule,
						fco->common.chain_index,
						act->chain_index);
			break;
		default:
			NL_SET_ERR_MSG_MOD(fco->common.extack,
					   "Unsupported TC action");
			err = -EOPNOTSUPP;
			goto out;
		}
	}

	err = sparx5_tc_select_protocol_keyset(ndev, vrule, admin, l3_proto,
					       &multi);
	if (err) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "No matching port keyset for filter protocol and keys");
		goto out;
	}

	/* provide the l3 protocol to guide the keyset selection */
	err = vcap_val_rule(vrule, l3_proto);
	if (err) {
		vcap_set_tc_exterr(fco, vrule);
		goto out;
	}
	err = vcap_add_rule(vrule);
	if (err)
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Could not add the filter");

	if (l3_proto == ETH_P_ALL)
		err = sparx5_tc_add_remaining_rules(vctrl, fco, vrule, admin,
						    &multi);

out:
	vcap_free_rule(vrule);
	return err;
}

static int sparx5_tc_flower_destroy(struct net_device *ndev,
				    struct flow_cls_offload *fco,
				    struct vcap_admin *admin)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_control *vctrl;
	int err = -ENOENT, rule_id;

	vctrl = port->sparx5->vcap_ctrl;
	while (true) {
		rule_id = vcap_lookup_rule_by_cookie(vctrl, fco->cookie);
		if (rule_id <= 0)
			break;
		err = vcap_del_rule(vctrl, ndev, rule_id);
		if (err) {
			pr_err("%s:%d: could not delete rule %d\n",
			       __func__, __LINE__, rule_id);
			break;
		}
	}
	return err;
}

static int sparx5_tc_flower_stats(struct net_device *ndev,
				  struct flow_cls_offload *fco,
				  struct vcap_admin *admin)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_counter ctr = {};
	struct vcap_control *vctrl;
	ulong lastused = 0;
	int err;

	vctrl = port->sparx5->vcap_ctrl;
	err = vcap_get_rule_count_by_cookie(vctrl, &ctr, fco->cookie);
	if (err)
		return err;
	flow_stats_update(&fco->stats, 0x0, ctr.value, 0, lastused,
			  FLOW_ACTION_HW_STATS_IMMEDIATE);
	return err;
}

int sparx5_tc_flower(struct net_device *ndev, struct flow_cls_offload *fco,
		     bool ingress)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_control *vctrl;
	struct vcap_admin *admin;
	int err = -EINVAL;

	/* Get vcap instance from the chain id */
	vctrl = port->sparx5->vcap_ctrl;
	admin = vcap_find_admin(vctrl, fco->common.chain_index);
	if (!admin) {
		NL_SET_ERR_MSG_MOD(fco->common.extack, "Invalid chain");
		return err;
	}

	switch (fco->command) {
	case FLOW_CLS_REPLACE:
		return sparx5_tc_flower_replace(ndev, fco, admin, ingress);
	case FLOW_CLS_DESTROY:
		return sparx5_tc_flower_destroy(ndev, fco, admin);
	case FLOW_CLS_STATS:
		return sparx5_tc_flower_stats(ndev, fco, admin);
	default:
		return -EOPNOTSUPP;
	}
}
