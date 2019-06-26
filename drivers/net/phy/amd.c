// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for AMD am79c PHYs
 *
 * Author: Heiko Schocher <hs@denx.de>
 *
 * Copyright (c) 2011 DENX Software Engineering GmbH
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/phy.h>

#define PHY_ID_AM79C874		0x0022561b

#define MII_AM79C_IR		17	/* Interrupt Status/Control Register */
#define MII_AM79C_IR_EN_LINK	0x0400	/* IR enable Linkstate */
#define MII_AM79C_IR_EN_ANEG	0x0100	/* IR enable Aneg Complete */
#define MII_AM79C_IR_IMASK_INIT	(MII_AM79C_IR_EN_LINK | MII_AM79C_IR_EN_ANEG)

MODULE_DESCRIPTION("AMD PHY driver");
MODULE_AUTHOR("Heiko Schocher <hs@denx.de>");
MODULE_LICENSE("GPL");

static int am79c_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, MII_BMSR);
	if (err < 0)
		return err;

	err = phy_read(phydev, MII_AM79C_IR);
	if (err < 0)
		return err;

	return 0;
}

static int am79c_config_init(struct phy_device *phydev)
{
	return 0;
}

static int am79c_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, MII_AM79C_IR, MII_AM79C_IR_IMASK_INIT);
	else
		err = phy_write(phydev, MII_AM79C_IR, 0);

	return err;
}

static struct phy_driver am79c_driver[] = { {
	.phy_id		= PHY_ID_AM79C874,
	.name		= "AM79C874",
	.phy_id_mask	= 0xfffffff0,
	.features	= PHY_BASIC_FEATURES,
	.config_init	= am79c_config_init,
	.ack_interrupt	= am79c_ack_interrupt,
	.config_intr	= am79c_config_intr,
} };

module_phy_driver(am79c_driver);

static struct mdio_device_id __maybe_unused amd_tbl[] = {
	{ PHY_ID_AM79C874, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, amd_tbl);
