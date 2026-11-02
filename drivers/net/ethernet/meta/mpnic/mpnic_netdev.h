/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _MPNIC_NETDEV_H_
#define _MPNIC_NETDEV_H_

#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <linux/types.h>

#include "mpnic.h"
#include "mpnic_txrx.h"

struct mpnic_base_stats {
	struct {
		u64 packets;
		u64 bytes;
		u64 dropped;
		u64 stop;
		u64 wake;
	} tx;
	struct {
		u64 packets;
		u64 bytes;
		u64 dropped;
		u64 alloc_failed;
		u64 errors;
	} rx;

	unsigned int num_tx_queues;
	unsigned int num_rx_queues;
	struct mpnic_ring **txr;
	struct mpnic_ring **rxr;
};

struct mpnic_net {
	struct mpnic_ring *tx[MPNIC_MAX_TXQS];
	struct mpnic_ring *rx[MPNIC_MAX_RXQS];

	struct mpnic_napi_vector *napi[MPNIC_MAX_NAPI_VECTORS];

	struct net_device *netdev;
	struct mpnic_dev *mpd;

	u32 txq_size;
	u32 hpq_size;
	u32 ppq_size;
	u32 rcq_size;

	u16 num_napi;
	u16 num_tx_queues;
	u16 num_rx_queues;

	struct {
		struct mpnic_base_stats __rcu *base;
		struct mpnic_base_stats buf[2];
	} stats;
};

struct net_device *mpnic_netdev_alloc(struct mpnic_dev *mpd);
void mpnic_netdev_free(struct mpnic_dev *mpd);
int mpnic_netdev_register(struct net_device *netdev);

#endif /* _MPNIC_NETDEV_H_ */
