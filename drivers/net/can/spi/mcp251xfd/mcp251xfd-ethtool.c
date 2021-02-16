// SPDX-License-Identifier: GPL-2.0
//
// mcp251xfd - Microchip MCP251xFD Family CAN controller driver
//
// Copyright (c) 2021, 2022 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <linux/ethtool.h>

#include "mcp251xfd.h"
#include "mcp251xfd-ram.h"

static void
mcp251xfd_ring_get_ringparam(struct net_device *ndev,
			     struct ethtool_ringparam *ring,
			     struct kernel_ethtool_ringparam *kernel_ring,
			     struct netlink_ext_ack *extack)
{
	const struct mcp251xfd_priv *priv = netdev_priv(ndev);
	const bool fd_mode = mcp251xfd_is_fd_mode(priv);
	struct can_ram_layout layout;

	can_ram_get_layout(&layout, &mcp251xfd_ram_config, NULL, NULL, fd_mode);
	ring->rx_max_pending = layout.max_rx;
	ring->tx_max_pending = layout.max_tx;

	ring->rx_pending = priv->rx_obj_num;
	ring->tx_pending = priv->tx->obj_num;
}

static int
mcp251xfd_ring_set_ringparam(struct net_device *ndev,
			     struct ethtool_ringparam *ring,
			     struct kernel_ethtool_ringparam *kernel_ring,
			     struct netlink_ext_ack *extack)
{
	struct mcp251xfd_priv *priv = netdev_priv(ndev);
	const bool fd_mode = mcp251xfd_is_fd_mode(priv);
	struct can_ram_layout layout;

	can_ram_get_layout(&layout, &mcp251xfd_ram_config, ring, NULL, fd_mode);
	if ((layout.cur_rx != priv->rx_obj_num ||
	     layout.cur_tx != priv->tx->obj_num) &&
	    netif_running(ndev))
		return -EBUSY;

	priv->rx_obj_num = layout.cur_rx;
	priv->tx->obj_num = layout.cur_tx;

	return 0;
}

static const struct ethtool_ops mcp251xfd_ethtool_ops = {
	.get_ringparam = mcp251xfd_ring_get_ringparam,
	.set_ringparam = mcp251xfd_ring_set_ringparam,
};

void mcp251xfd_ethtool_init(struct mcp251xfd_priv *priv)
{
	struct can_ram_layout layout;

	priv->ndev->ethtool_ops = &mcp251xfd_ethtool_ops;

	can_ram_get_layout(&layout, &mcp251xfd_ram_config, NULL, NULL, false);
	priv->rx_obj_num = layout.default_rx;
	priv->tx->obj_num = layout.default_tx;

	priv->rx_obj_num_coalesce_irq = 0;
	priv->rx_coalesce_usecs_irq = 0;
}
