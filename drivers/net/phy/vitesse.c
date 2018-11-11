/*
 * Driver for Vitesse PHYs
 *
 * Author: Kriston Carson
 *
 * Copyright (c) 2005, 2009, 2011 Freescale Semiconductor, Inc.
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

/* Vitesse Extended Page Magic Register(s) */
#define MII_VSC82X4_EXT_PAGE_16E	0x10
#define MII_VSC82X4_EXT_PAGE_17E	0x11
#define MII_VSC82X4_EXT_PAGE_18E	0x12

/* Vitesse Extended Control Register 1 */
#define MII_VSC8244_EXT_CON1           0x17
#define MII_VSC8244_EXTCON1_INIT       0x0000
#define MII_VSC8244_EXTCON1_TX_SKEW_MASK	0x0c00
#define MII_VSC8244_EXTCON1_RX_SKEW_MASK	0x0300
#define MII_VSC8244_EXTCON1_TX_SKEW	0x0800
#define MII_VSC8244_EXTCON1_RX_SKEW	0x0200

/* Vitesse Interrupt Mask Register */
#define MII_VSC8244_IMASK		0x19
#define MII_VSC8244_IMASK_IEN		0x8000
#define MII_VSC8244_IMASK_SPEED		0x4000
#define MII_VSC8244_IMASK_LINK		0x2000
#define MII_VSC8244_IMASK_DUPLEX	0x1000
#define MII_VSC8244_IMASK_MASK		0xf000

#define MII_VSC8221_IMASK_MASK		0xa000

/* Vitesse Interrupt Status Register */
#define MII_VSC8244_ISTAT		0x1a
#define MII_VSC8244_ISTAT_STATUS	0x8000
#define MII_VSC8244_ISTAT_SPEED		0x4000
#define MII_VSC8244_ISTAT_LINK		0x2000
#define MII_VSC8244_ISTAT_DUPLEX	0x1000

/* Vitesse Auxiliary Control/Status Register */
#define MII_VSC8244_AUX_CONSTAT		0x1c
#define MII_VSC8244_AUXCONSTAT_INIT	0x0000
#define MII_VSC8244_AUXCONSTAT_DUPLEX	0x0020
#define MII_VSC8244_AUXCONSTAT_SPEED	0x0018
#define MII_VSC8244_AUXCONSTAT_GBIT	0x0010
#define MII_VSC8244_AUXCONSTAT_100	0x0008

#define MII_VSC8221_AUXCONSTAT_INIT	0x0004 /* need to set this bit? */
#define MII_VSC8221_AUXCONSTAT_RESERVED	0x0004

/* Vitesse Extended Page Access Register */
#define MII_VSC82X4_EXT_PAGE_ACCESS	0x1f

/* Vitesse VSC8601 Extended PHY Control Register 1 */
#define MII_VSC8601_EPHY_CTL		0x17
#define MII_VSC8601_EPHY_CTL_RGMII_SKEW	(1 << 8)

#define PHY_ID_VSC8234			0x000fc620
#define PHY_ID_VSC8244			0x000fc6c0
#define PHY_ID_VSC8514			0x00070670
#define PHY_ID_VSC8572			0x000704d0
#define PHY_ID_VSC8574			0x000704a0
#define PHY_ID_VSC8601			0x00070420
#define PHY_ID_VSC7385			0x00070450
#define PHY_ID_VSC7388			0x00070480
#define PHY_ID_VSC7395			0x00070550
#define PHY_ID_VSC7398			0x00070580
#define PHY_ID_VSC8662			0x00070660
#define PHY_ID_VSC8221			0x000fc550
#define PHY_ID_VSC8211			0x000fc4b0

MODULE_DESCRIPTION("Vitesse PHY driver");
MODULE_AUTHOR("Kriston Carson");
MODULE_LICENSE("GPL");

static int vsc824x_add_skew(struct phy_device *phydev)
{
	int err;
	int extcon;

	extcon = phy_read(phydev, MII_VSC8244_EXT_CON1);

	if (extcon < 0)
		return extcon;

	extcon &= ~(MII_VSC8244_EXTCON1_TX_SKEW_MASK |
			MII_VSC8244_EXTCON1_RX_SKEW_MASK);

	extcon |= (MII_VSC8244_EXTCON1_TX_SKEW |
			MII_VSC8244_EXTCON1_RX_SKEW);

	err = phy_write(phydev, MII_VSC8244_EXT_CON1, extcon);

	return err;
}

static int vsc824x_config_init(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_VSC8244_AUX_CONSTAT,
			MII_VSC8244_AUXCONSTAT_INIT);
	if (err < 0)
		return err;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
		err = vsc824x_add_skew(phydev);

	return err;
}

#define VSC73XX_EXT_PAGE_ACCESS 0x1f

static int vsc73xx_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, VSC73XX_EXT_PAGE_ACCESS);
}

static int vsc73xx_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, VSC73XX_EXT_PAGE_ACCESS, page);
}

static void vsc73xx_config_init(struct phy_device *phydev)
{
	/* Receiver init */
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x0c, 0x0300, 0x0200);
	phy_write(phydev, 0x1f, 0x0000);

	/* Config LEDs 0x61 */
	phy_modify(phydev, MII_TPISTATUS, 0xff00, 0x0061);
}

static int vsc738x_config_init(struct phy_device *phydev)
{
	u16 rev;
	/* This magic sequence appear in the application note
	 * "VSC7385/7388 PHY Configuration".
	 *
	 * Maybe one day we will get to know what it all means.
	 */
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x08, 0x0200, 0x0200);
	phy_write(phydev, 0x1f, 0x52b5);
	phy_write(phydev, 0x10, 0xb68a);
	phy_modify(phydev, 0x12, 0xff07, 0x0003);
	phy_modify(phydev, 0x11, 0x00ff, 0x00a2);
	phy_write(phydev, 0x10, 0x968a);
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x08, 0x0200, 0x0000);
	phy_write(phydev, 0x1f, 0x0000);

	/* Read revision */
	rev = phy_read(phydev, MII_PHYSID2);
	rev &= 0x0f;

	/* Special quirk for revision 0 */
	if (rev == 0) {
		phy_write(phydev, 0x1f, 0x2a30);
		phy_modify(phydev, 0x08, 0x0200, 0x0200);
		phy_write(phydev, 0x1f, 0x52b5);
		phy_write(phydev, 0x12, 0x0000);
		phy_write(phydev, 0x11, 0x0689);
		phy_write(phydev, 0x10, 0x8f92);
		phy_write(phydev, 0x1f, 0x52b5);
		phy_write(phydev, 0x12, 0x0000);
		phy_write(phydev, 0x11, 0x0e35);
		phy_write(phydev, 0x10, 0x9786);
		phy_write(phydev, 0x1f, 0x2a30);
		phy_modify(phydev, 0x08, 0x0200, 0x0000);
		phy_write(phydev, 0x17, 0xff80);
		phy_write(phydev, 0x17, 0x0000);
	}

	phy_write(phydev, 0x1f, 0x0000);
	phy_write(phydev, 0x12, 0x0048);

	if (rev == 0) {
		phy_write(phydev, 0x1f, 0x2a30);
		phy_write(phydev, 0x14, 0x6600);
		phy_write(phydev, 0x1f, 0x0000);
		phy_write(phydev, 0x18, 0xa24e);
	} else {
		phy_write(phydev, 0x1f, 0x2a30);
		phy_modify(phydev, 0x16, 0x0fc0, 0x0240);
		phy_modify(phydev, 0x14, 0x6000, 0x4000);
		/* bits 14-15 in extended register 0x14 controls DACG amplitude
		 * 6 = -8%, 2 is hardware default
		 */
		phy_write(phydev, 0x1f, 0x0001);
		phy_modify(phydev, 0x14, 0xe000, 0x6000);
		phy_write(phydev, 0x1f, 0x0000);
	}

	vsc73xx_config_init(phydev);

	return genphy_config_init(phydev);
}

static int vsc739x_config_init(struct phy_device *phydev)
{
	/* This magic sequence appears in the VSC7395 SparX-G5e application
	 * note "VSC7395/VSC7398 PHY Configuration"
	 *
	 * Maybe one day we will get to know what it all means.
	 */
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x08, 0x0200, 0x0200);
	phy_write(phydev, 0x1f, 0x52b5);
	phy_write(phydev, 0x10, 0xb68a);
	phy_modify(phydev, 0x12, 0xff07, 0x0003);
	phy_modify(phydev, 0x11, 0x00ff, 0x00a2);
	phy_write(phydev, 0x10, 0x968a);
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x08, 0x0200, 0x0000);
	phy_write(phydev, 0x1f, 0x0000);

	phy_write(phydev, 0x1f, 0x0000);
	phy_write(phydev, 0x12, 0x0048);
	phy_write(phydev, 0x1f, 0x2a30);
	phy_modify(phydev, 0x16, 0x0fc0, 0x0240);
	phy_modify(phydev, 0x14, 0x6000, 0x4000);
	phy_write(phydev, 0x1f, 0x0001);
	phy_modify(phydev, 0x14, 0xe000, 0x6000);
	phy_write(phydev, 0x1f, 0x0000);

	vsc73xx_config_init(phydev);

	return genphy_config_init(phydev);
}

static int vsc73xx_config_aneg(struct phy_device *phydev)
{
	/* The VSC73xx switches does not like to be instructed to
	 * do autonegotiation in any way, it prefers that you just go
	 * with the power-on/reset defaults. Writing some registers will
	 * just make autonegotiation permanently fail.
	 */
	return 0;
}

/* This adds a skew for both TX and RX clocks, so the skew should only be
 * applied to "rgmii-id" interfaces. It may not work as expected
 * on "rgmii-txid", "rgmii-rxid" or "rgmii" interfaces. */
static int vsc8601_add_skew(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_VSC8601_EPHY_CTL);
	if (ret < 0)
		return ret;

	ret |= MII_VSC8601_EPHY_CTL_RGMII_SKEW;
	return phy_write(phydev, MII_VSC8601_EPHY_CTL, ret);
}

static int vsc8601_config_init(struct phy_device *phydev)
{
	int ret = 0;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
		ret = vsc8601_add_skew(phydev);

	if (ret < 0)
		return ret;

	return genphy_config_init(phydev);
}

static int vsc824x_ack_interrupt(struct phy_device *phydev)
{
	int err = 0;

	/* Don't bother to ACK the interrupts if interrupts
	 * are disabled.  The 824x cannot clear the interrupts
	 * if they are disabled.
	 */
	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_read(phydev, MII_VSC8244_ISTAT);

	return (err < 0) ? err : 0;
}

static int vsc82xx_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, MII_VSC8244_IMASK,
			(phydev->drv->phy_id == PHY_ID_VSC8234 ||
			 phydev->drv->phy_id == PHY_ID_VSC8244 ||
			 phydev->drv->phy_id == PHY_ID_VSC8514 ||
			 phydev->drv->phy_id == PHY_ID_VSC8572 ||
			 phydev->drv->phy_id == PHY_ID_VSC8574 ||
			 phydev->drv->phy_id == PHY_ID_VSC8601) ?
				MII_VSC8244_IMASK_MASK :
				MII_VSC8221_IMASK_MASK);
	else {
		/* The Vitesse PHY cannot clear the interrupt
		 * once it has disabled them, so we clear them first
		 */
		err = phy_read(phydev, MII_VSC8244_ISTAT);

		if (err < 0)
			return err;

		err = phy_write(phydev, MII_VSC8244_IMASK, 0);
	}

	return err;
}

static int vsc8221_config_init(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_VSC8244_AUX_CONSTAT,
			MII_VSC8221_AUXCONSTAT_INIT);
	return err;

	/* Perhaps we should set EXT_CON1 based on the interface?
	 * Options are 802.3Z SerDes or SGMII
	 */
}

/* vsc82x4_config_autocross_enable - Enable auto MDI/MDI-X for forced links
 * @phydev: target phy_device struct
 *
 * Enable auto MDI/MDI-X when in 10/100 forced link speeds by writing
 * special values in the VSC8234/VSC8244 extended reserved registers
 */
static int vsc82x4_config_autocross_enable(struct phy_device *phydev)
{
	int ret;

	if (phydev->autoneg == AUTONEG_ENABLE || phydev->speed > SPEED_100)
		return 0;

	/* map extended registers set 0x10 - 0x1e */
	ret = phy_write(phydev, MII_VSC82X4_EXT_PAGE_ACCESS, 0x52b5);
	if (ret >= 0)
		ret = phy_write(phydev, MII_VSC82X4_EXT_PAGE_18E, 0x0012);
	if (ret >= 0)
		ret = phy_write(phydev, MII_VSC82X4_EXT_PAGE_17E, 0x2803);
	if (ret >= 0)
		ret = phy_write(phydev, MII_VSC82X4_EXT_PAGE_16E, 0x87fa);
	/* map standard registers set 0x10 - 0x1e */
	if (ret >= 0)
		ret = phy_write(phydev, MII_VSC82X4_EXT_PAGE_ACCESS, 0x0000);
	else
		phy_write(phydev, MII_VSC82X4_EXT_PAGE_ACCESS, 0x0000);

	return ret;
}

/* vsc82x4_config_aneg - restart auto-negotiation or write BMCR
 * @phydev: target phy_device struct
 *
 * Description: If auto-negotiation is enabled, we configure the
 *   advertising, and then restart auto-negotiation.  If it is not
 *   enabled, then we write the BMCR and also start the auto
 *   MDI/MDI-X feature
 */
static int vsc82x4_config_aneg(struct phy_device *phydev)
{
	int ret;

	/* Enable auto MDI/MDI-X when in 10/100 forced link speeds by
	 * writing special values in the VSC8234 extended reserved registers
	 */
	if (phydev->autoneg != AUTONEG_ENABLE && phydev->speed <= SPEED_100) {
		ret = genphy_setup_forced(phydev);

		if (ret < 0) /* error */
			return ret;

		return vsc82x4_config_autocross_enable(phydev);
	}

	return genphy_config_aneg(phydev);
}

/* Vitesse 82xx */
static struct phy_driver vsc82xx_driver[] = {
{
	.phy_id         = PHY_ID_VSC8234,
	.name           = "Vitesse VSC8234",
	.phy_id_mask    = 0x000ffff0,
	.features       = PHY_GBIT_FEATURES,
	.config_init    = &vsc824x_config_init,
	.config_aneg    = &vsc82x4_config_aneg,
	.ack_interrupt  = &vsc824x_ack_interrupt,
	.config_intr    = &vsc82xx_config_intr,
}, {
	.phy_id		= PHY_ID_VSC8244,
	.name		= "Vitesse VSC8244",
	.phy_id_mask	= 0x000fffc0,
	.features	= PHY_GBIT_FEATURES,
	.config_init	= &vsc824x_config_init,
	.config_aneg	= &vsc82x4_config_aneg,
	.ack_interrupt	= &vsc824x_ack_interrupt,
	.config_intr	= &vsc82xx_config_intr,
}, {
	.phy_id		= PHY_ID_VSC8514,
	.name		= "Vitesse VSC8514",
	.phy_id_mask	= 0x000ffff0,
	.features	= PHY_GBIT_FEATURES,
	.config_init	= &vsc824x_config_init,
	.config_aneg	= &vsc82x4_config_aneg,
	.ack_interrupt	= &vsc824x_ack_interrupt,
	.config_intr	= &vsc82xx_config_intr,
}, {
	.phy_id         = PHY_ID_VSC8572,
	.name           = "Vitesse VSC8572",
	.phy_id_mask    = 0x000ffff0,
	.features       = PHY_GBIT_FEATURES,
	.config_init    = &vsc824x_config_init,
	.config_aneg    = &vsc82x4_config_aneg,
	.ack_interrupt  = &vsc824x_ack_interrupt,
	.config_intr    = &vsc82xx_config_intr,
}, {
	.phy_id         = PHY_ID_VSC8574,
	.name           = "Vitesse VSC8574",
	.phy_id_mask    = 0x000ffff0,
	.features       = PHY_GBIT_FEATURES,
	.config_init    = &vsc824x_config_init,
	.config_aneg    = &vsc82x4_config_aneg,
	.ack_interrupt  = &vsc824x_ack_interrupt,
	.config_intr    = &vsc82xx_config_intr,
}, {
	.phy_id         = PHY_ID_VSC8601,
	.name           = "Vitesse VSC8601",
	.phy_id_mask    = 0x000ffff0,
	.features       = PHY_GBIT_FEATURES,
	.config_init    = &vsc8601_config_init,
	.ack_interrupt  = &vsc824x_ack_interrupt,
	.config_intr    = &vsc82xx_config_intr,
}, {
	.phy_id         = PHY_ID_VSC7385,
	.name           = "Vitesse VSC7385",
	.phy_id_mask    = 0x000ffff0,
	.features       = PHY_GBIT_FEATURES,
	.config_init    = vsc738x_config_init,
	.config_aneg    = vsc73xx_config_aneg,
	.read_page      = vsc73xx_read_page,
	.write_page     = vsc73xx_write_page,
}, {
	.phy_id         = PHY_ID_VSC7388,
	.name           = "Vitesse VSC7388",
	.phy_id_mask    = 0x000ffff0,
	.features       = PHY_GBIT_FEATURES,
	.config_init    = vsc738x_config_init,
	.config_aneg    = vsc73xx_config_aneg,
	.read_page      = vsc73xx_read_page,
	.write_page     = vsc73xx_write_page,
}, {
	.phy_id         = PHY_ID_VSC7395,
	.name           = "Vitesse VSC7395",
	.phy_id_mask    = 0x000ffff0,
	.features       = PHY_GBIT_FEATURES,
	.config_init    = vsc739x_config_init,
	.config_aneg    = vsc73xx_config_aneg,
	.read_page      = vsc73xx_read_page,
	.write_page     = vsc73xx_write_page,
}, {
	.phy_id         = PHY_ID_VSC7398,
	.name           = "Vitesse VSC7398",
	.phy_id_mask    = 0x000ffff0,
	.features       = PHY_GBIT_FEATURES,
	.config_init    = vsc739x_config_init,
	.config_aneg    = vsc73xx_config_aneg,
	.read_page      = vsc73xx_read_page,
	.write_page     = vsc73xx_write_page,
}, {
	.phy_id         = PHY_ID_VSC8662,
	.name           = "Vitesse VSC8662",
	.phy_id_mask    = 0x000ffff0,
	.features       = PHY_GBIT_FEATURES,
	.config_init    = &vsc824x_config_init,
	.config_aneg    = &vsc82x4_config_aneg,
	.ack_interrupt  = &vsc824x_ack_interrupt,
	.config_intr    = &vsc82xx_config_intr,
}, {
	/* Vitesse 8221 */
	.phy_id		= PHY_ID_VSC8221,
	.phy_id_mask	= 0x000ffff0,
	.name		= "Vitesse VSC8221",
	.features	= PHY_GBIT_FEATURES,
	.config_init	= &vsc8221_config_init,
	.ack_interrupt	= &vsc824x_ack_interrupt,
	.config_intr	= &vsc82xx_config_intr,
}, {
	/* Vitesse 8211 */
	.phy_id		= PHY_ID_VSC8211,
	.phy_id_mask	= 0x000ffff0,
	.name		= "Vitesse VSC8211",
	.features	= PHY_GBIT_FEATURES,
	.config_init	= &vsc8221_config_init,
	.ack_interrupt	= &vsc824x_ack_interrupt,
	.config_intr	= &vsc82xx_config_intr,
} };

module_phy_driver(vsc82xx_driver);

static struct mdio_device_id __maybe_unused vitesse_tbl[] = {
	{ PHY_ID_VSC8234, 0x000ffff0 },
	{ PHY_ID_VSC8244, 0x000fffc0 },
	{ PHY_ID_VSC8514, 0x000ffff0 },
	{ PHY_ID_VSC8572, 0x000ffff0 },
	{ PHY_ID_VSC8574, 0x000ffff0 },
	{ PHY_ID_VSC7385, 0x000ffff0 },
	{ PHY_ID_VSC7388, 0x000ffff0 },
	{ PHY_ID_VSC7395, 0x000ffff0 },
	{ PHY_ID_VSC7398, 0x000ffff0 },
	{ PHY_ID_VSC8662, 0x000ffff0 },
	{ PHY_ID_VSC8221, 0x000ffff0 },
	{ PHY_ID_VSC8211, 0x000ffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, vitesse_tbl);
