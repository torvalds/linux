/*
 * Network device driver for Cell Processor-Based Blade
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Authors : Utz Bacher <utz.bacher@de.ibm.com>
 *           Jens Osterkamp <Jens.Osterkamp@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/pci.h>

#include "spider_net.h"


#define SPIDER_NET_NUM_STATS 13

static struct {
	const char str[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	{ "tx_packets" },
	{ "tx_bytes" },
	{ "rx_packets" },
	{ "rx_bytes" },
	{ "tx_errors" },
	{ "tx_dropped" },
	{ "rx_dropped" },
	{ "rx_descriptor_error" },
	{ "tx_timeouts" },
	{ "alloc_rx_skb_error" },
	{ "rx_iommu_map_error" },
	{ "tx_iommu_map_error" },
	{ "rx_desc_unk_state" },
};

static int
spider_net_ethtool_get_settings(struct net_device *netdev,
			       struct ethtool_cmd *cmd)
{
	struct spider_net_card *card;
	card = netdev_priv(netdev);

	cmd->supported   = (SUPPORTED_1000baseT_Full |
			     SUPPORTED_FIBRE);
	cmd->advertising = (ADVERTISED_1000baseT_Full |
			     ADVERTISED_FIBRE);
	cmd->port = PORT_FIBRE;
	cmd->speed = card->phy.speed;
	cmd->duplex = DUPLEX_FULL;

	return 0;
}

static void
spider_net_ethtool_get_drvinfo(struct net_device *netdev,
			       struct ethtool_drvinfo *drvinfo)
{
	struct spider_net_card *card;
	card = netdev_priv(netdev);

	/* clear and fill out info */
	memset(drvinfo, 0, sizeof(struct ethtool_drvinfo));
	strncpy(drvinfo->driver, spider_net_driver_name, 32);
	strncpy(drvinfo->version, VERSION, 32);
	strcpy(drvinfo->fw_version, "no information");
	strncpy(drvinfo->bus_info, pci_name(card->pdev), 32);
}

static void
spider_net_ethtool_get_wol(struct net_device *netdev,
			   struct ethtool_wolinfo *wolinfo)
{
	/* no support for wol */
	wolinfo->supported = 0;
	wolinfo->wolopts = 0;
}

static u32
spider_net_ethtool_get_msglevel(struct net_device *netdev)
{
	struct spider_net_card *card;
	card = netdev_priv(netdev);
	return card->msg_enable;
}

static void
spider_net_ethtool_set_msglevel(struct net_device *netdev,
				u32 level)
{
	struct spider_net_card *card;
	card = netdev_priv(netdev);
	card->msg_enable = level;
}

static int
spider_net_ethtool_nway_reset(struct net_device *netdev)
{
	if (netif_running(netdev)) {
		spider_net_stop(netdev);
		spider_net_open(netdev);
	}
	return 0;
}

static u32
spider_net_ethtool_get_rx_csum(struct net_device *netdev)
{
	struct spider_net_card *card = netdev->priv;

	return card->options.rx_csum;
}

static int
spider_net_ethtool_set_rx_csum(struct net_device *netdev, u32 n)
{
	struct spider_net_card *card = netdev->priv;

	card->options.rx_csum = n;
	return 0;
}

static uint32_t
spider_net_ethtool_get_tx_csum(struct net_device *netdev)
{
        return (netdev->features & NETIF_F_HW_CSUM) != 0;
}

static int
spider_net_ethtool_set_tx_csum(struct net_device *netdev, uint32_t data)
{
        if (data)
                netdev->features |= NETIF_F_HW_CSUM;
        else
                netdev->features &= ~NETIF_F_HW_CSUM;

        return 0;
}

static void
spider_net_ethtool_get_ringparam(struct net_device *netdev,
				 struct ethtool_ringparam *ering)
{
	struct spider_net_card *card = netdev->priv;

	ering->tx_max_pending = SPIDER_NET_TX_DESCRIPTORS_MAX;
	ering->tx_pending = card->tx_chain.num_desc;
	ering->rx_max_pending = SPIDER_NET_RX_DESCRIPTORS_MAX;
	ering->rx_pending = card->rx_chain.num_desc;
}

static int spider_net_get_stats_count(struct net_device *netdev)
{
	return SPIDER_NET_NUM_STATS;
}

static void spider_net_get_ethtool_stats(struct net_device *netdev,
		struct ethtool_stats *stats, u64 *data)
{
	struct spider_net_card *card = netdev->priv;

	data[0] = card->netdev_stats.tx_packets;
	data[1] = card->netdev_stats.tx_bytes;
	data[2] = card->netdev_stats.rx_packets;
	data[3] = card->netdev_stats.rx_bytes;
	data[4] = card->netdev_stats.tx_errors;
	data[5] = card->netdev_stats.tx_dropped;
	data[6] = card->netdev_stats.rx_dropped;
	data[7] = card->spider_stats.rx_desc_error;
	data[8] = card->spider_stats.tx_timeouts;
	data[9] = card->spider_stats.alloc_rx_skb_error;
	data[10] = card->spider_stats.rx_iommu_map_error;
	data[11] = card->spider_stats.tx_iommu_map_error;
	data[12] = card->spider_stats.rx_desc_unk_state;
}

static void spider_net_get_strings(struct net_device *netdev, u32 stringset,
				   u8 *data)
{
	memcpy(data, ethtool_stats_keys, sizeof(ethtool_stats_keys));
}

const struct ethtool_ops spider_net_ethtool_ops = {
	.get_settings		= spider_net_ethtool_get_settings,
	.get_drvinfo		= spider_net_ethtool_get_drvinfo,
	.get_wol		= spider_net_ethtool_get_wol,
	.get_msglevel		= spider_net_ethtool_get_msglevel,
	.set_msglevel		= spider_net_ethtool_set_msglevel,
	.nway_reset		= spider_net_ethtool_nway_reset,
	.get_rx_csum		= spider_net_ethtool_get_rx_csum,
	.set_rx_csum		= spider_net_ethtool_set_rx_csum,
	.get_tx_csum		= spider_net_ethtool_get_tx_csum,
	.set_tx_csum		= spider_net_ethtool_set_tx_csum,
	.get_ringparam          = spider_net_ethtool_get_ringparam,
	.get_strings		= spider_net_get_strings,
	.get_stats_count	= spider_net_get_stats_count,
	.get_ethtool_stats	= spider_net_get_ethtool_stats,
};

