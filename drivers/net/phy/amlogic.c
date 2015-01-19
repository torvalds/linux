/*
 * drivers/net/phy/amlogic.c
 *
 * Driver for AMLOGIC PHY
 *
 * Author: Baoqi wang
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support added for Amlogic Internal Phy by baoqi.wang@amlogic.com
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/netdevice.h>

#define  SMI_ADDR_TSTCNTL     20
#define  SMI_ADDR_TSTREAD1    21
#define  SMI_ADDR_TSTREAD2    22
#define  SMI_ADDR_TSTWRITE    23

#define  WR_ADDR_A1CFG        0x12
#define  WR_ADDR_A2CFG        0x13
#define  WR_ADDR_A3CFG        0x14
#define  WR_ADDR_A4CFG        0x15
#define  WR_ADDR_A5CFG        0x16
#define  WR_ADDR_A6CFG        0x17
#define  WR_ADDR_A7CFG        0x18
#define  WR_ADDR_A8CFG        0x1a
#define  WR_ADDR_A9CFG        0x1b
#define  WR_ADDR_A10CFG       0x1c
#define  WR_ADDR_A11CFG       0x1d

#define  RD_ADDR_A3CFG        (0x14 << 5)
#define  RD_ADDR_A4CFG        (0x15 << 5)
#define  RD_ADDR_A5CFG        (0x16 << 5)
#define  RD_ADDR_A6CFG        (0x17 << 5)

#define  TSTCNTL_RD           ((1 << 15) | (1 << 10))
#define  TSTCNTL_WR           ((1 << 14) | (1 << 10))

#define MII_INTERNAL_ISF 29 /* Interrupt Source Flags */
#define MII_INTERNAL_IM  30 /* Interrupt Mask */
#define MII_INTERNAL_CTRL_STATUS 17 /* Mode/Status Register */
#define MII_INTERNAL_SPECIAL_MODES 18 /* Special Modes Register */

#define MII_INTERNAL_ISF_INT1 (1<<1) /* Auto-Negotiation Page Received */
#define MII_INTERNAL_ISF_INT2 (1<<2) /* Parallel Detection Fault */
#define MII_INTERNAL_ISF_INT3 (1<<3) /* Auto-Negotiation LP Ack */
#define MII_INTERNAL_ISF_INT4 (1<<4) /* Link Down */
#define MII_INTERNAL_ISF_INT5 (1<<5) /* Remote Fault Detected */
#define MII_INTERNAL_ISF_INT6 (1<<6) /* Auto-Negotiation complete */
#define MII_INTERNAL_ISF_INT7 (1<<7) /* ENERGYON */

#define MII_INTERNAL_ISF_INT_ALL (0x0e)

#define MII_INTERNAL_ISF_INT_PHYLIB_EVENTS \
	(MII_INTERNAL_ISF_INT6 | MII_INTERNAL_ISF_INT4 | \
	 MII_INTERNAL_ISF_INT7)

#define MII_INTERNAL_EDPWRDOWN (1 << 13) /* EDPWRDOWN */
#define MII_INTERNAL_ENERGYON  (1 << 1)  /* ENERGYON */

#define MII_INTERNAL_MODE_MASK      0xE0
#define MII_INTERNAL_MODE_POWERDOWN 0xC0 /* Power Down mode */
#define MII_INTERNAL_MODE_ALL       0xE0 /* All capable mode */
static void initTSTMODE(struct phy_device *phydev)
{
        phy_write(phydev, SMI_ADDR_TSTCNTL, 0x0400);
        phy_write(phydev, SMI_ADDR_TSTCNTL, 0x0000);
        phy_write(phydev, SMI_ADDR_TSTCNTL, 0x0400);

}

static void closeTSTMODE(struct phy_device *phydev)
{
	phy_write(phydev, SMI_ADDR_TSTCNTL, 0x0000);

}


static void init_internal_phy(struct phy_device *phydev)
{
        initTSTMODE(phydev);
        // write tstcntl addr val
        phy_write(phydev,SMI_ADDR_TSTWRITE,0x1354);//write val
        phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR);//write addr 0
        phy_write(phydev,SMI_ADDR_TSTWRITE,0x3e01);//write val
        phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A2CFG);//write addr 0x13
        phy_write(phydev,SMI_ADDR_TSTWRITE,0x8900);//write val
        phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A3CFG);//write addr 0x14
        phy_write(phydev,SMI_ADDR_TSTWRITE,0x3412);//write val
        phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A4CFG);//write addr 0x15
        phy_write(phydev,SMI_ADDR_TSTWRITE,0x2636);//write val
        phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A5CFG);//write addr 0x16
        phy_write(phydev,SMI_ADDR_TSTWRITE,3);//write val
        phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A7CFG);//write addr 0x18
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x108);
        phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A9CFG);//write addr 0x1b
        phy_write(phydev,SMI_ADDR_TSTWRITE,0xda00);//write val
        phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A11CFG);//write addr 0x1d
        closeTSTMODE(phydev);
}


void init_internal_phy_10B(struct phy_device *phydev)
{

	initTSTMODE(phydev);
	// write tstcntl addr val
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x0000);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR);//write addr 0
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x3e01);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A2CFG);//write addr 0x13
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x8900);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A3CFG);//write addr 0x14
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x3412);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A4CFG);//write addr 0x15
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x2236);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A5CFG);//write addr 0x16
	phy_write(phydev,SMI_ADDR_TSTWRITE,3);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A7CFG);//write addr 0x18
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x108);//write val by chandle (2)
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A9CFG);//write addr 0x1b
	phy_write(phydev,SMI_ADDR_TSTWRITE,0xda06);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A11CFG);//write addr 0x1d
	closeTSTMODE(phydev);
}

void init_internal_phy_100B(struct phy_device *phydev)
{

	initTSTMODE(phydev);
	// write tstcntl addr val
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x9354);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|0x00);//write addr 0x00
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x3e00);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A2CFG);//write addr 0x13
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x8900);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A3CFG);//write addr 0x14
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x3412);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A4CFG);//write addr 0x15
	phy_write(phydev,SMI_ADDR_TSTWRITE,0xa406);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A5CFG);//write addr 0x16
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x0003);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A7CFG);//write addr 0x18
	phy_write(phydev,SMI_ADDR_TSTWRITE,0x00a6);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A9CFG);//write addr 0x1b
	phy_write(phydev,SMI_ADDR_TSTWRITE,0xda06);//write val
	phy_write(phydev,SMI_ADDR_TSTCNTL,TSTCNTL_WR|WR_ADDR_A11CFG);//write addr 0x1d
	closeTSTMODE(phydev);
}

static int amlogic_phy_config_intr(struct phy_device *phydev)
{
	int rc = phy_write (phydev, MII_INTERNAL_IM,
			((PHY_INTERRUPT_ENABLED == phydev->interrupts)
			? MII_INTERNAL_ISF_INT_PHYLIB_EVENTS
			: 0));

	return rc < 0 ? rc : 0;
}

static int amlogic_phy_ack_interrupt(struct phy_device *phydev)
{
	int rc = phy_read (phydev, MII_INTERNAL_ISF);

	return rc < 0 ? rc : 0;
}

static int amlogic_phy_config_init(struct phy_device *phydev)
{
	int rc = phy_read(phydev, MII_INTERNAL_SPECIAL_MODES);
	if (rc < 0)
		return rc;
	init_internal_phy(phydev);
	/* If the SMSC PHY is in power down mode, then set it
	 * in all capable mode before using it.
	 */
	if ((rc & MII_INTERNAL_MODE_MASK) == MII_INTERNAL_MODE_POWERDOWN) {
		int timeout = 50000;

		/* set "all capable" mode and reset the phy */
		rc |= MII_INTERNAL_MODE_ALL;
		phy_write(phydev, MII_INTERNAL_SPECIAL_MODES, rc);
		phy_write(phydev, MII_BMCR, BMCR_RESET);

		/* wait end of reset (max 500 ms) */
		do {
			udelay(10);
			if (timeout-- == 0)
				return -1;
			rc = phy_read(phydev, MII_BMCR);
		} while (rc & BMCR_RESET);
	}

	rc = phy_read(phydev, MII_INTERNAL_CTRL_STATUS);
	if (rc < 0)
		return rc;

	/* Enable energy detect mode for this SMSC Transceivers */
	rc = phy_write(phydev, MII_INTERNAL_CTRL_STATUS,
		       rc | MII_INTERNAL_EDPWRDOWN);
	if (rc < 0)
		return rc;

	return amlogic_phy_ack_interrupt (phydev);
}

/*
 * This workaround will manually toggle the PHY on/off upon calls to read_status
 * in order to generate link test pulses if the link is down.  If a link partner
 * is present, it will respond to the pulses, which will cause the ENERGYON bit
 * to be set and will cause the EDPD mode to be exited.
 */
static int internal_read_status(struct phy_device *phydev)
{
	int err = genphy_read_status(phydev);
	if(phydev->speed == SPEED_10){
		init_internal_phy_10B(phydev);
	}
	if(phydev->speed == SPEED_100){
		init_internal_phy_100B(phydev);
	}
	if (!phydev->link) {
		/* Disable EDPD to wake up PHY */
		int rc = phy_read(phydev, MII_INTERNAL_CTRL_STATUS);
		if (rc < 0)
			return rc;

		rc = phy_write(phydev, MII_INTERNAL_CTRL_STATUS,
			       rc & ~MII_INTERNAL_EDPWRDOWN);
		if (rc < 0)
			return rc;

		/* Sleep 64 ms to allow ~5 link test pulses to be sent */
		msleep(64);

		/* Re-enable EDPD */
		rc = phy_read(phydev, MII_INTERNAL_CTRL_STATUS);
		if (rc < 0)
			return rc;

		rc = phy_write(phydev, MII_INTERNAL_CTRL_STATUS,
			       rc | MII_INTERNAL_EDPWRDOWN);
		if (rc < 0)
			return rc;
	}

	return err;
}


int amlogic_phy_config_aneg(struct phy_device *phydev)
{
	int err;
	err = genphy_config_aneg(phydev);
	if(err < 0)
		return err;
	
	return 0;
}

static struct phy_driver amlogic_phy_driver[] = {
 {
	.phy_id		= 0x79898963,
	.phy_id_mask	= 0xffffffff,
	.name		= "AMLOGIC internal phy",

	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause
				| SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_INTERRUPT | PHY_HAS_MAGICANEG,

	/* basic functions */
	.config_aneg	= &amlogic_phy_config_aneg,
	.read_status	= &internal_read_status,
	.config_init	= &amlogic_phy_config_init,

	/* IRQ related */
	.ack_interrupt	= &amlogic_phy_ack_interrupt,
	.config_intr	= &amlogic_phy_config_intr,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,

	.driver		= { .owner = THIS_MODULE, }
} };

static int __init amlogic_init(void)
{
	return phy_drivers_register(amlogic_phy_driver,
		ARRAY_SIZE(amlogic_phy_driver));
}

static void __exit amlogic_exit(void)
{
	return phy_drivers_unregister(amlogic_phy_driver,
		ARRAY_SIZE(amlogic_phy_driver));
}

MODULE_DESCRIPTION("Amlogic PHY driver");
MODULE_AUTHOR("Baoqi wang");
MODULE_LICENSE("GPL");

module_init(amlogic_init);
module_exit(amlogic_exit);

static struct mdio_device_id __maybe_unused amlogic_tbl[] = {
	{ 0x79898963, 0xffffffff },
	{ }
};

MODULE_DEVICE_TABLE(mdio, amlogic_tbl);
