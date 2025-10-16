// SPDX-License-Identifier: GPL-2.0
/*
 * dwmac-ingenic.c - Ingenic SoCs DWMAC specific glue layer
 *
 * Copyright (c) 2021 周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/stmmac.h>

#include "stmmac_platform.h"

#define MACPHYC_TXCLK_SEL_MASK		GENMASK(31, 31)
#define MACPHYC_TXCLK_SEL_OUTPUT	0x1
#define MACPHYC_TXCLK_SEL_INPUT		0x0
#define MACPHYC_MODE_SEL_MASK		GENMASK(31, 31)
#define MACPHYC_MODE_SEL_RMII		0x0
#define MACPHYC_TX_SEL_MASK			GENMASK(19, 19)
#define MACPHYC_TX_SEL_ORIGIN		0x0
#define MACPHYC_TX_SEL_DELAY		0x1
#define MACPHYC_TX_DELAY_MASK		GENMASK(18, 12)
#define MACPHYC_RX_SEL_MASK			GENMASK(11, 11)
#define MACPHYC_RX_SEL_ORIGIN		0x0
#define MACPHYC_RX_SEL_DELAY		0x1
#define MACPHYC_RX_DELAY_MASK		GENMASK(10, 4)
#define MACPHYC_SOFT_RST_MASK		GENMASK(3, 3)
#define MACPHYC_PHY_INFT_MASK		GENMASK(2, 0)
#define MACPHYC_PHY_INFT_RMII		0x4
#define MACPHYC_PHY_INFT_RGMII		0x1
#define MACPHYC_PHY_INFT_GMII		0x0
#define MACPHYC_PHY_INFT_MII		0x0

#define MACPHYC_TX_DELAY_PS_MAX		2496
#define MACPHYC_TX_DELAY_PS_MIN		20

#define MACPHYC_RX_DELAY_PS_MAX		2496
#define MACPHYC_RX_DELAY_PS_MIN		20

enum ingenic_mac_version {
	ID_JZ4775,
	ID_X1000,
	ID_X1600,
	ID_X1830,
	ID_X2000,
};

struct ingenic_mac {
	const struct ingenic_soc_info *soc_info;
	struct plat_stmmacenet_data *plat_dat;
	struct device *dev;
	struct regmap *regmap;

	int rx_delay;
	int tx_delay;
};

struct ingenic_soc_info {
	enum ingenic_mac_version version;
	u32 mask;

	int (*set_mode)(struct plat_stmmacenet_data *plat_dat);
};

static int ingenic_mac_init(struct platform_device *pdev, void *bsp_priv)
{
	struct ingenic_mac *mac = bsp_priv;
	int ret;

	if (mac->soc_info->set_mode) {
		ret = mac->soc_info->set_mode(mac->plat_dat);
		if (ret)
			return ret;
	}

	return 0;
}

static int jz4775_mac_set_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct ingenic_mac *mac = plat_dat->bsp_priv;
	unsigned int val;

	switch (plat_dat->phy_interface) {
	case PHY_INTERFACE_MODE_MII:
		val = FIELD_PREP(MACPHYC_TXCLK_SEL_MASK, MACPHYC_TXCLK_SEL_INPUT) |
			  FIELD_PREP(MACPHYC_PHY_INFT_MASK, MACPHYC_PHY_INFT_MII);
		dev_dbg(mac->dev, "MAC PHY Control Register: PHY_INTERFACE_MODE_MII\n");
		break;

	case PHY_INTERFACE_MODE_GMII:
		val = FIELD_PREP(MACPHYC_TXCLK_SEL_MASK, MACPHYC_TXCLK_SEL_INPUT) |
			  FIELD_PREP(MACPHYC_PHY_INFT_MASK, MACPHYC_PHY_INFT_GMII);
		dev_dbg(mac->dev, "MAC PHY Control Register: PHY_INTERFACE_MODE_GMII\n");
		break;

	case PHY_INTERFACE_MODE_RMII:
		val = FIELD_PREP(MACPHYC_TXCLK_SEL_MASK, MACPHYC_TXCLK_SEL_INPUT) |
			  FIELD_PREP(MACPHYC_PHY_INFT_MASK, MACPHYC_PHY_INFT_RMII);
		dev_dbg(mac->dev, "MAC PHY Control Register: PHY_INTERFACE_MODE_RMII\n");
		break;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val = FIELD_PREP(MACPHYC_TXCLK_SEL_MASK, MACPHYC_TXCLK_SEL_INPUT) |
			  FIELD_PREP(MACPHYC_PHY_INFT_MASK, MACPHYC_PHY_INFT_RGMII);
		dev_dbg(mac->dev, "MAC PHY Control Register: PHY_INTERFACE_MODE_RGMII\n");
		break;

	default:
		dev_err(mac->dev, "Unsupported interface %s\n",
			phy_modes(plat_dat->phy_interface));
		return -EINVAL;
	}

	/* Update MAC PHY control register */
	return regmap_update_bits(mac->regmap, 0, mac->soc_info->mask, val);
}

static int x1000_mac_set_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct ingenic_mac *mac = plat_dat->bsp_priv;

	switch (plat_dat->phy_interface) {
	case PHY_INTERFACE_MODE_RMII:
		dev_dbg(mac->dev, "MAC PHY Control Register: PHY_INTERFACE_MODE_RMII\n");
		break;

	default:
		dev_err(mac->dev, "Unsupported interface %s\n",
			phy_modes(plat_dat->phy_interface));
		return -EINVAL;
	}

	/* Update MAC PHY control register */
	return regmap_update_bits(mac->regmap, 0, mac->soc_info->mask, 0);
}

static int x1600_mac_set_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct ingenic_mac *mac = plat_dat->bsp_priv;
	unsigned int val;

	switch (plat_dat->phy_interface) {
	case PHY_INTERFACE_MODE_RMII:
		val = FIELD_PREP(MACPHYC_PHY_INFT_MASK, MACPHYC_PHY_INFT_RMII);
		dev_dbg(mac->dev, "MAC PHY Control Register: PHY_INTERFACE_MODE_RMII\n");
		break;

	default:
		dev_err(mac->dev, "Unsupported interface %s\n",
			phy_modes(plat_dat->phy_interface));
		return -EINVAL;
	}

	/* Update MAC PHY control register */
	return regmap_update_bits(mac->regmap, 0, mac->soc_info->mask, val);
}

static int x1830_mac_set_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct ingenic_mac *mac = plat_dat->bsp_priv;
	unsigned int val;

	switch (plat_dat->phy_interface) {
	case PHY_INTERFACE_MODE_RMII:
		val = FIELD_PREP(MACPHYC_MODE_SEL_MASK, MACPHYC_MODE_SEL_RMII) |
			  FIELD_PREP(MACPHYC_PHY_INFT_MASK, MACPHYC_PHY_INFT_RMII);
		dev_dbg(mac->dev, "MAC PHY Control Register: PHY_INTERFACE_MODE_RMII\n");
		break;

	default:
		dev_err(mac->dev, "Unsupported interface %s\n",
			phy_modes(plat_dat->phy_interface));
		return -EINVAL;
	}

	/* Update MAC PHY control register */
	return regmap_update_bits(mac->regmap, 0, mac->soc_info->mask, val);
}

static int x2000_mac_set_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct ingenic_mac *mac = plat_dat->bsp_priv;
	unsigned int val;

	switch (plat_dat->phy_interface) {
	case PHY_INTERFACE_MODE_RMII:
		val = FIELD_PREP(MACPHYC_TX_SEL_MASK, MACPHYC_TX_SEL_ORIGIN) |
			  FIELD_PREP(MACPHYC_RX_SEL_MASK, MACPHYC_RX_SEL_ORIGIN) |
			  FIELD_PREP(MACPHYC_PHY_INFT_MASK, MACPHYC_PHY_INFT_RMII);
		dev_dbg(mac->dev, "MAC PHY Control Register: PHY_INTERFACE_MODE_RMII\n");
		break;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val = FIELD_PREP(MACPHYC_PHY_INFT_MASK, MACPHYC_PHY_INFT_RGMII);

		if (mac->tx_delay == 0)
			val |= FIELD_PREP(MACPHYC_TX_SEL_MASK, MACPHYC_TX_SEL_ORIGIN);
		else
			val |= FIELD_PREP(MACPHYC_TX_SEL_MASK, MACPHYC_TX_SEL_DELAY) |
				   FIELD_PREP(MACPHYC_TX_DELAY_MASK, (mac->tx_delay + 9750) / 19500 - 1);

		if (mac->rx_delay == 0)
			val |= FIELD_PREP(MACPHYC_RX_SEL_MASK, MACPHYC_RX_SEL_ORIGIN);
		else
			val |= FIELD_PREP(MACPHYC_RX_SEL_MASK, MACPHYC_RX_SEL_DELAY) |
				   FIELD_PREP(MACPHYC_RX_DELAY_MASK, (mac->rx_delay + 9750) / 19500 - 1);

		dev_dbg(mac->dev, "MAC PHY Control Register: PHY_INTERFACE_MODE_RGMII\n");
		break;

	default:
		dev_err(mac->dev, "Unsupported interface %s\n",
			phy_modes(plat_dat->phy_interface));
		return -EINVAL;
	}

	/* Update MAC PHY control register */
	return regmap_update_bits(mac->regmap, 0, mac->soc_info->mask, val);
}

static int ingenic_mac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct ingenic_mac *mac;
	const struct ingenic_soc_info *data;
	u32 tx_delay_ps, rx_delay_ps;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	mac = devm_kzalloc(&pdev->dev, sizeof(*mac), GFP_KERNEL);
	if (!mac)
		return -ENOMEM;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "No of match data provided\n");
		return -EINVAL;
	}

	/* Get MAC PHY control register */
	mac->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "mode-reg");
	if (IS_ERR(mac->regmap)) {
		dev_err(&pdev->dev, "%s: Failed to get syscon regmap\n", __func__);
		return PTR_ERR(mac->regmap);
	}

	if (!of_property_read_u32(pdev->dev.of_node, "tx-clk-delay-ps", &tx_delay_ps)) {
		if (tx_delay_ps >= MACPHYC_TX_DELAY_PS_MIN &&
			tx_delay_ps <= MACPHYC_TX_DELAY_PS_MAX) {
			mac->tx_delay = tx_delay_ps * 1000;
		} else {
			dev_err(&pdev->dev, "Invalid TX clock delay: %dps\n", tx_delay_ps);
			return -EINVAL;
		}
	}

	if (!of_property_read_u32(pdev->dev.of_node, "rx-clk-delay-ps", &rx_delay_ps)) {
		if (rx_delay_ps >= MACPHYC_RX_DELAY_PS_MIN &&
			rx_delay_ps <= MACPHYC_RX_DELAY_PS_MAX) {
			mac->rx_delay = rx_delay_ps * 1000;
		} else {
			dev_err(&pdev->dev, "Invalid RX clock delay: %dps\n", rx_delay_ps);
			return -EINVAL;
		}
	}

	mac->soc_info = data;
	mac->dev = &pdev->dev;
	mac->plat_dat = plat_dat;

	plat_dat->bsp_priv = mac;
	plat_dat->init = ingenic_mac_init;

	return devm_stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
}

static struct ingenic_soc_info jz4775_soc_info = {
	.version = ID_JZ4775,
	.mask = MACPHYC_TXCLK_SEL_MASK | MACPHYC_SOFT_RST_MASK | MACPHYC_PHY_INFT_MASK,

	.set_mode = jz4775_mac_set_mode,
};

static struct ingenic_soc_info x1000_soc_info = {
	.version = ID_X1000,
	.mask = MACPHYC_SOFT_RST_MASK,

	.set_mode = x1000_mac_set_mode,
};

static struct ingenic_soc_info x1600_soc_info = {
	.version = ID_X1600,
	.mask = MACPHYC_SOFT_RST_MASK | MACPHYC_PHY_INFT_MASK,

	.set_mode = x1600_mac_set_mode,
};

static struct ingenic_soc_info x1830_soc_info = {
	.version = ID_X1830,
	.mask = MACPHYC_MODE_SEL_MASK | MACPHYC_SOFT_RST_MASK | MACPHYC_PHY_INFT_MASK,

	.set_mode = x1830_mac_set_mode,
};

static struct ingenic_soc_info x2000_soc_info = {
	.version = ID_X2000,
	.mask = MACPHYC_TX_SEL_MASK | MACPHYC_TX_DELAY_MASK | MACPHYC_RX_SEL_MASK |
			MACPHYC_RX_DELAY_MASK | MACPHYC_SOFT_RST_MASK | MACPHYC_PHY_INFT_MASK,

	.set_mode = x2000_mac_set_mode,
};

static const struct of_device_id ingenic_mac_of_matches[] = {
	{ .compatible = "ingenic,jz4775-mac", .data = &jz4775_soc_info },
	{ .compatible = "ingenic,x1000-mac", .data = &x1000_soc_info },
	{ .compatible = "ingenic,x1600-mac", .data = &x1600_soc_info },
	{ .compatible = "ingenic,x1830-mac", .data = &x1830_soc_info },
	{ .compatible = "ingenic,x2000-mac", .data = &x2000_soc_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ingenic_mac_of_matches);

static struct platform_driver ingenic_mac_driver = {
	.probe		= ingenic_mac_probe,
	.driver		= {
		.name	= "ingenic-mac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = ingenic_mac_of_matches,
	},
};
module_platform_driver(ingenic_mac_driver);

MODULE_AUTHOR("周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>");
MODULE_DESCRIPTION("Ingenic SoCs DWMAC specific glue layer");
MODULE_LICENSE("GPL v2");
