/*
 * Driver for Vitesse PHYs
 *
 * Author: Kriston Carson
 *
 * Copyright (c) 2005 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>

/* Vitesse Extended Control Register 1 */
#define MII_VSC8244_EXT_CON1           0x17
#define MII_VSC8244_EXTCON1_INIT       0x0000

/* Vitesse Interrupt Mask Register */
#define MII_VSC8244_IMASK		0x19
#define MII_VSC8244_IMASK_IEN		0x8000
#define MII_VSC8244_IMASK_SPEED		0x4000
#define MII_VSC8244_IMASK_LINK		0x2000
#define MII_VSC8244_IMASK_DUPLEX	0x1000
#define MII_VSC8244_IMASK_MASK		0xf000

/* Vitesse Interrupt Status Register */
#define MII_VSC8244_ISTAT		0x1a
#define MII_VSC8244_ISTAT_STATUS	0x8000
#define MII_VSC8244_ISTAT_SPEED		0x4000
#define MII_VSC8244_ISTAT_LINK		0x2000
#define MII_VSC8244_ISTAT_DUPLEX	0x1000

/* Vitesse Auxiliary Control/Status Register */
#define MII_VSC8244_AUX_CONSTAT        	0x1c
#define MII_VSC8244_AUXCONSTAT_INIT    	0x0004
#define MII_VSC8244_AUXCONSTAT_DUPLEX  	0x0020
#define MII_VSC8244_AUXCONSTAT_SPEED   	0x0018
#define MII_VSC8244_AUXCONSTAT_GBIT    	0x0010
#define MII_VSC8244_AUXCONSTAT_100     	0x0008

MODULE_DESCRIPTION("Vitesse PHY driver");
MODULE_AUTHOR("Kriston Carson");
MODULE_LICENSE("GPL");

static int vsc824x_config_init(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_VSC8244_AUX_CONSTAT,
			MII_VSC8244_AUXCONSTAT_INIT);
	if (err < 0)
		return err;

	err = phy_write(phydev, MII_VSC8244_EXT_CON1,
			MII_VSC8244_EXTCON1_INIT);
	return err;
}

static int vsc824x_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, MII_VSC8244_ISTAT);

	return (err < 0) ? err : 0;
}

static int vsc824x_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, MII_VSC8244_IMASK,
				MII_VSC8244_IMASK_MASK);
	else
		err = phy_write(phydev, MII_VSC8244_IMASK, 0);
	return err;
}

/* Vitesse 824x */
static struct phy_driver vsc8244_driver = {
	.phy_id		= 0x000fc6c2,
	.name		= "Vitesse VSC8244",
	.phy_id_mask	= 0x000fffc0,
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.config_init	= &vsc824x_config_init,
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &genphy_read_status,
	.ack_interrupt	= &vsc824x_ack_interrupt,
	.config_intr	= &vsc824x_config_intr,
	.driver 	= { .owner = THIS_MODULE,},
};

static int __init vsc8244_init(void)
{
	return phy_driver_register(&vsc8244_driver);
}

static void __exit vsc8244_exit(void)
{
	phy_driver_unregister(&vsc8244_driver);
}

module_init(vsc8244_init);
module_exit(vsc8244_exit);
