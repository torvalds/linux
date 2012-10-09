/*
 * drivers/net/phy/lxt.c
 *
 * Driver for Intel LXT PHYs
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/* The Level one LXT970 is used by many boards				     */

#define MII_LXT970_IER       17  /* Interrupt Enable Register */

#define MII_LXT970_IER_IEN	0x0002

#define MII_LXT970_ISR       18  /* Interrupt Status Register */

#define MII_LXT970_CONFIG    19  /* Configuration Register    */

/* ------------------------------------------------------------------------- */
/* The Level one LXT971 is used on some of my custom boards                  */

/* register definitions for the 971 */
#define MII_LXT971_IER		18  /* Interrupt Enable Register */
#define MII_LXT971_IER_IEN	0x00f2

#define MII_LXT971_ISR		19  /* Interrupt Status Register */

/* register definitions for the 973 */
#define MII_LXT973_PCR 16 /* Port Configuration Register */
#define PCR_FIBER_SELECT 1

MODULE_DESCRIPTION("Intel LXT PHY driver");
MODULE_AUTHOR("Andy Fleming");
MODULE_LICENSE("GPL");

static int lxt970_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, MII_BMSR);

	if (err < 0)
		return err;

	err = phy_read(phydev, MII_LXT970_ISR);

	if (err < 0)
		return err;

	return 0;
}

static int lxt970_config_intr(struct phy_device *phydev)
{
	int err;

	if(phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, MII_LXT970_IER, MII_LXT970_IER_IEN);
	else
		err = phy_write(phydev, MII_LXT970_IER, 0);

	return err;
}

static int lxt970_config_init(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_LXT970_CONFIG, 0);

	return err;
}


static int lxt971_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, MII_LXT971_ISR);

	if (err < 0)
		return err;

	return 0;
}

static int lxt971_config_intr(struct phy_device *phydev)
{
	int err;

	if(phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, MII_LXT971_IER, MII_LXT971_IER_IEN);
	else
		err = phy_write(phydev, MII_LXT971_IER, 0);

	return err;
}

static int lxt973_probe(struct phy_device *phydev)
{
	int val = phy_read(phydev, MII_LXT973_PCR);

	if (val & PCR_FIBER_SELECT) {
		/*
		 * If fiber is selected, then the only correct setting
		 * is 100Mbps, full duplex, and auto negotiation off.
		 */
		val = phy_read(phydev, MII_BMCR);
		val |= (BMCR_SPEED100 | BMCR_FULLDPLX);
		val &= ~BMCR_ANENABLE;
		phy_write(phydev, MII_BMCR, val);
		/* Remember that the port is in fiber mode. */
		phydev->priv = lxt973_probe;
	} else {
		phydev->priv = NULL;
	}
	return 0;
}

static int lxt973_config_aneg(struct phy_device *phydev)
{
	/* Do nothing if port is in fiber mode. */
	return phydev->priv ? 0 : genphy_config_aneg(phydev);
}

static struct phy_driver lxt97x_driver[] = {
{
	.phy_id		= 0x78100000,
	.name		= "LXT970",
	.phy_id_mask	= 0xfffffff0,
	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.config_init	= lxt970_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= lxt970_ack_interrupt,
	.config_intr	= lxt970_config_intr,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= 0x001378e0,
	.name		= "LXT971",
	.phy_id_mask	= 0xfffffff0,
	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= lxt971_ack_interrupt,
	.config_intr	= lxt971_config_intr,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= 0x00137a10,
	.name		= "LXT973",
	.phy_id_mask	= 0xfffffff0,
	.features	= PHY_BASIC_FEATURES,
	.flags		= 0,
	.probe		= lxt973_probe,
	.config_aneg	= lxt973_config_aneg,
	.read_status	= genphy_read_status,
	.driver		= { .owner = THIS_MODULE,},
} };

static int __init lxt_init(void)
{
	return phy_drivers_register(lxt97x_driver,
		ARRAY_SIZE(lxt97x_driver));
}

static void __exit lxt_exit(void)
{
	phy_drivers_unregister(lxt97x_driver,
		ARRAY_SIZE(lxt97x_driver));
}

module_init(lxt_init);
module_exit(lxt_exit);

static struct mdio_device_id __maybe_unused lxt_tbl[] = {
	{ 0x78100000, 0xfffffff0 },
	{ 0x001378e0, 0xfffffff0 },
	{ 0x00137a10, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, lxt_tbl);
