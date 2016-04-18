/*
 * Driver for Aquantia PHY
 *
 * Author: Shaohui Xie <Shaohui.Xie@freescale.com>
 *
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/mdio.h>

#define PHY_ID_AQ1202	0x03a1b445
#define PHY_ID_AQ2104	0x03a1b460
#define PHY_ID_AQR105	0x03a1b4a2
#define PHY_ID_AQR405	0x03a1b4b0

#define PHY_AQUANTIA_FEATURES	(SUPPORTED_10000baseT_Full | \
				 SUPPORTED_1000baseT_Full | \
				 SUPPORTED_100baseT_Full | \
				 PHY_DEFAULT_FEATURES)

static int aquantia_config_aneg(struct phy_device *phydev)
{
	phydev->supported = PHY_AQUANTIA_FEATURES;
	phydev->advertising = phydev->supported;

	return 0;
}

static int aquantia_aneg_done(struct phy_device *phydev)
{
	int reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	return (reg < 0) ? reg : (reg & BMSR_ANEGCOMPLETE);
}

static int aquantia_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = phy_write_mmd(phydev, MDIO_MMD_AN, 0xd401, 1);
		if (err < 0)
			return err;

		err = phy_write_mmd(phydev, MDIO_MMD_VEND1, 0xff00, 1);
		if (err < 0)
			return err;

		err = phy_write_mmd(phydev, MDIO_MMD_VEND1, 0xff01, 0x1001);
	} else {
		err = phy_write_mmd(phydev, MDIO_MMD_AN, 0xd401, 0);
		if (err < 0)
			return err;

		err = phy_write_mmd(phydev, MDIO_MMD_VEND1, 0xff00, 0);
		if (err < 0)
			return err;

		err = phy_write_mmd(phydev, MDIO_MMD_VEND1, 0xff01, 0);
	}

	return err;
}

static int aquantia_ack_interrupt(struct phy_device *phydev)
{
	int reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_AN, 0xcc01);
	return (reg < 0) ? reg : 0;
}

static int aquantia_read_status(struct phy_device *phydev)
{
	int reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	reg = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (reg & MDIO_STAT1_LSTATUS)
		phydev->link = 1;
	else
		phydev->link = 0;

	reg = phy_read_mmd(phydev, MDIO_MMD_AN, 0xc800);
	mdelay(10);
	reg = phy_read_mmd(phydev, MDIO_MMD_AN, 0xc800);

	switch (reg) {
	case 0x9:
		phydev->speed = SPEED_2500;
		break;
	case 0x5:
		phydev->speed = SPEED_1000;
		break;
	case 0x3:
		phydev->speed = SPEED_100;
		break;
	case 0x7:
	default:
		phydev->speed = SPEED_10000;
		break;
	}
	phydev->duplex = DUPLEX_FULL;

	return 0;
}

static struct phy_driver aquantia_driver[] = {
{
	.phy_id		= PHY_ID_AQ1202,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQ1202",
	.features	= PHY_AQUANTIA_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.aneg_done	= aquantia_aneg_done,
	.config_aneg    = aquantia_config_aneg,
	.config_intr	= aquantia_config_intr,
	.ack_interrupt	= aquantia_ack_interrupt,
	.read_status	= aquantia_read_status,
},
{
	.phy_id		= PHY_ID_AQ2104,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQ2104",
	.features	= PHY_AQUANTIA_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.aneg_done	= aquantia_aneg_done,
	.config_aneg    = aquantia_config_aneg,
	.config_intr	= aquantia_config_intr,
	.ack_interrupt	= aquantia_ack_interrupt,
	.read_status	= aquantia_read_status,
},
{
	.phy_id		= PHY_ID_AQR105,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQR105",
	.features	= PHY_AQUANTIA_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.aneg_done	= aquantia_aneg_done,
	.config_aneg    = aquantia_config_aneg,
	.config_intr	= aquantia_config_intr,
	.ack_interrupt	= aquantia_ack_interrupt,
	.read_status	= aquantia_read_status,
},
{
	.phy_id		= PHY_ID_AQR405,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Aquantia AQR405",
	.features	= PHY_AQUANTIA_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.aneg_done	= aquantia_aneg_done,
	.config_aneg    = aquantia_config_aneg,
	.config_intr	= aquantia_config_intr,
	.ack_interrupt	= aquantia_ack_interrupt,
	.read_status	= aquantia_read_status,
},
};

module_phy_driver(aquantia_driver);

static struct mdio_device_id __maybe_unused aquantia_tbl[] = {
	{ PHY_ID_AQ1202, 0xfffffff0 },
	{ PHY_ID_AQ2104, 0xfffffff0 },
	{ PHY_ID_AQR105, 0xfffffff0 },
	{ PHY_ID_AQR405, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, aquantia_tbl);

MODULE_DESCRIPTION("Aquantia PHY driver");
MODULE_AUTHOR("Shaohui Xie <Shaohui.Xie@freescale.com>");
MODULE_LICENSE("GPL v2");
