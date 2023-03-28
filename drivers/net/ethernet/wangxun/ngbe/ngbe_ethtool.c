// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>
#include <linux/phy.h>
#include <linux/netdevice.h>

#include "../libwx/wx_ethtool.h"
#include "ngbe_ethtool.h"

static const struct ethtool_ops ngbe_ethtool_ops = {
	.get_drvinfo		= wx_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.nway_reset		= phy_ethtool_nway_reset,
};

void ngbe_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ngbe_ethtool_ops;
}
