/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2018 Microchip Technology Inc. */

#include <linux/netdevice.h>
#include "lan743x_main.h"
#include "lan743x_ethtool.h"
#include <linux/pci.h>

static void lan743x_ethtool_get_drvinfo(struct net_device *netdev,
					struct ethtool_drvinfo *info)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	strlcpy(info->driver, DRIVER_NAME, sizeof(info->driver));
	strlcpy(info->bus_info,
		pci_name(adapter->pdev), sizeof(info->bus_info));
}

const struct ethtool_ops lan743x_ethtool_ops = {
	.get_drvinfo = lan743x_ethtool_get_drvinfo,
};
