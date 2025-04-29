// SPDX-License-Identifier: GPL-2.0-only
/*
 * Amlogic Meson6 and Meson8 DWMAC glue layer
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 */

#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/stmmac.h>

#include "stmmac_platform.h"

#define ETHMAC_SPEED_100	BIT(1)

struct meson_dwmac {
	struct device	*dev;
	void __iomem	*reg;
};

static int meson6_dwmac_set_clk_tx_rate(void *bsp_priv, struct clk *clk_tx_i,
					phy_interface_t interface, int speed)
{
	struct meson_dwmac *dwmac = bsp_priv;
	unsigned int val;

	val = readl(dwmac->reg);

	switch (speed) {
	case SPEED_10:
		val &= ~ETHMAC_SPEED_100;
		break;
	case SPEED_100:
		val |= ETHMAC_SPEED_100;
		break;
	}

	writel(val, dwmac->reg);

	return 0;
}

static int meson6_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct meson_dwmac *dwmac;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	dwmac->reg = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(dwmac->reg))
		return PTR_ERR(dwmac->reg);

	plat_dat->bsp_priv = dwmac;
	plat_dat->set_clk_tx_rate = meson6_dwmac_set_clk_tx_rate;

	return stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
}

static const struct of_device_id meson6_dwmac_match[] = {
	{ .compatible = "amlogic,meson6-dwmac" },
	{ }
};
MODULE_DEVICE_TABLE(of, meson6_dwmac_match);

static struct platform_driver meson6_dwmac_driver = {
	.probe  = meson6_dwmac_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name           = "meson6-dwmac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = meson6_dwmac_match,
	},
};
module_platform_driver(meson6_dwmac_driver);

MODULE_AUTHOR("Beniamino Galvani <b.galvani@gmail.com>");
MODULE_DESCRIPTION("Amlogic Meson6 and Meson8 DWMAC glue layer");
MODULE_LICENSE("GPL v2");
