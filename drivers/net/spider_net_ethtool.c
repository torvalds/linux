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
	strncpy(drvinfo->version, "0.1", 32);
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

struct ethtool_ops spider_net_ethtool_ops = {
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
};

