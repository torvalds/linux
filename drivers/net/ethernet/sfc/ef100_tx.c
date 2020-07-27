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
