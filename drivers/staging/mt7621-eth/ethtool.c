/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#include "mtk_eth_soc.h"
#include "ethtool.h"

static const char mtk_gdma_str[][ETH_GSTRING_LEN] = {
#define _FE(x...)	# x,
MTK_STAT_REG_DECLARE
#undef _FE
};

static int mtk_get_link_ksettings(struct net_device *dev,
				  struct ethtool_link_ksettings *cmd)
{
	struct mtk_mac *mac = netdev_priv(dev);
	int err;

	if (!mac->phy_dev)
		return -ENODEV;

	if (mac->phy_flags == MTK_PHY_FLAG_ATTACH) {
		err = phy_read_status(mac->phy_dev);
		if (err)
			return -ENODEV;
	}

	phy_ethtool_ksettings_get(mac->phy_dev, cmd);
	return 0;
}

static int mtk_set_link_ksettings(struct net_device *dev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct mtk_mac *mac = netdev_priv(dev);

	if (!mac->phy_dev)
		return -ENODEV;

	if (cmd->base.phy_address != mac->phy_dev->mdio.addr) {
		if (mac->hw->phy->phy_node[cmd->base.phy_address]) {
			mac->phy_dev = mac->hw->phy->phy[cmd->base.phy_address];
			mac->phy_flags = MTK_PHY_FLAG_PORT;
		} else if (mac->hw->mii_bus) {
			mac->phy_dev = mdiobus_get_phy(mac->hw->mii_bus, cmd->base.phy_address);
			if (!mac->phy_dev)
				return -ENODEV;
			mac->phy_flags = MTK_PHY_FLAG_ATTACH;
		} else {
			return -ENODEV;
		}
	}

	return phy_ethtool_ksettings_set(mac->phy_dev, cmd);
}

static void mtk_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_soc_data *soc = mac->hw->soc;

	strlcpy(info->driver, mac->hw->dev->driver->name, sizeof(info->driver));
	strlcpy(info->bus_info, dev_name(mac->hw->dev), sizeof(info->bus_info));

	if (soc->reg_table[MTK_REG_MTK_COUNTER_BASE])
		info->n_stats = ARRAY_SIZE(mtk_gdma_str);
}

static u32 mtk_get_msglevel(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);

	return mac->hw->msg_enable;
}

static void mtk_set_msglevel(struct net_device *dev, u32 value)
{
	struct mtk_mac *mac = netdev_priv(dev);

	mac->hw->msg_enable = value;
}

static int mtk_nway_reset(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);

	if (!mac->phy_dev)
		return -EOPNOTSUPP;

	return genphy_restart_aneg(mac->phy_dev);
}

static u32 mtk_get_link(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	int err;

	if (!mac->phy_dev)
		goto out_get_link;

	if (mac->phy_flags == MTK_PHY_FLAG_ATTACH) {
		err = genphy_update_link(mac->phy_dev);
		if (err)
			goto out_get_link;
	}

	return mac->phy_dev->link;

out_get_link:
	return ethtool_op_get_link(dev);
}

static int mtk_set_ringparam(struct net_device *dev,
			     struct ethtool_ringparam *ring)
{
	struct mtk_mac *mac = netdev_priv(dev);

	if ((ring->tx_pending < 2) ||
	    (ring->rx_pending < 2) ||
	    (ring->rx_pending > mac->hw->soc->dma_ring_size) ||
	    (ring->tx_pending > mac->hw->soc->dma_ring_size))
		return -EINVAL;

	dev->netdev_ops->ndo_stop(dev);

	mac->hw->tx_ring.tx_ring_size = BIT(fls(ring->tx_pending) - 1);
	mac->hw->rx_ring[0].rx_ring_size = BIT(fls(ring->rx_pending) - 1);

	return dev->netdev_ops->ndo_open(dev);
}

static void mtk_get_ringparam(struct net_device *dev,
			      struct ethtool_ringparam *ring)
{
	struct mtk_mac *mac = netdev_priv(dev);

	ring->rx_max_pending = mac->hw->soc->dma_ring_size;
	ring->tx_max_pending = mac->hw->soc->dma_ring_size;
	ring->rx_pending = mac->hw->rx_ring[0].rx_ring_size;
	ring->tx_pending = mac->hw->tx_ring.tx_ring_size;
}

static void mtk_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, *mtk_gdma_str, sizeof(mtk_gdma_str));
		break;
	}
}

static int mtk_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(mtk_gdma_str);
	default:
		return -EOPNOTSUPP;
	}
}

static void mtk_get_ethtool_stats(struct net_device *dev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_hw_stats *hwstats = mac->hw_stats;
	u64 *data_src, *data_dst;
	unsigned int start;
	int i;

	if (netif_running(dev) && netif_device_present(dev)) {
		if (spin_trylock(&hwstats->stats_lock)) {
			mtk_stats_update_mac(mac);
			spin_unlock(&hwstats->stats_lock);
		}
	}

	do {
		data_src = &hwstats->tx_bytes;
		data_dst = data;
		start = u64_stats_fetch_begin_irq(&hwstats->syncp);

		for (i = 0; i < ARRAY_SIZE(mtk_gdma_str); i++)
			*data_dst++ = *data_src++;

	} while (u64_stats_fetch_retry_irq(&hwstats->syncp, start));
}

static struct ethtool_ops mtk_ethtool_ops = {
	.get_link_ksettings     = mtk_get_link_ksettings,
	.set_link_ksettings     = mtk_set_link_ksettings,
	.get_drvinfo		= mtk_get_drvinfo,
	.get_msglevel		= mtk_get_msglevel,
	.set_msglevel		= mtk_set_msglevel,
	.nway_reset		= mtk_nway_reset,
	.get_link		= mtk_get_link,
	.set_ringparam		= mtk_set_ringparam,
	.get_ringparam		= mtk_get_ringparam,
};

void mtk_set_ethtool_ops(struct net_device *netdev)
{
	struct mtk_mac *mac = netdev_priv(netdev);
	struct mtk_soc_data *soc = mac->hw->soc;

	if (soc->reg_table[MTK_REG_MTK_COUNTER_BASE]) {
		mtk_ethtool_ops.get_strings = mtk_get_strings;
		mtk_ethtool_ops.get_sset_count = mtk_get_sset_count;
		mtk_ethtool_ops.get_ethtool_stats = mtk_get_ethtool_stats;
	}

	netdev->ethtool_ops = &mtk_ethtool_ops;
}
