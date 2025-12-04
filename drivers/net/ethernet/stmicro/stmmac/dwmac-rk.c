// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * DOC: dwmac-rk.c - Rockchip RK3288 DWMAC specific glue layer
 *
 * Copyright (C) 2014 Chen-Zhi (Roger Chen)
 *
 * Chen-Zhi (Roger Chen)  <roger.chen@rock-chips.com>
 */

#include <linux/stmmac.h>
#include <linux/hw_bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/phy.h>
#include <linux/of_net.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include "stmmac_platform.h"

struct rk_priv_data;

struct rk_reg_speed_data {
	unsigned int rgmii_10;
	unsigned int rgmii_100;
	unsigned int rgmii_1000;
	unsigned int rmii_10;
	unsigned int rmii_100;
};

struct rk_gmac_ops {
	void (*set_to_rgmii)(struct rk_priv_data *bsp_priv,
			     int tx_delay, int rx_delay);
	void (*set_to_rmii)(struct rk_priv_data *bsp_priv);
	int (*set_speed)(struct rk_priv_data *bsp_priv,
			 phy_interface_t interface, int speed);
	void (*set_clock_selection)(struct rk_priv_data *bsp_priv, bool input,
				    bool enable);
	void (*integrated_phy_powerup)(struct rk_priv_data *bsp_priv);
	void (*integrated_phy_powerdown)(struct rk_priv_data *bsp_priv);
	bool php_grf_required;
	bool regs_valid;
	u32 regs[];
};

static const char * const rk_clocks[] = {
	"aclk_mac", "pclk_mac", "mac_clk_tx", "clk_mac_speed",
};

static const char * const rk_rmii_clocks[] = {
	"mac_clk_rx", "clk_mac_ref", "clk_mac_refout",
};

enum rk_clocks_index {
	RK_ACLK_MAC = 0,
	RK_PCLK_MAC,
	RK_MAC_CLK_TX,
	RK_CLK_MAC_SPEED,
	RK_MAC_CLK_RX,
	RK_CLK_MAC_REF,
	RK_CLK_MAC_REFOUT,
};

struct rk_priv_data {
	struct device *dev;
	phy_interface_t phy_iface;
	int id;
	struct regulator *regulator;
	const struct rk_gmac_ops *ops;

	bool clk_enabled;
	bool clock_input;
	bool integrated_phy;

	struct clk_bulk_data *clks;
	int num_clks;
	struct clk *clk_phy;

	struct reset_control *phy_reset;

	int tx_delay;
	int rx_delay;

	struct regmap *grf;
	struct regmap *php_grf;
};

static int rk_set_reg_speed(struct rk_priv_data *bsp_priv,
			    const struct rk_reg_speed_data *rsd,
			    unsigned int reg, phy_interface_t interface,
			    int speed)
{
	unsigned int val;

	if (phy_interface_mode_is_rgmii(interface)) {
		if (speed == SPEED_10) {
			val = rsd->rgmii_10;
		} else if (speed == SPEED_100) {
			val = rsd->rgmii_100;
		} else if (speed == SPEED_1000) {
			val = rsd->rgmii_1000;
		} else {
			/* Phylink will not allow inappropriate speeds for
			 * interface modes, so this should never happen.
			 */
			return -EINVAL;
		}
	} else if (interface == PHY_INTERFACE_MODE_RMII) {
		if (speed == SPEED_10) {
			val = rsd->rmii_10;
		} else if (speed == SPEED_100) {
			val = rsd->rmii_100;
		} else {
			/* Phylink will not allow inappropriate speeds for
			 * interface modes, so this should never happen.
			 */
			return -EINVAL;
		}
	} else {
		/* This should never happen, as .get_interfaces() limits
		 * the interface modes that are supported to RGMII and/or
		 * RMII.
		 */
		return -EINVAL;
	}

	regmap_write(bsp_priv->grf, reg, val);

	return 0;

}

static int rk_set_clk_mac_speed(struct rk_priv_data *bsp_priv,
				phy_interface_t interface, int speed)
{
	struct clk *clk_mac_speed = bsp_priv->clks[RK_CLK_MAC_SPEED].clk;
	long rate;

	rate = rgmii_clock(speed);
	if (rate < 0)
		return rate;

	return clk_set_rate(clk_mac_speed, rate);
}

#define HIWORD_UPDATE(val, mask, shift) \
		(FIELD_PREP_WM16((mask) << (shift), (val)))

#define GRF_BIT(nr)	(BIT(nr) | BIT(nr+16))
#define GRF_CLR_BIT(nr)	(BIT(nr+16))

#define DELAY_ENABLE(soc, tx, rx) \
	(((tx) ? soc##_GMAC_TXCLK_DLY_ENABLE : soc##_GMAC_TXCLK_DLY_DISABLE) | \
	 ((rx) ? soc##_GMAC_RXCLK_DLY_ENABLE : soc##_GMAC_RXCLK_DLY_DISABLE))

#define RK_GRF_MACPHY_CON0		0xb00
#define RK_GRF_MACPHY_CON1		0xb04
#define RK_GRF_MACPHY_CON2		0xb08
#define RK_GRF_MACPHY_CON3		0xb0c

#define RK_MACPHY_ENABLE		GRF_BIT(0)
#define RK_MACPHY_DISABLE		GRF_CLR_BIT(0)
#define RK_MACPHY_CFG_CLK_50M		GRF_BIT(14)
#define RK_GMAC2PHY_RMII_MODE		(GRF_BIT(6) | GRF_CLR_BIT(7))
#define RK_GRF_CON2_MACPHY_ID		HIWORD_UPDATE(0x1234, 0xffff, 0)
#define RK_GRF_CON3_MACPHY_ID		HIWORD_UPDATE(0x35, 0x3f, 0)

static void rk_gmac_integrated_ephy_powerup(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_CFG_CLK_50M);
	regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_GMAC2PHY_RMII_MODE);

	regmap_write(priv->grf, RK_GRF_MACPHY_CON2, RK_GRF_CON2_MACPHY_ID);
	regmap_write(priv->grf, RK_GRF_MACPHY_CON3, RK_GRF_CON3_MACPHY_ID);

	if (priv->phy_reset) {
		/* PHY needs to be disabled before trying to reset it */
		regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_DISABLE);
		if (priv->phy_reset)
			reset_control_assert(priv->phy_reset);
		usleep_range(10, 20);
		if (priv->phy_reset)
			reset_control_deassert(priv->phy_reset);
		usleep_range(10, 20);
		regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_ENABLE);
		msleep(30);
	}
}

static void rk_gmac_integrated_ephy_powerdown(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_DISABLE);
	if (priv->phy_reset)
		reset_control_assert(priv->phy_reset);
}

#define RK_FEPHY_SHUTDOWN		GRF_BIT(1)
#define RK_FEPHY_POWERUP		GRF_CLR_BIT(1)
#define RK_FEPHY_INTERNAL_RMII_SEL	GRF_BIT(6)
#define RK_FEPHY_24M_CLK_SEL		(GRF_BIT(8) | GRF_BIT(9))
#define RK_FEPHY_PHY_ID			GRF_BIT(11)

static void rk_gmac_integrated_fephy_powerup(struct rk_priv_data *priv,
					     unsigned int reg)
{
	reset_control_assert(priv->phy_reset);
	usleep_range(20, 30);

	regmap_write(priv->grf, reg,
		     RK_FEPHY_POWERUP |
		     RK_FEPHY_INTERNAL_RMII_SEL |
		     RK_FEPHY_24M_CLK_SEL |
		     RK_FEPHY_PHY_ID);
	usleep_range(10000, 12000);

	reset_control_deassert(priv->phy_reset);
	usleep_range(50000, 60000);
}

static void rk_gmac_integrated_fephy_powerdown(struct rk_priv_data *priv,
					       unsigned int reg)
{
	regmap_write(priv->grf, reg, RK_FEPHY_SHUTDOWN);
}

#define PX30_GRF_GMAC_CON1		0x0904

/* PX30_GRF_GMAC_CON1 */
#define PX30_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | \
					 GRF_BIT(6))
#define PX30_GMAC_SPEED_10M		GRF_CLR_BIT(2)
#define PX30_GMAC_SPEED_100M		GRF_BIT(2)

static void px30_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, PX30_GRF_GMAC_CON1,
		     PX30_GMAC_PHY_INTF_SEL_RMII);
}

static int px30_set_speed(struct rk_priv_data *bsp_priv,
			  phy_interface_t interface, int speed)
{
	struct clk *clk_mac_speed = bsp_priv->clks[RK_CLK_MAC_SPEED].clk;
	struct device *dev = bsp_priv->dev;
	unsigned int con1;
	long rate;

	if (!clk_mac_speed) {
		dev_err(dev, "%s: Missing clk_mac_speed clock\n", __func__);
		return -EINVAL;
	}

	if (speed == 10) {
		con1 = PX30_GMAC_SPEED_10M;
		rate = 2500000;
	} else if (speed == 100) {
		con1 = PX30_GMAC_SPEED_100M;
		rate = 25000000;
	} else {
		dev_err(dev, "unknown speed value for RMII! speed=%d", speed);
		return -EINVAL;
	}

	regmap_write(bsp_priv->grf, PX30_GRF_GMAC_CON1, con1);

	return clk_set_rate(clk_mac_speed, rate);
}

static const struct rk_gmac_ops px30_ops = {
	.set_to_rmii = px30_set_to_rmii,
	.set_speed = px30_set_speed,
};

#define RK3128_GRF_MAC_CON0	0x0168
#define RK3128_GRF_MAC_CON1	0x016c

/* RK3128_GRF_MAC_CON0 */
#define RK3128_GMAC_TXCLK_DLY_ENABLE   GRF_BIT(14)
#define RK3128_GMAC_TXCLK_DLY_DISABLE  GRF_CLR_BIT(14)
#define RK3128_GMAC_RXCLK_DLY_ENABLE   GRF_BIT(15)
#define RK3128_GMAC_RXCLK_DLY_DISABLE  GRF_CLR_BIT(15)
#define RK3128_GMAC_CLK_RX_DL_CFG(val) HIWORD_UPDATE(val, 0x7F, 7)
#define RK3128_GMAC_CLK_TX_DL_CFG(val) HIWORD_UPDATE(val, 0x7F, 0)

/* RK3128_GRF_MAC_CON1 */
#define RK3128_GMAC_PHY_INTF_SEL_RGMII	\
		(GRF_BIT(6) | GRF_CLR_BIT(7) | GRF_CLR_BIT(8))
#define RK3128_GMAC_PHY_INTF_SEL_RMII	\
		(GRF_CLR_BIT(6) | GRF_CLR_BIT(7) | GRF_BIT(8))
#define RK3128_GMAC_FLOW_CTRL          GRF_BIT(9)
#define RK3128_GMAC_FLOW_CTRL_CLR      GRF_CLR_BIT(9)
#define RK3128_GMAC_SPEED_10M          GRF_CLR_BIT(10)
#define RK3128_GMAC_SPEED_100M         GRF_BIT(10)
#define RK3128_GMAC_RMII_CLK_25M       GRF_BIT(11)
#define RK3128_GMAC_RMII_CLK_2_5M      GRF_CLR_BIT(11)
#define RK3128_GMAC_CLK_125M           (GRF_CLR_BIT(12) | GRF_CLR_BIT(13))
#define RK3128_GMAC_CLK_25M            (GRF_BIT(12) | GRF_BIT(13))
#define RK3128_GMAC_CLK_2_5M           (GRF_CLR_BIT(12) | GRF_BIT(13))
#define RK3128_GMAC_RMII_MODE          GRF_BIT(14)
#define RK3128_GMAC_RMII_MODE_CLR      GRF_CLR_BIT(14)

static void rk3128_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3128_GRF_MAC_CON1,
		     RK3128_GMAC_PHY_INTF_SEL_RGMII |
		     RK3128_GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3128_GRF_MAC_CON0,
		     DELAY_ENABLE(RK3128, tx_delay, rx_delay) |
		     RK3128_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3128_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3128_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3128_GRF_MAC_CON1,
		     RK3128_GMAC_PHY_INTF_SEL_RMII | RK3128_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3128_reg_speed_data = {
	.rgmii_10 = RK3128_GMAC_CLK_2_5M,
	.rgmii_100 = RK3128_GMAC_CLK_25M,
	.rgmii_1000 = RK3128_GMAC_CLK_125M,
	.rmii_10 = RK3128_GMAC_RMII_CLK_2_5M | RK3128_GMAC_SPEED_10M,
	.rmii_100 = RK3128_GMAC_RMII_CLK_25M | RK3128_GMAC_SPEED_100M,
};

static int rk3128_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3128_reg_speed_data,
				RK3128_GRF_MAC_CON1, interface, speed);
}

static const struct rk_gmac_ops rk3128_ops = {
	.set_to_rgmii = rk3128_set_to_rgmii,
	.set_to_rmii = rk3128_set_to_rmii,
	.set_speed = rk3128_set_speed,
};

#define RK3228_GRF_MAC_CON0	0x0900
#define RK3228_GRF_MAC_CON1	0x0904

#define RK3228_GRF_CON_MUX	0x50

/* RK3228_GRF_MAC_CON0 */
#define RK3228_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 7)
#define RK3228_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

/* RK3228_GRF_MAC_CON1 */
#define RK3228_GMAC_PHY_INTF_SEL_RGMII	\
		(GRF_BIT(4) | GRF_CLR_BIT(5) | GRF_CLR_BIT(6))
#define RK3228_GMAC_PHY_INTF_SEL_RMII	\
		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | GRF_BIT(6))
#define RK3228_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RK3228_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)
#define RK3228_GMAC_SPEED_10M		GRF_CLR_BIT(2)
#define RK3228_GMAC_SPEED_100M		GRF_BIT(2)
#define RK3228_GMAC_RMII_CLK_25M	GRF_BIT(7)
#define RK3228_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(7)
#define RK3228_GMAC_CLK_125M		(GRF_CLR_BIT(8) | GRF_CLR_BIT(9))
#define RK3228_GMAC_CLK_25M		(GRF_BIT(8) | GRF_BIT(9))
#define RK3228_GMAC_CLK_2_5M		(GRF_CLR_BIT(8) | GRF_BIT(9))
#define RK3228_GMAC_RMII_MODE		GRF_BIT(10)
#define RK3228_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(10)
#define RK3228_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(0)
#define RK3228_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(0)
#define RK3228_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(1)
#define RK3228_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(1)

/* RK3228_GRF_COM_MUX */
#define RK3228_GRF_CON_MUX_GMAC_INTEGRATED_PHY	GRF_BIT(15)

static void rk3228_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON1,
		     RK3228_GMAC_PHY_INTF_SEL_RGMII |
		     RK3228_GMAC_RMII_MODE_CLR |
		     DELAY_ENABLE(RK3228, tx_delay, rx_delay));

	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON0,
		     RK3228_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3228_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3228_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON1,
		     RK3228_GMAC_PHY_INTF_SEL_RMII |
		     RK3228_GMAC_RMII_MODE);

	/* set MAC to RMII mode */
	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON1, GRF_BIT(11));
}

static const struct rk_reg_speed_data rk3228_reg_speed_data = {
	.rgmii_10 = RK3228_GMAC_CLK_2_5M,
	.rgmii_100 = RK3228_GMAC_CLK_25M,
	.rgmii_1000 = RK3228_GMAC_CLK_125M,
	.rmii_10 = RK3228_GMAC_RMII_CLK_2_5M | RK3228_GMAC_SPEED_10M,
	.rmii_100 = RK3228_GMAC_RMII_CLK_25M | RK3228_GMAC_SPEED_100M,
};

static int rk3228_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3228_reg_speed_data,
				RK3228_GRF_MAC_CON1, interface, speed);
}

static void rk3228_integrated_phy_powerup(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK3228_GRF_CON_MUX,
		     RK3228_GRF_CON_MUX_GMAC_INTEGRATED_PHY);

	rk_gmac_integrated_ephy_powerup(priv);
}

static const struct rk_gmac_ops rk3228_ops = {
	.set_to_rgmii = rk3228_set_to_rgmii,
	.set_to_rmii = rk3228_set_to_rmii,
	.set_speed = rk3228_set_speed,
	.integrated_phy_powerup = rk3228_integrated_phy_powerup,
	.integrated_phy_powerdown = rk_gmac_integrated_ephy_powerdown,
};

#define RK3288_GRF_SOC_CON1	0x0248
#define RK3288_GRF_SOC_CON3	0x0250

/*RK3288_GRF_SOC_CON1*/
#define RK3288_GMAC_PHY_INTF_SEL_RGMII	(GRF_BIT(6) | GRF_CLR_BIT(7) | \
					 GRF_CLR_BIT(8))
#define RK3288_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(6) | GRF_CLR_BIT(7) | \
					 GRF_BIT(8))
#define RK3288_GMAC_FLOW_CTRL		GRF_BIT(9)
#define RK3288_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(9)
#define RK3288_GMAC_SPEED_10M		GRF_CLR_BIT(10)
#define RK3288_GMAC_SPEED_100M		GRF_BIT(10)
#define RK3288_GMAC_RMII_CLK_25M	GRF_BIT(11)
#define RK3288_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(11)
#define RK3288_GMAC_CLK_125M		(GRF_CLR_BIT(12) | GRF_CLR_BIT(13))
#define RK3288_GMAC_CLK_25M		(GRF_BIT(12) | GRF_BIT(13))
#define RK3288_GMAC_CLK_2_5M		(GRF_CLR_BIT(12) | GRF_BIT(13))
#define RK3288_GMAC_RMII_MODE		GRF_BIT(14)
#define RK3288_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(14)

/*RK3288_GRF_SOC_CON3*/
#define RK3288_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(14)
#define RK3288_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(14)
#define RK3288_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3288_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3288_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 7)
#define RK3288_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rk3288_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
		     RK3288_GMAC_PHY_INTF_SEL_RGMII |
		     RK3288_GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON3,
		     DELAY_ENABLE(RK3288, tx_delay, rx_delay) |
		     RK3288_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3288_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3288_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
		     RK3288_GMAC_PHY_INTF_SEL_RMII | RK3288_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3288_reg_speed_data = {
	.rgmii_10 = RK3288_GMAC_CLK_2_5M,
	.rgmii_100 = RK3288_GMAC_CLK_25M,
	.rgmii_1000 = RK3288_GMAC_CLK_125M,
	.rmii_10 = RK3288_GMAC_RMII_CLK_2_5M | RK3288_GMAC_SPEED_10M,
	.rmii_100 = RK3288_GMAC_RMII_CLK_25M | RK3288_GMAC_SPEED_100M,
};

static int rk3288_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3288_reg_speed_data,
				RK3288_GRF_SOC_CON1, interface, speed);
}

static const struct rk_gmac_ops rk3288_ops = {
	.set_to_rgmii = rk3288_set_to_rgmii,
	.set_to_rmii = rk3288_set_to_rmii,
	.set_speed = rk3288_set_speed,
};

#define RK3308_GRF_MAC_CON0		0x04a0

/* RK3308_GRF_MAC_CON0 */
#define RK3308_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(2) | GRF_CLR_BIT(3) | \
					GRF_BIT(4))
#define RK3308_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RK3308_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)
#define RK3308_GMAC_SPEED_10M		GRF_CLR_BIT(0)
#define RK3308_GMAC_SPEED_100M		GRF_BIT(0)

static void rk3308_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3308_GRF_MAC_CON0,
		     RK3308_GMAC_PHY_INTF_SEL_RMII);
}

static const struct rk_reg_speed_data rk3308_reg_speed_data = {
	.rmii_10 = RK3308_GMAC_SPEED_10M,
	.rmii_100 = RK3308_GMAC_SPEED_100M,
};

static int rk3308_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3308_reg_speed_data,
				RK3308_GRF_MAC_CON0, interface, speed);
}

static const struct rk_gmac_ops rk3308_ops = {
	.set_to_rmii = rk3308_set_to_rmii,
	.set_speed = rk3308_set_speed,
};

#define RK3328_GRF_MAC_CON0	0x0900
#define RK3328_GRF_MAC_CON1	0x0904
#define RK3328_GRF_MAC_CON2	0x0908
#define RK3328_GRF_MACPHY_CON1	0xb04

/* RK3328_GRF_MAC_CON0 */
#define RK3328_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 7)
#define RK3328_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

/* RK3328_GRF_MAC_CON1 */
#define RK3328_GMAC_PHY_INTF_SEL_RGMII	\
		(GRF_BIT(4) | GRF_CLR_BIT(5) | GRF_CLR_BIT(6))
#define RK3328_GMAC_PHY_INTF_SEL_RMII	\
		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | GRF_BIT(6))
#define RK3328_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RK3328_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)
#define RK3328_GMAC_SPEED_10M		GRF_CLR_BIT(2)
#define RK3328_GMAC_SPEED_100M		GRF_BIT(2)
#define RK3328_GMAC_RMII_CLK_25M	GRF_BIT(7)
#define RK3328_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(7)
#define RK3328_GMAC_CLK_125M		(GRF_CLR_BIT(11) | GRF_CLR_BIT(12))
#define RK3328_GMAC_CLK_25M		(GRF_BIT(11) | GRF_BIT(12))
#define RK3328_GMAC_CLK_2_5M		(GRF_CLR_BIT(11) | GRF_BIT(12))
#define RK3328_GMAC_RMII_MODE		GRF_BIT(9)
#define RK3328_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(9)
#define RK3328_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(0)
#define RK3328_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(1)

/* RK3328_GRF_MACPHY_CON1 */
#define RK3328_MACPHY_RMII_MODE		GRF_BIT(9)

static void rk3328_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3328_GRF_MAC_CON1,
		     RK3328_GMAC_PHY_INTF_SEL_RGMII |
		     RK3328_GMAC_RMII_MODE_CLR |
		     RK3328_GMAC_RXCLK_DLY_ENABLE |
		     RK3328_GMAC_TXCLK_DLY_ENABLE);

	regmap_write(bsp_priv->grf, RK3328_GRF_MAC_CON0,
		     RK3328_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3328_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3328_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	unsigned int reg;

	reg = bsp_priv->integrated_phy ? RK3328_GRF_MAC_CON2 :
		  RK3328_GRF_MAC_CON1;

	regmap_write(bsp_priv->grf, reg,
		     RK3328_GMAC_PHY_INTF_SEL_RMII |
		     RK3328_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3328_reg_speed_data = {
	.rgmii_10 = RK3328_GMAC_CLK_2_5M,
	.rgmii_100 = RK3328_GMAC_CLK_25M,
	.rgmii_1000 = RK3328_GMAC_CLK_125M,
	.rmii_10 = RK3328_GMAC_RMII_CLK_2_5M | RK3328_GMAC_SPEED_10M,
	.rmii_100 = RK3328_GMAC_RMII_CLK_25M | RK3328_GMAC_SPEED_100M,
};

static int rk3328_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	unsigned int reg;

	if (interface == PHY_INTERFACE_MODE_RMII && bsp_priv->integrated_phy)
		reg = RK3328_GRF_MAC_CON2;
	else
		reg = RK3328_GRF_MAC_CON1;

	return rk_set_reg_speed(bsp_priv, &rk3328_reg_speed_data, reg,
				interface, speed);
}

static void rk3328_integrated_phy_powerup(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK3328_GRF_MACPHY_CON1,
		     RK3328_MACPHY_RMII_MODE);

	rk_gmac_integrated_ephy_powerup(priv);
}

static const struct rk_gmac_ops rk3328_ops = {
	.set_to_rgmii = rk3328_set_to_rgmii,
	.set_to_rmii = rk3328_set_to_rmii,
	.set_speed = rk3328_set_speed,
	.integrated_phy_powerup = rk3328_integrated_phy_powerup,
	.integrated_phy_powerdown = rk_gmac_integrated_ephy_powerdown,
};

#define RK3366_GRF_SOC_CON6	0x0418
#define RK3366_GRF_SOC_CON7	0x041c

/* RK3366_GRF_SOC_CON6 */
#define RK3366_GMAC_PHY_INTF_SEL_RGMII	(GRF_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_CLR_BIT(11))
#define RK3366_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_BIT(11))
#define RK3366_GMAC_FLOW_CTRL		GRF_BIT(8)
#define RK3366_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(8)
#define RK3366_GMAC_SPEED_10M		GRF_CLR_BIT(7)
#define RK3366_GMAC_SPEED_100M		GRF_BIT(7)
#define RK3366_GMAC_RMII_CLK_25M	GRF_BIT(3)
#define RK3366_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(3)
#define RK3366_GMAC_CLK_125M		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5))
#define RK3366_GMAC_CLK_25M		(GRF_BIT(4) | GRF_BIT(5))
#define RK3366_GMAC_CLK_2_5M		(GRF_CLR_BIT(4) | GRF_BIT(5))
#define RK3366_GMAC_RMII_MODE		GRF_BIT(6)
#define RK3366_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(6)

/* RK3366_GRF_SOC_CON7 */
#define RK3366_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(7)
#define RK3366_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(7)
#define RK3366_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3366_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3366_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RK3366_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rk3366_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3366_GRF_SOC_CON6,
		     RK3366_GMAC_PHY_INTF_SEL_RGMII |
		     RK3366_GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3366_GRF_SOC_CON7,
		     DELAY_ENABLE(RK3366, tx_delay, rx_delay) |
		     RK3366_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3366_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3366_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3366_GRF_SOC_CON6,
		     RK3366_GMAC_PHY_INTF_SEL_RMII | RK3366_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3366_reg_speed_data = {
	.rgmii_10 = RK3366_GMAC_CLK_2_5M,
	.rgmii_100 = RK3366_GMAC_CLK_25M,
	.rgmii_1000 = RK3366_GMAC_CLK_125M,
	.rmii_10 = RK3366_GMAC_RMII_CLK_2_5M | RK3366_GMAC_SPEED_10M,
	.rmii_100 = RK3366_GMAC_RMII_CLK_25M | RK3366_GMAC_SPEED_100M,
};

static int rk3366_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3366_reg_speed_data,
				RK3366_GRF_SOC_CON6, interface, speed);
}

static const struct rk_gmac_ops rk3366_ops = {
	.set_to_rgmii = rk3366_set_to_rgmii,
	.set_to_rmii = rk3366_set_to_rmii,
	.set_speed = rk3366_set_speed,
};

#define RK3368_GRF_SOC_CON15	0x043c
#define RK3368_GRF_SOC_CON16	0x0440

/* RK3368_GRF_SOC_CON15 */
#define RK3368_GMAC_PHY_INTF_SEL_RGMII	(GRF_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_CLR_BIT(11))
#define RK3368_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_BIT(11))
#define RK3368_GMAC_FLOW_CTRL		GRF_BIT(8)
#define RK3368_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(8)
#define RK3368_GMAC_SPEED_10M		GRF_CLR_BIT(7)
#define RK3368_GMAC_SPEED_100M		GRF_BIT(7)
#define RK3368_GMAC_RMII_CLK_25M	GRF_BIT(3)
#define RK3368_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(3)
#define RK3368_GMAC_CLK_125M		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5))
#define RK3368_GMAC_CLK_25M		(GRF_BIT(4) | GRF_BIT(5))
#define RK3368_GMAC_CLK_2_5M		(GRF_CLR_BIT(4) | GRF_BIT(5))
#define RK3368_GMAC_RMII_MODE		GRF_BIT(6)
#define RK3368_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(6)

/* RK3368_GRF_SOC_CON16 */
#define RK3368_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(7)
#define RK3368_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(7)
#define RK3368_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3368_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3368_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RK3368_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rk3368_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3368_GRF_SOC_CON15,
		     RK3368_GMAC_PHY_INTF_SEL_RGMII |
		     RK3368_GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3368_GRF_SOC_CON16,
		     DELAY_ENABLE(RK3368, tx_delay, rx_delay) |
		     RK3368_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3368_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3368_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3368_GRF_SOC_CON15,
		     RK3368_GMAC_PHY_INTF_SEL_RMII | RK3368_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3368_reg_speed_data = {
	.rgmii_10 = RK3368_GMAC_CLK_2_5M,
	.rgmii_100 = RK3368_GMAC_CLK_25M,
	.rgmii_1000 = RK3368_GMAC_CLK_125M,
	.rmii_10 = RK3368_GMAC_RMII_CLK_2_5M | RK3368_GMAC_SPEED_10M,
	.rmii_100 = RK3368_GMAC_RMII_CLK_25M | RK3368_GMAC_SPEED_100M,
};

static int rk3368_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3368_reg_speed_data,
				RK3368_GRF_SOC_CON15, interface, speed);
}

static const struct rk_gmac_ops rk3368_ops = {
	.set_to_rgmii = rk3368_set_to_rgmii,
	.set_to_rmii = rk3368_set_to_rmii,
	.set_speed = rk3368_set_speed,
};

#define RK3399_GRF_SOC_CON5	0xc214
#define RK3399_GRF_SOC_CON6	0xc218

/* RK3399_GRF_SOC_CON5 */
#define RK3399_GMAC_PHY_INTF_SEL_RGMII	(GRF_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_CLR_BIT(11))
#define RK3399_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_BIT(11))
#define RK3399_GMAC_FLOW_CTRL		GRF_BIT(8)
#define RK3399_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(8)
#define RK3399_GMAC_SPEED_10M		GRF_CLR_BIT(7)
#define RK3399_GMAC_SPEED_100M		GRF_BIT(7)
#define RK3399_GMAC_RMII_CLK_25M	GRF_BIT(3)
#define RK3399_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(3)
#define RK3399_GMAC_CLK_125M		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5))
#define RK3399_GMAC_CLK_25M		(GRF_BIT(4) | GRF_BIT(5))
#define RK3399_GMAC_CLK_2_5M		(GRF_CLR_BIT(4) | GRF_BIT(5))
#define RK3399_GMAC_RMII_MODE		GRF_BIT(6)
#define RK3399_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(6)

/* RK3399_GRF_SOC_CON6 */
#define RK3399_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(7)
#define RK3399_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(7)
#define RK3399_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3399_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3399_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RK3399_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rk3399_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3399_GRF_SOC_CON5,
		     RK3399_GMAC_PHY_INTF_SEL_RGMII |
		     RK3399_GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3399_GRF_SOC_CON6,
		     DELAY_ENABLE(RK3399, tx_delay, rx_delay) |
		     RK3399_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3399_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3399_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3399_GRF_SOC_CON5,
		     RK3399_GMAC_PHY_INTF_SEL_RMII | RK3399_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3399_reg_speed_data = {
	.rgmii_10 = RK3399_GMAC_CLK_2_5M,
	.rgmii_100 = RK3399_GMAC_CLK_25M,
	.rgmii_1000 = RK3399_GMAC_CLK_125M,
	.rmii_10 = RK3399_GMAC_RMII_CLK_2_5M | RK3399_GMAC_SPEED_10M,
	.rmii_100 = RK3399_GMAC_RMII_CLK_25M | RK3399_GMAC_SPEED_100M,
};

static int rk3399_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3399_reg_speed_data,
				RK3399_GRF_SOC_CON5, interface, speed);
}

static const struct rk_gmac_ops rk3399_ops = {
	.set_to_rgmii = rk3399_set_to_rgmii,
	.set_to_rmii = rk3399_set_to_rmii,
	.set_speed = rk3399_set_speed,
};

#define RK3528_VO_GRF_GMAC_CON		0x0018
#define RK3528_VO_GRF_MACPHY_CON0	0x001c
#define RK3528_VO_GRF_MACPHY_CON1	0x0020
#define RK3528_VPU_GRF_GMAC_CON5	0x0018
#define RK3528_VPU_GRF_GMAC_CON6	0x001c

#define RK3528_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3528_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3528_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(14)
#define RK3528_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(14)

#define RK3528_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0xFF, 8)
#define RK3528_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0xFF, 0)

#define RK3528_GMAC0_PHY_INTF_SEL_RMII	GRF_BIT(1)
#define RK3528_GMAC1_PHY_INTF_SEL_RGMII	GRF_CLR_BIT(8)
#define RK3528_GMAC1_PHY_INTF_SEL_RMII	GRF_BIT(8)

#define RK3528_GMAC1_CLK_SELECT_CRU	GRF_CLR_BIT(12)
#define RK3528_GMAC1_CLK_SELECT_IO	GRF_BIT(12)

#define RK3528_GMAC0_CLK_RMII_DIV2	GRF_BIT(3)
#define RK3528_GMAC0_CLK_RMII_DIV20	GRF_CLR_BIT(3)
#define RK3528_GMAC1_CLK_RMII_DIV2	GRF_BIT(10)
#define RK3528_GMAC1_CLK_RMII_DIV20	GRF_CLR_BIT(10)

#define RK3528_GMAC1_CLK_RGMII_DIV1	(GRF_CLR_BIT(11) | GRF_CLR_BIT(10))
#define RK3528_GMAC1_CLK_RGMII_DIV5	(GRF_BIT(11) | GRF_BIT(10))
#define RK3528_GMAC1_CLK_RGMII_DIV50	(GRF_BIT(11) | GRF_CLR_BIT(10))

#define RK3528_GMAC0_CLK_RMII_GATE	GRF_BIT(2)
#define RK3528_GMAC0_CLK_RMII_NOGATE	GRF_CLR_BIT(2)
#define RK3528_GMAC1_CLK_RMII_GATE	GRF_BIT(9)
#define RK3528_GMAC1_CLK_RMII_NOGATE	GRF_CLR_BIT(9)

static void rk3528_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON5,
		     RK3528_GMAC1_PHY_INTF_SEL_RGMII);

	regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON5,
		     DELAY_ENABLE(RK3528, tx_delay, rx_delay));

	regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON6,
		     RK3528_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3528_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3528_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	if (bsp_priv->id == 1)
		regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON5,
			     RK3528_GMAC1_PHY_INTF_SEL_RMII);
	else
		regmap_write(bsp_priv->grf, RK3528_VO_GRF_GMAC_CON,
			     RK3528_GMAC0_PHY_INTF_SEL_RMII |
			     RK3528_GMAC0_CLK_RMII_DIV2);
}

static const struct rk_reg_speed_data rk3528_gmac0_reg_speed_data = {
	.rmii_10 = RK3528_GMAC0_CLK_RMII_DIV20,
	.rmii_100 = RK3528_GMAC0_CLK_RMII_DIV2,
};

static const struct rk_reg_speed_data rk3528_gmac1_reg_speed_data = {
	.rgmii_10 = RK3528_GMAC1_CLK_RGMII_DIV50,
	.rgmii_100 = RK3528_GMAC1_CLK_RGMII_DIV5,
	.rgmii_1000 = RK3528_GMAC1_CLK_RGMII_DIV1,
	.rmii_10 = RK3528_GMAC1_CLK_RMII_DIV20,
	.rmii_100 = RK3528_GMAC1_CLK_RMII_DIV2,
};

static int rk3528_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	const struct rk_reg_speed_data *rsd;
	unsigned int reg;

	if (bsp_priv->id == 1) {
		rsd = &rk3528_gmac1_reg_speed_data;
		reg = RK3528_VPU_GRF_GMAC_CON5;
	} else {
		rsd = &rk3528_gmac0_reg_speed_data;
		reg = RK3528_VO_GRF_GMAC_CON;
	}

	return rk_set_reg_speed(bsp_priv, rsd, reg, interface, speed);
}

static void rk3528_set_clock_selection(struct rk_priv_data *bsp_priv,
				       bool input, bool enable)
{
	unsigned int val;

	if (bsp_priv->id == 1) {
		val = input ? RK3528_GMAC1_CLK_SELECT_IO :
			      RK3528_GMAC1_CLK_SELECT_CRU;
		val |= enable ? RK3528_GMAC1_CLK_RMII_NOGATE :
				RK3528_GMAC1_CLK_RMII_GATE;
		regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON5, val);
	} else {
		val = enable ? RK3528_GMAC0_CLK_RMII_NOGATE :
			       RK3528_GMAC0_CLK_RMII_GATE;
		regmap_write(bsp_priv->grf, RK3528_VO_GRF_GMAC_CON, val);
	}
}

static void rk3528_integrated_phy_powerup(struct rk_priv_data *bsp_priv)
{
	rk_gmac_integrated_fephy_powerup(bsp_priv, RK3528_VO_GRF_MACPHY_CON0);
}

static void rk3528_integrated_phy_powerdown(struct rk_priv_data *bsp_priv)
{
	rk_gmac_integrated_fephy_powerdown(bsp_priv, RK3528_VO_GRF_MACPHY_CON0);
}

static const struct rk_gmac_ops rk3528_ops = {
	.set_to_rgmii = rk3528_set_to_rgmii,
	.set_to_rmii = rk3528_set_to_rmii,
	.set_speed = rk3528_set_speed,
	.set_clock_selection = rk3528_set_clock_selection,
	.integrated_phy_powerup = rk3528_integrated_phy_powerup,
	.integrated_phy_powerdown = rk3528_integrated_phy_powerdown,
	.regs_valid = true,
	.regs = {
		0xffbd0000, /* gmac0 */
		0xffbe0000, /* gmac1 */
		0x0, /* sentinel */
	},
};

#define RK3568_GRF_GMAC0_CON0		0x0380
#define RK3568_GRF_GMAC0_CON1		0x0384
#define RK3568_GRF_GMAC1_CON0		0x0388
#define RK3568_GRF_GMAC1_CON1		0x038c

/* RK3568_GRF_GMAC0_CON1 && RK3568_GRF_GMAC1_CON1 */
#define RK3568_GMAC_PHY_INTF_SEL_RGMII	\
		(GRF_BIT(4) | GRF_CLR_BIT(5) | GRF_CLR_BIT(6))
#define RK3568_GMAC_PHY_INTF_SEL_RMII	\
		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | GRF_BIT(6))
#define RK3568_GMAC_FLOW_CTRL			GRF_BIT(3)
#define RK3568_GMAC_FLOW_CTRL_CLR		GRF_CLR_BIT(3)
#define RK3568_GMAC_RXCLK_DLY_ENABLE		GRF_BIT(1)
#define RK3568_GMAC_RXCLK_DLY_DISABLE		GRF_CLR_BIT(1)
#define RK3568_GMAC_TXCLK_DLY_ENABLE		GRF_BIT(0)
#define RK3568_GMAC_TXCLK_DLY_DISABLE		GRF_CLR_BIT(0)

/* RK3568_GRF_GMAC0_CON0 && RK3568_GRF_GMAC1_CON0 */
#define RK3568_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RK3568_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rk3568_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	u32 con0, con1;

	con0 = (bsp_priv->id == 1) ? RK3568_GRF_GMAC1_CON0 :
				     RK3568_GRF_GMAC0_CON0;
	con1 = (bsp_priv->id == 1) ? RK3568_GRF_GMAC1_CON1 :
				     RK3568_GRF_GMAC0_CON1;

	regmap_write(bsp_priv->grf, con0,
		     RK3568_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3568_GMAC_CLK_TX_DL_CFG(tx_delay));

	regmap_write(bsp_priv->grf, con1,
		     RK3568_GMAC_PHY_INTF_SEL_RGMII |
		     RK3568_GMAC_RXCLK_DLY_ENABLE |
		     RK3568_GMAC_TXCLK_DLY_ENABLE);
}

static void rk3568_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	u32 con1;

	con1 = (bsp_priv->id == 1) ? RK3568_GRF_GMAC1_CON1 :
				     RK3568_GRF_GMAC0_CON1;
	regmap_write(bsp_priv->grf, con1, RK3568_GMAC_PHY_INTF_SEL_RMII);
}

static const struct rk_gmac_ops rk3568_ops = {
	.set_to_rgmii = rk3568_set_to_rgmii,
	.set_to_rmii = rk3568_set_to_rmii,
	.set_speed = rk_set_clk_mac_speed,
	.regs_valid = true,
	.regs = {
		0xfe2a0000, /* gmac0 */
		0xfe010000, /* gmac1 */
		0x0, /* sentinel */
	},
};

/* VCCIO0_1_3_IOC */
#define RK3576_VCCIO0_1_3_IOC_CON2		0X6408
#define RK3576_VCCIO0_1_3_IOC_CON3		0X640c
#define RK3576_VCCIO0_1_3_IOC_CON4		0X6410
#define RK3576_VCCIO0_1_3_IOC_CON5		0X6414

#define RK3576_GMAC_RXCLK_DLY_ENABLE		GRF_BIT(15)
#define RK3576_GMAC_RXCLK_DLY_DISABLE		GRF_CLR_BIT(15)
#define RK3576_GMAC_TXCLK_DLY_ENABLE		GRF_BIT(7)
#define RK3576_GMAC_TXCLK_DLY_DISABLE		GRF_CLR_BIT(7)

#define RK3576_GMAC_CLK_RX_DL_CFG(val)		HIWORD_UPDATE(val, 0x7F, 8)
#define RK3576_GMAC_CLK_TX_DL_CFG(val)		HIWORD_UPDATE(val, 0x7F, 0)

/* SDGMAC_GRF */
#define RK3576_GRF_GMAC_CON0			0X0020
#define RK3576_GRF_GMAC_CON1			0X0024

#define RK3576_GMAC_RMII_MODE			GRF_BIT(3)
#define RK3576_GMAC_RGMII_MODE			GRF_CLR_BIT(3)

#define RK3576_GMAC_CLK_SELECT_IO		GRF_BIT(7)
#define RK3576_GMAC_CLK_SELECT_CRU		GRF_CLR_BIT(7)

#define RK3576_GMAC_CLK_RMII_DIV2		GRF_BIT(5)
#define RK3576_GMAC_CLK_RMII_DIV20		GRF_CLR_BIT(5)

#define RK3576_GMAC_CLK_RGMII_DIV1		\
			(GRF_CLR_BIT(6) | GRF_CLR_BIT(5))
#define RK3576_GMAC_CLK_RGMII_DIV5		\
			(GRF_BIT(6) | GRF_BIT(5))
#define RK3576_GMAC_CLK_RGMII_DIV50		\
			(GRF_BIT(6) | GRF_CLR_BIT(5))

#define RK3576_GMAC_CLK_RMII_GATE		GRF_BIT(4)
#define RK3576_GMAC_CLK_RMII_NOGATE		GRF_CLR_BIT(4)

static void rk3576_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	unsigned int offset_con;

	offset_con = bsp_priv->id == 1 ? RK3576_GRF_GMAC_CON1 :
					 RK3576_GRF_GMAC_CON0;

	regmap_write(bsp_priv->grf, offset_con, RK3576_GMAC_RGMII_MODE);

	offset_con = bsp_priv->id == 1 ? RK3576_VCCIO0_1_3_IOC_CON4 :
					 RK3576_VCCIO0_1_3_IOC_CON2;

	/* m0 && m1 delay enabled */
	regmap_write(bsp_priv->php_grf, offset_con,
		     DELAY_ENABLE(RK3576, tx_delay, rx_delay));
	regmap_write(bsp_priv->php_grf, offset_con + 0x4,
		     DELAY_ENABLE(RK3576, tx_delay, rx_delay));

	/* m0 && m1 delay value */
	regmap_write(bsp_priv->php_grf, offset_con,
		     RK3576_GMAC_CLK_TX_DL_CFG(tx_delay) |
		     RK3576_GMAC_CLK_RX_DL_CFG(rx_delay));
	regmap_write(bsp_priv->php_grf, offset_con + 0x4,
		     RK3576_GMAC_CLK_TX_DL_CFG(tx_delay) |
		     RK3576_GMAC_CLK_RX_DL_CFG(rx_delay));
}

static void rk3576_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	unsigned int offset_con;

	offset_con = bsp_priv->id == 1 ? RK3576_GRF_GMAC_CON1 :
					 RK3576_GRF_GMAC_CON0;

	regmap_write(bsp_priv->grf, offset_con, RK3576_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3578_reg_speed_data = {
	.rgmii_10 = RK3576_GMAC_CLK_RGMII_DIV50,
	.rgmii_100 = RK3576_GMAC_CLK_RGMII_DIV5,
	.rgmii_1000 = RK3576_GMAC_CLK_RGMII_DIV1,
	.rmii_10 = RK3576_GMAC_CLK_RMII_DIV20,
	.rmii_100 = RK3576_GMAC_CLK_RMII_DIV2,
};

static int rk3576_set_gmac_speed(struct rk_priv_data *bsp_priv,
				 phy_interface_t interface, int speed)
{
	unsigned int offset_con;

	offset_con = bsp_priv->id == 1 ? RK3576_GRF_GMAC_CON1 :
					 RK3576_GRF_GMAC_CON0;

	return rk_set_reg_speed(bsp_priv, &rk3578_reg_speed_data, offset_con,
				interface, speed);
}

static void rk3576_set_clock_selection(struct rk_priv_data *bsp_priv, bool input,
				       bool enable)
{
	unsigned int val = input ? RK3576_GMAC_CLK_SELECT_IO :
				   RK3576_GMAC_CLK_SELECT_CRU;
	unsigned int offset_con;

	val |= enable ? RK3576_GMAC_CLK_RMII_NOGATE :
			RK3576_GMAC_CLK_RMII_GATE;

	offset_con = bsp_priv->id == 1 ? RK3576_GRF_GMAC_CON1 :
					 RK3576_GRF_GMAC_CON0;

	regmap_write(bsp_priv->grf, offset_con, val);
}

static const struct rk_gmac_ops rk3576_ops = {
	.set_to_rgmii = rk3576_set_to_rgmii,
	.set_to_rmii = rk3576_set_to_rmii,
	.set_speed = rk3576_set_gmac_speed,
	.set_clock_selection = rk3576_set_clock_selection,
	.php_grf_required = true,
	.regs_valid = true,
	.regs = {
		0x2a220000, /* gmac0 */
		0x2a230000, /* gmac1 */
		0x0, /* sentinel */
	},
};

/* sys_grf */
#define RK3588_GRF_GMAC_CON7			0X031c
#define RK3588_GRF_GMAC_CON8			0X0320
#define RK3588_GRF_GMAC_CON9			0X0324

#define RK3588_GMAC_RXCLK_DLY_ENABLE(id)	GRF_BIT(2 * (id) + 3)
#define RK3588_GMAC_RXCLK_DLY_DISABLE(id)	GRF_CLR_BIT(2 * (id) + 3)
#define RK3588_GMAC_TXCLK_DLY_ENABLE(id)	GRF_BIT(2 * (id) + 2)
#define RK3588_GMAC_TXCLK_DLY_DISABLE(id)	GRF_CLR_BIT(2 * (id) + 2)

#define RK3588_GMAC_CLK_RX_DL_CFG(val)		HIWORD_UPDATE(val, 0xFF, 8)
#define RK3588_GMAC_CLK_TX_DL_CFG(val)		HIWORD_UPDATE(val, 0xFF, 0)

/* php_grf */
#define RK3588_GRF_GMAC_CON0			0X0008
#define RK3588_GRF_CLK_CON1			0X0070

#define RK3588_GMAC_PHY_INTF_SEL_RGMII(id)	\
	(GRF_BIT(3 + (id) * 6) | GRF_CLR_BIT(4 + (id) * 6) | GRF_CLR_BIT(5 + (id) * 6))
#define RK3588_GMAC_PHY_INTF_SEL_RMII(id)	\
	(GRF_CLR_BIT(3 + (id) * 6) | GRF_CLR_BIT(4 + (id) * 6) | GRF_BIT(5 + (id) * 6))

#define RK3588_GMAC_CLK_RMII_MODE(id)		GRF_BIT(5 * (id))
#define RK3588_GMAC_CLK_RGMII_MODE(id)		GRF_CLR_BIT(5 * (id))

#define RK3588_GMAC_CLK_SELECT_CRU(id)		GRF_BIT(5 * (id) + 4)
#define RK3588_GMAC_CLK_SELECT_IO(id)		GRF_CLR_BIT(5 * (id) + 4)

#define RK3588_GMA_CLK_RMII_DIV2(id)		GRF_BIT(5 * (id) + 2)
#define RK3588_GMA_CLK_RMII_DIV20(id)		GRF_CLR_BIT(5 * (id) + 2)

#define RK3588_GMAC_CLK_RGMII_DIV1(id)		\
			(GRF_CLR_BIT(5 * (id) + 2) | GRF_CLR_BIT(5 * (id) + 3))
#define RK3588_GMAC_CLK_RGMII_DIV5(id)		\
			(GRF_BIT(5 * (id) + 2) | GRF_BIT(5 * (id) + 3))
#define RK3588_GMAC_CLK_RGMII_DIV50(id)		\
			(GRF_CLR_BIT(5 * (id) + 2) | GRF_BIT(5 * (id) + 3))

#define RK3588_GMAC_CLK_RMII_GATE(id)		GRF_BIT(5 * (id) + 1)
#define RK3588_GMAC_CLK_RMII_NOGATE(id)		GRF_CLR_BIT(5 * (id) + 1)

static void rk3588_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	u32 offset_con, id = bsp_priv->id;

	offset_con = bsp_priv->id == 1 ? RK3588_GRF_GMAC_CON9 :
					 RK3588_GRF_GMAC_CON8;

	regmap_write(bsp_priv->php_grf, RK3588_GRF_GMAC_CON0,
		     RK3588_GMAC_PHY_INTF_SEL_RGMII(id));

	regmap_write(bsp_priv->php_grf, RK3588_GRF_CLK_CON1,
		     RK3588_GMAC_CLK_RGMII_MODE(id));

	regmap_write(bsp_priv->grf, RK3588_GRF_GMAC_CON7,
		     RK3588_GMAC_RXCLK_DLY_ENABLE(id) |
		     RK3588_GMAC_TXCLK_DLY_ENABLE(id));

	regmap_write(bsp_priv->grf, offset_con,
		     RK3588_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3588_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3588_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->php_grf, RK3588_GRF_GMAC_CON0,
		     RK3588_GMAC_PHY_INTF_SEL_RMII(bsp_priv->id));

	regmap_write(bsp_priv->php_grf, RK3588_GRF_CLK_CON1,
		     RK3588_GMAC_CLK_RMII_MODE(bsp_priv->id));
}

static int rk3588_set_gmac_speed(struct rk_priv_data *bsp_priv,
				 phy_interface_t interface, int speed)
{
	unsigned int val = 0, id = bsp_priv->id;

	switch (speed) {
	case 10:
		if (interface == PHY_INTERFACE_MODE_RMII)
			val = RK3588_GMA_CLK_RMII_DIV20(id);
		else
			val = RK3588_GMAC_CLK_RGMII_DIV50(id);
		break;
	case 100:
		if (interface == PHY_INTERFACE_MODE_RMII)
			val = RK3588_GMA_CLK_RMII_DIV2(id);
		else
			val = RK3588_GMAC_CLK_RGMII_DIV5(id);
		break;
	case 1000:
		if (interface != PHY_INTERFACE_MODE_RMII)
			val = RK3588_GMAC_CLK_RGMII_DIV1(id);
		else
			goto err;
		break;
	default:
		goto err;
	}

	regmap_write(bsp_priv->php_grf, RK3588_GRF_CLK_CON1, val);

	return 0;
err:
	return -EINVAL;
}

static void rk3588_set_clock_selection(struct rk_priv_data *bsp_priv, bool input,
				       bool enable)
{
	unsigned int val = input ? RK3588_GMAC_CLK_SELECT_IO(bsp_priv->id) :
				   RK3588_GMAC_CLK_SELECT_CRU(bsp_priv->id);

	val |= enable ? RK3588_GMAC_CLK_RMII_NOGATE(bsp_priv->id) :
			RK3588_GMAC_CLK_RMII_GATE(bsp_priv->id);

	regmap_write(bsp_priv->php_grf, RK3588_GRF_CLK_CON1, val);
}

static const struct rk_gmac_ops rk3588_ops = {
	.set_to_rgmii = rk3588_set_to_rgmii,
	.set_to_rmii = rk3588_set_to_rmii,
	.set_speed = rk3588_set_gmac_speed,
	.set_clock_selection = rk3588_set_clock_selection,
	.php_grf_required = true,
	.regs_valid = true,
	.regs = {
		0xfe1b0000, /* gmac0 */
		0xfe1c0000, /* gmac1 */
		0x0, /* sentinel */
	},
};

#define RV1108_GRF_GMAC_CON0		0X0900

/* RV1108_GRF_GMAC_CON0 */
#define RV1108_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | \
					GRF_BIT(6))
#define RV1108_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RV1108_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)
#define RV1108_GMAC_SPEED_10M		GRF_CLR_BIT(2)
#define RV1108_GMAC_SPEED_100M		GRF_BIT(2)
#define RV1108_GMAC_RMII_CLK_25M	GRF_BIT(7)
#define RV1108_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(7)

static void rv1108_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RV1108_GRF_GMAC_CON0,
		     RV1108_GMAC_PHY_INTF_SEL_RMII);
}

static const struct rk_reg_speed_data rv1108_reg_speed_data = {
	.rmii_10 = RV1108_GMAC_RMII_CLK_2_5M | RV1108_GMAC_SPEED_10M,
	.rmii_100 = RV1108_GMAC_RMII_CLK_25M | RV1108_GMAC_SPEED_100M,
};

static int rv1108_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rv1108_reg_speed_data,
				RV1108_GRF_GMAC_CON0, interface, speed);
}

static const struct rk_gmac_ops rv1108_ops = {
	.set_to_rmii = rv1108_set_to_rmii,
	.set_speed = rv1108_set_speed,
};

#define RV1126_GRF_GMAC_CON0		0X0070
#define RV1126_GRF_GMAC_CON1		0X0074
#define RV1126_GRF_GMAC_CON2		0X0078

/* RV1126_GRF_GMAC_CON0 */
#define RV1126_GMAC_PHY_INTF_SEL_RGMII	\
		(GRF_BIT(4) | GRF_CLR_BIT(5) | GRF_CLR_BIT(6))
#define RV1126_GMAC_PHY_INTF_SEL_RMII	\
		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | GRF_BIT(6))
#define RV1126_GMAC_FLOW_CTRL			GRF_BIT(7)
#define RV1126_GMAC_FLOW_CTRL_CLR		GRF_CLR_BIT(7)
#define RV1126_GMAC_M0_RXCLK_DLY_ENABLE		GRF_BIT(1)
#define RV1126_GMAC_M0_RXCLK_DLY_DISABLE	GRF_CLR_BIT(1)
#define RV1126_GMAC_M0_TXCLK_DLY_ENABLE		GRF_BIT(0)
#define RV1126_GMAC_M0_TXCLK_DLY_DISABLE	GRF_CLR_BIT(0)
#define RV1126_GMAC_M1_RXCLK_DLY_ENABLE		GRF_BIT(3)
#define RV1126_GMAC_M1_RXCLK_DLY_DISABLE	GRF_CLR_BIT(3)
#define RV1126_GMAC_M1_TXCLK_DLY_ENABLE		GRF_BIT(2)
#define RV1126_GMAC_M1_TXCLK_DLY_DISABLE	GRF_CLR_BIT(2)

/* RV1126_GRF_GMAC_CON1 */
#define RV1126_GMAC_M0_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RV1126_GMAC_M0_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)
/* RV1126_GRF_GMAC_CON2 */
#define RV1126_GMAC_M1_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RV1126_GMAC_M1_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rv1126_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON0,
		     RV1126_GMAC_PHY_INTF_SEL_RGMII |
		     RV1126_GMAC_M0_RXCLK_DLY_ENABLE |
		     RV1126_GMAC_M0_TXCLK_DLY_ENABLE |
		     RV1126_GMAC_M1_RXCLK_DLY_ENABLE |
		     RV1126_GMAC_M1_TXCLK_DLY_ENABLE);

	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON1,
		     RV1126_GMAC_M0_CLK_RX_DL_CFG(rx_delay) |
		     RV1126_GMAC_M0_CLK_TX_DL_CFG(tx_delay));

	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON2,
		     RV1126_GMAC_M1_CLK_RX_DL_CFG(rx_delay) |
		     RV1126_GMAC_M1_CLK_TX_DL_CFG(tx_delay));
}

static void rv1126_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON0,
		     RV1126_GMAC_PHY_INTF_SEL_RMII);
}

static const struct rk_gmac_ops rv1126_ops = {
	.set_to_rgmii = rv1126_set_to_rgmii,
	.set_to_rmii = rv1126_set_to_rmii,
	.set_speed = rk_set_clk_mac_speed,
};

static int rk_gmac_clk_init(struct plat_stmmacenet_data *plat)
{
	struct rk_priv_data *bsp_priv = plat->bsp_priv;
	int phy_iface = bsp_priv->phy_iface;
	struct device *dev = bsp_priv->dev;
	int i, j, ret;

	bsp_priv->clk_enabled = false;

	bsp_priv->num_clks = ARRAY_SIZE(rk_clocks);
	if (phy_iface == PHY_INTERFACE_MODE_RMII)
		bsp_priv->num_clks += ARRAY_SIZE(rk_rmii_clocks);

	bsp_priv->clks = devm_kcalloc(dev, bsp_priv->num_clks,
				      sizeof(*bsp_priv->clks), GFP_KERNEL);
	if (!bsp_priv->clks)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(rk_clocks); i++)
		bsp_priv->clks[i].id = rk_clocks[i];

	if (phy_iface == PHY_INTERFACE_MODE_RMII) {
		for (j = 0; j < ARRAY_SIZE(rk_rmii_clocks); j++)
			bsp_priv->clks[i++].id = rk_rmii_clocks[j];
	}

	ret = devm_clk_bulk_get_optional(dev, bsp_priv->num_clks,
					 bsp_priv->clks);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get clocks\n");

	if (bsp_priv->clock_input) {
		dev_info(dev, "clock input from PHY\n");
	} else if (phy_iface == PHY_INTERFACE_MODE_RMII) {
		clk_set_rate(plat->stmmac_clk, 50000000);
	}

	if (plat->phy_node && bsp_priv->integrated_phy) {
		bsp_priv->clk_phy = of_clk_get(plat->phy_node, 0);
		ret = PTR_ERR_OR_ZERO(bsp_priv->clk_phy);
		if (ret)
			return dev_err_probe(dev, ret, "Cannot get PHY clock\n");
		clk_set_rate(bsp_priv->clk_phy, 50000000);
	}

	return 0;
}

static int gmac_clk_enable(struct rk_priv_data *bsp_priv, bool enable)
{
	int ret;

	if (enable) {
		if (!bsp_priv->clk_enabled) {
			ret = clk_bulk_prepare_enable(bsp_priv->num_clks,
						      bsp_priv->clks);
			if (ret)
				return ret;

			ret = clk_prepare_enable(bsp_priv->clk_phy);
			if (ret)
				return ret;

			if (bsp_priv->ops && bsp_priv->ops->set_clock_selection)
				bsp_priv->ops->set_clock_selection(bsp_priv,
					       bsp_priv->clock_input, true);

			mdelay(5);
			bsp_priv->clk_enabled = true;
		}
	} else {
		if (bsp_priv->clk_enabled) {
			if (bsp_priv->ops && bsp_priv->ops->set_clock_selection) {
				bsp_priv->ops->set_clock_selection(bsp_priv,
					      bsp_priv->clock_input, false);
			}

			clk_bulk_disable_unprepare(bsp_priv->num_clks,
						   bsp_priv->clks);
			clk_disable_unprepare(bsp_priv->clk_phy);

			bsp_priv->clk_enabled = false;
		}
	}

	return 0;
}

static int phy_power_on(struct rk_priv_data *bsp_priv, bool enable)
{
	struct regulator *ldo = bsp_priv->regulator;
	struct device *dev = bsp_priv->dev;
	int ret;

	if (enable) {
		ret = regulator_enable(ldo);
		if (ret)
			dev_err(dev, "fail to enable phy-supply\n");
	} else {
		ret = regulator_disable(ldo);
		if (ret)
			dev_err(dev, "fail to disable phy-supply\n");
	}

	return 0;
}

static struct rk_priv_data *rk_gmac_setup(struct platform_device *pdev,
					  struct plat_stmmacenet_data *plat,
					  const struct rk_gmac_ops *ops)
{
	struct rk_priv_data *bsp_priv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;
	const char *strings = NULL;
	int value;

	bsp_priv = devm_kzalloc(dev, sizeof(*bsp_priv), GFP_KERNEL);
	if (!bsp_priv)
		return ERR_PTR(-ENOMEM);

	bsp_priv->phy_iface = plat->phy_interface;
	bsp_priv->ops = ops;

	/* Some SoCs have multiple MAC controllers, which need
	 * to be distinguished.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res && ops->regs_valid) {
		int i = 0;

		while (ops->regs[i]) {
			if (ops->regs[i] == res->start) {
				bsp_priv->id = i;
				break;
			}
			i++;
		}
	}

	bsp_priv->regulator = devm_regulator_get(dev, "phy");
	if (IS_ERR(bsp_priv->regulator)) {
		ret = PTR_ERR(bsp_priv->regulator);
		dev_err_probe(dev, ret, "failed to get phy regulator\n");
		return ERR_PTR(ret);
	}

	ret = of_property_read_string(dev->of_node, "clock_in_out", &strings);
	if (ret) {
		dev_err(dev, "Can not read property: clock_in_out.\n");
		bsp_priv->clock_input = true;
	} else {
		dev_info(dev, "clock input or output? (%s).\n",
			 strings);
		if (!strcmp(strings, "input"))
			bsp_priv->clock_input = true;
		else
			bsp_priv->clock_input = false;
	}

	ret = of_property_read_u32(dev->of_node, "tx_delay", &value);
	if (ret) {
		bsp_priv->tx_delay = 0x30;
		dev_err(dev, "Can not read property: tx_delay.");
		dev_err(dev, "set tx_delay to 0x%x\n",
			bsp_priv->tx_delay);
	} else {
		dev_info(dev, "TX delay(0x%x).\n", value);
		bsp_priv->tx_delay = value;
	}

	ret = of_property_read_u32(dev->of_node, "rx_delay", &value);
	if (ret) {
		bsp_priv->rx_delay = 0x10;
		dev_err(dev, "Can not read property: rx_delay.");
		dev_err(dev, "set rx_delay to 0x%x\n",
			bsp_priv->rx_delay);
	} else {
		dev_info(dev, "RX delay(0x%x).\n", value);
		bsp_priv->rx_delay = value;
	}

	bsp_priv->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							"rockchip,grf");
	if (IS_ERR(bsp_priv->grf)) {
		dev_err_probe(dev, PTR_ERR(bsp_priv->grf),
			      "failed to lookup rockchip,grf\n");
		return ERR_CAST(bsp_priv->grf);
	}

	if (ops->php_grf_required) {
		bsp_priv->php_grf =
			syscon_regmap_lookup_by_phandle(dev->of_node,
							"rockchip,php-grf");
		if (IS_ERR(bsp_priv->php_grf)) {
			dev_err_probe(dev, PTR_ERR(bsp_priv->php_grf),
				      "failed to lookup rockchip,php-grf\n");
			return ERR_CAST(bsp_priv->php_grf);
		}
	}

	if (plat->phy_node) {
		bsp_priv->integrated_phy = of_property_read_bool(plat->phy_node,
								 "phy-is-integrated");
		if (bsp_priv->integrated_phy) {
			bsp_priv->phy_reset = of_reset_control_get(plat->phy_node, NULL);
			if (IS_ERR(bsp_priv->phy_reset)) {
				dev_err(&pdev->dev, "No PHY reset control found.\n");
				bsp_priv->phy_reset = NULL;
			}
		}
	}
	dev_info(dev, "integrated PHY? (%s).\n",
		 bsp_priv->integrated_phy ? "yes" : "no");

	bsp_priv->dev = dev;

	return bsp_priv;
}

static int rk_gmac_check_ops(struct rk_priv_data *bsp_priv)
{
	switch (bsp_priv->phy_iface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		if (!bsp_priv->ops->set_to_rgmii)
			return -EINVAL;
		break;
	case PHY_INTERFACE_MODE_RMII:
		if (!bsp_priv->ops->set_to_rmii)
			return -EINVAL;
		break;
	default:
		dev_err(bsp_priv->dev,
			"unsupported interface %d", bsp_priv->phy_iface);
	}
	return 0;
}

static int rk_gmac_powerup(struct rk_priv_data *bsp_priv)
{
	struct device *dev = bsp_priv->dev;
	int ret;

	ret = rk_gmac_check_ops(bsp_priv);
	if (ret)
		return ret;

	ret = gmac_clk_enable(bsp_priv, true);
	if (ret)
		return ret;

	/*rmii or rgmii*/
	switch (bsp_priv->phy_iface) {
	case PHY_INTERFACE_MODE_RGMII:
		dev_info(dev, "init for RGMII\n");
		bsp_priv->ops->set_to_rgmii(bsp_priv, bsp_priv->tx_delay,
					    bsp_priv->rx_delay);
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		dev_info(dev, "init for RGMII_ID\n");
		bsp_priv->ops->set_to_rgmii(bsp_priv, 0, 0);
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		dev_info(dev, "init for RGMII_RXID\n");
		bsp_priv->ops->set_to_rgmii(bsp_priv, bsp_priv->tx_delay, 0);
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		dev_info(dev, "init for RGMII_TXID\n");
		bsp_priv->ops->set_to_rgmii(bsp_priv, 0, bsp_priv->rx_delay);
		break;
	case PHY_INTERFACE_MODE_RMII:
		dev_info(dev, "init for RMII\n");
		bsp_priv->ops->set_to_rmii(bsp_priv);
		break;
	default:
		dev_err(dev, "NO interface defined!\n");
	}

	ret = phy_power_on(bsp_priv, true);
	if (ret) {
		gmac_clk_enable(bsp_priv, false);
		return ret;
	}

	pm_runtime_get_sync(dev);

	if (bsp_priv->integrated_phy && bsp_priv->ops->integrated_phy_powerup)
		bsp_priv->ops->integrated_phy_powerup(bsp_priv);

	return 0;
}

static void rk_gmac_powerdown(struct rk_priv_data *gmac)
{
	if (gmac->integrated_phy && gmac->ops->integrated_phy_powerdown)
		gmac->ops->integrated_phy_powerdown(gmac);

	pm_runtime_put_sync(gmac->dev);

	phy_power_on(gmac, false);
	gmac_clk_enable(gmac, false);
}

static void rk_get_interfaces(struct stmmac_priv *priv, void *bsp_priv,
			      unsigned long *interfaces)
{
	struct rk_priv_data *rk = bsp_priv;

	if (rk->ops->set_to_rgmii)
		phy_interface_set_rgmii(interfaces);

	if (rk->ops->set_to_rmii)
		__set_bit(PHY_INTERFACE_MODE_RMII, interfaces);
}

static int rk_set_clk_tx_rate(void *bsp_priv_, struct clk *clk_tx_i,
			      phy_interface_t interface, int speed)
{
	struct rk_priv_data *bsp_priv = bsp_priv_;

	if (bsp_priv->ops->set_speed)
		return bsp_priv->ops->set_speed(bsp_priv, bsp_priv->phy_iface,
						speed);

	return -EINVAL;
}

static int rk_gmac_suspend(struct device *dev, void *bsp_priv_)
{
	struct rk_priv_data *bsp_priv = bsp_priv_;

	/* Keep the PHY up if we use Wake-on-Lan. */
	if (!device_may_wakeup(dev))
		rk_gmac_powerdown(bsp_priv);

	return 0;
}

static int rk_gmac_resume(struct device *dev, void *bsp_priv_)
{
	struct rk_priv_data *bsp_priv = bsp_priv_;

	/* The PHY was up for Wake-on-Lan. */
	if (!device_may_wakeup(dev))
		rk_gmac_powerup(bsp_priv);

	return 0;
}

static int rk_gmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	const struct rk_gmac_ops *data;
	int ret;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "no of match data provided\n");
		return -EINVAL;
	}

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	/* If the stmmac is not already selected as gmac4,
	 * then make sure we fallback to gmac.
	 */
	if (!plat_dat->has_gmac4) {
		plat_dat->has_gmac = true;
		plat_dat->rx_fifo_size = 4096;
		plat_dat->tx_fifo_size = 2048;
	}

	plat_dat->get_interfaces = rk_get_interfaces;
	plat_dat->set_clk_tx_rate = rk_set_clk_tx_rate;
	plat_dat->suspend = rk_gmac_suspend;
	plat_dat->resume = rk_gmac_resume;

	plat_dat->bsp_priv = rk_gmac_setup(pdev, plat_dat, data);
	if (IS_ERR(plat_dat->bsp_priv))
		return PTR_ERR(plat_dat->bsp_priv);

	ret = rk_gmac_clk_init(plat_dat);
	if (ret)
		return ret;

	ret = rk_gmac_powerup(plat_dat->bsp_priv);
	if (ret)
		return ret;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_gmac_powerdown;

	return 0;

err_gmac_powerdown:
	rk_gmac_powerdown(plat_dat->bsp_priv);

	return ret;
}

static void rk_gmac_remove(struct platform_device *pdev)
{
	struct stmmac_priv *priv = netdev_priv(platform_get_drvdata(pdev));
	struct rk_priv_data *bsp_priv = priv->plat->bsp_priv;

	stmmac_dvr_remove(&pdev->dev);

	rk_gmac_powerdown(bsp_priv);

	if (priv->plat->phy_node && bsp_priv->integrated_phy)
		clk_put(bsp_priv->clk_phy);
}

static const struct of_device_id rk_gmac_dwmac_match[] = {
	{ .compatible = "rockchip,px30-gmac",	.data = &px30_ops   },
	{ .compatible = "rockchip,rk3128-gmac", .data = &rk3128_ops },
	{ .compatible = "rockchip,rk3228-gmac", .data = &rk3228_ops },
	{ .compatible = "rockchip,rk3288-gmac", .data = &rk3288_ops },
	{ .compatible = "rockchip,rk3308-gmac", .data = &rk3308_ops },
	{ .compatible = "rockchip,rk3328-gmac", .data = &rk3328_ops },
	{ .compatible = "rockchip,rk3366-gmac", .data = &rk3366_ops },
	{ .compatible = "rockchip,rk3368-gmac", .data = &rk3368_ops },
	{ .compatible = "rockchip,rk3399-gmac", .data = &rk3399_ops },
	{ .compatible = "rockchip,rk3528-gmac", .data = &rk3528_ops },
	{ .compatible = "rockchip,rk3568-gmac", .data = &rk3568_ops },
	{ .compatible = "rockchip,rk3576-gmac", .data = &rk3576_ops },
	{ .compatible = "rockchip,rk3588-gmac", .data = &rk3588_ops },
	{ .compatible = "rockchip,rv1108-gmac", .data = &rv1108_ops },
	{ .compatible = "rockchip,rv1126-gmac", .data = &rv1126_ops },
	{ }
};
MODULE_DEVICE_TABLE(of, rk_gmac_dwmac_match);

static struct platform_driver rk_gmac_dwmac_driver = {
	.probe  = rk_gmac_probe,
	.remove = rk_gmac_remove,
	.driver = {
		.name           = "rk_gmac-dwmac",
		.pm		= &stmmac_simple_pm_ops,
		.of_match_table = rk_gmac_dwmac_match,
	},
};
module_platform_driver(rk_gmac_dwmac_driver);

MODULE_AUTHOR("Chen-Zhi (Roger Chen) <roger.chen@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK3288 DWMAC specific glue layer");
MODULE_LICENSE("GPL");
