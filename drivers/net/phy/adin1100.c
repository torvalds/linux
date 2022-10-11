// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 *  Driver for Analog Devices Industrial Ethernet T1L PHYs
 *
 * Copyright 2020 Analog Devices Inc.
 */
#include <linux/kernel.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/property.h>

#define PHY_ID_ADIN1100				0x0283bc81
#define PHY_ID_ADIN1110				0x0283bc91
#define PHY_ID_ADIN2111				0x0283bca1

#define ADIN_FORCED_MODE			0x8000
#define   ADIN_FORCED_MODE_EN			BIT(0)

#define ADIN_CRSM_SFT_RST			0x8810
#define   ADIN_CRSM_SFT_RST_EN			BIT(0)

#define ADIN_CRSM_SFT_PD_CNTRL			0x8812
#define   ADIN_CRSM_SFT_PD_CNTRL_EN		BIT(0)

#define ADIN_AN_PHY_INST_STATUS			0x8030
#define   ADIN_IS_CFG_SLV			BIT(2)
#define   ADIN_IS_CFG_MST			BIT(3)

#define ADIN_CRSM_STAT				0x8818
#define   ADIN_CRSM_SFT_PD_RDY			BIT(1)
#define   ADIN_CRSM_SYS_RDY			BIT(0)

#define ADIN_MSE_VAL				0x830B

#define ADIN_SQI_MAX	7

struct adin_mse_sqi_range {
	u16 start;
	u16 end;
};

static const struct adin_mse_sqi_range adin_mse_sqi_map[] = {
	{ 0x0A74, 0xFFFF },
	{ 0x084E, 0x0A74 },
	{ 0x0698, 0x084E },
	{ 0x053D, 0x0698 },
	{ 0x0429, 0x053D },
	{ 0x034E, 0x0429 },
	{ 0x02A0, 0x034E },
	{ 0x0000, 0x02A0 },
};

/**
 * struct adin_priv - ADIN PHY driver private data
 * @tx_level_2v4_able:		set if the PHY supports 2.4V TX levels (10BASE-T1L)
 * @tx_level_2v4:		set if the PHY requests 2.4V TX levels (10BASE-T1L)
 * @tx_level_prop_present:	set if the TX level is specified in DT
 */
struct adin_priv {
	unsigned int		tx_level_2v4_able:1;
	unsigned int		tx_level_2v4:1;
	unsigned int		tx_level_prop_present:1;
};

static int adin_read_status(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_read_status(phydev);
	if (ret)
		return ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_AN, ADIN_AN_PHY_INST_STATUS);
	if (ret < 0)
		return ret;

	if (ret & ADIN_IS_CFG_SLV)
		phydev->master_slave_state = MASTER_SLAVE_STATE_SLAVE;

	if (ret & ADIN_IS_CFG_MST)
		phydev->master_slave_state = MASTER_SLAVE_STATE_MASTER;

	return 0;
}

static int adin_config_aneg(struct phy_device *phydev)
{
	struct adin_priv *priv = phydev->priv;
	int ret;

	if (phydev->autoneg == AUTONEG_DISABLE) {
		ret = genphy_c45_pma_setup_forced(phydev);
		if (ret < 0)
			return ret;

		if (priv->tx_level_prop_present && priv->tx_level_2v4)
			ret = phy_set_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_B10L_PMA_CTRL,
					       MDIO_PMA_10T1L_CTRL_2V4_EN);
		else
			ret = phy_clear_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_B10L_PMA_CTRL,
						 MDIO_PMA_10T1L_CTRL_2V4_EN);
		if (ret < 0)
			return ret;

		/* Force PHY to use above configurations */
		return phy_set_bits_mmd(phydev, MDIO_MMD_AN, ADIN_FORCED_MODE, ADIN_FORCED_MODE_EN);
	}

	ret = phy_clear_bits_mmd(phydev, MDIO_MMD_AN, ADIN_FORCED_MODE, ADIN_FORCED_MODE_EN);
	if (ret < 0)
		return ret;

	/* Request increased transmit level from LP. */
	if (priv->tx_level_prop_present && priv->tx_level_2v4) {
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_AN, MDIO_AN_T1_ADV_H,
				       MDIO_AN_T1_ADV_H_10L_TX_HI |
				       MDIO_AN_T1_ADV_H_10L_TX_HI_REQ);
		if (ret < 0)
			return ret;
	}

	/* Disable 2.4 Vpp transmit level. */
	if ((priv->tx_level_prop_present && !priv->tx_level_2v4) || !priv->tx_level_2v4_able) {
		ret = phy_clear_bits_mmd(phydev, MDIO_MMD_AN, MDIO_AN_T1_ADV_H,
					 MDIO_AN_T1_ADV_H_10L_TX_HI |
					 MDIO_AN_T1_ADV_H_10L_TX_HI_REQ);
		if (ret < 0)
			return ret;
	}

	return genphy_c45_config_aneg(phydev);
}

static int adin_set_powerdown_mode(struct phy_device *phydev, bool en)
{
	int ret;
	int val;

	val = en ? ADIN_CRSM_SFT_PD_CNTRL_EN : 0;
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
			    ADIN_CRSM_SFT_PD_CNTRL, val);
	if (ret < 0)
		return ret;

	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1, ADIN_CRSM_STAT, ret,
					 (ret & ADIN_CRSM_SFT_PD_RDY) == val,
					 1000, 30000, true);
}

static int adin_suspend(struct phy_device *phydev)
{
	return adin_set_powerdown_mode(phydev, true);
}

static int adin_resume(struct phy_device *phydev)
{
	return adin_set_powerdown_mode(phydev, false);
}

static int adin_set_loopback(struct phy_device *phydev, bool enable)
{
	if (enable)
		return phy_set_bits_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_10T1L_CTRL,
					BMCR_LOOPBACK);

	/* PCS loopback (according to 10BASE-T1L spec) */
	return phy_clear_bits_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_10T1L_CTRL,
				 BMCR_LOOPBACK);
}

static int adin_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, ADIN_CRSM_SFT_RST, ADIN_CRSM_SFT_RST_EN);
	if (ret < 0)
		return ret;

	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1, ADIN_CRSM_STAT, ret,
					 (ret & ADIN_CRSM_SYS_RDY),
					 10000, 30000, true);
}

static int adin_get_features(struct phy_device *phydev)
{
	struct adin_priv *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	int ret;
	u8 val;

	ret = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_10T1L_STAT);
	if (ret < 0)
		return ret;

	/* This depends on the voltage level from the power source */
	priv->tx_level_2v4_able = !!(ret & MDIO_PMA_10T1L_STAT_2V4_ABLE);

	phydev_dbg(phydev, "PHY supports 2.4V TX level: %s\n",
		   priv->tx_level_2v4_able ? "yes" : "no");

	priv->tx_level_prop_present = device_property_present(dev, "phy-10base-t1l-2.4vpp");
	if (priv->tx_level_prop_present) {
		ret = device_property_read_u8(dev, "phy-10base-t1l-2.4vpp", &val);
		if (ret < 0)
			return ret;

		priv->tx_level_2v4 = val;
		if (!priv->tx_level_2v4 && priv->tx_level_2v4_able)
			phydev_info(phydev,
				    "PHY supports 2.4V TX level, but disabled via config\n");
	}

	linkmode_set_bit_array(phy_basic_ports_array, ARRAY_SIZE(phy_basic_ports_array),
			       phydev->supported);

	return genphy_c45_pma_read_abilities(phydev);
}

static int adin_get_sqi(struct phy_device *phydev)
{
	u16 mse_val;
	int sqi;
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_STAT1);
	if (ret < 0)
		return ret;
	else if (!(ret & MDIO_STAT1_LSTATUS))
		return 0;

	ret = phy_read_mmd(phydev, MDIO_STAT1, ADIN_MSE_VAL);
	if (ret < 0)
		return ret;

	mse_val = 0xFFFF & ret;
	for (sqi = 0; sqi < ARRAY_SIZE(adin_mse_sqi_map); sqi++) {
		if (mse_val >= adin_mse_sqi_map[sqi].start && mse_val <= adin_mse_sqi_map[sqi].end)
			return sqi;
	}

	return -EINVAL;
}

static int adin_get_sqi_max(struct phy_device *phydev)
{
	return ADIN_SQI_MAX;
}

static int adin_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct adin_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

static struct phy_driver adin_driver[] = {
	{
		.phy_id			= PHY_ID_ADIN1100,
		.phy_id_mask		= 0xffffffcf,
		.name			= "ADIN1100",
		.get_features		= adin_get_features,
		.soft_reset		= adin_soft_reset,
		.probe			= adin_probe,
		.config_aneg		= adin_config_aneg,
		.read_status		= adin_read_status,
		.set_loopback		= adin_set_loopback,
		.suspend		= adin_suspend,
		.resume			= adin_resume,
		.get_sqi		= adin_get_sqi,
		.get_sqi_max		= adin_get_sqi_max,
	},
};

module_phy_driver(adin_driver);

static struct mdio_device_id __maybe_unused adin_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_ADIN1100) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_ADIN1110) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_ADIN2111) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, adin_tbl);
MODULE_DESCRIPTION("Analog Devices Industrial Ethernet T1L PHY driver");
MODULE_LICENSE("Dual BSD/GPL");
