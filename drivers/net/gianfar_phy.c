/* 
 * drivers/net/gianfar_phy.c
 *
 * Gianfar Ethernet Driver -- PHY handling
 * Driver for FEC on MPC8540 and TSEC on MPC8540/MPC8560
 * Based on 8260_io/fcc_enet.c
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala (kumar.gala@freescale.com)
 *
 * Copyright (c) 2002-2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/crc32.h>
#include <linux/mii.h>

#include "gianfar.h"
#include "gianfar_phy.h"

static void config_genmii_advert(struct gfar_mii_info *mii_info);
static void genmii_setup_forced(struct gfar_mii_info *mii_info);
static void genmii_restart_aneg(struct gfar_mii_info *mii_info);
static int gbit_config_aneg(struct gfar_mii_info *mii_info);
static int genmii_config_aneg(struct gfar_mii_info *mii_info);
static int genmii_update_link(struct gfar_mii_info *mii_info);
static int genmii_read_status(struct gfar_mii_info *mii_info);
u16 phy_read(struct gfar_mii_info *mii_info, u16 regnum);
void phy_write(struct gfar_mii_info *mii_info, u16 regnum, u16 val);

/* Write value to the PHY for this device to the register at regnum, */
/* waiting until the write is done before it returns.  All PHY */
/* configuration has to be done through the TSEC1 MIIM regs */
void write_phy_reg(struct net_device *dev, int mii_id, int regnum, int value)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar *regbase = priv->phyregs;

	/* Set the PHY address and the register address we want to write */
	gfar_write(&regbase->miimadd, (mii_id << 8) | regnum);

	/* Write out the value we want */
	gfar_write(&regbase->miimcon, value);

	/* Wait for the transaction to finish */
	while (gfar_read(&regbase->miimind) & MIIMIND_BUSY)
		cpu_relax();
}

/* Reads from register regnum in the PHY for device dev, */
/* returning the value.  Clears miimcom first.  All PHY */
/* configuration has to be done through the TSEC1 MIIM regs */
int read_phy_reg(struct net_device *dev, int mii_id, int regnum)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar *regbase = priv->phyregs;
	u16 value;

	/* Set the PHY address and the register address we want to read */
	gfar_write(&regbase->miimadd, (mii_id << 8) | regnum);

	/* Clear miimcom, and then initiate a read */
	gfar_write(&regbase->miimcom, 0);
	gfar_write(&regbase->miimcom, MII_READ_COMMAND);

	/* Wait for the transaction to finish */
	while (gfar_read(&regbase->miimind) & (MIIMIND_NOTVALID | MIIMIND_BUSY))
		cpu_relax();

	/* Grab the value of the register from miimstat */
	value = gfar_read(&regbase->miimstat);

	return value;
}

void mii_clear_phy_interrupt(struct gfar_mii_info *mii_info)
{
	if(mii_info->phyinfo->ack_interrupt)
		mii_info->phyinfo->ack_interrupt(mii_info);
}


void mii_configure_phy_interrupt(struct gfar_mii_info *mii_info, u32 interrupts)
{
	mii_info->interrupts = interrupts;
	if(mii_info->phyinfo->config_intr)
		mii_info->phyinfo->config_intr(mii_info);
}


/* Writes MII_ADVERTISE with the appropriate values, after
 * sanitizing advertise to make sure only supported features
 * are advertised 
 */
static void config_genmii_advert(struct gfar_mii_info *mii_info)
{
	u32 advertise;
	u16 adv;

	/* Only allow advertising what this PHY supports */
	mii_info->advertising &= mii_info->phyinfo->features;
	advertise = mii_info->advertising;

	/* Setup standard advertisement */
	adv = phy_read(mii_info, MII_ADVERTISE);
	adv &= ~(ADVERTISE_ALL | ADVERTISE_100BASE4);
	if (advertise & ADVERTISED_10baseT_Half)
		adv |= ADVERTISE_10HALF;
	if (advertise & ADVERTISED_10baseT_Full)
		adv |= ADVERTISE_10FULL;
	if (advertise & ADVERTISED_100baseT_Half)
		adv |= ADVERTISE_100HALF;
	if (advertise & ADVERTISED_100baseT_Full)
		adv |= ADVERTISE_100FULL;
	phy_write(mii_info, MII_ADVERTISE, adv);
}

static void genmii_setup_forced(struct gfar_mii_info *mii_info)
{
	u16 ctrl;
	u32 features = mii_info->phyinfo->features;
	
	ctrl = phy_read(mii_info, MII_BMCR);

	ctrl &= ~(BMCR_FULLDPLX|BMCR_SPEED100|BMCR_SPEED1000|BMCR_ANENABLE);
	ctrl |= BMCR_RESET;

	switch(mii_info->speed) {
		case SPEED_1000:
			if(features & (SUPPORTED_1000baseT_Half
						| SUPPORTED_1000baseT_Full)) {
				ctrl |= BMCR_SPEED1000;
				break;
			}
			mii_info->speed = SPEED_100;
		case SPEED_100:
			if (features & (SUPPORTED_100baseT_Half
						| SUPPORTED_100baseT_Full)) {
				ctrl |= BMCR_SPEED100;
				break;
			}
			mii_info->speed = SPEED_10;
		case SPEED_10:
			if (features & (SUPPORTED_10baseT_Half
						| SUPPORTED_10baseT_Full))
				break;
		default: /* Unsupported speed! */
			printk(KERN_ERR "%s: Bad speed!\n", 
					mii_info->dev->name);
			break;
	}

	phy_write(mii_info, MII_BMCR, ctrl);
}


/* Enable and Restart Autonegotiation */
static void genmii_restart_aneg(struct gfar_mii_info *mii_info)
{
	u16 ctl;

	ctl = phy_read(mii_info, MII_BMCR);
	ctl |= (BMCR_ANENABLE | BMCR_ANRESTART);
	phy_write(mii_info, MII_BMCR, ctl);
}


static int gbit_config_aneg(struct gfar_mii_info *mii_info)
{
	u16 adv;
	u32 advertise;

	if(mii_info->autoneg) {
		/* Configure the ADVERTISE register */
		config_genmii_advert(mii_info);
		advertise = mii_info->advertising;

		adv = phy_read(mii_info, MII_1000BASETCONTROL);
		adv &= ~(MII_1000BASETCONTROL_FULLDUPLEXCAP |
				MII_1000BASETCONTROL_HALFDUPLEXCAP);
		if (advertise & SUPPORTED_1000baseT_Half)
			adv |= MII_1000BASETCONTROL_HALFDUPLEXCAP;
		if (advertise & SUPPORTED_1000baseT_Full)
			adv |= MII_1000BASETCONTROL_FULLDUPLEXCAP;
		phy_write(mii_info, MII_1000BASETCONTROL, adv);

		/* Start/Restart aneg */
		genmii_restart_aneg(mii_info);
	} else
		genmii_setup_forced(mii_info);

	return 0;
}

static int marvell_config_aneg(struct gfar_mii_info *mii_info)
{
	/* The Marvell PHY has an errata which requires
	 * that certain registers get written in order
	 * to restart autonegotiation */
	phy_write(mii_info, MII_BMCR, BMCR_RESET);

	phy_write(mii_info, 0x1d, 0x1f);
	phy_write(mii_info, 0x1e, 0x200c);
	phy_write(mii_info, 0x1d, 0x5);
	phy_write(mii_info, 0x1e, 0);
	phy_write(mii_info, 0x1e, 0x100);

	gbit_config_aneg(mii_info);

	return 0;
}
static int genmii_config_aneg(struct gfar_mii_info *mii_info)
{
	if (mii_info->autoneg) {
		config_genmii_advert(mii_info);
		genmii_restart_aneg(mii_info);
	} else
		genmii_setup_forced(mii_info);

	return 0;
}


static int genmii_update_link(struct gfar_mii_info *mii_info)
{
	u16 status;

	/* Do a fake read */
	phy_read(mii_info, MII_BMSR);

	/* Read link and autonegotiation status */
	status = phy_read(mii_info, MII_BMSR);
	if ((status & BMSR_LSTATUS) == 0)
		mii_info->link = 0;
	else
		mii_info->link = 1;

	/* If we are autonegotiating, and not done, 
	 * return an error */
	if (mii_info->autoneg && !(status & BMSR_ANEGCOMPLETE))
		return -EAGAIN;

	return 0;
}

static int genmii_read_status(struct gfar_mii_info *mii_info)
{
	u16 status;
	int err;

	/* Update the link, but return if there
	 * was an error */
	err = genmii_update_link(mii_info);
	if (err)
		return err;

	if (mii_info->autoneg) {
		status = phy_read(mii_info, MII_LPA);

		if (status & (LPA_10FULL | LPA_100FULL))
			mii_info->duplex = DUPLEX_FULL;
		else
			mii_info->duplex = DUPLEX_HALF;
		if (status & (LPA_100FULL | LPA_100HALF))
			mii_info->speed = SPEED_100;
		else
			mii_info->speed = SPEED_10;
		mii_info->pause = 0;
	}
	/* On non-aneg, we assume what we put in BMCR is the speed,
	 * though magic-aneg shouldn't prevent this case from occurring
	 */

	return 0;
}
static int marvell_read_status(struct gfar_mii_info *mii_info)
{
	u16 status;
	int err;

	/* Update the link, but return if there
	 * was an error */
	err = genmii_update_link(mii_info);
	if (err)
		return err;

	/* If the link is up, read the speed and duplex */
	/* If we aren't autonegotiating, assume speeds 
	 * are as set */
	if (mii_info->autoneg && mii_info->link) {
		int speed;
		status = phy_read(mii_info, MII_M1011_PHY_SPEC_STATUS);

#if 0
		/* If speed and duplex aren't resolved,
		 * return an error.  Isn't this handled
		 * by checking aneg?
		 */
		if ((status & MII_M1011_PHY_SPEC_STATUS_RESOLVED) == 0)
			return -EAGAIN;
#endif

		/* Get the duplexity */
		if (status & MII_M1011_PHY_SPEC_STATUS_FULLDUPLEX)
			mii_info->duplex = DUPLEX_FULL;
		else
			mii_info->duplex = DUPLEX_HALF;

		/* Get the speed */
		speed = status & MII_M1011_PHY_SPEC_STATUS_SPD_MASK;
		switch(speed) {
			case MII_M1011_PHY_SPEC_STATUS_1000:
				mii_info->speed = SPEED_1000;
				break;
			case MII_M1011_PHY_SPEC_STATUS_100:
				mii_info->speed = SPEED_100;
				break;
			default:
				mii_info->speed = SPEED_10;
				break;
		}
		mii_info->pause = 0;
	}

	return 0;
}


static int cis820x_read_status(struct gfar_mii_info *mii_info)
{
	u16 status;
	int err;

	/* Update the link, but return if there
	 * was an error */
	err = genmii_update_link(mii_info);
	if (err)
		return err;

	/* If the link is up, read the speed and duplex */
	/* If we aren't autonegotiating, assume speeds 
	 * are as set */
	if (mii_info->autoneg && mii_info->link) {
		int speed;

		status = phy_read(mii_info, MII_CIS8201_AUX_CONSTAT);
		if (status & MII_CIS8201_AUXCONSTAT_DUPLEX)
			mii_info->duplex = DUPLEX_FULL;
		else
			mii_info->duplex = DUPLEX_HALF;

		speed = status & MII_CIS8201_AUXCONSTAT_SPEED;

		switch (speed) {
		case MII_CIS8201_AUXCONSTAT_GBIT:
			mii_info->speed = SPEED_1000;
			break;
		case MII_CIS8201_AUXCONSTAT_100:
			mii_info->speed = SPEED_100;
			break;
		default:
			mii_info->speed = SPEED_10;
			break;
		}
	}

	return 0;
}

static int marvell_ack_interrupt(struct gfar_mii_info *mii_info)
{
	/* Clear the interrupts by reading the reg */
	phy_read(mii_info, MII_M1011_IEVENT);

	return 0;
}

static int marvell_config_intr(struct gfar_mii_info *mii_info)
{
	if(mii_info->interrupts == MII_INTERRUPT_ENABLED)
		phy_write(mii_info, MII_M1011_IMASK, MII_M1011_IMASK_INIT);
	else
		phy_write(mii_info, MII_M1011_IMASK, MII_M1011_IMASK_CLEAR);

	return 0;
}

static int cis820x_init(struct gfar_mii_info *mii_info)
{
	phy_write(mii_info, MII_CIS8201_AUX_CONSTAT, 
			MII_CIS8201_AUXCONSTAT_INIT);
	phy_write(mii_info, MII_CIS8201_EXT_CON1,
			MII_CIS8201_EXTCON1_INIT);

	return 0;
}

static int cis820x_ack_interrupt(struct gfar_mii_info *mii_info)
{
	phy_read(mii_info, MII_CIS8201_ISTAT);

	return 0;
}

static int cis820x_config_intr(struct gfar_mii_info *mii_info)
{
	if(mii_info->interrupts == MII_INTERRUPT_ENABLED)
		phy_write(mii_info, MII_CIS8201_IMASK, MII_CIS8201_IMASK_MASK);
	else
		phy_write(mii_info, MII_CIS8201_IMASK, 0);

	return 0;
}

#define DM9161_DELAY 10

static int dm9161_read_status(struct gfar_mii_info *mii_info)
{
	u16 status;
	int err;

	/* Update the link, but return if there
	 * was an error */
	err = genmii_update_link(mii_info);
	if (err)
		return err;

	/* If the link is up, read the speed and duplex */
	/* If we aren't autonegotiating, assume speeds 
	 * are as set */
	if (mii_info->autoneg && mii_info->link) {
		status = phy_read(mii_info, MII_DM9161_SCSR);
		if (status & (MII_DM9161_SCSR_100F | MII_DM9161_SCSR_100H))
			mii_info->speed = SPEED_100;
		else
			mii_info->speed = SPEED_10;

		if (status & (MII_DM9161_SCSR_100F | MII_DM9161_SCSR_10F))
			mii_info->duplex = DUPLEX_FULL;
		else
			mii_info->duplex = DUPLEX_HALF;
	}

	return 0;
}


static int dm9161_config_aneg(struct gfar_mii_info *mii_info)
{
	struct dm9161_private *priv = mii_info->priv;

	if(0 == priv->resetdone)
		return -EAGAIN;

	return 0;
}

static void dm9161_timer(unsigned long data)
{
	struct gfar_mii_info *mii_info = (struct gfar_mii_info *)data;
	struct dm9161_private *priv = mii_info->priv;
	u16 status = phy_read(mii_info, MII_BMSR);

	if (status & BMSR_ANEGCOMPLETE) {
		priv->resetdone = 1;
	} else
		mod_timer(&priv->timer, jiffies + DM9161_DELAY * HZ);
}

static int dm9161_init(struct gfar_mii_info *mii_info)
{
	struct dm9161_private *priv;

	/* Allocate the private data structure */
	priv = kmalloc(sizeof(struct dm9161_private), GFP_KERNEL);

	if (NULL == priv)
		return -ENOMEM;

	mii_info->priv = priv;

	/* Reset is not done yet */
	priv->resetdone = 0;

	/* Isolate the PHY */
	phy_write(mii_info, MII_BMCR, BMCR_ISOLATE);

	/* Do not bypass the scrambler/descrambler */
	phy_write(mii_info, MII_DM9161_SCR, MII_DM9161_SCR_INIT);

	/* Clear 10BTCSR to default */
	phy_write(mii_info, MII_DM9161_10BTCSR, MII_DM9161_10BTCSR_INIT);

	/* Reconnect the PHY, and enable Autonegotiation */
	phy_write(mii_info, MII_BMCR, BMCR_ANENABLE);

	/* Start a timer for DM9161_DELAY seconds to wait
	 * for the PHY to be ready */
	init_timer(&priv->timer);
	priv->timer.function = &dm9161_timer;
	priv->timer.data = (unsigned long) mii_info;
	mod_timer(&priv->timer, jiffies + DM9161_DELAY * HZ);

	return 0;
}

static void dm9161_close(struct gfar_mii_info *mii_info)
{
	struct dm9161_private *priv = mii_info->priv;

	del_timer_sync(&priv->timer);
	kfree(priv);
}

#if 0
static int dm9161_ack_interrupt(struct gfar_mii_info *mii_info)
{
	phy_read(mii_info, MII_DM9161_INTR);

	return 0;
}
#endif

/* Cicada 820x */
static struct phy_info phy_info_cis820x = {
	0x000fc440,
	"Cicada Cis8204",
	0x000fffc0,
	.features	= MII_GBIT_FEATURES,
	.init		= &cis820x_init,
	.config_aneg	= &gbit_config_aneg,
	.read_status	= &cis820x_read_status,
	.ack_interrupt	= &cis820x_ack_interrupt,
	.config_intr	= &cis820x_config_intr,
};

static struct phy_info phy_info_dm9161 = {
	.phy_id		= 0x0181b880,
	.name		= "Davicom DM9161E",
	.phy_id_mask	= 0x0ffffff0,
	.init		= dm9161_init,
	.config_aneg	= dm9161_config_aneg,
	.read_status	= dm9161_read_status,
	.close		= dm9161_close,
};

static struct phy_info phy_info_marvell = {
	.phy_id		= 0x01410c00,
	.phy_id_mask	= 0xffffff00,
	.name		= "Marvell 88E1101/88E1111",
	.features	= MII_GBIT_FEATURES,
	.config_aneg	= &marvell_config_aneg,
	.read_status	= &marvell_read_status,
	.ack_interrupt	= &marvell_ack_interrupt,
	.config_intr	= &marvell_config_intr,
};

static struct phy_info phy_info_genmii= {
	.phy_id		= 0x00000000,
	.phy_id_mask	= 0x00000000,
	.name		= "Generic MII",
	.features	= MII_BASIC_FEATURES,
	.config_aneg	= genmii_config_aneg,
	.read_status	= genmii_read_status,
};

static struct phy_info *phy_info[] = {
	&phy_info_cis820x,
	&phy_info_marvell,
	&phy_info_dm9161,
	&phy_info_genmii,
	NULL
};

u16 phy_read(struct gfar_mii_info *mii_info, u16 regnum)
{
	u16 retval;
	unsigned long flags;

	spin_lock_irqsave(&mii_info->mdio_lock, flags);
	retval = mii_info->mdio_read(mii_info->dev, mii_info->mii_id, regnum);
	spin_unlock_irqrestore(&mii_info->mdio_lock, flags);

	return retval;
}

void phy_write(struct gfar_mii_info *mii_info, u16 regnum, u16 val)
{
	unsigned long flags;

	spin_lock_irqsave(&mii_info->mdio_lock, flags);
	mii_info->mdio_write(mii_info->dev, 
			mii_info->mii_id, 
			regnum, val);
	spin_unlock_irqrestore(&mii_info->mdio_lock, flags);
}

/* Use the PHY ID registers to determine what type of PHY is attached
 * to device dev.  return a struct phy_info structure describing that PHY
 */
struct phy_info * get_phy_info(struct gfar_mii_info *mii_info)
{
	u16 phy_reg;
	u32 phy_ID;
	int i;
	struct phy_info *theInfo = NULL;
	struct net_device *dev = mii_info->dev;

	/* Grab the bits from PHYIR1, and put them in the upper half */
	phy_reg = phy_read(mii_info, MII_PHYSID1);
	phy_ID = (phy_reg & 0xffff) << 16;

	/* Grab the bits from PHYIR2, and put them in the lower half */
	phy_reg = phy_read(mii_info, MII_PHYSID2);
	phy_ID |= (phy_reg & 0xffff);

	/* loop through all the known PHY types, and find one that */
	/* matches the ID we read from the PHY. */
	for (i = 0; phy_info[i]; i++)
		if (phy_info[i]->phy_id == 
				(phy_ID & phy_info[i]->phy_id_mask)) {
			theInfo = phy_info[i];
			break;
		}

	/* This shouldn't happen, as we have generic PHY support */
	if (theInfo == NULL) {
		printk("%s: PHY id %x is not supported!\n", dev->name, phy_ID);
		return NULL;
	} else {
		printk("%s: PHY is %s (%x)\n", dev->name, theInfo->name,
		       phy_ID);
	}

	return theInfo;
}
