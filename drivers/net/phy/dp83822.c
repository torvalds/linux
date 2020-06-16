// SPDX-License-Identifier: GPL-2.0
/* Driver for the Texas Instruments DP83822, DP83825 and DP83826 PHYs.
 *
 * Copyright (C) 2017 Texas Instruments Inc.
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
#define DP83825S_PHY_ID		0x2000a140
#define DP83825I_PHY_ID		0x2000a150
#define DP83825CM_PHY_ID	0x2000a160
#define DP83825CS_PHY_ID	0x2000a170
#define DP83826C_PHY_ID		0x2000a130
#define DP83826NC_PHY_ID	0x2000a110

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

		/* Clear any pending WoL interrupt */
		phy_read(phydev, MII_DP83822_MISR2);

		value |= DP83822_WOL_EN | DP83822_WOL_INDICATION_SEL |
			 DP83822_WOL_CLR_INDICATION;

		return phy_write_mmd(phydev, DP83822_DEVADDR,
				     MII_DP83822_WOL_CFG, value);
	} else {
		return phy_clear_bits_mmd(phydev, DP83822_DEVADDR,
					  MII_DP83822_WOL_CFG, DP83822_WOL_EN);
	}
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
	int value = DP83822_WOL_EN | DP83822_WOL_MAGIC_EN |
		    DP83822_WOL_SECURE_ON;

	return phy_clear_bits_mmd(phydev, DP83822_DEVADDR,
				  MII_DP83822_WOL_CFG, value);
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

#define DP83822_PHY_DRIVER(_id, _name)				\
	{							\
		PHY_ID_MATCH_MODEL(_id),			\
		.name		= (_name),			\
		/* PHY_BASIC_FEATURES */			\
		.soft_reset	= dp83822_phy_reset,		\
		.config_init	= dp83822_config_init,		\
		.get_wol = dp83822_get_wol,			\
		.set_wol = dp83822_set_wol,			\
		.ack_interrupt = dp83822_ack_interrupt,		\
		.config_intr = dp83822_config_intr,		\
		.suspend = dp83822_suspend,			\
		.resume = dp83822_resume,			\
	}

static struct phy_driver dp83822_driver[] = {
	DP83822_PHY_DRIVER(DP83822_PHY_ID, "TI DP83822"),
	DP83822_PHY_DRIVER(DP83825I_PHY_ID, "TI DP83825I"),
	DP83822_PHY_DRIVER(DP83826C_PHY_ID, "TI DP83826C"),
	DP83822_PHY_DRIVER(DP83826NC_PHY_ID, "TI DP83826NC"),
	DP83822_PHY_DRIVER(DP83825S_PHY_ID, "TI DP83825S"),
	DP83822_PHY_DRIVER(DP83825CM_PHY_ID, "TI DP83825M"),
	DP83822_PHY_DRIVER(DP83825CS_PHY_ID, "TI DP83825CS"),
};
module_phy_driver(dp83822_driver);

static struct mdio_device_id __maybe_unused dp83822_tbl[] = {
	{ DP83822_PHY_ID, 0xfffffff0 },
	{ DP83825I_PHY_ID, 0xfffffff0 },
	{ DP83826C_PHY_ID, 0xfffffff0 },
	{ DP83826NC_PHY_ID, 0xfffffff0 },
	{ DP83825S_PHY_ID, 0xfffffff0 },
	{ DP83825CM_PHY_ID, 0xfffffff0 },
	{ DP83825CS_PHY_ID, 0xfffffff0 },
	{ },
};
MODULE_DEVICE_TABLE(mdio, dp83822_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83822 PHY driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com");
MODULE_LICENSE("GPL v2");
