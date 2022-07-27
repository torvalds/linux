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
	priv->rx_obj_num_coalesce_irq = layout.rx_coalesce;
	priv->tx->obj_num = layout.cur_tx;

	return 0;
}

static int mcp251xfd_ring_get_coalesce(struct net_device *ndev,
				       struct ethtool_coalesce *ec,
				       struct kernel_ethtool_coalesce *kec,
				       struct netlink_ext_ack *ext_ack)
{
	struct mcp251xfd_priv *priv = netdev_priv(ndev);
	u32 rx_max_frames, tx_max_frames;

	/* The ethtool doc says:
	 * To disable coalescing, set usecs = 0 and max_frames = 1.
	 */
	if (priv->rx_obj_num_coalesce_irq == 0)
		rx_max_frames = 1;
	else
		rx_max_frames = priv->rx_obj_num_coalesce_irq;

	ec->rx_max_coalesced_frames_irq = rx_max_frames;
	ec->rx_coalesce_usecs_irq = priv->rx_coalesce_usecs_irq;

	if (priv->tx_obj_num_coalesce_irq == 0)
		tx_max_frames = 1;
	else
		tx_max_frames = priv->tx_obj_num_coalesce_irq;

	ec->tx_max_coalesced_frames_irq = tx_max_frames;
	ec->tx_coalesce_usecs_irq = priv->tx_coalesce_usecs_irq;

	return 0;
}

static int mcp251xfd_ring_set_coalesce(struct net_device *ndev,
				       struct ethtool_coalesce *ec,
				       struct kernel_ethtool_coalesce *kec,
				       struct netlink_ext_ack *ext_ack)
{
	struct mcp251xfd_priv *priv = netdev_priv(ndev);
	const bool fd_mode = mcp251xfd_is_fd_mode(priv);
	const struct ethtool_ringparam ring = {
		.rx_pending = priv->rx_obj_num,
		.tx_pending = priv->tx->obj_num,
	};
	struct can_ram_layout layout;

	can_ram_get_layout(&layout, &mcp251xfd_ram_config, &ring, ec, fd_mode);

	if ((layout.rx_coalesce != priv->rx_obj_num_coalesce_irq ||
	     ec->rx_coalesce_usecs_irq != priv->rx_coalesce_usecs_irq ||
	     layout.tx_coalesce != priv->tx_obj_num_coalesce_irq ||
	     ec->tx_coalesce_usecs_irq != priv->tx_coalesce_usecs_irq) &&
	    netif_running(ndev))
		return -EBUSY;

	priv->rx_obj_num = layout.cur_rx;
	priv->rx_obj_num_coalesce_irq = layout.rx_coalesce;
	priv->rx_coalesce_usecs_irq = ec->rx_coalesce_usecs_irq;

	priv->tx->obj_num = layout.cur_tx;
	priv->tx_obj_num_coalesce_irq = layout.tx_coalesce;
	priv->tx_coalesce_usecs_irq = ec->tx_coalesce_usecs_irq;

	return 0;
}

static const struct ethtool_ops mcp251xfd_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS_IRQ |
		ETHTOOL_COALESCE_RX_MAX_FRAMES_IRQ |
		ETHTOOL_COALESCE_TX_USECS_IRQ |
		ETHTOOL_COALESCE_TX_MAX_FRAMES_IRQ,
	.get_ringparam = mcp251xfd_ring_get_ringparam,
	.set_ringparam = mcp251xfd_ring_set_ringparam,
	.get_coalesce = mcp251xfd_ring_get_coalesce,
	.set_coalesce = mcp251xfd_ring_set_coalesce,
	.get_ts_info = can_ethtool_op_get_ts_info_hwts,
};

void mcp251xfd_ethtool_init(struct mcp251xfd_priv *priv)
{
	struct can_ram_layout layout;

	priv->ndev->ethtool_ops = &mcp251xfd_ethtool_ops;

	can_ram_get_layout(&layout, &mcp251xfd_ram_config, NULL, NULL, false);
	priv->rx_obj_num = layout.default_rx;
	priv->tx->obj_num = layout.default_tx;

	priv->rx_obj_num_coalesce_irq = 0;
	priv->tx_obj_num_coalesce_irq = 0;
	priv->rx_coalesce_usecs_irq = 0;
	priv->tx_coalesce_usecs_irq = 0;
}
