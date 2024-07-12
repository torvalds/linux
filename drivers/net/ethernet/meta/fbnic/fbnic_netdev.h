/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_NETDEV_H_
#define _FBNIC_NETDEV_H_

#include <linux/types.h>

#include "fbnic_txrx.h"

struct fbnic_net {
	struct fbnic_ring *tx[FBNIC_MAX_TXQS];
	struct fbnic_ring *rx[FBNIC_MAX_RXQS];

	struct net_device *netdev;
	struct fbnic_dev *fbd;

	u16 num_napi;

	u16 num_tx_queues;
	u16 num_rx_queues;

	struct list_head napis;
};

int __fbnic_open(struct fbnic_net *fbn);
void fbnic_up(struct fbnic_net *fbn);
void fbnic_down(struct fbnic_net *fbn);

struct net_device *fbnic_netdev_alloc(struct fbnic_dev *fbd);
void fbnic_netdev_free(struct fbnic_dev *fbd);
int fbnic_netdev_register(struct net_device *netdev);
void fbnic_netdev_unregister(struct net_device *netdev);
void fbnic_reset_queues(struct fbnic_net *fbn,
			unsigned int tx, unsigned int rx);

#endif /* _FBNIC_NETDEV_H_ */
