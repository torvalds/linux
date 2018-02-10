/**
 * drivers/net/phy/rockchip.c
 *
 * Driver for ROCKCHIP Ethernet PHYs
 *
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * David Wu <david.wu@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/phy.h>

#define INTERNAL_EPHY_ID			0x1234d400

#define MII_INTERNAL_CTRL_STATUS		17
#define SMI_ADDR_TSTCNTL			20
#define SMI_ADDR_TSTREAD1			21
#define SMI_ADDR_TSTREAD2			22
#define SMI_ADDR_TSTWRITE			23
#define MII_SPECIAL_CONTROL_STATUS		31

#define MII_AUTO_MDIX_EN			BIT(7)
#define MII_MDIX_EN				BIT(6)

#define MII_SPEED_10				BIT(2)
#define MII_SPEED_100				BIT(3)

#define TSTCNTL_RD				(BIT(15) | BIT(10))
#define TSTCNTL_WR				(BIT(14) | BIT(10))

#define TSTMODE_ENABLE				0x400
#define TSTMODE_DISABLE				0x0

#define DSP_REG_BANK_SEL			0
#define WOL_REG_BANK_SEL			BIT(11)
#define BIST_REG_BANK_SEL			GENMASK(12, 11)

#define WR_ADDR_A3CFG                           0x14
#define WR_ADDR_A7CFG				0x18

#define RD_ADDR_A3CFG				(0x14 << 5)
#define RD_ADDR_LPISTA				(12 << 5)

#define A3CFG_100M				0xFC00
#define A3CFG_TAG				0xFFFF

#define PHY_ABNORMAL_THRESHOLD			15

#define ENABLE_PHY_FIXUP_RESET

struct rk_int_phy_priv {
	int restore_reg0;
	int restore_a3_config;
	int force_10m_full_mode;
	int a3_config_set;
	int txrx_counters_done_count;
	int last_speed;
	int last_state;
	int reset;
};

static int rockchip_integrated_phy_init_tstmode(struct phy_device *phydev)
{
	int ret;

	ret = phy_write(phydev, SMI_ADDR_TSTCNTL, TSTMODE_DISABLE);
	if (ret)
		return ret;

	/* Enable access to Analog and DSP register banks */
	ret = phy_write(phydev, SMI_ADDR_TSTCNTL, TSTMODE_ENABLE);
	if (ret)
		return ret;

	ret = phy_write(phydev, SMI_ADDR_TSTCNTL, TSTMODE_DISABLE);
	if (ret)
		return ret;

	return phy_write(phydev, SMI_ADDR_TSTCNTL, TSTMODE_ENABLE);
}

static int rockchip_integrated_phy_analog_init(struct phy_device *phydev)
{
	int ret;

	phydev_dbg(phydev, "%s: %d\n", __func__, __LINE__);

	ret = rockchip_integrated_phy_init_tstmode(phydev);
	if (ret)
		return ret;
	/*
	 * Adjust tx amplitude to make sginal better,
	 * the default value is 0x8.
	 */
	ret = phy_write(phydev, SMI_ADDR_TSTWRITE, 0xB);
	if (ret)
		return ret;
	ret = phy_write(phydev, SMI_ADDR_TSTCNTL, TSTCNTL_WR | WR_ADDR_A7CFG);
	if (ret)
		return ret;

	return ret;
}

static int rockchip_integrated_phy_config_init(struct phy_device *phydev)
{
	int val, ret;

	phydev_dbg(phydev, "%s: %d\n", __func__, __LINE__);

	/*
	 * The auto MIDX has linked problem on some board,
	 * workround to disable auto MDIX.
	 */
	val = phy_read(phydev, MII_INTERNAL_CTRL_STATUS);
	if (val < 0)
		return val;
	val &= ~MII_AUTO_MDIX_EN;
	ret = phy_write(phydev, MII_INTERNAL_CTRL_STATUS, val);
	if (ret)
		return ret;

	return rockchip_integrated_phy_analog_init(phydev);
}

static int rockchip_integrated_phy_probe(struct phy_device *phydev)
{
	struct rk_int_phy_priv *priv;

	phydev_dbg(phydev, "%s: %d\n", __func__, __LINE__);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

static void rockchip_integrated_phy_remove(struct phy_device *phydev)
{
	struct rk_int_phy_priv *priv = phydev->priv;

	phydev_dbg(phydev, "%s: %d\n", __func__, __LINE__);

	kfree(priv);
}

static int rockchip_integrated_phy_adjust_a3_config(struct phy_device *phydev,
						    int value)
{
	int ret, val;
	struct rk_int_phy_priv *priv = phydev->priv;

	phydev_dbg(phydev, "%s: %d: value = %x\n", __func__, __LINE__, value);

	if (value == A3CFG_TAG) {
		ret = phy_write(phydev, SMI_ADDR_TSTCNTL,
				TSTCNTL_RD | RD_ADDR_A3CFG);
		if (ret)
			return ret;
		val = phy_read(phydev, SMI_ADDR_TSTREAD1);
		if (val < 0)
			return val;
		priv->restore_a3_config = val;
		priv->a3_config_set = 1;

		ret = phy_write(phydev, SMI_ADDR_TSTWRITE, A3CFG_100M);
		if (ret)
			return ret;
	} else {
		ret = phy_write(phydev, SMI_ADDR_TSTWRITE, value);
		if (ret)
			return ret;
	}

	ret = phy_write(phydev, SMI_ADDR_TSTCNTL, TSTCNTL_WR | WR_ADDR_A3CFG);
	if (ret)
		return ret;

	return 0;
}

static
void rockchip_integrated_phy_link_change_notify(struct phy_device *phydev)
{
	struct rk_int_phy_priv *priv = phydev->priv;

	/*
	 * If mode switch happens from 10BT to 100BT, all DSP/AFE
	 * registers are set to default values. So any AFE/DSP
	 * registers have to be re-initialized in this case.
	 */
	if (phydev->link &&
	    phydev->speed == SPEED_100 &&
	    priv->last_speed == SPEED_10) {
		int ret = rockchip_integrated_phy_analog_init(phydev);

		if (ret)
			phydev_err(phydev, "rockchip_integrated_phy_analog_init err: %d.\n",
				   ret);
	}

	if (phydev->link)
		priv->last_speed = phydev->speed;
}

static int rockchip_set_polarity(struct phy_device *phydev, int polarity)
{
	int reg, err, val;

	/* get the current settings */
	reg = phy_read(phydev, MII_INTERNAL_CTRL_STATUS);
	if (reg < 0)
		return reg;

	reg &= ~MII_AUTO_MDIX_EN;
	val = reg;
	switch (polarity) {
	case ETH_TP_MDI:
		val &= ~MII_MDIX_EN;
		break;
	case ETH_TP_MDI_X:
		val |= MII_MDIX_EN;
		break;
	case ETH_TP_MDI_AUTO:
	case ETH_TP_MDI_INVALID:
	default:
		return 0;
	}

	if (val != reg) {
		/* Set the new polarity value in the register */
		err = phy_write(phydev, MII_INTERNAL_CTRL_STATUS, val);
		if (err)
			return err;
	}

	return 0;
}

static int rockchip_integrated_phy_config_aneg(struct phy_device *phydev)
{
	int err;

	err = rockchip_set_polarity(phydev, phydev->mdix);
	if (err < 0)
		return err;

	return genphy_config_aneg(phydev);
}

static int rockchip_integrated_phy_fixup_10M(struct phy_device *phydev)
{
	int val, ret;
	struct rk_int_phy_priv *priv = phydev->priv;

	/* link partner does not have auto-negotiation ability
	 * setting PHY to 10M full-duplex by force
	 */
	if (phydev->state == PHY_RUNNING && !priv->force_10m_full_mode &&
	    phydev->link && phydev->speed == SPEED_10) {
		int an_expan;

		val = phy_read(phydev, MII_EXPANSION);
		if (val < 0)
			return val;

		an_expan = val;
		if (!(an_expan & 0x1)) {
			phydev_dbg(phydev, "%s: force_10m_full_mode\n",
				   __func__);

			val = phy_read(phydev, MII_BMCR);
			if (val < 0)
				return val;
			priv->restore_reg0 = val;

			val = val & (~BMCR_ANENABLE);
			val &= ~BMCR_SPEED100;
			val |= BMCR_FULLDPLX;
			ret = phy_write(phydev, MII_BMCR, val);
			if (ret)
				return ret;
			priv->force_10m_full_mode = 1;
		}
	/* restore BMCR register */
	} else if (!phydev->link && priv->force_10m_full_mode) {
		phydev_dbg(phydev, "%s: restore force_10m_full_mode\n",
			   __func__);
		priv->force_10m_full_mode = 0;
		ret = phy_write(phydev, MII_BMCR, priv->restore_reg0);
		if (ret)
			return ret;
	}

	return 0;
}

static int rockchip_integrated_phy_fixup_100M(struct phy_device *phydev)
{
	int ret, val;
	struct rk_int_phy_priv *priv = phydev->priv;

	/* reset phy when continue detect phy abnormal */
	if (phydev->link && phydev->speed == SPEED_100) {
		/* read wol bank reg12 and check bit 12 */
		ret = phy_write(phydev, SMI_ADDR_TSTCNTL, TSTCNTL_RD |
					WOL_REG_BANK_SEL | RD_ADDR_LPISTA);
		if (ret)
			return ret;

		val = phy_read(phydev, SMI_ADDR_TSTREAD1);
		if (val < 0)
			return val;

		if (val & 0x1000)
			priv->txrx_counters_done_count = 0;
		if (!(val & 0x1000))
			priv->txrx_counters_done_count++;

		/* phydev_dbg(phydev, "txrx_counters_done_count = %d,"
		 *		threshol = %d\n",
		 *		priv->txrx_counters_done_count,
		 *		PHY_ABNORMAL_THRESHOLD);
		 */
		if (priv->txrx_counters_done_count >=
		    PHY_ABNORMAL_THRESHOLD) {
			phydev_err(phydev, "%s: reset phy\n", __func__);
			priv->txrx_counters_done_count = 0;
			ret = genphy_soft_reset(phydev);
			if (ret < 0)
				return ret;
		}
	} else {
		priv->txrx_counters_done_count = 0;
	}

	/* adjust A3_CONFIG for optimize long cable when 100M link up*/
	if (phydev->link && phydev->speed == SPEED_100 &&
	    !priv->a3_config_set) {
		ret = rockchip_integrated_phy_adjust_a3_config
					(phydev,
					A3CFG_TAG);
		if (ret) {
			phydev_err(phydev, "adjust_a3_config fail %x\n",
				   A3CFG_TAG);
			return ret;
		}
	/* restore A3_CONFIG when 100M link down */
	} else if (!phydev->link && priv->a3_config_set) {
		priv->a3_config_set = 0;
		ret = rockchip_integrated_phy_adjust_a3_config
					(phydev,
					priv->restore_a3_config);
		if (ret) {
			phydev_err(phydev, "adjust_a3_config fail %x\n",
				   priv->restore_a3_config);
			return ret;
		}
	}

	return 0;
}

#ifdef ENABLE_PHY_FIXUP_RESET
static int rockchip_integrated_phy_fixup_reset(struct phy_device *phydev)
{
	int ret;
	struct rk_int_phy_priv *priv = phydev->priv;

	/* reset phy once
	 * solve failed to linkup for very few chip
	 * reduce udp package lost rate sometimes
	 */
	if (priv->last_state == PHY_NOLINK && phydev->state == PHY_NOLINK) {
		if (++priv->reset == 2) {
			phydev_dbg(phydev, "%s\n", __func__);
			ret = genphy_soft_reset(phydev);
			if (ret < 0)
				return ret;
		}
	}

	priv->last_state = phydev->state;
	return 0;
}
#endif

static int rockchip_integrated_phy_read_status(struct phy_device *phydev)
{
	int ret;

	ret = genphy_read_status(phydev);
	if (ret)
		return ret;

	ret = rockchip_integrated_phy_fixup_10M(phydev);
	if (ret)
		return ret;

	ret = rockchip_integrated_phy_fixup_100M(phydev);
	if (ret)
		return ret;

#ifdef ENABLE_PHY_FIXUP_RESET
	ret = rockchip_integrated_phy_fixup_reset(phydev);
	if (ret)
		return ret;
#endif

	return 0;
}

static int rockchip_integrated_phy_resume(struct phy_device *phydev)
{
	struct rk_int_phy_priv *priv = phydev->priv;

	if (phydev->link && phydev->speed == SPEED_100)
		priv->a3_config_set = 0;

	genphy_resume(phydev);

	return rockchip_integrated_phy_config_init(phydev);
}

static struct phy_driver rockchip_phy_driver[] = {
{
	.phy_id			= INTERNAL_EPHY_ID,
	.phy_id_mask		= 0xfffffff0,
	.name			= "Rockchip integrated EPHY",
	.features		= PHY_BASIC_FEATURES,
	.flags			= 0,
	.probe			= rockchip_integrated_phy_probe,
	.remove			= rockchip_integrated_phy_remove,
	.link_change_notify	= rockchip_integrated_phy_link_change_notify,
	.soft_reset		= genphy_soft_reset,
	.config_init		= rockchip_integrated_phy_config_init,
	.config_aneg		= rockchip_integrated_phy_config_aneg,
	.read_status		= rockchip_integrated_phy_read_status,
	.suspend		= genphy_suspend,
	.resume			= rockchip_integrated_phy_resume,
},
};

module_phy_driver(rockchip_phy_driver);

static struct mdio_device_id __maybe_unused rockchip_phy_tbl[] = {
	{ INTERNAL_EPHY_ID, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, rockchip_phy_tbl);

MODULE_AUTHOR("David Wu <david.wu@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Ethernet PHY driver");
MODULE_LICENSE("GPL v2");
