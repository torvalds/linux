/*
 * Copyright(c) 2009 - 2009 Atheros Corporation. All rights reserved.
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
 *
 */

#include <linux/netdevice.h>
#include <linux/ethtool.h>

#include "atl1c.h"

static int atl1c_get_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct atl1c_hw *hw = &adapter->hw;

	ecmd->supported = (SUPPORTED_10baseT_Half  |
			   SUPPORTED_10baseT_Full  |
			   SUPPORTED_100baseT_Half |
			   SUPPORTED_100baseT_Full |
			   SUPPORTED_Autoneg       |
			   SUPPORTED_TP);
	if (hw->ctrl_flags & ATL1C_LINK_CAP_1000M)
		ecmd->supported |= SUPPORTED_1000baseT_Full;

	ecmd->advertising = ADVERTISED_TP;

	ecmd->advertising |= hw->autoneg_advertised;

	ecmd->port = PORT_TP;
	ecmd->phy_address = 0;
	ecmd->transceiver = XCVR_INTERNAL;

	if (adapter->link_speed != SPEED_0) {
		ecmd->speed = adapter->link_speed;
		if (adapter->link_duplex == FULL_DUPLEX)
			ecmd->duplex = DUPLEX_FULL;
		else
			ecmd->duplex = DUPLEX_HALF;
	} else {
		ecmd->speed = -1;
		ecmd->duplex = -1;
	}

	ecmd->autoneg = AUTONEG_ENABLE;
	return 0;
}

static int atl1c_set_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct atl1c_hw *hw = &adapter->hw;
	u16  autoneg_advertised;

	while (test_and_set_bit(__AT_RESETTING, &adapter->flags))
		msleep(1);

	if (ecmd->autoneg == AUTONEG_ENABLE) {
		autoneg_advertised = ADVERTISED_Autoneg;
	} else {
		if (ecmd->speed == SPEED_1000) {
			if (ecmd->duplex != DUPLEX_FULL) {
				if (netif_msg_link(adapter))
					dev_warn(&adapter->pdev->dev,
						"1000M half is invalid\n");
				clear_bit(__AT_RESETTING, &adapter->flags);
				return -EINVAL;
			}
			autoneg_advertised = ADVERTISED_1000baseT_Full;
		} else if (ecmd->speed == SPEED_100) {
			if (ecmd->duplex == DUPLEX_FULL)
				autoneg_advertised = ADVERTISED_100baseT_Full;
			else
				autoneg_advertised = ADVERTISED_100baseT_Half;
		} else {
			if (ecmd->duplex == DUPLEX_FULL)
				autoneg_advertised = ADVERTISED_10baseT_Full;
			else
				autoneg_advertised = ADVERTISED_10baseT_Half;
		}
	}

	if (hw->autoneg_advertised != autoneg_advertised) {
		hw->autoneg_advertised = autoneg_advertised;
		if (atl1c_restart_autoneg(hw) != 0) {
			if (netif_msg_link(adapter))
				dev_warn(&adapter->pdev->dev,
					"ethtool speed/duplex setting failed\n");
			clear_bit(__AT_RESETTING, &adapter->flags);
			return -EINVAL;
		}
	}
	clear_bit(__AT_RESETTING, &adapter->flags);
	return 0;
}

static u32 atl1c_get_tx_csum(struct net_device *netdev)
{
	return (netdev->features & NETIF_F_HW_CSUM) != 0;
}

static u32 atl1c_get_msglevel(struct net_device *netdev)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	return adapter->msg_enable;
}

static void atl1c_set_msglevel(struct net_device *netdev, u32 data)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	adapter->msg_enable = data;
}

static int atl1c_get_regs_len(struct net_device *netdev)
{
	return AT_REGS_LEN;
}

static void atl1c_get_regs(struct net_device *netdev,
			   struct ethtool_regs *regs, void *p)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct atl1c_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u16 phy_data;

	memset(p, 0, AT_REGS_LEN);

	regs->version = 0;
	AT_READ_REG(hw, REG_VPD_CAP, 		  p++);
	AT_READ_REG(hw, REG_PM_CTRL, 		  p++);
	AT_READ_REG(hw, REG_MAC_HALF_DUPLX_CTRL,  p++);
	AT_READ_REG(hw, REG_TWSI_CTRL, 		  p++);
	AT_READ_REG(hw, REG_PCIE_DEV_MISC_CTRL,   p++);
	AT_READ_REG(hw, REG_MASTER_CTRL, 	  p++);
	AT_READ_REG(hw, REG_MANUAL_TIMER_INIT,    p++);
	AT_READ_REG(hw, REG_IRQ_MODRT_TIMER_INIT, p++);
	AT_READ_REG(hw, REG_GPHY_CTRL, 		  p++);
	AT_READ_REG(hw, REG_LINK_CTRL, 		  p++);
	AT_READ_REG(hw, REG_IDLE_STATUS, 	  p++);
	AT_READ_REG(hw, REG_MDIO_CTRL, 		  p++);
	AT_READ_REG(hw, REG_SERDES_LOCK, 	  p++);
	AT_READ_REG(hw, REG_MAC_CTRL, 		  p++);
	AT_READ_REG(hw, REG_MAC_IPG_IFG, 	  p++);
	AT_READ_REG(hw, REG_MAC_STA_ADDR, 	  p++);
	AT_READ_REG(hw, REG_MAC_STA_ADDR+4, 	  p++);
	AT_READ_REG(hw, REG_RX_HASH_TABLE, 	  p++);
	AT_READ_REG(hw, REG_RX_HASH_TABLE+4, 	  p++);
	AT_READ_REG(hw, REG_RXQ_CTRL, 		  p++);
	AT_READ_REG(hw, REG_TXQ_CTRL, 		  p++);
	AT_READ_REG(hw, REG_MTU, 		  p++);
	AT_READ_REG(hw, REG_WOL_CTRL, 		  p++);

	atl1c_read_phy_reg(hw, MII_BMCR, &phy_data);
	regs_buff[73] =	(u32) phy_data;
	atl1c_read_phy_reg(hw, MII_BMSR, &phy_data);
	regs_buff[74] = (u32) phy_data;
}

static int atl1c_get_eeprom_len(struct net_device *netdev)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	if (atl1c_check_eeprom_exist(&adapter->hw))
		return AT_EEPROM_LEN;
	else
		return 0;
}

static int atl1c_get_eeprom(struct net_device *netdev,
		struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	struct atl1c_hw *hw = &adapter->hw;
	u32 *eeprom_buff;
	int first_dword, last_dword;
	int ret_val = 0;
	int i;

	if (eeprom->len == 0)
		return -EINVAL;

	if (!atl1c_check_eeprom_exist(hw)) /* not exist */
		return -EINVAL;

	eeprom->magic = adapter->pdev->vendor |
			(adapter->pdev->device << 16);

	first_dword = eeprom->offset >> 2;
	last_dword = (eeprom->offset + eeprom->len - 1) >> 2;

	eeprom_buff = kmalloc(sizeof(u32) *
			(last_dword - first_dword + 1), GFP_KERNEL);
	if (eeprom_buff == NULL)
		return -ENOMEM;

	for (i = first_dword; i < last_dword; i++) {
		if (!atl1c_read_eeprom(hw, i * 4, &(eeprom_buff[i-first_dword]))) {
			kfree(eeprom_buff);
			return -EIO;
		}
	}

	memcpy(bytes, (u8 *)eeprom_buff + (eeprom->offset & 3),
			eeprom->len);
	kfree(eeprom_buff);

	return ret_val;
	return 0;
}

static void atl1c_get_drvinfo(struct net_device *netdev,
		struct ethtool_drvinfo *drvinfo)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	strncpy(drvinfo->driver,  atl1c_driver_name, sizeof(drvinfo->driver));
	strncpy(drvinfo->version, atl1c_driver_version,
		sizeof(drvinfo->version));
	strncpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strncpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->n_stats = 0;
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = atl1c_get_regs_len(netdev);
	drvinfo->eedump_len = atl1c_get_eeprom_len(netdev);
}

static void atl1c_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	wol->supported = WAKE_MAGIC | WAKE_PHY;
	wol->wolopts = 0;

	if (adapter->wol & AT_WUFC_EX)
		wol->wolopts |= WAKE_UCAST;
	if (adapter->wol & AT_WUFC_MC)
		wol->wolopts |= WAKE_MCAST;
	if (adapter->wol & AT_WUFC_BC)
		wol->wolopts |= WAKE_BCAST;
	if (adapter->wol & AT_WUFC_MAG)
		wol->wolopts |= WAKE_MAGIC;
	if (adapter->wol & AT_WUFC_LNKC)
		wol->wolopts |= WAKE_PHY;

	return;
}

static int atl1c_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & (WAKE_ARP | WAKE_MAGICSECURE |
			    WAKE_UCAST | WAKE_BCAST | WAKE_MCAST))
		return -EOPNOTSUPP;
	/* these settings will always override what we currently have */
	adapter->wol = 0;

	if (wol->wolopts & WAKE_MAGIC)
		adapter->wol |= AT_WUFC_MAG;
	if (wol->wolopts & WAKE_PHY)
		adapter->wol |= AT_WUFC_LNKC;

	return 0;
}

static int atl1c_nway_reset(struct net_device *netdev)
{
	struct atl1c_adapter *adapter = netdev_priv(netdev);
	if (netif_running(netdev))
		atl1c_reinit_locked(adapter);
	return 0;
}

static struct ethtool_ops atl1c_ethtool_ops = {
	.get_settings           = atl1c_get_settings,
	.set_settings           = atl1c_set_settings,
	.get_drvinfo            = atl1c_get_drvinfo,
	.get_regs_len           = atl1c_get_regs_len,
	.get_regs               = atl1c_get_regs,
	.get_wol                = atl1c_get_wol,
	.set_wol                = atl1c_set_wol,
	.get_msglevel           = atl1c_get_msglevel,
	.set_msglevel           = atl1c_set_msglevel,
	.nway_reset             = atl1c_nway_reset,
	.get_link               = ethtool_op_get_link,
	.get_eeprom_len         = atl1c_get_eeprom_len,
	.get_eeprom             = atl1c_get_eeprom,
	.get_tx_csum            = atl1c_get_tx_csum,
	.get_sg                 = ethtool_op_get_sg,
	.set_sg                 = ethtool_op_set_sg,
};

void atl1c_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &atl1c_ethtool_ops);
}
