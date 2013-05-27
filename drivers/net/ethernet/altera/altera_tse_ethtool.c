/*
 * Ethtool support for Altera Triple-Speed Ethernet MAC driver
 *
 * Copyright (C) 2008-2013 Altera Corporation
 *
 * Contributors:
 *   Dalon Westergreen
 *   Thomas Chou
 *   Ian Abbott
 *   Yuriy Kozlov
 *   Tobias Klauser
 *
 * Original driver contributed by SLS.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/phy.h>

#include "altera_tse.h"

#define TSE_STATS_LEN 35

static char stat_gstrings[][ETH_GSTRING_LEN] = {
	"aFramesTransmittedOK",
	"aFramesReceivedOK",
	"aFramesCheckSequenceErrors",
	"aAlignmentErrors",
	"aOctetsTransmittedOK",
	"aOctetsReceivedOK",
	"aTxPAUSEMACCtrlFrames",
	"aRxPAUSEMACCtrlFrames",
	"ifInErrors",
	"ifOutErrors",
	"ifInUcastPkts",
	"ifInMulticastPkts",
	"ifInBroadcastPkts",
	"ifOutDiscards",
	"ifOutUcastPkts",
	"ifOutMulticastPkts",
	"ifOutBroadcastPkts",
	"etherStatsDropEvent",
	"etherStatsOctets",
	"etherStatsPkts",
	"etherStatsUndersizePkts",
	"etherStatsOversizePkts",
	"etherStatsPkts64Octets",
	"etherStatsPkts65to127Octets",
	"etherStatsPkts128to255Octets",
	"etherStatsPkts256to511Octets",
	"etherStatsPkts512to1023Octets",
	"etherStatsPkts1024to1518Octets",
	"etherStatsPkts1519toXOctets",
	"etherStatsJabbers",
	"etherStatsFragments",
	"ipaccTxConf",
	"ipaccRxConf",
	"ipaccRxStat",
	"ipaccRxStatSum",
};

static void tse_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	u32 megacore_rev = tse_priv->mac_dev->megacore_revision;

	strcpy(info->driver, "Altera TSE MAC IP Driver");
	strcpy(info->version, "v8.0");
	snprintf(info->fw_version, ETHTOOL_FWVERS_LEN, "v%d.%d",
			megacore_rev & 0xFFFF,
			(megacore_rev & 0xFFFF0000) >> 16);
	sprintf(info->bus_info, "AVALON");
}

/* Fill in a buffer with the strings which correspond to the
 * stats
 */
static void tse_gstrings(struct net_device *dev, u32 stringset, u8 *buf)
{
	memcpy(buf, stat_gstrings, TSE_STATS_LEN * ETH_GSTRING_LEN);
}

static void tse_fill_stats(struct net_device *dev, struct ethtool_stats *dummy,
				u64 *buf)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);

	buf[0] = (u64) tse_priv->mac_dev->aFramesTransmittedOK;
	buf[1] = (u64) tse_priv->mac_dev->aFramesReceivedOK;
	buf[2] = (u64) tse_priv->mac_dev->aFramesCheckSequenceErrors;
	buf[3] = (u64) tse_priv->mac_dev->aAlignmentErrors;
	buf[4] = (u64) tse_priv->mac_dev->aOctetsTransmittedOK;
	buf[5] = (u64) tse_priv->mac_dev->aOctetsReceivedOK;
	buf[6] = (u64) tse_priv->mac_dev->aTxPAUSEMACCtrlFrames;
	buf[7] = (u64) tse_priv->mac_dev->aRxPAUSEMACCtrlFrames;
	buf[8] = (u64) tse_priv->mac_dev->ifInErrors;
	buf[9] = (u64) tse_priv->mac_dev->ifOutErrors;
	buf[10] = (u64) tse_priv->mac_dev->ifInUcastPkts;
	buf[11] = (u64) tse_priv->mac_dev->ifInMulticastPkts;
	buf[12] = (u64) tse_priv->mac_dev->ifInBroadcastPkts;
	buf[13] = (u64) tse_priv->mac_dev->ifOutDiscards;
	buf[14] = (u64) tse_priv->mac_dev->ifOutUcastPkts;
	buf[15] = (u64) tse_priv->mac_dev->ifOutMulticastPkts;
	buf[16] = (u64) tse_priv->mac_dev->ifOutBroadcastPkts;
	buf[17] = (u64) tse_priv->mac_dev->etherStatsDropEvent;
	buf[18] = (u64) tse_priv->mac_dev->etherStatsOctets;
	buf[19] = (u64) tse_priv->mac_dev->etherStatsPkts;
	buf[20] = (u64) tse_priv->mac_dev->etherStatsUndersizePkts;
	buf[21] = (u64) tse_priv->mac_dev->etherStatsOversizePkts;
	buf[22] = (u64) tse_priv->mac_dev->etherStatsPkts64Octets;
	buf[23] = (u64) tse_priv->mac_dev->etherStatsPkts65to127Octets;
	buf[24] = (u64) tse_priv->mac_dev->etherStatsPkts128to255Octets;
	buf[25] = (u64) tse_priv->mac_dev->etherStatsPkts256to511Octets;
	buf[26] = (u64) tse_priv->mac_dev->etherStatsPkts512to1023Octets;
	buf[27] = (u64) tse_priv->mac_dev->etherStatsPkts1024to1518Octets;
	buf[28] = (u64) tse_priv->mac_dev->etherStatsPkts1519toXOctets;
	buf[29] = (u64) tse_priv->mac_dev->etherStatsJabbers;
	buf[30] = (u64) tse_priv->mac_dev->etherStatsFragments;
	buf[31] = (u64) tse_priv->mac_dev->ipaccTxConf;
	buf[32] = (u64) tse_priv->mac_dev->ipaccRxConf;
	buf[33] = (u64) tse_priv->mac_dev->ipaccRxStat;
	buf[34] = (u64) tse_priv->mac_dev->ipaccRxStatSum;
}

static int tse_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return TSE_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static u32 tse_get_msglevel(struct net_device *dev)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	return tse_priv->msg_enable;
}

static void tse_set_msglevel(struct net_device *dev, uint32_t data)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	tse_priv->msg_enable = data;
}

static int tse_reglen(struct net_device *dev)
{
	return sizeof(struct alt_tse_private);
}

static void tse_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			void *regbuf)
{
	int i;
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	u32 *tse_mac_regs = (u32 *) tse_priv;
	u32 *buf = (u32 *) regbuf;

	for (i = 0; i < sizeof(struct alt_tse_private) / sizeof(u32); i++)
		buf[i] = tse_mac_regs[i];
}

static int tse_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	struct phy_device *phydev = tse_priv->phydev;

	if (phydev == NULL)
		return -ENODEV;

	return phy_ethtool_gset(phydev, cmd);
}

static int tse_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	struct phy_device *phydev = tse_priv->phydev;

	if (phydev == NULL)
		return -ENODEV;

	return phy_ethtool_sset(phydev, cmd);
}

static const struct ethtool_ops tse_ethtool_ops = {
	.get_drvinfo = tse_get_drvinfo,
	.get_regs_len = tse_reglen,
	.get_regs = tse_get_regs,
	.get_link = ethtool_op_get_link,
	.get_settings = tse_get_settings,
	.set_settings = tse_set_settings,
	.get_strings = tse_gstrings,
	.get_sset_count = tse_sset_count,
	.get_ethtool_stats = tse_fill_stats,
	.get_msglevel = tse_get_msglevel,
	.set_msglevel = tse_set_msglevel,
};

void tse_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &tse_ethtool_ops);
}
