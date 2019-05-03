// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/et1011c.c
 *
 * Driver for LSI ET1011C PHYs
 *
 * Author: Chaithrika U S
 *
 * Copyright (c) 2008 Texas Instruments
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
#include <linux/io.h>
#include <linux/uaccess.h>
#include <asm/irq.h>

#define ET1011C_STATUS_REG	(0x1A)
#define ET1011C_CONFIG_REG	(0x16)
#define ET1011C_SPEED_MASK		(0x0300)
#define ET1011C_GIGABIT_SPEED		(0x0200)
#define ET1011C_TX_FIFO_MASK		(0x3000)
#define ET1011C_TX_FIFO_DEPTH_8		(0x0000)
#define ET1011C_TX_FIFO_DEPTH_16	(0x1000)
#define ET1011C_INTERFACE_MASK		(0x0007)
#define ET1011C_GMII_INTERFACE		(0x0002)
#define ET1011C_SYS_CLK_EN		(0x01 << 4)


MODULE_DESCRIPTION("LSI ET1011C PHY driver");
MODULE_AUTHOR("Chaithrika U S");
MODULE_LICENSE("GPL");

static int et1011c_config_aneg(struct phy_device *phydev)
{
	int ctl = 0;
	ctl = phy_read(phydev, MII_BMCR);
	if (ctl < 0)
		return ctl;
	ctl &= ~(BMCR_FULLDPLX | BMCR_SPEED100 | BMCR_SPEED1000 |
		 BMCR_ANENABLE);
	/* First clear the PHY */
	phy_write(phydev, MII_BMCR, ctl | BMCR_RESET);

	return genphy_config_aneg(phydev);
}

static int et1011c_read_status(struct phy_device *phydev)
{
	int ret;
	u32 val;
	static int speed;
	ret = genphy_read_status(phydev);

	if (speed != phydev->speed) {
		speed = phydev->speed;
		val = phy_read(phydev, ET1011C_STATUS_REG);
		if ((val & ET1011C_SPEED_MASK) ==
					ET1011C_GIGABIT_SPEED) {
			val = phy_read(phydev, ET1011C_CONFIG_REG);
			val &= ~ET1011C_TX_FIFO_MASK;
			phy_write(phydev, ET1011C_CONFIG_REG, val\
					| ET1011C_GMII_INTERFACE\
					| ET1011C_SYS_CLK_EN\
					| ET1011C_TX_FIFO_DEPTH_16);

		}
	}
	return ret;
}

static struct phy_driver et1011c_driver[] = { {
	.phy_id		= 0x0282f014,
	.name		= "ET1011C",
	.phy_id_mask	= 0xfffffff0,
	.features	= PHY_GBIT_FEATURES,
	.config_aneg	= et1011c_config_aneg,
	.read_status	= et1011c_read_status,
} };

module_phy_driver(et1011c_driver);

static struct mdio_device_id __maybe_unused et1011c_tbl[] = {
	{ 0x0282f014, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, et1011c_tbl);
