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

struct prestera_rif_entry *
prestera_rif_entry_find(const struct prestera_switch *sw,
			const struct prestera_rif_entry_key *k);
void prestera_rif_entry_destroy(struct prestera_switch *sw,
				struct prestera_rif_entry *e);
struct prestera_rif_entry *
prestera_rif_entry_create(struct prestera_switch *sw,
			  struct prestera_rif_entry_key *k,
			  u32 tb_id, const unsigned char *addr);
int prestera_router_hw_init(struct prestera_switch *sw);
void prestera_router_hw_fini(struct prestera_switch *sw);

#endif /* _PRESTERA_ROUTER_HW_H_ */
