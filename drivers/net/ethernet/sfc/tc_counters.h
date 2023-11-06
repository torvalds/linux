/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_TC_COUNTERS_H
#define EFX_TC_COUNTERS_H
#include <linux/refcount.h>
#include "net_driver.h"

#include "mcdi_pcol.h" /* for MAE_COUNTER_TYPE_* */

enum efx_tc_counter_type {
	EFX_TC_COUNTER_TYPE_AR = MAE_COUNTER_TYPE_AR,
	EFX_TC_COUNTER_TYPE_CT = MAE_COUNTER_TYPE_CT,
	EFX_TC_COUNTER_TYPE_OR = MAE_COUNTER_TYPE_OR,
	EFX_TC_COUNTER_TYPE_MAX
};

struct efx_tc_counter {
	u32 fw_id; /* index in firmware counter table */
	enum efx_tc_counter_type type;
	struct rhash_head linkage; /* efx->tc->counter_ht */
	spinlock_t lock; /* Serialises updates to counter values */
	u32 gen; /* Generation count at which this counter is current */
	u64 packets, bytes;
	u64 old_packets, old_bytes; /* Values last time passed to userspace */
	/* jiffies of the last time we saw packets increase */
	unsigned long touched;
	struct work_struct work; /* For notifying encap actions */
	/* owners of corresponding count actions */
	struct list_head users;
};

struct efx_tc_counter_index {
	unsigned long cookie;
	struct rhash_head linkage; /* efx->tc->counter_id_ht */
	refcount_t ref;
	struct efx_tc_counter *cnt;
};

/* create/uncreate/teardown hashtables */
int efx_tc_init_counters(struct efx_nic *efx);
void efx_tc_destroy_counters(struct efx_nic *efx);
void efx_tc_fini_counters(struct efx_nic *efx);

struct efx_tc_counter_index *efx_tc_flower_get_counter_index(
				struct efx_nic *efx, unsigned long cookie,
				enum efx_tc_counter_type type);
void efx_tc_flower_put_counter_index(struct efx_nic *efx,
				     struct efx_tc_counter_index *ctr);
struct efx_tc_counter_index *efx_tc_flower_find_counter_index(
				struct efx_nic *efx, unsigned long cookie);

extern const struct efx_channel_type efx_tc_channel_type;

#endif /* EFX_TC_COUNTERS_H */
