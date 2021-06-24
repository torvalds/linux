// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve.h"
#include "gve_adminq.h"
#include "gve_utils.h"
#include "gve_dqo.h"
#include <linux/tcp.h>
#include <linux/slab.h>
#include <linux/skbuff.h>

netdev_tx_t gve_tx_dqo(struct sk_buff *skb, struct net_device *dev)
{
	return NETDEV_TX_OK;
}

bool gve_tx_poll_dqo(struct gve_notify_block *block, bool do_clean)
{
	return false;
}
