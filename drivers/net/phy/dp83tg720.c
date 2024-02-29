// SPDX-License-Identifier: GPL-2.0
/* Driver for the Texas Instruments DP83TG720 PHY
 * Copyright (c) 2023 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */
#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>

#define DP83TG720S_PHY_ID			0x2000a284

/* MDIO_MMD_VEND2 registers */
#define DP83TG720S_MII_REG_10			0x10
#define DP83TG720S_STS_MII_INT			BIT(7)
#define DP83TG720S_LINK_STATUS			BIT(0)

#define DP83TG720S_PHY_RESET			0x1f
#define DP83TG720S_HW_RESET			BIT(15)

#define DP83TG720S_RGMII_DELAY_CTRL		0x602
/* In RGMII mode, Enable or disable the internal delay for RXD */
#define DP83TG720S_RGMII_RX_CLK_SEL		BIT(1)
/* In RGMII mode, Enable or disable the internal delay for TXD */
#define DP83TG720S_RGMII_TX_CLK_SEL		BIT(0)

#define DP83TG720S_SQI_REG_1			0x871
#define DP83TG720S_SQI_OUT_WORST		GENMASK(7, 5)
#define DP83TG720S_SQI_OUT			GENMASK(3, 1)

#define DP83TG720_SQI_MAX			7

static int dp83tg720_config_aneg(struct phy_device *phydev)
{
	/* Autoneg is not supported and this PHY supports only one speed.
	 * We need to care only about master/slave configuration if it was
	 * changed by user.
	 */
	return genphy_c45_pma_baset1_setup_master_slave(phydev);
}

static int dp83tg720_read_status(struct phy_device *phydev)
{
	u16 phy_sts;
	int ret;

	phydev->pause = 0;
	phydev->asym_pause = 0;

	/* Most of Clause 45 registers are not present, so we can't use
	 * genphy_c45_read_status() here.
	 */
	phy_sts = phy_read(phydev, DP83TG720S_MII_REG_10);
	phydev->link = !!(phy_sts & DP83TG720S_LINK_STATUS);
	if (!phydev->link) {
		/* According to the "DP83TC81x, DP83TG72x Software
		 * Implementation Guide", the PHY needs to be reset after a
		 * link loss or if no link is created after at least 100ms.
		 *
		 * Currently we are polling with the PHY_STATE_TIME (1000ms)
		 * interval, which is still enough for not automotive use cases.
		 */
		ret = phy_init_hw(phydev);
		if (ret)
			return ret;

		/* After HW reset we need to restore master/slave configuration.
		 */
		ret = dp83tg720_config_aneg(phydev);
		if (ret)
			return ret;

		phydev->speed = SPEED_UNKNOWN;
		phydev->duplex = DUPLEX_UNKNOWN;
	} else {
		/* PMA/PMD control 1 register (Register 1.0) is present, but it
		 * doesn't contain the link speed information.
		 * So genphy_c45_read_pma() can't be used here.
		 */
		ret = genphy_c45_pma_baset1_read_master_slave(phydev);
		if (ret)
			return ret;

		phydev->duplex = DUPLEX_FULL;
		phydev->speed = SPEED_1000;
	}

	return 0;
}

static int dp83tg720_get_sqi(struct phy_device *phydev)
{
	int ret;

	if (!phydev->link)
		return 0;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_SQI_REG_1);
	if (ret < 0)
		return ret;

	return FIELD_GET(DP83TG720S_SQI_OUT, ret);
}

static int dp83tg720_get_sqi_max(struct phy_device *phydev)
{
	return DP83TG720_SQI_MAX;
}

static int dp83tg720_config_rgmii_delay(struct phy_device *phydev)
{
	u16 rgmii_delay_mask;
	u16 rgmii_delay = 0;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		rgmii_delay = 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		rgmii_delay = DP83TG720S_RGMII_RX_CLK_SEL |
				DP83TG720S_RGMII_TX_CLK_SEL;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		rgmii_delay = DP83TG720S_RGMII_RX_CLK_SEL;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rgmii_delay = DP83TG720S_RGMII_TX_CLK_SEL;
		break;
	default:
		return 0;
	}

	rgmii_delay_mask = DP83TG720S_RGMII_RX_CLK_SEL |
		DP83TG720S_RGMII_TX_CLK_SEL;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND2,
			      DP83TG720S_RGMII_DELAY_CTRL, rgmii_delay_mask,
			      rgmii_delay);
}

static int dp83tg720_config_init(struct phy_device *phydev)
{
	int ret;

	/* Software Restart is not enough to recover from a link failure.
	 * Using Hardware Reset instead.
	 */
	ret = phy_write(phydev, DP83TG720S_PHY_RESET, DP83TG720S_HW_RESET);
	if (ret)
		return ret;

	/* Wait until MDC can be used again.
	 * The wait value of one 1ms is documented in "DP83TG720S-Q1 1000BASE-T1
	 * Automotive Ethernet PHY with SGMII and RGMII" datasheet.
	 */
	usleep_range(1000, 2000);

	if (phy_interface_is_rgmii(phydev))
		return dp83tg720_config_rgmii_delay(phydev);

	return 0;
}

static struct phy_driver dp83tg720_driver[] = {
{
	PHY_ID_MATCH_MODEL(DP83TG720S_PHY_ID),
	.name		= "TI DP83TG720S",

	.config_aneg	= dp83tg720_config_aneg,
	.read_status	= dp83tg720_read_status,
	.get_features	= genphy_c45_pma_read_ext_abilities,
	.config_init	= dp83tg720_config_init,
	.get_sqi	= dp83tg720_get_sqi,
	.get_sqi_max	= dp83tg720_get_sqi_max,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
} };
module_phy_driver(dp83tg720_driver);

static struct mdio_device_id __maybe_unused dp83tg720_tbl[] = {
	{ PHY_ID_MATCH_MODEL(DP83TG720S_PHY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, dp83tg720_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83TG720S PHY driver");
MODULE_AUTHOR("Oleksij Rempel <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
