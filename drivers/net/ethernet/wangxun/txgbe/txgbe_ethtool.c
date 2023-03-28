// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>
#include <linux/phylink.h>
#include <linux/netdevice.h>

#include "../libwx/wx_ethtool.h"
#include "txgbe_ethtool.h"

static const struct ethtool_ops txgbe_ethtool_ops = {
	.get_drvinfo		= wx_get_drvinfo,
	.get_link		= ethtool_op_get_link,
};

void txgbe_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &txgbe_ethtool_ops;
}
