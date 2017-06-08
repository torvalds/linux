/*
 * drivers/net/phy/smsc.c
 *
 * Driver for SMSC PHYs
 *
 * Author: Herbert Valerio Riedel
 *
 * Copyright (c) 2006 Herbert Valerio Riedel <hvr@gnu.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support added for SMSC LAN8187 and LAN8700 by steve.glendinning@shawell.net
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/smscphy.h>

struct smsc_hw_stat {
	const char *string;
	u8 reg;
	u8 bits;
};

static struct smsc_hw_stat smsc_hw_stats[] = {
	{ "phy_symbol_errors", 26, 16},
};

struct smsc_phy_priv {
	bool energy_enable;
};

static int smsc_phy_config_intr(struct phy_device *phydev)
{
	int rc = phy_write (phydev, MII_LAN83C185_IM,
			((PHY_INTERRUPT_ENABLED == phydev->interrupts)
			? MII_LAN83C185_ISF_INT_PHYLIB_EVENTS
			: 0));

	return rc < 0 ? rc : 0;
}

static int smsc_phy_ack_interrupt(struct phy_device *phydev)
{
	int rc = phy_read (phydev, MII_LAN83C185_ISF);

	return rc < 0 ? rc : 0;
}

static int smsc_phy_config_init(struct phy_device *phydev)
{
	struct smsc_phy_priv *priv = phydev->priv;

	int rc = phy_read(phydev, MII_LAN83C185_CTRL_STATUS);

	if (rc < 0)
		return rc;

	if (priv->energy_enable) {
		/* Enable energy detect mode for this SMSC Transceivers */
		rc = phy_write(phydev, MII_LAN83C185_CTRL_STATUS,
			       rc | MII_LAN83C185_EDPWRDOWN);
		if (rc < 0)
			return rc;
	}

	return smsc_phy_ack_interrupt(phydev);
}

static int smsc_phy_reset(struct phy_device *phydev)
{
	int rc = phy_read(phydev, MII_LAN83C185_SPECIAL_MODES);
	if (rc < 0)
		return rc;

	/* If the SMSC PHY is in power down mode, then set it
	 * in all capable mode before using it.
	 */
	if ((rc & MII_LAN83C185_MODE_MASK) == MII_LAN83C185_MODE_POWERDOWN) {
		/* set "all capable" mode */
		rc |= MII_LAN83C185_MODE_ALL;
		phy_write(phydev, MII_LAN83C185_SPECIAL_MODES, rc);
	}

	/* reset the phy */
	return genphy_soft_reset(phydev);
}

static int lan911x_config_init(struct phy_device *phydev)
{
	return smsc_phy_ack_interrupt(phydev);
}

/*
 * The LAN87xx suffers from rare absence of the ENERGYON-bit when Ethernet cable
 * plugs in while LAN87xx is in Energy Detect Power-Down mode. This leads to
 * unstable detection of plugging in Ethernet cable.
 * This workaround disables Energy Detect Power-Down mode and waiting for
 * response on link pulses to detect presence of plugged Ethernet cable.
 * The Energy Detect Power-Down mode is enabled again in the end of procedure to
 * save approximately 220 mW of power if cable is unplugged.
 */
static int lan87xx_read_status(struct phy_device *phydev)
{
	struct smsc_phy_priv *priv = phydev->priv;

	int err = genphy_read_status(phydev);

	if (!phydev->link && priv->energy_enable) {
		int i;

		/* Disable EDPD to wake up PHY */
		int rc = phy_read(phydev, MII_LAN83C185_CTRL_STATUS);
		if (rc < 0)
			return rc;

		rc = phy_write(phydev, MII_LAN83C185_CTRL_STATUS,
			       rc & ~MII_LAN83C185_EDPWRDOWN);
		if (rc < 0)
			return rc;

		/* Wait max 640 ms to detect energy */
		for (i = 0; i < 64; i++) {
			/* Sleep to allow link test pulses to be sent */
			msleep(10);
			rc = phy_read(phydev, MII_LAN83C185_CTRL_STATUS);
			if (rc < 0)
				return rc;
			if (rc & MII_LAN83C185_ENERGYON)
				break;
		}

		/* Re-enable EDPD */
		rc = phy_read(phydev, MII_LAN83C185_CTRL_STATUS);
		if (rc < 0)
			return rc;

		rc = phy_write(phydev, MII_LAN83C185_CTRL_STATUS,
			       rc | MII_LAN83C185_EDPWRDOWN);
		if (rc < 0)
			return rc;
	}

	return err;
}

static int smsc_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(smsc_hw_stats);
}

static void smsc_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smsc_hw_stats); i++) {
		memcpy(data + i * ETH_GSTRING_LEN,
		       smsc_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

#ifndef UINT64_MAX
#define UINT64_MAX              (u64)(~((u64)0))
#endif
static u64 smsc_get_stat(struct phy_device *phydev, int i)
{
	struct smsc_hw_stat stat = smsc_hw_stats[i];
	int val;
	u64 ret;

	val = phy_read(phydev, stat.reg);
	if (val < 0)
		ret = UINT64_MAX;
	else
		ret = val;

	return ret;
}

static void smsc_get_stats(struct phy_device *phydev,
			   struct ethtool_stats *stats, u64 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smsc_hw_stats); i++)
		data[i] = smsc_get_stat(phydev, i);
}

static int smsc_phy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct device_node *of_node = dev->of_node;
	struct smsc_phy_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->energy_enable = true;

	if (of_property_read_bool(of_node, "smsc,disable-energy-detect"))
		priv->energy_enable = false;

	phydev->priv = priv;

	return 0;
}

static struct phy_driver smsc_phy_driver[] = {
{
	.phy_id		= 0x0007c0a0, /* OUI=0x00800f, Model#=0x0a */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN83C185",

	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,

	.probe		= smsc_phy_probe,

	/* basic functions */
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.ack_interrupt	= smsc_phy_ack_interrupt,
	.config_intr	= smsc_phy_config_intr,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c0b0, /* OUI=0x00800f, Model#=0x0b */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8187",

	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,

	.probe		= smsc_phy_probe,

	/* basic functions */
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.ack_interrupt	= smsc_phy_ack_interrupt,
	.config_intr	= smsc_phy_config_intr,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c0c0, /* OUI=0x00800f, Model#=0x0c */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8700",

	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,

	.probe		= smsc_phy_probe,

	/* basic functions */
	.config_aneg	= genphy_config_aneg,
	.read_status	= lan87xx_read_status,
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.ack_interrupt	= smsc_phy_ack_interrupt,
	.config_intr	= smsc_phy_config_intr,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c0d0, /* OUI=0x00800f, Model#=0x0d */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN911x Internal PHY",

	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,

	.probe		= smsc_phy_probe,

	/* basic functions */
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.config_init	= lan911x_config_init,

	/* IRQ related */
	.ack_interrupt	= smsc_phy_ack_interrupt,
	.config_intr	= smsc_phy_config_intr,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c0f0, /* OUI=0x00800f, Model#=0x0f */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8710/LAN8720",

	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,

	.probe		= smsc_phy_probe,

	/* basic functions */
	.config_aneg	= genphy_config_aneg,
	.read_status	= lan87xx_read_status,
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.ack_interrupt	= smsc_phy_ack_interrupt,
	.config_intr	= smsc_phy_config_intr,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c110,
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8740",

	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,

	.probe		= smsc_phy_probe,

	/* basic functions */
	.config_aneg	= genphy_config_aneg,
	.read_status	= lan87xx_read_status,
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.ack_interrupt	= smsc_phy_ack_interrupt,
	.config_intr	= smsc_phy_config_intr,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
} };

module_phy_driver(smsc_phy_driver);

MODULE_DESCRIPTION("SMSC PHY driver");
MODULE_AUTHOR("Herbert Valerio Riedel");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused smsc_tbl[] = {
	{ 0x0007c0a0, 0xfffffff0 },
	{ 0x0007c0b0, 0xfffffff0 },
	{ 0x0007c0c0, 0xfffffff0 },
	{ 0x0007c0d0, 0xfffffff0 },
	{ 0x0007c0f0, 0xfffffff0 },
	{ 0x0007c110, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, smsc_tbl);
