/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/ethtool.h>
#include <linux/phy.h>

#include "emac.h"

static const char * const emac_ethtool_stat_strings[] = {
	"rx_ok",
	"rx_bcast",
	"rx_mcast",
	"rx_pause",
	"rx_ctrl",
	"rx_fcs_err",
	"rx_len_err",
	"rx_byte_cnt",
	"rx_runt",
	"rx_frag",
	"rx_sz_64",
	"rx_sz_65_127",
	"rx_sz_128_255",
	"rx_sz_256_511",
	"rx_sz_512_1023",
	"rx_sz_1024_1518",
	"rx_sz_1519_max",
	"rx_sz_ov",
	"rx_rxf_ov",
	"rx_align_err",
	"rx_bcast_byte_cnt",
	"rx_mcast_byte_cnt",
	"rx_err_addr",
	"rx_crc_align",
	"rx_jabbers",
	"tx_ok",
	"tx_bcast",
	"tx_mcast",
	"tx_pause",
	"tx_exc_defer",
	"tx_ctrl",
	"tx_defer",
	"tx_byte_cnt",
	"tx_sz_64",
	"tx_sz_65_127",
	"tx_sz_128_255",
	"tx_sz_256_511",
	"tx_sz_512_1023",
	"tx_sz_1024_1518",
	"tx_sz_1519_max",
	"tx_1_col",
	"tx_2_col",
	"tx_late_col",
	"tx_abort_col",
	"tx_underrun",
	"tx_rd_eop",
	"tx_len_err",
	"tx_trunc",
	"tx_bcast_byte",
	"tx_mcast_byte",
	"tx_col",
};

#define EMAC_STATS_LEN	ARRAY_SIZE(emac_ethtool_stat_strings)

static u32 emac_get_msglevel(struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	return adpt->msg_enable;
}

static void emac_set_msglevel(struct net_device *netdev, u32 data)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	adpt->msg_enable = data;
}

static int emac_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return EMAC_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void emac_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	unsigned int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < EMAC_STATS_LEN; i++) {
			strlcpy(data, emac_ethtool_stat_strings[i],
				ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void emac_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats,
				   u64 *data)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	spin_lock(&adpt->stats.lock);

	emac_update_hw_stats(adpt);
	memcpy(data, &adpt->stats, EMAC_STATS_LEN * sizeof(u64));

	spin_unlock(&adpt->stats.lock);
}

static int emac_nway_reset(struct net_device *netdev)
{
	struct phy_device *phydev = netdev->phydev;

	if (!phydev)
		return -ENODEV;

	return genphy_restart_aneg(phydev);
}

static void emac_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	ring->rx_max_pending = EMAC_MAX_RX_DESCS;
	ring->tx_max_pending = EMAC_MAX_TX_DESCS;
	ring->rx_pending = adpt->rx_desc_cnt;
	ring->tx_pending = adpt->tx_desc_cnt;
}

static void emac_get_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct phy_device *phydev = netdev->phydev;

	if (phydev) {
		if (phydev->autoneg)
			pause->autoneg = 1;
		if (phydev->pause)
			pause->rx_pause = 1;
		if (phydev->pause != phydev->asym_pause)
			pause->tx_pause = 1;
	}
}

static const struct ethtool_ops emac_ethtool_ops = {
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,

	.get_msglevel    = emac_get_msglevel,
	.set_msglevel    = emac_set_msglevel,

	.get_sset_count  = emac_get_sset_count,
	.get_strings = emac_get_strings,
	.get_ethtool_stats = emac_get_ethtool_stats,

	.get_ringparam = emac_get_ringparam,
	.get_pauseparam = emac_get_pauseparam,

	.nway_reset = emac_nway_reset,

	.get_link = ethtool_op_get_link,
};

void emac_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &emac_ethtool_ops;
}
