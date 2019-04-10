// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/davicom.c
 *
 * Driver for Davicom PHYs
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
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
#include <linux/uaccess.h>

#define MII_DM9161_SCR		0x10
#define MII_DM9161_SCR_INIT	0x0610
#define MII_DM9161_SCR_RMII	0x0100

/* DM9161 Interrupt Register */
#define MII_DM9161_INTR	0x15
#define MII_DM9161_INTR_PEND		0x8000
#define MII_DM9161_INTR_DPLX_MASK	0x0800
#define MII_DM9161_INTR_SPD_MASK	0x0400
#define MII_DM9161_INTR_LINK_MASK	0x0200
#define MII_DM9161_INTR_MASK		0x0100
#define MII_DM9161_INTR_DPLX_CHANGE	0x0010
#define MII_DM9161_INTR_SPD_CHANGE	0x0008
#define MII_DM9161_INTR_LINK_CHANGE	0x0004
#define MII_DM9161_INTR_INIT 		0x0000
#define MII_DM9161_INTR_STOP	\
(MII_DM9161_INTR_DPLX_MASK | MII_DM9161_INTR_SPD_MASK \
 | MII_DM9161_INTR_LINK_MASK | MII_DM9161_INTR_MASK)

/* DM9161 10BT Configuration/Status */
#define MII_DM9161_10BTCSR	0x12
#define MII_DM9161_10BTCSR_INIT	0x7800

MODULE_DESCRIPTION("Davicom PHY driver");
MODULE_AUTHOR("Andy Fleming");
MODULE_LICENSE("GPL");


#define DM9161_DELAY 1
static int dm9161_config_intr(struct phy_device *phydev)
{
	int temp;

	temp = phy_read(phydev, MII_DM9161_INTR);

	if (temp < 0)
		return temp;

	if (PHY_INTERRUPT_ENABLED == phydev->interrupts)
		temp &= ~(MII_DM9161_INTR_STOP);
	else
		temp |= MII_DM9161_INTR_STOP;

	temp = phy_write(phydev, MII_DM9161_INTR, temp);

	return temp;
}

static int dm9161_config_aneg(struct phy_device *phydev)
{
	int err;

	/* Isolate the PHY */
	err = phy_write(phydev, MII_BMCR, BMCR_ISOLATE);

	if (err < 0)
		return err;

	/* Configure the new settings */
	err = genphy_config_aneg(phydev);

	if (err < 0)
		return err;

	return 0;
}

static int dm9161_config_init(struct phy_device *phydev)
{
	int err, temp;

	/* Isolate the PHY */
	err = phy_write(phydev, MII_BMCR, BMCR_ISOLATE);

	if (err < 0)
		return err;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_MII:
		temp = MII_DM9161_SCR_INIT;
		break;
	case PHY_INTERFACE_MODE_RMII:
		temp =  MII_DM9161_SCR_INIT | MII_DM9161_SCR_RMII;
		break;
	default:
		return -EINVAL;
	}

	/* Do not bypass the scrambler/descrambler */
	err = phy_write(phydev, MII_DM9161_SCR, temp);
	if (err < 0)
		return err;

	/* Clear 10BTCSR to default */
	err = phy_write(phydev, MII_DM9161_10BTCSR, MII_DM9161_10BTCSR_INIT);

	if (err < 0)
		return err;

	/* Reconnect the PHY, and enable Autonegotiation */
	return phy_write(phydev, MII_BMCR, BMCR_ANENABLE);
}

static int dm9161_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, MII_DM9161_INTR);

	return (err < 0) ? err : 0;
}

static struct phy_driver dm91xx_driver[] = {
{
	.phy_id		= 0x0181b880,
	.name		= "Davicom DM9161E",
	.phy_id_mask	= 0x0ffffff0,
	.features	= PHY_BASIC_FEATURES,
	.config_init	= dm9161_config_init,
	.config_aneg	= dm9161_config_aneg,
	.ack_interrupt	= dm9161_ack_interrupt,
	.config_intr	= dm9161_config_intr,
}, {
	.phy_id		= 0x0181b8b0,
	.name		= "Davicom DM9161B/C",
	.phy_id_mask	= 0x0ffffff0,
	.features	= PHY_BASIC_FEATURES,
	.config_init	= dm9161_config_init,
	.config_aneg	= dm9161_config_aneg,
	.ack_interrupt	= dm9161_ack_interrupt,
	.config_intr	= dm9161_config_intr,
}, {
	.phy_id		= 0x0181b8a0,
	.name		= "Davicom DM9161A",
	.phy_id_mask	= 0x0ffffff0,
	.features	= PHY_BASIC_FEATURES,
	.config_init	= dm9161_config_init,
	.config_aneg	= dm9161_config_aneg,
	.ack_interrupt	= dm9161_ack_interrupt,
	.config_intr	= dm9161_config_intr,
}, {
	.phy_id		= 0x00181b80,
	.name		= "Davicom DM9131",
	.phy_id_mask	= 0x0ffffff0,
	.features	= PHY_BASIC_FEATURES,
	.ack_interrupt	= dm9161_ack_interrupt,
	.config_intr	= dm9161_config_intr,
} };

module_phy_driver(dm91xx_driver);

static struct mdio_device_id __maybe_unused davicom_tbl[] = {
	{ 0x0181b880, 0x0ffffff0 },
	{ 0x0181b8b0, 0x0ffffff0 },
	{ 0x0181b8a0, 0x0ffffff0 },
	{ 0x00181b80, 0x0ffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, davicom_tbl);
