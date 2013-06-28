/* atlx.c -- common functions for Attansic network drivers
 *
 * Copyright(c) 2005 - 2006 Attansic Corporation. All rights reserved.
 * Copyright(c) 2006 - 2007 Chris Snook <csnook@redhat.com>
 * Copyright(c) 2006 - 2008 Jay Cliburn <jcliburn@gmail.com>
 * Copyright(c) 2007 Atheros Corporation. All rights reserved.
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

/* Including this file like a header is a temporary hack, I promise. -- CHS */
#ifndef ATLX_C
#define ATLX_C

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "atlx.h"

static s32 atlx_read_phy_reg(struct atl1_hw *hw, u16 reg_addr, u16 *phy_data);
static u32 atlx_hash_mc_addr(struct atl1_hw *hw, u8 *mc_addr);
static void atlx_set_mac_addr(struct atl1_hw *hw);

static struct atlx_spi_flash_dev flash_table[] = {
/*	MFR_NAME  WRSR  READ  PRGM  WREN  WRDI  RDSR  RDID  SEC_ERS CHIP_ERS */
	{"Atmel", 0x00, 0x03, 0x02, 0x06, 0x04, 0x05, 0x15, 0x52,   0x62},
	{"SST",   0x01, 0x03, 0x02, 0x06, 0x04, 0x05, 0x90, 0x20,   0x60},
	{"ST",    0x01, 0x03, 0x02, 0x06, 0x04, 0x05, 0xAB, 0xD8,   0xC7},
};

static int atlx_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return atlx_mii_ioctl(netdev, ifr, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * atlx_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 */
static int atlx_set_mac(struct net_device *netdev, void *p)
{
	struct atlx_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (netif_running(netdev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(adapter->hw.mac_addr, addr->sa_data, netdev->addr_len);

	atlx_set_mac_addr(&adapter->hw);
	return 0;
}

static void atlx_check_for_link(struct atlx_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u16 phy_data = 0;

	spin_lock(&adapter->lock);
	adapter->phy_timer_pending = false;
	atlx_read_phy_reg(&adapter->hw, MII_BMSR, &phy_data);
	atlx_read_phy_reg(&adapter->hw, MII_BMSR, &phy_data);
	spin_unlock(&adapter->lock);

	/* notify upper layer link down ASAP */
	if (!(phy_data & BMSR_LSTATUS)) {
		/* Link Down */
		if (netif_carrier_ok(netdev)) {
			/* old link state: Up */
			dev_info(&adapter->pdev->dev, "%s link is down\n",
				netdev->name);
			adapter->link_speed = SPEED_0;
			netif_carrier_off(netdev);
		}
	}
	schedule_work(&adapter->link_chg_task);
}

/**
 * atlx_set_multi - Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_multi entry point is called whenever the multicast address
 * list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper multicast,
 * promiscuous mode, and all-multi behavior.
 */
static void atlx_set_multi(struct net_device *netdev)
{
	struct atlx_adapter *adapter = netdev_priv(netdev);
	struct atlx_hw *hw = &adapter->hw;
	struct netdev_hw_addr *ha;
	u32 rctl;
	u32 hash_value;

	/* Check for Promiscuous and All Multicast modes */
	rctl = ioread32(hw->hw_addr + REG_MAC_CTRL);
	if (netdev->flags & IFF_PROMISC)
		rctl |= MAC_CTRL_PROMIS_EN;
	else if (netdev->flags & IFF_ALLMULTI) {
		rctl |= MAC_CTRL_MC_ALL_EN;
		rctl &= ~MAC_CTRL_PROMIS_EN;
	} else
		rctl &= ~(MAC_CTRL_PROMIS_EN | MAC_CTRL_MC_ALL_EN);

	iowrite32(rctl, hw->hw_addr + REG_MAC_CTRL);

	/* clear the old settings from the multicast hash table */
	iowrite32(0, hw->hw_addr + REG_RX_HASH_TABLE);
	iowrite32(0, (hw->hw_addr + REG_RX_HASH_TABLE) + (1 << 2));

	/* compute mc addresses' hash value ,and put it into hash table */
	netdev_for_each_mc_addr(ha, netdev) {
		hash_value = atlx_hash_mc_addr(hw, ha->addr);
		atlx_hash_set(hw, hash_value);
	}
}

static inline void atlx_imr_set(struct atlx_adapter *adapter,
				unsigned int imr)
{
	iowrite32(imr, adapter->hw.hw_addr + REG_IMR);
	ioread32(adapter->hw.hw_addr + REG_IMR);
}

/**
 * atlx_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 */
static void atlx_irq_enable(struct atlx_adapter *adapter)
{
	atlx_imr_set(adapter, IMR_NORMAL_MASK);
	adapter->int_enabled = true;
}

/**
 * atlx_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 */
static void atlx_irq_disable(struct atlx_adapter *adapter)
{
	adapter->int_enabled = false;
	atlx_imr_set(adapter, 0);
	synchronize_irq(adapter->pdev->irq);
}

static void atlx_clear_phy_int(struct atlx_adapter *adapter)
{
	u16 phy_data;
	unsigned long flags;

	spin_lock_irqsave(&adapter->lock, flags);
	atlx_read_phy_reg(&adapter->hw, 19, &phy_data);
	spin_unlock_irqrestore(&adapter->lock, flags);
}

/**
 * atlx_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 */
static void atlx_tx_timeout(struct net_device *netdev)
{
	struct atlx_adapter *adapter = netdev_priv(netdev);
	/* Do the reset outside of interrupt context */
	schedule_work(&adapter->reset_dev_task);
}

/*
 * atlx_link_chg_task - deal with link change event Out of interrupt context
 */
static void atlx_link_chg_task(struct work_struct *work)
{
	struct atlx_adapter *adapter;
	unsigned long flags;

	adapter = container_of(work, struct atlx_adapter, link_chg_task);

	spin_lock_irqsave(&adapter->lock, flags);
	atlx_check_link(adapter);
	spin_unlock_irqrestore(&adapter->lock, flags);
}

static void __atlx_vlan_mode(netdev_features_t features, u32 *ctrl)
{
	if (features & NETIF_F_HW_VLAN_CTAG_RX) {
		/* enable VLAN tag insert/strip */
		*ctrl |= MAC_CTRL_RMV_VLAN;
	} else {
		/* disable VLAN tag insert/strip */
		*ctrl &= ~MAC_CTRL_RMV_VLAN;
	}
}

static void atlx_vlan_mode(struct net_device *netdev,
	netdev_features_t features)
{
	struct atlx_adapter *adapter = netdev_priv(netdev);
	unsigned long flags;
	u32 ctrl;

	spin_lock_irqsave(&adapter->lock, flags);
	/* atlx_irq_disable(adapter); FIXME: confirm/remove */
	ctrl = ioread32(adapter->hw.hw_addr + REG_MAC_CTRL);
	__atlx_vlan_mode(features, &ctrl);
	iowrite32(ctrl, adapter->hw.hw_addr + REG_MAC_CTRL);
	/* atlx_irq_enable(adapter); FIXME */
	spin_unlock_irqrestore(&adapter->lock, flags);
}

static void atlx_restore_vlan(struct atlx_adapter *adapter)
{
	atlx_vlan_mode(adapter->netdev, adapter->netdev->features);
}

static netdev_features_t atlx_fix_features(struct net_device *netdev,
	netdev_features_t features)
{
	/*
	 * Since there is no support for separate rx/tx vlan accel
	 * enable/disable make sure tx flag is always in same state as rx.
	 */
	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		features |= NETIF_F_HW_VLAN_CTAG_TX;
	else
		features &= ~NETIF_F_HW_VLAN_CTAG_TX;

	return features;
}

static int atlx_set_features(struct net_device *netdev,
	netdev_features_t features)
{
	netdev_features_t changed = netdev->features ^ features;

	if (changed & NETIF_F_HW_VLAN_CTAG_RX)
		atlx_vlan_mode(netdev, features);

	return 0;
}

#endif /* ATLX_C */
