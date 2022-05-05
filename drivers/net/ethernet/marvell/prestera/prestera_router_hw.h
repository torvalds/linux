/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2019-2021 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_ROUTER_HW_H_
#define _PRESTERA_ROUTER_HW_H_

struct prestera_vr {
	struct list_head router_node;
	refcount_t refcount;
	u32 tb_id;			/* key (kernel fib table id) */
	u16 hw_vr_id;			/* virtual router ID */
	u8 __pad[2];
};

struct prestera_rif_entry {
	struct prestera_rif_entry_key {
		struct prestera_iface iface;
	} key;
	struct prestera_vr *vr;
	unsigned char addr[ETH_ALEN];
	u16 hw_id; /* rif_id */
	struct list_head router_node; /* ht */
};

struct prestera_ip_addr {
	union {
		__be32 ipv4;
		struct in6_addr ipv6;
	} u;
	enum {
		PRESTERA_IPV4 = 0,
		PRESTERA_IPV6
	} v;
};

struct prestera_fib_key {
	struct prestera_ip_addr addr;
	u32 prefix_len;
	u32 tb_id;
};

struct prestera_fib_info {
	struct prestera_vr *vr;
	struct list_head vr_node;
	enum prestera_fib_type {
		PRESTERA_FIB_TYPE_INVALID = 0,
		/* It can be connected route
		 * and will be overlapped with neighbours
		 */
		PRESTERA_FIB_TYPE_TRAP,
		PRESTERA_FIB_TYPE_DROP
	} type;
};

struct prestera_fib_node {
	struct rhash_head ht_node; /* node of prestera_vr */
	struct prestera_fib_key key;
	struct prestera_fib_info info; /* action related info */
};

struct prestera_rif_entry *
prestera_rif_entry_find(const struct prestera_switch *sw,
			const struct prestera_rif_entry_key *k);
void prestera_rif_entry_destroy(struct prestera_switch *sw,
				struct prestera_rif_entry *e);
struct prestera_rif_entry *
prestera_rif_entry_create(struct prestera_switch *sw,
			  struct prestera_rif_entry_key *k,
			  u32 tb_id, const unsigned char *addr);
struct prestera_fib_node *prestera_fib_node_find(struct prestera_switch *sw,
						 struct prestera_fib_key *key);
void prestera_fib_node_destroy(struct prestera_switch *sw,
			       struct prestera_fib_node *fib_node);
struct prestera_fib_node *
prestera_fib_node_create(struct prestera_switch *sw,
			 struct prestera_fib_key *key,
			 enum prestera_fib_type fib_type);
int prestera_router_hw_init(struct prestera_switch *sw);
void prestera_router_hw_fini(struct prestera_switch *sw);

#endif /* _PRESTERA_ROUTER_HW_H_ */
