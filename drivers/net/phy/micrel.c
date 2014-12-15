/*
 * drivers/net/phy/micrel.c
 *
 * Driver for Micrel PHYs
 *
 * Author: David J. Choi
 *
 * Copyright (c) 2010-2013 Micrel, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support : Micrel Phys:
 *		Giga phys: ksz9021, ksz9031
 *		100/10 Phys : ksz8001, ksz8721, ksz8737, ksz8041
 *			   ksz8021, ksz8031, ksz8051,
 *			   ksz8081, ksz8091,
 *			   ksz8061,
 *		Switch : ksz8873, ksz886x
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/micrel_phy.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
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

static int ksz_config_flags(struct phy_device *phydev)
{
	int regval;

	if (phydev->dev_flags & MICREL_PHY_50MHZ_CLK) {
		regval = phy_read(phydev, MII_KSZPHY_CTRL);
		regval |= KSZ8051_RMII_50MHZ_CLK;
		return phy_write(phydev, MII_KSZPHY_CTRL, regval);
	}
	return 0;
}

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
	int temp;
	temp = phy_read(phydev, 0x16);
	//phy_write(phydev, 0x16, ((temp|(1 << 1)) & ~(0x8020)));
	phy_write(phydev, 0x16, 0x2);
	temp = phy_read(phydev, 0x1f);// link speed 1f bit5 4 = 01
	phy_write(phydev, 0x1f, (temp | 1 << 4));
	return 0;
}

static int ksz8021_config_init(struct phy_device *phydev)
{
	int rc;
	const u16 val = KSZPHY_OMSO_B_CAST_OFF | KSZPHY_OMSO_RMII_OVERRIDE;
	phy_write(phydev, MII_KSZPHY_OMSO, val);
	rc = ksz_config_flags(phydev);
	return rc < 0 ? rc : 0;
}

static int ks8051_config_init(struct phy_device *phydev)
{
	int rc;

	rc = ksz_config_flags(phydev);
	return rc < 0 ? rc : 0;
}

#define KSZ8873MLL_GLOBAL_CONTROL_4	0x06
#define KSZ8873MLL_GLOBAL_CONTROL_4_DUPLEX	(1 << 6)
#define KSZ8873MLL_GLOBAL_CONTROL_4_SPEED	(1 << 4)
int ksz8873mll_read_status(struct phy_device *phydev)
{
	int regval;

	/* dummy read */
	regval = phy_read(phydev, KSZ8873MLL_GLOBAL_CONTROL_4);

	regval = phy_read(phydev, KSZ8873MLL_GLOBAL_CONTROL_4);

	if (regval & KSZ8873MLL_GLOBAL_CONTROL_4_DUPLEX)
		phydev->duplex = DUPLEX_HALF;
	else
		phydev->duplex = DUPLEX_FULL;

	if (regval & KSZ8873MLL_GLOBAL_CONTROL_4_SPEED)
		phydev->speed = SPEED_10;
	else
		phydev->speed = SPEED_100;

	phydev->link = 1;
	phydev->pause = phydev->asym_pause = 0;

	return 0;
}

static int ksz8873mll_config_aneg(struct phy_device *phydev)
{
	return 0;
}
static int ksz8091_suspend(struct phy_device *phydev)
{
	int value;
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, (value | BMCR_PDOWN));
	return 0;
}


static int KSZ8091_resume(struct phy_device *phydev)
{
	int value;
	int i;
	value = phy_read(phydev, MII_BMCR);
	phy_write(phydev, MII_BMCR, (value & ~BMCR_PDOWN));
		/* Software Reset 2 time PHY  */
	for(i = 0; i < 2; i++)
	{
		value = phy_read(phydev, MII_BMCR);
		value |= BMCR_RESET;
		value = phy_write(phydev, MII_BMCR, value);
	do {
		value = phy_read(phydev, MII_BMCR);
		if (value < 0)
			return value;
	} while (value & BMCR_RESET);
	}
	return 0;
}


#define KSZ8091_MMD_CTRL			       0x0d
#define KSZ8091_MMD_REG_DATA			0x0e
#define KSZ8091_WOL_C			              0x00
#define KSZ8091_WOL_CPT0M0			0x01
#define KSZ8091_WOL_CPT0M1			0x02
#define KSZ8091_WOL_CPT0M2			0x03
#define KSZ8091_WOL_CPT0M3			0x04
#define KSZ8091_WOL_CPT0E0			0x05
#define KSZ8091_WOL_CPT0E1			0x06
#define KSZ8091_WOL_CPT0M3			0x04
#define KSZ8091_WOL_CPT0M3			0x04
#define KSZ8091_WOL_MACDA0			0x19
#define KSZ8091_WOL_MACDA1			0x1a
#define KSZ8091_WOL_MACDA2			0x1b
#define KSZ8091_WOL_OMSO                         0x16

static void ksz8091_get_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	int err;
	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;
		// write addr 1f reg 0 bit6 0  disable Magic
	err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x1F);
	err = phy_write(phydev, KSZ8091_MMD_REG_DATA, KSZ8091_WOL_C);// select reg 0
	err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x801f); //read
	if (phy_read(phydev,KSZ8091_MMD_REG_DATA ) & ( 1<<6))
		wol->wolopts |= WAKE_MAGIC;
}


static int ksz8091_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	int err, oldpage, temp;

	oldpage = phy_read(phydev, KSZ8091_MMD_CTRL);
/*
Magic-packet detection is enabled by writing a 1 to MMD address 1Fh,register 0h, bit [6]
 The MAC address (for the local MAC device) is written to and stored in MMD address 1Fh, registers 19h -1Bh
The KSZ8091MNX/RNB does not generate the magic packet. The magic packet must be provided by the external system.
*/
	printk("wol->wolopts = %d\nWAKE_MAGIC = %d\n",wol->wolopts,WAKE_MAGIC);
	int i = 0;
	if (wol->wolopts & WAKE_MAGIC) {
	temp =  phy_read(phydev, KSZ8091_WOL_OMSO);

	phy_write(phydev, KSZ8091_WOL_OMSO,temp| 1<<15);
		/* Explicitly switch to page 0x1F, just to be sure */
	printk("wol->wolopts = %d\nWAKE_MAGIC = %d\n",wol->wolopts,WAKE_MAGIC);
	for(i =0;i< 6;i++){
		printk("phydev->attached_dev->dev_addr[%d] = %x \n ",i,phydev->attached_dev->dev_addr[i]);
	}

		/* Store the device address for the magic packet */
	/***************************************KSZ8091_WOL_MACDA2************************************************************/
		err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x1F);
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_REG_DATA, KSZ8091_WOL_MACDA2);
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x401f); //write
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_REG_DATA, (phydev->attached_dev->dev_addr[5] ) << 8 |(phydev->attached_dev->dev_addr[4]));
		if (err < 0)
			return err;
	/***************************************KSZ8091_WOL_MACDA1*********************************************/
		err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x1F);
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_REG_DATA, KSZ8091_WOL_MACDA1);
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x401f); //write
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_REG_DATA, (phydev->attached_dev->dev_addr[3] ) << 8 | (phydev->attached_dev->dev_addr[2]));
		if (err < 0)
			return err;
	/***************************************KSZ8091_WOL_MACDA0*****************************************************/
		err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x1F);
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_REG_DATA, KSZ8091_WOL_MACDA0);// select reg 0
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x401f); //write
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_REG_DATA, (phydev->attached_dev->dev_addr[1] ) << 8 |(phydev->attached_dev->dev_addr[0]));
		if (err < 0)
			return err;
	/**************************************************MACEND***************************************************/
		err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x1F);
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_REG_DATA, KSZ8091_WOL_C);// select reg 0
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x401f); //write
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_REG_DATA, 1<<6| 1<<14); //link down enable
		if (err < 0)
			return err;
		/* Clear WOL status and enable magic packet matching */
	} else {
		// write addr 1f reg 0 bit6 0  disable Magic
		err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x1F);
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_REG_DATA, KSZ8091_WOL_C);// select reg 0
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_CTRL, 0x401f); //write
		if (err < 0)
			return err;
		err = phy_write(phydev, KSZ8091_MMD_REG_DATA,  ~(1<<6)); //link down enable
		if (err < 0)
			return err;
	temp =  phy_read(phydev, KSZ8091_WOL_OMSO);

	phy_write(phydev, KSZ8091_WOL_OMSO,temp|~( 1<<15));
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
	.name		= "Micrel KSZ8021 or KSZ8031",
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
	.phy_id		= PHY_ID_KSZ8031,
	.phy_id_mask	= 0x00ffffff,
	.name		= "Micrel KSZ8031",
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
	.phy_id		= PHY_ID_KSZ8081,
	.name		= "Micrel KSZ8081 or KSZ8091",
	.phy_id_mask	= 0x00fffff0,
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend   	= ksz8091_suspend,
	.resume		= KSZ8091_resume,
	.set_wol        = ksz8091_set_wol,
	.get_wol         = ksz8091_get_wol,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8061,
	.name		= "Micrel KSZ8061",
	.phy_id_mask	= 0x00fffff0,
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
	.features	= (PHY_GBIT_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= ksz9021_config_intr,
	.driver		= { .owner = THIS_MODULE, },
}, {
	.phy_id		= PHY_ID_KSZ9031,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ9031 Gigabit PHY",
	.features	= (PHY_GBIT_FEATURES | SUPPORTED_Pause
				| SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= ksz9021_config_intr,
	.driver		= { .owner = THIS_MODULE, },
}, {
	.phy_id		= PHY_ID_KSZ8873MLL,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8873MLL Switch",
	.features	= (SUPPORTED_Pause | SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG,
	.config_init	= kszphy_config_init,
	.config_aneg	= ksz8873mll_config_aneg,
	.read_status	= ksz8873mll_read_status,
	.driver		= { .owner = THIS_MODULE, },
}, {
	.phy_id		= PHY_ID_KSZ886X,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ886X Switch",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
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
	{ PHY_ID_KSZ9031, 0x00fffff0 },
	{ PHY_ID_KSZ8001, 0x00ffffff },
	{ PHY_ID_KS8737, 0x00fffff0 },
	{ PHY_ID_KSZ8021, 0x00ffffff },
	{ PHY_ID_KSZ8031, 0x00ffffff },
	{ PHY_ID_KSZ8041, 0x00fffff0 },
	{ PHY_ID_KSZ8051, 0x00fffff0 },
	{ PHY_ID_KSZ8061, 0x00fffff0 },
	{ PHY_ID_KSZ8081, 0x00fffff0 },
	{ PHY_ID_KSZ8873MLL, 0x00fffff0 },
	{ PHY_ID_KSZ886X, 0x00fffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, micrel_tbl);
