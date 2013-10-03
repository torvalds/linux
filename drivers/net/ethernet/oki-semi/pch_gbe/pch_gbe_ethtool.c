/*
 * Copyright (C) 1999 - 2010 Intel Corporation.
 * Copyright (C) 2010 OKI SEMICONDUCTOR Co., LTD.
 *
 * This code was derived from the Intel e1000e Linux driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */
#include "pch_gbe.h"
#include "pch_gbe_api.h"

/**
 * pch_gbe_stats - Stats item information
 */
struct pch_gbe_stats {
	char string[ETH_GSTRING_LEN];
	size_t size;
	size_t offset;
};

#define PCH_GBE_STAT(m)						\
{								\
	.string = #m,						\
	.size = FIELD_SIZEOF(struct pch_gbe_hw_stats, m),	\
	.offset = offsetof(struct pch_gbe_hw_stats, m),		\
}

/**
 * pch_gbe_gstrings_stats - ethtool information status name list
 */
static const struct pch_gbe_stats pch_gbe_gstrings_stats[] = {
	PCH_GBE_STAT(rx_packets),
	PCH_GBE_STAT(tx_packets),
	PCH_GBE_STAT(rx_bytes),
	PCH_GBE_STAT(tx_bytes),
	PCH_GBE_STAT(rx_errors),
	PCH_GBE_STAT(tx_errors),
	PCH_GBE_STAT(rx_dropped),
	PCH_GBE_STAT(tx_dropped),
	PCH_GBE_STAT(multicast),
	PCH_GBE_STAT(collisions),
	PCH_GBE_STAT(rx_crc_errors),
	PCH_GBE_STAT(rx_frame_errors),
	PCH_GBE_STAT(rx_alloc_buff_failed),
	PCH_GBE_STAT(tx_length_errors),
	PCH_GBE_STAT(tx_aborted_errors),
	PCH_GBE_STAT(tx_carrier_errors),
	PCH_GBE_STAT(tx_timeout_count),
	PCH_GBE_STAT(tx_restart_count),
	PCH_GBE_STAT(intr_rx_dsc_empty_count),
	PCH_GBE_STAT(intr_rx_frame_err_count),
	PCH_GBE_STAT(intr_rx_fifo_err_count),
	PCH_GBE_STAT(intr_rx_dma_err_count),
	PCH_GBE_STAT(intr_tx_fifo_err_count),
	PCH_GBE_STAT(intr_tx_dma_err_count),
	PCH_GBE_STAT(intr_tcpip_err_count)
};

#define PCH_GBE_QUEUE_STATS_LEN 0
#define PCH_GBE_GLOBAL_STATS_LEN	ARRAY_SIZE(pch_gbe_gstrings_stats)
#define PCH_GBE_STATS_LEN (PCH_GBE_GLOBAL_STATS_LEN + PCH_GBE_QUEUE_STATS_LEN)

#define PCH_GBE_MAC_REGS_LEN    (sizeof(struct pch_gbe_regs) / 4)
#define PCH_GBE_REGS_LEN        (PCH_GBE_MAC_REGS_LEN + PCH_GBE_PHY_REGS_LEN)
/**
 * pch_gbe_get_settings - Get device-specific settings
 * @netdev: Network interface device structure
 * @ecmd:   Ethtool command
 * Returns:
 *	0:			Successful.
 *	Negative value:		Failed.
 */
static int pch_gbe_get_settings(struct net_device *netdev,
				 struct ethtool_cmd *ecmd)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	int ret;

	ret = mii_ethtool_gset(&adapter->mii, ecmd);
	ecmd->supported &= ~(SUPPORTED_TP | SUPPORTED_1000baseT_Half);
	ecmd->advertising &= ~(ADVERTISED_TP | ADVERTISED_1000baseT_Half);

	if (!netif_carrier_ok(adapter->netdev))
		ethtool_cmd_speed_set(ecmd, -1);
	return ret;
}

/**
 * pch_gbe_set_settings - Set device-specific settings
 * @netdev: Network interface device structure
 * @ecmd:   Ethtool command
 * Returns:
 *	0:			Successful.
 *	Negative value:		Failed.
 */
static int pch_gbe_set_settings(struct net_device *netdev,
				 struct ethtool_cmd *ecmd)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 speed = ethtool_cmd_speed(ecmd);
	int ret;

	pch_gbe_hal_write_phy_reg(hw, MII_BMCR, BMCR_RESET);

	/* when set_settings() is called with a ethtool_cmd previously
	 * filled by get_settings() on a down link, speed is -1: */
	if (speed == UINT_MAX) {
		speed = SPEED_1000;
		ethtool_cmd_speed_set(ecmd, speed);
		ecmd->duplex = DUPLEX_FULL;
	}
	ret = mii_ethtool_sset(&adapter->mii, ecmd);
	if (ret) {
		netdev_err(netdev, "Error: mii_ethtool_sset\n");
		return ret;
	}
	hw->mac.link_speed = speed;
	hw->mac.link_duplex = ecmd->duplex;
	hw->phy.autoneg_advertised = ecmd->advertising;
	hw->mac.autoneg = ecmd->autoneg;

	/* reset the link */
	if (netif_running(adapter->netdev)) {
		pch_gbe_down(adapter);
		ret = pch_gbe_up(adapter);
	} else {
		pch_gbe_reset(adapter);
	}
	return ret;
}

/**
 * pch_gbe_get_regs_len - Report the size of device registers
 * @netdev: Network interface device structure
 * Returns: the size of device registers.
 */
static int pch_gbe_get_regs_len(struct net_device *netdev)
{
	return PCH_GBE_REGS_LEN * (int)sizeof(u32);
}

/**
 * pch_gbe_get_drvinfo - Report driver information
 * @netdev:  Network interface device structure
 * @drvinfo: Driver information structure
 */
static void pch_gbe_get_drvinfo(struct net_device *netdev,
				 struct ethtool_drvinfo *drvinfo)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	strlcpy(drvinfo->driver, KBUILD_MODNAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, pch_driver_version, sizeof(drvinfo->version));
	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->regdump_len = pch_gbe_get_regs_len(netdev);
}

/**
 * pch_gbe_get_regs - Get device registers
 * @netdev: Network interface device structure
 * @regs:   Ethtool register structure
 * @p:      Buffer pointer of read device register date
 */
static void pch_gbe_get_regs(struct net_device *netdev,
				struct ethtool_regs *regs, void *p)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	u32 *regs_buff = p;
	u16 i, tmp;

	regs->version = 0x1000000 | (__u32)pdev->revision << 16 | pdev->device;
	for (i = 0; i < PCH_GBE_MAC_REGS_LEN; i++)
		*regs_buff++ = ioread32(&hw->reg->INT_ST + i);
	/* PHY register */
	for (i = 0; i < PCH_GBE_PHY_REGS_LEN; i++) {
		pch_gbe_hal_read_phy_reg(&adapter->hw, i, &tmp);
		*regs_buff++ = tmp;
	}
}

/**
 * pch_gbe_get_wol - Report whether Wake-on-Lan is enabled
 * @netdev: Network interface device structure
 * @wol:    Wake-on-Lan information
 */
static void pch_gbe_get_wol(struct net_device *netdev,
				struct ethtool_wolinfo *wol)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	wol->supported = WAKE_UCAST | WAKE_MCAST | WAKE_BCAST | WAKE_MAGIC;
	wol->wolopts = 0;

	if ((adapter->wake_up_evt & PCH_GBE_WLC_IND))
		wol->wolopts |= WAKE_UCAST;
	if ((adapter->wake_up_evt & PCH_GBE_WLC_MLT))
		wol->wolopts |= WAKE_MCAST;
	if ((adapter->wake_up_evt & PCH_GBE_WLC_BR))
		wol->wolopts |= WAKE_BCAST;
	if ((adapter->wake_up_evt & PCH_GBE_WLC_MP))
		wol->wolopts |= WAKE_MAGIC;
}

/**
 * pch_gbe_set_wol - Turn Wake-on-Lan on or off
 * @netdev: Network interface device structure
 * @wol:    Pointer of wake-on-Lan information straucture
 * Returns:
 *	0:			Successful.
 *	Negative value:		Failed.
 */
static int pch_gbe_set_wol(struct net_device *netdev,
				struct ethtool_wolinfo *wol)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	if ((wol->wolopts & (WAKE_PHY | WAKE_ARP | WAKE_MAGICSECURE)))
		return -EOPNOTSUPP;
	/* these settings will always override what we currently have */
	adapter->wake_up_evt = 0;

	if ((wol->wolopts & WAKE_UCAST))
		adapter->wake_up_evt |= PCH_GBE_WLC_IND;
	if ((wol->wolopts & WAKE_MCAST))
		adapter->wake_up_evt |= PCH_GBE_WLC_MLT;
	if ((wol->wolopts & WAKE_BCAST))
		adapter->wake_up_evt |= PCH_GBE_WLC_BR;
	if ((wol->wolopts & WAKE_MAGIC))
		adapter->wake_up_evt |= PCH_GBE_WLC_MP;
	return 0;
}

/**
 * pch_gbe_nway_reset - Restart autonegotiation
 * @netdev: Network interface device structure
 * Returns:
 *	0:			Successful.
 *	Negative value:		Failed.
 */
static int pch_gbe_nway_reset(struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	return mii_nway_restart(&adapter->mii);
}

/**
 * pch_gbe_get_ringparam - Report ring sizes
 * @netdev:  Network interface device structure
 * @ring:    Ring param structure
 */
static void pch_gbe_get_ringparam(struct net_device *netdev,
					struct ethtool_ringparam *ring)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_tx_ring *txdr = adapter->tx_ring;
	struct pch_gbe_rx_ring *rxdr = adapter->rx_ring;

	ring->rx_max_pending = PCH_GBE_MAX_RXD;
	ring->tx_max_pending = PCH_GBE_MAX_TXD;
	ring->rx_pending = rxdr->count;
	ring->tx_pending = txdr->count;
}

/**
 * pch_gbe_set_ringparam - Set ring sizes
 * @netdev:  Network interface device structure
 * @ring:    Ring param structure
 * Returns
 *	0:			Successful.
 *	Negative value:		Failed.
 */
static int pch_gbe_set_ringparam(struct net_device *netdev,
					struct ethtool_ringparam *ring)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_tx_ring *txdr, *tx_old;
	struct pch_gbe_rx_ring *rxdr, *rx_old;
	int tx_ring_size, rx_ring_size;
	int err = 0;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;
	tx_ring_size = (int)sizeof(struct pch_gbe_tx_ring);
	rx_ring_size = (int)sizeof(struct pch_gbe_rx_ring);

	if ((netif_running(adapter->netdev)))
		pch_gbe_down(adapter);
	tx_old = adapter->tx_ring;
	rx_old = adapter->rx_ring;

	txdr = kzalloc(tx_ring_size, GFP_KERNEL);
	if (!txdr) {
		err = -ENOMEM;
		goto err_alloc_tx;
	}
	rxdr = kzalloc(rx_ring_size, GFP_KERNEL);
	if (!rxdr) {
		err = -ENOMEM;
		goto err_alloc_rx;
	}
	adapter->tx_ring = txdr;
	adapter->rx_ring = rxdr;

	rxdr->count =
		clamp_val(ring->rx_pending, PCH_GBE_MIN_RXD, PCH_GBE_MAX_RXD);
	rxdr->count = roundup(rxdr->count, PCH_GBE_RX_DESC_MULTIPLE);

	txdr->count =
		clamp_val(ring->tx_pending, PCH_GBE_MIN_RXD, PCH_GBE_MAX_RXD);
	txdr->count = roundup(txdr->count, PCH_GBE_TX_DESC_MULTIPLE);

	if ((netif_running(adapter->netdev))) {
		/* Try to get new resources before deleting old */
		err = pch_gbe_setup_rx_resources(adapter, adapter->rx_ring);
		if (err)
			goto err_setup_rx;
		err = pch_gbe_setup_tx_resources(adapter, adapter->tx_ring);
		if (err)
			goto err_setup_tx;
		/* save the new, restore the old in order to free it,
		 * then restore the new back again */
#ifdef RINGFREE
		adapter->rx_ring = rx_old;
		adapter->tx_ring = tx_old;
		pch_gbe_free_rx_resources(adapter, adapter->rx_ring);
		pch_gbe_free_tx_resources(adapter, adapter->tx_ring);
		kfree(tx_old);
		kfree(rx_old);
		adapter->rx_ring = rxdr;
		adapter->tx_ring = txdr;
#else
		pch_gbe_free_rx_resources(adapter, rx_old);
		pch_gbe_free_tx_resources(adapter, tx_old);
		kfree(tx_old);
		kfree(rx_old);
		adapter->rx_ring = rxdr;
		adapter->tx_ring = txdr;
#endif
		err = pch_gbe_up(adapter);
	}
	return err;

err_setup_tx:
	pch_gbe_free_rx_resources(adapter, adapter->rx_ring);
err_setup_rx:
	adapter->rx_ring = rx_old;
	adapter->tx_ring = tx_old;
	kfree(rxdr);
err_alloc_rx:
	kfree(txdr);
err_alloc_tx:
	if (netif_running(adapter->netdev))
		pch_gbe_up(adapter);
	return err;
}

/**
 * pch_gbe_get_pauseparam - Report pause parameters
 * @netdev:  Network interface device structure
 * @pause:   Pause parameters structure
 */
static void pch_gbe_get_pauseparam(struct net_device *netdev,
				       struct ethtool_pauseparam *pause)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;

	pause->autoneg =
	    ((hw->mac.fc_autoneg) ? AUTONEG_ENABLE : AUTONEG_DISABLE);

	if (hw->mac.fc == PCH_GBE_FC_RX_PAUSE) {
		pause->rx_pause = 1;
	} else if (hw->mac.fc == PCH_GBE_FC_TX_PAUSE) {
		pause->tx_pause = 1;
	} else if (hw->mac.fc == PCH_GBE_FC_FULL) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

/**
 * pch_gbe_set_pauseparam - Set pause paramters
 * @netdev:  Network interface device structure
 * @pause:   Pause parameters structure
 * Returns:
 *	0:			Successful.
 *	Negative value:		Failed.
 */
static int pch_gbe_set_pauseparam(struct net_device *netdev,
				       struct ethtool_pauseparam *pause)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	int ret = 0;

	hw->mac.fc_autoneg = pause->autoneg;
	if ((pause->rx_pause) && (pause->tx_pause))
		hw->mac.fc = PCH_GBE_FC_FULL;
	else if ((pause->rx_pause) && (!pause->tx_pause))
		hw->mac.fc = PCH_GBE_FC_RX_PAUSE;
	else if ((!pause->rx_pause) && (pause->tx_pause))
		hw->mac.fc = PCH_GBE_FC_TX_PAUSE;
	else if ((!pause->rx_pause) && (!pause->tx_pause))
		hw->mac.fc = PCH_GBE_FC_NONE;

	if (hw->mac.fc_autoneg == AUTONEG_ENABLE) {
		if ((netif_running(adapter->netdev))) {
			pch_gbe_down(adapter);
			ret = pch_gbe_up(adapter);
		} else {
			pch_gbe_reset(adapter);
		}
	} else {
		ret = pch_gbe_mac_force_mac_fc(hw);
	}
	return ret;
}

/**
 * pch_gbe_get_strings - Return a set of strings that describe the requested
 *			 objects
 * @netdev:    Network interface device structure
 * @stringset: Select the stringset. [ETH_SS_TEST] [ETH_SS_STATS]
 * @data:      Pointer of read string data.
 */
static void pch_gbe_get_strings(struct net_device *netdev, u32 stringset,
					u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case (u32) ETH_SS_STATS:
		for (i = 0; i < PCH_GBE_GLOBAL_STATS_LEN; i++) {
			memcpy(p, pch_gbe_gstrings_stats[i].string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

/**
 * pch_gbe_get_ethtool_stats - Return statistics about the device
 * @netdev: Network interface device structure
 * @stats:  Ethtool statue structure
 * @data:   Pointer of read status area
 */
static void pch_gbe_get_ethtool_stats(struct net_device *netdev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	int i;
	const struct pch_gbe_stats *gstats = pch_gbe_gstrings_stats;
	char *hw_stats = (char *)&adapter->stats;

	pch_gbe_update_stats(adapter);
	for (i = 0; i < PCH_GBE_GLOBAL_STATS_LEN; i++) {
		char *p = hw_stats + gstats->offset;
		data[i] = gstats->size == sizeof(u64) ? *(u64 *)p:(*(u32 *)p);
		gstats++;
	}
}

static int pch_gbe_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return PCH_GBE_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ethtool_ops pch_gbe_ethtool_ops = {
	.get_settings = pch_gbe_get_settings,
	.set_settings = pch_gbe_set_settings,
	.get_drvinfo = pch_gbe_get_drvinfo,
	.get_regs_len = pch_gbe_get_regs_len,
	.get_regs = pch_gbe_get_regs,
	.get_wol = pch_gbe_get_wol,
	.set_wol = pch_gbe_set_wol,
	.nway_reset = pch_gbe_nway_reset,
	.get_link = ethtool_op_get_link,
	.get_ringparam = pch_gbe_get_ringparam,
	.set_ringparam = pch_gbe_set_ringparam,
	.get_pauseparam = pch_gbe_get_pauseparam,
	.set_pauseparam = pch_gbe_set_pauseparam,
	.get_strings = pch_gbe_get_strings,
	.get_ethtool_stats = pch_gbe_get_ethtool_stats,
	.get_sset_count = pch_gbe_get_sset_count,
};

void pch_gbe_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &pch_gbe_ethtool_ops);
}
