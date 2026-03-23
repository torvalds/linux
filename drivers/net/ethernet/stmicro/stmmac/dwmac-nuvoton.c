// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nuvoton DWMAC specific glue layer
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <a0987203069@gmail.com>
 */

#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/stmmac.h>

#include "stmmac_platform.h"

#define NVT_REG_SYS_GMAC0MISCR  0x108
#define NVT_REG_SYS_GMAC1MISCR  0x10C

#define NVT_MISCR_RMII          BIT(0)

/* Two thousand picoseconds are evenly mapped to a 4-bit field,
 * resulting in each step being 2000/15 picoseconds.
 */
#define NVT_PATH_DELAY_STEP     134
#define NVT_TX_DELAY_MASK       GENMASK(19, 16)
#define NVT_RX_DELAY_MASK       GENMASK(23, 20)

struct nvt_priv_data {
	struct device *dev;
	struct regmap *regmap;
	u32 macid;
};

static int nvt_gmac_get_delay(struct device *dev, const char *property)
{
	u32 arg;

	if (of_property_read_u32(dev->of_node, property, &arg))
		return 0;

	if (arg > 2000)
		return -EINVAL;

	if (arg == 2000)
		return 15;

	return arg / NVT_PATH_DELAY_STEP;
}

static int nvt_set_phy_intf_sel(void *bsp_priv, u8 phy_intf_sel)
{
	struct nvt_priv_data *priv = bsp_priv;
	u32 reg, val;
	int ret;

	if (phy_intf_sel == PHY_INTF_SEL_RGMII) {
		ret = nvt_gmac_get_delay(priv->dev, "rx-internal-delay-ps");
		if (ret < 0)
			return ret;
		val = FIELD_PREP(NVT_RX_DELAY_MASK, ret);

		ret = nvt_gmac_get_delay(priv->dev, "tx-internal-delay-ps");
		if (ret < 0)
			return ret;
		val |= FIELD_PREP(NVT_TX_DELAY_MASK, ret);
	} else if (phy_intf_sel == PHY_INTF_SEL_RMII) {
		val = NVT_MISCR_RMII;
	} else {
		return -EINVAL;
	}

	reg = (priv->macid == 0) ? NVT_REG_SYS_GMAC0MISCR : NVT_REG_SYS_GMAC1MISCR;
	regmap_update_bits(priv->regmap, reg,
			   NVT_RX_DELAY_MASK | NVT_TX_DELAY_MASK | NVT_MISCR_RMII, val);

	return 0;
}

static int nvt_gmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct device *dev = &pdev->dev;
	struct nvt_priv_data *priv;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get platform resources\n");

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return dev_err_probe(dev, PTR_ERR(plat_dat), "Failed to get platform data\n");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return dev_err_probe(dev, -ENOMEM, "Failed to allocate private data\n");

	priv->regmap = syscon_regmap_lookup_by_phandle_args(dev->of_node, "nuvoton,sys",
							    1, &priv->macid);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(dev, PTR_ERR(priv->regmap), "Failed to get sys register\n");

	if (priv->macid > 1)
		return dev_err_probe(dev, -EINVAL, "Invalid sys arguments\n");

	plat_dat->bsp_priv = priv;
	plat_dat->set_phy_intf_sel = nvt_set_phy_intf_sel;

	return stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
}

static const struct of_device_id nvt_dwmac_match[] = {
	{ .compatible = "nuvoton,ma35d1-dwmac"},
	{ }
};
MODULE_DEVICE_TABLE(of, nvt_dwmac_match);

static struct platform_driver nvt_dwmac_driver = {
	.probe  = nvt_gmac_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name           = "nuvoton-dwmac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = nvt_dwmac_match,
	},
};
module_platform_driver(nvt_dwmac_driver);

MODULE_AUTHOR("Joey Lu <a0987203069@gmail.com>");
MODULE_DESCRIPTION("Nuvoton DWMAC specific glue layer");
MODULE_LICENSE("GPL");
