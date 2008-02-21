/*
 * Copyright (C) 2006-2008 PA Semi, Inc
 *
 * Ethtool hooks for the PA Semi PWRficient onchip 1G/10G Ethernet MACs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */


#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/inet_lro.h>

#include <asm/pasemi_dma.h>
#include "pasemi_mac.h"

static struct {
	const char str[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	{ "rx-drops" },
	{ "rx-bytes" },
	{ "rx-packets" },
	{ "rx-broadcast-packets" },
	{ "rx-multicast-packets" },
	{ "rx-crc-errors" },
	{ "rx-undersize-errors" },
	{ "rx-oversize-errors" },
	{ "rx-short-fragment-errors" },
	{ "rx-jabber-errors" },
	{ "rx-64-byte-packets" },
	{ "rx-65-127-byte-packets" },
	{ "rx-128-255-byte-packets" },
	{ "rx-256-511-byte-packets" },
	{ "rx-512-1023-byte-packets" },
	{ "rx-1024-1518-byte-packets" },
	{ "rx-pause-frames" },
	{ "tx-bytes" },
	{ "tx-packets" },
	{ "tx-broadcast-packets" },
	{ "tx-multicast-packets" },
	{ "tx-collisions" },
	{ "tx-late-collisions" },
	{ "tx-excessive-collisions" },
	{ "tx-crc-errors" },
	{ "tx-undersize-errors" },
	{ "tx-oversize-errors" },
	{ "tx-64-byte-packets" },
	{ "tx-65-127-byte-packets" },
	{ "tx-128-255-byte-packets" },
	{ "tx-256-511-byte-packets" },
	{ "tx-512-1023-byte-packets" },
	{ "tx-1024-1518-byte-packets" },
};

static int
pasemi_mac_ethtool_get_settings(struct net_device *netdev,
			       struct ethtool_cmd *cmd)
{
	struct pasemi_mac *mac = netdev_priv(netdev);
	struct phy_device *phydev = mac->phydev;

	return phy_ethtool_gset(phydev, cmd);
}

static void
pasemi_mac_ethtool_get_drvinfo(struct net_device *netdev,
			       struct ethtool_drvinfo *drvinfo)
{
	struct pasemi_mac *mac;
	mac = netdev_priv(netdev);

	/* clear and fill out info */
	memset(drvinfo, 0, sizeof(struct ethtool_drvinfo));
	strncpy(drvinfo->driver, "pasemi_mac", 12);
	strcpy(drvinfo->version, "N/A");
	strcpy(drvinfo->fw_version, "N/A");
	strncpy(drvinfo->bus_info, pci_name(mac->pdev), 32);
}

static u32
pasemi_mac_ethtool_get_msglevel(struct net_device *netdev)
{
	struct pasemi_mac *mac = netdev_priv(netdev);
	return mac->msg_enable;
}

static void
pasemi_mac_ethtool_set_msglevel(struct net_device *netdev,
				u32 level)
{
	struct pasemi_mac *mac = netdev_priv(netdev);
	mac->msg_enable = level;
}


static void
pasemi_mac_ethtool_get_ringparam(struct net_device *netdev,
				 struct ethtool_ringparam *ering)
{
	struct pasemi_mac *mac = netdev->priv;

	ering->tx_max_pending = TX_RING_SIZE/2;
	ering->tx_pending = RING_USED(mac->tx)/2;
	ering->rx_max_pending = RX_RING_SIZE/4;
	ering->rx_pending = RING_USED(mac->rx)/4;
}

static int pasemi_mac_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(ethtool_stats_keys);
	default:
		return -EOPNOTSUPP;
	}
}

static void pasemi_mac_get_ethtool_stats(struct net_device *netdev,
		struct ethtool_stats *stats, u64 *data)
{
	struct pasemi_mac *mac = netdev->priv;
	int i;

	data[0] = pasemi_read_dma_reg(PAS_DMA_RXINT_RCMDSTA(mac->dma_if))
			>> PAS_DMA_RXINT_RCMDSTA_DROPS_S;
	for (i = 0; i < 32; i++)
		data[1+i] = pasemi_read_mac_reg(mac->dma_if, PAS_MAC_RMON(i));
}

static void pasemi_mac_get_strings(struct net_device *netdev, u32 stringset,
				   u8 *data)
{
	memcpy(data, ethtool_stats_keys, sizeof(ethtool_stats_keys));
}

const struct ethtool_ops pasemi_mac_ethtool_ops = {
	.get_settings		= pasemi_mac_ethtool_get_settings,
	.get_drvinfo		= pasemi_mac_ethtool_get_drvinfo,
	.get_msglevel		= pasemi_mac_ethtool_get_msglevel,
	.set_msglevel		= pasemi_mac_ethtool_set_msglevel,
	.get_link		= ethtool_op_get_link,
	.get_ringparam          = pasemi_mac_ethtool_get_ringparam,
	.get_strings		= pasemi_mac_get_strings,
	.get_sset_count		= pasemi_mac_get_sset_count,
	.get_ethtool_stats	= pasemi_mac_get_ethtool_stats,
};

