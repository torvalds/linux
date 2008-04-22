/* atlx.c -- common functions for Attansic network drivers
 *
 * Copyright(c) 2005 - 2006 Attansic Corporation. All rights reserved.
 * Copyright(c) 2006 - 2007 Chris Snook <csnook@redhat.com>
 * Copyright(c) 2006 Jay Cliburn <jcliburn@gmail.com>
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

/*
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
			netif_stop_queue(netdev);
		}
	}
	schedule_work(&adapter->link_chg_task);
}

/*
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
	struct dev_mc_list *mc_ptr;
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
	for (mc_ptr = netdev->mc_list; mc_ptr; mc_ptr = mc_ptr->next) {
		hash_value = atlx_hash_mc_addr(hw, mc_ptr->dmi_addr);
		atlx_hash_set(hw, hash_value);
	}
}

/*
 * atlx_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 */
static void atlx_irq_enable(struct atlx_adapter *adapter)
{
	iowrite32(IMR_NORMAL_MASK, adapter->hw.hw_addr + REG_IMR);
	ioread32(adapter->hw.hw_addr + REG_IMR);
}

/*
 * atlx_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 */
static void atlx_irq_disable(struct atlx_adapter *adapter)
{
	iowrite32(0, adapter->hw.hw_addr + REG_IMR);
	ioread32(adapter->hw.hw_addr + REG_IMR);
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

/*
 * atlx_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 */
static struct net_device_stats *atlx_get_stats(struct net_device *netdev)
{
	struct atlx_adapter *adapter = netdev_priv(netdev);
	return &adapter->net_stats;
}

/*
 * atlx_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 */
static void atlx_tx_timeout(struct net_device *netdev)
{
	struct atlx_adapter *adapter = netdev_priv(netdev);
	/* Do the reset outside of interrupt context */
	schedule_work(&adapter->tx_timeout_task);
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

static void atlx_vlan_rx_register(struct net_device *netdev,
	struct vlan_group *grp)
{
	struct atlx_adapter *adapter = netdev_priv(netdev);
	unsigned long flags;
	u32 ctrl;

	spin_lock_irqsave(&adapter->lock, flags);
	/* atlx_irq_disable(adapter); FIXME: confirm/remove */
	adapter->vlgrp = grp;

	if (grp) {
		/* enable VLAN tag insert/strip */
		ctrl = ioread32(adapter->hw.hw_addr + REG_MAC_CTRL);
		ctrl |= MAC_CTRL_RMV_VLAN;
		iowrite32(ctrl, adapter->hw.hw_addr + REG_MAC_CTRL);
	} else {
		/* disable VLAN tag insert/strip */
		ctrl = ioread32(adapter->hw.hw_addr + REG_MAC_CTRL);
		ctrl &= ~MAC_CTRL_RMV_VLAN;
		iowrite32(ctrl, adapter->hw.hw_addr + REG_MAC_CTRL);
	}

	/* atlx_irq_enable(adapter); FIXME */
	spin_unlock_irqrestore(&adapter->lock, flags);
}

static void atlx_restore_vlan(struct atlx_adapter *adapter)
{
	atlx_vlan_rx_register(adapter->netdev, adapter->vlgrp);
}

/*
 * This is the only thing that needs to be changed to adjust the
 * maximum number of ports that the driver can manage.
 */
#define ATL1_MAX_NIC 4

#define OPTION_UNSET    -1
#define OPTION_DISABLED 0
#define OPTION_ENABLED  1

#define ATL1_PARAM_INIT { [0 ... ATL1_MAX_NIC] = OPTION_UNSET }

/*
 * Interrupt Moderate Timer in units of 2 us
 *
 * Valid Range: 10-65535
 *
 * Default Value: 100 (200us)
 */
static int __devinitdata int_mod_timer[ATL1_MAX_NIC+1] = ATL1_PARAM_INIT;
static int num_int_mod_timer;
module_param_array_named(int_mod_timer, int_mod_timer, int,
	&num_int_mod_timer, 0);
MODULE_PARM_DESC(int_mod_timer, "Interrupt moderator timer");

/*
 * flash_vendor
 *
 * Valid Range: 0-2
 *
 * 0 - Atmel
 * 1 - SST
 * 2 - ST
 *
 * Default Value: 0
 */
static int __devinitdata flash_vendor[ATL1_MAX_NIC+1] = ATL1_PARAM_INIT;
static int num_flash_vendor;
module_param_array_named(flash_vendor, flash_vendor, int, &num_flash_vendor, 0);
MODULE_PARM_DESC(flash_vendor, "SPI flash vendor");

#define DEFAULT_INT_MOD_CNT	100	/* 200us */
#define MAX_INT_MOD_CNT		65000
#define MIN_INT_MOD_CNT		50

#define FLASH_VENDOR_DEFAULT	0
#define FLASH_VENDOR_MIN	0
#define FLASH_VENDOR_MAX	2

struct atl1_option {
	enum { enable_option, range_option, list_option } type;
	char *name;
	char *err;
	int def;
	union {
		struct {	/* range_option info */
			int min;
			int max;
		} r;
		struct {	/* list_option info */
			int nr;
			struct atl1_opt_list {
				int i;
				char *str;
			} *p;
		} l;
	} arg;
};

static int __devinit atl1_validate_option(int *value, struct atl1_option *opt,
	struct pci_dev *pdev)
{
	if (*value == OPTION_UNSET) {
		*value = opt->def;
		return 0;
	}

	switch (opt->type) {
	case enable_option:
		switch (*value) {
		case OPTION_ENABLED:
			dev_info(&pdev->dev, "%s enabled\n", opt->name);
			return 0;
		case OPTION_DISABLED:
			dev_info(&pdev->dev, "%s disabled\n", opt->name);
			return 0;
		}
		break;
	case range_option:
		if (*value >= opt->arg.r.min && *value <= opt->arg.r.max) {
			dev_info(&pdev->dev, "%s set to %i\n", opt->name,
				*value);
			return 0;
		}
		break;
	case list_option:{
			int i;
			struct atl1_opt_list *ent;

			for (i = 0; i < opt->arg.l.nr; i++) {
				ent = &opt->arg.l.p[i];
				if (*value == ent->i) {
					if (ent->str[0] != '\0')
						dev_info(&pdev->dev, "%s\n",
							ent->str);
					return 0;
				}
			}
		}
		break;

	default:
		break;
	}

	dev_info(&pdev->dev, "invalid %s specified (%i) %s\n",
		opt->name, *value, opt->err);
	*value = opt->def;
	return -1;
}

/*
 * atl1_check_options - Range Checking for Command Line Parameters
 * @adapter: board private structure
 *
 * This routine checks all command line parameters for valid user
 * input.  If an invalid value is given, or if no user specified
 * value exists, a default value is used.  The final value is stored
 * in a variable in the adapter structure.
 */
void __devinit atl1_check_options(struct atl1_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	int bd = adapter->bd_number;
	if (bd >= ATL1_MAX_NIC) {
		dev_notice(&pdev->dev, "no configuration for board#%i\n", bd);
		dev_notice(&pdev->dev, "using defaults for all values\n");
	}
	{			/* Interrupt Moderate Timer */
		struct atl1_option opt = {
			.type = range_option,
			.name = "Interrupt Moderator Timer",
			.err = "using default of "
				__MODULE_STRING(DEFAULT_INT_MOD_CNT),
			.def = DEFAULT_INT_MOD_CNT,
			.arg = {.r = {.min = MIN_INT_MOD_CNT,
					.max = MAX_INT_MOD_CNT} }
		};
		int val;
		if (num_int_mod_timer > bd) {
			val = int_mod_timer[bd];
			atl1_validate_option(&val, &opt, pdev);
			adapter->imt = (u16) val;
		} else
			adapter->imt = (u16) (opt.def);
	}

	{			/* Flash Vendor */
		struct atl1_option opt = {
			.type = range_option,
			.name = "SPI Flash Vendor",
			.err = "using default of "
				__MODULE_STRING(FLASH_VENDOR_DEFAULT),
			.def = DEFAULT_INT_MOD_CNT,
			.arg = {.r = {.min = FLASH_VENDOR_MIN,
					.max = FLASH_VENDOR_MAX} }
		};
		int val;
		if (num_flash_vendor > bd) {
			val = flash_vendor[bd];
			atl1_validate_option(&val, &opt, pdev);
			adapter->hw.flash_vendor = (u8) val;
		} else
			adapter->hw.flash_vendor = (u8) (opt.def);
	}
}

#endif /* ATLX_C */
