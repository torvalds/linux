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

#define TSE_STATS_LEN 31

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
	"etherStatsDropEvents",
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
};

static void tse_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	u32 rev = ioread32(&priv->regs->mac.megacore_revision);

	strcpy(info->driver, "Altera TSE MAC IP Driver");
	strcpy(info->version, "v8.0");
	snprintf(info->fw_version, ETHTOOL_FWVERS_LEN, "v%d.%d",
			rev & 0xFFFF, (rev & 0xFFFF0000) >> 16);
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
	struct alt_tse_private *priv = netdev_priv(dev);
	struct alt_tse_mac *mac = &priv->regs->mac;
	u64 ext;

	buf[0] = (u64) ioread32(&mac->aFramesTransmittedOK);
	buf[1] = (u64) ioread32(&mac->aFramesReceivedOK);
	buf[2] = (u64) ioread32(&mac->aFramesCheckSequenceErrors);
	buf[3] = (u64) ioread32(&mac->aAlignmentErrors);

	/* Extended aOctetsTransmittedOK counter */
	ext = (u64) ioread32(&mac->msb_aOctetsTransmittedOK) << 32;
	ext |= (u64) ioread32(&mac->aOctetsTransmittedOK);
	buf[4] = ext;

	/* Extended aOctetsReceivedOK counter */
	ext = (u64) ioread32(&mac->msb_aOctetsReceivedOK) << 32;
	ext |= (u64) ioread32(&mac->aOctetsReceivedOK);
	buf[5] = ext;

	buf[6] = (u64) ioread32(&mac->aTxPAUSEMACCtrlFrames);
	buf[7] = (u64) ioread32(&mac->aRxPAUSEMACCtrlFrames);
	buf[8] = (u64) ioread32(&mac->ifInErrors);
	buf[9] = (u64) ioread32(&mac->ifOutErrors);
	buf[10] = (u64) ioread32(&mac->ifInUcastPkts);
	buf[11] = (u64) ioread32(&mac->ifInMulticastPkts);
	buf[12] = (u64) ioread32(&mac->ifInBroadcastPkts);
	buf[13] = (u64) ioread32(&mac->ifOutDiscards);
	buf[14] = (u64) ioread32(&mac->ifOutUcastPkts);
	buf[15] = (u64) ioread32(&mac->ifOutMulticastPkts);
	buf[16] = (u64) ioread32(&mac->ifOutBroadcastPkts);
	buf[17] = (u64) ioread32(&mac->etherStatsDropEvents);

	/* Extended etherStatsOctets counter */
	ext = (u64) ioread32(&mac->msb_etherStatsOctets) << 32;
	ext |= (u64) ioread32(&mac->etherStatsOctets);
	buf[18] = ext;

	buf[19] = (u64) ioread32(&mac->etherStatsPkts);
	buf[20] = (u64) ioread32(&mac->etherStatsUndersizePkts);
	buf[21] = (u64) ioread32(&mac->etherStatsOversizePkts);
	buf[22] = (u64) ioread32(&mac->etherStatsPkts64Octets);
	buf[23] = (u64) ioread32(&mac->etherStatsPkts65to127Octets);
	buf[24] = (u64) ioread32(&mac->etherStatsPkts128to255Octets);
	buf[25] = (u64) ioread32(&mac->etherStatsPkts256to511Octets);
	buf[26] = (u64) ioread32(&mac->etherStatsPkts512to1023Octets);
	buf[27] = (u64) ioread32(&mac->etherStatsPkts1024to1518Octets);
	buf[28] = (u64) ioread32(&mac->etherStatsPkts1519toXOctets);
	buf[29] = (u64) ioread32(&mac->etherStatsJabbers);
	buf[30] = (u64) ioread32(&mac->etherStatsFragments);
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
	struct alt_tse_private *priv = netdev_priv(dev);
	return priv->msg_enable;
}

static void tse_set_msglevel(struct net_device *dev, uint32_t data)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	priv->msg_enable = data;
}

static int tse_reglen(struct net_device *dev)
{
	return sizeof(struct alt_tse_private);
}

static void tse_get_regs(struct net_device *dev, struct ethtool_regs *regs,
				void *regbuf)
{
	int i;
	struct alt_tse_private *priv = netdev_priv(dev);
	u32 *tse_mac_regs = (u32 *) priv;
	u32 *buf = (u32 *) regbuf;

	for (i = 0; i < sizeof(struct alt_tse_private) / sizeof(u32); i++)
		buf[i] = tse_mac_regs[i];
}

static int tse_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;

	if (phydev == NULL)
		return -ENODEV;

	return phy_ethtool_gset(phydev, cmd);
}

static int tse_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;

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
