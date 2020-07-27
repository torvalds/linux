// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 * Copyright 2019-2020 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "tx_common.h"
#include "nic_common.h"
#include "ef100_tx.h"

/* TX queue stubs */
int ef100_tx_probe(struct efx_tx_queue *tx_queue)
{
	return 0;
}

void ef100_tx_init(struct efx_tx_queue *tx_queue)
{
	/* must be the inverse of lookup in efx_get_tx_channel */
	tx_queue->core_txq =
		netdev_get_tx_queue(tx_queue->efx->net_dev,
				    tx_queue->channel->channel -
				    tx_queue->efx->tx_channel_offset);
}

void ef100_tx_write(struct efx_tx_queue *tx_queue)
{
}

/* Add a socket buffer to a TX queue
 *
 * You must hold netif_tx_lock() to call this function.
 *
 * Returns 0 on success, error code otherwise. In case of an error this
 * function will free the SKB.
 */
int ef100_enqueue_skb(struct efx_tx_queue *tx_queue, struct sk_buff *skb)
{
	/* Stub.  No TX path yet. */
	struct efx_nic *efx = tx_queue->efx;

	netif_stop_queue(efx->net_dev);
	dev_kfree_skb_any(skb);
	return -ENODEV;
}
