// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024 Schneider-Electric
 *
 * Clément Léger <clement.leger@bootlin.com>
 */

#include <linux/of.h>
#include <linux/pcs-rzn1-miic.h>
#include <linux/phylink.h>
#include <linux/platform_device.h>

#include "stmmac_platform.h"
#include "stmmac.h"

static int rzn1_dwmac_pcs_init(struct stmmac_priv *priv)
{
	struct device_node *np = priv->device->of_node;
	struct device_node *pcs_node;
	struct phylink_pcs *pcs;

	pcs_node = of_parse_phandle(np, "pcs-handle", 0);

	if (pcs_node) {
		pcs = miic_create(priv->device, pcs_node);
		of_node_put(pcs_node);
		if (IS_ERR(pcs))
			return PTR_ERR(pcs);

		priv->hw->phylink_pcs = pcs;
	}

	return 0;
}

static void rzn1_dwmac_pcs_exit(struct stmmac_priv *priv)
{
	if (priv->hw->phylink_pcs)
		miic_destroy(priv->hw->phylink_pcs);
}

static int rzn1_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct device *dev = &pdev->dev;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	plat_dat->bsp_priv = plat_dat;
	plat_dat->pcs_init = rzn1_dwmac_pcs_init;
	plat_dat->pcs_exit = rzn1_dwmac_pcs_exit;

	ret = stmmac_dvr_probe(dev, plat_dat, &stmmac_res);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id rzn1_dwmac_match[] = {
	{ .compatible = "renesas,rzn1-gmac" },
	{ }
};
MODULE_DEVICE_TABLE(of, rzn1_dwmac_match);

static struct platform_driver rzn1_dwmac_driver = {
	.probe  = rzn1_dwmac_probe,
	.remove_new = stmmac_pltfr_remove,
	.driver = {
		.name           = "rzn1-dwmac",
		.of_match_table = rzn1_dwmac_match,
	},
};
module_platform_driver(rzn1_dwmac_driver);

MODULE_AUTHOR("Clément Léger <clement.leger@bootlin.com>");
MODULE_DESCRIPTION("Renesas RZN1 DWMAC specific glue layer");
MODULE_LICENSE("GPL");
