/*
 * Copyright(c) 2005 - 2006 Attansic Corporation. All rights reserved.
 * Copyright(c) 2006 Chris Snook <csnook@redhat.com>
 * Copyright(c) 2006 Jay Cliburn <jcliburn@gmail.com>
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/mii.h>
#include <asm/uaccess.h>

#include "atl1.h"

struct atl1_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define ATL1_STAT(m) sizeof(((struct atl1_adapter *)0)->m), \
	offsetof(struct atl1_adapter, m)

static struct atl1_stats atl1_gstrings_stats[] = {
	{"rx_packets", ATL1_STAT(soft_stats.rx_packets)},
	{"tx_packets", ATL1_STAT(soft_stats.tx_packets)},
	{"rx_bytes", ATL1_STAT(soft_stats.rx_bytes)},
	{"tx_bytes", ATL1_STAT(soft_stats.tx_bytes)},
	{"rx_errors", ATL1_STAT(soft_stats.rx_errors)},
	{"tx_errors", ATL1_STAT(soft_stats.tx_errors)},
	{"rx_dropped", ATL1_STAT(net_stats.rx_dropped)},
	{"tx_dropped", ATL1_STAT(net_stats.tx_dropped)},
	{"multicast", ATL1_STAT(soft_stats.multicast)},
	{"collisions", ATL1_STAT(soft_stats.collisions)},
	{"rx_length_errors", ATL1_STAT(soft_stats.rx_length_errors)},
	{"rx_over_errors", ATL1_STAT(soft_stats.rx_missed_errors)},
	{"rx_crc_errors", ATL1_STAT(soft_stats.rx_crc_errors)},
	{"rx_frame_errors", ATL1_STAT(soft_stats.rx_frame_errors)},
	{"rx_fifo_errors", ATL1_STAT(soft_stats.rx_fifo_errors)},
	{"rx_missed_errors", ATL1_STAT(soft_stats.rx_missed_errors)},
	{"tx_aborted_errors", ATL1_STAT(soft_stats.tx_aborted_errors)},
	{"tx_carrier_errors", ATL1_STAT(soft_stats.tx_carrier_errors)},
	{"tx_fifo_errors", ATL1_STAT(soft_stats.tx_fifo_errors)},
	{"tx_window_errors", ATL1_STAT(soft_stats.tx_window_errors)},
	{"tx_abort_exce_coll", ATL1_STAT(soft_stats.excecol)},
	{"tx_abort_late_coll", ATL1_STAT(soft_stats.latecol)},
	{"tx_deferred_ok", ATL1_STAT(soft_stats.deffer)},
	{"tx_single_coll_ok", ATL1_STAT(soft_stats.scc)},
	{"tx_multi_coll_ok", ATL1_STAT(soft_stats.mcc)},
	{"tx_underun", ATL1_STAT(soft_stats.tx_underun)},
	{"tx_trunc", ATL1_STAT(soft_stats.tx_trunc)},
	{"tx_pause", ATL1_STAT(soft_stats.tx_pause)},
	{"rx_pause", ATL1_STAT(soft_stats.rx_pause)},
	{"rx_rrd_ov", ATL1_STAT(soft_stats.rx_rrd_ov)},
	{"rx_trunc", ATL1_STAT(soft_stats.rx_trunc)}
};

static void atl1_get_ethtool_stats(struct net_device *netdev,
				struct ethtool_stats *stats, u64 *data)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	int i;
	char *p;

	for (i = 0; i < ARRAY_SIZE(atl1_gstrings_stats); i++) {
		p = (char *)adapter+atl1_gstrings_stats[i].stat_offset;
		data[i] = (atl1_gstrings_stats[i].sizeof_stat ==
			sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

}

static int atl1_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(atl1_gstrings_stats);
	default:
		return -EOPNOTSUPP;
	}
}

static int atl1_get_settings(struct net_device *netdev,
				struct ethtool_cmd *ecmd)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;

	ecmd->supported = (SUPPORTED_10baseT_Half |
			   SUPPORTED_10baseT_Full |
			   SUPPORTED_100baseT_Half |
			   SUPPORTED_100baseT_Full |
			   SUPPORTED_1000baseT_Full |
			   SUPPORTED_Autoneg | SUPPORTED_TP);
	ecmd->advertising = ADVERTISED_TP;
	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL) {
		ecmd->advertising |= ADVERTISED_Autoneg;
		if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR) {
			ecmd->advertising |= ADVERTISED_Autoneg;
			ecmd->advertising |=
			    (ADVERTISED_10baseT_Half |
			     ADVERTISED_10baseT_Full |
			     ADVERTISED_100baseT_Half |
			     ADVERTISED_100baseT_Full |
			     ADVERTISED_1000baseT_Full);
		}
		else
			ecmd->advertising |= (ADVERTISED_1000baseT_Full);
	}
	ecmd->port = PORT_TP;
	ecmd->phy_address = 0;
	ecmd->transceiver = XCVR_INTERNAL;

	if (netif_carrier_ok(adapter->netdev)) {
		u16 link_speed, link_duplex;
		atl1_get_speed_and_duplex(hw, &link_speed, &link_duplex);
		ecmd->speed = link_speed;
		if (link_duplex == FULL_DUPLEX)
			ecmd->duplex = DUPLEX_FULL;
		else
			ecmd->duplex = DUPLEX_HALF;
	} else {
		ecmd->speed = -1;
		ecmd->duplex = -1;
	}
	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL)
		ecmd->autoneg = AUTONEG_ENABLE;
	else
		ecmd->autoneg = AUTONEG_DISABLE;

	return 0;
}

static int atl1_set_settings(struct net_device *netdev,
				struct ethtool_cmd *ecmd)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;
	u16 phy_data;
	int ret_val = 0;
	u16 old_media_type = hw->media_type;

	if (netif_running(adapter->netdev)) {
		dev_dbg(&adapter->pdev->dev, "ethtool shutting down adapter\n");
		atl1_down(adapter);
	}

	if (ecmd->autoneg == AUTONEG_ENABLE)
		hw->media_type = MEDIA_TYPE_AUTO_SENSOR;
	else {
		if (ecmd->speed == SPEED_1000) {
			if (ecmd->duplex != DUPLEX_FULL) {
				dev_warn(&adapter->pdev->dev,
					"can't force to 1000M half duplex\n");
				ret_val = -EINVAL;
				goto exit_sset;
			}
			hw->media_type = MEDIA_TYPE_1000M_FULL;
		} else if (ecmd->speed == SPEED_100) {
			if (ecmd->duplex == DUPLEX_FULL) {
				hw->media_type = MEDIA_TYPE_100M_FULL;
			} else
				hw->media_type = MEDIA_TYPE_100M_HALF;
		} else {
			if (ecmd->duplex == DUPLEX_FULL)
				hw->media_type = MEDIA_TYPE_10M_FULL;
			else
				hw->media_type = MEDIA_TYPE_10M_HALF;
		}
	}
	switch (hw->media_type) {
	case MEDIA_TYPE_AUTO_SENSOR:
		ecmd->advertising =
		    ADVERTISED_10baseT_Half |
		    ADVERTISED_10baseT_Full |
		    ADVERTISED_100baseT_Half |
		    ADVERTISED_100baseT_Full |
		    ADVERTISED_1000baseT_Full |
		    ADVERTISED_Autoneg | ADVERTISED_TP;
		break;
	case MEDIA_TYPE_1000M_FULL:
		ecmd->advertising =
		    ADVERTISED_1000baseT_Full |
		    ADVERTISED_Autoneg | ADVERTISED_TP;
		break;
	default:
		ecmd->advertising = 0;
		break;
	}
	if (atl1_phy_setup_autoneg_adv(hw)) {
		ret_val = -EINVAL;
		dev_warn(&adapter->pdev->dev,
			"invalid ethtool speed/duplex setting\n");
		goto exit_sset;
	}
	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL)
		phy_data = MII_CR_RESET | MII_CR_AUTO_NEG_EN;
	else {
		switch (hw->media_type) {
		case MEDIA_TYPE_100M_FULL:
			phy_data =
			    MII_CR_FULL_DUPLEX | MII_CR_SPEED_100 |
			    MII_CR_RESET;
			break;
		case MEDIA_TYPE_100M_HALF:
			phy_data = MII_CR_SPEED_100 | MII_CR_RESET;
			break;
		case MEDIA_TYPE_10M_FULL:
			phy_data =
			    MII_CR_FULL_DUPLEX | MII_CR_SPEED_10 | MII_CR_RESET;
			break;
		default:	/* MEDIA_TYPE_10M_HALF: */
			phy_data = MII_CR_SPEED_10 | MII_CR_RESET;
			break;
		}
	}
	atl1_write_phy_reg(hw, MII_BMCR, phy_data);
exit_sset:
	if (ret_val)
		hw->media_type = old_media_type;

	if (netif_running(adapter->netdev)) {
		dev_dbg(&adapter->pdev->dev, "ethtool starting adapter\n");
		atl1_up(adapter);
	} else if (!ret_val) {
		dev_dbg(&adapter->pdev->dev, "ethtool resetting adapter\n");
		atl1_reset(adapter);
	}
	return ret_val;
}

static void atl1_get_drvinfo(struct net_device *netdev,
				struct ethtool_drvinfo *drvinfo)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);

	strncpy(drvinfo->driver, atl1_driver_name, sizeof(drvinfo->driver));
	strncpy(drvinfo->version, atl1_driver_version,
		sizeof(drvinfo->version));
	strncpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strncpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->eedump_len = ATL1_EEDUMP_LEN;
}

static void atl1_get_wol(struct net_device *netdev,
			    struct ethtool_wolinfo *wol)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);

	wol->supported = WAKE_UCAST | WAKE_MCAST | WAKE_BCAST | WAKE_MAGIC;
	wol->wolopts = 0;
	if (adapter->wol & ATL1_WUFC_EX)
		wol->wolopts |= WAKE_UCAST;
	if (adapter->wol & ATL1_WUFC_MC)
		wol->wolopts |= WAKE_MCAST;
	if (adapter->wol & ATL1_WUFC_BC)
		wol->wolopts |= WAKE_BCAST;
	if (adapter->wol & ATL1_WUFC_MAG)
		wol->wolopts |= WAKE_MAGIC;
	return;
}

static int atl1_set_wol(struct net_device *netdev,
			struct ethtool_wolinfo *wol)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & (WAKE_PHY | WAKE_ARP | WAKE_MAGICSECURE))
		return -EOPNOTSUPP;
	adapter->wol = 0;
	if (wol->wolopts & WAKE_UCAST)
		adapter->wol |= ATL1_WUFC_EX;
	if (wol->wolopts & WAKE_MCAST)
		adapter->wol |= ATL1_WUFC_MC;
	if (wol->wolopts & WAKE_BCAST)
		adapter->wol |= ATL1_WUFC_BC;
	if (wol->wolopts & WAKE_MAGIC)
		adapter->wol |= ATL1_WUFC_MAG;
	return 0;
}

static void atl1_get_ringparam(struct net_device *netdev,
			    struct ethtool_ringparam *ring)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_tpd_ring *txdr = &adapter->tpd_ring;
	struct atl1_rfd_ring *rxdr = &adapter->rfd_ring;

	ring->rx_max_pending = ATL1_MAX_RFD;
	ring->tx_max_pending = ATL1_MAX_TPD;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = rxdr->count;
	ring->tx_pending = txdr->count;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int atl1_set_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_tpd_ring *tpdr = &adapter->tpd_ring;
	struct atl1_rrd_ring *rrdr = &adapter->rrd_ring;
	struct atl1_rfd_ring *rfdr = &adapter->rfd_ring;

	struct atl1_tpd_ring tpd_old, tpd_new;
	struct atl1_rfd_ring rfd_old, rfd_new;
	struct atl1_rrd_ring rrd_old, rrd_new;
	struct atl1_ring_header rhdr_old, rhdr_new;
	int err;

	tpd_old = adapter->tpd_ring;
	rfd_old = adapter->rfd_ring;
	rrd_old = adapter->rrd_ring;
	rhdr_old = adapter->ring_header;

	if (netif_running(adapter->netdev))
		atl1_down(adapter);

	rfdr->count = (u16) max(ring->rx_pending, (u32) ATL1_MIN_RFD);
	rfdr->count = rfdr->count > ATL1_MAX_RFD ? ATL1_MAX_RFD :
			rfdr->count;
	rfdr->count = (rfdr->count + 3) & ~3;
	rrdr->count = rfdr->count;

	tpdr->count = (u16) max(ring->tx_pending, (u32) ATL1_MIN_TPD);
	tpdr->count = tpdr->count > ATL1_MAX_TPD ? ATL1_MAX_TPD :
			tpdr->count;
	tpdr->count = (tpdr->count + 3) & ~3;

	if (netif_running(adapter->netdev)) {
		/* try to get new resources before deleting old */
		err = atl1_setup_ring_resources(adapter);
		if (err)
			goto err_setup_ring;

		/*
		 * save the new, restore the old in order to free it,
		 * then restore the new back again
		 */

		rfd_new = adapter->rfd_ring;
		rrd_new = adapter->rrd_ring;
		tpd_new = adapter->tpd_ring;
		rhdr_new = adapter->ring_header;
		adapter->rfd_ring = rfd_old;
		adapter->rrd_ring = rrd_old;
		adapter->tpd_ring = tpd_old;
		adapter->ring_header = rhdr_old;
		atl1_free_ring_resources(adapter);
		adapter->rfd_ring = rfd_new;
		adapter->rrd_ring = rrd_new;
		adapter->tpd_ring = tpd_new;
		adapter->ring_header = rhdr_new;

		err = atl1_up(adapter);
		if (err)
			return err;
	}
	return 0;

err_setup_ring:
	adapter->rfd_ring = rfd_old;
	adapter->rrd_ring = rrd_old;
	adapter->tpd_ring = tpd_old;
	adapter->ring_header = rhdr_old;
	atl1_up(adapter);
	return err;
}

static void atl1_get_pauseparam(struct net_device *netdev,
			     struct ethtool_pauseparam *epause)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;

	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL) {
		epause->autoneg = AUTONEG_ENABLE;
	} else {
		epause->autoneg = AUTONEG_DISABLE;
	}
	epause->rx_pause = 1;
	epause->tx_pause = 1;
}

static int atl1_set_pauseparam(struct net_device *netdev,
			     struct ethtool_pauseparam *epause)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;

	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL) {
		epause->autoneg = AUTONEG_ENABLE;
	} else {
		epause->autoneg = AUTONEG_DISABLE;
	}

	epause->rx_pause = 1;
	epause->tx_pause = 1;

	return 0;
}

static u32 atl1_get_rx_csum(struct net_device *netdev)
{
	return 1;
}

static void atl1_get_strings(struct net_device *netdev, u32 stringset,
				u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(atl1_gstrings_stats); i++) {
			memcpy(p, atl1_gstrings_stats[i].stat_string,
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int atl1_nway_reset(struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;

	if (netif_running(netdev)) {
		u16 phy_data;
		atl1_down(adapter);

		if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
			hw->media_type == MEDIA_TYPE_1000M_FULL) {
			phy_data = MII_CR_RESET | MII_CR_AUTO_NEG_EN;
		} else {
			switch (hw->media_type) {
			case MEDIA_TYPE_100M_FULL:
				phy_data = MII_CR_FULL_DUPLEX |
					MII_CR_SPEED_100 | MII_CR_RESET;
				break;
			case MEDIA_TYPE_100M_HALF:
				phy_data = MII_CR_SPEED_100 | MII_CR_RESET;
				break;
			case MEDIA_TYPE_10M_FULL:
				phy_data = MII_CR_FULL_DUPLEX |
					MII_CR_SPEED_10 | MII_CR_RESET;
				break;
			default:  /* MEDIA_TYPE_10M_HALF */
				phy_data = MII_CR_SPEED_10 | MII_CR_RESET;
			}
		}
		atl1_write_phy_reg(hw, MII_BMCR, phy_data);
		atl1_up(adapter);
	}
	return 0;
}

const struct ethtool_ops atl1_ethtool_ops = {
	.get_settings		= atl1_get_settings,
	.set_settings		= atl1_set_settings,
	.get_drvinfo		= atl1_get_drvinfo,
	.get_wol		= atl1_get_wol,
	.set_wol		= atl1_set_wol,
	.get_ringparam		= atl1_get_ringparam,
	.set_ringparam		= atl1_set_ringparam,
	.get_pauseparam		= atl1_get_pauseparam,
	.set_pauseparam 	= atl1_set_pauseparam,
	.get_rx_csum		= atl1_get_rx_csum,
	.set_tx_csum		= ethtool_op_set_tx_hw_csum,
	.get_link		= ethtool_op_get_link,
	.set_sg			= ethtool_op_set_sg,
	.get_strings		= atl1_get_strings,
	.nway_reset		= atl1_nway_reset,
	.get_ethtool_stats	= atl1_get_ethtool_stats,
	.get_sset_count		= atl1_get_sset_count,
	.set_tso		= ethtool_op_set_tso,
};
