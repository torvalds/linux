/*
 * drivers/net/phy/marvell.c
 *
 * Driver for Marvell PHYs
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
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#define MII_M1011_IEVENT		0x13
#define MII_M1011_IEVENT_CLEAR		0x0000

#define MII_M1011_IMASK			0x12
#define MII_M1011_IMASK_INIT		0x6400
#define MII_M1011_IMASK_CLEAR		0x0000

#define MII_M1011_PHY_SCR		0x10
#define MII_M1011_PHY_SCR_AUTO_CROSS	0x0060

#define MII_M1145_PHY_EXT_CR		0x14
#define MII_M1145_RGMII_RX_DELAY	0x0080
#define MII_M1145_RGMII_TX_DELAY	0x0002

#define M1145_DEV_FLAGS_RESISTANCE	0x00000001

#define MII_M1111_PHY_LED_CONTROL	0x18
#define MII_M1111_PHY_LED_DIRECT	0x4100
#define MII_M1111_PHY_LED_COMBINE	0x411c
#define MII_M1111_PHY_EXT_CR		0x14
#define MII_M1111_RX_DELAY		0x80
#define MII_M1111_TX_DELAY		0x2
#define MII_M1111_PHY_EXT_SR		0x1b
#define MII_M1111_HWCFG_MODE_MASK	0xf
#define MII_M1111_HWCFG_MODE_RGMII	0xb

MODULE_DESCRIPTION("Marvell PHY driver");
MODULE_AUTHOR("Andy Fleming");
MODULE_LICENSE("GPL");

static int marvell_ack_interrupt(struct phy_device *phydev)
{
	int err;

	/* Clear the interrupts by reading the reg */
	err = phy_read(phydev, MII_M1011_IEVENT);

	if (err < 0)
		return err;

	return 0;
}

static int marvell_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, MII_M1011_IMASK, MII_M1011_IMASK_INIT);
	else
		err = phy_write(phydev, MII_M1011_IMASK, MII_M1011_IMASK_CLEAR);

	return err;
}

static int marvell_config_aneg(struct phy_device *phydev)
{
	int err;

	/* The Marvell PHY has an errata which requires
	 * that certain registers get written in order
	 * to restart autonegotiation */
	err = phy_write(phydev, MII_BMCR, BMCR_RESET);

	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1d, 0x1f);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0x200c);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1d, 0x5);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0x100);
	if (err < 0)
		return err;

	err = phy_write(phydev, MII_M1011_PHY_SCR,
			MII_M1011_PHY_SCR_AUTO_CROSS);
	if (err < 0)
		return err;

	err = phy_write(phydev, MII_M1111_PHY_LED_CONTROL,
			MII_M1111_PHY_LED_DIRECT);
	if (err < 0)
		return err;

	err = genphy_config_aneg(phydev);

	return err;
}

static int m88e1111_config_init(struct phy_device *phydev)
{
	int err;

	if ((phydev->interface == PHY_INTERFACE_MODE_RGMII) ||
	    (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)) {
		int temp;

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID) {
			temp = phy_read(phydev, MII_M1111_PHY_EXT_CR);
			if (temp < 0)
				return temp;

			temp |= (MII_M1111_RX_DELAY | MII_M1111_TX_DELAY);

			err = phy_write(phydev, MII_M1111_PHY_EXT_CR, temp);
			if (err < 0)
				return err;
		}

		temp = phy_read(phydev, MII_M1111_PHY_EXT_SR);
		if (temp < 0)
			return temp;

		temp &= ~(MII_M1111_HWCFG_MODE_MASK);
		temp |= MII_M1111_HWCFG_MODE_RGMII;

		err = phy_write(phydev, MII_M1111_PHY_EXT_SR, temp);
		if (err < 0)
			return err;
	}

	err = phy_write(phydev, MII_BMCR, BMCR_RESET);
	if (err < 0)
		return err;

	return 0;
}

static int m88e1145_config_init(struct phy_device *phydev)
{
	int err;

	/* Take care of errata E0 & E1 */
	err = phy_write(phydev, 0x1d, 0x001b);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0x418f);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1d, 0x0016);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0xa2da);
	if (err < 0)
		return err;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID) {
		int temp = phy_read(phydev, MII_M1145_PHY_EXT_CR);
		if (temp < 0)
			return temp;

		temp |= (MII_M1145_RGMII_RX_DELAY | MII_M1145_RGMII_TX_DELAY);

		err = phy_write(phydev, MII_M1145_PHY_EXT_CR, temp);
		if (err < 0)
			return err;

		if (phydev->dev_flags & M1145_DEV_FLAGS_RESISTANCE) {
			err = phy_write(phydev, 0x1d, 0x0012);
			if (err < 0)
				return err;

			temp = phy_read(phydev, 0x1e);
			if (temp < 0)
				return temp;

			temp &= 0xf03f;
			temp |= 2 << 9;	/* 36 ohm */
			temp |= 2 << 6;	/* 39 ohm */

			err = phy_write(phydev, 0x1e, temp);
			if (err < 0)
				return err;

			err = phy_write(phydev, 0x1d, 0x3);
			if (err < 0)
				return err;

			err = phy_write(phydev, 0x1e, 0x8000);
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static struct phy_driver m88e1101_driver = {
	.phy_id = 0x01410c60,
	.phy_id_mask = 0xfffffff0,
	.name = "Marvell 88E1101",
	.features = PHY_GBIT_FEATURES,
	.flags = PHY_HAS_INTERRUPT,
	.config_aneg = &marvell_config_aneg,
	.read_status = &genphy_read_status,
	.ack_interrupt = &marvell_ack_interrupt,
	.config_intr = &marvell_config_intr,
	.driver = {.owner = THIS_MODULE,},
};

static struct phy_driver m88e1111_driver = {
	.phy_id = 0x01410cc0,
	.phy_id_mask = 0xfffffff0,
	.name = "Marvell 88E1111",
	.features = PHY_GBIT_FEATURES,
	.flags = PHY_HAS_INTERRUPT,
	.config_aneg = &marvell_config_aneg,
	.read_status = &genphy_read_status,
	.ack_interrupt = &marvell_ack_interrupt,
	.config_intr = &marvell_config_intr,
	.config_init = &m88e1111_config_init,
	.driver = {.owner = THIS_MODULE,},
};

static struct phy_driver m88e1145_driver = {
	.phy_id = 0x01410cd0,
	.phy_id_mask = 0xfffffff0,
	.name = "Marvell 88E1145",
	.features = PHY_GBIT_FEATURES,
	.flags = PHY_HAS_INTERRUPT,
	.config_init = &m88e1145_config_init,
	.config_aneg = &marvell_config_aneg,
	.read_status = &genphy_read_status,
	.ack_interrupt = &marvell_ack_interrupt,
	.config_intr = &marvell_config_intr,
	.driver = {.owner = THIS_MODULE,},
};

static int __init marvell_init(void)
{
	int ret;

	ret = phy_driver_register(&m88e1101_driver);
	if (ret)
		return ret;

	ret = phy_driver_register(&m88e1111_driver);
	if (ret)
		goto err1111;

	ret = phy_driver_register(&m88e1145_driver);
	if (ret)
		goto err1145;

	return 0;

err1145:
	phy_driver_unregister(&m88e1111_driver);
err1111:
	phy_driver_unregister(&m88e1101_driver);
	return ret;
}

static void __exit marvell_exit(void)
{
	phy_driver_unregister(&m88e1101_driver);
	phy_driver_unregister(&m88e1111_driver);
	phy_driver_unregister(&m88e1145_driver);
}

module_init(marvell_init);
module_exit(marvell_exit);
