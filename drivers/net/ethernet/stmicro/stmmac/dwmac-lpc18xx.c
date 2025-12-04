/*
 * DWMAC glue for NXP LPC18xx/LPC43xx Ethernet
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/stmmac.h>

#include "stmmac_platform.h"

/* Register defines for CREG syscon */
#define LPC18XX_CREG_CREG6			0x12c
# define LPC18XX_CREG_CREG6_ETHMODE_MASK	GENMASK(2, 0)

static int lpc18xx_set_phy_intf_sel(void *bsp_priv, u8 phy_intf_sel)
{
	struct regmap *reg = bsp_priv;

	if (phy_intf_sel != PHY_INTF_SEL_GMII_MII &&
	    phy_intf_sel != PHY_INTF_SEL_RMII)
		return -EINVAL;

	regmap_update_bits(reg, LPC18XX_CREG_CREG6,
			   LPC18XX_CREG_CREG6_ETHMODE_MASK,
			   FIELD_PREP(LPC18XX_CREG_CREG6_ETHMODE_MASK,
				      phy_intf_sel));

	return 0;
}

static int lpc18xx_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct regmap *regmap;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	plat_dat->core_type = DWMAC_CORE_GMAC;

	regmap = syscon_regmap_lookup_by_compatible("nxp,lpc1850-creg");
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "syscon lookup failed\n");
		return PTR_ERR(regmap);
	}

	plat_dat->bsp_priv = regmap;
	plat_dat->set_phy_intf_sel = lpc18xx_set_phy_intf_sel;

	return stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
}

static const struct of_device_id lpc18xx_dwmac_match[] = {
	{ .compatible = "nxp,lpc1850-dwmac" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpc18xx_dwmac_match);

static struct platform_driver lpc18xx_dwmac_driver = {
	.probe  = lpc18xx_dwmac_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name           = "lpc18xx-dwmac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = lpc18xx_dwmac_match,
	},
};
module_platform_driver(lpc18xx_dwmac_driver);

MODULE_AUTHOR("Joachim Eastwood <manabian@gmail.com>");
MODULE_DESCRIPTION("DWMAC glue for LPC18xx/43xx Ethernet");
MODULE_LICENSE("GPL v2");
