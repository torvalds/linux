/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2023, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_TC_CONNTRACK_H
#define EFX_TC_CONNTRACK_H
#include "net_driver.h"

#if IS_ENABLED(CONFIG_SFC_SRIOV)
#include <linux/refcount.h>
#include <net/netfilter/nf_flow_table.h>

struct efx_tc_ct_zone {
	u16 zone;
	struct rhash_head linkage;
	refcount_t ref;
	struct nf_flowtable *nf_ft;
	struct efx_nic *efx;
	struct mutex mutex; /* protects cts list */
	struct list_head cts; /* list of efx_tc_ct_entry in this zone */
};

/* create/uncreate/teardown hashtables */
int efx_tc_init_conntrack(struct efx_nic *efx);
void efx_tc_destroy_conntrack(struct efx_nic *efx);
void efx_tc_fini_conntrack(struct efx_nic *efx);

struct efx_tc_ct_zone *efx_tc_ct_register_zone(struct efx_nic *efx, u16 zone,
					       struct nf_flowtable *ct_ft);
void efx_tc_ct_unregister_zone(struct efx_nic *efx,
			       struct efx_tc_ct_zone *ct_zone);

struct efx_tc_ct_entry {
	unsigned long cookie;
	struct rhash_head linkage;
	__be16 eth_proto;
	u8 ip_proto;
	bool dnat;
	__be32 src_ip, dst_ip, nat_ip;
	struct in6_addr src_ip6, dst_ip6;
	__be16 l4_sport, l4_dport, l4_natport; /* Ports (UDP, TCP) */
	struct efx_tc_ct_zone *zone;
	u32 mark;
	struct efx_tc_counter *cnt;
	struct list_head list; /* entry on zone->cts */
};

#endif /* CONFIG_SFC_SRIOV */
#endif /* EFX_TC_CONNTRACK_H */
