/*
 * Copyright (C) Freescale Semicondutor, Inc. 2006. All rights reserved.
 *
 * Author: Shlomi Gridish <gridish@freescale.com>
 *
 * Description:
 * UCC GETH Driver -- PHY handling
 *
 * Changelog:
 * Jun 28, 2006 Li Yang <LeoLi@freescale.com>
 * - Rearrange code and style fixes
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

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
#include <linux/module.h>
#include <linux/version.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/ethtool.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "ucc_geth.h"
#include "ucc_geth_phy.h"
#include <platforms/83xx/mpc8360e_pb.h>

#define ugphy_printk(level, format, arg...)  \
        printk(level format "\n", ## arg)

#define ugphy_dbg(format, arg...)            \
        ugphy_printk(KERN_DEBUG, format , ## arg)
#define ugphy_err(format, arg...)            \
        ugphy_printk(KERN_ERR, format , ## arg)
#define ugphy_info(format, arg...)           \
        ugphy_printk(KERN_INFO, format , ## arg)
#define ugphy_warn(format, arg...)           \
        ugphy_printk(KERN_WARNING, format , ## arg)

#ifdef UGETH_VERBOSE_DEBUG
#define ugphy_vdbg ugphy_dbg
#else
#define ugphy_vdbg(fmt, args...) do { } while (0)
#endif				/* UGETH_VERBOSE_DEBUG */

static void config_genmii_advert(struct ugeth_mii_info *mii_info);
static void genmii_setup_forced(struct ugeth_mii_info *mii_info);
static void genmii_restart_aneg(struct ugeth_mii_info *mii_info);
static int gbit_config_aneg(struct ugeth_mii_info *mii_info);
static int genmii_config_aneg(struct ugeth_mii_info *mii_info);
static int genmii_update_link(struct ugeth_mii_info *mii_info);
static int genmii_read_status(struct ugeth_mii_info *mii_info);
u16 phy_read(struct ugeth_mii_info *mii_info, u16 regnum);
void phy_write(struct ugeth_mii_info *mii_info, u16 regnum, u16 val);

static u8 *bcsr_regs = NULL;

/* Write value to the PHY for this device to the register at regnum, */
/* waiting until the write is done before it returns.  All PHY */
/* configuration has to be done through the TSEC1 MIIM regs */
void write_phy_reg(struct net_device *dev, int mii_id, int regnum, int value)
{
	ucc_geth_private_t *ugeth = netdev_priv(dev);
	ucc_mii_mng_t *mii_regs;
	enet_tbi_mii_reg_e mii_reg = (enet_tbi_mii_reg_e) regnum;
	u32 tmp_reg;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	spin_lock_irq(&ugeth->lock);

	mii_regs = ugeth->mii_info->mii_regs;

	/* Set this UCC to be the master of the MII managment */
	ucc_set_qe_mux_mii_mng(ugeth->ug_info->uf_info.ucc_num);

	/* Stop the MII management read cycle */
	out_be32(&mii_regs->miimcom, 0);
	/* Setting up the MII Mangement Address Register */
	tmp_reg = ((u32) mii_id << MIIMADD_PHY_ADDRESS_SHIFT) | mii_reg;
	out_be32(&mii_regs->miimadd, tmp_reg);

	/* Setting up the MII Mangement Control Register with the value */
	out_be32(&mii_regs->miimcon, (u32) value);

	/* Wait till MII management write is complete */
	while ((in_be32(&mii_regs->miimind)) & MIIMIND_BUSY)
		cpu_relax();

	spin_unlock_irq(&ugeth->lock);

	udelay(10000);
}

/* Reads from register regnum in the PHY for device dev, */
/* returning the value.  Clears miimcom first.  All PHY */
/* configuration has to be done through the TSEC1 MIIM regs */
int read_phy_reg(struct net_device *dev, int mii_id, int regnum)
{
	ucc_geth_private_t *ugeth = netdev_priv(dev);
	ucc_mii_mng_t *mii_regs;
	enet_tbi_mii_reg_e mii_reg = (enet_tbi_mii_reg_e) regnum;
	u32 tmp_reg;
	u16 value;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	spin_lock_irq(&ugeth->lock);

	mii_regs = ugeth->mii_info->mii_regs;

	/* Setting up the MII Mangement Address Register */
	tmp_reg = ((u32) mii_id << MIIMADD_PHY_ADDRESS_SHIFT) | mii_reg;
	out_be32(&mii_regs->miimadd, tmp_reg);

	/* Perform an MII management read cycle */
	out_be32(&mii_regs->miimcom, MIIMCOM_READ_CYCLE);

	/* Wait till MII management write is complete */
	while ((in_be32(&mii_regs->miimind)) & MIIMIND_BUSY)
		cpu_relax();

	udelay(10000);

	/* Read MII management status  */
	value = (u16) in_be32(&mii_regs->miimstat);
	out_be32(&mii_regs->miimcom, 0);
	if (value == 0xffff)
		ugphy_warn("read wrong value : mii_id %d,mii_reg %d, base %08x",
			   mii_id, mii_reg, (u32) & (mii_regs->miimcfg));

	spin_unlock_irq(&ugeth->lock);

	return (value);
}

void mii_clear_phy_interrupt(struct ugeth_mii_info *mii_info)
{
	ugphy_vdbg("%s: IN", __FUNCTION__);

	if (mii_info->phyinfo->ack_interrupt)
		mii_info->phyinfo->ack_interrupt(mii_info);
}

void mii_configure_phy_interrupt(struct ugeth_mii_info *mii_info,
				 u32 interrupts)
{
	ugphy_vdbg("%s: IN", __FUNCTION__);

	mii_info->interrupts = interrupts;
	if (mii_info->phyinfo->config_intr)
		mii_info->phyinfo->config_intr(mii_info);
}

/* Writes MII_ADVERTISE with the appropriate values, after
 * sanitizing advertise to make sure only supported features
 * are advertised
 */
static void config_genmii_advert(struct ugeth_mii_info *mii_info)
{
	u32 advertise;
	u16 adv;

	ugphy_vdbg("%s: IN", __FUNCTION__);

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

static void genmii_setup_forced(struct ugeth_mii_info *mii_info)
{
	u16 ctrl;
	u32 features = mii_info->phyinfo->features;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	ctrl = phy_read(mii_info, MII_BMCR);

	ctrl &=
	    ~(BMCR_FULLDPLX | BMCR_SPEED100 | BMCR_SPEED1000 | BMCR_ANENABLE);
	ctrl |= BMCR_RESET;

	switch (mii_info->speed) {
	case SPEED_1000:
		if (features & (SUPPORTED_1000baseT_Half
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
	default:		/* Unsupported speed! */
		ugphy_err("%s: Bad speed!", mii_info->dev->name);
		break;
	}

	phy_write(mii_info, MII_BMCR, ctrl);
}

/* Enable and Restart Autonegotiation */
static void genmii_restart_aneg(struct ugeth_mii_info *mii_info)
{
	u16 ctl;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	ctl = phy_read(mii_info, MII_BMCR);
	ctl |= (BMCR_ANENABLE | BMCR_ANRESTART);
	phy_write(mii_info, MII_BMCR, ctl);
}

static int gbit_config_aneg(struct ugeth_mii_info *mii_info)
{
	u16 adv;
	u32 advertise;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	if (mii_info->autoneg) {
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

static int genmii_config_aneg(struct ugeth_mii_info *mii_info)
{
	ugphy_vdbg("%s: IN", __FUNCTION__);

	if (mii_info->autoneg) {
		config_genmii_advert(mii_info);
		genmii_restart_aneg(mii_info);
	} else
		genmii_setup_forced(mii_info);

	return 0;
}

static int genmii_update_link(struct ugeth_mii_info *mii_info)
{
	u16 status;

	ugphy_vdbg("%s: IN", __FUNCTION__);

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

static int genmii_read_status(struct ugeth_mii_info *mii_info)
{
	u16 status;
	int err;

	ugphy_vdbg("%s: IN", __FUNCTION__);

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

static int marvell_init(struct ugeth_mii_info *mii_info)
{
	ugphy_vdbg("%s: IN", __FUNCTION__);

	phy_write(mii_info, 0x14, 0x0cd2);
	phy_write(mii_info, MII_BMCR,
		  phy_read(mii_info, MII_BMCR) | BMCR_RESET);
	msleep(4000);

	return 0;
}

static int marvell_config_aneg(struct ugeth_mii_info *mii_info)
{
	ugphy_vdbg("%s: IN", __FUNCTION__);

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

static int marvell_read_status(struct ugeth_mii_info *mii_info)
{
	u16 status;
	int err;

	ugphy_vdbg("%s: IN", __FUNCTION__);

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

		/* Get the duplexity */
		if (status & MII_M1011_PHY_SPEC_STATUS_FULLDUPLEX)
			mii_info->duplex = DUPLEX_FULL;
		else
			mii_info->duplex = DUPLEX_HALF;

		/* Get the speed */
		speed = status & MII_M1011_PHY_SPEC_STATUS_SPD_MASK;
		switch (speed) {
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

static int marvell_ack_interrupt(struct ugeth_mii_info *mii_info)
{
	ugphy_vdbg("%s: IN", __FUNCTION__);

	/* Clear the interrupts by reading the reg */
	phy_read(mii_info, MII_M1011_IEVENT);

	return 0;
}

static int marvell_config_intr(struct ugeth_mii_info *mii_info)
{
	ugphy_vdbg("%s: IN", __FUNCTION__);

	if (mii_info->interrupts == MII_INTERRUPT_ENABLED)
		phy_write(mii_info, MII_M1011_IMASK, MII_M1011_IMASK_INIT);
	else
		phy_write(mii_info, MII_M1011_IMASK, MII_M1011_IMASK_CLEAR);

	return 0;
}

static int cis820x_init(struct ugeth_mii_info *mii_info)
{
	ugphy_vdbg("%s: IN", __FUNCTION__);

	phy_write(mii_info, MII_CIS8201_AUX_CONSTAT,
		  MII_CIS8201_AUXCONSTAT_INIT);
	phy_write(mii_info, MII_CIS8201_EXT_CON1, MII_CIS8201_EXTCON1_INIT);

	return 0;
}

static int cis820x_read_status(struct ugeth_mii_info *mii_info)
{
	u16 status;
	int err;

	ugphy_vdbg("%s: IN", __FUNCTION__);

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

static int cis820x_ack_interrupt(struct ugeth_mii_info *mii_info)
{
	ugphy_vdbg("%s: IN", __FUNCTION__);

	phy_read(mii_info, MII_CIS8201_ISTAT);

	return 0;
}

static int cis820x_config_intr(struct ugeth_mii_info *mii_info)
{
	ugphy_vdbg("%s: IN", __FUNCTION__);

	if (mii_info->interrupts == MII_INTERRUPT_ENABLED)
		phy_write(mii_info, MII_CIS8201_IMASK, MII_CIS8201_IMASK_MASK);
	else
		phy_write(mii_info, MII_CIS8201_IMASK, 0);

	return 0;
}

#define DM9161_DELAY 10

static int dm9161_read_status(struct ugeth_mii_info *mii_info)
{
	u16 status;
	int err;

	ugphy_vdbg("%s: IN", __FUNCTION__);

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

static int dm9161_config_aneg(struct ugeth_mii_info *mii_info)
{
	struct dm9161_private *priv = mii_info->priv;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	if (0 == priv->resetdone)
		return -EAGAIN;

	return 0;
}

static void dm9161_timer(unsigned long data)
{
	struct ugeth_mii_info *mii_info = (struct ugeth_mii_info *)data;
	struct dm9161_private *priv = mii_info->priv;
	u16 status = phy_read(mii_info, MII_BMSR);

	ugphy_vdbg("%s: IN", __FUNCTION__);

	if (status & BMSR_ANEGCOMPLETE) {
		priv->resetdone = 1;
	} else
		mod_timer(&priv->timer, jiffies + DM9161_DELAY * HZ);
}

static int dm9161_init(struct ugeth_mii_info *mii_info)
{
	struct dm9161_private *priv;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	/* Allocate the private data structure */
	priv = kmalloc(sizeof(struct dm9161_private), GFP_KERNEL);

	if (NULL == priv)
		return -ENOMEM;

	mii_info->priv = priv;

	/* Reset is not done yet */
	priv->resetdone = 0;

	phy_write(mii_info, MII_BMCR,
		  phy_read(mii_info, MII_BMCR) | BMCR_RESET);

	phy_write(mii_info, MII_BMCR,
		  phy_read(mii_info, MII_BMCR) & ~BMCR_ISOLATE);

	config_genmii_advert(mii_info);
	/* Start/Restart aneg */
	genmii_config_aneg(mii_info);

	/* Start a timer for DM9161_DELAY seconds to wait
	 * for the PHY to be ready */
	init_timer(&priv->timer);
	priv->timer.function = &dm9161_timer;
	priv->timer.data = (unsigned long)mii_info;
	mod_timer(&priv->timer, jiffies + DM9161_DELAY * HZ);

	return 0;
}

static void dm9161_close(struct ugeth_mii_info *mii_info)
{
	struct dm9161_private *priv = mii_info->priv;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	del_timer_sync(&priv->timer);
	kfree(priv);
}

static int dm9161_ack_interrupt(struct ugeth_mii_info *mii_info)
{
/* FIXME: This lines are for BUG fixing in the mpc8325.
Remove this from here when it's fixed */
	if (bcsr_regs == NULL)
		bcsr_regs = (u8 *) ioremap(BCSR_PHYS_ADDR, BCSR_SIZE);
	bcsr_regs[14] |= 0x40;
	ugphy_vdbg("%s: IN", __FUNCTION__);

	/* Clear the interrupts by reading the reg */
	phy_read(mii_info, MII_DM9161_INTR);


	return 0;
}

static int dm9161_config_intr(struct ugeth_mii_info *mii_info)
{
/* FIXME: This lines are for BUG fixing in the mpc8325.
Remove this from here when it's fixed */
	if (bcsr_regs == NULL) {
		bcsr_regs = (u8 *) ioremap(BCSR_PHYS_ADDR, BCSR_SIZE);
		bcsr_regs[14] &= ~0x40;
	}
	ugphy_vdbg("%s: IN", __FUNCTION__);

	if (mii_info->interrupts == MII_INTERRUPT_ENABLED)
		phy_write(mii_info, MII_DM9161_INTR, MII_DM9161_INTR_INIT);
	else
		phy_write(mii_info, MII_DM9161_INTR, MII_DM9161_INTR_STOP);

	return 0;
}

/* Cicada 820x */
static struct phy_info phy_info_cis820x = {
	.phy_id = 0x000fc440,
	.name = "Cicada Cis8204",
	.phy_id_mask = 0x000fffc0,
	.features = MII_GBIT_FEATURES,
	.init = &cis820x_init,
	.config_aneg = &gbit_config_aneg,
	.read_status = &cis820x_read_status,
	.ack_interrupt = &cis820x_ack_interrupt,
	.config_intr = &cis820x_config_intr,
};

static struct phy_info phy_info_dm9161 = {
	.phy_id = 0x0181b880,
	.phy_id_mask = 0x0ffffff0,
	.name = "Davicom DM9161E",
	.init = dm9161_init,
	.config_aneg = dm9161_config_aneg,
	.read_status = dm9161_read_status,
	.close = dm9161_close,
};

static struct phy_info phy_info_dm9161a = {
	.phy_id = 0x0181b8a0,
	.phy_id_mask = 0x0ffffff0,
	.name = "Davicom DM9161A",
	.features = MII_BASIC_FEATURES,
	.init = dm9161_init,
	.config_aneg = dm9161_config_aneg,
	.read_status = dm9161_read_status,
	.ack_interrupt = dm9161_ack_interrupt,
	.config_intr = dm9161_config_intr,
	.close = dm9161_close,
};

static struct phy_info phy_info_marvell = {
	.phy_id = 0x01410c00,
	.phy_id_mask = 0xffffff00,
	.name = "Marvell 88E11x1",
	.features = MII_GBIT_FEATURES,
	.init = &marvell_init,
	.config_aneg = &marvell_config_aneg,
	.read_status = &marvell_read_status,
	.ack_interrupt = &marvell_ack_interrupt,
	.config_intr = &marvell_config_intr,
};

static struct phy_info phy_info_genmii = {
	.phy_id = 0x00000000,
	.phy_id_mask = 0x00000000,
	.name = "Generic MII",
	.features = MII_BASIC_FEATURES,
	.config_aneg = genmii_config_aneg,
	.read_status = genmii_read_status,
};

static struct phy_info *phy_info[] = {
	&phy_info_cis820x,
	&phy_info_marvell,
	&phy_info_dm9161,
	&phy_info_dm9161a,
	&phy_info_genmii,
	NULL
};

u16 phy_read(struct ugeth_mii_info *mii_info, u16 regnum)
{
	u16 retval;
	unsigned long flags;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	spin_lock_irqsave(&mii_info->mdio_lock, flags);
	retval = mii_info->mdio_read(mii_info->dev, mii_info->mii_id, regnum);
	spin_unlock_irqrestore(&mii_info->mdio_lock, flags);

	return retval;
}

void phy_write(struct ugeth_mii_info *mii_info, u16 regnum, u16 val)
{
	unsigned long flags;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	spin_lock_irqsave(&mii_info->mdio_lock, flags);
	mii_info->mdio_write(mii_info->dev, mii_info->mii_id, regnum, val);
	spin_unlock_irqrestore(&mii_info->mdio_lock, flags);
}

/* Use the PHY ID registers to determine what type of PHY is attached
 * to device dev.  return a struct phy_info structure describing that PHY
 */
struct phy_info *get_phy_info(struct ugeth_mii_info *mii_info)
{
	u16 phy_reg;
	u32 phy_ID;
	int i;
	struct phy_info *theInfo = NULL;
	struct net_device *dev = mii_info->dev;

	ugphy_vdbg("%s: IN", __FUNCTION__);

	/* Grab the bits from PHYIR1, and put them in the upper half */
	phy_reg = phy_read(mii_info, MII_PHYSID1);
	phy_ID = (phy_reg & 0xffff) << 16;

	/* Grab the bits from PHYIR2, and put them in the lower half */
	phy_reg = phy_read(mii_info, MII_PHYSID2);
	phy_ID |= (phy_reg & 0xffff);

	/* loop through all the known PHY types, and find one that */
	/* matches the ID we read from the PHY. */
	for (i = 0; phy_info[i]; i++)
		if (phy_info[i]->phy_id == (phy_ID & phy_info[i]->phy_id_mask)){
			theInfo = phy_info[i];
			break;
		}

	/* This shouldn't happen, as we have generic PHY support */
	if (theInfo == NULL) {
		ugphy_info("%s: PHY id %x is not supported!", dev->name,
			   phy_ID);
		return NULL;
	} else {
		ugphy_info("%s: PHY is %s (%x)", dev->name, theInfo->name,
			   phy_ID);
	}

	return theInfo;
}
