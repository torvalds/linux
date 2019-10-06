// SPDX-License-Identifier: GPL-2.0+
/**
 * drivers/net/phy/rockchip.c
 *
 * Driver for ROCKCHIP Ethernet PHYs
 *
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * David Wu <david.wu@rock-chips.com>
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

#define WR_ADDR_A7CFG				0x18

static int rockchip_init_tstmode(struct phy_device *phydev)
{
	int ret;

	/* Enable access to Analog and DSP register banks */
	ret = phy_write(phydev, SMI_ADDR_TSTCNTL, TSTMODE_ENABLE);
	if (ret)
		return ret;

	ret = phy_write(phydev, SMI_ADDR_TSTCNTL, TSTMODE_DISABLE);
	if (ret)
		return ret;

	return phy_write(phydev, SMI_ADDR_TSTCNTL, TSTMODE_ENABLE);
}

static int rockchip_close_tstmode(struct phy_device *phydev)
{
	/* Back to basic register bank */
	return phy_write(phydev, SMI_ADDR_TSTCNTL, TSTMODE_DISABLE);
}

static int rockchip_integrated_phy_analog_init(struct phy_device *phydev)
{
	int ret;

	ret = rockchip_init_tstmode(phydev);
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

	return rockchip_close_tstmode(phydev);
}

static int rockchip_integrated_phy_config_init(struct phy_device *phydev)
{
	int val, ret;

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

static void rockchip_link_change_notify(struct phy_device *phydev)
{
	/*
	 * If mode switch happens from 10BT to 100BT, all DSP/AFE
	 * registers are set to default values. So any AFE/DSP
	 * registers have to be re-initialized in this case.
	 */
	if (phydev->state == PHY_RUNNING && phydev->speed == SPEED_100) {
		int ret = rockchip_integrated_phy_analog_init(phydev);

		if (ret)
			phydev_err(phydev, "rockchip_integrated_phy_analog_init err: %d.\n",
				   ret);
	}
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

static int rockchip_config_aneg(struct phy_device *phydev)
{
	int err;

	err = rockchip_set_polarity(phydev, phydev->mdix);
	if (err < 0)
		return err;

	return genphy_config_aneg(phydev);
}

static int rockchip_phy_resume(struct phy_device *phydev)
{
	genphy_resume(phydev);

	return rockchip_integrated_phy_config_init(phydev);
}

static struct phy_driver rockchip_phy_driver[] = {
{
	.phy_id			= INTERNAL_EPHY_ID,
	.phy_id_mask		= 0xfffffff0,
	.name			= "Rockchip integrated EPHY",
	/* PHY_BASIC_FEATURES */
	.flags			= 0,
	.link_change_notify	= rockchip_link_change_notify,
	.soft_reset		= genphy_soft_reset,
	.config_init		= rockchip_integrated_phy_config_init,
	.config_aneg		= rockchip_config_aneg,
	.suspend		= genphy_suspend,
	.resume			= rockchip_phy_resume,
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
MODULE_LICENSE("GPL");
