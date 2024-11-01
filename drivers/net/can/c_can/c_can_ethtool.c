// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021, Dario Binacchi <dariobin@libero.it>
 */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/can/dev.h>

#include "c_can.h"

static void c_can_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring,
				struct kernel_ethtool_ringparam *kernel_ring,
				struct netlink_ext_ack *extack)
{
	struct c_can_priv *priv = netdev_priv(netdev);

	ring->rx_max_pending = priv->msg_obj_num;
	ring->tx_max_pending = priv->msg_obj_num;
	ring->rx_pending = priv->msg_obj_rx_num;
	ring->tx_pending = priv->msg_obj_tx_num;
}

const struct ethtool_ops c_can_ethtool_ops = {
	.get_ringparam = c_can_get_ringparam,
	.get_ts_info = ethtool_op_get_ts_info,
};
