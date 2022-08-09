// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2022 Amarula Solutions, Dario Binacchi <dario.binacchi@amarulasolutions.com>
 * Copyright (c) 2022 Pengutronix, Marc Kleine-Budde <kernel@pengutronix.de>
 *
 */

#include <linux/can/dev.h>
#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>

#include "flexcan.h"

static const char flexcan_priv_flags_strings[][ETH_GSTRING_LEN] = {
#define FLEXCAN_PRIV_FLAGS_RX_RTR BIT(0)
	"rx-rtr",
};

static void
flexcan_get_ringparam(struct net_device *ndev, struct ethtool_ringparam *ring,
		      struct kernel_ethtool_ringparam *kernel_ring,
		      struct netlink_ext_ack *ext_ack)
{
	const struct flexcan_priv *priv = netdev_priv(ndev);

	ring->rx_max_pending = priv->mb_count;
	ring->tx_max_pending = priv->mb_count;

	if (priv->devtype_data.quirks & FLEXCAN_QUIRK_USE_RX_MAILBOX)
		ring->rx_pending = priv->offload.mb_last -
			priv->offload.mb_first + 1;
	else
		ring->rx_pending = 6;	/* RX-FIFO depth is fixed */

	/* the drive currently supports only on TX buffer */
	ring->tx_pending = 1;
}

static void
flexcan_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		memcpy(data, flexcan_priv_flags_strings,
		       sizeof(flexcan_priv_flags_strings));
	}
}

static u32 flexcan_get_priv_flags(struct net_device *ndev)
{
	const struct flexcan_priv *priv = netdev_priv(ndev);
	u32 priv_flags = 0;

	if (flexcan_active_rx_rtr(priv))
		priv_flags |= FLEXCAN_PRIV_FLAGS_RX_RTR;

	return priv_flags;
}

static int flexcan_set_priv_flags(struct net_device *ndev, u32 priv_flags)
{
	struct flexcan_priv *priv = netdev_priv(ndev);
	u32 quirks = priv->devtype_data.quirks;

	if (priv_flags & FLEXCAN_PRIV_FLAGS_RX_RTR) {
		if (flexcan_supports_rx_mailbox_rtr(priv))
			quirks |= FLEXCAN_QUIRK_USE_RX_MAILBOX;
		else if (flexcan_supports_rx_fifo(priv))
			quirks &= ~FLEXCAN_QUIRK_USE_RX_MAILBOX;
		else
			quirks |= FLEXCAN_QUIRK_USE_RX_MAILBOX;
	} else {
		if (flexcan_supports_rx_mailbox(priv))
			quirks |= FLEXCAN_QUIRK_USE_RX_MAILBOX;
		else
			quirks &= ~FLEXCAN_QUIRK_USE_RX_MAILBOX;
	}

	if (quirks != priv->devtype_data.quirks && netif_running(ndev))
		return -EBUSY;

	priv->devtype_data.quirks = quirks;

	if (!(priv_flags & FLEXCAN_PRIV_FLAGS_RX_RTR) &&
	    !flexcan_active_rx_rtr(priv))
		netdev_info(ndev,
			    "Activating RX mailbox mode, cannot receive RTR frames.\n");

	return 0;
}

static int flexcan_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_PRIV_FLAGS:
		return ARRAY_SIZE(flexcan_priv_flags_strings);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ethtool_ops flexcan_ethtool_ops = {
	.get_ringparam = flexcan_get_ringparam,
	.get_strings = flexcan_get_strings,
	.get_priv_flags = flexcan_get_priv_flags,
	.set_priv_flags = flexcan_set_priv_flags,
	.get_sset_count = flexcan_get_sset_count,
};

void flexcan_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &flexcan_ethtool_ops;
}
