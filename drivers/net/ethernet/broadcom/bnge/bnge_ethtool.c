// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/unaligned.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <net/devlink.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/ethtool_netlink.h>

#include "bnge.h"
#include "bnge_ethtool.h"
#include "bnge_hwrm_lib.h"

static int bnge_nway_reset(struct net_device *dev)
{
	struct bnge_net *bn = netdev_priv(dev);
	struct bnge_dev *bd = bn->bd;
	bool set_pause = false;
	int rc = 0;

	if (!BNGE_PHY_CFG_ABLE(bd))
		return -EOPNOTSUPP;

	if (!(bn->eth_link_info.autoneg & BNGE_AUTONEG_SPEED))
		return -EINVAL;

	if (!(bd->phy_flags & BNGE_PHY_FL_NO_PAUSE))
		set_pause = true;

	if (netif_running(dev))
		rc = bnge_hwrm_set_link_setting(bn, set_pause);

	return rc;
}

static void bnge_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	struct bnge_net *bn = netdev_priv(dev);
	struct bnge_dev *bd = bn->bd;

	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->fw_version, bd->fw_ver_str, sizeof(info->fw_version));
	strscpy(info->bus_info, pci_name(bd->pdev), sizeof(info->bus_info));
}

static void bnge_get_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *epause)
{
	struct bnge_net *bn = netdev_priv(dev);
	struct bnge_dev *bd = bn->bd;

	if (bd->phy_flags & BNGE_PHY_FL_NO_PAUSE) {
		epause->autoneg = 0;
		epause->rx_pause = 0;
		epause->tx_pause = 0;
		return;
	}

	epause->autoneg = !!(bn->eth_link_info.autoneg &
			     BNGE_AUTONEG_FLOW_CTRL);
	epause->rx_pause = !!(bn->eth_link_info.req_flow_ctrl &
			      BNGE_LINK_PAUSE_RX);
	epause->tx_pause = !!(bn->eth_link_info.req_flow_ctrl &
			      BNGE_LINK_PAUSE_TX);
}

static int bnge_set_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *epause)
{
	struct bnge_ethtool_link_info old_elink_info, *elink_info;
	struct bnge_net *bn = netdev_priv(dev);
	struct bnge_dev *bd = bn->bd;
	int rc = 0;

	if (!BNGE_PHY_CFG_ABLE(bd) || (bd->phy_flags & BNGE_PHY_FL_NO_PAUSE))
		return -EOPNOTSUPP;

	elink_info = &bn->eth_link_info;
	old_elink_info = *elink_info;

	if (epause->autoneg) {
		if (!(elink_info->autoneg & BNGE_AUTONEG_SPEED))
			return -EINVAL;

		elink_info->autoneg |= BNGE_AUTONEG_FLOW_CTRL;
	} else {
		if (elink_info->autoneg & BNGE_AUTONEG_FLOW_CTRL)
			elink_info->force_link_chng = true;
		elink_info->autoneg &= ~BNGE_AUTONEG_FLOW_CTRL;
	}

	elink_info->req_flow_ctrl = 0;
	if (epause->rx_pause)
		elink_info->req_flow_ctrl |= BNGE_LINK_PAUSE_RX;
	if (epause->tx_pause)
		elink_info->req_flow_ctrl |= BNGE_LINK_PAUSE_TX;

	if (netif_running(dev)) {
		rc = bnge_hwrm_set_pause(bn);
		if (rc)
			*elink_info = old_elink_info;
	}

	return rc;
}

static const struct ethtool_ops bnge_ethtool_ops = {
	.cap_link_lanes_supported	= 1,
	.get_link_ksettings	= bnge_get_link_ksettings,
	.set_link_ksettings	= bnge_set_link_ksettings,
	.get_drvinfo		= bnge_get_drvinfo,
	.get_link		= bnge_get_link,
	.nway_reset		= bnge_nway_reset,
	.get_pauseparam		= bnge_get_pauseparam,
	.set_pauseparam		= bnge_set_pauseparam,
};

void bnge_set_ethtool_ops(struct net_device *dev)
{
	dev->ethtool_ops = &bnge_ethtool_ops;
}
