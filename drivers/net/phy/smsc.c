// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/smsc.c
 *
 * Driver for SMSC PHYs
 *
 * Author: Herbert Valerio Riedel
 *
 * Copyright (c) 2006 Herbert Valerio Riedel <hvr@gnu.org>
 *
 * Support added for SMSC LAN8187 and LAN8700 by steve.glendinning@shawell.net
 *
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/smscphy.h>

/* Vendor-specific PHY Definitions */
/* EDPD NLP / crossover time configuration */
#define PHY_EDPD_CONFIG			16
#define PHY_EDPD_CONFIG_EXT_CROSSOVER_	0x0001

/* Control/Status Indication Register */
#define SPECIAL_CTRL_STS		27
#define SPECIAL_CTRL_STS_OVRRD_AMDIX_	0x8000
#define SPECIAL_CTRL_STS_AMDIX_ENABLE_	0x4000
#define SPECIAL_CTRL_STS_AMDIX_STATE_	0x2000

struct smsc_hw_stat {
	const char *string;
	u8 reg;
	u8 bits;
};

static struct smsc_hw_stat smsc_hw_stats[] = {
	{ "phy_symbol_errors", 26, 16},
};

struct smsc_phy_priv {
	u16 intmask;
	bool energy_enable;
	struct clk *refclk;
};

static int smsc_phy_ack_interrupt(struct phy_device *phydev)
{
	int rc = phy_read(phydev, MII_LAN83C185_ISF);

	return rc < 0 ? rc : 0;
}

static int smsc_phy_config_intr(struct phy_device *phydev)
{
	struct smsc_phy_priv *priv = phydev->priv;
	int rc;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		rc = smsc_phy_ack_interrupt(phydev);
		if (rc)
			return rc;

		priv->intmask = MII_LAN83C185_ISF_INT4 | MII_LAN83C185_ISF_INT6;
		if (priv->energy_enable)
			priv->intmask |= MII_LAN83C185_ISF_INT7;

		rc = phy_write(phydev, MII_LAN83C185_IM, priv->intmask);
	} else {
		priv->intmask = 0;

		rc = phy_write(phydev, MII_LAN83C185_IM, 0);
		if (rc)
			return rc;

		rc = smsc_phy_ack_interrupt(phydev);
	}

	return rc < 0 ? rc : 0;
}

static irqreturn_t smsc_phy_handle_interrupt(struct phy_device *phydev)
{
	struct smsc_phy_priv *priv = phydev->priv;
	int irq_status;

	irq_status = phy_read(phydev, MII_LAN83C185_ISF);
	if (irq_status < 0) {
		if (irq_status != -ENODEV)
			phy_error(phydev);

		return IRQ_NONE;
	}

	if (!(irq_status & priv->intmask))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int smsc_phy_config_init(struct phy_device *phydev)
{
	struct smsc_phy_priv *priv = phydev->priv;
	int rc;

	if (!priv->energy_enable || phydev->irq != PHY_POLL)
		return 0;

	rc = phy_read(phydev, MII_LAN83C185_CTRL_STATUS);

	if (rc < 0)
		return rc;

	/* Enable energy detect mode for this SMSC Transceivers */
	rc = phy_write(phydev, MII_LAN83C185_CTRL_STATUS,
		       rc | MII_LAN83C185_EDPWRDOWN);
	return rc;
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

static int lan87xx_config_aneg(struct phy_device *phydev)
{
	int rc;
	int val;

	switch (phydev->mdix_ctrl) {
	case ETH_TP_MDI:
		val = SPECIAL_CTRL_STS_OVRRD_AMDIX_;
		break;
	case ETH_TP_MDI_X:
		val = SPECIAL_CTRL_STS_OVRRD_AMDIX_ |
			SPECIAL_CTRL_STS_AMDIX_STATE_;
		break;
	case ETH_TP_MDI_AUTO:
		val = SPECIAL_CTRL_STS_AMDIX_ENABLE_;
		break;
	default:
		return genphy_config_aneg(phydev);
	}

	rc = phy_read(phydev, SPECIAL_CTRL_STS);
	if (rc < 0)
		return rc;

	rc &= ~(SPECIAL_CTRL_STS_OVRRD_AMDIX_ |
		SPECIAL_CTRL_STS_AMDIX_ENABLE_ |
		SPECIAL_CTRL_STS_AMDIX_STATE_);
	rc |= val;
	phy_write(phydev, SPECIAL_CTRL_STS, rc);

	phydev->mdix = phydev->mdix_ctrl;
	return genphy_config_aneg(phydev);
}

static int lan95xx_config_aneg_ext(struct phy_device *phydev)
{
	int rc;

	if (phydev->phy_id != 0x0007c0f0) /* not (LAN9500A or LAN9505A) */
		return lan87xx_config_aneg(phydev);

	/* Extend Manual AutoMDIX timer */
	rc = phy_read(phydev, PHY_EDPD_CONFIG);
	if (rc < 0)
		return rc;

	rc |= PHY_EDPD_CONFIG_EXT_CROSSOVER_;
	phy_write(phydev, PHY_EDPD_CONFIG, rc);
	return lan87xx_config_aneg(phydev);
}

/*
 * The LAN87xx suffers from rare absence of the ENERGYON-bit when Ethernet cable
 * plugs in while LAN87xx is in Energy Detect Power-Down mode. This leads to
 * unstable detection of plugging in Ethernet cable.
 * This workaround disables Energy Detect Power-Down mode and waiting for
 * response on link pulses to detect presence of plugged Ethernet cable.
 * The Energy Detect Power-Down mode is enabled again in the end of procedure to
 * save approximately 220 mW of power if cable is unplugged.
 * The workaround is only applicable to poll mode. Energy Detect Power-Down may
 * not be used in interrupt mode lest link change detection becomes unreliable.
 */
static int lan87xx_read_status(struct phy_device *phydev)
{
	struct smsc_phy_priv *priv = phydev->priv;

	int err = genphy_read_status(phydev);

	if (!phydev->link && priv->energy_enable && phydev->irq == PHY_POLL) {
		/* Disable EDPD to wake up PHY */
		int rc = phy_read(phydev, MII_LAN83C185_CTRL_STATUS);
		if (rc < 0)
			return rc;

		rc = phy_write(phydev, MII_LAN83C185_CTRL_STATUS,
			       rc & ~MII_LAN83C185_EDPWRDOWN);
		if (rc < 0)
			return rc;

		/* Wait max 640 ms to detect energy and the timeout is not
		 * an actual error.
		 */
		read_poll_timeout(phy_read, rc,
				  rc & MII_LAN83C185_ENERGYON || rc < 0,
				  10000, 640000, true, phydev,
				  MII_LAN83C185_CTRL_STATUS);
		if (rc < 0)
			return rc;

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
		strncpy(data + i * ETH_GSTRING_LEN,
		       smsc_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 smsc_get_stat(struct phy_device *phydev, int i)
{
	struct smsc_hw_stat stat = smsc_hw_stats[i];
	int val;
	u64 ret;

	val = phy_read(phydev, stat.reg);
	if (val < 0)
		ret = U64_MAX;
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

static void smsc_phy_remove(struct phy_device *phydev)
{
	struct smsc_phy_priv *priv = phydev->priv;

	clk_disable_unprepare(priv->refclk);
	clk_put(priv->refclk);
}

static int smsc_phy_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct device_node *of_node = dev->of_node;
	struct smsc_phy_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->energy_enable = true;

	if (of_property_read_bool(of_node, "smsc,disable-energy-detect"))
		priv->energy_enable = false;

	phydev->priv = priv;

	/* Make clk optional to keep DTB backward compatibility. */
	priv->refclk = clk_get_optional(dev, NULL);
	if (IS_ERR(priv->refclk))
		return dev_err_probe(dev, PTR_ERR(priv->refclk),
				     "Failed to request clock\n");

	ret = clk_prepare_enable(priv->refclk);
	if (ret)
		return ret;

	ret = clk_set_rate(priv->refclk, 50 * 1000 * 1000);
	if (ret) {
		clk_disable_unprepare(priv->refclk);
		return ret;
	}

	return 0;
}

static struct phy_driver smsc_phy_driver[] = {
{
	.phy_id		= 0x0007c0a0, /* OUI=0x00800f, Model#=0x0a */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN83C185",

	/* PHY_BASIC_FEATURES */

	.probe		= smsc_phy_probe,

	/* basic functions */
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c0b0, /* OUI=0x00800f, Model#=0x0b */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8187",

	/* PHY_BASIC_FEATURES */

	.probe		= smsc_phy_probe,

	/* basic functions */
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	/* This covers internal PHY (phy_id: 0x0007C0C3) for
	 * LAN9500 (PID: 0x9500), LAN9514 (PID: 0xec00), LAN9505 (PID: 0x9505)
	 */
	.phy_id		= 0x0007c0c0, /* OUI=0x00800f, Model#=0x0c */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8700",

	/* PHY_BASIC_FEATURES */

	.probe		= smsc_phy_probe,

	/* basic functions */
	.read_status	= lan87xx_read_status,
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,
	.config_aneg	= lan87xx_config_aneg,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

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

	/* PHY_BASIC_FEATURES */

	.probe		= smsc_phy_probe,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	/* This covers internal PHY (phy_id: 0x0007C0F0) for
	 * LAN9500A (PID: 0x9E00), LAN9505A (PID: 0x9E01)
	 */
	.phy_id		= 0x0007c0f0, /* OUI=0x00800f, Model#=0x0f */
	.phy_id_mask	= 0xfffffff0,
	.name		= "SMSC LAN8710/LAN8720",

	/* PHY_BASIC_FEATURES */

	.probe		= smsc_phy_probe,
	.remove		= smsc_phy_remove,

	/* basic functions */
	.read_status	= lan87xx_read_status,
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,
	.config_aneg	= lan95xx_config_aneg_ext,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

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

	/* PHY_BASIC_FEATURES */
	.flags		= PHY_RST_AFTER_CLK_EN,

	.probe		= smsc_phy_probe,

	/* basic functions */
	.read_status	= lan87xx_read_status,
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

	/* Statistics */
	.get_sset_count = smsc_get_sset_count,
	.get_strings	= smsc_get_strings,
	.get_stats	= smsc_get_stats,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= 0x0007c130,	/* 0x0007c130 and 0x0007c131 */
	/* This mask (0xfffffff2) is to differentiate from
	 * LAN88xx (phy_id 0x0007c132)
	 * and allows future phy_id revisions.
	 */
	.phy_id_mask	= 0xfffffff2,
	.name		= "Microchip LAN8742",

	/* PHY_BASIC_FEATURES */
	.flags		= PHY_RST_AFTER_CLK_EN,

	.probe		= smsc_phy_probe,

	/* basic functions */
	.read_status	= lan87xx_read_status,
	.config_init	= smsc_phy_config_init,
	.soft_reset	= smsc_phy_reset,

	/* IRQ related */
	.config_intr	= smsc_phy_config_intr,
	.handle_interrupt = smsc_phy_handle_interrupt,

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
	{ 0x0007c130, 0xfffffff2 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, smsc_tbl);
