// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>
#include <linux/phylink.h>
#include <linux/netdevice.h>

#include "../libwx/wx_ethtool.h"
#include "../libwx/wx_type.h"
#include "txgbe_type.h"
#include "txgbe_ethtool.h"

static int txgbe_nway_reset(struct net_device *netdev)
{
	struct txgbe *txgbe = netdev_to_txgbe(netdev);

	return phylink_ethtool_nway_reset(txgbe->phylink);
}

static int txgbe_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *cmd)
{
	struct txgbe *txgbe = netdev_to_txgbe(netdev);

	return phylink_ethtool_ksettings_get(txgbe->phylink, cmd);
}

static int txgbe_set_link_ksettings(struct net_device *netdev,
				    const struct ethtool_link_ksettings *cmd)
{
	struct txgbe *txgbe = netdev_to_txgbe(netdev);

	return phylink_ethtool_ksettings_set(txgbe->phylink, cmd);
}

static const struct ethtool_ops txgbe_ethtool_ops = {
	.get_drvinfo		= wx_get_drvinfo,
	.nway_reset		= txgbe_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= txgbe_get_link_ksettings,
	.set_link_ksettings	= txgbe_set_link_ksettings,
};

void txgbe_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &txgbe_ethtool_ops;
}
