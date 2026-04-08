// SPDX-License-Identifier: GPL-2.0-or-later
/* Broadcom NetXtreme-C/E network driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/netdev_queues.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <net/tso.h>
#include <linux/bnxt/hsi.h>

#include "bnxt.h"
#include "bnxt_gso.h"

netdev_tx_t bnxt_sw_udp_gso_xmit(struct bnxt *bp,
				 struct bnxt_tx_ring_info *txr,
				 struct netdev_queue *txq,
				 struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
	dev_core_stats_tx_dropped_inc(bp->dev);
	return NETDEV_TX_OK;
}
