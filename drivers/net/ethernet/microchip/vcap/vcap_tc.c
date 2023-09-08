// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP TC
 *
 * Copyright (c) 2023 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/flow_offload.h>
#include <net/ipv6.h>
#include <net/tcp.h>

#include "vcap_api_client.h"
#include "vcap_tc.h"

enum vcap_is2_arp_opcode {
	VCAP_IS2_ARP_REQUEST,
	VCAP_IS2_ARP_REPLY,
	VCAP_IS2_RARP_REQUEST,
	VCAP_IS2_RARP_REPLY,
};

enum vcap_arp_opcode {
	VCAP_ARP_OP_RESERVED,
	VCAP_ARP_OP_REQUEST,
	VCAP_ARP_OP_REPLY,
};

int vcap_tc_flower_handler_ethaddr_usage(struct vcap_tc_flower_parse_usage *st)
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
EXPORT_SYMBOL_GPL(vcap_tc_flower_handler_ethaddr_usage);

int vcap_tc_flower_handler_ipv4_usage(struct vcap_tc_flower_parse_usage *st)
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
EXPORT_SYMBOL_GPL(vcap_tc_flower_handler_ipv4_usage);

int vcap_tc_flower_handler_ipv6_usage(struct vcap_tc_flower_parse_usage *st)
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
EXPORT_SYMBOL_GPL(vcap_tc_flower_handler_ipv6_usage);

int vcap_tc_flower_handler_portnum_usage(struct vcap_tc_flower_parse_usage *st)
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
EXPORT_SYMBOL_GPL(vcap_tc_flower_handler_portnum_usage);

int vcap_tc_flower_handler_cvlan_usage(struct vcap_tc_flower_parse_usage *st)
{
	enum vcap_key_field vid_key = VCAP_KF_8021Q_VID0;
	enum vcap_key_field pcp_key = VCAP_KF_8021Q_PCP0;
	struct flow_match_vlan mt;
	u16 tpid;
	int err;

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
EXPORT_SYMBOL_GPL(vcap_tc_flower_handler_cvlan_usage);

int vcap_tc_flower_handler_vlan_usage(struct vcap_tc_flower_parse_usage *st,
				      enum vcap_key_field vid_key,
				      enum vcap_key_field pcp_key)
{
	struct flow_match_vlan mt;
	int err;

	flow_rule_match_vlan(st->frule, &mt);

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

	if (mt.mask->vlan_tpid)
		st->tpid = be16_to_cpu(mt.key->vlan_tpid);

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_VLAN);

	return 0;
out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "vlan parse error");
	return err;
}
EXPORT_SYMBOL_GPL(vcap_tc_flower_handler_vlan_usage);

int vcap_tc_flower_handler_tcp_usage(struct vcap_tc_flower_parse_usage *st)
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
EXPORT_SYMBOL_GPL(vcap_tc_flower_handler_tcp_usage);

int vcap_tc_flower_handler_arp_usage(struct vcap_tc_flower_parse_usage *st)
{
	struct flow_match_arp mt;
	u16 value, mask;
	u32 ipval, ipmsk;
	int err;

	flow_rule_match_arp(st->frule, &mt);

	if (mt.mask->op) {
		mask = 0x3;
		if (st->l3_proto == ETH_P_ARP) {
			value = mt.key->op == VCAP_ARP_OP_REQUEST ?
					VCAP_IS2_ARP_REQUEST :
					VCAP_IS2_ARP_REPLY;
		} else { /* RARP */
			value = mt.key->op == VCAP_ARP_OP_REQUEST ?
					VCAP_IS2_RARP_REQUEST :
					VCAP_IS2_RARP_REPLY;
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
EXPORT_SYMBOL_GPL(vcap_tc_flower_handler_arp_usage);

int vcap_tc_flower_handler_ip_usage(struct vcap_tc_flower_parse_usage *st)
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
EXPORT_SYMBOL_GPL(vcap_tc_flower_handler_ip_usage);
