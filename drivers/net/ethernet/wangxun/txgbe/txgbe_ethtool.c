// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>
#include <linux/phylink.h>
#include <linux/netdevice.h>

#include "../libwx/wx_ethtool.h"
#include "../libwx/wx_type.h"
#include "../libwx/wx_lib.h"
#include "txgbe_type.h"
#include "txgbe_ethtool.h"

static int txgbe_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct wx *wx = netdev_priv(netdev);
	u32 new_rx_count, new_tx_count;
	struct wx_ring *temp_ring;
	int i, err = 0;

	new_tx_count = clamp_t(u32, ring->tx_pending, WX_MIN_TXD, WX_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, WX_REQ_TX_DESCRIPTOR_MULTIPLE);

	new_rx_count = clamp_t(u32, ring->rx_pending, WX_MIN_RXD, WX_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, WX_REQ_RX_DESCRIPTOR_MULTIPLE);

	if (new_tx_count == wx->tx_ring_count &&
	    new_rx_count == wx->rx_ring_count)
		return 0;

	err = wx_set_state_reset(wx);
	if (err)
		return err;

	if (!netif_running(wx->netdev)) {
		for (i = 0; i < wx->num_tx_queues; i++)
			wx->tx_ring[i]->count = new_tx_count;
		for (i = 0; i < wx->num_rx_queues; i++)
			wx->rx_ring[i]->count = new_rx_count;
		wx->tx_ring_count = new_tx_count;
		wx->rx_ring_count = new_rx_count;

		goto clear_reset;
	}

	/* allocate temporary buffer to store rings in */
	i = max_t(int, wx->num_tx_queues, wx->num_rx_queues);
	temp_ring = kvmalloc_array(i, sizeof(struct wx_ring), GFP_KERNEL);
	if (!temp_ring) {
		err = -ENOMEM;
		goto clear_reset;
	}

	txgbe_down(wx);

	wx_set_ring(wx, new_tx_count, new_rx_count, temp_ring);
	kvfree(temp_ring);

	txgbe_up(wx);

clear_reset:
	clear_bit(WX_STATE_RESETTING, wx->state);
	return err;
}

static int txgbe_set_channels(struct net_device *dev,
			      struct ethtool_channels *ch)
{
	int err;

	err = wx_set_channels(dev, ch);
	if (err < 0)
		return err;

	/* use setup TC to update any traffic class queue mapping */
	return txgbe_setup_tc(dev, netdev_get_num_tc(dev));
}

static const struct ethtool_ops txgbe_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_TX_MAX_FRAMES_IRQ,
	.get_drvinfo		= wx_get_drvinfo,
	.nway_reset		= wx_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= wx_get_link_ksettings,
	.set_link_ksettings	= wx_set_link_ksettings,
	.get_sset_count		= wx_get_sset_count,
	.get_strings		= wx_get_strings,
	.get_ethtool_stats	= wx_get_ethtool_stats,
	.get_eth_mac_stats	= wx_get_mac_stats,
	.get_pause_stats	= wx_get_pause_stats,
	.get_pauseparam		= wx_get_pauseparam,
	.set_pauseparam		= wx_set_pauseparam,
	.get_ringparam		= wx_get_ringparam,
	.set_ringparam		= txgbe_set_ringparam,
	.get_coalesce		= wx_get_coalesce,
	.set_coalesce		= wx_set_coalesce,
	.get_channels		= wx_get_channels,
	.set_channels		= txgbe_set_channels,
	.get_msglevel		= wx_get_msglevel,
	.set_msglevel		= wx_set_msglevel,
};

void txgbe_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &txgbe_ethtool_ops;
}
