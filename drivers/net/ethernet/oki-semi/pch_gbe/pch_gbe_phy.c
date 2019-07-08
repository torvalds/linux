// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 1999 - 2010 Intel Corporation.
 * Copyright (C) 2010 OKI SEMICONDUCTOR Co., LTD.
 *
 * This code was derived from the Intel e1000e Linux driver.
 */

#include "pch_gbe.h"
#include "pch_gbe_phy.h"

#define PHY_MAX_REG_ADDRESS   0x1F	/* 5 bit address bus (0-0x1F) */

/* PHY 1000 MII Register/Bit Definitions */
/* PHY Registers defined by IEEE */
#define PHY_CONTROL           0x00  /* Control Register */
#define PHY_STATUS            0x01  /* Status Regiser */
#define PHY_ID1               0x02  /* Phy Id Register (word 1) */
#define PHY_ID2               0x03  /* Phy Id Register (word 2) */
#define PHY_AUTONEG_ADV       0x04  /* Autoneg Advertisement */
#define PHY_LP_ABILITY        0x05  /* Link Partner Ability (Base Page) */
#define PHY_AUTONEG_EXP       0x06  /* Autoneg Expansion Register */
#define PHY_NEXT_PAGE_TX      0x07  /* Next Page TX */
#define PHY_LP_NEXT_PAGE      0x08  /* Link Partner Next Page */
#define PHY_1000T_CTRL        0x09  /* 1000Base-T Control Register */
#define PHY_1000T_STATUS      0x0A  /* 1000Base-T Status Register */
#define PHY_EXT_STATUS        0x0F  /* Extended Status Register */
#define PHY_PHYSP_CONTROL     0x10  /* PHY Specific Control Register */
#define PHY_EXT_PHYSP_CONTROL 0x14  /* Extended PHY Specific Control Register */
#define PHY_LED_CONTROL       0x18  /* LED Control Register */
#define PHY_EXT_PHYSP_STATUS  0x1B  /* Extended PHY Specific Status Register */

/* PHY Control Register */
#define MII_CR_SPEED_SELECT_MSB 0x0040	/* bits 6,13: 10=1000, 01=100, 00=10 */
#define MII_CR_COLL_TEST_ENABLE 0x0080	/* Collision test enable */
#define MII_CR_FULL_DUPLEX      0x0100	/* FDX =1, half duplex =0 */
#define MII_CR_RESTART_AUTO_NEG 0x0200	/* Restart auto negotiation */
#define MII_CR_ISOLATE          0x0400	/* Isolate PHY from MII */
#define MII_CR_POWER_DOWN       0x0800	/* Power down */
#define MII_CR_AUTO_NEG_EN      0x1000	/* Auto Neg Enable */
#define MII_CR_SPEED_SELECT_LSB 0x2000	/* bits 6,13: 10=1000, 01=100, 00=10 */
#define MII_CR_LOOPBACK         0x4000	/* 0 = normal, 1 = loopback */
#define MII_CR_RESET            0x8000	/* 0 = normal, 1 = PHY reset */
#define MII_CR_SPEED_1000       0x0040
#define MII_CR_SPEED_100        0x2000
#define MII_CR_SPEED_10         0x0000

/* PHY Status Register */
#define MII_SR_EXTENDED_CAPS     0x0001	/* Extended register capabilities */
#define MII_SR_JABBER_DETECT     0x0002	/* Jabber Detected */
#define MII_SR_LINK_STATUS       0x0004	/* Link Status 1 = link */
#define MII_SR_AUTONEG_CAPS      0x0008	/* Auto Neg Capable */
#define MII_SR_REMOTE_FAULT      0x0010	/* Remote Fault Detect */
#define MII_SR_AUTONEG_COMPLETE  0x0020	/* Auto Neg Complete */
#define MII_SR_PREAMBLE_SUPPRESS 0x0040	/* Preamble may be suppressed */
#define MII_SR_EXTENDED_STATUS   0x0100	/* Ext. status info in Reg 0x0F */
#define MII_SR_100T2_HD_CAPS     0x0200	/* 100T2 Half Duplex Capable */
#define MII_SR_100T2_FD_CAPS     0x0400	/* 100T2 Full Duplex Capable */
#define MII_SR_10T_HD_CAPS       0x0800	/* 10T   Half Duplex Capable */
#define MII_SR_10T_FD_CAPS       0x1000	/* 10T   Full Duplex Capable */
#define MII_SR_100X_HD_CAPS      0x2000	/* 100X  Half Duplex Capable */
#define MII_SR_100X_FD_CAPS      0x4000	/* 100X  Full Duplex Capable */
#define MII_SR_100T4_CAPS        0x8000	/* 100T4 Capable */

/* AR8031 PHY Debug Registers */
#define PHY_AR803X_ID           0x00001374
#define PHY_AR8031_DBG_OFF      0x1D
#define PHY_AR8031_DBG_DAT      0x1E
#define PHY_AR8031_SERDES       0x05
#define PHY_AR8031_HIBERNATE    0x0B
#define PHY_AR8031_SERDES_TX_CLK_DLY   0x0100 /* TX clock delay of 2.0ns */
#define PHY_AR8031_PS_HIB_EN           0x8000 /* Hibernate enable */

/* Phy Id Register (word 2) */
#define PHY_REVISION_MASK        0x000F

/* PHY Specific Control Register */
#define PHYSP_CTRL_ASSERT_CRS_TX  0x0800


/* Default value of PHY register */
#define PHY_CONTROL_DEFAULT         0x1140 /* Control Register */
#define PHY_AUTONEG_ADV_DEFAULT     0x01e0 /* Autoneg Advertisement */
#define PHY_NEXT_PAGE_TX_DEFAULT    0x2001 /* Next Page TX */
#define PHY_1000T_CTRL_DEFAULT      0x0300 /* 1000Base-T Control Register */
#define PHY_PHYSP_CONTROL_DEFAULT   0x01EE /* PHY Specific Control Register */

/**
 * pch_gbe_phy_get_id - Retrieve the PHY ID and revision
 * @hw:	       Pointer to the HW structure
 * Returns
 *	0:			Successful.
 *	Negative value:		Failed.
 */
s32 pch_gbe_phy_get_id(struct pch_gbe_hw *hw)
{
	struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);
	struct pch_gbe_phy_info *phy = &hw->phy;
	s32 ret;
	u16 phy_id1;
	u16 phy_id2;

	ret = pch_gbe_phy_read_reg_miic(hw, PHY_ID1, &phy_id1);
	if (ret)
		return ret;
	ret = pch_gbe_phy_read_reg_miic(hw, PHY_ID2, &phy_id2);
	if (ret)
		return ret;
	/*
	 * PHY_ID1: [bit15-0:ID(21-6)]
	 * PHY_ID2: [bit15-10:ID(5-0)][bit9-4:Model][bit3-0:revision]
	 */
	phy->id = (u32)phy_id1;
	phy->id = ((phy->id << 6) | ((phy_id2 & 0xFC00) >> 10));
	phy->revision = (u32) (phy_id2 & 0x000F);
	netdev_dbg(adapter->netdev,
		   "phy->id : 0x%08x  phy->revision : 0x%08x\n",
		   phy->id, phy->revision);
	return 0;
}

/**
 * pch_gbe_phy_read_reg_miic - Read MII control register
 * @hw:	     Pointer to the HW structure
 * @offset:  Register offset to be read
 * @data:    Pointer to the read data
 * Returns
 *	0:		Successful.
 *	-EINVAL:	Invalid argument.
 */
s32 pch_gbe_phy_read_reg_miic(struct pch_gbe_hw *hw, u32 offset, u16 *data)
{
	struct pch_gbe_phy_info *phy = &hw->phy;

	if (offset > PHY_MAX_REG_ADDRESS) {
		struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);

		netdev_err(adapter->netdev, "PHY Address %d is out of range\n",
			   offset);
		return -EINVAL;
	}
	*data = pch_gbe_mac_ctrl_miim(hw, phy->addr, PCH_GBE_HAL_MIIM_READ,
				      offset, (u16)0);
	return 0;
}

/**
 * pch_gbe_phy_write_reg_miic - Write MII control register
 * @hw:	     Pointer to the HW structure
 * @offset:  Register offset to be read
 * @data:    data to write to register at offset
 * Returns
 *	0:		Successful.
 *	-EINVAL:	Invalid argument.
 */
s32 pch_gbe_phy_write_reg_miic(struct pch_gbe_hw *hw, u32 offset, u16 data)
{
	struct pch_gbe_phy_info *phy = &hw->phy;

	if (offset > PHY_MAX_REG_ADDRESS) {
		struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);

		netdev_err(adapter->netdev, "PHY Address %d is out of range\n",
			   offset);
		return -EINVAL;
	}
	pch_gbe_mac_ctrl_miim(hw, phy->addr, PCH_GBE_HAL_MIIM_WRITE,
				 offset, data);
	return 0;
}

/**
 * pch_gbe_phy_sw_reset - PHY software reset
 * @hw:	            Pointer to the HW structure
 */
static void pch_gbe_phy_sw_reset(struct pch_gbe_hw *hw)
{
	u16 phy_ctrl;

	pch_gbe_phy_read_reg_miic(hw, PHY_CONTROL, &phy_ctrl);
	phy_ctrl |= MII_CR_RESET;
	pch_gbe_phy_write_reg_miic(hw, PHY_CONTROL, phy_ctrl);
	udelay(1);
}

/**
 * pch_gbe_phy_hw_reset - PHY hardware reset
 * @hw:	   Pointer to the HW structure
 */
void pch_gbe_phy_hw_reset(struct pch_gbe_hw *hw)
{
	pch_gbe_phy_write_reg_miic(hw, PHY_CONTROL, PHY_CONTROL_DEFAULT);
	pch_gbe_phy_write_reg_miic(hw, PHY_AUTONEG_ADV,
					PHY_AUTONEG_ADV_DEFAULT);
	pch_gbe_phy_write_reg_miic(hw, PHY_NEXT_PAGE_TX,
					PHY_NEXT_PAGE_TX_DEFAULT);
	pch_gbe_phy_write_reg_miic(hw, PHY_1000T_CTRL, PHY_1000T_CTRL_DEFAULT);
	pch_gbe_phy_write_reg_miic(hw, PHY_PHYSP_CONTROL,
					PHY_PHYSP_CONTROL_DEFAULT);
}

/**
 * pch_gbe_phy_power_up - restore link in case the phy was powered down
 * @hw:	   Pointer to the HW structure
 */
void pch_gbe_phy_power_up(struct pch_gbe_hw *hw)
{
	u16 mii_reg;

	mii_reg = 0;
	/* Just clear the power down bit to wake the phy back up */
	/* according to the manual, the phy will retain its
	 * settings across a power-down/up cycle */
	pch_gbe_phy_read_reg_miic(hw, PHY_CONTROL, &mii_reg);
	mii_reg &= ~MII_CR_POWER_DOWN;
	pch_gbe_phy_write_reg_miic(hw, PHY_CONTROL, mii_reg);
}

/**
 * pch_gbe_phy_power_down - Power down PHY
 * @hw:	   Pointer to the HW structure
 */
void pch_gbe_phy_power_down(struct pch_gbe_hw *hw)
{
	u16 mii_reg;

	mii_reg = 0;
	/* Power down the PHY so no link is implied when interface is down *
	 * The PHY cannot be powered down if any of the following is TRUE *
	 * (a) WoL is enabled
	 * (b) AMT is active
	 */
	pch_gbe_phy_read_reg_miic(hw, PHY_CONTROL, &mii_reg);
	mii_reg |= MII_CR_POWER_DOWN;
	pch_gbe_phy_write_reg_miic(hw, PHY_CONTROL, mii_reg);
	mdelay(1);
}

/**
 * pch_gbe_phy_set_rgmii - RGMII interface setting
 * @hw:	            Pointer to the HW structure
 */
void pch_gbe_phy_set_rgmii(struct pch_gbe_hw *hw)
{
	pch_gbe_phy_sw_reset(hw);
}

/**
 * pch_gbe_phy_tx_clk_delay - Setup TX clock delay via the PHY
 * @hw:	            Pointer to the HW structure
 * Returns
 *	0:		Successful.
 *	-EINVAL:	Invalid argument.
 */
static int pch_gbe_phy_tx_clk_delay(struct pch_gbe_hw *hw)
{
	/* The RGMII interface requires a ~2ns TX clock delay. This is typically
	 * done in layout with a longer trace or via PHY strapping, but can also
	 * be done via PHY configuration registers.
	 */
	struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);
	u16 mii_reg;
	int ret = 0;

	switch (hw->phy.id) {
	case PHY_AR803X_ID:
		netdev_dbg(adapter->netdev,
			   "Configuring AR803X PHY for 2ns TX clock delay\n");
		pch_gbe_phy_read_reg_miic(hw, PHY_AR8031_DBG_OFF, &mii_reg);
		ret = pch_gbe_phy_write_reg_miic(hw, PHY_AR8031_DBG_OFF,
						 PHY_AR8031_SERDES);
		if (ret)
			break;

		pch_gbe_phy_read_reg_miic(hw, PHY_AR8031_DBG_DAT, &mii_reg);
		mii_reg |= PHY_AR8031_SERDES_TX_CLK_DLY;
		ret = pch_gbe_phy_write_reg_miic(hw, PHY_AR8031_DBG_DAT,
						 mii_reg);
		break;
	default:
		netdev_err(adapter->netdev,
			   "Unknown PHY (%x), could not set TX clock delay\n",
			   hw->phy.id);
		return -EINVAL;
	}

	if (ret)
		netdev_err(adapter->netdev,
			   "Could not configure tx clock delay for PHY\n");
	return ret;
}

/**
 * pch_gbe_phy_init_setting - PHY initial setting
 * @hw:	            Pointer to the HW structure
 */
void pch_gbe_phy_init_setting(struct pch_gbe_hw *hw)
{
	struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);
	struct ethtool_cmd     cmd = { .cmd = ETHTOOL_GSET };
	int ret;
	u16 mii_reg;

	ret = mii_ethtool_gset(&adapter->mii, &cmd);
	if (ret)
		netdev_err(adapter->netdev, "Error: mii_ethtool_gset\n");

	ethtool_cmd_speed_set(&cmd, hw->mac.link_speed);
	cmd.duplex = hw->mac.link_duplex;
	cmd.advertising = hw->phy.autoneg_advertised;
	cmd.autoneg = hw->mac.autoneg;
	pch_gbe_phy_write_reg_miic(hw, MII_BMCR, BMCR_RESET);
	ret = mii_ethtool_sset(&adapter->mii, &cmd);
	if (ret)
		netdev_err(adapter->netdev, "Error: mii_ethtool_sset\n");

	pch_gbe_phy_sw_reset(hw);

	pch_gbe_phy_read_reg_miic(hw, PHY_PHYSP_CONTROL, &mii_reg);
	mii_reg |= PHYSP_CTRL_ASSERT_CRS_TX;
	pch_gbe_phy_write_reg_miic(hw, PHY_PHYSP_CONTROL, mii_reg);

	/* Setup a TX clock delay on certain platforms */
	if (adapter->pdata && adapter->pdata->phy_tx_clk_delay)
		pch_gbe_phy_tx_clk_delay(hw);
}

/**
 * pch_gbe_phy_disable_hibernate - Disable the PHY low power state
 * @hw:	            Pointer to the HW structure
 * Returns
 *	0:		Successful.
 *	-EINVAL:	Invalid argument.
 */
int pch_gbe_phy_disable_hibernate(struct pch_gbe_hw *hw)
{
	struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);
	u16 mii_reg;
	int ret = 0;

	switch (hw->phy.id) {
	case PHY_AR803X_ID:
		netdev_dbg(adapter->netdev,
			   "Disabling hibernation for AR803X PHY\n");
		ret = pch_gbe_phy_write_reg_miic(hw, PHY_AR8031_DBG_OFF,
						 PHY_AR8031_HIBERNATE);
		if (ret)
			break;

		pch_gbe_phy_read_reg_miic(hw, PHY_AR8031_DBG_DAT, &mii_reg);
		mii_reg &= ~PHY_AR8031_PS_HIB_EN;
		ret = pch_gbe_phy_write_reg_miic(hw, PHY_AR8031_DBG_DAT,
						 mii_reg);
		break;
	default:
		netdev_err(adapter->netdev,
			   "Unknown PHY (%x), could not disable hibernation\n",
			   hw->phy.id);
		return -EINVAL;
	}

	if (ret)
		netdev_err(adapter->netdev,
			   "Could not disable PHY hibernation\n");
	return ret;
}
