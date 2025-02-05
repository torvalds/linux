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

#define PHY_ID_AC101L		0x00225520
#define PHY_ID_AM79C874		0x0022561b

#define MII_AM79C_IR		17	/* Interrupt Status/Control Register */
#define MII_AM79C_IR_EN_LINK	0x0400	/* IR enable Linkstate */
#define MII_AM79C_IR_EN_ANEG	0x0100	/* IR enable Aneg Complete */
#define MII_AM79C_IR_IMASK_INIT	(MII_AM79C_IR_EN_LINK | MII_AM79C_IR_EN_ANEG)

#define MII_AM79C_IR_LINK_DOWN	BIT(2)
#define MII_AM79C_IR_ANEG_DONE	BIT(0)
#define MII_AM79C_IR_IMASK_STAT	(MII_AM79C_IR_LINK_DOWN | MII_AM79C_IR_ANEG_DONE)

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

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = am79c_ack_interrupt(phydev);
		if (err)
			return err;

		err = phy_write(phydev, MII_AM79C_IR, MII_AM79C_IR_IMASK_INIT);
	} else {
		err = phy_write(phydev, MII_AM79C_IR, 0);
		if (err)
			return err;

		err = am79c_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t am79c_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, MII_AM79C_IR);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & MII_AM79C_IR_IMASK_STAT))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static struct phy_driver am79c_drivers[] = {
	{
		.phy_id		= PHY_ID_AM79C874,
		.name		= "AM79C874",
		.phy_id_mask	= 0xfffffff0,
		/* PHY_BASIC_FEATURES */
		.config_init	= am79c_config_init,
		.config_intr	= am79c_config_intr,
		.handle_interrupt = am79c_handle_interrupt,
	},
	{
		.phy_id		= PHY_ID_AC101L,
		.name		= "AC101L",
		.phy_id_mask	= 0xfffffff0,
		/* PHY_BASIC_FEATURES */
		.config_init	= am79c_config_init,
		.config_intr	= am79c_config_intr,
		.handle_interrupt = am79c_handle_interrupt,
	},
};

module_phy_driver(am79c_drivers);

static const struct mdio_device_id __maybe_unused amd_tbl[] = {
	{ PHY_ID_AC101L, 0xfffffff0 },
	{ PHY_ID_AM79C874, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, amd_tbl);
