/*
 * Driver for the Texas Instruments DP83822 PHY
 *
 * Copyright (C) 2017 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/netdevice.h>

#define DP83822_PHY_ID	        0x2000a240
#define DP83822_DEVADDR		0x1f

#define MII_DP83822_PHYSCR	0x11
#define MII_DP83822_MISR1	0x12
#define MII_DP83822_MISR2	0x13
#define MII_DP83822_RESET_CTRL	0x1f

#define DP83822_HW_RESET	BIT(15)
#define DP83822_SW_RESET	BIT(14)

/* PHYSCR Register Fields */
#define DP83822_PHYSCR_INT_OE		BIT(0) /* Interrupt Output Enable */
#define DP83822_PHYSCR_INTEN		BIT(1) /* Interrupt Enable */

/* MISR1 bits */
#define DP83822_RX_ERR_HF_INT_EN	BIT(0)
#define DP83822_FALSE_CARRIER_HF_INT_EN	BIT(1)
#define DP83822_ANEG_COMPLETE_INT_EN	BIT(2)
#define DP83822_DUP_MODE_CHANGE_INT_EN	BIT(3)
#define DP83822_SPEED_CHANGED_INT_EN	BIT(4)
#define DP83822_LINK_STAT_INT_EN	BIT(5)
#define DP83822_ENERGY_DET_INT_EN	BIT(6)
#define DP83822_LINK_QUAL_INT_EN	BIT(7)

/* MISR2 bits */
#define DP83822_JABBER_DET_INT_EN	BIT(0)
#define DP83822_WOL_PKT_INT_EN		BIT(1)
#define DP83822_SLEEP_MODE_INT_EN	BIT(2)
#define DP83822_MDI_XOVER_INT_EN	BIT(3)
#define DP83822_LB_FIFO_INT_EN		BIT(4)
#define DP83822_PAGE_RX_INT_EN		BIT(5)
#define DP83822_ANEG_ERR_INT_EN		BIT(6)
#define DP83822_EEE_ERROR_CHANGE_INT_EN	BIT(7)

/* INT_STAT1 bits */
#define DP83822_WOL_INT_EN	BIT(4)
#define DP83822_WOL_INT_STAT	BIT(12)

#define MII_DP83822_RXSOP1	0x04a5
#define	MII_DP83822_RXSOP2	0x04a6
#define	MII_DP83822_RXSOP3	0x04a7

/* WoL Registers */
#define	MII_DP83822_WOL_CFG	0x04a0
#define	MII_DP83822_WOL_STAT	0x04a1
#define	MII_DP83822_WOL_DA1	0x04a2
#define	MII_DP83822_WOL_DA2	0x04a3
#define	MII_DP83822_WOL_DA3	0x04a4

/* WoL bits */
#define DP83822_WOL_MAGIC_EN	BIT(0)
#define DP83822_WOL_SECURE_ON	BIT(5)
#define DP83822_WOL_EN		BIT(7)
#define DP83822_WOL_INDICATION_SEL BIT(8)
#define DP83822_WOL_CLR_INDICATION BIT(11)

static int dp83822_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, MII_DP83822_MISR1);
	if (err < 0)
		return err;

	err = phy_read(phydev, MII_DP83822_MISR2);
	if (err < 0)
		return err;

	return 0;
}

static int dp83822_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct net_device *ndev = phydev->attached_dev;
	u16 value;
	const u8 *mac;

	if (wol->wolopts & (WAKE_MAGIC | WAKE_MAGICSECURE)) {
		mac = (const u8 *)ndev->dev_addr;

		if (!is_valid_ether_addr(mac))
			return -EINVAL;

		/* MAC addresses start with byte 5, but stored in mac[0].
		 * 822 PHYs store bytes 4|5, 2|3, 0|1
		 */
		phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_DA1,
			      (mac[1] << 8) | mac[0]);
		phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_DA2,
			      (mac[3] << 8) | mac[2]);
		phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_DA3,
			      (mac[5] << 8) | mac[4]);

		value = phy_read_mmd(phydev, DP83822_DEVADDR,
				     MII_DP83822_WOL_CFG);
		if (wol->wolopts & WAKE_MAGIC)
			value |= DP83822_WOL_MAGIC_EN;
		else
			value &= ~DP83822_WOL_MAGIC_EN;

		if (wol->wolopts & WAKE_MAGICSECURE) {
			phy_write_mmd(phydev, DP83822_DEVADDR,
				      MII_DP83822_RXSOP1,
				      (wol->sopass[1] << 8) | wol->sopass[0]);
			phy_write_mmd(phydev, DP83822_DEVADDR,
				      MII_DP83822_RXSOP2,
				      (wol->sopass[3] << 8) | wol->sopass[2]);
			phy_write_mmd(phydev, DP83822_DEVADDR,
				      MII_DP83822_RXSOP3,
				      (wol->sopass[5] << 8) | wol->sopass[4]);
			value |= DP83822_WOL_SECURE_ON;
		} else {
			value &= ~DP83822_WOL_SECURE_ON;
		}

		value |= (DP83822_WOL_EN | DP83822_WOL_INDICATION_SEL |
			  DP83822_WOL_CLR_INDICATION);
		phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG,
			      value);
	} else {
		value = phy_read_mmd(phydev, DP83822_DEVADDR,
				     MII_DP83822_WOL_CFG);
		value &= ~DP83822_WOL_EN;
		phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG,
			      value);
	}

	return 0;
}

static void dp83822_get_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	int value;
	u16 sopass_val;

	wol->supported = (WAKE_MAGIC | WAKE_MAGICSECURE);
	wol->wolopts = 0;

	value = phy_read_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG);

	if (value & DP83822_WOL_MAGIC_EN)
		wol->wolopts |= WAKE_MAGIC;

	if (value & DP83822_WOL_SECURE_ON) {
		sopass_val = phy_read_mmd(phydev, DP83822_DEVADDR,
					  MII_DP83822_RXSOP1);
		wol->sopass[0] = (sopass_val & 0xff);
		wol->sopass[1] = (sopass_val >> 8);

		sopass_val = phy_read_mmd(phydev, DP83822_DEVADDR,
					  MII_DP83822_RXSOP2);
		wol->sopass[2] = (sopass_val & 0xff);
		wol->sopass[3] = (sopass_val >> 8);

		sopass_val = phy_read_mmd(phydev, DP83822_DEVADDR,
					  MII_DP83822_RXSOP3);
		wol->sopass[4] = (sopass_val & 0xff);
		wol->sopass[5] = (sopass_val >> 8);

		wol->wolopts |= WAKE_MAGICSECURE;
	}

	/* WoL is not enabled so set wolopts to 0 */
	if (!(value & DP83822_WOL_EN))
		wol->wolopts = 0;
}

static int dp83822_config_intr(struct phy_device *phydev)
{
	int misr_status;
	int physcr_status;
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		misr_status = phy_read(phydev, MII_DP83822_MISR1);
		if (misr_status < 0)
			return misr_status;

		misr_status |= (DP83822_RX_ERR_HF_INT_EN |
				DP83822_FALSE_CARRIER_HF_INT_EN |
				DP83822_ANEG_COMPLETE_INT_EN |
				DP83822_DUP_MODE_CHANGE_INT_EN |
				DP83822_SPEED_CHANGED_INT_EN |
				DP83822_LINK_STAT_INT_EN |
				DP83822_ENERGY_DET_INT_EN |
				DP83822_LINK_QUAL_INT_EN);

		err = phy_write(phydev, MII_DP83822_MISR1, misr_status);
		if (err < 0)
			return err;

		misr_status = phy_read(phydev, MII_DP83822_MISR2);
		if (misr_status < 0)
			return misr_status;

		misr_status |= (DP83822_JABBER_DET_INT_EN |
				DP83822_WOL_PKT_INT_EN |
				DP83822_SLEEP_MODE_INT_EN |
				DP83822_MDI_XOVER_INT_EN |
				DP83822_LB_FIFO_INT_EN |
				DP83822_PAGE_RX_INT_EN |
				DP83822_ANEG_ERR_INT_EN |
				DP83822_EEE_ERROR_CHANGE_INT_EN);

		err = phy_write(phydev, MII_DP83822_MISR2, misr_status);
		if (err < 0)
			return err;

		physcr_status = phy_read(phydev, MII_DP83822_PHYSCR);
		if (physcr_status < 0)
			return physcr_status;

		physcr_status |= DP83822_PHYSCR_INT_OE | DP83822_PHYSCR_INTEN;

	} else {
		err = phy_write(phydev, MII_DP83822_MISR1, 0);
		if (err < 0)
			return err;

		err = phy_write(phydev, MII_DP83822_MISR1, 0);
		if (err < 0)
			return err;

		physcr_status = phy_read(phydev, MII_DP83822_PHYSCR);
		if (physcr_status < 0)
			return physcr_status;

		physcr_status &= ~DP83822_PHYSCR_INTEN;
	}

	return phy_write(phydev, MII_DP83822_PHYSCR, physcr_status);
}

static int dp83822_config_init(struct phy_device *phydev)
{
	int err;
	int value;

	err = genphy_config_init(phydev);
	if (err < 0)
		return err;

	value = DP83822_WOL_MAGIC_EN | DP83822_WOL_SECURE_ON | DP83822_WOL_EN;

	return phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG,
	      value);
}

static int dp83822_phy_reset(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_DP83822_RESET_CTRL, DP83822_HW_RESET);
	if (err < 0)
		return err;

	dp83822_config_init(phydev);

	return 0;
}

static int dp83822_suspend(struct phy_device *phydev)
{
	int value;

	value = phy_read_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG);

	if (!(value & DP83822_WOL_EN))
		genphy_suspend(phydev);

	return 0;
}

static int dp83822_resume(struct phy_device *phydev)
{
	int value;

	genphy_resume(phydev);

	value = phy_read_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG);

	phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG, value |
		      DP83822_WOL_CLR_INDICATION);

	return 0;
}

static struct phy_driver dp83822_driver[] = {
	{
		.phy_id = DP83822_PHY_ID,
		.phy_id_mask = 0xfffffff0,
		.name = "TI DP83822",
		.features = PHY_BASIC_FEATURES,
		.config_init = dp83822_config_init,
		.soft_reset = dp83822_phy_reset,
		.get_wol = dp83822_get_wol,
		.set_wol = dp83822_set_wol,
		.ack_interrupt = dp83822_ack_interrupt,
		.config_intr = dp83822_config_intr,
		.suspend = dp83822_suspend,
		.resume = dp83822_resume,
	 },
};
module_phy_driver(dp83822_driver);

static struct mdio_device_id __maybe_unused dp83822_tbl[] = {
	{ DP83822_PHY_ID, 0xfffffff0 },
	{ },
};
MODULE_DEVICE_TABLE(mdio, dp83822_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83822 PHY driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com");
MODULE_LICENSE("GPL");
