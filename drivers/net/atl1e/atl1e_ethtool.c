/*
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
 *
 */

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/slab.h>

#include "atl1e.h"

static int atl1e_get_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct atl1e_hw *hw = &adapter->hw;

	ecmd->supported = (SUPPORTED_10baseT_Half  |
			   SUPPORTED_10baseT_Full  |
			   SUPPORTED_100baseT_Half |
			   SUPPORTED_100baseT_Full |
			   SUPPORTED_Autoneg       |
			   SUPPORTED_TP);
	if (hw->nic_type == athr_l1e)
		ecmd->supported |= SUPPORTED_1000baseT_Full;

	ecmd->advertising = ADVERTISED_TP;

	ecmd->advertising |= ADVERTISED_Autoneg;
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

static int atl1e_set_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct atl1e_hw *hw = &adapter->hw;

	while (test_and_set_bit(__AT_RESETTING, &adapter->flags))
		msleep(1);

	if (ecmd->autoneg == AUTONEG_ENABLE) {
		u16 adv4, adv9;

		if ((ecmd->advertising&ADVERTISE_1000_FULL)) {
			if (hw->nic_type == athr_l1e) {
				hw->autoneg_advertised =
					ecmd->advertising & AT_ADV_MASK;
			} else {
				clear_bit(__AT_RESETTING, &adapter->flags);
				return -EINVAL;
			}
		} else if (ecmd->advertising&ADVERTISE_1000_HALF) {
			clear_bit(__AT_RESETTING, &adapter->flags);
			return -EINVAL;
		} else {
			hw->autoneg_advertised =
				ecmd->advertising & AT_ADV_MASK;
		}
		ecmd->advertising = hw->autoneg_advertised |
				    ADVERTISED_TP | ADVERTISED_Autoneg;

		adv4 = hw->mii_autoneg_adv_reg & ~MII_AR_SPEED_MASK;
		adv9 = hw->mii_1000t_ctrl_reg & ~MII_AT001_CR_1000T_SPEED_MASK;
		if (hw->autoneg_advertised & ADVERTISE_10_HALF)
			adv4 |= MII_AR_10T_HD_CAPS;
		if (hw->autoneg_advertised & ADVERTISE_10_FULL)
			adv4 |= MII_AR_10T_FD_CAPS;
		if (hw->autoneg_advertised & ADVERTISE_100_HALF)
			adv4 |= MII_AR_100TX_HD_CAPS;
		if (hw->autoneg_advertised & ADVERTISE_100_FULL)
			adv4 |= MII_AR_100TX_FD_CAPS;
		if (hw->autoneg_advertised & ADVERTISE_1000_FULL)
			adv9 |= MII_AT001_CR_1000T_FD_CAPS;

		if (adv4 != hw->mii_autoneg_adv_reg ||
				adv9 != hw->mii_1000t_ctrl_reg) {
			hw->mii_autoneg_adv_reg = adv4;
			hw->mii_1000t_ctrl_reg = adv9;
			hw->re_autoneg = true;
		}

	} else {
		clear_bit(__AT_RESETTING, &adapter->flags);
		return -EINVAL;
	}

	/* reset the link */

	if (netif_running(adapter->netdev)) {
		atl1e_down(adapter);
		atl1e_up(adapter);
	} else
		atl1e_reset_hw(&adapter->hw);

	clear_bit(__AT_RESETTING, &adapter->flags);
	return 0;
}

static u32 atl1e_get_msglevel(struct net_device *netdev)
{
#ifdef DBG
	return 1;
#else
	return 0;
#endif
}

static int atl1e_get_regs_len(struct net_device *netdev)
{
	return AT_REGS_LEN * sizeof(u32);
}

static void atl1e_get_regs(struct net_device *netdev,
			   struct ethtool_regs *regs, void *p)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct atl1e_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u16 phy_data;

	memset(p, 0, AT_REGS_LEN * sizeof(u32));

	regs->version = (1 << 24) | (hw->revision_id << 16) | hw->device_id;

	regs_buff[0]  = AT_READ_REG(hw, REG_VPD_CAP);
	regs_buff[1]  = AT_READ_REG(hw, REG_SPI_FLASH_CTRL);
	regs_buff[2]  = AT_READ_REG(hw, REG_SPI_FLASH_CONFIG);
	regs_buff[3]  = AT_READ_REG(hw, REG_TWSI_CTRL);
	regs_buff[4]  = AT_READ_REG(hw, REG_PCIE_DEV_MISC_CTRL);
	regs_buff[5]  = AT_READ_REG(hw, REG_MASTER_CTRL);
	regs_buff[6]  = AT_READ_REG(hw, REG_MANUAL_TIMER_INIT);
	regs_buff[7]  = AT_READ_REG(hw, REG_IRQ_MODU_TIMER_INIT);
	regs_buff[8]  = AT_READ_REG(hw, REG_GPHY_CTRL);
	regs_buff[9]  = AT_READ_REG(hw, REG_CMBDISDMA_TIMER);
	regs_buff[10] = AT_READ_REG(hw, REG_IDLE_STATUS);
	regs_buff[11] = AT_READ_REG(hw, REG_MDIO_CTRL);
	regs_buff[12] = AT_READ_REG(hw, REG_SERDES_LOCK);
	regs_buff[13] = AT_READ_REG(hw, REG_MAC_CTRL);
	regs_buff[14] = AT_READ_REG(hw, REG_MAC_IPG_IFG);
	regs_buff[15] = AT_READ_REG(hw, REG_MAC_STA_ADDR);
	regs_buff[16] = AT_READ_REG(hw, REG_MAC_STA_ADDR+4);
	regs_buff[17] = AT_READ_REG(hw, REG_RX_HASH_TABLE);
	regs_buff[18] = AT_READ_REG(hw, REG_RX_HASH_TABLE+4);
	regs_buff[19] = AT_READ_REG(hw, REG_MAC_HALF_DUPLX_CTRL);
	regs_buff[20] = AT_READ_REG(hw, REG_MTU);
	regs_buff[21] = AT_READ_REG(hw, REG_WOL_CTRL);
	regs_buff[22] = AT_READ_REG(hw, REG_SRAM_TRD_ADDR);
	regs_buff[23] = AT_READ_REG(hw, REG_SRAM_TRD_LEN);
	regs_buff[24] = AT_READ_REG(hw, REG_SRAM_RXF_ADDR);
	regs_buff[25] = AT_READ_REG(hw, REG_SRAM_RXF_LEN);
	regs_buff[26] = AT_READ_REG(hw, REG_SRAM_TXF_ADDR);
	regs_buff[27] = AT_READ_REG(hw, REG_SRAM_TXF_LEN);
	regs_buff[28] = AT_READ_REG(hw, REG_SRAM_TCPH_ADDR);
	regs_buff[29] = AT_READ_REG(hw, REG_SRAM_PKTH_ADDR);

	atl1e_read_phy_reg(hw, MII_BMCR, &phy_data);
	regs_buff[73] = (u32)phy_data;
	atl1e_read_phy_reg(hw, MII_BMSR, &phy_data);
	regs_buff[74] = (u32)phy_data;
}

static int atl1e_get_eeprom_len(struct net_device *netdev)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);

	if (!atl1e_check_eeprom_exist(&adapter->hw))
		return AT_EEPROM_LEN;
	else
		return 0;
}

static int atl1e_get_eeprom(struct net_device *netdev,
		struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct atl1e_hw *hw = &adapter->hw;
	u32 *eeprom_buff;
	int first_dword, last_dword;
	int ret_val = 0;
	int i;

	if (eeprom->len == 0)
		return -EINVAL;

	if (atl1e_check_eeprom_exist(hw)) /* not exist */
		return -EINVAL;

	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	first_dword = eeprom->offset >> 2;
	last_dword = (eeprom->offset + eeprom->len - 1) >> 2;

	eeprom_buff = kmalloc(sizeof(u32) *
			(last_dword - first_dword + 1), GFP_KERNEL);
	if (eeprom_buff == NULL)
		return -ENOMEM;

	for (i = first_dword; i < last_dword; i++) {
		if (!atl1e_read_eeprom(hw, i * 4, &(eeprom_buff[i-first_dword]))) {
			kfree(eeprom_buff);
			return -EIO;
		}
	}

	memcpy(bytes, (u8 *)eeprom_buff + (eeprom->offset & 3),
			eeprom->len);
	kfree(eeprom_buff);

	return ret_val;
}

static int atl1e_set_eeprom(struct net_device *netdev,
			    struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct atl1e_hw *hw = &adapter->hw;
	u32 *eeprom_buff;
	u32 *ptr;
	int first_dword, last_dword;
	int ret_val = 0;
	int i;

	if (eeprom->len == 0)
		return -EOPNOTSUPP;

	if (eeprom->magic != (hw->vendor_id | (hw->device_id << 16)))
		return -EINVAL;

	first_dword = eeprom->offset >> 2;
	last_dword = (eeprom->offset + eeprom->len - 1) >> 2;
	eeprom_buff = kmalloc(AT_EEPROM_LEN, GFP_KERNEL);
	if (eeprom_buff == NULL)
		return -ENOMEM;

	ptr = (u32 *)eeprom_buff;

	if (eeprom->offset & 3) {
		/* need read/modify/write of first changed EEPROM word */
		/* only the second byte of the word is being modified */
		if (!atl1e_read_eeprom(hw, first_dword * 4, &(eeprom_buff[0]))) {
			ret_val = -EIO;
			goto out;
		}
		ptr++;
	}
	if (((eeprom->offset + eeprom->len) & 3)) {
		/* need read/modify/write of last changed EEPROM word */
		/* only the first byte of the word is being modified */

		if (!atl1e_read_eeprom(hw, last_dword * 4,
				&(eeprom_buff[last_dword - first_dword]))) {
			ret_val = -EIO;
			goto out;
		}
	}

	/* Device's eeprom is always little-endian, word addressable */
	memcpy(ptr, bytes, eeprom->len);

	for (i = 0; i < last_dword - first_dword + 1; i++) {
		if (!atl1e_write_eeprom(hw, ((first_dword + i) * 4),
				  eeprom_buff[i])) {
			ret_val = -EIO;
			goto out;
		}
	}
out:
	kfree(eeprom_buff);
	return ret_val;
}

static void atl1e_get_drvinfo(struct net_device *netdev,
		struct ethtool_drvinfo *drvinfo)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);

	strncpy(drvinfo->driver,  atl1e_driver_name, 32);
	strncpy(drvinfo->version, atl1e_driver_version, 32);
	strncpy(drvinfo->fw_version, "L1e", 32);
	strncpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
	drvinfo->n_stats = 0;
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = atl1e_get_regs_len(netdev);
	drvinfo->eedump_len = atl1e_get_eeprom_len(netdev);
}

static void atl1e_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);

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

static int atl1e_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & (WAKE_ARP | WAKE_MAGICSECURE |
			    WAKE_UCAST | WAKE_MCAST | WAKE_BCAST))
		return -EOPNOTSUPP;
	/* these settings will always override what we currently have */
	adapter->wol = 0;

	if (wol->wolopts & WAKE_MAGIC)
		adapter->wol |= AT_WUFC_MAG;
	if (wol->wolopts & WAKE_PHY)
		adapter->wol |= AT_WUFC_LNKC;

	device_set_wakeup_enable(&adapter->pdev->dev, adapter->wol);

	return 0;
}

static int atl1e_nway_reset(struct net_device *netdev)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	if (netif_running(netdev))
		atl1e_reinit_locked(adapter);
	return 0;
}

static const struct ethtool_ops atl1e_ethtool_ops = {
	.get_settings           = atl1e_get_settings,
	.set_settings           = atl1e_set_settings,
	.get_drvinfo            = atl1e_get_drvinfo,
	.get_regs_len           = atl1e_get_regs_len,
	.get_regs               = atl1e_get_regs,
	.get_wol                = atl1e_get_wol,
	.set_wol                = atl1e_set_wol,
	.get_msglevel           = atl1e_get_msglevel,
	.nway_reset             = atl1e_nway_reset,
	.get_link               = ethtool_op_get_link,
	.get_eeprom_len         = atl1e_get_eeprom_len,
	.get_eeprom             = atl1e_get_eeprom,
	.set_eeprom             = atl1e_set_eeprom,
	.set_tx_csum            = ethtool_op_set_tx_hw_csum,
	.set_sg                 = ethtool_op_set_sg,
	.set_tso                = ethtool_op_set_tso,
};

void atl1e_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &atl1e_ethtool_ops);
}
