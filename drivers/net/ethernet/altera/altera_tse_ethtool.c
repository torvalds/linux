/* Ethtool support for Altera Triple-Speed Ethernet MAC driver
 * Copyright (C) 2008-2014 Altera Corporation. All rights reserved
 *
 * Contributors:
 *   Dalon Westergreen
 *   Thomas Chou
 *   Ian Abbott
 *   Yuriy Kozlov
 *   Tobias Klauser
 *   Andriy Smolskyy
 *   Roman Bulgakov
 *   Dmytro Mytarchuk
 *
 * Original driver contributed by SLS.
 * Major updates contributed by GlobalLogic
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/phy.h>

#include "altera_tse.h"

#define TSE_STATS_LEN	31
#define TSE_NUM_REGS	128

static char const stat_gstrings[][ETH_GSTRING_LEN] = {
	"tx_packets",
	"rx_packets",
	"rx_crc_errors",
	"rx_align_errors",
	"tx_bytes",
	"rx_bytes",
	"tx_pause",
	"rx_pause",
	"rx_errors",
	"tx_errors",
	"rx_unicast",
	"rx_multicast",
	"rx_broadcast",
	"tx_discards",
	"tx_unicast",
	"tx_multicast",
	"tx_broadcast",
	"ether_drops",
	"rx_total_bytes",
	"rx_total_packets",
	"rx_undersize",
	"rx_oversize",
	"rx_64_bytes",
	"rx_65_127_bytes",
	"rx_128_255_bytes",
	"rx_256_511_bytes",
	"rx_512_1023_bytes",
	"rx_1024_1518_bytes",
	"rx_gte_1519_bytes",
	"rx_jabbers",
	"rx_runts",
};

static void tse_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	u32 rev = ioread32(&priv->mac_dev->megacore_revision);

	strcpy(info->driver, "altera_tse");
	strcpy(info->version, "v8.0");
	snprintf(info->fw_version, ETHTOOL_FWVERS_LEN, "v%d.%d",
		 rev & 0xFFFF, (rev & 0xFFFF0000) >> 16);
	sprintf(info->bus_info, "platform");
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
	struct altera_tse_private *priv = netdev_priv(dev);
	u64 ext;

	buf[0] = csrrd32(priv->mac_dev,
			 tse_csroffs(frames_transmitted_ok));
	buf[1] = csrrd32(priv->mac_dev,
			 tse_csroffs(frames_received_ok));
	buf[2] = csrrd32(priv->mac_dev,
			 tse_csroffs(frames_check_sequence_errors));
	buf[3] = csrrd32(priv->mac_dev,
			 tse_csroffs(alignment_errors));

	/* Extended aOctetsTransmittedOK counter */
	ext = (u64) csrrd32(priv->mac_dev,
			    tse_csroffs(msb_octets_transmitted_ok)) << 32;

	ext |= csrrd32(priv->mac_dev,
		       tse_csroffs(octets_transmitted_ok));
	buf[4] = ext;

	/* Extended aOctetsReceivedOK counter */
	ext = (u64) csrrd32(priv->mac_dev,
			    tse_csroffs(msb_octets_received_ok)) << 32;

	ext |= csrrd32(priv->mac_dev,
		       tse_csroffs(octets_received_ok));
	buf[5] = ext;

	buf[6] = csrrd32(priv->mac_dev,
			 tse_csroffs(tx_pause_mac_ctrl_frames));
	buf[7] = csrrd32(priv->mac_dev,
			 tse_csroffs(rx_pause_mac_ctrl_frames));
	buf[8] = csrrd32(priv->mac_dev,
			 tse_csroffs(if_in_errors));
	buf[9] = csrrd32(priv->mac_dev,
			 tse_csroffs(if_out_errors));
	buf[10] = csrrd32(priv->mac_dev,
			  tse_csroffs(if_in_ucast_pkts));
	buf[11] = csrrd32(priv->mac_dev,
			  tse_csroffs(if_in_multicast_pkts));
	buf[12] = csrrd32(priv->mac_dev,
			  tse_csroffs(if_in_broadcast_pkts));
	buf[13] = csrrd32(priv->mac_dev,
			  tse_csroffs(if_out_discards));
	buf[14] = csrrd32(priv->mac_dev,
			  tse_csroffs(if_out_ucast_pkts));
	buf[15] = csrrd32(priv->mac_dev,
			  tse_csroffs(if_out_multicast_pkts));
	buf[16] = csrrd32(priv->mac_dev,
			  tse_csroffs(if_out_broadcast_pkts));
	buf[17] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_drop_events));

	/* Extended etherStatsOctets counter */
	ext = (u64) csrrd32(priv->mac_dev,
			    tse_csroffs(msb_ether_stats_octets)) << 32;
	ext |= csrrd32(priv->mac_dev,
		       tse_csroffs(ether_stats_octets));
	buf[18] = ext;

	buf[19] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_pkts));
	buf[20] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_undersize_pkts));
	buf[21] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_oversize_pkts));
	buf[22] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_pkts_64_octets));
	buf[23] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_pkts_65to127_octets));
	buf[24] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_pkts_128to255_octets));
	buf[25] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_pkts_256to511_octets));
	buf[26] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_pkts_512to1023_octets));
	buf[27] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_pkts_1024to1518_octets));
	buf[28] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_pkts_1519tox_octets));
	buf[29] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_jabbers));
	buf[30] = csrrd32(priv->mac_dev,
			  tse_csroffs(ether_stats_fragments));
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
	struct altera_tse_private *priv = netdev_priv(dev);
	return priv->msg_enable;
}

static void tse_set_msglevel(struct net_device *dev, uint32_t data)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	priv->msg_enable = data;
}

static int tse_reglen(struct net_device *dev)
{
	return TSE_NUM_REGS * sizeof(u32);
}

static void tse_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			 void *regbuf)
{
	int i;
	struct altera_tse_private *priv = netdev_priv(dev);
	u32 *buf = regbuf;

	/* Set version to a known value, so ethtool knows
	 * how to do any special formatting of this data.
	 * This version number will need to change if and
	 * when this register table is changed.
	 *
	 * version[31:0] = 1: Dump the first 128 TSE Registers
	 *      Upper bits are all 0 by default
	 *
	 * Upper 16-bits will indicate feature presence for
	 * Ethtool register decoding in future version.
	 */

	regs->version = 1;

	for (i = 0; i < TSE_NUM_REGS; i++)
		buf[i] = csrrd32(priv->mac_dev, i * 4);
}

static int tse_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;

	if (phydev == NULL)
		return -ENODEV;

	return phy_ethtool_gset(phydev, cmd);
}

static int tse_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct altera_tse_private *priv = netdev_priv(dev);
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

void altera_tse_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &tse_ethtool_ops;
}
