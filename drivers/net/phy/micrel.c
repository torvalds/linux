/*
 * drivers/net/phy/micrel.c
 *
 * Driver for Micrel PHYs
 *
 * Author: David J. Choi
 *
 * Copyright (c) 2010 Micrel, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support : ksz9021 1000/100/10 phy from Micrel
 *		ks8001, ks8737, ks8721, ks8041, ks8051 100/10 phy
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/micrel_phy.h>

/* Operation Mode Strap Override */
#define MII_KSZPHY_OMSO				0x16
#define KSZPHY_OMSO_B_CAST_OFF			(1 << 9)
#define KSZPHY_OMSO_RMII_OVERRIDE		(1 << 1)
#define KSZPHY_OMSO_MII_OVERRIDE		(1 << 0)

/* general Interrupt control/status reg in vendor specific block. */
#define MII_KSZPHY_INTCS			0x1B
#define	KSZPHY_INTCS_JABBER			(1 << 15)
#define	KSZPHY_INTCS_RECEIVE_ERR		(1 << 14)
#define	KSZPHY_INTCS_PAGE_RECEIVE		(1 << 13)
#define	KSZPHY_INTCS_PARELLEL			(1 << 12)
#define	KSZPHY_INTCS_LINK_PARTNER_ACK		(1 << 11)
#define	KSZPHY_INTCS_LINK_DOWN			(1 << 10)
#define	KSZPHY_INTCS_REMOTE_FAULT		(1 << 9)
#define	KSZPHY_INTCS_LINK_UP			(1 << 8)
#define	KSZPHY_INTCS_ALL			(KSZPHY_INTCS_LINK_UP |\
						KSZPHY_INTCS_LINK_DOWN)

/* general PHY control reg in vendor specific block. */
#define	MII_KSZPHY_CTRL			0x1F
/* bitmap of PHY register to set interrupt mode */
#define KSZPHY_CTRL_INT_ACTIVE_HIGH		(1 << 9)
#define KSZ9021_CTRL_INT_ACTIVE_HIGH		(1 << 14)
#define KS8737_CTRL_INT_ACTIVE_HIGH		(1 << 14)
#define KSZ8051_RMII_50MHZ_CLK			(1 << 7)

static int kszphy_ack_interrupt(struct phy_device *phydev)
{
	/* bit[7..0] int status, which is a read and clear register. */
	int rc;

	rc = phy_read(phydev, MII_KSZPHY_INTCS);

	return (rc < 0) ? rc : 0;
}

static int kszphy_set_interrupt(struct phy_device *phydev)
{
	int temp;
	temp = (PHY_INTERRUPT_ENABLED == phydev->interrupts) ?
		KSZPHY_INTCS_ALL : 0;
	return phy_write(phydev, MII_KSZPHY_INTCS, temp);
}

static int kszphy_config_intr(struct phy_device *phydev)
{
	int temp, rc;

	/* set the interrupt pin active low */
	temp = phy_read(phydev, MII_KSZPHY_CTRL);
	temp &= ~KSZPHY_CTRL_INT_ACTIVE_HIGH;
	phy_write(phydev, MII_KSZPHY_CTRL, temp);
	rc = kszphy_set_interrupt(phydev);
	return rc < 0 ? rc : 0;
}

static int ksz9021_config_intr(struct phy_device *phydev)
{
	int temp, rc;

	/* set the interrupt pin active low */
	temp = phy_read(phydev, MII_KSZPHY_CTRL);
	temp &= ~KSZ9021_CTRL_INT_ACTIVE_HIGH;
	phy_write(phydev, MII_KSZPHY_CTRL, temp);
	rc = kszphy_set_interrupt(phydev);
	return rc < 0 ? rc : 0;
}

static int ks8737_config_intr(struct phy_device *phydev)
{
	int temp, rc;

	/* set the interrupt pin active low */
	temp = phy_read(phydev, MII_KSZPHY_CTRL);
	temp &= ~KS8737_CTRL_INT_ACTIVE_HIGH;
	phy_write(phydev, MII_KSZPHY_CTRL, temp);
	rc = kszphy_set_interrupt(phydev);
	return rc < 0 ? rc : 0;
}

static int kszphy_config_init(struct phy_device *phydev)
{
	return 0;
}

static int ksz8021_config_init(struct phy_device *phydev)
{
	const u16 val = KSZPHY_OMSO_B_CAST_OFF | KSZPHY_OMSO_RMII_OVERRIDE;
	phy_write(phydev, MII_KSZPHY_OMSO, val);
	return 0;
}

static int ks8051_config_init(struct phy_device *phydev)
{
	int regval;

	if (phydev->dev_flags & MICREL_PHY_50MHZ_CLK) {
		regval = phy_read(phydev, MII_KSZPHY_CTRL);
		regval |= KSZ8051_RMII_50MHZ_CLK;
		phy_write(phydev, MII_KSZPHY_CTRL, regval);
	}

	return 0;
}

static struct phy_driver ksphy_driver[] = {
{
	.phy_id		= PHY_ID_KS8737,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KS8737",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= ks8737_config_intr,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8021,
	.phy_id_mask	= 0x00ffffff,
	.name		= "Micrel KSZ8021",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause |
			   SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= ksz8021_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8041,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8041",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause
				| SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8051,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8051",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause
				| SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= ks8051_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8001,
	.name		= "Micrel KSZ8001 or KS8721",
	.phy_id_mask	= 0x00ffffff,
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ9021,
	.phy_id_mask	= 0x000ffffe,
	.name		= "Micrel KSZ9021 Gigabit PHY",
	.features	= (PHY_GBIT_FEATURES | SUPPORTED_Pause
				| SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= ksz9021_config_intr,
	.driver		= { .owner = THIS_MODULE, },
} };

static int __init ksphy_init(void)
{
	return phy_drivers_register(ksphy_driver,
		ARRAY_SIZE(ksphy_driver));
}

static void __exit ksphy_exit(void)
{
	phy_drivers_unregister(ksphy_driver,
		ARRAY_SIZE(ksphy_driver));
}

module_init(ksphy_init);
module_exit(ksphy_exit);

MODULE_DESCRIPTION("Micrel PHY driver");
MODULE_AUTHOR("David J. Choi");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused micrel_tbl[] = {
	{ PHY_ID_KSZ9021, 0x000ffffe },
	{ PHY_ID_KSZ8001, 0x00ffffff },
	{ PHY_ID_KS8737, 0x00fffff0 },
	{ PHY_ID_KSZ8021, 0x00ffffff },
	{ PHY_ID_KSZ8041, 0x00fffff0 },
	{ PHY_ID_KSZ8051, 0x00fffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, micrel_tbl);
