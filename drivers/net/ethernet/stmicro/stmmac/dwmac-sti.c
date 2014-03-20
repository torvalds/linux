/**
 * dwmac-sti.c - STMicroelectronics DWMAC Specific Glue layer
 *
 * Copyright (C) 2003-2014 STMicroelectronics (R&D) Limited
 * Author: Srinivas Kandagatla <srinivas.kandagatla@st.com>
 *
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
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_net.h>

/**
 *			STi GMAC glue logic.
 *			--------------------
 *
 *		 _
 *		|  \
 *	--------|0  \ ETH_SEL_INTERNAL_NOTEXT_PHYCLK
 * phyclk	|    |___________________________________________
 *		|    |	|			(phyclk-in)
 *	--------|1  /	|
 * int-clk	|_ /	|
 *			|	 _
 *			|	|  \
 *			|_______|1  \ ETH_SEL_TX_RETIME_CLK
 *				|    |___________________________
 *				|    |		(tx-retime-clk)
 *			 _______|0  /
 *			|	|_ /
 *		 _	|
 *		|  \	|
 *	--------|0  \	|
 * clk_125	|    |__|
 *		|    |	ETH_SEL_TXCLK_NOT_CLK125
 *	--------|1  /
 * txclk	|_ /
 *
 *
 * ETH_SEL_INTERNAL_NOTEXT_PHYCLK is valid only for RMII where PHY can
 * generate 50MHz clock or MAC can generate it.
 * This bit is configured by "st,ext-phyclk" property.
 *
 * ETH_SEL_TXCLK_NOT_CLK125 is only valid for gigabit modes, where the 125Mhz
 * clock either comes from clk-125 pin or txclk pin. This configuration is
 * totally driven by the board wiring. This bit is configured by
 * "st,tx-retime-src" property.
 *
 * TXCLK configuration is different for different phy interface modes
 * and changes according to link speed in modes like RGMII.
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
 * ------------------------------------------------
 *|	RMII	|	n/a	 |	25Mhz	   |
 *|		|		 |clkgen/phyclk-in |
 * ------------------------------------------------
 *
 * TX lines are always retimed with a clk, which can vary depending
 * on the board configuration. Below is the table of these bits
 * in eth configuration register depending on source of retime clk.
 *
 *---------------------------------------------------------------
 * src	 | tx_rt_clk	| int_not_ext_phyclk	| txclk_n_clk125|
 *---------------------------------------------------------------
 * txclk |	0	|	n/a		|	1	|
 *---------------------------------------------------------------
 * ck_125|	0	|	n/a		|	0	|
 *---------------------------------------------------------------
 * phyclk|	1	|	0		|	n/a	|
 *---------------------------------------------------------------
 * clkgen|	1	|	1		|	n/a	|
 *---------------------------------------------------------------
 */

 /* Register definition */

 /* 3 bits [8:6]
  *  [6:6]      ETH_SEL_TXCLK_NOT_CLK125
  *  [7:7]      ETH_SEL_INTERNAL_NOTEXT_PHYCLK
  *  [8:8]      ETH_SEL_TX_RETIME_CLK
  *
  */

#define TX_RETIME_SRC_MASK		GENMASK(8, 6)
#define ETH_SEL_TX_RETIME_CLK		BIT(8)
#define ETH_SEL_INTERNAL_NOTEXT_PHYCLK	BIT(7)
#define ETH_SEL_TXCLK_NOT_CLK125	BIT(6)

#define ENMII_MASK			GENMASK(5, 5)
#define ENMII				BIT(5)

/**
 * 3 bits [4:2]
 *	000-GMII/MII
 *	001-RGMII
 *	010-SGMII
 *	100-RMII
*/
#define MII_PHY_SEL_MASK		GENMASK(4, 2)
#define ETH_PHY_SEL_RMII		BIT(4)
#define ETH_PHY_SEL_SGMII		BIT(3)
#define ETH_PHY_SEL_RGMII		BIT(2)
#define ETH_PHY_SEL_GMII		0x0
#define ETH_PHY_SEL_MII			0x0

#define IS_PHY_IF_MODE_RGMII(iface)	(iface == PHY_INTERFACE_MODE_RGMII || \
			iface == PHY_INTERFACE_MODE_RGMII_ID || \
			iface == PHY_INTERFACE_MODE_RGMII_RXID || \
			iface == PHY_INTERFACE_MODE_RGMII_TXID)

#define IS_PHY_IF_MODE_GBIT(iface)	(IS_PHY_IF_MODE_RGMII(iface) || \
			iface == PHY_INTERFACE_MODE_GMII)

struct sti_dwmac {
	int interface;
	bool ext_phyclk;
	bool is_tx_retime_src_clk_125;
	struct clk *clk;
	int reg;
	struct device *dev;
	struct regmap *regmap;
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

static const char *const tx_retime_srcs[] = {
	[TX_RETIME_SRC_NA] = "",
	[TX_RETIME_SRC_TXCLK] = "txclk",
	[TX_RETIME_SRC_CLK_125] = "clk_125",
	[TX_RETIME_SRC_PHYCLK] = "phyclk",
	[TX_RETIME_SRC_CLKGEN] = "clkgen",
};

static u32 tx_retime_val[] = {
	[TX_RETIME_SRC_TXCLK] = ETH_SEL_TXCLK_NOT_CLK125,
	[TX_RETIME_SRC_CLK_125] = 0x0,
	[TX_RETIME_SRC_PHYCLK] = ETH_SEL_TX_RETIME_CLK,
	[TX_RETIME_SRC_CLKGEN] = ETH_SEL_TX_RETIME_CLK |
	    ETH_SEL_INTERNAL_NOTEXT_PHYCLK,
};

static void setup_retime_src(struct sti_dwmac *dwmac, u32 spd)
{
	u32 src = 0, freq = 0;

	if (spd == SPEED_100) {
		if (dwmac->interface == PHY_INTERFACE_MODE_MII ||
		    dwmac->interface == PHY_INTERFACE_MODE_GMII) {
			src = TX_RETIME_SRC_TXCLK;
		} else if (dwmac->interface == PHY_INTERFACE_MODE_RMII) {
			if (dwmac->ext_phyclk) {
				src = TX_RETIME_SRC_PHYCLK;
			} else {
				src = TX_RETIME_SRC_CLKGEN;
				freq = 50000000;
			}

		} else if (IS_PHY_IF_MODE_RGMII(dwmac->interface)) {
			src = TX_RETIME_SRC_CLKGEN;
			freq = 25000000;
		}

		if (src == TX_RETIME_SRC_CLKGEN && dwmac->clk)
			clk_set_rate(dwmac->clk, freq);

	} else if (spd == SPEED_1000) {
		if (dwmac->is_tx_retime_src_clk_125)
			src = TX_RETIME_SRC_CLK_125;
		else
			src = TX_RETIME_SRC_TXCLK;
	}

	regmap_update_bits(dwmac->regmap, dwmac->reg,
			   TX_RETIME_SRC_MASK, tx_retime_val[src]);
}

static void sti_dwmac_exit(struct platform_device *pdev, void *priv)
{
	struct sti_dwmac *dwmac = priv;

	if (dwmac->clk)
		clk_disable_unprepare(dwmac->clk);
}

static void sti_fix_mac_speed(void *priv, unsigned int spd)
{
	struct sti_dwmac *dwmac = priv;

	setup_retime_src(dwmac, spd);

	return;
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

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sti-ethconf");
	if (!res)
		return -ENODATA;

	regmap = syscon_regmap_lookup_by_phandle(np, "st,syscon");
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	dwmac->dev = dev;
	dwmac->interface = of_get_phy_mode(np);
	dwmac->regmap = regmap;
	dwmac->reg = res->start;
	dwmac->ext_phyclk = of_property_read_bool(np, "st,ext-phyclk");
	dwmac->is_tx_retime_src_clk_125 = false;

	if (IS_PHY_IF_MODE_GBIT(dwmac->interface)) {
		const char *rs;

		err = of_property_read_string(np, "st,tx-retime-src", &rs);
		if (err < 0) {
			dev_err(dev, "st,tx-retime-src not specified\n");
			return err;
		}

		if (!strcasecmp(rs, "clk_125"))
			dwmac->is_tx_retime_src_clk_125 = true;
	}

	dwmac->clk = devm_clk_get(dev, "sti-ethclk");

	if (IS_ERR(dwmac->clk))
		dwmac->clk = NULL;

	return 0;
}

static int sti_dwmac_init(struct platform_device *pdev, void *priv)
{
	struct sti_dwmac *dwmac = priv;
	struct regmap *regmap = dwmac->regmap;
	int iface = dwmac->interface;
	u32 reg = dwmac->reg;
	u32 val, spd;

	if (dwmac->clk)
		clk_prepare_enable(dwmac->clk);

	regmap_update_bits(regmap, reg, MII_PHY_SEL_MASK, phy_intf_sels[iface]);

	val = (iface == PHY_INTERFACE_MODE_REVMII) ? 0 : ENMII;
	regmap_update_bits(regmap, reg, ENMII_MASK, val);

	if (IS_PHY_IF_MODE_GBIT(iface))
		spd = SPEED_1000;
	else
		spd = SPEED_100;

	setup_retime_src(dwmac, spd);

	return 0;
}

static void *sti_dwmac_setup(struct platform_device *pdev)
{
	struct sti_dwmac *dwmac;
	int ret;

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return ERR_PTR(-ENOMEM);

	ret = sti_dwmac_parse_data(dwmac, pdev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to parse OF data\n");
		return ERR_PTR(ret);
	}

	return dwmac;
}

const struct stmmac_of_data sti_gmac_data = {
	.fix_mac_speed = sti_fix_mac_speed,
	.setup = sti_dwmac_setup,
	.init = sti_dwmac_init,
	.exit = sti_dwmac_exit,
};
