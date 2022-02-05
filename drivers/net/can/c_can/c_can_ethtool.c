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

static void c_can_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *info)
{
	struct c_can_priv *priv = netdev_priv(netdev);
	strscpy(info->driver, "c_can", sizeof(info->driver));
	strscpy(info->bus_info, dev_name(priv->device), sizeof(info->bus_info));
}

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

static const struct ethtool_ops c_can_ethtool_ops = {
	.get_drvinfo = c_can_get_drvinfo,
	.get_ringparam = c_can_get_ringparam,
};

void c_can_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &c_can_ethtool_ops;
}
