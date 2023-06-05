// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 * Copyright 2020-2022 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <net/pkt_cls.h>
#include <net/vxlan.h>
#include <net/geneve.h>
#include "tc.h"
#include "tc_bindings.h"
#include "mae.h"
#include "ef100_rep.h"
#include "efx.h"

static enum efx_encap_type efx_tc_indr_netdev_type(struct net_device *net_dev)
{
	if (netif_is_vxlan(net_dev))
		return EFX_ENCAP_TYPE_VXLAN;
	if (netif_is_geneve(net_dev))
		return EFX_ENCAP_TYPE_GENEVE;

	return EFX_ENCAP_TYPE_NONE;
}

#define EFX_EFV_PF	NULL
/* Look up the representor information (efv) for a device.
 * May return NULL for the PF (us), or an error pointer for a device that
 * isn't supported as a TC offload endpoint
 */
static struct efx_rep *efx_tc_flower_lookup_efv(struct efx_nic *efx,
						struct net_device *dev)
{
	struct efx_rep *efv;

	if (!dev)
		return ERR_PTR(-EOPNOTSUPP);
	/* Is it us (the PF)? */
	if (dev == efx->net_dev)
		return EFX_EFV_PF;
	/* Is it an efx vfrep at all? */
	if (dev->netdev_ops != &efx_ef100_rep_netdev_ops)
		return ERR_PTR(-EOPNOTSUPP);
	/* Is it ours?  We don't support TC rules that include another
	 * EF100's netdevices (not even on another port of the same NIC).
	 */
	efv = netdev_priv(dev);
	if (efv->parent != efx)
		return ERR_PTR(-EOPNOTSUPP);
	return efv;
}

/* Convert a driver-internal vport ID into an internal device (PF or VF) */
static s64 efx_tc_flower_internal_mport(struct efx_nic *efx, struct efx_rep *efv)
{
	u32 mport;

	if (IS_ERR(efv))
		return PTR_ERR(efv);
	if (!efv) /* device is PF (us) */
		efx_mae_mport_uplink(efx, &mport);
	else /* device is repr */
		efx_mae_mport_mport(efx, efv->mport, &mport);
	return mport;
}

/* Convert a driver-internal vport ID into an external device (wire or VF) */
static s64 efx_tc_flower_external_mport(struct efx_nic *efx, struct efx_rep *efv)
{
	u32 mport;

	if (IS_ERR(efv))
		return PTR_ERR(efv);
	if (!efv) /* device is PF (us) */
		efx_mae_mport_wire(efx, &mport);
	else /* device is repr */
		efx_mae_mport_mport(efx, efv->mport, &mport);
	return mport;
}

static const struct rhashtable_params efx_tc_encap_match_ht_params = {
	.key_len	= offsetof(struct efx_tc_encap_match, linkage),
	.key_offset	= 0,
	.head_offset	= offsetof(struct efx_tc_encap_match, linkage),
};

static const struct rhashtable_params efx_tc_match_action_ht_params = {
	.key_len	= sizeof(unsigned long),
	.key_offset	= offsetof(struct efx_tc_flow_rule, cookie),
	.head_offset	= offsetof(struct efx_tc_flow_rule, linkage),
};

static void efx_tc_free_action_set(struct efx_nic *efx,
				   struct efx_tc_action_set *act, bool in_hw)
{
	/* Failure paths calling this on the 'cursor' action set in_hw=false,
	 * because if the alloc had succeeded we'd've put it in acts.list and
	 * not still have it in act.
	 */
	if (in_hw) {
		efx_mae_free_action_set(efx, act->fw_id);
		/* in_hw is true iff we are on an acts.list; make sure to
		 * remove ourselves from that list before we are freed.
		 */
		list_del(&act->list);
	}
	if (act->count)
		efx_tc_flower_put_counter_index(efx, act->count);
	kfree(act);
}

static void efx_tc_free_action_set_list(struct efx_nic *efx,
					struct efx_tc_action_set_list *acts,
					bool in_hw)
{
	struct efx_tc_action_set *act, *next;

	/* Failure paths set in_hw=false, because usually the acts didn't get
	 * to efx_mae_alloc_action_set_list(); if they did, the failure tree
	 * has a separate efx_mae_free_action_set_list() before calling us.
	 */
	if (in_hw)
		efx_mae_free_action_set_list(efx, acts);
	/* Any act that's on the list will be in_hw even if the list isn't */
	list_for_each_entry_safe(act, next, &acts->list, list)
		efx_tc_free_action_set(efx, act, true);
	/* Don't kfree, as acts is embedded inside a struct efx_tc_flow_rule */
}

static void efx_tc_flow_free(void *ptr, void *arg)
{
	struct efx_tc_flow_rule *rule = ptr;
	struct efx_nic *efx = arg;

	netif_err(efx, drv, efx->net_dev,
		  "tc rule %lx still present at teardown, removing\n",
		  rule->cookie);

	efx_mae_delete_rule(efx, rule->fw_id);

	/* Release entries in subsidiary tables */
	efx_tc_free_action_set_list(efx, &rule->acts, true);

	kfree(rule);
}

/* Boilerplate for the simple 'copy a field' cases */
#define _MAP_KEY_AND_MASK(_name, _type, _tcget, _tcfield, _field)	\
if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_##_name)) {		\
	struct flow_match_##_type fm;					\
									\
	flow_rule_match_##_tcget(rule, &fm);				\
	match->value._field = fm.key->_tcfield;				\
	match->mask._field = fm.mask->_tcfield;				\
}
#define MAP_KEY_AND_MASK(_name, _type, _tcfield, _field)	\
	_MAP_KEY_AND_MASK(_name, _type, _type, _tcfield, _field)
#define MAP_ENC_KEY_AND_MASK(_name, _type, _tcget, _tcfield, _field)	\
	_MAP_KEY_AND_MASK(ENC_##_name, _type, _tcget, _tcfield, _field)

static int efx_tc_flower_parse_match(struct efx_nic *efx,
				     struct flow_rule *rule,
				     struct efx_tc_match *match,
				     struct netlink_ext_ack *extack)
{
	struct flow_dissector *dissector = rule->match.dissector;
	unsigned char ipv = 0;

	/* Owing to internal TC infelicities, the IPV6_ADDRS key might be set
	 * even on IPv4 filters; so rather than relying on dissector->used_keys
	 * we check the addr_type in the CONTROL key.  If we don't find it (or
	 * it's masked, which should never happen), we treat both IPV4_ADDRS
	 * and IPV6_ADDRS as absent.
	 */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control fm;

		flow_rule_match_control(rule, &fm);
		if (IS_ALL_ONES(fm.mask->addr_type))
			switch (fm.key->addr_type) {
			case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
				ipv = 4;
				break;
			case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
				ipv = 6;
				break;
			default:
				break;
			}

		if (fm.mask->flags & FLOW_DIS_IS_FRAGMENT) {
			match->value.ip_frag = fm.key->flags & FLOW_DIS_IS_FRAGMENT;
			match->mask.ip_frag = true;
		}
		if (fm.mask->flags & FLOW_DIS_FIRST_FRAG) {
			match->value.ip_firstfrag = fm.key->flags & FLOW_DIS_FIRST_FRAG;
			match->mask.ip_firstfrag = true;
		}
		if (fm.mask->flags & ~(FLOW_DIS_IS_FRAGMENT | FLOW_DIS_FIRST_FRAG)) {
			NL_SET_ERR_MSG_FMT_MOD(extack, "Unsupported match on control.flags %#x",
					       fm.mask->flags);
			return -EOPNOTSUPP;
		}
	}
	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_CVLAN) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_KEYID) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_TCP) |
	      BIT(FLOW_DISSECTOR_KEY_IP))) {
		NL_SET_ERR_MSG_FMT_MOD(extack, "Unsupported flower keys %#x",
				       dissector->used_keys);
		return -EOPNOTSUPP;
	}

	MAP_KEY_AND_MASK(BASIC, basic, n_proto, eth_proto);
	/* Make sure we're IP if any L3/L4 keys used. */
	if (!IS_ALL_ONES(match->mask.eth_proto) ||
	    !(match->value.eth_proto == htons(ETH_P_IP) ||
	      match->value.eth_proto == htons(ETH_P_IPV6)))
		if (dissector->used_keys &
		    (BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
		     BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
		     BIT(FLOW_DISSECTOR_KEY_PORTS) |
		     BIT(FLOW_DISSECTOR_KEY_IP) |
		     BIT(FLOW_DISSECTOR_KEY_TCP))) {
			NL_SET_ERR_MSG_FMT_MOD(extack, "L3/L4 flower keys %#x require protocol ipv[46]",
					       dissector->used_keys);
			return -EINVAL;
		}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan fm;

		flow_rule_match_vlan(rule, &fm);
		if (fm.mask->vlan_id || fm.mask->vlan_priority || fm.mask->vlan_tpid) {
			match->value.vlan_proto[0] = fm.key->vlan_tpid;
			match->mask.vlan_proto[0] = fm.mask->vlan_tpid;
			match->value.vlan_tci[0] = cpu_to_be16(fm.key->vlan_priority << 13 |
							       fm.key->vlan_id);
			match->mask.vlan_tci[0] = cpu_to_be16(fm.mask->vlan_priority << 13 |
							      fm.mask->vlan_id);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CVLAN)) {
		struct flow_match_vlan fm;

		flow_rule_match_cvlan(rule, &fm);
		if (fm.mask->vlan_id || fm.mask->vlan_priority || fm.mask->vlan_tpid) {
			match->value.vlan_proto[1] = fm.key->vlan_tpid;
			match->mask.vlan_proto[1] = fm.mask->vlan_tpid;
			match->value.vlan_tci[1] = cpu_to_be16(fm.key->vlan_priority << 13 |
							       fm.key->vlan_id);
			match->mask.vlan_tci[1] = cpu_to_be16(fm.mask->vlan_priority << 13 |
							      fm.mask->vlan_id);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs fm;

		flow_rule_match_eth_addrs(rule, &fm);
		ether_addr_copy(match->value.eth_saddr, fm.key->src);
		ether_addr_copy(match->value.eth_daddr, fm.key->dst);
		ether_addr_copy(match->mask.eth_saddr, fm.mask->src);
		ether_addr_copy(match->mask.eth_daddr, fm.mask->dst);
	}

	MAP_KEY_AND_MASK(BASIC, basic, ip_proto, ip_proto);
	/* Make sure we're TCP/UDP if any L4 keys used. */
	if ((match->value.ip_proto != IPPROTO_UDP &&
	     match->value.ip_proto != IPPROTO_TCP) || !IS_ALL_ONES(match->mask.ip_proto))
		if (dissector->used_keys &
		    (BIT(FLOW_DISSECTOR_KEY_PORTS) |
		     BIT(FLOW_DISSECTOR_KEY_TCP))) {
			NL_SET_ERR_MSG_FMT_MOD(extack, "L4 flower keys %#x require ipproto udp or tcp",
					       dissector->used_keys);
			return -EINVAL;
		}
	MAP_KEY_AND_MASK(IP, ip, tos, ip_tos);
	MAP_KEY_AND_MASK(IP, ip, ttl, ip_ttl);
	if (ipv == 4) {
		MAP_KEY_AND_MASK(IPV4_ADDRS, ipv4_addrs, src, src_ip);
		MAP_KEY_AND_MASK(IPV4_ADDRS, ipv4_addrs, dst, dst_ip);
	}
#ifdef CONFIG_IPV6
	else if (ipv == 6) {
		MAP_KEY_AND_MASK(IPV6_ADDRS, ipv6_addrs, src, src_ip6);
		MAP_KEY_AND_MASK(IPV6_ADDRS, ipv6_addrs, dst, dst_ip6);
	}
#endif
	MAP_KEY_AND_MASK(PORTS, ports, src, l4_sport);
	MAP_KEY_AND_MASK(PORTS, ports, dst, l4_dport);
	MAP_KEY_AND_MASK(TCP, tcp, flags, tcp_flags);
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_CONTROL)) {
		struct flow_match_control fm;

		flow_rule_match_enc_control(rule, &fm);
		if (fm.mask->flags) {
			NL_SET_ERR_MSG_FMT_MOD(extack, "Unsupported match on enc_control.flags %#x",
					       fm.mask->flags);
			return -EOPNOTSUPP;
		}
		if (!IS_ALL_ONES(fm.mask->addr_type)) {
			NL_SET_ERR_MSG_FMT_MOD(extack, "Unsupported enc addr_type mask %u (key %u)",
					       fm.mask->addr_type,
					       fm.key->addr_type);
			return -EOPNOTSUPP;
		}
		switch (fm.key->addr_type) {
		case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
			MAP_ENC_KEY_AND_MASK(IPV4_ADDRS, ipv4_addrs, enc_ipv4_addrs,
					     src, enc_src_ip);
			MAP_ENC_KEY_AND_MASK(IPV4_ADDRS, ipv4_addrs, enc_ipv4_addrs,
					     dst, enc_dst_ip);
			break;
#ifdef CONFIG_IPV6
		case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
			MAP_ENC_KEY_AND_MASK(IPV6_ADDRS, ipv6_addrs, enc_ipv6_addrs,
					     src, enc_src_ip6);
			MAP_ENC_KEY_AND_MASK(IPV6_ADDRS, ipv6_addrs, enc_ipv6_addrs,
					     dst, enc_dst_ip6);
			break;
#endif
		default:
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Unsupported enc addr_type %u (supported are IPv4, IPv6)",
					       fm.key->addr_type);
			return -EOPNOTSUPP;
		}
		MAP_ENC_KEY_AND_MASK(IP, ip, enc_ip, tos, enc_ip_tos);
		MAP_ENC_KEY_AND_MASK(IP, ip, enc_ip, ttl, enc_ip_ttl);
		MAP_ENC_KEY_AND_MASK(PORTS, ports, enc_ports, src, enc_sport);
		MAP_ENC_KEY_AND_MASK(PORTS, ports, enc_ports, dst, enc_dport);
		MAP_ENC_KEY_AND_MASK(KEYID, enc_keyid, enc_keyid, keyid, enc_keyid);
	} else if (dissector->used_keys &
		   (BIT(FLOW_DISSECTOR_KEY_ENC_KEYID) |
		    BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) |
		    BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) |
		    BIT(FLOW_DISSECTOR_KEY_ENC_IP) |
		    BIT(FLOW_DISSECTOR_KEY_ENC_PORTS))) {
		NL_SET_ERR_MSG_FMT_MOD(extack, "Flower enc keys require enc_control (keys: %#x)",
				       dissector->used_keys);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int efx_tc_flower_record_encap_match(struct efx_nic *efx,
					    struct efx_tc_match *match,
					    enum efx_encap_type type,
					    struct netlink_ext_ack *extack)
{
	struct efx_tc_encap_match *encap, *old;
	bool ipv6 = false;
	int rc;

	/* We require that the socket-defining fields (IP addrs and UDP dest
	 * port) are present and exact-match.  Other fields are currently not
	 * allowed.  This meets what OVS will ask for, and means that we don't
	 * need to handle difficult checks for overlapping matches as could
	 * come up if we allowed masks or varying sets of match fields.
	 */
	if (match->mask.enc_dst_ip | match->mask.enc_src_ip) {
		if (!IS_ALL_ONES(match->mask.enc_dst_ip)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Egress encap match is not exact on dst IP address");
			return -EOPNOTSUPP;
		}
		if (!IS_ALL_ONES(match->mask.enc_src_ip)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Egress encap match is not exact on src IP address");
			return -EOPNOTSUPP;
		}
#ifdef CONFIG_IPV6
		if (!ipv6_addr_any(&match->mask.enc_dst_ip6) ||
		    !ipv6_addr_any(&match->mask.enc_src_ip6)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Egress encap match on both IPv4 and IPv6, don't understand");
			return -EOPNOTSUPP;
		}
	} else {
		ipv6 = true;
		if (!efx_ipv6_addr_all_ones(&match->mask.enc_dst_ip6)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Egress encap match is not exact on dst IP address");
			return -EOPNOTSUPP;
		}
		if (!efx_ipv6_addr_all_ones(&match->mask.enc_src_ip6)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Egress encap match is not exact on src IP address");
			return -EOPNOTSUPP;
		}
#endif
	}
	if (!IS_ALL_ONES(match->mask.enc_dport)) {
		NL_SET_ERR_MSG_MOD(extack, "Egress encap match is not exact on dst UDP port");
		return -EOPNOTSUPP;
	}
	if (match->mask.enc_sport) {
		NL_SET_ERR_MSG_MOD(extack, "Egress encap match on src UDP port not supported");
		return -EOPNOTSUPP;
	}
	if (match->mask.enc_ip_tos) {
		NL_SET_ERR_MSG_MOD(extack, "Egress encap match on IP ToS not supported");
		return -EOPNOTSUPP;
	}
	if (match->mask.enc_ip_ttl) {
		NL_SET_ERR_MSG_MOD(extack, "Egress encap match on IP TTL not supported");
		return -EOPNOTSUPP;
	}

	rc = efx_mae_check_encap_match_caps(efx, ipv6, extack);
	if (rc) {
		NL_SET_ERR_MSG_FMT_MOD(extack, "MAE hw reports no support for IPv%d encap matches",
				       ipv6 ? 6 : 4);
		return -EOPNOTSUPP;
	}

	encap = kzalloc(sizeof(*encap), GFP_USER);
	if (!encap)
		return -ENOMEM;
	encap->src_ip = match->value.enc_src_ip;
	encap->dst_ip = match->value.enc_dst_ip;
#ifdef CONFIG_IPV6
	encap->src_ip6 = match->value.enc_src_ip6;
	encap->dst_ip6 = match->value.enc_dst_ip6;
#endif
	encap->udp_dport = match->value.enc_dport;
	encap->tun_type = type;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->encap_match_ht,
						&encap->linkage,
						efx_tc_encap_match_ht_params);
	if (old) {
		/* don't need our new entry */
		kfree(encap);
		if (old->tun_type != type) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Egress encap match with conflicting tun_type %u != %u",
					       old->tun_type, type);
			return -EEXIST;
		}
		if (!refcount_inc_not_zero(&old->ref))
			return -EAGAIN;
		/* existing entry found */
		encap = old;
	} else {
		rc = efx_mae_register_encap_match(efx, encap);
		if (rc) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to record egress encap match in HW");
			goto fail;
		}
		refcount_set(&encap->ref, 1);
	}
	match->encap = encap;
	return 0;
fail:
	rhashtable_remove_fast(&efx->tc->encap_match_ht, &encap->linkage,
			       efx_tc_encap_match_ht_params);
	kfree(encap);
	return rc;
}

static void efx_tc_flower_release_encap_match(struct efx_nic *efx,
					      struct efx_tc_encap_match *encap)
{
	int rc;

	if (!refcount_dec_and_test(&encap->ref))
		return; /* still in use */

	rc = efx_mae_unregister_encap_match(efx, encap);
	if (rc)
		/* Display message but carry on and remove entry from our
		 * SW tables, because there's not much we can do about it.
		 */
		netif_err(efx, drv, efx->net_dev,
			  "Failed to release encap match %#x, rc %d\n",
			  encap->fw_id, rc);
	rhashtable_remove_fast(&efx->tc->encap_match_ht, &encap->linkage,
			       efx_tc_encap_match_ht_params);
	kfree(encap);
}

static void efx_tc_delete_rule(struct efx_nic *efx, struct efx_tc_flow_rule *rule)
{
	efx_mae_delete_rule(efx, rule->fw_id);

	/* Release entries in subsidiary tables */
	efx_tc_free_action_set_list(efx, &rule->acts, true);
	if (rule->match.encap)
		efx_tc_flower_release_encap_match(efx, rule->match.encap);
	rule->fw_id = MC_CMD_MAE_ACTION_RULE_INSERT_OUT_ACTION_RULE_ID_NULL;
}

static const char *efx_tc_encap_type_name(enum efx_encap_type typ)
{
	switch (typ) {
	case EFX_ENCAP_TYPE_NONE:
		return "none";
	case EFX_ENCAP_TYPE_VXLAN:
		return "vxlan";
	case EFX_ENCAP_TYPE_GENEVE:
		return "geneve";
	default:
		pr_warn_once("Unknown efx_encap_type %d encountered\n", typ);
		return "unknown";
	}
}

/* For details of action order constraints refer to SF-123102-TC-1§12.6.1 */
enum efx_tc_action_order {
	EFX_TC_AO_DECAP,
	EFX_TC_AO_VLAN_POP,
	EFX_TC_AO_VLAN_PUSH,
	EFX_TC_AO_COUNT,
	EFX_TC_AO_DELIVER
};
/* Determine whether we can add @new action without violating order */
static bool efx_tc_flower_action_order_ok(const struct efx_tc_action_set *act,
					  enum efx_tc_action_order new)
{
	switch (new) {
	case EFX_TC_AO_DECAP:
		if (act->decap)
			return false;
		fallthrough;
	case EFX_TC_AO_VLAN_POP:
		if (act->vlan_pop >= 2)
			return false;
		/* If we've already pushed a VLAN, we can't then pop it;
		 * the hardware would instead try to pop an existing VLAN
		 * before pushing the new one.
		 */
		if (act->vlan_push)
			return false;
		fallthrough;
	case EFX_TC_AO_VLAN_PUSH:
		if (act->vlan_push >= 2)
			return false;
		fallthrough;
	case EFX_TC_AO_COUNT:
		if (act->count)
			return false;
		fallthrough;
	case EFX_TC_AO_DELIVER:
		return !act->deliver;
	default:
		/* Bad caller.  Whatever they wanted to do, say they can't. */
		WARN_ON_ONCE(1);
		return false;
	}
}

static int efx_tc_flower_replace_foreign(struct efx_nic *efx,
					 struct net_device *net_dev,
					 struct flow_cls_offload *tc)
{
	struct flow_rule *fr = flow_cls_offload_flow_rule(tc);
	struct netlink_ext_ack *extack = tc->common.extack;
	struct efx_tc_flow_rule *rule = NULL, *old = NULL;
	struct efx_tc_action_set *act = NULL;
	bool found = false, uplinked = false;
	const struct flow_action_entry *fa;
	struct efx_tc_match match;
	struct efx_rep *to_efv;
	s64 rc;
	int i;

	/* Parse match */
	memset(&match, 0, sizeof(match));
	rc = efx_tc_flower_parse_match(efx, fr, &match, NULL);
	if (rc)
		return rc;
	/* The rule as given to us doesn't specify a source netdevice.
	 * But, determining whether packets from a VF should match it is
	 * complicated, so leave those to the software slowpath: qualify
	 * the filter with source m-port == wire.
	 */
	rc = efx_tc_flower_external_mport(efx, EFX_EFV_PF);
	if (rc < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to identify ingress m-port for foreign filter");
		return rc;
	}
	match.value.ingress_port = rc;
	match.mask.ingress_port = ~0;

	if (tc->common.chain_index) {
		NL_SET_ERR_MSG_MOD(extack, "No support for nonzero chain_index");
		return -EOPNOTSUPP;
	}
	match.mask.recirc_id = 0xff;

	flow_action_for_each(i, fa, &fr->action) {
		switch (fa->id) {
		case FLOW_ACTION_REDIRECT:
		case FLOW_ACTION_MIRRED: /* mirred means mirror here */
			to_efv = efx_tc_flower_lookup_efv(efx, fa->dev);
			if (IS_ERR(to_efv))
				continue;
			found = true;
			break;
		default:
			break;
		}
	}
	if (!found) { /* We don't care. */
		netif_dbg(efx, drv, efx->net_dev,
			  "Ignoring foreign filter that doesn't egdev us\n");
		return -EOPNOTSUPP;
	}

	rc = efx_mae_match_check_caps(efx, &match.mask, NULL);
	if (rc)
		return rc;

	if (efx_tc_match_is_encap(&match.mask)) {
		enum efx_encap_type type;

		type = efx_tc_indr_netdev_type(net_dev);
		if (type == EFX_ENCAP_TYPE_NONE) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Egress encap match on unsupported tunnel device");
			return -EOPNOTSUPP;
		}

		rc = efx_mae_check_encap_type_supported(efx, type);
		if (rc) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Firmware reports no support for %s encap match",
					       efx_tc_encap_type_name(type));
			return rc;
		}

		rc = efx_tc_flower_record_encap_match(efx, &match, type,
						      extack);
		if (rc)
			return rc;
	} else {
		/* This is not a tunnel decap rule, ignore it */
		netif_dbg(efx, drv, efx->net_dev,
			  "Ignoring foreign filter without encap match\n");
		return -EOPNOTSUPP;
	}

	rule = kzalloc(sizeof(*rule), GFP_USER);
	if (!rule) {
		rc = -ENOMEM;
		goto out_free;
	}
	INIT_LIST_HEAD(&rule->acts.list);
	rule->cookie = tc->cookie;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->match_action_ht,
						&rule->linkage,
						efx_tc_match_action_ht_params);
	if (old) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Ignoring already-offloaded rule (cookie %lx)\n",
			  tc->cookie);
		rc = -EEXIST;
		goto out_free;
	}

	act = kzalloc(sizeof(*act), GFP_USER);
	if (!act) {
		rc = -ENOMEM;
		goto release;
	}

	/* Parse actions.  For foreign rules we only support decap & redirect.
	 * See corresponding code in efx_tc_flower_replace() for theory of
	 * operation & how 'act' cursor is used.
	 */
	flow_action_for_each(i, fa, &fr->action) {
		struct efx_tc_action_set save;

		switch (fa->id) {
		case FLOW_ACTION_REDIRECT:
		case FLOW_ACTION_MIRRED:
			/* See corresponding code in efx_tc_flower_replace() for
			 * long explanations of what's going on here.
			 */
			save = *act;
			if (fa->hw_stats) {
				struct efx_tc_counter_index *ctr;

				if (!(fa->hw_stats & FLOW_ACTION_HW_STATS_DELAYED)) {
					NL_SET_ERR_MSG_FMT_MOD(extack,
							       "hw_stats_type %u not supported (only 'delayed')",
							       fa->hw_stats);
					rc = -EOPNOTSUPP;
					goto release;
				}
				if (!efx_tc_flower_action_order_ok(act, EFX_TC_AO_COUNT)) {
					rc = -EOPNOTSUPP;
					goto release;
				}

				ctr = efx_tc_flower_get_counter_index(efx,
								      tc->cookie,
								      EFX_TC_COUNTER_TYPE_AR);
				if (IS_ERR(ctr)) {
					rc = PTR_ERR(ctr);
					NL_SET_ERR_MSG_MOD(extack, "Failed to obtain a counter");
					goto release;
				}
				act->count = ctr;
			}

			if (!efx_tc_flower_action_order_ok(act, EFX_TC_AO_DELIVER)) {
				/* can't happen */
				rc = -EOPNOTSUPP;
				NL_SET_ERR_MSG_MOD(extack,
						   "Deliver action violates action order (can't happen)");
				goto release;
			}
			to_efv = efx_tc_flower_lookup_efv(efx, fa->dev);
			/* PF implies egdev is us, in which case we really
			 * want to deliver to the uplink (because this is an
			 * ingress filter).  If we don't recognise the egdev
			 * at all, then we'd better trap so SW can handle it.
			 */
			if (IS_ERR(to_efv))
				to_efv = EFX_EFV_PF;
			if (to_efv == EFX_EFV_PF) {
				if (uplinked)
					break;
				uplinked = true;
			}
			rc = efx_tc_flower_internal_mport(efx, to_efv);
			if (rc < 0) {
				NL_SET_ERR_MSG_MOD(extack, "Failed to identify egress m-port");
				goto release;
			}
			act->dest_mport = rc;
			act->deliver = 1;
			rc = efx_mae_alloc_action_set(efx, act);
			if (rc) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Failed to write action set to hw (mirred)");
				goto release;
			}
			list_add_tail(&act->list, &rule->acts.list);
			act = NULL;
			if (fa->id == FLOW_ACTION_REDIRECT)
				break; /* end of the line */
			/* Mirror, so continue on with saved act */
			act = kzalloc(sizeof(*act), GFP_USER);
			if (!act) {
				rc = -ENOMEM;
				goto release;
			}
			*act = save;
			break;
		case FLOW_ACTION_TUNNEL_DECAP:
			if (!efx_tc_flower_action_order_ok(act, EFX_TC_AO_DECAP)) {
				rc = -EINVAL;
				NL_SET_ERR_MSG_MOD(extack, "Decap action violates action order");
				goto release;
			}
			act->decap = 1;
			/* If we previously delivered/trapped to uplink, now
			 * that we've decapped we'll want another copy if we
			 * try to deliver/trap to uplink again.
			 */
			uplinked = false;
			break;
		default:
			NL_SET_ERR_MSG_FMT_MOD(extack, "Unhandled action %u",
					       fa->id);
			rc = -EOPNOTSUPP;
			goto release;
		}
	}

	if (act) {
		if (!uplinked) {
			/* Not shot/redirected, so deliver to default dest (which is
			 * the uplink, as this is an ingress filter)
			 */
			efx_mae_mport_uplink(efx, &act->dest_mport);
			act->deliver = 1;
		}
		rc = efx_mae_alloc_action_set(efx, act);
		if (rc) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to write action set to hw (deliver)");
			goto release;
		}
		list_add_tail(&act->list, &rule->acts.list);
		act = NULL; /* Prevent double-free in error path */
	}

	rule->match = match;

	netif_dbg(efx, drv, efx->net_dev,
		  "Successfully parsed foreign filter (cookie %lx)\n",
		  tc->cookie);

	rc = efx_mae_alloc_action_set_list(efx, &rule->acts);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to write action set list to hw");
		goto release;
	}
	rc = efx_mae_insert_rule(efx, &rule->match, EFX_TC_PRIO_TC,
				 rule->acts.fw_id, &rule->fw_id);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to insert rule in hw");
		goto release_acts;
	}
	return 0;

release_acts:
	efx_mae_free_action_set_list(efx, &rule->acts);
release:
	/* We failed to insert the rule, so free up any entries we created in
	 * subsidiary tables.
	 */
	if (act)
		efx_tc_free_action_set(efx, act, false);
	if (rule) {
		rhashtable_remove_fast(&efx->tc->match_action_ht,
				       &rule->linkage,
				       efx_tc_match_action_ht_params);
		efx_tc_free_action_set_list(efx, &rule->acts, false);
	}
out_free:
	kfree(rule);
	if (match.encap)
		efx_tc_flower_release_encap_match(efx, match.encap);
	return rc;
}

static int efx_tc_flower_replace(struct efx_nic *efx,
				 struct net_device *net_dev,
				 struct flow_cls_offload *tc,
				 struct efx_rep *efv)
{
	struct flow_rule *fr = flow_cls_offload_flow_rule(tc);
	struct netlink_ext_ack *extack = tc->common.extack;
	struct efx_tc_flow_rule *rule = NULL, *old;
	struct efx_tc_action_set *act = NULL;
	const struct flow_action_entry *fa;
	struct efx_rep *from_efv, *to_efv;
	struct efx_tc_match match;
	s64 rc;
	int i;

	if (!tc_can_offload_extack(efx->net_dev, extack))
		return -EOPNOTSUPP;
	if (WARN_ON(!efx->tc))
		return -ENETDOWN;
	if (WARN_ON(!efx->tc->up))
		return -ENETDOWN;

	from_efv = efx_tc_flower_lookup_efv(efx, net_dev);
	if (IS_ERR(from_efv)) {
		/* Not from our PF or representors, so probably a tunnel dev */
		return efx_tc_flower_replace_foreign(efx, net_dev, tc);
	}

	if (efv != from_efv) {
		/* can't happen */
		NL_SET_ERR_MSG_FMT_MOD(extack, "for %s efv is %snull but from_efv is %snull (can't happen)",
				       netdev_name(net_dev), efv ? "non-" : "",
				       from_efv ? "non-" : "");
		return -EINVAL;
	}

	/* Parse match */
	memset(&match, 0, sizeof(match));
	rc = efx_tc_flower_external_mport(efx, from_efv);
	if (rc < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to identify ingress m-port");
		return rc;
	}
	match.value.ingress_port = rc;
	match.mask.ingress_port = ~0;
	rc = efx_tc_flower_parse_match(efx, fr, &match, extack);
	if (rc)
		return rc;
	if (efx_tc_match_is_encap(&match.mask)) {
		NL_SET_ERR_MSG_MOD(extack, "Ingress enc_key matches not supported");
		return -EOPNOTSUPP;
	}

	if (tc->common.chain_index) {
		NL_SET_ERR_MSG_MOD(extack, "No support for nonzero chain_index");
		return -EOPNOTSUPP;
	}
	match.mask.recirc_id = 0xff;

	rc = efx_mae_match_check_caps(efx, &match.mask, extack);
	if (rc)
		return rc;

	rule = kzalloc(sizeof(*rule), GFP_USER);
	if (!rule)
		return -ENOMEM;
	INIT_LIST_HEAD(&rule->acts.list);
	rule->cookie = tc->cookie;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->match_action_ht,
						&rule->linkage,
						efx_tc_match_action_ht_params);
	if (old) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Already offloaded rule (cookie %lx)\n", tc->cookie);
		NL_SET_ERR_MSG_MOD(extack, "Rule already offloaded");
		kfree(rule);
		return -EEXIST;
	}

	/* Parse actions */
	act = kzalloc(sizeof(*act), GFP_USER);
	if (!act) {
		rc = -ENOMEM;
		goto release;
	}

	/**
	 * DOC: TC action translation
	 *
	 * Actions in TC are sequential and cumulative, with delivery actions
	 * potentially anywhere in the order.  The EF100 MAE, however, takes
	 * an 'action set list' consisting of 'action sets', each of which is
	 * applied to the _original_ packet, and consists of a set of optional
	 * actions in a fixed order with delivery at the end.
	 * To translate between these two models, we maintain a 'cursor', @act,
	 * which describes the cumulative effect of all the packet-mutating
	 * actions encountered so far; on handling a delivery (mirred or drop)
	 * action, once the action-set has been inserted into hardware, we
	 * append @act to the action-set list (@rule->acts); if this is a pipe
	 * action (mirred mirror) we then allocate a new @act with a copy of
	 * the cursor state _before_ the delivery action, otherwise we set @act
	 * to %NULL.
	 * This ensures that every allocated action-set is either attached to
	 * @rule->acts or pointed to by @act (and never both), and that only
	 * those action-sets in @rule->acts exist in hardware.  Consequently,
	 * in the failure path, @act only needs to be freed in memory, whereas
	 * for @rule->acts we remove each action-set from hardware before
	 * freeing it (efx_tc_free_action_set_list()), even if the action-set
	 * list itself is not in hardware.
	 */
	flow_action_for_each(i, fa, &fr->action) {
		struct efx_tc_action_set save;
		u16 tci;

		if (!act) {
			/* more actions after a non-pipe action */
			NL_SET_ERR_MSG_MOD(extack, "Action follows non-pipe action");
			rc = -EINVAL;
			goto release;
		}

		if ((fa->id == FLOW_ACTION_REDIRECT ||
		     fa->id == FLOW_ACTION_MIRRED ||
		     fa->id == FLOW_ACTION_DROP) && fa->hw_stats) {
			struct efx_tc_counter_index *ctr;

			/* Currently the only actions that want stats are
			 * mirred and gact (ok, shot, trap, goto-chain), which
			 * means we want stats just before delivery.  Also,
			 * note that tunnel_key set shouldn't change the length
			 * — it's only the subsequent mirred that does that,
			 * and the stats are taken _before_ the mirred action
			 * happens.
			 */
			if (!efx_tc_flower_action_order_ok(act, EFX_TC_AO_COUNT)) {
				/* All supported actions that count either steal
				 * (gact shot, mirred redirect) or clone act
				 * (mirred mirror), so we should never get two
				 * count actions on one action_set.
				 */
				NL_SET_ERR_MSG_MOD(extack, "Count-action conflict (can't happen)");
				rc = -EOPNOTSUPP;
				goto release;
			}

			if (!(fa->hw_stats & FLOW_ACTION_HW_STATS_DELAYED)) {
				NL_SET_ERR_MSG_FMT_MOD(extack, "hw_stats_type %u not supported (only 'delayed')",
						       fa->hw_stats);
				rc = -EOPNOTSUPP;
				goto release;
			}

			ctr = efx_tc_flower_get_counter_index(efx, tc->cookie,
							      EFX_TC_COUNTER_TYPE_AR);
			if (IS_ERR(ctr)) {
				rc = PTR_ERR(ctr);
				NL_SET_ERR_MSG_MOD(extack, "Failed to obtain a counter");
				goto release;
			}
			act->count = ctr;
		}

		switch (fa->id) {
		case FLOW_ACTION_DROP:
			rc = efx_mae_alloc_action_set(efx, act);
			if (rc) {
				NL_SET_ERR_MSG_MOD(extack, "Failed to write action set to hw (drop)");
				goto release;
			}
			list_add_tail(&act->list, &rule->acts.list);
			act = NULL; /* end of the line */
			break;
		case FLOW_ACTION_REDIRECT:
		case FLOW_ACTION_MIRRED:
			save = *act;

			if (!efx_tc_flower_action_order_ok(act, EFX_TC_AO_DELIVER)) {
				/* can't happen */
				rc = -EOPNOTSUPP;
				NL_SET_ERR_MSG_MOD(extack, "Deliver action violates action order (can't happen)");
				goto release;
			}

			to_efv = efx_tc_flower_lookup_efv(efx, fa->dev);
			if (IS_ERR(to_efv)) {
				NL_SET_ERR_MSG_MOD(extack, "Mirred egress device not on switch");
				rc = PTR_ERR(to_efv);
				goto release;
			}
			rc = efx_tc_flower_external_mport(efx, to_efv);
			if (rc < 0) {
				NL_SET_ERR_MSG_MOD(extack, "Failed to identify egress m-port");
				goto release;
			}
			act->dest_mport = rc;
			act->deliver = 1;
			rc = efx_mae_alloc_action_set(efx, act);
			if (rc) {
				NL_SET_ERR_MSG_MOD(extack, "Failed to write action set to hw (mirred)");
				goto release;
			}
			list_add_tail(&act->list, &rule->acts.list);
			act = NULL;
			if (fa->id == FLOW_ACTION_REDIRECT)
				break; /* end of the line */
			/* Mirror, so continue on with saved act */
			save.count = NULL;
			act = kzalloc(sizeof(*act), GFP_USER);
			if (!act) {
				rc = -ENOMEM;
				goto release;
			}
			*act = save;
			break;
		case FLOW_ACTION_VLAN_POP:
			if (act->vlan_push) {
				act->vlan_push--;
			} else if (efx_tc_flower_action_order_ok(act, EFX_TC_AO_VLAN_POP)) {
				act->vlan_pop++;
			} else {
				NL_SET_ERR_MSG_MOD(extack,
						   "More than two VLAN pops, or action order violated");
				rc = -EINVAL;
				goto release;
			}
			break;
		case FLOW_ACTION_VLAN_PUSH:
			if (!efx_tc_flower_action_order_ok(act, EFX_TC_AO_VLAN_PUSH)) {
				rc = -EINVAL;
				NL_SET_ERR_MSG_MOD(extack,
						   "More than two VLAN pushes, or action order violated");
				goto release;
			}
			tci = fa->vlan.vid & VLAN_VID_MASK;
			tci |= fa->vlan.prio << VLAN_PRIO_SHIFT;
			act->vlan_tci[act->vlan_push] = cpu_to_be16(tci);
			act->vlan_proto[act->vlan_push] = fa->vlan.proto;
			act->vlan_push++;
			break;
		default:
			NL_SET_ERR_MSG_FMT_MOD(extack, "Unhandled action %u",
					       fa->id);
			rc = -EOPNOTSUPP;
			goto release;
		}
	}

	if (act) {
		/* Not shot/redirected, so deliver to default dest */
		if (from_efv == EFX_EFV_PF)
			/* Rule applies to traffic from the wire,
			 * and default dest is thus the PF
			 */
			efx_mae_mport_uplink(efx, &act->dest_mport);
		else
			/* Representor, so rule applies to traffic from
			 * representee, and default dest is thus the rep.
			 * All reps use the same mport for delivery
			 */
			efx_mae_mport_mport(efx, efx->tc->reps_mport_id,
					    &act->dest_mport);
		act->deliver = 1;
		rc = efx_mae_alloc_action_set(efx, act);
		if (rc) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to write action set to hw (deliver)");
			goto release;
		}
		list_add_tail(&act->list, &rule->acts.list);
		act = NULL; /* Prevent double-free in error path */
	}

	netif_dbg(efx, drv, efx->net_dev,
		  "Successfully parsed filter (cookie %lx)\n",
		  tc->cookie);

	rule->match = match;

	rc = efx_mae_alloc_action_set_list(efx, &rule->acts);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to write action set list to hw");
		goto release;
	}
	rc = efx_mae_insert_rule(efx, &rule->match, EFX_TC_PRIO_TC,
				 rule->acts.fw_id, &rule->fw_id);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to insert rule in hw");
		goto release_acts;
	}
	return 0;

release_acts:
	efx_mae_free_action_set_list(efx, &rule->acts);
release:
	/* We failed to insert the rule, so free up any entries we created in
	 * subsidiary tables.
	 */
	if (act)
		efx_tc_free_action_set(efx, act, false);
	if (rule) {
		rhashtable_remove_fast(&efx->tc->match_action_ht,
				       &rule->linkage,
				       efx_tc_match_action_ht_params);
		efx_tc_free_action_set_list(efx, &rule->acts, false);
	}
	kfree(rule);
	return rc;
}

static int efx_tc_flower_destroy(struct efx_nic *efx,
				 struct net_device *net_dev,
				 struct flow_cls_offload *tc)
{
	struct netlink_ext_ack *extack = tc->common.extack;
	struct efx_tc_flow_rule *rule;

	rule = rhashtable_lookup_fast(&efx->tc->match_action_ht, &tc->cookie,
				      efx_tc_match_action_ht_params);
	if (!rule) {
		/* Only log a message if we're the ingress device.  Otherwise
		 * it's a foreign filter and we might just not have been
		 * interested (e.g. we might not have been the egress device
		 * either).
		 */
		if (!IS_ERR(efx_tc_flower_lookup_efv(efx, net_dev)))
			netif_warn(efx, drv, efx->net_dev,
				   "Filter %lx not found to remove\n", tc->cookie);
		NL_SET_ERR_MSG_MOD(extack, "Flow cookie not found in offloaded rules");
		return -ENOENT;
	}

	/* Remove it from HW */
	efx_tc_delete_rule(efx, rule);
	/* Delete it from SW */
	rhashtable_remove_fast(&efx->tc->match_action_ht, &rule->linkage,
			       efx_tc_match_action_ht_params);
	netif_dbg(efx, drv, efx->net_dev, "Removed filter %lx\n", rule->cookie);
	kfree(rule);
	return 0;
}

static int efx_tc_flower_stats(struct efx_nic *efx, struct net_device *net_dev,
			       struct flow_cls_offload *tc)
{
	struct netlink_ext_ack *extack = tc->common.extack;
	struct efx_tc_counter_index *ctr;
	struct efx_tc_counter *cnt;
	u64 packets, bytes;

	ctr = efx_tc_flower_find_counter_index(efx, tc->cookie);
	if (!ctr) {
		/* See comment in efx_tc_flower_destroy() */
		if (!IS_ERR(efx_tc_flower_lookup_efv(efx, net_dev)))
			if (net_ratelimit())
				netif_warn(efx, drv, efx->net_dev,
					   "Filter %lx not found for stats\n",
					   tc->cookie);
		NL_SET_ERR_MSG_MOD(extack, "Flow cookie not found in offloaded rules");
		return -ENOENT;
	}
	if (WARN_ON(!ctr->cnt)) /* can't happen */
		return -EIO;
	cnt = ctr->cnt;

	spin_lock_bh(&cnt->lock);
	/* Report only new pkts/bytes since last time TC asked */
	packets = cnt->packets;
	bytes = cnt->bytes;
	flow_stats_update(&tc->stats, bytes - cnt->old_bytes,
			  packets - cnt->old_packets, 0, cnt->touched,
			  FLOW_ACTION_HW_STATS_DELAYED);
	cnt->old_packets = packets;
	cnt->old_bytes = bytes;
	spin_unlock_bh(&cnt->lock);
	return 0;
}

int efx_tc_flower(struct efx_nic *efx, struct net_device *net_dev,
		  struct flow_cls_offload *tc, struct efx_rep *efv)
{
	int rc;

	if (!efx->tc)
		return -EOPNOTSUPP;

	mutex_lock(&efx->tc->mutex);
	switch (tc->command) {
	case FLOW_CLS_REPLACE:
		rc = efx_tc_flower_replace(efx, net_dev, tc, efv);
		break;
	case FLOW_CLS_DESTROY:
		rc = efx_tc_flower_destroy(efx, net_dev, tc);
		break;
	case FLOW_CLS_STATS:
		rc = efx_tc_flower_stats(efx, net_dev, tc);
		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&efx->tc->mutex);
	return rc;
}

static int efx_tc_configure_default_rule(struct efx_nic *efx, u32 ing_port,
					 u32 eg_port, struct efx_tc_flow_rule *rule)
{
	struct efx_tc_action_set_list *acts = &rule->acts;
	struct efx_tc_match *match = &rule->match;
	struct efx_tc_action_set *act;
	int rc;

	match->value.ingress_port = ing_port;
	match->mask.ingress_port = ~0;
	act = kzalloc(sizeof(*act), GFP_KERNEL);
	if (!act)
		return -ENOMEM;
	act->deliver = 1;
	act->dest_mport = eg_port;
	rc = efx_mae_alloc_action_set(efx, act);
	if (rc)
		goto fail1;
	EFX_WARN_ON_PARANOID(!list_empty(&acts->list));
	list_add_tail(&act->list, &acts->list);
	rc = efx_mae_alloc_action_set_list(efx, acts);
	if (rc)
		goto fail2;
	rc = efx_mae_insert_rule(efx, match, EFX_TC_PRIO_DFLT,
				 acts->fw_id, &rule->fw_id);
	if (rc)
		goto fail3;
	return 0;
fail3:
	efx_mae_free_action_set_list(efx, acts);
fail2:
	list_del(&act->list);
	efx_mae_free_action_set(efx, act->fw_id);
fail1:
	kfree(act);
	return rc;
}

static int efx_tc_configure_default_rule_pf(struct efx_nic *efx)
{
	struct efx_tc_flow_rule *rule = &efx->tc->dflt.pf;
	u32 ing_port, eg_port;

	efx_mae_mport_uplink(efx, &ing_port);
	efx_mae_mport_wire(efx, &eg_port);
	return efx_tc_configure_default_rule(efx, ing_port, eg_port, rule);
}

static int efx_tc_configure_default_rule_wire(struct efx_nic *efx)
{
	struct efx_tc_flow_rule *rule = &efx->tc->dflt.wire;
	u32 ing_port, eg_port;

	efx_mae_mport_wire(efx, &ing_port);
	efx_mae_mport_uplink(efx, &eg_port);
	return efx_tc_configure_default_rule(efx, ing_port, eg_port, rule);
}

int efx_tc_configure_default_rule_rep(struct efx_rep *efv)
{
	struct efx_tc_flow_rule *rule = &efv->dflt;
	struct efx_nic *efx = efv->parent;
	u32 ing_port, eg_port;

	efx_mae_mport_mport(efx, efv->mport, &ing_port);
	efx_mae_mport_mport(efx, efx->tc->reps_mport_id, &eg_port);
	return efx_tc_configure_default_rule(efx, ing_port, eg_port, rule);
}

void efx_tc_deconfigure_default_rule(struct efx_nic *efx,
				     struct efx_tc_flow_rule *rule)
{
	if (rule->fw_id != MC_CMD_MAE_ACTION_RULE_INSERT_OUT_ACTION_RULE_ID_NULL)
		efx_tc_delete_rule(efx, rule);
	rule->fw_id = MC_CMD_MAE_ACTION_RULE_INSERT_OUT_ACTION_RULE_ID_NULL;
}

static int efx_tc_configure_rep_mport(struct efx_nic *efx)
{
	u32 rep_mport_label;
	int rc;

	rc = efx_mae_allocate_mport(efx, &efx->tc->reps_mport_id, &rep_mport_label);
	if (rc)
		return rc;
	pci_dbg(efx->pci_dev, "created rep mport 0x%08x (0x%04x)\n",
		efx->tc->reps_mport_id, rep_mport_label);
	/* Use mport *selector* as vport ID */
	efx_mae_mport_mport(efx, efx->tc->reps_mport_id,
			    &efx->tc->reps_mport_vport_id);
	return 0;
}

static void efx_tc_deconfigure_rep_mport(struct efx_nic *efx)
{
	efx_mae_free_mport(efx, efx->tc->reps_mport_id);
	efx->tc->reps_mport_id = MAE_MPORT_SELECTOR_NULL;
}

int efx_tc_insert_rep_filters(struct efx_nic *efx)
{
	struct efx_filter_spec promisc, allmulti;
	int rc;

	if (efx->type->is_vf)
		return 0;
	if (!efx->tc)
		return 0;
	efx_filter_init_rx(&promisc, EFX_FILTER_PRI_REQUIRED, 0, 0);
	efx_filter_set_uc_def(&promisc);
	efx_filter_set_vport_id(&promisc, efx->tc->reps_mport_vport_id);
	rc = efx_filter_insert_filter(efx, &promisc, false);
	if (rc < 0)
		return rc;
	efx->tc->reps_filter_uc = rc;
	efx_filter_init_rx(&allmulti, EFX_FILTER_PRI_REQUIRED, 0, 0);
	efx_filter_set_mc_def(&allmulti);
	efx_filter_set_vport_id(&allmulti, efx->tc->reps_mport_vport_id);
	rc = efx_filter_insert_filter(efx, &allmulti, false);
	if (rc < 0)
		return rc;
	efx->tc->reps_filter_mc = rc;
	return 0;
}

void efx_tc_remove_rep_filters(struct efx_nic *efx)
{
	if (efx->type->is_vf)
		return;
	if (!efx->tc)
		return;
	if (efx->tc->reps_filter_mc >= 0)
		efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_REQUIRED, efx->tc->reps_filter_mc);
	efx->tc->reps_filter_mc = -1;
	if (efx->tc->reps_filter_uc >= 0)
		efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_REQUIRED, efx->tc->reps_filter_uc);
	efx->tc->reps_filter_uc = -1;
}

int efx_init_tc(struct efx_nic *efx)
{
	int rc;

	rc = efx_mae_get_caps(efx, efx->tc->caps);
	if (rc)
		return rc;
	if (efx->tc->caps->match_field_count > MAE_NUM_FIELDS)
		/* Firmware supports some match fields the driver doesn't know
		 * about.  Not fatal, unless any of those fields are required
		 * (MAE_FIELD_SUPPORTED_MATCH_ALWAYS) but if so we don't know.
		 */
		netif_warn(efx, probe, efx->net_dev,
			   "FW reports additional match fields %u\n",
			   efx->tc->caps->match_field_count);
	if (efx->tc->caps->action_prios < EFX_TC_PRIO__NUM) {
		netif_err(efx, probe, efx->net_dev,
			  "Too few action prios supported (have %u, need %u)\n",
			  efx->tc->caps->action_prios, EFX_TC_PRIO__NUM);
		return -EIO;
	}
	rc = efx_tc_configure_default_rule_pf(efx);
	if (rc)
		return rc;
	rc = efx_tc_configure_default_rule_wire(efx);
	if (rc)
		return rc;
	rc = efx_tc_configure_rep_mport(efx);
	if (rc)
		return rc;
	efx->tc->up = true;
	rc = flow_indr_dev_register(efx_tc_indr_setup_cb, efx);
	if (rc)
		return rc;
	return 0;
}

void efx_fini_tc(struct efx_nic *efx)
{
	/* We can get called even if efx_init_struct_tc() failed */
	if (!efx->tc)
		return;
	if (efx->tc->up)
		flow_indr_dev_unregister(efx_tc_indr_setup_cb, efx, efx_tc_block_unbind);
	efx_tc_deconfigure_rep_mport(efx);
	efx_tc_deconfigure_default_rule(efx, &efx->tc->dflt.pf);
	efx_tc_deconfigure_default_rule(efx, &efx->tc->dflt.wire);
	efx->tc->up = false;
}

/* At teardown time, all TC filter rules (and thus all resources they created)
 * should already have been removed.  If we find any in our hashtables, make a
 * cursory attempt to clean up the software side.
 */
static void efx_tc_encap_match_free(void *ptr, void *__unused)
{
	struct efx_tc_encap_match *encap = ptr;

	WARN_ON(refcount_read(&encap->ref));
	kfree(encap);
}

int efx_init_struct_tc(struct efx_nic *efx)
{
	int rc;

	if (efx->type->is_vf)
		return 0;

	efx->tc = kzalloc(sizeof(*efx->tc), GFP_KERNEL);
	if (!efx->tc)
		return -ENOMEM;
	efx->tc->caps = kzalloc(sizeof(struct mae_caps), GFP_KERNEL);
	if (!efx->tc->caps) {
		rc = -ENOMEM;
		goto fail_alloc_caps;
	}
	INIT_LIST_HEAD(&efx->tc->block_list);

	mutex_init(&efx->tc->mutex);
	init_waitqueue_head(&efx->tc->flush_wq);
	rc = efx_tc_init_counters(efx);
	if (rc < 0)
		goto fail_counters;
	rc = rhashtable_init(&efx->tc->encap_match_ht, &efx_tc_encap_match_ht_params);
	if (rc < 0)
		goto fail_encap_match_ht;
	rc = rhashtable_init(&efx->tc->match_action_ht, &efx_tc_match_action_ht_params);
	if (rc < 0)
		goto fail_match_action_ht;
	efx->tc->reps_filter_uc = -1;
	efx->tc->reps_filter_mc = -1;
	INIT_LIST_HEAD(&efx->tc->dflt.pf.acts.list);
	efx->tc->dflt.pf.fw_id = MC_CMD_MAE_ACTION_RULE_INSERT_OUT_ACTION_RULE_ID_NULL;
	INIT_LIST_HEAD(&efx->tc->dflt.wire.acts.list);
	efx->tc->dflt.wire.fw_id = MC_CMD_MAE_ACTION_RULE_INSERT_OUT_ACTION_RULE_ID_NULL;
	efx->extra_channel_type[EFX_EXTRA_CHANNEL_TC] = &efx_tc_channel_type;
	return 0;
fail_match_action_ht:
	rhashtable_destroy(&efx->tc->encap_match_ht);
fail_encap_match_ht:
	efx_tc_destroy_counters(efx);
fail_counters:
	mutex_destroy(&efx->tc->mutex);
	kfree(efx->tc->caps);
fail_alloc_caps:
	kfree(efx->tc);
	efx->tc = NULL;
	return rc;
}

void efx_fini_struct_tc(struct efx_nic *efx)
{
	if (!efx->tc)
		return;

	mutex_lock(&efx->tc->mutex);
	EFX_WARN_ON_PARANOID(efx->tc->dflt.pf.fw_id !=
			     MC_CMD_MAE_ACTION_RULE_INSERT_OUT_ACTION_RULE_ID_NULL);
	EFX_WARN_ON_PARANOID(efx->tc->dflt.wire.fw_id !=
			     MC_CMD_MAE_ACTION_RULE_INSERT_OUT_ACTION_RULE_ID_NULL);
	rhashtable_free_and_destroy(&efx->tc->match_action_ht, efx_tc_flow_free,
				    efx);
	rhashtable_free_and_destroy(&efx->tc->encap_match_ht,
				    efx_tc_encap_match_free, NULL);
	efx_tc_fini_counters(efx);
	mutex_unlock(&efx->tc->mutex);
	mutex_destroy(&efx->tc->mutex);
	kfree(efx->tc->caps);
	kfree(efx->tc);
	efx->tc = NULL;
}
