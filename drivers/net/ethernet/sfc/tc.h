/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 * Copyright 2020-2022 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_TC_H
#define EFX_TC_H
#include <net/flow_offload.h>
#include <linux/rhashtable.h>
#include "net_driver.h"
#include "tc_counters.h"

#define IS_ALL_ONES(v)	(!(typeof (v))~(v))

#ifdef CONFIG_IPV6
static inline bool efx_ipv6_addr_all_ones(struct in6_addr *addr)
{
	return !memchr_inv(addr, 0xff, sizeof(*addr));
}
#endif

struct efx_tc_encap_action; /* see tc_encap_actions.h */

struct efx_tc_action_set {
	u16 vlan_push:2;
	u16 vlan_pop:2;
	u16 decap:1;
	u16 deliver:1;
	__be16 vlan_tci[2]; /* TCIs for vlan_push */
	__be16 vlan_proto[2]; /* Ethertypes for vlan_push */
	struct efx_tc_counter_index *count;
	struct efx_tc_encap_action *encap_md; /* entry in tc_encap_ht table */
	struct list_head encap_user; /* entry on encap_md->users list */
	struct efx_tc_action_set_list *user; /* Only populated if encap_md */
	struct list_head count_user; /* entry on counter->users list, if encap */
	u32 dest_mport;
	u32 fw_id; /* index of this entry in firmware actions table */
	struct list_head list;
};

struct efx_tc_match_fields {
	/* L1 */
	u32 ingress_port;
	u8 recirc_id;
	/* L2 (inner when encap) */
	__be16 eth_proto;
	__be16 vlan_tci[2], vlan_proto[2];
	u8 eth_saddr[ETH_ALEN], eth_daddr[ETH_ALEN];
	/* L3 (when IP) */
	u8 ip_proto, ip_tos, ip_ttl;
	__be32 src_ip, dst_ip;
#ifdef CONFIG_IPV6
	struct in6_addr src_ip6, dst_ip6;
#endif
	bool ip_frag, ip_firstfrag;
	/* L4 */
	__be16 l4_sport, l4_dport; /* Ports (UDP, TCP) */
	__be16 tcp_flags;
	/* Encap.  The following are *outer* fields.  Note that there are no
	 * outer eth (L2) fields; this is because TC doesn't have them.
	 */
	__be32 enc_src_ip, enc_dst_ip;
	struct in6_addr enc_src_ip6, enc_dst_ip6;
	u8 enc_ip_tos, enc_ip_ttl;
	__be16 enc_sport, enc_dport;
	__be32 enc_keyid; /* e.g. VNI, VSID */
};

static inline bool efx_tc_match_is_encap(const struct efx_tc_match_fields *mask)
{
	return mask->enc_src_ip || mask->enc_dst_ip ||
	       !ipv6_addr_any(&mask->enc_src_ip6) ||
	       !ipv6_addr_any(&mask->enc_dst_ip6) || mask->enc_ip_tos ||
	       mask->enc_ip_ttl || mask->enc_sport || mask->enc_dport;
}

/**
 * enum efx_tc_em_pseudo_type - &struct efx_tc_encap_match pseudo type
 *
 * These are used to classify "pseudo" encap matches, which don't refer
 * to an entry in hardware but rather indicate that a section of the
 * match space is in use by another Outer Rule.
 *
 * @EFX_TC_EM_DIRECT: real HW entry in Outer Rule table; not a pseudo.
 *	Hardware index in &struct efx_tc_encap_match.fw_id is valid.
 * @EFX_TC_EM_PSEUDO_MASK: registered by an encap match which includes a
 *	match on an optional field (currently ip_tos and/or udp_sport),
 *	to prevent an overlapping encap match _without_ optional fields.
 *	The pseudo encap match may be referenced again by an encap match
 *	with different values for these fields, but all masks must match the
 *	first (stored in our child_* fields).
 */
enum efx_tc_em_pseudo_type {
	EFX_TC_EM_DIRECT,
	EFX_TC_EM_PSEUDO_MASK,
};

struct efx_tc_encap_match {
	__be32 src_ip, dst_ip;
	struct in6_addr src_ip6, dst_ip6;
	__be16 udp_dport;
	__be16 udp_sport, udp_sport_mask;
	u8 ip_tos, ip_tos_mask;
	struct rhash_head linkage;
	enum efx_encap_type tun_type;
	u8 child_ip_tos_mask;
	__be16 child_udp_sport_mask;
	refcount_t ref;
	enum efx_tc_em_pseudo_type type;
	u32 fw_id; /* index of this entry in firmware encap match table */
	struct efx_tc_encap_match *pseudo; /* Referenced pseudo EM if needed */
};

struct efx_tc_match {
	struct efx_tc_match_fields value;
	struct efx_tc_match_fields mask;
	struct efx_tc_encap_match *encap;
};

struct efx_tc_action_set_list {
	struct list_head list;
	u32 fw_id;
};

struct efx_tc_flow_rule {
	unsigned long cookie;
	struct rhash_head linkage;
	struct efx_tc_match match;
	struct efx_tc_action_set_list acts;
	struct efx_tc_action_set_list *fallback; /* what to use when unready? */
	u32 fw_id;
};

enum efx_tc_rule_prios {
	EFX_TC_PRIO_TC, /* Rule inserted by TC */
	EFX_TC_PRIO_DFLT, /* Default switch rule; one of efx_tc_default_rules */
	EFX_TC_PRIO__NUM
};

/**
 * struct efx_tc_state - control plane data for TC offload
 *
 * @caps: MAE capabilities reported by MCDI
 * @block_list: List of &struct efx_tc_block_binding
 * @mutex: Used to serialise operations on TC hashtables
 * @counter_ht: Hashtable of TC counters (FW IDs and counter values)
 * @counter_id_ht: Hashtable mapping TC counter cookies to counters
 * @encap_ht: Hashtable of TC encap actions
 * @encap_match_ht: Hashtable of TC encap matches
 * @match_action_ht: Hashtable of TC match-action rules
 * @neigh_ht: Hashtable of neighbour watches (&struct efx_neigh_binder)
 * @reps_mport_id: MAE port allocated for representor RX
 * @reps_filter_uc: VNIC filter for representor unicast RX (promisc)
 * @reps_filter_mc: VNIC filter for representor multicast RX (allmulti)
 * @reps_mport_vport_id: vport_id for representor RX filters
 * @flush_counters: counters have been stopped, waiting for drain
 * @flush_gen: final generation count per type array as reported by
 *             MC_CMD_MAE_COUNTERS_STREAM_STOP
 * @seen_gen: most recent generation count per type as seen by efx_tc_rx()
 * @flush_wq: wait queue used by efx_mae_stop_counters() to wait for
 *	MAE counters RXQ to finish draining
 * @dflt: Match-action rules for default switching; at priority
 *	%EFX_TC_PRIO_DFLT.  Named by *ingress* port
 * @dflt.pf: rule for traffic ingressing from PF (egresses to wire)
 * @dflt.wire: rule for traffic ingressing from wire (egresses to PF)
 * @facts: Fallback action-set-lists for unready rules.  Named by *egress* port
 * @facts.pf: action-set-list for unready rules on PF netdev, hence applying to
 *	traffic from wire, and egressing to PF
 * @facts.reps: action-set-list for unready rules on representors, hence
 *	applying to traffic from representees, and egressing to the reps mport
 * @up: have TC datastructures been set up?
 */
struct efx_tc_state {
	struct mae_caps *caps;
	struct list_head block_list;
	struct mutex mutex;
	struct rhashtable counter_ht;
	struct rhashtable counter_id_ht;
	struct rhashtable encap_ht;
	struct rhashtable encap_match_ht;
	struct rhashtable match_action_ht;
	struct rhashtable neigh_ht;
	u32 reps_mport_id, reps_mport_vport_id;
	s32 reps_filter_uc, reps_filter_mc;
	bool flush_counters;
	u32 flush_gen[EFX_TC_COUNTER_TYPE_MAX];
	u32 seen_gen[EFX_TC_COUNTER_TYPE_MAX];
	wait_queue_head_t flush_wq;
	struct {
		struct efx_tc_flow_rule pf;
		struct efx_tc_flow_rule wire;
	} dflt;
	struct {
		struct efx_tc_action_set_list pf;
		struct efx_tc_action_set_list reps;
	} facts;
	bool up;
};

struct efx_rep;

enum efx_encap_type efx_tc_indr_netdev_type(struct net_device *net_dev);
struct efx_rep *efx_tc_flower_lookup_efv(struct efx_nic *efx,
					 struct net_device *dev);
s64 efx_tc_flower_external_mport(struct efx_nic *efx, struct efx_rep *efv);
int efx_tc_configure_default_rule_rep(struct efx_rep *efv);
void efx_tc_deconfigure_default_rule(struct efx_nic *efx,
				     struct efx_tc_flow_rule *rule);
int efx_tc_flower(struct efx_nic *efx, struct net_device *net_dev,
		  struct flow_cls_offload *tc, struct efx_rep *efv);

int efx_tc_insert_rep_filters(struct efx_nic *efx);
void efx_tc_remove_rep_filters(struct efx_nic *efx);

int efx_init_tc(struct efx_nic *efx);
void efx_fini_tc(struct efx_nic *efx);

int efx_init_struct_tc(struct efx_nic *efx);
void efx_fini_struct_tc(struct efx_nic *efx);

#endif /* EFX_TC_H */
