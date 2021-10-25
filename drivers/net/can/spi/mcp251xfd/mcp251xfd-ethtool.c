// SPDX-License-Identifier: GPL-2.0
//
// mcp251xfd - Microchip MCP251xFD Family CAN controller driver
//
// Copyright (c) 2021, 2022 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <linux/ethtool.h>

#include "mcp251xfd.h"

static void
mcp251xfd_ring_get_ringparam(struct net_device *ndev,
			     struct ethtool_ringparam *ring,
			     struct kernel_ethtool_ringparam *kernel_ring,
			     struct netlink_ext_ack *extack)
{
	const struct mcp251xfd_priv *priv = netdev_priv(ndev);

	ring->rx_max_pending = MCP251XFD_RX_OBJ_NUM_MAX;
	ring->tx_max_pending = MCP251XFD_TX_OBJ_NUM_MAX;

	ring->rx_pending = priv->rx_obj_num;
	ring->tx_pending = priv->tx->obj_num;
}

static const struct ethtool_ops mcp251xfd_ethtool_ops = {
	.get_ringparam = mcp251xfd_ring_get_ringparam,
};

void mcp251xfd_ethtool_init(struct mcp251xfd_priv *priv)
{
	priv->ndev->ethtool_ops = &mcp251xfd_ethtool_ops;
}
