/*
 * dwmac-sti.c - STMicroelectronics DWMAC Specific Glue layer
 *
 * Copyright (C) 2003-2014 STMicroelectronics (R&D) Limited
 * Author: Srinivas Kandagatla <srinivas.kandagatla@st.com>
 * Contributors: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/stmmac.h>
#include <linux/phy.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>

#include "stmmac_platform.h"

#define DWMAC_125MHZ	125000000
#define DWMAC_50MHZ	50000000
#define DWMAC_25MHZ	25000000
#define DWMAC_2_5MHZ	2500000

#define IS_PHY_IF_MODE_RGMII(iface)	(iface == PHY_INTERFACE_MODE_RGMII || \
			iface == PHY_INTERFACE_MODE_RGMII_ID || \
			iface == PHY_INTERFACE_MODE_RGMII_RXID || \
			iface == PHY_INTERFACE_MODE_RGMII_TXID)

#define IS_PHY_IF_MODE_GBIT(iface)	(IS_PHY_IF_MODE_RGMII(iface) || \
					 iface == PHY_INTERFACE_MODE_GMII)

/* STiH4xx register definitions (STiH415/STiH416/STiH407/STiH410 families)
 *
 * Below table summarizes the clock requirement and clock sources for
 * supported phy interface modes with link speeds.
 * ________________________________________________
 *|  PHY_MODE	| 1000 Mbit Link | 100 Mbit Link   |
 * ------------------------------------------------
 *|	MII	|	n/a	 |	25Mhz	   |
 *|		|		 |	txclk	   |
 * ------------------------------------------------
 *|	GMII	|     125Mhz	 |	25Mhz	   |
 *|		|  clk-125/txclk |	txclk	   |
 * ------------------------------------------------
 *|	RGMII	|     125Mhz	 |	25Mhz	   |
 *|		|  clk-125/txclk |	clkgen     |
 *|		|    clkgen	 |		   |
 * ------------------------------------------------
 *|	RMII	|	n/a	 |	25Mhz	   |
 *|		|		 |clkgen/phyclk-in |
 * ------------------------------------------------
 *
 *	  Register Configuration
 *-------------------------------
 * src	 |BIT(8)| BIT(7)| BIT(6)|
 *-------------------------------
 * txclk |   0	|  n/a	|   1	|
 *-------------------------------
 * ck_125|   0	|  n/a	|   0	|
 *-------------------------------
 * phyclk|   1	|   0	|  n/a	|
 *-------------------------------
 * clkgen|   1	|   1	|  n/a	|
 *-------------------------------
 */

#define STIH4XX_RETIME_SRC_MASK			GENMASK(8, 6)
#define STIH4XX_ETH_SEL_TX_RETIME_CLK		BIT(8)
#define STIH4XX_ETH_SEL_INTERNAL_NOTEXT_PHYCLK	BIT(7)
#define STIH4XX_ETH_SEL_TXCLK_NOT_CLK125	BIT(6)

/* STiD127 register definitions
 *-----------------------
 * src	 |BIT(6)| BIT(7)|
 *-----------------------
 * MII   |  1	|   n/a	|
 *-----------------------
 * RMII  |  n/a	|   1	|
 * clkgen|	|	|
 *-----------------------
 * RMII  |  n/a	|   0	|
 * phyclk|	|	|
 *-----------------------
 * RGMII |  1	|  n/a	|
 * clkgen|	|	|
 *-----------------------
 */

#define STID127_RETIME_SRC_MASK			GENMASK(7, 6)
#define STID127_ETH_SEL_INTERNAL_NOTEXT_PHYCLK	BIT(7)
#define STID127_ETH_SEL_INTERNAL_NOTEXT_TXCLK	BIT(6)

#define ENMII_MASK	GENMASK(5, 5)
#define ENMII		BIT(5)
#define EN_MASK		GENMASK(1, 1)
#define EN		BIT(1)

/*
 * 3 bits [4:2]
 *	000-GMII/MII
 *	001-RGMII
 *	010-SGMII
 *	100-RMII
 */
#define MII_PHY_SEL_MASK	GENMASK(4, 2)
#define ETH_PHY_SEL_RMII	BIT(4)
#define ETH_PHY_SEL_SGMII	BIT(3)
#define ETH_PHY_SEL_RGMII	BIT(2)
#define ETH_PHY_SEL_GMII	0x0
#define ETH_PHY_SEL_MII		0x0

struct sti_dwmac {
	int interface;		/* MII interface */
	bool ext_phyclk;	/* Clock from external PHY */
	u32 tx_retime_src;	/* TXCLK Retiming*/
	struct clk *clk;	/* PHY clock */
	u32 ctrl_reg;		/* GMAC glue-logic control register */
	int clk_sel_reg;	/* GMAC ext clk selection register */
	struct device *dev;
	struct regmap *regmap;
	u32 speed;
	void (*fix_retime_src)(void *priv, unsigned int speed);
};

struct sti_dwmac_of_data {
	void (*fix_retime_src)(void *priv, unsigned int speed);
};

static u32 phy_intf_sels[] = {
	[PHY_INTERFACE_MODE_MII] = ETH_PHY_SEL_MII,
	[PHY_INTERFACE_MODE_GMII] = ETH_PHY_SEL_GMII,
	[PHY_INTERFACE_MODE_RGMII] = ETH_PHY_SEL_RGMII,
	[PHY_INTERFACE_MODE_RGMII_ID] = ETH_PHY_SEL_RGMII,
	[PHY_INTERFACE_MODE_SGMII] = ETH_PHY_SEL_SGMII,
	[PHY_INTERFACE_MODE_RMII] = ETH_PHY_SEL_RMII,
};

enum {
	TX_RETIME_SRC_NA = 0,
	TX_RETIME_SRC_TXCLK = 1,
	TX_RETIME_SRC_CLK_125,
	TX_RETIME_SRC_PHYCLK,
	TX_RETIME_SRC_CLKGEN,
};

static u32 stih4xx_tx_retime_val[] = {
	[TX_RETIME_SRC_TXCLK] = STIH4XX_ETH_SEL_TXCLK_NOT_CLK125,
	[TX_RETIME_SRC_CLK_125] = 0x0,
	[TX_RETIME_SRC_PHYCLK] = STIH4XX_ETH_SEL_TX_RETIME_CLK,
	[TX_RETIME_SRC_CLKGEN] = STIH4XX_ETH_SEL_TX_RETIME_CLK
				 | STIH4XX_ETH_SEL_INTERNAL_NOTEXT_PHYCLK,
};

static void stih4xx_fix_retime_src(void *priv, u32 spd)
{
	struct sti_dwmac *dwmac = priv;
	u32 src = dwmac->tx_retime_src;
	u32 reg = dwmac->ctrl_reg;
	u32 freq = 0;

	if (dwmac->interface == PHY_INTERFACE_MODE_MII) {
		src = TX_RETIME_SRC_TXCLK;
	} else if (dwmac->interface == PHY_INTERFACE_MODE_RMII) {
		if (dwmac->ext_phyclk) {
			src = TX_RETIME_SRC_PHYCLK;
		} else {
			src = TX_RETIME_SRC_CLKGEN;
			freq = DWMAC_50MHZ;
		}
	} else if (IS_PHY_IF_MODE_RGMII(dwmac->interface)) {
		/* On GiGa clk source can be either ext or from clkgen */
		if (spd == SPEED_1000) {
			freq = DWMAC_125MHZ;
		} else {
			/* Switch to clkgen for these speeds */
			src = TX_RETIME_SRC_CLKGEN;
			if (spd == SPEED_100)
				freq = DWMAC_25MHZ;
			else if (spd == SPEED_10)
				freq = DWMAC_2_5MHZ;
		}
	}

	if (src == TX_RETIME_SRC_CLKGEN && dwmac->clk && freq)
		clk_set_rate(dwmac->clk, freq);

	regmap_update_bits(dwmac->regmap, reg, STIH4XX_RETIME_SRC_MASK,
			   stih4xx_tx_retime_val[src]);
}

static void stid127_fix_retime_src(void *priv, u32 spd)
{
	struct sti_dwmac *dwmac = priv;
	u32 reg = dwmac->ctrl_reg;
	u32 freq = 0;
	u32 val = 0;

	if (dwmac->interface == PHY_INTERFACE_MODE_MII) {
		val = STID127_ETH_SEL_INTERNAL_NOTEXT_TXCLK;
	} else if (dwmac->interface == PHY_INTERFACE_MODE_RMII) {
		if (!dwmac->ext_phyclk) {
			val = STID127_ETH_SEL_INTERNAL_NOTEXT_PHYCLK;
			freq = DWMAC_50MHZ;
		}
	} else if (IS_PHY_IF_MODE_RGMII(dwmac->interface)) {
		val = STID127_ETH_SEL_INTERNAL_NOTEXT_TXCLK;
		if (spd == SPEED_1000)
			freq = DWMAC_125MHZ;
		else if (spd == SPEED_100)
			freq = DWMAC_25MHZ;
		else if (spd == SPEED_10)
			freq = DWMAC_2_5MHZ;
	}

	if (dwmac->clk && freq)
		clk_set_rate(dwmac->clk, freq);

	regmap_update_bits(dwmac->regmap, reg, STID127_RETIME_SRC_MASK, val);
}

static int sti_dwmac_init(struct platform_device *pdev, void *priv)
{
	struct sti_dwmac *dwmac = priv;
	struct regmap *regmap = dwmac->regmap;
	int iface = dwmac->interface;
	struct device *dev = dwmac->dev;
	struct device_node *np = dev->of_node;
	u32 reg = dwmac->ctrl_reg;
	u32 val;

	if (dwmac->clk)
		clk_prepare_enable(dwmac->clk);

	if (of_property_read_bool(np, "st,gmac_en"))
		regmap_update_bits(regmap, reg, EN_MASK, EN);

	regmap_update_bits(regmap, reg, MII_PHY_SEL_MASK, phy_intf_sels[iface]);

	val = (iface == PHY_INTERFACE_MODE_REVMII) ? 0 : ENMII;
	regmap_update_bits(regmap, reg, ENMII_MASK, val);

	dwmac->fix_retime_src(priv, dwmac->speed);

	return 0;
}

static void sti_dwmac_exit(struct platform_device *pdev, void *priv)
{
	struct sti_dwmac *dwmac = priv;

	if (dwmac->clk)
		clk_disable_unprepare(dwmac->clk);
}
static int sti_dwmac_parse_data(struct sti_dwmac *dwmac,
				struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regmap *regmap;
	int err;

	if (!np)
		return -EINVAL;

	/* clk selection from extra syscfg register */
	dwmac->clk_sel_reg = -ENXIO;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sti-clkconf");
	if (res)
		dwmac->clk_sel_reg = res->start;

	regmap = syscon_regmap_lookup_by_phandle(np, "st,syscon");
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	err = of_property_read_u32_index(np, "st,syscon", 1, &dwmac->ctrl_reg);
	if (err) {
		dev_err(dev, "Can't get sysconfig ctrl offset (%d)\n", err);
		return err;
	}

	dwmac->dev = dev;
	dwmac->interface = of_get_phy_mode(np);
	dwmac->regmap = regmap;
	dwmac->ext_phyclk = of_property_read_bool(np, "st,ext-phyclk");
	dwmac->tx_retime_src = TX_RETIME_SRC_NA;
	dwmac->speed = SPEED_100;

	if (IS_PHY_IF_MODE_GBIT(dwmac->interface)) {
		const char *rs;

		dwmac->tx_retime_src = TX_RETIME_SRC_CLKGEN;

		err = of_property_read_string(np, "st,tx-retime-src", &rs);
		if (err < 0) {
			dev_warn(dev, "Use internal clock source\n");
		} else {
			if (!strcasecmp(rs, "clk_125"))
				dwmac->tx_retime_src = TX_RETIME_SRC_CLK_125;
			else if (!strcasecmp(rs, "txclk"))
				dwmac->tx_retime_src = TX_RETIME_SRC_TXCLK;
		}
		dwmac->speed = SPEED_1000;
	}

	dwmac->clk = devm_clk_get(dev, "sti-ethclk");
	if (IS_ERR(dwmac->clk)) {
		dev_warn(dev, "No phy clock provided...\n");
		dwmac->clk = NULL;
	}

	return 0;
}

static int sti_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	const struct sti_dwmac_of_data *data;
	struct stmmac_resources stmmac_res;
	struct sti_dwmac *dwmac;
	int ret;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "No OF match data provided\n");
		return -EINVAL;
	}

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = stmmac_probe_config_dt(pdev, &stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	ret = sti_dwmac_parse_data(dwmac, pdev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to parse OF data\n");
		return ret;
	}

	dwmac->fix_retime_src = data->fix_retime_src;

	plat_dat->bsp_priv = dwmac;
	plat_dat->init = sti_dwmac_init;
	plat_dat->exit = sti_dwmac_exit;
	plat_dat->fix_mac_speed = data->fix_retime_src;

	ret = sti_dwmac_init(pdev, plat_dat->bsp_priv);
	if (ret)
		return ret;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_dwmac_exit;

	return 0;

err_dwmac_exit:
	sti_dwmac_exit(pdev, plat_dat->bsp_priv);

	return ret;
}

static const struct sti_dwmac_of_data stih4xx_dwmac_data = {
	.fix_retime_src = stih4xx_fix_retime_src,
};

static const struct sti_dwmac_of_data stid127_dwmac_data = {
	.fix_retime_src = stid127_fix_retime_src,
};

static const struct of_device_id sti_dwmac_match[] = {
	{ .compatible = "st,stih415-dwmac", .data = &stih4xx_dwmac_data},
	{ .compatible = "st,stih416-dwmac", .data = &stih4xx_dwmac_data},
	{ .compatible = "st,stid127-dwmac", .data = &stid127_dwmac_data},
	{ .compatible = "st,stih407-dwmac", .data = &stih4xx_dwmac_data},
	{ }
};
MODULE_DEVICE_TABLE(of, sti_dwmac_match);

static struct platform_driver sti_dwmac_driver = {
	.probe  = sti_dwmac_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name           = "sti-dwmac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = sti_dwmac_match,
	},
};
module_platform_driver(sti_dwmac_driver);

MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@st.com>");
MODULE_DESCRIPTION("STMicroelectronics DWMAC Specific Glue layer");
MODULE_LICENSE("GPL");
