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
#include "tc.h"

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
 * @write_index: number of packets enqueued to @rx_list
 * @read_index: number of packets consumed from @rx_list
 * @rx_pring_size: max length of RX list
 * @dflt: default-rule for MAE switching
 * @list: entry on efx->vf_reps
 * @rx_list: list of SKBs queued for receive in NAPI poll
 * @rx_lock: protects @rx_list
 * @napi: NAPI control structure
 * @stats: software traffic counters for netdev stats
 */
struct efx_rep {
	struct efx_nic *parent;
	struct net_device *net_dev;
	u32 msg_enable;
	u32 mport;
	unsigned int idx;
	unsigned int write_index, read_index;
	unsigned int rx_pring_size;
	struct efx_tc_flow_rule dflt;
	struct list_head list;
	struct list_head rx_list;
	spinlock_t rx_lock;
	struct napi_struct napi;
	struct efx_rep_sw_stats stats;
};

int efx_ef100_vfrep_create(struct efx_nic *efx, unsigned int i);
void efx_ef100_vfrep_destroy(struct efx_nic *efx, struct efx_rep *efv);
void efx_ef100_fini_vfreps(struct efx_nic *efx);

void efx_ef100_rep_rx_packet(struct efx_rep *efv, struct efx_rx_buffer *rx_buf);
/* Returns the representor corresponding to a VF m-port, or NULL
 * @mport is an m-port label, *not* an m-port ID!
 * Caller must hold rcu_read_lock().
 */
struct efx_rep *efx_ef100_find_rep_by_mport(struct efx_nic *efx, u16 mport);
extern const struct net_device_ops efx_ef100_rep_netdev_ops;
#endif /* EF100_REP_H */
