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
#include <net/tc_act/tc_ct.h>
#include "tc.h"
#include "tc_bindings.h"
#include "tc_encap_actions.h"
#include "tc_conntrack.h"
#include "mae.h"
#include "ef100_rep.h"
#include "efx.h"

enum efx_encap_type efx_tc_indr_netdev_type(struct net_device *net_dev)
{
	if (netif_is_vxlan(net_dev))
		return EFX_ENCAP_TYPE_VXLAN;
	if (netif_is_geneve(net_dev))
		return EFX_ENCAP_TYPE_GENEVE;

	return EFX_ENCAP_TYPE_NONE;
}

#define EFX_TC_HDR_TYPE_TTL_MASK ((u32)0xff)
/* Hoplimit is stored in the most significant byte in the pedit ipv6 header action */
#define EFX_TC_HDR_TYPE_HLIMIT_MASK ~((u32)0xff000000)
#define EFX_EFV_PF	NULL
/* Look up the representor information (efv) for a device.
 * May return NULL for the PF (us), or an error pointer for a device that
 * isn't supported as a TC offload endpoint
 */
struct efx_rep *efx_tc_flower_lookup_efv(struct efx_nic *efx,
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
s64 efx_tc_flower_external_mport(struct efx_nic *efx, struct efx_rep *efv)
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

static const struct rhashtable_params efx_tc_mac_ht_params = {
	.key_len	= offsetofend(struct efx_tc_mac_pedit_action, h_addr),
	.key_offset	= 0,
	.head_offset	= offsetof(struct efx_tc_mac_pedit_action, linkage),
};

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

static const struct rhashtable_params efx_tc_lhs_rule_ht_params = {
	.key_len	= sizeof(unsigned long),
	.key_offset	= offsetof(struct efx_tc_lhs_rule, cookie),
	.head_offset	= offsetof(struct efx_tc_lhs_rule, linkage),
};

static const struct rhashtable_params efx_tc_recirc_ht_params = {
	.key_len	= offsetof(struct efx_tc_recirc_id, linkage),
	.key_offset	= 0,
	.head_offset	= offsetof(struct efx_tc_recirc_id, linkage),
};

static struct efx_tc_mac_pedit_action *efx_tc_flower_get_mac(struct efx_nic *efx,
							     unsigned char h_addr[ETH_ALEN],
							     struct netlink_ext_ack *extack)
{
	struct efx_tc_mac_pedit_action *ped, *old;
	int rc;

	ped = kzalloc(sizeof(*ped), GFP_USER);
	if (!ped)
		return ERR_PTR(-ENOMEM);
	memcpy(ped->h_addr, h_addr, ETH_ALEN);
	old = rhashtable_lookup_get_insert_fast(&efx->tc->mac_ht,
						&ped->linkage,
						efx_tc_mac_ht_params);
	if (old) {
		/* don't need our new entry */
		kfree(ped);
		if (IS_ERR(old)) /* oh dear, it's actually an error */
			return ERR_CAST(old);
		if (!refcount_inc_not_zero(&old->ref))
			return ERR_PTR(-EAGAIN);
		/* existing entry found, ref taken */
		return old;
	}

	rc = efx_mae_allocate_pedit_mac(efx, ped);
	if (rc < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to store pedit MAC address in hw");
		goto out_remove;
	}

	/* ref and return */
	refcount_set(&ped->ref, 1);
	return ped;
out_remove:
	rhashtable_remove_fast(&efx->tc->mac_ht, &ped->linkage,
			       efx_tc_mac_ht_params);
	kfree(ped);
	return ERR_PTR(rc);
}

static void efx_tc_flower_put_mac(struct efx_nic *efx,
				  struct efx_tc_mac_pedit_action *ped)
{
	if (!refcount_dec_and_test(&ped->ref))
		return; /* still in use */
	rhashtable_remove_fast(&efx->tc->mac_ht, &ped->linkage,
			       efx_tc_mac_ht_params);
	efx_mae_free_pedit_mac(efx, ped);
	kfree(ped);
}

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
	if (act->count) {
		spin_lock_bh(&act->count->cnt->lock);
		if (!list_empty(&act->count_user))
			list_del(&act->count_user);
		spin_unlock_bh(&act->count->cnt->lock);
		efx_tc_flower_put_counter_index(efx, act->count);
	}
	if (act->encap_md) {
		list_del(&act->encap_user);
		efx_tc_flower_release_encap_md(efx, act->encap_md);
	}
	if (act->src_mac)
		efx_tc_flower_put_mac(efx, act->src_mac);
	if (act->dst_mac)
		efx_tc_flower_put_mac(efx, act->dst_mac);
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
	    ~(BIT_ULL(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_CVLAN) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ENC_KEYID) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ENC_IP) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ENC_PORTS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ENC_CONTROL) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_CT) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_TCP) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IP))) {
		NL_SET_ERR_MSG_FMT_MOD(extack, "Unsupported flower keys %#llx",
				       dissector->used_keys);
		return -EOPNOTSUPP;
	}

	MAP_KEY_AND_MASK(BASIC, basic, n_proto, eth_proto);
	/* Make sure we're IP if any L3/L4 keys used. */
	if (!IS_ALL_ONES(match->mask.eth_proto) ||
	    !(match->value.eth_proto == htons(ETH_P_IP) ||
	      match->value.eth_proto == htons(ETH_P_IPV6)))
		if (dissector->used_keys &
		    (BIT_ULL(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
		     BIT_ULL(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
		     BIT_ULL(FLOW_DISSECTOR_KEY_PORTS) |
		     BIT_ULL(FLOW_DISSECTOR_KEY_IP) |
		     BIT_ULL(FLOW_DISSECTOR_KEY_TCP))) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "L3/L4 flower keys %#llx require protocol ipv[46]",
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
		    (BIT_ULL(FLOW_DISSECTOR_KEY_PORTS) |
		     BIT_ULL(FLOW_DISSECTOR_KEY_TCP))) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "L4 flower keys %#llx require ipproto udp or tcp",
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
		   (BIT_ULL(FLOW_DISSECTOR_KEY_ENC_KEYID) |
		    BIT_ULL(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) |
		    BIT_ULL(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) |
		    BIT_ULL(FLOW_DISSECTOR_KEY_ENC_IP) |
		    BIT_ULL(FLOW_DISSECTOR_KEY_ENC_PORTS))) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Flower enc keys require enc_control (keys: %#llx)",
				       dissector->used_keys);
		return -EOPNOTSUPP;
	}
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CT)) {
		struct flow_match_ct fm;

		flow_rule_match_ct(rule, &fm);
		match->value.ct_state_trk = !!(fm.key->ct_state & TCA_FLOWER_KEY_CT_FLAGS_TRACKED);
		match->mask.ct_state_trk = !!(fm.mask->ct_state & TCA_FLOWER_KEY_CT_FLAGS_TRACKED);
		match->value.ct_state_est = !!(fm.key->ct_state & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED);
		match->mask.ct_state_est = !!(fm.mask->ct_state & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED);
		if (fm.mask->ct_state & ~(TCA_FLOWER_KEY_CT_FLAGS_TRACKED |
					  TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED)) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Unsupported ct_state match %#x",
					       fm.mask->ct_state);
			return -EOPNOTSUPP;
		}
		match->value.ct_mark = fm.key->ct_mark;
		match->mask.ct_mark = fm.mask->ct_mark;
		match->value.ct_zone = fm.key->ct_zone;
		match->mask.ct_zone = fm.mask->ct_zone;

		if (memchr_inv(fm.mask->ct_labels, 0, sizeof(fm.mask->ct_labels))) {
			NL_SET_ERR_MSG_MOD(extack, "Matching on ct_label not supported");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static void efx_tc_flower_release_encap_match(struct efx_nic *efx,
					      struct efx_tc_encap_match *encap)
{
	int rc;

	if (!refcount_dec_and_test(&encap->ref))
		return; /* still in use */

	if (encap->type == EFX_TC_EM_DIRECT) {
		rc = efx_mae_unregister_encap_match(efx, encap);
		if (rc)
			/* Display message but carry on and remove entry from our
			 * SW tables, because there's not much we can do about it.
			 */
			netif_err(efx, drv, efx->net_dev,
				  "Failed to release encap match %#x, rc %d\n",
				  encap->fw_id, rc);
	}
	rhashtable_remove_fast(&efx->tc->encap_match_ht, &encap->linkage,
			       efx_tc_encap_match_ht_params);
	if (encap->pseudo)
		efx_tc_flower_release_encap_match(efx, encap->pseudo);
	kfree(encap);
}

static int efx_tc_flower_record_encap_match(struct efx_nic *efx,
					    struct efx_tc_match *match,
					    enum efx_encap_type type,
					    enum efx_tc_em_pseudo_type em_type,
					    u8 child_ip_tos_mask,
					    __be16 child_udp_sport_mask,
					    struct netlink_ext_ack *extack)
{
	struct efx_tc_encap_match *encap, *old, *pseudo = NULL;
	bool ipv6 = false;
	int rc;

	/* We require that the socket-defining fields (IP addrs and UDP dest
	 * port) are present and exact-match.  Other fields may only be used
	 * if the field-set (and any masks) are the same for all encap
	 * matches on the same <sip,dip,dport> tuple; this is enforced by
	 * pseudo encap matches.
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
	if (match->mask.enc_sport || match->mask.enc_ip_tos) {
		struct efx_tc_match pmatch = *match;

		if (em_type == EFX_TC_EM_PSEUDO_MASK) { /* can't happen */
			NL_SET_ERR_MSG_MOD(extack, "Bad recursion in egress encap match handler");
			return -EOPNOTSUPP;
		}
		pmatch.value.enc_ip_tos = 0;
		pmatch.mask.enc_ip_tos = 0;
		pmatch.value.enc_sport = 0;
		pmatch.mask.enc_sport = 0;
		rc = efx_tc_flower_record_encap_match(efx, &pmatch, type,
						      EFX_TC_EM_PSEUDO_MASK,
						      match->mask.enc_ip_tos,
						      match->mask.enc_sport,
						      extack);
		if (rc)
			return rc;
		pseudo = pmatch.encap;
	}
	if (match->mask.enc_ip_ttl) {
		NL_SET_ERR_MSG_MOD(extack, "Egress encap match on IP TTL not supported");
		rc = -EOPNOTSUPP;
		goto fail_pseudo;
	}

	rc = efx_mae_check_encap_match_caps(efx, ipv6, match->mask.enc_ip_tos,
					    match->mask.enc_sport, extack);
	if (rc)
		goto fail_pseudo;

	encap = kzalloc(sizeof(*encap), GFP_USER);
	if (!encap) {
		rc = -ENOMEM;
		goto fail_pseudo;
	}
	encap->src_ip = match->value.enc_src_ip;
	encap->dst_ip = match->value.enc_dst_ip;
#ifdef CONFIG_IPV6
	encap->src_ip6 = match->value.enc_src_ip6;
	encap->dst_ip6 = match->value.enc_dst_ip6;
#endif
	encap->udp_dport = match->value.enc_dport;
	encap->tun_type = type;
	encap->ip_tos = match->value.enc_ip_tos;
	encap->ip_tos_mask = match->mask.enc_ip_tos;
	encap->child_ip_tos_mask = child_ip_tos_mask;
	encap->udp_sport = match->value.enc_sport;
	encap->udp_sport_mask = match->mask.enc_sport;
	encap->child_udp_sport_mask = child_udp_sport_mask;
	encap->type = em_type;
	encap->pseudo = pseudo;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->encap_match_ht,
						&encap->linkage,
						efx_tc_encap_match_ht_params);
	if (old) {
		/* don't need our new entry */
		kfree(encap);
		if (pseudo) /* don't need our new pseudo either */
			efx_tc_flower_release_encap_match(efx, pseudo);
		if (IS_ERR(old)) /* oh dear, it's actually an error */
			return PTR_ERR(old);
		/* check old and new em_types are compatible */
		switch (old->type) {
		case EFX_TC_EM_DIRECT:
			/* old EM is in hardware, so mustn't overlap with a
			 * pseudo, but may be shared with another direct EM
			 */
			if (em_type == EFX_TC_EM_DIRECT)
				break;
			NL_SET_ERR_MSG_MOD(extack, "Pseudo encap match conflicts with existing direct entry");
			return -EEXIST;
		case EFX_TC_EM_PSEUDO_MASK:
			/* old EM is protecting a ToS- or src port-qualified
			 * filter, so may only be shared with another pseudo
			 * for the same ToS and src port masks.
			 */
			if (em_type != EFX_TC_EM_PSEUDO_MASK) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "%s encap match conflicts with existing pseudo(MASK) entry",
						       em_type ? "Pseudo" : "Direct");
				return -EEXIST;
			}
			if (child_ip_tos_mask != old->child_ip_tos_mask) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Pseudo encap match for TOS mask %#04x conflicts with existing pseudo(MASK) entry for TOS mask %#04x",
						       child_ip_tos_mask,
						       old->child_ip_tos_mask);
				return -EEXIST;
			}
			if (child_udp_sport_mask != old->child_udp_sport_mask) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Pseudo encap match for UDP src port mask %#x conflicts with existing pseudo(MASK) entry for mask %#x",
						       child_udp_sport_mask,
						       old->child_udp_sport_mask);
				return -EEXIST;
			}
			break;
		case EFX_TC_EM_PSEUDO_OR:
			/* old EM corresponds to an OR that has to be unique
			 * (it must not overlap with any other OR, whether
			 * direct-EM or pseudo).
			 */
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "%s encap match conflicts with existing pseudo(OR) entry",
					       em_type ? "Pseudo" : "Direct");
			return -EEXIST;
		default: /* Unrecognised pseudo-type.  Just say no */
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "%s encap match conflicts with existing pseudo(%d) entry",
					       em_type ? "Pseudo" : "Direct",
					       old->type);
			return -EEXIST;
		}
		/* check old and new tun_types are compatible */
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
		if (em_type == EFX_TC_EM_DIRECT) {
			rc = efx_mae_register_encap_match(efx, encap);
			if (rc) {
				NL_SET_ERR_MSG_MOD(extack, "Failed to record egress encap match in HW");
				goto fail;
			}
		}
		refcount_set(&encap->ref, 1);
	}
	match->encap = encap;
	return 0;
fail:
	rhashtable_remove_fast(&efx->tc->encap_match_ht, &encap->linkage,
			       efx_tc_encap_match_ht_params);
	kfree(encap);
fail_pseudo:
	if (pseudo)
		efx_tc_flower_release_encap_match(efx, pseudo);
	return rc;
}

static struct efx_tc_recirc_id *efx_tc_get_recirc_id(struct efx_nic *efx,
						     u32 chain_index,
						     struct net_device *net_dev)
{
	struct efx_tc_recirc_id *rid, *old;
	int rc;

	rid = kzalloc(sizeof(*rid), GFP_USER);
	if (!rid)
		return ERR_PTR(-ENOMEM);
	rid->chain_index = chain_index;
	/* We don't take a reference here, because it's implied - if there's
	 * a rule on the net_dev that's been offloaded to us, then the net_dev
	 * can't go away until the rule has been deoffloaded.
	 */
	rid->net_dev = net_dev;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->recirc_ht,
						&rid->linkage,
						efx_tc_recirc_ht_params);
	if (old) {
		/* don't need our new entry */
		kfree(rid);
		if (IS_ERR(old)) /* oh dear, it's actually an error */
			return ERR_CAST(old);
		if (!refcount_inc_not_zero(&old->ref))
			return ERR_PTR(-EAGAIN);
		/* existing entry found */
		rid = old;
	} else {
		rc = ida_alloc_range(&efx->tc->recirc_ida, 1, U8_MAX, GFP_USER);
		if (rc < 0) {
			rhashtable_remove_fast(&efx->tc->recirc_ht,
					       &rid->linkage,
					       efx_tc_recirc_ht_params);
			kfree(rid);
			return ERR_PTR(rc);
		}
		rid->fw_id = rc;
		refcount_set(&rid->ref, 1);
	}
	return rid;
}

static void efx_tc_put_recirc_id(struct efx_nic *efx, struct efx_tc_recirc_id *rid)
{
	if (!refcount_dec_and_test(&rid->ref))
		return; /* still in use */
	rhashtable_remove_fast(&efx->tc->recirc_ht, &rid->linkage,
			       efx_tc_recirc_ht_params);
	ida_free(&efx->tc->recirc_ida, rid->fw_id);
	kfree(rid);
}

static void efx_tc_delete_rule(struct efx_nic *efx, struct efx_tc_flow_rule *rule)
{
	efx_mae_delete_rule(efx, rule->fw_id);

	/* Release entries in subsidiary tables */
	efx_tc_free_action_set_list(efx, &rule->acts, true);
	if (rule->match.rid)
		efx_tc_put_recirc_id(efx, rule->match.rid);
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
	EFX_TC_AO_DEC_TTL,
	EFX_TC_AO_PEDIT_MAC_ADDRS,
	EFX_TC_AO_VLAN_POP,
	EFX_TC_AO_VLAN_PUSH,
	EFX_TC_AO_COUNT,
	EFX_TC_AO_ENCAP,
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
		/* PEDIT_MAC_ADDRS must not happen before DECAP, though it
		 * can wait until much later
		 */
		if (act->dst_mac || act->src_mac)
			return false;

		/* Decrementing ttl must not happen before DECAP */
		if (act->do_ttl_dec)
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
	case EFX_TC_AO_PEDIT_MAC_ADDRS:
	case EFX_TC_AO_ENCAP:
		if (act->encap_md)
			return false;
		fallthrough;
	case EFX_TC_AO_DELIVER:
		return !act->deliver;
	case EFX_TC_AO_DEC_TTL:
		if (act->encap_md)
			return false;
		return !act->do_ttl_dec;
	default:
		/* Bad caller.  Whatever they wanted to do, say they can't. */
		WARN_ON_ONCE(1);
		return false;
	}
}

/**
 * DOC: TC conntrack sequences
 *
 * The MAE hardware can handle at most two rounds of action rule matching,
 * consequently we support conntrack through the notion of a "left-hand side
 * rule".  This is a rule which typically contains only the actions "ct" and
 * "goto chain N", and corresponds to one or more "right-hand side rules" in
 * chain N, which typically match on +trk+est, and may perform ct(nat) actions.
 * RHS rules go in the Action Rule table as normal but with a nonzero recirc_id
 * (the hardware equivalent of chain_index), while LHS rules may go in either
 * the Action Rule or the Outer Rule table, the latter being preferred for
 * performance reasons, and set both DO_CT and a recirc_id in their response.
 *
 * Besides the RHS rules, there are often also similar rules matching on
 * +trk+new which perform the ct(commit) action.  These are not offloaded.
 */

static bool efx_tc_rule_is_lhs_rule(struct flow_rule *fr,
				    struct efx_tc_match *match)
{
	const struct flow_action_entry *fa;
	int i;

	flow_action_for_each(i, fa, &fr->action) {
		switch (fa->id) {
		case FLOW_ACTION_GOTO:
			return true;
		case FLOW_ACTION_CT:
			/* If rule is -trk, or doesn't mention trk at all, then
			 * a CT action implies a conntrack lookup (hence it's an
			 * LHS rule).  If rule is +trk, then a CT action could
			 * just be ct(nat) or even ct(commit) (though the latter
			 * can't be offloaded).
			 */
			if (!match->mask.ct_state_trk || !match->value.ct_state_trk)
				return true;
			break;
		default:
			break;
		}
	}
	return false;
}

/* A foreign LHS rule has matches on enc_ keys at the TC layer (including an
 * implied match on enc_ip_proto UDP).  Translate these into non-enc_ keys,
 * so that we can use the same MAE machinery as local LHS rules (and so that
 * the lhs_rules entries have uniform semantics).  It may seem odd to do it
 * this way round, given that the corresponding fields in the MAE MCDIs are
 * all ENC_, but (a) we don't have enc_L2 or enc_ip_proto in struct
 * efx_tc_match_fields and (b) semantically an LHS rule doesn't have inner
 * fields so it's just matching on *the* header rather than the outer header.
 * Make sure that the non-enc_ keys were not already being matched on, as that
 * would imply a rule that needed a triple lookup.  (Hardware can do that,
 * with OR-AR-CT-AR, but it halves packet rate so we avoid it where possible;
 * see efx_tc_flower_flhs_needs_ar().)
 */
static int efx_tc_flower_translate_flhs_match(struct efx_tc_match *match)
{
	int rc = 0;

#define COPY_MASK_AND_VALUE(_key, _ekey)	({	\
	if (match->mask._key) {				\
		rc = -EOPNOTSUPP;			\
	} else {					\
		match->mask._key = match->mask._ekey;	\
		match->mask._ekey = 0;			\
		match->value._key = match->value._ekey;	\
		match->value._ekey = 0;			\
	}						\
	rc;						\
})
#define COPY_FROM_ENC(_key)	COPY_MASK_AND_VALUE(_key, enc_##_key)
	if (match->mask.ip_proto)
		return -EOPNOTSUPP;
	match->mask.ip_proto = ~0;
	match->value.ip_proto = IPPROTO_UDP;
	if (COPY_FROM_ENC(src_ip) || COPY_FROM_ENC(dst_ip))
		return rc;
#ifdef CONFIG_IPV6
	if (!ipv6_addr_any(&match->mask.src_ip6))
		return -EOPNOTSUPP;
	match->mask.src_ip6 = match->mask.enc_src_ip6;
	memset(&match->mask.enc_src_ip6, 0, sizeof(struct in6_addr));
	if (!ipv6_addr_any(&match->mask.dst_ip6))
		return -EOPNOTSUPP;
	match->mask.dst_ip6 = match->mask.enc_dst_ip6;
	memset(&match->mask.enc_dst_ip6, 0, sizeof(struct in6_addr));
#endif
	if (COPY_FROM_ENC(ip_tos) || COPY_FROM_ENC(ip_ttl))
		return rc;
	/* should really copy enc_ip_frag but we don't have that in
	 * parse_match yet
	 */
	if (COPY_MASK_AND_VALUE(l4_sport, enc_sport) ||
	    COPY_MASK_AND_VALUE(l4_dport, enc_dport))
		return rc;
	return 0;
#undef COPY_FROM_ENC
#undef COPY_MASK_AND_VALUE
}

/* If a foreign LHS rule wants to match on keys that are only available after
 * encap header identification and parsing, then it can't be done in the Outer
 * Rule lookup, because that lookup determines the encap type used to parse
 * beyond the outer headers.  Thus, such rules must use the OR-AR-CT-AR lookup
 * sequence, with an EM (struct efx_tc_encap_match) in the OR step.
 * Return true iff the passed match requires this.
 */
static bool efx_tc_flower_flhs_needs_ar(struct efx_tc_match *match)
{
	/* matches on inner-header keys can't be done in OR */
	return match->mask.eth_proto ||
	       match->mask.vlan_tci[0] || match->mask.vlan_tci[1] ||
	       match->mask.vlan_proto[0] || match->mask.vlan_proto[1] ||
	       memchr_inv(match->mask.eth_saddr, 0, ETH_ALEN) ||
	       memchr_inv(match->mask.eth_daddr, 0, ETH_ALEN) ||
	       match->mask.ip_proto ||
	       match->mask.ip_tos || match->mask.ip_ttl ||
	       match->mask.src_ip || match->mask.dst_ip ||
#ifdef CONFIG_IPV6
	       !ipv6_addr_any(&match->mask.src_ip6) ||
	       !ipv6_addr_any(&match->mask.dst_ip6) ||
#endif
	       match->mask.ip_frag || match->mask.ip_firstfrag ||
	       match->mask.l4_sport || match->mask.l4_dport ||
	       match->mask.tcp_flags ||
	/* nor can VNI */
	       match->mask.enc_keyid;
}

static int efx_tc_flower_handle_lhs_actions(struct efx_nic *efx,
					    struct flow_cls_offload *tc,
					    struct flow_rule *fr,
					    struct net_device *net_dev,
					    struct efx_tc_lhs_rule *rule)

{
	struct netlink_ext_ack *extack = tc->common.extack;
	struct efx_tc_lhs_action *act = &rule->lhs_act;
	const struct flow_action_entry *fa;
	enum efx_tc_counter_type ctype;
	bool pipe = true;
	int i;

	ctype = rule->is_ar ? EFX_TC_COUNTER_TYPE_AR : EFX_TC_COUNTER_TYPE_OR;

	flow_action_for_each(i, fa, &fr->action) {
		struct efx_tc_ct_zone *ct_zone;
		struct efx_tc_recirc_id *rid;

		if (!pipe) {
			/* more actions after a non-pipe action */
			NL_SET_ERR_MSG_MOD(extack, "Action follows non-pipe action");
			return -EINVAL;
		}
		switch (fa->id) {
		case FLOW_ACTION_GOTO:
			if (!fa->chain_index) {
				NL_SET_ERR_MSG_MOD(extack, "Can't goto chain 0, no looping in hw");
				return -EOPNOTSUPP;
			}
			rid = efx_tc_get_recirc_id(efx, fa->chain_index,
						   net_dev);
			if (IS_ERR(rid)) {
				NL_SET_ERR_MSG_MOD(extack, "Failed to allocate a hardware recirculation ID for this chain_index");
				return PTR_ERR(rid);
			}
			act->rid = rid;
			if (fa->hw_stats) {
				struct efx_tc_counter_index *cnt;

				if (!(fa->hw_stats & FLOW_ACTION_HW_STATS_DELAYED)) {
					NL_SET_ERR_MSG_FMT_MOD(extack,
							       "hw_stats_type %u not supported (only 'delayed')",
							       fa->hw_stats);
					return -EOPNOTSUPP;
				}
				cnt = efx_tc_flower_get_counter_index(efx, tc->cookie,
								      ctype);
				if (IS_ERR(cnt)) {
					NL_SET_ERR_MSG_MOD(extack, "Failed to obtain a counter");
					return PTR_ERR(cnt);
				}
				WARN_ON(act->count); /* can't happen */
				act->count = cnt;
			}
			pipe = false;
			break;
		case FLOW_ACTION_CT:
			if (act->zone) {
				NL_SET_ERR_MSG_MOD(extack, "Can't offload multiple ct actions");
				return -EOPNOTSUPP;
			}
			if (fa->ct.action & (TCA_CT_ACT_COMMIT |
					     TCA_CT_ACT_FORCE)) {
				NL_SET_ERR_MSG_MOD(extack, "Can't offload ct commit/force");
				return -EOPNOTSUPP;
			}
			if (fa->ct.action & TCA_CT_ACT_CLEAR) {
				NL_SET_ERR_MSG_MOD(extack, "Can't clear ct in LHS rule");
				return -EOPNOTSUPP;
			}
			if (fa->ct.action & (TCA_CT_ACT_NAT |
					     TCA_CT_ACT_NAT_SRC |
					     TCA_CT_ACT_NAT_DST)) {
				NL_SET_ERR_MSG_MOD(extack, "Can't perform NAT in LHS rule - packet isn't conntracked yet");
				return -EOPNOTSUPP;
			}
			if (fa->ct.action) {
				NL_SET_ERR_MSG_FMT_MOD(extack, "Unhandled ct.action %u for LHS rule\n",
						       fa->ct.action);
				return -EOPNOTSUPP;
			}
			ct_zone = efx_tc_ct_register_zone(efx, fa->ct.zone,
							  fa->ct.flow_table);
			if (IS_ERR(ct_zone)) {
				NL_SET_ERR_MSG_MOD(extack, "Failed to register for CT updates");
				return PTR_ERR(ct_zone);
			}
			act->zone = ct_zone;
			break;
		default:
			NL_SET_ERR_MSG_FMT_MOD(extack, "Unhandled action %u for LHS rule\n",
					       fa->id);
			return -EOPNOTSUPP;
		}
	}

	if (pipe) {
		NL_SET_ERR_MSG_MOD(extack, "Missing goto chain in LHS rule");
		return -EOPNOTSUPP;
	}
	return 0;
}

static void efx_tc_flower_release_lhs_actions(struct efx_nic *efx,
					      struct efx_tc_lhs_action *act)
{
	if (act->rid)
		efx_tc_put_recirc_id(efx, act->rid);
	if (act->zone)
		efx_tc_ct_unregister_zone(efx, act->zone);
	if (act->count)
		efx_tc_flower_put_counter_index(efx, act->count);
}

/**
 * struct efx_tc_mangler_state - accumulates 32-bit pedits into fields
 *
 * @dst_mac_32:	dst_mac[0:3] has been populated
 * @dst_mac_16:	dst_mac[4:5] has been populated
 * @src_mac_16:	src_mac[0:1] has been populated
 * @src_mac_32:	src_mac[2:5] has been populated
 * @dst_mac:	h_dest field of ethhdr
 * @src_mac:	h_source field of ethhdr
 *
 * Since FLOW_ACTION_MANGLE comes in 32-bit chunks that do not
 * necessarily equate to whole fields of the packet header, this
 * structure is used to hold the cumulative effect of the partial
 * field pedits that have been processed so far.
 */
struct efx_tc_mangler_state {
	u8 dst_mac_32:1; /* eth->h_dest[0:3] */
	u8 dst_mac_16:1; /* eth->h_dest[4:5] */
	u8 src_mac_16:1; /* eth->h_source[0:1] */
	u8 src_mac_32:1; /* eth->h_source[2:5] */
	unsigned char dst_mac[ETH_ALEN];
	unsigned char src_mac[ETH_ALEN];
};

/** efx_tc_complete_mac_mangle() - pull complete field pedits out of @mung
 * @efx:	NIC we're installing a flow rule on
 * @act:	action set (cursor) to update
 * @mung:	accumulated partial mangles
 * @extack:	netlink extended ack for reporting errors
 *
 * Check @mung to find any combinations of partial mangles that can be
 * combined into a complete packet field edit, add that edit to @act,
 * and consume the partial mangles from @mung.
 */

static int efx_tc_complete_mac_mangle(struct efx_nic *efx,
				      struct efx_tc_action_set *act,
				      struct efx_tc_mangler_state *mung,
				      struct netlink_ext_ack *extack)
{
	struct efx_tc_mac_pedit_action *ped;

	if (mung->dst_mac_32 && mung->dst_mac_16) {
		ped = efx_tc_flower_get_mac(efx, mung->dst_mac, extack);
		if (IS_ERR(ped))
			return PTR_ERR(ped);

		/* Check that we have not already populated dst_mac */
		if (act->dst_mac)
			efx_tc_flower_put_mac(efx, act->dst_mac);

		act->dst_mac = ped;

		/* consume the incomplete state */
		mung->dst_mac_32 = 0;
		mung->dst_mac_16 = 0;
	}
	if (mung->src_mac_16 && mung->src_mac_32) {
		ped = efx_tc_flower_get_mac(efx, mung->src_mac, extack);
		if (IS_ERR(ped))
			return PTR_ERR(ped);

		/* Check that we have not already populated src_mac */
		if (act->src_mac)
			efx_tc_flower_put_mac(efx, act->src_mac);

		act->src_mac = ped;

		/* consume the incomplete state */
		mung->src_mac_32 = 0;
		mung->src_mac_16 = 0;
	}
	return 0;
}

static int efx_tc_pedit_add(struct efx_nic *efx, struct efx_tc_action_set *act,
			    const struct flow_action_entry *fa,
			    struct netlink_ext_ack *extack)
{
	switch (fa->mangle.htype) {
	case FLOW_ACT_MANGLE_HDR_TYPE_IP4:
		switch (fa->mangle.offset) {
		case offsetof(struct iphdr, ttl):
			/* check that pedit applies to ttl only */
			if (fa->mangle.mask != ~EFX_TC_HDR_TYPE_TTL_MASK)
				break;

			/* Adding 0xff is equivalent to decrementing the ttl.
			 * Other added values are not supported.
			 */
			if ((fa->mangle.val & EFX_TC_HDR_TYPE_TTL_MASK) != U8_MAX)
				break;

			/* check that we do not decrement ttl twice */
			if (!efx_tc_flower_action_order_ok(act,
							   EFX_TC_AO_DEC_TTL)) {
				NL_SET_ERR_MSG_MOD(extack, "Unsupported: multiple dec ttl");
				return -EOPNOTSUPP;
			}
			act->do_ttl_dec = 1;
			return 0;
		default:
			break;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_IP6:
		switch (fa->mangle.offset) {
		case round_down(offsetof(struct ipv6hdr, hop_limit), 4):
			/* check that pedit applies to hoplimit only */
			if (fa->mangle.mask != EFX_TC_HDR_TYPE_HLIMIT_MASK)
				break;

			/* Adding 0xff is equivalent to decrementing the hoplimit.
			 * Other added values are not supported.
			 */
			if ((fa->mangle.val >> 24) != U8_MAX)
				break;

			/* check that we do not decrement hoplimit twice */
			if (!efx_tc_flower_action_order_ok(act,
							   EFX_TC_AO_DEC_TTL)) {
				NL_SET_ERR_MSG_MOD(extack, "Unsupported: multiple dec ttl");
				return -EOPNOTSUPP;
			}
			act->do_ttl_dec = 1;
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	NL_SET_ERR_MSG_FMT_MOD(extack,
			       "Unsupported: ttl add action type %x %x %x/%x",
			       fa->mangle.htype, fa->mangle.offset,
			       fa->mangle.val, fa->mangle.mask);
	return -EOPNOTSUPP;
}

/**
 * efx_tc_mangle() - handle a single 32-bit (or less) pedit
 * @efx:	NIC we're installing a flow rule on
 * @act:	action set (cursor) to update
 * @fa:		FLOW_ACTION_MANGLE action metadata
 * @mung:	accumulator for partial mangles
 * @extack:	netlink extended ack for reporting errors
 * @match:	original match used along with the mangle action
 *
 * Identify the fields written by a FLOW_ACTION_MANGLE, and record
 * the partial mangle state in @mung.  If this mangle completes an
 * earlier partial mangle, consume and apply to @act by calling
 * efx_tc_complete_mac_mangle().
 */

static int efx_tc_mangle(struct efx_nic *efx, struct efx_tc_action_set *act,
			 const struct flow_action_entry *fa,
			 struct efx_tc_mangler_state *mung,
			 struct netlink_ext_ack *extack,
			 struct efx_tc_match *match)
{
	__le32 mac32;
	__le16 mac16;
	u8 tr_ttl;

	switch (fa->mangle.htype) {
	case FLOW_ACT_MANGLE_HDR_TYPE_ETH:
		BUILD_BUG_ON(offsetof(struct ethhdr, h_dest) != 0);
		BUILD_BUG_ON(offsetof(struct ethhdr, h_source) != 6);
		if (!efx_tc_flower_action_order_ok(act, EFX_TC_AO_PEDIT_MAC_ADDRS)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Pedit mangle mac action violates action order");
			return -EOPNOTSUPP;
		}
		switch (fa->mangle.offset) {
		case 0:
			if (fa->mangle.mask) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Unsupported: mask (%#x) of eth.dst32 mangle",
						       fa->mangle.mask);
				return -EOPNOTSUPP;
			}
			/* Ethernet address is little-endian */
			mac32 = cpu_to_le32(fa->mangle.val);
			memcpy(mung->dst_mac, &mac32, sizeof(mac32));
			mung->dst_mac_32 = 1;
			return efx_tc_complete_mac_mangle(efx, act, mung, extack);
		case 4:
			if (fa->mangle.mask == 0xffff) {
				mac16 = cpu_to_le16(fa->mangle.val >> 16);
				memcpy(mung->src_mac, &mac16, sizeof(mac16));
				mung->src_mac_16 = 1;
			} else if (fa->mangle.mask == 0xffff0000) {
				mac16 = cpu_to_le16((u16)fa->mangle.val);
				memcpy(mung->dst_mac + 4, &mac16, sizeof(mac16));
				mung->dst_mac_16 = 1;
			} else {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Unsupported: mask (%#x) of eth+4 mangle is not high or low 16b",
						       fa->mangle.mask);
				return -EOPNOTSUPP;
			}
			return efx_tc_complete_mac_mangle(efx, act, mung, extack);
		case 8:
			if (fa->mangle.mask) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Unsupported: mask (%#x) of eth.src32 mangle",
						       fa->mangle.mask);
				return -EOPNOTSUPP;
			}
			mac32 = cpu_to_le32(fa->mangle.val);
			memcpy(mung->src_mac + 2, &mac32, sizeof(mac32));
			mung->src_mac_32 = 1;
			return efx_tc_complete_mac_mangle(efx, act, mung, extack);
		default:
			NL_SET_ERR_MSG_FMT_MOD(extack, "Unsupported: mangle eth+%u %x/%x",
					       fa->mangle.offset, fa->mangle.val, fa->mangle.mask);
			return -EOPNOTSUPP;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_IP4:
		switch (fa->mangle.offset) {
		case offsetof(struct iphdr, ttl):
			/* we currently only support pedit IP4 when it applies
			 * to TTL and then only when it can be achieved with a
			 * decrement ttl action
			 */

			/* check that pedit applies to ttl only */
			if (fa->mangle.mask != ~EFX_TC_HDR_TYPE_TTL_MASK) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Unsupported: mask (%#x) out of range, only support mangle action on ipv4.ttl",
						       fa->mangle.mask);
				return -EOPNOTSUPP;
			}

			/* we can only convert to a dec ttl when we have an
			 * exact match on the ttl field
			 */
			if (match->mask.ip_ttl != U8_MAX) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Unsupported: only support mangle ipv4.ttl when we have an exact match on ttl, mask used for match (%#x)",
						       match->mask.ip_ttl);
				return -EOPNOTSUPP;
			}

			/* check that we don't try to decrement 0, which equates
			 * to setting the ttl to 0xff
			 */
			if (match->value.ip_ttl == 0) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Unsupported: we cannot decrement ttl past 0");
				return -EOPNOTSUPP;
			}

			/* check that we do not decrement ttl twice */
			if (!efx_tc_flower_action_order_ok(act,
							   EFX_TC_AO_DEC_TTL)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Unsupported: multiple dec ttl");
				return -EOPNOTSUPP;
			}

			/* check pedit can be achieved with decrement action */
			tr_ttl = match->value.ip_ttl - 1;
			if ((fa->mangle.val & EFX_TC_HDR_TYPE_TTL_MASK) == tr_ttl) {
				act->do_ttl_dec = 1;
				return 0;
			}

			fallthrough;
		default:
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Unsupported: only support mangle on the ttl field (offset is %u)",
					       fa->mangle.offset);
			return -EOPNOTSUPP;
		}
		break;
	case FLOW_ACT_MANGLE_HDR_TYPE_IP6:
		switch (fa->mangle.offset) {
		case round_down(offsetof(struct ipv6hdr, hop_limit), 4):
			/* we currently only support pedit IP6 when it applies
			 * to the hoplimit and then only when it can be achieved
			 * with a decrement hoplimit action
			 */

			/* check that pedit applies to ttl only */
			if (fa->mangle.mask != EFX_TC_HDR_TYPE_HLIMIT_MASK) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Unsupported: mask (%#x) out of range, only support mangle action on ipv6.hop_limit",
						       fa->mangle.mask);

				return -EOPNOTSUPP;
			}

			/* we can only convert to a dec ttl when we have an
			 * exact match on the ttl field
			 */
			if (match->mask.ip_ttl != U8_MAX) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Unsupported: only support mangle ipv6.hop_limit when we have an exact match on ttl, mask used for match (%#x)",
						       match->mask.ip_ttl);
				return -EOPNOTSUPP;
			}

			/* check that we don't try to decrement 0, which equates
			 * to setting the ttl to 0xff
			 */
			if (match->value.ip_ttl == 0) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Unsupported: we cannot decrement hop_limit past 0");
				return -EOPNOTSUPP;
			}

			/* check that we do not decrement hoplimit twice */
			if (!efx_tc_flower_action_order_ok(act,
							   EFX_TC_AO_DEC_TTL)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Unsupported: multiple dec ttl");
				return -EOPNOTSUPP;
			}

			/* check pedit can be achieved with decrement action */
			tr_ttl = match->value.ip_ttl - 1;
			if ((fa->mangle.val >> 24) == tr_ttl) {
				act->do_ttl_dec = 1;
				return 0;
			}

			fallthrough;
		default:
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Unsupported: only support mangle on the hop_limit field");
			return -EOPNOTSUPP;
		}
	default:
		NL_SET_ERR_MSG_FMT_MOD(extack, "Unhandled mangle htype %u for action rule",
				       fa->mangle.htype);
		return -EOPNOTSUPP;
	}
	return 0;
}

/**
 * efx_tc_incomplete_mangle() - check for leftover partial pedits
 * @mung:	accumulator for partial mangles
 * @extack:	netlink extended ack for reporting errors
 *
 * Since the MAE can only overwrite whole fields, any partial
 * field mangle left over on reaching packet delivery (mirred or
 * end of TC actions) cannot be offloaded.  Check for any such
 * and reject them with -%EOPNOTSUPP.
 */

static int efx_tc_incomplete_mangle(struct efx_tc_mangler_state *mung,
				    struct netlink_ext_ack *extack)
{
	if (mung->dst_mac_32 || mung->dst_mac_16) {
		NL_SET_ERR_MSG_MOD(extack, "Incomplete pedit of destination MAC address");
		return -EOPNOTSUPP;
	}
	if (mung->src_mac_16 || mung->src_mac_32) {
		NL_SET_ERR_MSG_MOD(extack, "Incomplete pedit of source MAC address");
		return -EOPNOTSUPP;
	}
	return 0;
}

static int efx_tc_flower_replace_foreign_lhs_ar(struct efx_nic *efx,
						struct flow_cls_offload *tc,
						struct flow_rule *fr,
						struct efx_tc_match *match,
						struct net_device *net_dev)
{
	struct netlink_ext_ack *extack = tc->common.extack;
	struct efx_tc_lhs_rule *rule, *old;
	enum efx_encap_type type;
	int rc;

	type = efx_tc_indr_netdev_type(net_dev);
	if (type == EFX_ENCAP_TYPE_NONE) {
		NL_SET_ERR_MSG_MOD(extack, "Egress encap match on unsupported tunnel device");
		return -EOPNOTSUPP;
	}

	rc = efx_mae_check_encap_type_supported(efx, type);
	if (rc) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Firmware reports no support for %s encap match",
				       efx_tc_encap_type_name(type));
		return rc;
	}
	/* This is an Action Rule, so it needs a separate Encap Match in the
	 * Outer Rule table.  Insert that now.
	 */
	rc = efx_tc_flower_record_encap_match(efx, match, type,
					      EFX_TC_EM_DIRECT, 0, 0, extack);
	if (rc)
		return rc;

	match->mask.recirc_id = 0xff;
	if (match->mask.ct_state_trk && match->value.ct_state_trk) {
		NL_SET_ERR_MSG_MOD(extack, "LHS rule can never match +trk");
		rc = -EOPNOTSUPP;
		goto release_encap_match;
	}
	/* LHS rules are always -trk, so we don't need to match on that */
	match->mask.ct_state_trk = 0;
	match->value.ct_state_trk = 0;
	/* We must inhibit match on TCP SYN/FIN/RST, so that SW can see
	 * the packet and update the conntrack table.
	 * Outer Rules will do that with CT_TCP_FLAGS_INHIBIT, but Action
	 * Rules don't have that; instead they support matching on
	 * TCP_SYN_FIN_RST (aka TCP_INTERESTING_FLAGS), so use that.
	 * This is only strictly needed if there will be a DO_CT action,
	 * which we don't know yet, but typically there will be and it's
	 * simpler not to bother checking here.
	 */
	match->mask.tcp_syn_fin_rst = true;

	rc = efx_mae_match_check_caps(efx, &match->mask, extack);
	if (rc)
		goto release_encap_match;

	rule = kzalloc(sizeof(*rule), GFP_USER);
	if (!rule) {
		rc = -ENOMEM;
		goto release_encap_match;
	}
	rule->cookie = tc->cookie;
	rule->is_ar = true;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->lhs_rule_ht,
						&rule->linkage,
						efx_tc_lhs_rule_ht_params);
	if (old) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Already offloaded rule (cookie %lx)\n", tc->cookie);
		rc = -EEXIST;
		NL_SET_ERR_MSG_MOD(extack, "Rule already offloaded");
		goto release;
	}

	/* Parse actions */
	rc = efx_tc_flower_handle_lhs_actions(efx, tc, fr, net_dev, rule);
	if (rc)
		goto release;

	rule->match = *match;
	rule->lhs_act.tun_type = type;

	rc = efx_mae_insert_lhs_rule(efx, rule, EFX_TC_PRIO_TC);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to insert rule in hw");
		goto release;
	}
	netif_dbg(efx, drv, efx->net_dev,
		  "Successfully parsed lhs rule (cookie %lx)\n",
		  tc->cookie);
	return 0;

release:
	efx_tc_flower_release_lhs_actions(efx, &rule->lhs_act);
	if (!old)
		rhashtable_remove_fast(&efx->tc->lhs_rule_ht, &rule->linkage,
				       efx_tc_lhs_rule_ht_params);
	kfree(rule);
release_encap_match:
	if (match->encap)
		efx_tc_flower_release_encap_match(efx, match->encap);
	return rc;
}

static int efx_tc_flower_replace_foreign_lhs(struct efx_nic *efx,
					     struct flow_cls_offload *tc,
					     struct flow_rule *fr,
					     struct efx_tc_match *match,
					     struct net_device *net_dev)
{
	struct netlink_ext_ack *extack = tc->common.extack;
	struct efx_tc_lhs_rule *rule, *old;
	enum efx_encap_type type;
	int rc;

	if (tc->common.chain_index) {
		NL_SET_ERR_MSG_MOD(extack, "LHS rule only allowed in chain 0");
		return -EOPNOTSUPP;
	}

	if (!efx_tc_match_is_encap(&match->mask)) {
		/* This is not a tunnel decap rule, ignore it */
		netif_dbg(efx, drv, efx->net_dev, "Ignoring foreign LHS filter without encap match\n");
		return -EOPNOTSUPP;
	}

	if (efx_tc_flower_flhs_needs_ar(match))
		return efx_tc_flower_replace_foreign_lhs_ar(efx, tc, fr, match,
							    net_dev);

	type = efx_tc_indr_netdev_type(net_dev);
	if (type == EFX_ENCAP_TYPE_NONE) {
		NL_SET_ERR_MSG_MOD(extack, "Egress encap match on unsupported tunnel device\n");
		return -EOPNOTSUPP;
	}

	rc = efx_mae_check_encap_type_supported(efx, type);
	if (rc) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Firmware reports no support for %s encap match",
				       efx_tc_encap_type_name(type));
		return rc;
	}
	/* Reserve the outer tuple with a pseudo Encap Match */
	rc = efx_tc_flower_record_encap_match(efx, match, type,
					      EFX_TC_EM_PSEUDO_OR, 0, 0,
					      extack);
	if (rc)
		return rc;

	if (match->mask.ct_state_trk && match->value.ct_state_trk) {
		NL_SET_ERR_MSG_MOD(extack, "LHS rule can never match +trk");
		rc = -EOPNOTSUPP;
		goto release_encap_match;
	}
	/* LHS rules are always -trk, so we don't need to match on that */
	match->mask.ct_state_trk = 0;
	match->value.ct_state_trk = 0;

	rc = efx_tc_flower_translate_flhs_match(match);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "LHS rule cannot match on inner fields");
		goto release_encap_match;
	}

	rc = efx_mae_match_check_caps_lhs(efx, &match->mask, extack);
	if (rc)
		goto release_encap_match;

	rule = kzalloc(sizeof(*rule), GFP_USER);
	if (!rule) {
		rc = -ENOMEM;
		goto release_encap_match;
	}
	rule->cookie = tc->cookie;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->lhs_rule_ht,
						&rule->linkage,
						efx_tc_lhs_rule_ht_params);
	if (old) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Already offloaded rule (cookie %lx)\n", tc->cookie);
		rc = -EEXIST;
		NL_SET_ERR_MSG_MOD(extack, "Rule already offloaded");
		goto release;
	}

	/* Parse actions */
	rc = efx_tc_flower_handle_lhs_actions(efx, tc, fr, net_dev, rule);
	if (rc)
		goto release;

	rule->match = *match;
	rule->lhs_act.tun_type = type;

	rc = efx_mae_insert_lhs_rule(efx, rule, EFX_TC_PRIO_TC);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to insert rule in hw");
		goto release;
	}
	netif_dbg(efx, drv, efx->net_dev,
		  "Successfully parsed lhs rule (cookie %lx)\n",
		  tc->cookie);
	return 0;

release:
	efx_tc_flower_release_lhs_actions(efx, &rule->lhs_act);
	if (!old)
		rhashtable_remove_fast(&efx->tc->lhs_rule_ht, &rule->linkage,
				       efx_tc_lhs_rule_ht_params);
	kfree(rule);
release_encap_match:
	if (match->encap)
		efx_tc_flower_release_encap_match(efx, match->encap);
	return rc;
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
	rc = efx_tc_flower_parse_match(efx, fr, &match, extack);
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

	if (efx_tc_rule_is_lhs_rule(fr, &match))
		return efx_tc_flower_replace_foreign_lhs(efx, tc, fr, &match,
							 net_dev);

	if (tc->common.chain_index) {
		struct efx_tc_recirc_id *rid;

		rid = efx_tc_get_recirc_id(efx, tc->common.chain_index, net_dev);
		if (IS_ERR(rid)) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Failed to allocate a hardware recirculation ID for chain_index %u",
					       tc->common.chain_index);
			return PTR_ERR(rid);
		}
		match.rid = rid;
		match.value.recirc_id = rid->fw_id;
	}
	match.mask.recirc_id = 0xff;

	/* AR table can't match on DO_CT (+trk).  But a commonly used pattern is
	 * +trk+est, which is strictly implied by +est, so rewrite it to that.
	 */
	if (match.mask.ct_state_trk && match.value.ct_state_trk &&
	    match.mask.ct_state_est && match.value.ct_state_est)
		match.mask.ct_state_trk = 0;
	/* Thanks to CT_TCP_FLAGS_INHIBIT, packets with interesting flags could
	 * match +trk-est (CT_HIT=0) despite being on an established connection.
	 * So make -est imply -tcp_syn_fin_rst match to ensure these packets
	 * still hit the software path.
	 */
	if (match.mask.ct_state_est && !match.value.ct_state_est) {
		if (match.value.tcp_syn_fin_rst) {
			/* Can't offload this combination */
			NL_SET_ERR_MSG_MOD(extack, "TCP flags and -est conflict for offload");
			rc = -EOPNOTSUPP;
			goto release;
		}
		match.mask.tcp_syn_fin_rst = true;
	}

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
		rc = -EOPNOTSUPP;
		goto release;
	}

	rc = efx_mae_match_check_caps(efx, &match.mask, extack);
	if (rc)
		goto release;

	if (efx_tc_match_is_encap(&match.mask)) {
		enum efx_encap_type type;

		type = efx_tc_indr_netdev_type(net_dev);
		if (type == EFX_ENCAP_TYPE_NONE) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Egress encap match on unsupported tunnel device");
			rc = -EOPNOTSUPP;
			goto release;
		}

		rc = efx_mae_check_encap_type_supported(efx, type);
		if (rc) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Firmware reports no support for %s encap match",
					       efx_tc_encap_type_name(type));
			goto release;
		}

		rc = efx_tc_flower_record_encap_match(efx, &match, type,
						      EFX_TC_EM_DIRECT, 0, 0,
						      extack);
		if (rc)
			goto release;
	} else if (!tc->common.chain_index) {
		/* This is not a tunnel decap rule, ignore it */
		netif_dbg(efx, drv, efx->net_dev,
			  "Ignoring foreign filter without encap match\n");
		rc = -EOPNOTSUPP;
		goto release;
	}

	rule = kzalloc(sizeof(*rule), GFP_USER);
	if (!rule) {
		rc = -ENOMEM;
		goto release;
	}
	INIT_LIST_HEAD(&rule->acts.list);
	rule->cookie = tc->cookie;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->match_action_ht,
						&rule->linkage,
						efx_tc_match_action_ht_params);
	if (IS_ERR(old)) {
		rc = PTR_ERR(old);
		goto release;
	} else if (old) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Ignoring already-offloaded rule (cookie %lx)\n",
			  tc->cookie);
		rc = -EEXIST;
		goto release;
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
					NL_SET_ERR_MSG_MOD(extack, "Count action violates action order (can't happen)");
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
				INIT_LIST_HEAD(&act->count_user);
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
	if (match.rid)
		efx_tc_put_recirc_id(efx, match.rid);
	if (act)
		efx_tc_free_action_set(efx, act, false);
	if (rule) {
		if (!old)
			rhashtable_remove_fast(&efx->tc->match_action_ht,
					       &rule->linkage,
					       efx_tc_match_action_ht_params);
		efx_tc_free_action_set_list(efx, &rule->acts, false);
	}
	kfree(rule);
	if (match.encap)
		efx_tc_flower_release_encap_match(efx, match.encap);
	return rc;
}

static int efx_tc_flower_replace_lhs(struct efx_nic *efx,
				     struct flow_cls_offload *tc,
				     struct flow_rule *fr,
				     struct efx_tc_match *match,
				     struct efx_rep *efv,
				     struct net_device *net_dev)
{
	struct netlink_ext_ack *extack = tc->common.extack;
	struct efx_tc_lhs_rule *rule, *old;
	int rc;

	if (tc->common.chain_index) {
		NL_SET_ERR_MSG_MOD(extack, "LHS rule only allowed in chain 0");
		return -EOPNOTSUPP;
	}

	if (match->mask.ct_state_trk && match->value.ct_state_trk) {
		NL_SET_ERR_MSG_MOD(extack, "LHS rule can never match +trk");
		return -EOPNOTSUPP;
	}
	/* LHS rules are always -trk, so we don't need to match on that */
	match->mask.ct_state_trk = 0;
	match->value.ct_state_trk = 0;

	rc = efx_mae_match_check_caps_lhs(efx, &match->mask, extack);
	if (rc)
		return rc;

	rule = kzalloc(sizeof(*rule), GFP_USER);
	if (!rule)
		return -ENOMEM;
	rule->cookie = tc->cookie;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->lhs_rule_ht,
						&rule->linkage,
						efx_tc_lhs_rule_ht_params);
	if (IS_ERR(old)) {
		rc = PTR_ERR(old);
		goto release;
	} else if (old) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Already offloaded rule (cookie %lx)\n", tc->cookie);
		rc = -EEXIST;
		NL_SET_ERR_MSG_MOD(extack, "Rule already offloaded");
		goto release;
	}

	/* Parse actions */
	/* See note in efx_tc_flower_replace() regarding passed net_dev
	 * (used for efx_tc_get_recirc_id()).
	 */
	rc = efx_tc_flower_handle_lhs_actions(efx, tc, fr, efx->net_dev, rule);
	if (rc)
		goto release;

	rule->match = *match;

	rc = efx_mae_insert_lhs_rule(efx, rule, EFX_TC_PRIO_TC);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to insert rule in hw");
		goto release;
	}
	netif_dbg(efx, drv, efx->net_dev,
		  "Successfully parsed lhs rule (cookie %lx)\n",
		  tc->cookie);
	return 0;

release:
	efx_tc_flower_release_lhs_actions(efx, &rule->lhs_act);
	if (!old)
		rhashtable_remove_fast(&efx->tc->lhs_rule_ht, &rule->linkage,
				       efx_tc_lhs_rule_ht_params);
	kfree(rule);
	return rc;
}

static int efx_tc_flower_replace(struct efx_nic *efx,
				 struct net_device *net_dev,
				 struct flow_cls_offload *tc,
				 struct efx_rep *efv)
{
	struct flow_rule *fr = flow_cls_offload_flow_rule(tc);
	struct netlink_ext_ack *extack = tc->common.extack;
	const struct ip_tunnel_info *encap_info = NULL;
	struct efx_tc_flow_rule *rule = NULL, *old;
	struct efx_tc_mangler_state mung = {};
	struct efx_tc_action_set *act = NULL;
	const struct flow_action_entry *fa;
	struct efx_rep *from_efv, *to_efv;
	struct efx_tc_match match;
	u32 acts_id;
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

	if (efx_tc_rule_is_lhs_rule(fr, &match))
		return efx_tc_flower_replace_lhs(efx, tc, fr, &match, efv,
						 net_dev);

	/* chain_index 0 is always recirc_id 0 (and does not appear in recirc_ht).
	 * Conveniently, match.rid == NULL and match.value.recirc_id == 0 owing
	 * to the initial memset(), so we don't need to do anything in that case.
	 */
	if (tc->common.chain_index) {
		struct efx_tc_recirc_id *rid;

		/* Note regarding passed net_dev:
		 * VFreps and PF can share chain namespace, as they have
		 * distinct ingress_mports.  So we don't need to burn an
		 * extra recirc_id if both use the same chain_index.
		 * (Strictly speaking, we could give each VFrep its own
		 * recirc_id namespace that doesn't take IDs away from the
		 * PF, but that would require a bunch of additional IDAs -
		 * one for each representor - and that's not likely to be
		 * the main cause of recirc_id exhaustion anyway.)
		 */
		rid = efx_tc_get_recirc_id(efx, tc->common.chain_index,
					   efx->net_dev);
		if (IS_ERR(rid)) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "Failed to allocate a hardware recirculation ID for chain_index %u",
					       tc->common.chain_index);
			return PTR_ERR(rid);
		}
		match.rid = rid;
		match.value.recirc_id = rid->fw_id;
	}
	match.mask.recirc_id = 0xff;

	/* AR table can't match on DO_CT (+trk).  But a commonly used pattern is
	 * +trk+est, which is strictly implied by +est, so rewrite it to that.
	 */
	if (match.mask.ct_state_trk && match.value.ct_state_trk &&
	    match.mask.ct_state_est && match.value.ct_state_est)
		match.mask.ct_state_trk = 0;
	/* Thanks to CT_TCP_FLAGS_INHIBIT, packets with interesting flags could
	 * match +trk-est (CT_HIT=0) despite being on an established connection.
	 * So make -est imply -tcp_syn_fin_rst match to ensure these packets
	 * still hit the software path.
	 */
	if (match.mask.ct_state_est && !match.value.ct_state_est) {
		if (match.value.tcp_syn_fin_rst) {
			/* Can't offload this combination */
			rc = -EOPNOTSUPP;
			goto release;
		}
		match.mask.tcp_syn_fin_rst = true;
	}

	rc = efx_mae_match_check_caps(efx, &match.mask, extack);
	if (rc)
		goto release;

	rule = kzalloc(sizeof(*rule), GFP_USER);
	if (!rule) {
		rc = -ENOMEM;
		goto release;
	}
	INIT_LIST_HEAD(&rule->acts.list);
	rule->cookie = tc->cookie;
	old = rhashtable_lookup_get_insert_fast(&efx->tc->match_action_ht,
						&rule->linkage,
						efx_tc_match_action_ht_params);
	if (IS_ERR(old)) {
		rc = PTR_ERR(old);
		goto release;
	} else if (old) {
		netif_dbg(efx, drv, efx->net_dev,
			  "Already offloaded rule (cookie %lx)\n", tc->cookie);
		NL_SET_ERR_MSG_MOD(extack, "Rule already offloaded");
		rc = -EEXIST;
		goto release;
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
			INIT_LIST_HEAD(&act->count_user);
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

			if (encap_info) {
				struct efx_tc_encap_action *encap;

				if (!efx_tc_flower_action_order_ok(act,
								   EFX_TC_AO_ENCAP)) {
					rc = -EOPNOTSUPP;
					NL_SET_ERR_MSG_MOD(extack, "Encap action violates action order");
					goto release;
				}
				encap = efx_tc_flower_create_encap_md(
						efx, encap_info, fa->dev, extack);
				if (IS_ERR_OR_NULL(encap)) {
					rc = PTR_ERR(encap);
					if (!rc)
						rc = -EIO; /* arbitrary */
					goto release;
				}
				act->encap_md = encap;
				list_add_tail(&act->encap_user, &encap->users);
				act->dest_mport = encap->dest_mport;
				act->deliver = 1;
				if (act->count && !WARN_ON(!act->count->cnt)) {
					/* This counter is used by an encap
					 * action, which needs a reference back
					 * so it can prod neighbouring whenever
					 * traffic is seen.
					 */
					spin_lock_bh(&act->count->cnt->lock);
					list_add_tail(&act->count_user,
						      &act->count->cnt->users);
					spin_unlock_bh(&act->count->cnt->lock);
				}
				rc = efx_mae_alloc_action_set(efx, act);
				if (rc) {
					NL_SET_ERR_MSG_MOD(extack, "Failed to write action set to hw (encap)");
					goto release;
				}
				list_add_tail(&act->list, &rule->acts.list);
				act->user = &rule->acts;
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
			}

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
		case FLOW_ACTION_ADD:
			rc = efx_tc_pedit_add(efx, act, fa, extack);
			if (rc < 0)
				goto release;
			break;
		case FLOW_ACTION_MANGLE:
			rc = efx_tc_mangle(efx, act, fa, &mung, extack, &match);
			if (rc < 0)
				goto release;
			break;
		case FLOW_ACTION_TUNNEL_ENCAP:
			if (encap_info) {
				/* Can't specify encap multiple times.
				 * If you want to overwrite an existing
				 * encap_info, use an intervening
				 * FLOW_ACTION_TUNNEL_DECAP to clear it.
				 */
				NL_SET_ERR_MSG_MOD(extack, "Tunnel key set when already set");
				rc = -EINVAL;
				goto release;
			}
			if (!fa->tunnel) {
				NL_SET_ERR_MSG_MOD(extack, "Tunnel key set is missing key");
				rc = -EOPNOTSUPP;
				goto release;
			}
			encap_info = fa->tunnel;
			break;
		case FLOW_ACTION_TUNNEL_DECAP:
			if (encap_info) {
				encap_info = NULL;
				break;
			}
			/* Since we don't support enc_key matches on ingress
			 * (and if we did there'd be no tunnel-device to give
			 * us a type), we can't offload a decap that's not
			 * just undoing a previous encap action.
			 */
			NL_SET_ERR_MSG_MOD(extack, "Cannot offload tunnel decap action without tunnel device");
			rc = -EOPNOTSUPP;
			goto release;
		case FLOW_ACTION_CT:
			if (fa->ct.action != TCA_CT_ACT_NAT) {
				rc = -EOPNOTSUPP;
				NL_SET_ERR_MSG_FMT_MOD(extack, "Can only offload CT 'nat' action in RHS rules, not %d", fa->ct.action);
				goto release;
			}
			act->do_nat = 1;
			break;
		default:
			NL_SET_ERR_MSG_FMT_MOD(extack, "Unhandled action %u",
					       fa->id);
			rc = -EOPNOTSUPP;
			goto release;
		}
	}

	rc = efx_tc_incomplete_mangle(&mung, extack);
	if (rc < 0)
		goto release;
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
	if (from_efv == EFX_EFV_PF)
		/* PF netdev, so rule applies to traffic from wire */
		rule->fallback = &efx->tc->facts.pf;
	else
		/* repdev, so rule applies to traffic from representee */
		rule->fallback = &efx->tc->facts.reps;
	if (!efx_tc_check_ready(efx, rule)) {
		netif_dbg(efx, drv, efx->net_dev, "action not ready for hw\n");
		acts_id = rule->fallback->fw_id;
	} else {
		netif_dbg(efx, drv, efx->net_dev, "ready for hw\n");
		acts_id = rule->acts.fw_id;
	}
	rc = efx_mae_insert_rule(efx, &rule->match, EFX_TC_PRIO_TC,
				 acts_id, &rule->fw_id);
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
	if (match.rid)
		efx_tc_put_recirc_id(efx, match.rid);
	if (act)
		efx_tc_free_action_set(efx, act, false);
	if (rule) {
		if (!old)
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
	struct efx_tc_lhs_rule *lhs_rule;
	struct efx_tc_flow_rule *rule;

	lhs_rule = rhashtable_lookup_fast(&efx->tc->lhs_rule_ht, &tc->cookie,
					  efx_tc_lhs_rule_ht_params);
	if (lhs_rule) {
		/* Remove it from HW */
		efx_mae_remove_lhs_rule(efx, lhs_rule);
		/* Delete it from SW */
		efx_tc_flower_release_lhs_actions(efx, &lhs_rule->lhs_act);
		rhashtable_remove_fast(&efx->tc->lhs_rule_ht, &lhs_rule->linkage,
				       efx_tc_lhs_rule_ht_params);
		if (lhs_rule->match.encap)
			efx_tc_flower_release_encap_match(efx, lhs_rule->match.encap);
		netif_dbg(efx, drv, efx->net_dev, "Removed (lhs) filter %lx\n",
			  lhs_rule->cookie);
		kfree(lhs_rule);
		return 0;
	}

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

static int efx_tc_configure_fallback_acts(struct efx_nic *efx, u32 eg_port,
					  struct efx_tc_action_set_list *acts)
{
	struct efx_tc_action_set *act;
	int rc;

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
	return 0;
fail2:
	list_del(&act->list);
	efx_mae_free_action_set(efx, act->fw_id);
fail1:
	kfree(act);
	return rc;
}

static int efx_tc_configure_fallback_acts_pf(struct efx_nic *efx)
{
	struct efx_tc_action_set_list *acts = &efx->tc->facts.pf;
	u32 eg_port;

	efx_mae_mport_uplink(efx, &eg_port);
	return efx_tc_configure_fallback_acts(efx, eg_port, acts);
}

static int efx_tc_configure_fallback_acts_reps(struct efx_nic *efx)
{
	struct efx_tc_action_set_list *acts = &efx->tc->facts.reps;
	u32 eg_port;

	efx_mae_mport_mport(efx, efx->tc->reps_mport_id, &eg_port);
	return efx_tc_configure_fallback_acts(efx, eg_port, acts);
}

static void efx_tc_deconfigure_fallback_acts(struct efx_nic *efx,
					     struct efx_tc_action_set_list *acts)
{
	efx_tc_free_action_set_list(efx, acts, true);
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
	rc = efx_tc_configure_fallback_acts_pf(efx);
	if (rc)
		return rc;
	rc = efx_tc_configure_fallback_acts_reps(efx);
	if (rc)
		return rc;
	rc = efx_mae_get_tables(efx);
	if (rc)
		return rc;
	rc = flow_indr_dev_register(efx_tc_indr_setup_cb, efx);
	if (rc)
		goto out_free;
	efx->tc->up = true;
	return 0;
out_free:
	efx_mae_free_tables(efx);
	return rc;
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
	efx_tc_deconfigure_fallback_acts(efx, &efx->tc->facts.pf);
	efx_tc_deconfigure_fallback_acts(efx, &efx->tc->facts.reps);
	efx->tc->up = false;
	efx_mae_free_tables(efx);
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

static void efx_tc_recirc_free(void *ptr, void *arg)
{
	struct efx_tc_recirc_id *rid = ptr;
	struct efx_nic *efx = arg;

	WARN_ON(refcount_read(&rid->ref));
	ida_free(&efx->tc->recirc_ida, rid->fw_id);
	kfree(rid);
}

static void efx_tc_lhs_free(void *ptr, void *arg)
{
	struct efx_tc_lhs_rule *rule = ptr;
	struct efx_nic *efx = arg;

	netif_err(efx, drv, efx->net_dev,
		  "tc lhs_rule %lx still present at teardown, removing\n",
		  rule->cookie);

	if (rule->lhs_act.zone)
		efx_tc_ct_unregister_zone(efx, rule->lhs_act.zone);
	if (rule->lhs_act.count)
		efx_tc_flower_put_counter_index(efx, rule->lhs_act.count);
	efx_mae_remove_lhs_rule(efx, rule);

	kfree(rule);
}

static void efx_tc_mac_free(void *ptr, void *__unused)
{
	struct efx_tc_mac_pedit_action *ped = ptr;

	WARN_ON(refcount_read(&ped->ref));
	kfree(ped);
}

static void efx_tc_flow_free(void *ptr, void *arg)
{
	struct efx_tc_flow_rule *rule = ptr;
	struct efx_nic *efx = arg;

	netif_err(efx, drv, efx->net_dev,
		  "tc rule %lx still present at teardown, removing\n",
		  rule->cookie);

	/* Also releases entries in subsidiary tables */
	efx_tc_delete_rule(efx, rule);

	kfree(rule);
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
	rc = efx_tc_init_encap_actions(efx);
	if (rc < 0)
		goto fail_encap_actions;
	rc = efx_tc_init_counters(efx);
	if (rc < 0)
		goto fail_counters;
	rc = rhashtable_init(&efx->tc->mac_ht, &efx_tc_mac_ht_params);
	if (rc < 0)
		goto fail_mac_ht;
	rc = rhashtable_init(&efx->tc->encap_match_ht, &efx_tc_encap_match_ht_params);
	if (rc < 0)
		goto fail_encap_match_ht;
	rc = rhashtable_init(&efx->tc->match_action_ht, &efx_tc_match_action_ht_params);
	if (rc < 0)
		goto fail_match_action_ht;
	rc = rhashtable_init(&efx->tc->lhs_rule_ht, &efx_tc_lhs_rule_ht_params);
	if (rc < 0)
		goto fail_lhs_rule_ht;
	rc = efx_tc_init_conntrack(efx);
	if (rc < 0)
		goto fail_conntrack;
	rc = rhashtable_init(&efx->tc->recirc_ht, &efx_tc_recirc_ht_params);
	if (rc < 0)
		goto fail_recirc_ht;
	ida_init(&efx->tc->recirc_ida);
	efx->tc->reps_filter_uc = -1;
	efx->tc->reps_filter_mc = -1;
	INIT_LIST_HEAD(&efx->tc->dflt.pf.acts.list);
	efx->tc->dflt.pf.fw_id = MC_CMD_MAE_ACTION_RULE_INSERT_OUT_ACTION_RULE_ID_NULL;
	INIT_LIST_HEAD(&efx->tc->dflt.wire.acts.list);
	efx->tc->dflt.wire.fw_id = MC_CMD_MAE_ACTION_RULE_INSERT_OUT_ACTION_RULE_ID_NULL;
	INIT_LIST_HEAD(&efx->tc->facts.pf.list);
	efx->tc->facts.pf.fw_id = MC_CMD_MAE_ACTION_SET_ALLOC_OUT_ACTION_SET_ID_NULL;
	INIT_LIST_HEAD(&efx->tc->facts.reps.list);
	efx->tc->facts.reps.fw_id = MC_CMD_MAE_ACTION_SET_ALLOC_OUT_ACTION_SET_ID_NULL;
	efx->extra_channel_type[EFX_EXTRA_CHANNEL_TC] = &efx_tc_channel_type;
	return 0;
fail_recirc_ht:
	efx_tc_destroy_conntrack(efx);
fail_conntrack:
	rhashtable_destroy(&efx->tc->lhs_rule_ht);
fail_lhs_rule_ht:
	rhashtable_destroy(&efx->tc->match_action_ht);
fail_match_action_ht:
	rhashtable_destroy(&efx->tc->encap_match_ht);
fail_encap_match_ht:
	rhashtable_destroy(&efx->tc->mac_ht);
fail_mac_ht:
	efx_tc_destroy_counters(efx);
fail_counters:
	efx_tc_destroy_encap_actions(efx);
fail_encap_actions:
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
	EFX_WARN_ON_PARANOID(efx->tc->facts.pf.fw_id !=
			     MC_CMD_MAE_ACTION_SET_LIST_ALLOC_OUT_ACTION_SET_LIST_ID_NULL);
	EFX_WARN_ON_PARANOID(efx->tc->facts.reps.fw_id !=
			     MC_CMD_MAE_ACTION_SET_LIST_ALLOC_OUT_ACTION_SET_LIST_ID_NULL);
	rhashtable_free_and_destroy(&efx->tc->lhs_rule_ht, efx_tc_lhs_free, efx);
	rhashtable_free_and_destroy(&efx->tc->match_action_ht, efx_tc_flow_free,
				    efx);
	rhashtable_free_and_destroy(&efx->tc->encap_match_ht,
				    efx_tc_encap_match_free, NULL);
	efx_tc_fini_conntrack(efx);
	rhashtable_free_and_destroy(&efx->tc->recirc_ht, efx_tc_recirc_free, efx);
	WARN_ON(!ida_is_empty(&efx->tc->recirc_ida));
	ida_destroy(&efx->tc->recirc_ida);
	rhashtable_free_and_destroy(&efx->tc->mac_ht, efx_tc_mac_free, NULL);
	efx_tc_fini_counters(efx);
	efx_tc_fini_encap_actions(efx);
	mutex_unlock(&efx->tc->mutex);
	mutex_destroy(&efx->tc->mutex);
	kfree(efx->tc->caps);
	kfree(efx->tc);
	efx->tc = NULL;
}
