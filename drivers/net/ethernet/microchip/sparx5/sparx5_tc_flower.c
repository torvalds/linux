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

struct sparx5_tc_flower_parse_usage {
	struct flow_cls_offload *fco;
	struct flow_rule *frule;
	struct vcap_rule *vrule;
	u16 l3_proto;
	u8 l4_proto;
	unsigned int used_keys;
};

/* These protocols have dedicated keysets in IS2 and a TC dissector
 * ETH_P_ARP does not have a TC dissector
 */
static u16 sparx5_tc_known_etypes[] = {
	ETH_P_ALL,
	ETH_P_IP,
	ETH_P_IPV6,
};

static bool sparx5_tc_is_known_etype(u16 etype)
{
	int idx;

	/* For now this only knows about IS2 traffic classification */
	for (idx = 0; idx < ARRAY_SIZE(sparx5_tc_known_etypes); ++idx)
		if (sparx5_tc_known_etypes[idx] == etype)
			return true;

	return false;
}

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
		if (!sparx5_tc_is_known_etype(st->l3_proto)) {
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
sparx5_tc_flower_handler_vlan_usage(struct sparx5_tc_flower_parse_usage *st)
{
	enum vcap_key_field vid_key = VCAP_KF_8021Q_VID_CLS;
	enum vcap_key_field pcp_key = VCAP_KF_8021Q_PCP_CLS;
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

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_VLAN);

	return err;
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
	[FLOW_DISSECTOR_KEY_VLAN] = sparx5_tc_flower_handler_vlan_usage,
	[FLOW_DISSECTOR_KEY_TCP] = sparx5_tc_flower_handler_tcp_usage,
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
					 struct flow_cls_offload *fco,
					 struct vcap_admin *admin)
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

	/* Check that last action is a goto */
	if (last_actent->id != FLOW_ACTION_GOTO) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Last action must be 'goto'");
		return -EINVAL;
	}

	/* Check if the goto chain is in the next lookup */
	if (!vcap_is_next_lookup(vctrl, fco->common.chain_index,
				 last_actent->chain_index)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Invalid goto chain");
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

static int sparx5_tc_flower_replace(struct net_device *ndev,
				    struct flow_cls_offload *fco,
				    struct vcap_admin *admin)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct flow_action_entry *act;
	struct vcap_control *vctrl;
	struct flow_rule *frule;
	struct vcap_rule *vrule;
	u16 l3_proto;
	int err, idx;

	vctrl = port->sparx5->vcap_ctrl;

	err = sparx5_tc_flower_action_check(vctrl, fco, admin);
	if (err)
		return err;

	vrule = vcap_alloc_rule(vctrl, ndev, fco->common.chain_index, VCAP_USER_TC,
				fco->common.prio, 0);
	if (IS_ERR(vrule))
		return PTR_ERR(vrule);

	vrule->cookie = fco->cookie;
	sparx5_tc_use_dissectors(fco, admin, vrule, &l3_proto);
	frule = flow_cls_offload_flow_rule(fco);
	flow_action_for_each(idx, act, &frule->action) {
		switch (act->id) {
		case FLOW_ACTION_TRAP:
			err = vcap_rule_add_action_bit(vrule,
						       VCAP_AF_CPU_COPY_ENA,
						       VCAP_BIT_1);
			if (err)
				goto out;
			err = vcap_rule_add_action_u32(vrule,
						       VCAP_AF_CPU_QUEUE_NUM, 0);
			if (err)
				goto out;
			err = vcap_rule_add_action_u32(vrule, VCAP_AF_MASK_MODE,
						       SPX5_PMM_REPLACE_ALL);
			if (err)
				goto out;
			/* For now the actionset is hardcoded */
			err = vcap_set_rule_set_actionset(vrule,
							  VCAP_AFS_BASE_TYPE);
			if (err)
				goto out;
			break;
		case FLOW_ACTION_ACCEPT:
			/* For now the actionset is hardcoded */
			err = vcap_set_rule_set_actionset(vrule,
							  VCAP_AFS_BASE_TYPE);
			if (err)
				goto out;
			break;
		case FLOW_ACTION_GOTO:
			/* Links between VCAPs will be added later */
			break;
		default:
			NL_SET_ERR_MSG_MOD(fco->common.extack,
					   "Unsupported TC action");
			err = -EOPNOTSUPP;
			goto out;
		}
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
		return sparx5_tc_flower_replace(ndev, fco, admin);
	case FLOW_CLS_DESTROY:
		return sparx5_tc_flower_destroy(ndev, fco, admin);
	default:
		return -EOPNOTSUPP;
	}
}
