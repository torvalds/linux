// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>
#include <linux/phy.h>
#include <linux/netdevice.h>

#include "../libwx/wx_ethtool.h"
#include "../libwx/wx_type.h"
#include "ngbe_ethtool.h"

static void ngbe_get_wol(struct net_device *netdev,
			 struct ethtool_wolinfo *wol)
{
	struct wx *wx = netdev_priv(netdev);

	if (!wx->wol_hw_supported)
		return;
	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;
	if (wx->wol & WX_PSR_WKUP_CTL_MAG)
		wol->wolopts |= WAKE_MAGIC;
}

static int ngbe_set_wol(struct net_device *netdev,
			struct ethtool_wolinfo *wol)
{
	struct wx *wx = netdev_priv(netdev);
	struct pci_dev *pdev = wx->pdev;

	if (!wx->wol_hw_supported)
		return -EOPNOTSUPP;

	wx->wol = 0;
	if (wol->wolopts & WAKE_MAGIC)
		wx->wol = WX_PSR_WKUP_CTL_MAG;
	netdev->wol_enabled = !!(wx->wol);
	wr32(wx, WX_PSR_WKUP_CTL, wx->wol);
	device_set_wakeup_enable(&pdev->dev, netdev->wol_enabled);

	return 0;
}

static const struct ethtool_ops ngbe_ethtool_ops = {
	.get_drvinfo		= wx_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.nway_reset		= phy_ethtool_nway_reset,
	.get_wol		= ngbe_get_wol,
	.set_wol		= ngbe_set_wol,
};

void ngbe_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ngbe_ethtool_ops;
}
