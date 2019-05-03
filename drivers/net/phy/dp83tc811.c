// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Texas Instruments DP83TC811 PHY
 *
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 *
 */

#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/netdevice.h>

#define DP83TC811_PHY_ID	0x2000a253
#define DP83811_DEVADDR		0x1f

#define MII_DP83811_SGMII_CTRL	0x09
#define MII_DP83811_INT_STAT1	0x12
#define MII_DP83811_INT_STAT2	0x13
#define MII_DP83811_INT_STAT3	0x18
#define MII_DP83811_RESET_CTRL	0x1f

#define DP83811_HW_RESET	BIT(15)
#define DP83811_SW_RESET	BIT(14)

/* INT_STAT1 bits */
#define DP83811_RX_ERR_HF_INT_EN	BIT(0)
#define DP83811_MS_TRAINING_INT_EN	BIT(1)
#define DP83811_ANEG_COMPLETE_INT_EN	BIT(2)
#define DP83811_ESD_EVENT_INT_EN	BIT(3)
#define DP83811_WOL_INT_EN		BIT(4)
#define DP83811_LINK_STAT_INT_EN	BIT(5)
#define DP83811_ENERGY_DET_INT_EN	BIT(6)
#define DP83811_LINK_QUAL_INT_EN	BIT(7)

/* INT_STAT2 bits */
#define DP83811_JABBER_DET_INT_EN	BIT(0)
#define DP83811_POLARITY_INT_EN		BIT(1)
#define DP83811_SLEEP_MODE_INT_EN	BIT(2)
#define DP83811_OVERTEMP_INT_EN		BIT(3)
#define DP83811_OVERVOLTAGE_INT_EN	BIT(6)
#define DP83811_UNDERVOLTAGE_INT_EN	BIT(7)

/* INT_STAT3 bits */
#define DP83811_LPS_INT_EN	BIT(0)
#define DP83811_NO_FRAME_INT_EN	BIT(3)
#define DP83811_POR_DONE_INT_EN	BIT(4)

#define MII_DP83811_RXSOP1	0x04a5
#define MII_DP83811_RXSOP2	0x04a6
#define MII_DP83811_RXSOP3	0x04a7

/* WoL Registers */
#define MII_DP83811_WOL_CFG	0x04a0
#define MII_DP83811_WOL_STAT	0x04a1
#define MII_DP83811_WOL_DA1	0x04a2
#define MII_DP83811_WOL_DA2	0x04a3
#define MII_DP83811_WOL_DA3	0x04a4

/* WoL bits */
#define DP83811_WOL_MAGIC_EN	BIT(0)
#define DP83811_WOL_SECURE_ON	BIT(5)
#define DP83811_WOL_EN		BIT(7)
#define DP83811_WOL_INDICATION_SEL BIT(8)
#define DP83811_WOL_CLR_INDICATION BIT(11)

/* SGMII CTRL bits */
#define DP83811_TDR_AUTO		BIT(8)
#define DP83811_SGMII_EN		BIT(12)
#define DP83811_SGMII_AUTO_NEG_EN	BIT(13)
#define DP83811_SGMII_TX_ERR_DIS	BIT(14)
#define DP83811_SGMII_SOFT_RESET	BIT(15)

static int dp83811_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, MII_DP83811_INT_STAT1);
	if (err < 0)
		return err;

	err = phy_read(phydev, MII_DP83811_INT_STAT2);
	if (err < 0)
		return err;

	err = phy_read(phydev, MII_DP83811_INT_STAT3);
	if (err < 0)
		return err;

	return 0;
}

static int dp83811_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct net_device *ndev = phydev->attached_dev;
	const u8 *mac;
	u16 value;

	if (wol->wolopts & (WAKE_MAGIC | WAKE_MAGICSECURE)) {
		mac = (const u8 *)ndev->dev_addr;

		if (!is_valid_ether_addr(mac))
			return -EINVAL;

		/* MAC addresses start with byte 5, but stored in mac[0].
		 * 811 PHYs store bytes 4|5, 2|3, 0|1
		 */
		phy_write_mmd(phydev, DP83811_DEVADDR, MII_DP83811_WOL_DA1,
			      (mac[1] << 8) | mac[0]);
		phy_write_mmd(phydev, DP83811_DEVADDR, MII_DP83811_WOL_DA2,
			      (mac[3] << 8) | mac[2]);
		phy_write_mmd(phydev, DP83811_DEVADDR, MII_DP83811_WOL_DA3,
			      (mac[5] << 8) | mac[4]);

		value = phy_read_mmd(phydev, DP83811_DEVADDR,
				     MII_DP83811_WOL_CFG);
		if (wol->wolopts & WAKE_MAGIC)
			value |= DP83811_WOL_MAGIC_EN;
		else
			value &= ~DP83811_WOL_MAGIC_EN;

		if (wol->wolopts & WAKE_MAGICSECURE) {
			phy_write_mmd(phydev, DP83811_DEVADDR,
				      MII_DP83811_RXSOP1,
				      (wol->sopass[1] << 8) | wol->sopass[0]);
			phy_write_mmd(phydev, DP83811_DEVADDR,
				      MII_DP83811_RXSOP2,
				      (wol->sopass[3] << 8) | wol->sopass[2]);
			phy_write_mmd(phydev, DP83811_DEVADDR,
				      MII_DP83811_RXSOP3,
				      (wol->sopass[5] << 8) | wol->sopass[4]);
			value |= DP83811_WOL_SECURE_ON;
		} else {
			value &= ~DP83811_WOL_SECURE_ON;
		}

		value |= (DP83811_WOL_EN | DP83811_WOL_INDICATION_SEL |
			  DP83811_WOL_CLR_INDICATION);
		phy_write_mmd(phydev, DP83811_DEVADDR, MII_DP83811_WOL_CFG,
			      value);
	} else {
		phy_clear_bits_mmd(phydev, DP83811_DEVADDR, MII_DP83811_WOL_CFG,
				   DP83811_WOL_EN);
	}

	return 0;
}

static void dp83811_get_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	u16 sopass_val;
	int value;

	wol->supported = (WAKE_MAGIC | WAKE_MAGICSECURE);
	wol->wolopts = 0;

	value = phy_read_mmd(phydev, DP83811_DEVADDR, MII_DP83811_WOL_CFG);

	if (value & DP83811_WOL_MAGIC_EN)
		wol->wolopts |= WAKE_MAGIC;

	if (value & DP83811_WOL_SECURE_ON) {
		sopass_val = phy_read_mmd(phydev, DP83811_DEVADDR,
					  MII_DP83811_RXSOP1);
		wol->sopass[0] = (sopass_val & 0xff);
		wol->sopass[1] = (sopass_val >> 8);

		sopass_val = phy_read_mmd(phydev, DP83811_DEVADDR,
					  MII_DP83811_RXSOP2);
		wol->sopass[2] = (sopass_val & 0xff);
		wol->sopass[3] = (sopass_val >> 8);

		sopass_val = phy_read_mmd(phydev, DP83811_DEVADDR,
					  MII_DP83811_RXSOP3);
		wol->sopass[4] = (sopass_val & 0xff);
		wol->sopass[5] = (sopass_val >> 8);

		wol->wolopts |= WAKE_MAGICSECURE;
	}

	/* WoL is not enabled so set wolopts to 0 */
	if (!(value & DP83811_WOL_EN))
		wol->wolopts = 0;
}

static int dp83811_config_intr(struct phy_device *phydev)
{
	int misr_status, err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		misr_status = phy_read(phydev, MII_DP83811_INT_STAT1);
		if (misr_status < 0)
			return misr_status;

		misr_status |= (DP83811_RX_ERR_HF_INT_EN |
				DP83811_MS_TRAINING_INT_EN |
				DP83811_ANEG_COMPLETE_INT_EN |
				DP83811_ESD_EVENT_INT_EN |
				DP83811_WOL_INT_EN |
				DP83811_LINK_STAT_INT_EN |
				DP83811_ENERGY_DET_INT_EN |
				DP83811_LINK_QUAL_INT_EN);

		err = phy_write(phydev, MII_DP83811_INT_STAT1, misr_status);
		if (err < 0)
			return err;

		misr_status = phy_read(phydev, MII_DP83811_INT_STAT2);
		if (misr_status < 0)
			return misr_status;

		misr_status |= (DP83811_JABBER_DET_INT_EN |
				DP83811_POLARITY_INT_EN |
				DP83811_SLEEP_MODE_INT_EN |
				DP83811_OVERTEMP_INT_EN |
				DP83811_OVERVOLTAGE_INT_EN |
				DP83811_UNDERVOLTAGE_INT_EN);

		err = phy_write(phydev, MII_DP83811_INT_STAT2, misr_status);
		if (err < 0)
			return err;

		misr_status = phy_read(phydev, MII_DP83811_INT_STAT3);
		if (misr_status < 0)
			return misr_status;

		misr_status |= (DP83811_LPS_INT_EN |
				DP83811_NO_FRAME_INT_EN |
				DP83811_POR_DONE_INT_EN);

		err = phy_write(phydev, MII_DP83811_INT_STAT3, misr_status);

	} else {
		err = phy_write(phydev, MII_DP83811_INT_STAT1, 0);
		if (err < 0)
			return err;

		err = phy_write(phydev, MII_DP83811_INT_STAT2, 0);
		if (err < 0)
			return err;

		err = phy_write(phydev, MII_DP83811_INT_STAT3, 0);
	}

	return err;
}

static int dp83811_config_aneg(struct phy_device *phydev)
{
	int value, err;

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		value = phy_read(phydev, MII_DP83811_SGMII_CTRL);
		if (phydev->autoneg == AUTONEG_ENABLE) {
			err = phy_write(phydev, MII_DP83811_SGMII_CTRL,
					(DP83811_SGMII_AUTO_NEG_EN | value));
			if (err < 0)
				return err;
		} else {
			err = phy_write(phydev, MII_DP83811_SGMII_CTRL,
					(~DP83811_SGMII_AUTO_NEG_EN & value));
			if (err < 0)
				return err;
		}
	}

	return genphy_config_aneg(phydev);
}

static int dp83811_config_init(struct phy_device *phydev)
{
	int value, err;

	err = genphy_config_init(phydev);
	if (err < 0)
		return err;

	value = phy_read(phydev, MII_DP83811_SGMII_CTRL);
	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		err = phy_write(phydev, MII_DP83811_SGMII_CTRL,
					(DP83811_SGMII_EN | value));
	} else {
		err = phy_write(phydev, MII_DP83811_SGMII_CTRL,
				(~DP83811_SGMII_EN & value));
	}

	if (err < 0)

		return err;

	value = DP83811_WOL_MAGIC_EN | DP83811_WOL_SECURE_ON | DP83811_WOL_EN;

	return phy_write_mmd(phydev, DP83811_DEVADDR, MII_DP83811_WOL_CFG,
	      value);
}

static int dp83811_phy_reset(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_DP83811_RESET_CTRL, DP83811_HW_RESET);
	if (err < 0)
		return err;

	return 0;
}

static int dp83811_suspend(struct phy_device *phydev)
{
	int value;

	value = phy_read_mmd(phydev, DP83811_DEVADDR, MII_DP83811_WOL_CFG);

	if (!(value & DP83811_WOL_EN))
		genphy_suspend(phydev);

	return 0;
}

static int dp83811_resume(struct phy_device *phydev)
{
	genphy_resume(phydev);

	phy_set_bits_mmd(phydev, DP83811_DEVADDR, MII_DP83811_WOL_CFG,
			 DP83811_WOL_CLR_INDICATION);

	return 0;
}

static struct phy_driver dp83811_driver[] = {
	{
		.phy_id = DP83TC811_PHY_ID,
		.phy_id_mask = 0xfffffff0,
		.name = "TI DP83TC811",
		.features = PHY_BASIC_FEATURES,
		.config_init = dp83811_config_init,
		.config_aneg = dp83811_config_aneg,
		.soft_reset = dp83811_phy_reset,
		.get_wol = dp83811_get_wol,
		.set_wol = dp83811_set_wol,
		.ack_interrupt = dp83811_ack_interrupt,
		.config_intr = dp83811_config_intr,
		.suspend = dp83811_suspend,
		.resume = dp83811_resume,
	 },
};
module_phy_driver(dp83811_driver);

static struct mdio_device_id __maybe_unused dp83811_tbl[] = {
	{ DP83TC811_PHY_ID, 0xfffffff0 },
	{ },
};
MODULE_DEVICE_TABLE(mdio, dp83811_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83TC811 PHY driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com");
MODULE_LICENSE("GPL");
