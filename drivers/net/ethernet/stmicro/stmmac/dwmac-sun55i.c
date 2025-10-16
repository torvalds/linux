// SPDX-License-Identifier: GPL-2.0-only
/*
 * dwmac-sun55i.c - Allwinner sun55i GMAC200 specific glue layer
 *
 * Copyright (C) 2025 Chen-Yu Tsai <wens@csie.org>
 *
 * syscon parts taken from dwmac-sun8i.c, which is
 *
 * Copyright (C) 2017 Corentin Labbe <clabbe.montjoie@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/stmmac.h>

#include "stmmac.h"
#include "stmmac_platform.h"

#define SYSCON_REG		0x34

/* RMII specific bits */
#define SYSCON_RMII_EN		BIT(13) /* 1: enable RMII (overrides EPIT) */
/* Generic system control EMAC_CLK bits */
#define SYSCON_ETXDC_MASK		GENMASK(12, 10)
#define SYSCON_ERXDC_MASK		GENMASK(9, 5)
/* EMAC PHY Interface Type */
#define SYSCON_EPIT			BIT(2) /* 1: RGMII, 0: MII */
#define SYSCON_ETCS_MASK		GENMASK(1, 0)
#define SYSCON_ETCS_MII		0x0
#define SYSCON_ETCS_EXT_GMII	0x1
#define SYSCON_ETCS_INT_GMII	0x2

static int sun55i_gmac200_set_syscon(struct device *dev,
				     struct plat_stmmacenet_data *plat)
{
	struct device_node *node = dev->of_node;
	struct regmap *regmap;
	u32 val, reg = 0;
	int ret;

	regmap = syscon_regmap_lookup_by_phandle(node, "syscon");
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Unable to map syscon\n");

	if (!of_property_read_u32(node, "tx-internal-delay-ps", &val)) {
		if (val % 100)
			return dev_err_probe(dev, -EINVAL,
					     "tx-delay must be a multiple of 100ps\n");
		val /= 100;
		dev_dbg(dev, "set tx-delay to %x\n", val);
		if (!FIELD_FIT(SYSCON_ETXDC_MASK, val))
			return dev_err_probe(dev, -EINVAL,
					     "TX clock delay exceeds maximum (%u00ps > %lu00ps)\n",
					     val, FIELD_MAX(SYSCON_ETXDC_MASK));

		reg |= FIELD_PREP(SYSCON_ETXDC_MASK, val);
	}

	if (!of_property_read_u32(node, "rx-internal-delay-ps", &val)) {
		if (val % 100)
			return dev_err_probe(dev, -EINVAL,
					     "rx-delay must be a multiple of 100ps\n");
		val /= 100;
		dev_dbg(dev, "set rx-delay to %x\n", val);
		if (!FIELD_FIT(SYSCON_ERXDC_MASK, val))
			return dev_err_probe(dev, -EINVAL,
					     "RX clock delay exceeds maximum (%u00ps > %lu00ps)\n",
					     val, FIELD_MAX(SYSCON_ERXDC_MASK));

		reg |= FIELD_PREP(SYSCON_ERXDC_MASK, val);
	}

	switch (plat->phy_interface) {
	case PHY_INTERFACE_MODE_MII:
		/* default */
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		reg |= SYSCON_EPIT | SYSCON_ETCS_INT_GMII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		reg |= SYSCON_RMII_EN;
		break;
	default:
		return dev_err_probe(dev, -EINVAL, "Unsupported interface mode: %s",
				     phy_modes(plat->phy_interface));
	}

	ret = regmap_write(regmap, SYSCON_REG, reg);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to write to syscon\n");

	return 0;
}

static int sun55i_gmac200_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct device *dev = &pdev->dev;
	struct clk *clk;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	/* BSP disables it */
	plat_dat->flags |= STMMAC_FLAG_SPH_DISABLE;
	plat_dat->host_dma_width = 32;

	ret = sun55i_gmac200_set_syscon(dev, plat_dat);
	if (ret)
		return ret;

	clk = devm_clk_get_enabled(dev, "mbus");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk),
				     "Failed to get or enable MBUS clock\n");

	ret = devm_regulator_get_enable_optional(dev, "phy");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get or enable PHY supply\n");

	return devm_stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
}

static const struct of_device_id sun55i_gmac200_match[] = {
	{ .compatible = "allwinner,sun55i-a523-gmac200" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun55i_gmac200_match);

static struct platform_driver sun55i_gmac200_driver = {
	.probe  = sun55i_gmac200_probe,
	.driver = {
		.name           = "dwmac-sun55i",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = sun55i_gmac200_match,
	},
};
module_platform_driver(sun55i_gmac200_driver);

MODULE_AUTHOR("Chen-Yu Tsai <wens@csie.org>");
MODULE_DESCRIPTION("Allwinner sun55i GMAC200 specific glue layer");
MODULE_LICENSE("GPL");
