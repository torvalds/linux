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
};

/* create/teardown hashtables */
int efx_tc_init_conntrack(struct efx_nic *efx);
void efx_tc_fini_conntrack(struct efx_nic *efx);

struct efx_tc_ct_zone *efx_tc_ct_register_zone(struct efx_nic *efx, u16 zone,
					       struct nf_flowtable *ct_ft);
void efx_tc_ct_unregister_zone(struct efx_nic *efx,
			       struct efx_tc_ct_zone *ct_zone);

#endif /* CONFIG_SFC_SRIOV */
#endif /* EFX_TC_CONNTRACK_H */
