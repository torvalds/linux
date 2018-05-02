/*
 * drivers/net/phy/national.c
 *
 * Driver for National Semiconductor PHYs
 *
 * Author: Stuart Menefy <stuart.menefy@st.com>
 * Maintainer: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 *
 * Copyright (c) 2008 STMicroelectronics Limited
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/netdevice.h>

#define DEBUG

/* DP83865 phy identifier values */
#define DP83865_PHY_ID	0x20005c7a

#define DP83865_INT_STATUS	0x14
#define DP83865_INT_MASK	0x15
#define DP83865_INT_CLEAR	0x17

#define DP83865_INT_REMOTE_FAULT 0x0008
#define DP83865_INT_ANE_COMPLETED 0x0010
#define DP83865_INT_LINK_CHANGE	0xe000
#define DP83865_INT_MASK_DEFAULT (DP83865_INT_REMOTE_FAULT | \
				DP83865_INT_ANE_COMPLETED | \
				DP83865_INT_LINK_CHANGE)

/* Advanced proprietary configuration */
#define NS_EXP_MEM_CTL	0x16
#define NS_EXP_MEM_DATA	0x1d
#define NS_EXP_MEM_ADD	0x1e

#define LED_CTRL_REG 0x13
#define AN_FALLBACK_AN 0x0001
#define AN_FALLBACK_CRC 0x0002
#define AN_FALLBACK_IE 0x0004
#define ALL_FALLBACK_ON (AN_FALLBACK_AN |  AN_FALLBACK_CRC | AN_FALLBACK_IE)

enum hdx_loopback {
	hdx_loopback_on = 0,
	hdx_loopback_off = 1,
};

static u8 ns_exp_read(struct phy_device *phydev, u16 reg)
{
	phy_write(phydev, NS_EXP_MEM_ADD, reg);
	return phy_read(phydev, NS_EXP_MEM_DATA);
}

static void ns_exp_write(struct phy_device *phydev, u16 reg, u8 data)
{
	phy_write(phydev, NS_EXP_MEM_ADD, reg);
	phy_write(phydev, NS_EXP_MEM_DATA, data);
}

static int ns_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, DP83865_INT_MASK,
				DP83865_INT_MASK_DEFAULT);
	else
		err = phy_write(phydev, DP83865_INT_MASK, 0);

	return err;
}

static int ns_ack_interrupt(struct phy_device *phydev)
{
	int ret = phy_read(phydev, DP83865_INT_STATUS);
	if (ret < 0)
		return ret;

	/* Clear the interrupt status bit by writing a “1”
	 * to the corresponding bit in INT_CLEAR (2:0 are reserved) */
	ret = phy_write(phydev, DP83865_INT_CLEAR, ret & ~0x7);

	return ret;
}

static void ns_giga_speed_fallback(struct phy_device *phydev, int mode)
{
	int bmcr = phy_read(phydev, MII_BMCR);

	phy_write(phydev, MII_BMCR, (bmcr | BMCR_PDOWN));

	/* Enable 8 bit expended memory read/write (no auto increment) */
	phy_write(phydev, NS_EXP_MEM_CTL, 0);
	phy_write(phydev, NS_EXP_MEM_ADD, 0x1C0);
	phy_write(phydev, NS_EXP_MEM_DATA, 0x0008);
	phy_write(phydev, MII_BMCR, (bmcr & ~BMCR_PDOWN));
	phy_write(phydev, LED_CTRL_REG, mode);
}

static void ns_10_base_t_hdx_loopack(struct phy_device *phydev, int disable)
{
	if (disable)
		ns_exp_write(phydev, 0x1c0, ns_exp_read(phydev, 0x1c0) | 1);
	else
		ns_exp_write(phydev, 0x1c0,
			     ns_exp_read(phydev, 0x1c0) & 0xfffe);

	pr_debug("10BASE-T HDX loopback %s\n",
		 (ns_exp_read(phydev, 0x1c0) & 0x0001) ? "off" : "on");
}

static int ns_config_init(struct phy_device *phydev)
{
	ns_giga_speed_fallback(phydev, ALL_FALLBACK_ON);
	/* In the latest MAC or switches design, the 10 Mbps loopback
	   is desired to be turned off. */
	ns_10_base_t_hdx_loopack(phydev, hdx_loopback_off);
	return ns_ack_interrupt(phydev);
}

static struct phy_driver dp83865_driver[] = { {
	.phy_id = DP83865_PHY_ID,
	.phy_id_mask = 0xfffffff0,
	.name = "NatSemi DP83865",
	.features = PHY_GBIT_FEATURES,
	.flags = PHY_HAS_INTERRUPT,
	.config_init = ns_config_init,
	.ack_interrupt = ns_ack_interrupt,
	.config_intr = ns_config_intr,
} };

module_phy_driver(dp83865_driver);

MODULE_DESCRIPTION("NatSemi PHY driver");
MODULE_AUTHOR("Stuart Menefy");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused ns_tbl[] = {
	{ DP83865_PHY_ID, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, ns_tbl);
