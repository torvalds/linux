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

/* Handling for ef100 representor netdevs */
#ifndef EF100_REP_H
#define EF100_REP_H

#include "net_driver.h"

struct efx_rep_sw_stats {
	atomic64_t rx_packets, tx_packets;
	atomic64_t rx_bytes, tx_bytes;
	atomic64_t rx_dropped, tx_errors;
};

/**
 * struct efx_rep - Private data for an Efx representor
 *
 * @parent: the efx PF which manages this representor
 * @net_dev: representor netdevice
 * @msg_enable: log message enable flags
 * @mport: m-port ID of corresponding VF
 * @idx: VF index
 * @list: entry on efx->vf_reps
 * @stats: software traffic counters for netdev stats
 */
struct efx_rep {
	struct efx_nic *parent;
	struct net_device *net_dev;
	u32 msg_enable;
	u32 mport;
	unsigned int idx;
	struct list_head list;
	struct efx_rep_sw_stats stats;
};

int efx_ef100_vfrep_create(struct efx_nic *efx, unsigned int i);
void efx_ef100_vfrep_destroy(struct efx_nic *efx, struct efx_rep *efv);
void efx_ef100_fini_vfreps(struct efx_nic *efx);

#endif /* EF100_REP_H */
