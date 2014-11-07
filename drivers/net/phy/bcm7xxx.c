/*
 * Broadcom BCM7xxx internal transceivers support.
 *
 * Copyright (C) 2014, Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/phy.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/brcmphy.h>
#include <linux/mdio.h>

/* Broadcom BCM7xxx internal PHY registers */
#define MII_BCM7XXX_CHANNEL_WIDTH	0x2000

/* 40nm only register definitions */
#define MII_BCM7XXX_100TX_AUX_CTL	0x10
#define MII_BCM7XXX_100TX_FALSE_CAR	0x13
#define MII_BCM7XXX_100TX_DISC		0x14
#define MII_BCM7XXX_AUX_MODE		0x1d
#define  MII_BCM7XX_64CLK_MDIO		BIT(12)
#define MII_BCM7XXX_CORE_BASE1E		0x1e
#define MII_BCM7XXX_TEST		0x1f
#define  MII_BCM7XXX_SHD_MODE_2		BIT(2)

/* 28nm only register definitions */
#define MISC_ADDR(base, channel)	base, channel

#define DSP_TAP10			MISC_ADDR(0x0a, 0)
#define PLL_PLLCTRL_1			MISC_ADDR(0x32, 1)
#define PLL_PLLCTRL_2			MISC_ADDR(0x32, 2)
#define PLL_PLLCTRL_4			MISC_ADDR(0x33, 0)

#define AFE_RXCONFIG_0			MISC_ADDR(0x38, 0)
#define AFE_RXCONFIG_1			MISC_ADDR(0x38, 1)
#define AFE_RX_LP_COUNTER		MISC_ADDR(0x38, 3)
#define AFE_TX_CONFIG			MISC_ADDR(0x39, 0)
#define AFE_HPF_TRIM_OTHERS		MISC_ADDR(0x3a, 0)

#define CORE_EXPB0			0xb0

static int bcm7445_config_init(struct phy_device *phydev)
{
	int ret;
	const struct bcm7445_regs {
		int reg;
		u16 value;
	} bcm7445_regs_cfg[] = {
		/* increases ADC latency by 24ns */
		{ MII_BCM54XX_EXP_SEL, 0x0038 },
		{ MII_BCM54XX_EXP_DATA, 0xAB95 },
		/* increases internal 1V LDO voltage by 5% */
		{ MII_BCM54XX_EXP_SEL, 0x2038 },
		{ MII_BCM54XX_EXP_DATA, 0xBB22 },
		/* reduce RX low pass filter corner frequency */
		{ MII_BCM54XX_EXP_SEL, 0x6038 },
		{ MII_BCM54XX_EXP_DATA, 0xFFC5 },
		/* reduce RX high pass filter corner frequency */
		{ MII_BCM54XX_EXP_SEL, 0x003a },
		{ MII_BCM54XX_EXP_DATA, 0x2002 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(bcm7445_regs_cfg); i++) {
		ret = phy_write(phydev,
				bcm7445_regs_cfg[i].reg,
				bcm7445_regs_cfg[i].value);
		if (ret)
			return ret;
	}

	return 0;
}

static void phy_write_exp(struct phy_device *phydev,
					u16 reg, u16 value)
{
	phy_write(phydev, MII_BCM54XX_EXP_SEL, MII_BCM54XX_EXP_SEL_ER | reg);
	phy_write(phydev, MII_BCM54XX_EXP_DATA, value);
}

static void phy_write_misc(struct phy_device *phydev,
					u16 reg, u16 chl, u16 value)
{
	int tmp;

	phy_write(phydev, MII_BCM54XX_AUX_CTL, MII_BCM54XX_AUXCTL_SHDWSEL_MISC);

	tmp = phy_read(phydev, MII_BCM54XX_AUX_CTL);
	tmp |= MII_BCM54XX_AUXCTL_ACTL_SMDSP_ENA;
	phy_write(phydev, MII_BCM54XX_AUX_CTL, tmp);

	tmp = (chl * MII_BCM7XXX_CHANNEL_WIDTH) | reg;
	phy_write(phydev, MII_BCM54XX_EXP_SEL, tmp);

	phy_write(phydev, MII_BCM54XX_EXP_DATA, value);
}

static int bcm7xxx_28nm_afe_config_init(struct phy_device *phydev)
{
	/* Increase VCO range to prevent unlocking problem of PLL at low
	 * temp
	 */
	phy_write_misc(phydev, PLL_PLLCTRL_1, 0x0048);

	/* Change Ki to 011 */
	phy_write_misc(phydev, PLL_PLLCTRL_2, 0x021b);

	/* Disable loading of TVCO buffer to bandgap, set bandgap trim
	 * to 111
	 */
	phy_write_misc(phydev, PLL_PLLCTRL_4, 0x0e20);

	/* Adjust bias current trim by -3 */
	phy_write_misc(phydev, DSP_TAP10, 0x690b);

	/* Switch to CORE_BASE1E */
	phy_write(phydev, MII_BCM7XXX_CORE_BASE1E, 0xd);

	/* Reset R_CAL/RC_CAL Engine */
	phy_write_exp(phydev, CORE_EXPB0, 0x0010);

	/* Disable Reset R_CAL/RC_CAL Engine */
	phy_write_exp(phydev, CORE_EXPB0, 0x0000);

	/* write AFE_RXCONFIG_0 */
	phy_write_misc(phydev, AFE_RXCONFIG_0, 0xeb19);

	/* write AFE_RXCONFIG_1 */
	phy_write_misc(phydev, AFE_RXCONFIG_1, 0x9a3f);

	/* write AFE_RX_LP_COUNTER */
	phy_write_misc(phydev, AFE_RX_LP_COUNTER, 0x7fc0);

	/* write AFE_HPF_TRIM_OTHERS */
	phy_write_misc(phydev, AFE_HPF_TRIM_OTHERS, 0x000b);

	/* write AFTE_TX_CONFIG */
	phy_write_misc(phydev, AFE_TX_CONFIG, 0x0800);

	return 0;
}

static int bcm7xxx_apd_enable(struct phy_device *phydev)
{
	int val;

	/* Enable powering down of the DLL during auto-power down */
	val = bcm54xx_shadow_read(phydev, BCM54XX_SHD_SCR3);
	if (val < 0)
		return val;

	val |= BCM54XX_SHD_SCR3_DLLAPD_DIS;
	bcm54xx_shadow_write(phydev, BCM54XX_SHD_SCR3, val);

	/* Enable auto-power down */
	val = bcm54xx_shadow_read(phydev, BCM54XX_SHD_APD);
	if (val < 0)
		return val;

	val |= BCM54XX_SHD_APD_EN;
	return bcm54xx_shadow_write(phydev, BCM54XX_SHD_APD, val);
}

static int bcm7xxx_eee_enable(struct phy_device *phydev)
{
	int val;

	val = phy_read_mmd_indirect(phydev, BRCM_CL45VEN_EEE_CONTROL,
				    MDIO_MMD_AN, phydev->addr);
	if (val < 0)
		return val;

	/* Enable general EEE feature at the PHY level */
	val |= LPI_FEATURE_EN | LPI_FEATURE_EN_DIG1000X;

	phy_write_mmd_indirect(phydev, BRCM_CL45VEN_EEE_CONTROL,
			       MDIO_MMD_AN, phydev->addr, val);

	/* Advertise supported modes */
	val = phy_read_mmd_indirect(phydev, MDIO_AN_EEE_ADV,
				    MDIO_MMD_AN, phydev->addr);

	val |= (MDIO_AN_EEE_ADV_100TX | MDIO_AN_EEE_ADV_1000T);
	phy_write_mmd_indirect(phydev, MDIO_AN_EEE_ADV,
			       MDIO_MMD_AN, phydev->addr, val);

	return 0;
}

static int bcm7xxx_28nm_config_init(struct phy_device *phydev)
{
	u8 rev = PHY_BRCM_7XXX_REV(phydev->dev_flags);
	u8 patch = PHY_BRCM_7XXX_PATCH(phydev->dev_flags);
	int ret = 0;

	dev_info(&phydev->dev, "PHY revision: 0x%02x, patch: %d\n", rev, patch);

	switch (rev) {
	case 0xa0:
	case 0xb0:
		ret = bcm7445_config_init(phydev);
		break;
	default:
		ret = bcm7xxx_28nm_afe_config_init(phydev);
		break;
	}

	if (ret)
		return ret;

	ret = bcm7xxx_eee_enable(phydev);
	if (ret)
		return ret;

	return bcm7xxx_apd_enable(phydev);
}

static int bcm7xxx_28nm_resume(struct phy_device *phydev)
{
	int ret;

	/* Re-apply workarounds coming out suspend/resume */
	ret = bcm7xxx_28nm_config_init(phydev);
	if (ret)
		return ret;

	/* 28nm Gigabit PHYs come out of reset without any half-duplex
	 * or "hub" compliant advertised mode, fix that. This does not
	 * cause any problems with the PHY library since genphy_config_aneg()
	 * gracefully handles auto-negotiated and forced modes.
	 */
	return genphy_config_aneg(phydev);
}

static int phy_set_clr_bits(struct phy_device *dev, int location,
					int set_mask, int clr_mask)
{
	int v, ret;

	v = phy_read(dev, location);
	if (v < 0)
		return v;

	v &= ~clr_mask;
	v |= set_mask;

	ret = phy_write(dev, location, v);
	if (ret < 0)
		return ret;

	return v;
}

static int bcm7xxx_config_init(struct phy_device *phydev)
{
	int ret;

	/* Enable 64 clock MDIO */
	phy_write(phydev, MII_BCM7XXX_AUX_MODE, MII_BCM7XX_64CLK_MDIO);
	phy_read(phydev, MII_BCM7XXX_AUX_MODE);

	/* Workaround only required for 100Mbits/sec capable PHYs */
	if (phydev->supported & PHY_GBIT_FEATURES)
		return 0;

	/* set shadow mode 2 */
	ret = phy_set_clr_bits(phydev, MII_BCM7XXX_TEST,
			MII_BCM7XXX_SHD_MODE_2, MII_BCM7XXX_SHD_MODE_2);
	if (ret < 0)
		return ret;

	/* set iddq_clkbias */
	phy_write(phydev, MII_BCM7XXX_100TX_DISC, 0x0F00);
	udelay(10);

	/* reset iddq_clkbias */
	phy_write(phydev, MII_BCM7XXX_100TX_DISC, 0x0C00);

	phy_write(phydev, MII_BCM7XXX_100TX_FALSE_CAR, 0x7555);

	/* reset shadow mode 2 */
	ret = phy_set_clr_bits(phydev, MII_BCM7XXX_TEST, MII_BCM7XXX_SHD_MODE_2, 0);
	if (ret < 0)
		return ret;

	return 0;
}

/* Workaround for putting the PHY in IDDQ mode, required
 * for all BCM7XXX 40nm and 65nm PHYs
 */
static int bcm7xxx_suspend(struct phy_device *phydev)
{
	int ret;
	const struct bcm7xxx_regs {
		int reg;
		u16 value;
	} bcm7xxx_suspend_cfg[] = {
		{ MII_BCM7XXX_TEST, 0x008b },
		{ MII_BCM7XXX_100TX_AUX_CTL, 0x01c0 },
		{ MII_BCM7XXX_100TX_DISC, 0x7000 },
		{ MII_BCM7XXX_TEST, 0x000f },
		{ MII_BCM7XXX_100TX_AUX_CTL, 0x20d0 },
		{ MII_BCM7XXX_TEST, 0x000b },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(bcm7xxx_suspend_cfg); i++) {
		ret = phy_write(phydev,
				bcm7xxx_suspend_cfg[i].reg,
				bcm7xxx_suspend_cfg[i].value);
		if (ret)
			return ret;
	}

	return 0;
}

static int bcm7xxx_dummy_config_init(struct phy_device *phydev)
{
	return 0;
}

#define BCM7XXX_28NM_GPHY(_oui, _name)					\
{									\
	.phy_id		= (_oui),					\
	.phy_id_mask	= 0xfffffff0,					\
	.name		= _name,					\
	.features	= PHY_GBIT_FEATURES |				\
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,	\
	.flags		= PHY_IS_INTERNAL,				\
	.config_init	= bcm7xxx_28nm_afe_config_init,			\
	.config_aneg	= genphy_config_aneg,				\
	.read_status	= genphy_read_status,				\
	.resume		= bcm7xxx_28nm_resume,				\
	.driver		= { .owner = THIS_MODULE },			\
}

static struct phy_driver bcm7xxx_driver[] = {
	BCM7XXX_28NM_GPHY(PHY_ID_BCM7250, "Broadcom BCM7250"),
	BCM7XXX_28NM_GPHY(PHY_ID_BCM7364, "Broadcom BCM7364"),
	BCM7XXX_28NM_GPHY(PHY_ID_BCM7366, "Broadcom BCM7366"),
	BCM7XXX_28NM_GPHY(PHY_ID_BCM7439, "Broadcom BCM7439"),
	BCM7XXX_28NM_GPHY(PHY_ID_BCM7445, "Broadcom BCM7445"),
{
	.phy_id         = PHY_ID_BCM7425,
	.phy_id_mask    = 0xfffffff0,
	.name           = "Broadcom BCM7425",
	.features       = PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags          = 0,
	.config_init    = bcm7xxx_config_init,
	.config_aneg    = genphy_config_aneg,
	.read_status    = genphy_read_status,
	.suspend        = bcm7xxx_suspend,
	.resume         = bcm7xxx_config_init,
	.driver         = { .owner = THIS_MODULE },
}, {
	.phy_id         = PHY_ID_BCM7429,
	.phy_id_mask    = 0xfffffff0,
	.name           = "Broadcom BCM7429",
	.features       = PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags          = PHY_IS_INTERNAL,
	.config_init    = bcm7xxx_config_init,
	.config_aneg    = genphy_config_aneg,
	.read_status    = genphy_read_status,
	.suspend        = bcm7xxx_suspend,
	.resume         = bcm7xxx_config_init,
	.driver         = { .owner = THIS_MODULE },
}, {
	.phy_id		= PHY_BCM_OUI_4,
	.phy_id_mask	= 0xffff0000,
	.name		= "Broadcom BCM7XXX 40nm",
	.features	= PHY_GBIT_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_IS_INTERNAL,
	.config_init	= bcm7xxx_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.suspend	= bcm7xxx_suspend,
	.resume		= bcm7xxx_config_init,
	.driver		= { .owner = THIS_MODULE },
}, {
	.phy_id		= PHY_BCM_OUI_5,
	.phy_id_mask	= 0xffffff00,
	.name		= "Broadcom BCM7XXX 65nm",
	.features	= PHY_BASIC_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_IS_INTERNAL,
	.config_init	= bcm7xxx_dummy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.suspend	= bcm7xxx_suspend,
	.resume		= bcm7xxx_config_init,
	.driver		= { .owner = THIS_MODULE },
} };

static struct mdio_device_id __maybe_unused bcm7xxx_tbl[] = {
	{ PHY_ID_BCM7250, 0xfffffff0, },
	{ PHY_ID_BCM7364, 0xfffffff0, },
	{ PHY_ID_BCM7366, 0xfffffff0, },
	{ PHY_ID_BCM7425, 0xfffffff0, },
	{ PHY_ID_BCM7429, 0xfffffff0, },
	{ PHY_ID_BCM7439, 0xfffffff0, },
	{ PHY_ID_BCM7445, 0xfffffff0, },
	{ PHY_BCM_OUI_4, 0xffff0000 },
	{ PHY_BCM_OUI_5, 0xffffff00 },
	{ }
};

static int __init bcm7xxx_phy_init(void)
{
	return phy_drivers_register(bcm7xxx_driver,
			ARRAY_SIZE(bcm7xxx_driver));
}

static void __exit bcm7xxx_phy_exit(void)
{
	phy_drivers_unregister(bcm7xxx_driver,
			ARRAY_SIZE(bcm7xxx_driver));
}

module_init(bcm7xxx_phy_init);
module_exit(bcm7xxx_phy_exit);

MODULE_DEVICE_TABLE(mdio, bcm7xxx_tbl);

MODULE_DESCRIPTION("Broadcom BCM7xxx internal PHY driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Broadcom Corporation");
