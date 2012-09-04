/*
 * Driver for ICPlus PHYs
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
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

MODULE_DESCRIPTION("ICPlus IP175C/IP101A/IP101G/IC1001 PHY drivers");
MODULE_AUTHOR("Michael Barkowski");
MODULE_LICENSE("GPL");

/* IP101A/G - IP1001 */
#define IP10XX_SPEC_CTRL_STATUS		16	/* Spec. Control Register */
#define IP1001_SPEC_CTRL_STATUS_2	20	/* IP1001 Spec. Control Reg 2 */
#define IP1001_PHASE_SEL_MASK		3	/* IP1001 RX/TXPHASE_SEL */
#define IP1001_APS_ON			11	/* IP1001 APS Mode  bit */
#define IP101A_G_APS_ON			2	/* IP101A/G APS Mode bit */
#define IP101A_G_IRQ_CONF_STATUS	0x11	/* Conf Info IRQ & Status Reg */
#define	IP101A_G_IRQ_PIN_USED		(1<<15) /* INTR pin used */
#define	IP101A_G_IRQ_DEFAULT		IP101A_G_IRQ_PIN_USED

static int ip175c_config_init(struct phy_device *phydev)
{
	int err, i;
	static int full_reset_performed = 0;

	if (full_reset_performed == 0) {

		/* master reset */
		err = mdiobus_write(phydev->bus, 30, 0, 0x175c);
		if (err < 0)
			return err;

		/* ensure no bus delays overlap reset period */
		err = mdiobus_read(phydev->bus, 30, 0);

		/* data sheet specifies reset period is 2 msec */
		mdelay(2);

		/* enable IP175C mode */
		err = mdiobus_write(phydev->bus, 29, 31, 0x175c);
		if (err < 0)
			return err;

		/* Set MII0 speed and duplex (in PHY mode) */
		err = mdiobus_write(phydev->bus, 29, 22, 0x420);
		if (err < 0)
			return err;

		/* reset switch ports */
		for (i = 0; i < 5; i++) {
			err = mdiobus_write(phydev->bus, i,
					    MII_BMCR, BMCR_RESET);
			if (err < 0)
				return err;
		}

		for (i = 0; i < 5; i++)
			err = mdiobus_read(phydev->bus, i, MII_BMCR);

		mdelay(2);

		full_reset_performed = 1;
	}

	if (phydev->addr != 4) {
		phydev->state = PHY_RUNNING;
		phydev->speed = SPEED_100;
		phydev->duplex = DUPLEX_FULL;
		phydev->link = 1;
		netif_carrier_on(phydev->attached_dev);
	}

	return 0;
}

static int ip1xx_reset(struct phy_device *phydev)
{
	int bmcr;

	/* Software Reset PHY */
	bmcr = phy_read(phydev, MII_BMCR);
	if (bmcr < 0)
		return bmcr;
	bmcr |= BMCR_RESET;
	bmcr = phy_write(phydev, MII_BMCR, bmcr);
	if (bmcr < 0)
		return bmcr;

	do {
		bmcr = phy_read(phydev, MII_BMCR);
		if (bmcr < 0)
			return bmcr;
	} while (bmcr & BMCR_RESET);

	return 0;
}

static int ip1001_config_init(struct phy_device *phydev)
{
	int c;

	c = ip1xx_reset(phydev);
	if (c < 0)
		return c;

	/* Enable Auto Power Saving mode */
	c = phy_read(phydev, IP1001_SPEC_CTRL_STATUS_2);
	if (c < 0)
		return c;
	c |= IP1001_APS_ON;
	c = phy_write(phydev, IP1001_SPEC_CTRL_STATUS_2, c);
	if (c < 0)
		return c;

	/* INTR pin used: speed/link/duplex will cause an interrupt */
	c = phy_write(phydev, IP101A_G_IRQ_CONF_STATUS, IP101A_G_IRQ_DEFAULT);
	if (c < 0)
		return c;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII) {
		/* Additional delay (2ns) used to adjust RX clock phase
		 * at RGMII interface */
		c = phy_read(phydev, IP10XX_SPEC_CTRL_STATUS);
		if (c < 0)
			return c;

		c |= IP1001_PHASE_SEL_MASK;
		c = phy_write(phydev, IP10XX_SPEC_CTRL_STATUS, c);
		if (c < 0)
			return c;
	}

	return 0;
}

static int ip101a_g_config_init(struct phy_device *phydev)
{
	int c;

	c = ip1xx_reset(phydev);
	if (c < 0)
		return c;

	/* Enable Auto Power Saving mode */
	c = phy_read(phydev, IP10XX_SPEC_CTRL_STATUS);
	c |= IP101A_G_APS_ON;

	return phy_write(phydev, IP10XX_SPEC_CTRL_STATUS, c);
}

static int ip175c_read_status(struct phy_device *phydev)
{
	if (phydev->addr == 4) /* WAN port */
		genphy_read_status(phydev);
	else
		/* Don't need to read status for switch ports */
		phydev->irq = PHY_IGNORE_INTERRUPT;

	return 0;
}

static int ip175c_config_aneg(struct phy_device *phydev)
{
	if (phydev->addr == 4) /* WAN port */
		genphy_config_aneg(phydev);

	return 0;
}

static int ip101a_g_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, IP101A_G_IRQ_CONF_STATUS);
	if (err < 0)
		return err;

	return 0;
}

static struct phy_driver icplus_driver[] = {
{
	.phy_id		= 0x02430d80,
	.name		= "ICPlus IP175C",
	.phy_id_mask	= 0x0ffffff0,
	.features	= PHY_BASIC_FEATURES,
	.config_init	= &ip175c_config_init,
	.config_aneg	= &ip175c_config_aneg,
	.read_status	= &ip175c_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= 0x02430d90,
	.name		= "ICPlus IP1001",
	.phy_id_mask	= 0x0ffffff0,
	.features	= PHY_GBIT_FEATURES | SUPPORTED_Pause |
			  SUPPORTED_Asym_Pause,
	.config_init	= &ip1001_config_init,
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &genphy_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= 0x02430c54,
	.name		= "ICPlus IP101A/G",
	.phy_id_mask	= 0x0ffffff0,
	.features	= PHY_BASIC_FEATURES | SUPPORTED_Pause |
			  SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_INTERRUPT,
	.ack_interrupt	= ip101a_g_ack_interrupt,
	.config_init	= &ip101a_g_config_init,
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &genphy_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
} };

static int __init icplus_init(void)
{
	return phy_drivers_register(icplus_driver,
		ARRAY_SIZE(icplus_driver));
}

static void __exit icplus_exit(void)
{
	phy_drivers_unregister(icplus_driver,
		ARRAY_SIZE(icplus_driver));
}

module_init(icplus_init);
module_exit(icplus_exit);

static struct mdio_device_id __maybe_unused icplus_tbl[] = {
	{ 0x02430d80, 0x0ffffff0 },
	{ 0x02430d90, 0x0ffffff0 },
	{ 0x02430c54, 0x0ffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, icplus_tbl);
