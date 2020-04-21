// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright(c) 2007 Atheros Corporation. All rights reserved.
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 */

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/slab.h>

#include "atl1e.h"

static int atl1e_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *cmd)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct atl1e_hw *hw = &adapter->hw;
	u32 supported, advertising;

	supported = (SUPPORTED_10baseT_Half  |
			   SUPPORTED_10baseT_Full  |
			   SUPPORTED_100baseT_Half |
			   SUPPORTED_100baseT_Full |
			   SUPPORTED_Autoneg       |
			   SUPPORTED_TP);
	if (hw->nic_type == athr_l1e)
		supported |= SUPPORTED_1000baseT_Full;

	advertising = ADVERTISED_TP;

	advertising |= ADVERTISED_Autoneg;
	advertising |= hw->autoneg_advertised;

	cmd->base.port = PORT_TP;
	cmd->base.phy_address = 0;

	if (adapter->link_speed != SPEED_0) {
		cmd->base.speed = adapter->link_speed;
		if (adapter->link_duplex == FULL_DUPLEX)
			cmd->base.duplex = DUPLEX_FULL;
		else
			cmd->base.duplex = DUPLEX_HALF;
	} else {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}

	cmd->base.autoneg = AUTONEG_ENABLE;

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	return 0;
}

static int atl1e_set_link_ksettings(struct net_device *netdev,
				    const struct ethtool_link_ksettings *cmd)
{
	struct atl1e_adapter *adapter = netdev_priv(netdev);
	struct atl1e_hw *hw = &adapter->hw;
	u32 advertising;

	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);

	while (test_and_set_bit(__AT_RESETTING, &adapter->flags))
		msleep(1);

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		u16 adv4, adv9;

		if (advertising & ADVERTISE_1000_FULL) {
			if (hw->nic_type == athr_l1e) {
				hw->autoneg_advertised =
					advertising & AT_ADV_MASK;
			} else {
				clear_bit(__AT_RESETTING, &adapter->flags);
				return -EINVAL;
			}
		} else if (advertising & ADVERTISE_1000_HALF) {
			clear_bit(__AT_RESETTING, &adapter->flags);
			return -EINVAL;
		} else {
			hw->autoneg_advertised =
				advertising & AT_ADV_MASK;
		}
		advertising = hw->autoneg_advertised |
				    ADVERTISED_TP | ADVERTISED_Autoneg;

		adv4 = hw->mii_autoneg_adv_reg & ~ADVERTISE_ALL;
		adv9 = hw->mii_1000t_ctrl_reg & ~MII_AT001_CR_1000T_SPEED_MASK;
		if (hw->autoneg_advertised & ADVERTISE_10_HALF)
			adv4 |= ADVERTISE_10HALF;
		if (hw->autoneg_advertised & ADVERTISE_10_FULL)
			adv4 |= ADVERTISE_10FULL;
		if (hw->autoneg_advertised & ADVERTISE_100_HALF)
			adv4 |= ADVERTISE_100HALF;
		if (hw->autoneg_advertised & ADVERTISE_100_FULL)
			adv4 |= ADVERTISE_100FULL;
		if (hw->autoneg_advertised & ADVERTISE_1000_FULL)
			adv9 |= ADVERTISE_1000FULL;

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

	eeprom_buff = kmalloc_array(last_dword - first_dword + 1, sizeof(u32),
				    GFP_KERNEL);
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

	ptr = eeprom_buff;

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

	strlcpy(drvinfo->driver,  atl1e_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->fw_version, "L1e", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
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
	.get_link_ksettings     = atl1e_get_link_ksettings,
	.set_link_ksettings     = atl1e_set_link_ksettings,
};

void atl1e_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &atl1e_ethtool_ops;
}
