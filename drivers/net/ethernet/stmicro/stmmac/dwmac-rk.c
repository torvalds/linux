/**
 * dwmac-rk.c - Rockchip RK3288 DWMAC specific glue layer
 *
 * Copyright (C) 2014 Chen-Zhi (Roger Chen)
 *
 * Chen-Zhi (Roger Chen)  <roger.chen@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/stmmac.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/phy.h>
#include <linux/of_net.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

struct rk_priv_data {
	struct platform_device *pdev;
	int phy_iface;
	char regulator[32];

	bool clk_enabled;
	bool clock_input;

	struct clk *clk_mac;
	struct clk *clk_mac_pll;
	struct clk *gmac_clkin;
	struct clk *mac_clk_rx;
	struct clk *mac_clk_tx;
	struct clk *clk_mac_ref;
	struct clk *clk_mac_refout;
	struct clk *aclk_mac;
	struct clk *pclk_mac;

	int tx_delay;
	int rx_delay;

	struct regmap *grf;
};

#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

#define GRF_BIT(nr)	(BIT(nr) | BIT(nr+16))
#define GRF_CLR_BIT(nr)	(BIT(nr+16))

#define RK3288_GRF_SOC_CON1	0x0248
#define RK3288_GRF_SOC_CON3	0x0250
#define RK3288_GRF_GPIO3D_E	0x01ec
#define RK3288_GRF_GPIO4A_E	0x01f0
#define RK3288_GRF_GPIO4B_E	0x01f4

/*RK3288_GRF_SOC_CON1*/
#define GMAC_PHY_INTF_SEL_RGMII	(GRF_BIT(6) | GRF_CLR_BIT(7) | GRF_CLR_BIT(8))
#define GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(6) | GRF_CLR_BIT(7) | GRF_BIT(8))
#define GMAC_FLOW_CTRL		GRF_BIT(9)
#define GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(9)
#define GMAC_SPEED_10M		GRF_CLR_BIT(10)
#define GMAC_SPEED_100M		GRF_BIT(10)
#define GMAC_RMII_CLK_25M	GRF_BIT(11)
#define GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(11)
#define GMAC_CLK_125M		(GRF_CLR_BIT(12) | GRF_CLR_BIT(13))
#define GMAC_CLK_25M		(GRF_BIT(12) | GRF_BIT(13))
#define GMAC_CLK_2_5M		(GRF_CLR_BIT(12) | GRF_BIT(13))
#define GMAC_RMII_MODE		GRF_BIT(14)
#define GMAC_RMII_MODE_CLR	GRF_CLR_BIT(14)

/*RK3288_GRF_SOC_CON3*/
#define GMAC_TXCLK_DLY_ENABLE	GRF_BIT(14)
#define GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(14)
#define GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 7)
#define GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void set_to_rgmii(struct rk_priv_data *bsp_priv,
			 int tx_delay, int rx_delay)
{
	struct device *dev = &bsp_priv->pdev->dev;

	if (IS_ERR(bsp_priv->grf)) {
		dev_err(dev, "%s: Missing rockchip,grf property\n", __func__);
		return;
	}

	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
		     GMAC_PHY_INTF_SEL_RGMII | GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON3,
		     GMAC_RXCLK_DLY_ENABLE | GMAC_TXCLK_DLY_ENABLE |
		     GMAC_CLK_RX_DL_CFG(rx_delay) |
		     GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void set_to_rmii(struct rk_priv_data *bsp_priv)
{
	struct device *dev = &bsp_priv->pdev->dev;

	if (IS_ERR(bsp_priv->grf)) {
		dev_err(dev, "%s: Missing rockchip,grf property\n", __func__);
		return;
	}

	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
		     GMAC_PHY_INTF_SEL_RMII | GMAC_RMII_MODE);
}

static void set_rgmii_speed(struct rk_priv_data *bsp_priv, int speed)
{
	struct device *dev = &bsp_priv->pdev->dev;

	if (IS_ERR(bsp_priv->grf)) {
		dev_err(dev, "%s: Missing rockchip,grf property\n", __func__);
		return;
	}

	if (speed == 10)
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1, GMAC_CLK_2_5M);
	else if (speed == 100)
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1, GMAC_CLK_25M);
	else if (speed == 1000)
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1, GMAC_CLK_125M);
	else
		dev_err(dev, "unknown speed value for RGMII! speed=%d", speed);
}

static void set_rmii_speed(struct rk_priv_data *bsp_priv, int speed)
{
	struct device *dev = &bsp_priv->pdev->dev;

	if (IS_ERR(bsp_priv->grf)) {
		dev_err(dev, "%s: Missing rockchip,grf property\n", __func__);
		return;
	}

	if (speed == 10) {
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
			     GMAC_RMII_CLK_2_5M | GMAC_SPEED_10M);
	} else if (speed == 100) {
		regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
			     GMAC_RMII_CLK_25M | GMAC_SPEED_100M);
	} else {
		dev_err(dev, "unknown speed value for RMII! speed=%d", speed);
	}
}

static int gmac_clk_init(struct rk_priv_data *bsp_priv)
{
	struct device *dev = &bsp_priv->pdev->dev;

	bsp_priv->clk_enabled = false;

	bsp_priv->mac_clk_rx = devm_clk_get(dev, "mac_clk_rx");
	if (IS_ERR(bsp_priv->mac_clk_rx))
		dev_err(dev, "%s: cannot get clock %s\n",
			__func__, "mac_clk_rx");

	bsp_priv->mac_clk_tx = devm_clk_get(dev, "mac_clk_tx");
	if (IS_ERR(bsp_priv->mac_clk_tx))
		dev_err(dev, "%s: cannot get clock %s\n",
			__func__, "mac_clk_tx");

	bsp_priv->aclk_mac = devm_clk_get(dev, "aclk_mac");
	if (IS_ERR(bsp_priv->aclk_mac))
		dev_err(dev, "%s: cannot get clock %s\n",
			__func__, "aclk_mac");

	bsp_priv->pclk_mac = devm_clk_get(dev, "pclk_mac");
	if (IS_ERR(bsp_priv->pclk_mac))
		dev_err(dev, "%s: cannot get clock %s\n",
			__func__, "pclk_mac");

	bsp_priv->clk_mac = devm_clk_get(dev, "stmmaceth");
	if (IS_ERR(bsp_priv->clk_mac))
		dev_err(dev, "%s: cannot get clock %s\n",
			__func__, "stmmaceth");

	if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RMII) {
		bsp_priv->clk_mac_ref = devm_clk_get(dev, "clk_mac_ref");
		if (IS_ERR(bsp_priv->clk_mac_ref))
			dev_err(dev, "%s: cannot get clock %s\n",
				__func__, "clk_mac_ref");

		if (!bsp_priv->clock_input) {
			bsp_priv->clk_mac_refout =
				devm_clk_get(dev, "clk_mac_refout");
			if (IS_ERR(bsp_priv->clk_mac_refout))
				dev_err(dev, "%s: cannot get clock %s\n",
					__func__, "clk_mac_refout");
		}
	}

	if (bsp_priv->clock_input) {
		dev_info(dev, "%s: clock input from PHY\n", __func__);
	} else {
		if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RMII)
			clk_set_rate(bsp_priv->clk_mac_pll, 50000000);
	}

	return 0;
}

static int gmac_clk_enable(struct rk_priv_data *bsp_priv, bool enable)
{
	int phy_iface = phy_iface = bsp_priv->phy_iface;

	if (enable) {
		if (!bsp_priv->clk_enabled) {
			if (phy_iface == PHY_INTERFACE_MODE_RMII) {
				if (!IS_ERR(bsp_priv->mac_clk_rx))
					clk_prepare_enable(
						bsp_priv->mac_clk_rx);

				if (!IS_ERR(bsp_priv->clk_mac_ref))
					clk_prepare_enable(
						bsp_priv->clk_mac_ref);

				if (!IS_ERR(bsp_priv->clk_mac_refout))
					clk_prepare_enable(
						bsp_priv->clk_mac_refout);
			}

			if (!IS_ERR(bsp_priv->aclk_mac))
				clk_prepare_enable(bsp_priv->aclk_mac);

			if (!IS_ERR(bsp_priv->pclk_mac))
				clk_prepare_enable(bsp_priv->pclk_mac);

			if (!IS_ERR(bsp_priv->mac_clk_tx))
				clk_prepare_enable(bsp_priv->mac_clk_tx);

			/**
			 * if (!IS_ERR(bsp_priv->clk_mac))
			 *	clk_prepare_enable(bsp_priv->clk_mac);
			 */
			mdelay(5);
			bsp_priv->clk_enabled = true;
		}
	} else {
		if (bsp_priv->clk_enabled) {
			if (phy_iface == PHY_INTERFACE_MODE_RMII) {
				if (!IS_ERR(bsp_priv->mac_clk_rx))
					clk_disable_unprepare(
						bsp_priv->mac_clk_rx);

				if (!IS_ERR(bsp_priv->clk_mac_ref))
					clk_disable_unprepare(
						bsp_priv->clk_mac_ref);

				if (!IS_ERR(bsp_priv->clk_mac_refout))
					clk_disable_unprepare(
						bsp_priv->clk_mac_refout);
			}

			if (!IS_ERR(bsp_priv->aclk_mac))
				clk_disable_unprepare(bsp_priv->aclk_mac);

			if (!IS_ERR(bsp_priv->pclk_mac))
				clk_disable_unprepare(bsp_priv->pclk_mac);

			if (!IS_ERR(bsp_priv->mac_clk_tx))
				clk_disable_unprepare(bsp_priv->mac_clk_tx);
			/**
			 * if (!IS_ERR(bsp_priv->clk_mac))
			 *	clk_disable_unprepare(bsp_priv->clk_mac);
			 */
			bsp_priv->clk_enabled = false;
		}
	}

	return 0;
}

static int phy_power_on(struct rk_priv_data *bsp_priv, bool enable)
{
	struct regulator *ldo;
	char *ldostr = bsp_priv->regulator;
	int ret;
	struct device *dev = &bsp_priv->pdev->dev;

	if (!ldostr) {
		dev_err(dev, "%s: no ldo found\n", __func__);
		return -1;
	}

	ldo = regulator_get(NULL, ldostr);
	if (!ldo) {
		dev_err(dev, "\n%s get ldo %s failed\n", __func__, ldostr);
	} else {
		if (enable) {
			if (!regulator_is_enabled(ldo)) {
				regulator_set_voltage(ldo, 3300000, 3300000);
				ret = regulator_enable(ldo);
				if (ret != 0)
					dev_err(dev, "%s: fail to enable %s\n",
						__func__, ldostr);
				else
					dev_info(dev, "turn on ldo done.\n");
			} else {
				dev_warn(dev, "%s is enabled before enable",
					 ldostr);
			}
		} else {
			if (regulator_is_enabled(ldo)) {
				ret = regulator_disable(ldo);
				if (ret != 0)
					dev_err(dev, "%s: fail to disable %s\n",
						__func__, ldostr);
				else
					dev_info(dev, "turn off ldo done.\n");
			} else {
				dev_warn(dev, "%s is disabled before disable",
					 ldostr);
			}
		}
		regulator_put(ldo);
	}

	return 0;
}

static void *rk_gmac_setup(struct platform_device *pdev)
{
	struct rk_priv_data *bsp_priv;
	struct device *dev = &pdev->dev;
	int ret;
	const char *strings = NULL;
	int value;

	bsp_priv = devm_kzalloc(dev, sizeof(*bsp_priv), GFP_KERNEL);
	if (!bsp_priv)
		return ERR_PTR(-ENOMEM);

	bsp_priv->phy_iface = of_get_phy_mode(dev->of_node);

	ret = of_property_read_string(dev->of_node, "phy_regulator", &strings);
	if (ret) {
		dev_warn(dev, "%s: Can not read property: phy_regulator.\n",
			 __func__);
	} else {
		dev_info(dev, "%s: PHY power controlled by regulator(%s).\n",
			 __func__, strings);
		strcpy(bsp_priv->regulator, strings);
	}

	ret = of_property_read_string(dev->of_node, "clock_in_out", &strings);
	if (ret) {
		dev_err(dev, "%s: Can not read property: clock_in_out.\n",
			__func__);
		bsp_priv->clock_input = true;
	} else {
		dev_info(dev, "%s: clock input or output? (%s).\n",
			 __func__, strings);
		if (!strcmp(strings, "input"))
			bsp_priv->clock_input = true;
		else
			bsp_priv->clock_input = false;
	}

	ret = of_property_read_u32(dev->of_node, "tx_delay", &value);
	if (ret) {
		bsp_priv->tx_delay = 0x30;
		dev_err(dev, "%s: Can not read property: tx_delay.", __func__);
		dev_err(dev, "%s: set tx_delay to 0x%x\n",
			__func__, bsp_priv->tx_delay);
	} else {
		dev_info(dev, "%s: TX delay(0x%x).\n", __func__, value);
		bsp_priv->tx_delay = value;
	}

	ret = of_property_read_u32(dev->of_node, "rx_delay", &value);
	if (ret) {
		bsp_priv->rx_delay = 0x10;
		dev_err(dev, "%s: Can not read property: rx_delay.", __func__);
		dev_err(dev, "%s: set rx_delay to 0x%x\n",
			__func__, bsp_priv->rx_delay);
	} else {
		dev_info(dev, "%s: RX delay(0x%x).\n", __func__, value);
		bsp_priv->rx_delay = value;
	}

	bsp_priv->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							"rockchip,grf");
	bsp_priv->pdev = pdev;

	/*rmii or rgmii*/
	if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RGMII) {
		dev_info(dev, "%s: init for RGMII\n", __func__);
		set_to_rgmii(bsp_priv, bsp_priv->tx_delay, bsp_priv->rx_delay);
	} else if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RMII) {
		dev_info(dev, "%s: init for RMII\n", __func__);
		set_to_rmii(bsp_priv);
	} else {
		dev_err(dev, "%s: NO interface defined!\n", __func__);
	}

	gmac_clk_init(bsp_priv);

	return bsp_priv;
}

static int rk_gmac_init(struct platform_device *pdev, void *priv)
{
	struct rk_priv_data *bsp_priv = priv;
	int ret;

	ret = phy_power_on(bsp_priv, true);
	if (ret)
		return ret;

	ret = gmac_clk_enable(bsp_priv, true);
	if (ret)
		return ret;

	return 0;
}

static void rk_gmac_exit(struct platform_device *pdev, void *priv)
{
	struct rk_priv_data *gmac = priv;

	phy_power_on(gmac, false);
	gmac_clk_enable(gmac, false);
}

static void rk_fix_speed(void *priv, unsigned int speed)
{
	struct rk_priv_data *bsp_priv = priv;
	struct device *dev = &bsp_priv->pdev->dev;

	if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RGMII)
		set_rgmii_speed(bsp_priv, speed);
	else if (bsp_priv->phy_iface == PHY_INTERFACE_MODE_RMII)
		set_rmii_speed(bsp_priv, speed);
	else
		dev_err(dev, "unsupported interface %d", bsp_priv->phy_iface);
}

const struct stmmac_of_data rk3288_gmac_data = {
	.has_gmac = 1,
	.fix_mac_speed = rk_fix_speed,
	.setup = rk_gmac_setup,
	.init = rk_gmac_init,
	.exit = rk_gmac_exit,
};
