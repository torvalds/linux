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

static void bnge_get_eth_phy_stats(struct net_device *dev,
				   struct ethtool_eth_phy_stats *phy_stats)
{
	struct bnge_net *bn = netdev_priv(dev);
	u64 *rx;

	if (!(bn->flags & BNGE_FLAG_PORT_STATS_EXT))
		return;

	rx = bn->rx_port_stats_ext.sw_stats;
	phy_stats->SymbolErrorDuringCarrier =
		*(rx + BNGE_RX_STATS_EXT_OFFSET(rx_pcs_symbol_err));
}

static void bnge_get_eth_mac_stats(struct net_device *dev,
				   struct ethtool_eth_mac_stats *mac_stats)
{
	struct bnge_net *bn = netdev_priv(dev);
	u64 *rx, *tx;

	if (!(bn->flags & BNGE_FLAG_PORT_STATS))
		return;

	rx = bn->port_stats.sw_stats;
	tx = bn->port_stats.sw_stats + BNGE_TX_PORT_STATS_BYTE_OFFSET / 8;

	mac_stats->FramesReceivedOK =
		BNGE_GET_RX_PORT_STATS64(rx, rx_good_frames);
	mac_stats->FramesTransmittedOK =
		BNGE_GET_TX_PORT_STATS64(tx, tx_good_frames);
	mac_stats->FrameCheckSequenceErrors =
		BNGE_GET_RX_PORT_STATS64(rx, rx_fcs_err_frames);
	mac_stats->AlignmentErrors =
		BNGE_GET_RX_PORT_STATS64(rx, rx_align_err_frames);
	mac_stats->OutOfRangeLengthField =
		BNGE_GET_RX_PORT_STATS64(rx, rx_oor_len_frames);
	mac_stats->OctetsReceivedOK = BNGE_GET_RX_PORT_STATS64(rx, rx_bytes);
	mac_stats->OctetsTransmittedOK = BNGE_GET_TX_PORT_STATS64(tx, tx_bytes);
	mac_stats->MulticastFramesReceivedOK =
		BNGE_GET_RX_PORT_STATS64(rx, rx_mcast_frames);
	mac_stats->BroadcastFramesReceivedOK =
		BNGE_GET_RX_PORT_STATS64(rx, rx_bcast_frames);
	mac_stats->MulticastFramesXmittedOK =
		BNGE_GET_TX_PORT_STATS64(tx, tx_mcast_frames);
	mac_stats->BroadcastFramesXmittedOK =
		BNGE_GET_TX_PORT_STATS64(tx, tx_bcast_frames);
	mac_stats->FrameTooLongErrors =
		BNGE_GET_RX_PORT_STATS64(rx, rx_ovrsz_frames);
}

static void bnge_get_eth_ctrl_stats(struct net_device *dev,
				    struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct bnge_net *bn = netdev_priv(dev);
	u64 *rx;

	if (!(bn->flags & BNGE_FLAG_PORT_STATS))
		return;

	rx = bn->port_stats.sw_stats;
	ctrl_stats->MACControlFramesReceived =
		BNGE_GET_RX_PORT_STATS64(rx, rx_ctrl_frames);
}

static void bnge_get_pause_stats(struct net_device *dev,
				 struct ethtool_pause_stats *pause_stats)
{
	struct bnge_net *bn = netdev_priv(dev);
	u64 *rx, *tx;

	if (!(bn->flags & BNGE_FLAG_PORT_STATS))
		return;

	rx = bn->port_stats.sw_stats;
	tx = bn->port_stats.sw_stats + BNGE_TX_PORT_STATS_BYTE_OFFSET / 8;

	pause_stats->rx_pause_frames =
		BNGE_GET_RX_PORT_STATS64(rx, rx_pause_frames);
	pause_stats->tx_pause_frames =
		BNGE_GET_TX_PORT_STATS64(tx, tx_pause_frames);
}

static const struct ethtool_rmon_hist_range bnge_rmon_ranges[] = {
	{    0,    64 },
	{   65,   127 },
	{  128,   255 },
	{  256,   511 },
	{  512,  1023 },
	{ 1024,  1518 },
	{ 1519,  2047 },
	{ 2048,  4095 },
	{ 4096,  9216 },
	{ 9217, 16383 },
	{}
};

static void bnge_get_rmon_stats(struct net_device *dev,
				struct ethtool_rmon_stats *rmon_stats,
				const struct ethtool_rmon_hist_range **ranges)
{
	struct bnge_net *bn = netdev_priv(dev);
	u64 *rx, *tx;

	if (!(bn->flags & BNGE_FLAG_PORT_STATS))
		return;

	rx = bn->port_stats.sw_stats;
	tx = bn->port_stats.sw_stats + BNGE_TX_PORT_STATS_BYTE_OFFSET / 8;

	rmon_stats->jabbers = BNGE_GET_RX_PORT_STATS64(rx, rx_jbr_frames);
	rmon_stats->oversize_pkts =
		BNGE_GET_RX_PORT_STATS64(rx, rx_ovrsz_frames);
	rmon_stats->undersize_pkts =
		BNGE_GET_RX_PORT_STATS64(rx, rx_undrsz_frames);

	rmon_stats->hist[0] = BNGE_GET_RX_PORT_STATS64(rx, rx_64b_frames);
	rmon_stats->hist[1] = BNGE_GET_RX_PORT_STATS64(rx, rx_65b_127b_frames);
	rmon_stats->hist[2] = BNGE_GET_RX_PORT_STATS64(rx, rx_128b_255b_frames);
	rmon_stats->hist[3] = BNGE_GET_RX_PORT_STATS64(rx, rx_256b_511b_frames);
	rmon_stats->hist[4] =
		BNGE_GET_RX_PORT_STATS64(rx, rx_512b_1023b_frames);
	rmon_stats->hist[5] =
		BNGE_GET_RX_PORT_STATS64(rx, rx_1024b_1518b_frames);
	rmon_stats->hist[6] =
		BNGE_GET_RX_PORT_STATS64(rx, rx_1519b_2047b_frames);
	rmon_stats->hist[7] =
		BNGE_GET_RX_PORT_STATS64(rx, rx_2048b_4095b_frames);
	rmon_stats->hist[8] =
		BNGE_GET_RX_PORT_STATS64(rx, rx_4096b_9216b_frames);
	rmon_stats->hist[9] =
		BNGE_GET_RX_PORT_STATS64(rx, rx_9217b_16383b_frames);

	rmon_stats->hist_tx[0] = BNGE_GET_TX_PORT_STATS64(tx, tx_64b_frames);
	rmon_stats->hist_tx[1] =
		BNGE_GET_TX_PORT_STATS64(tx, tx_65b_127b_frames);
	rmon_stats->hist_tx[2] =
		BNGE_GET_TX_PORT_STATS64(tx, tx_128b_255b_frames);
	rmon_stats->hist_tx[3] =
		BNGE_GET_TX_PORT_STATS64(tx, tx_256b_511b_frames);
	rmon_stats->hist_tx[4] =
		BNGE_GET_TX_PORT_STATS64(tx, tx_512b_1023b_frames);
	rmon_stats->hist_tx[5] =
		BNGE_GET_TX_PORT_STATS64(tx, tx_1024b_1518b_frames);
	rmon_stats->hist_tx[6] =
		BNGE_GET_TX_PORT_STATS64(tx, tx_1519b_2047b_frames);
	rmon_stats->hist_tx[7] =
		BNGE_GET_TX_PORT_STATS64(tx, tx_2048b_4095b_frames);
	rmon_stats->hist_tx[8] =
		BNGE_GET_TX_PORT_STATS64(tx, tx_4096b_9216b_frames);
	rmon_stats->hist_tx[9] =
		BNGE_GET_TX_PORT_STATS64(tx, tx_9217b_16383b_frames);

	*ranges = bnge_rmon_ranges;
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
	.get_eth_phy_stats	= bnge_get_eth_phy_stats,
	.get_eth_mac_stats	= bnge_get_eth_mac_stats,
	.get_eth_ctrl_stats	= bnge_get_eth_ctrl_stats,
	.get_pause_stats	= bnge_get_pause_stats,
	.get_rmon_stats		= bnge_get_rmon_stats,
};

void bnge_set_ethtool_ops(struct net_device *dev)
{
	dev->ethtool_ops = &bnge_ethtool_ops;
}
