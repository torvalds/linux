// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/phy/phy.h>
#include <linux/regmap.h>

#include "sun8i_a83t_dphy.h"
#include "sun8i_a83t_mipi_csi2.h"

static int sun8i_a83t_dphy_configure(struct phy *dphy,
				     union phy_configure_opts *opts)
{
	return phy_mipi_dphy_config_validate(&opts->mipi_dphy);
}

static int sun8i_a83t_dphy_power_on(struct phy *dphy)
{
	struct sun8i_a83t_mipi_csi2_device *csi2_dev = phy_get_drvdata(dphy);
	struct regmap *regmap = csi2_dev->regmap;

	regmap_write(regmap, SUN8I_A83T_DPHY_CTRL_REG,
		     SUN8I_A83T_DPHY_CTRL_RESET_N |
		     SUN8I_A83T_DPHY_CTRL_SHUTDOWN_N);

	regmap_write(regmap, SUN8I_A83T_DPHY_ANA0_REG,
		     SUN8I_A83T_DPHY_ANA0_REXT_EN |
		     SUN8I_A83T_DPHY_ANA0_RINT(2) |
		     SUN8I_A83T_DPHY_ANA0_SNK(2));

	return 0;
};

static int sun8i_a83t_dphy_power_off(struct phy *dphy)
{
	struct sun8i_a83t_mipi_csi2_device *csi2_dev = phy_get_drvdata(dphy);
	struct regmap *regmap = csi2_dev->regmap;

	regmap_write(regmap, SUN8I_A83T_DPHY_CTRL_REG, 0);

	return 0;
};

static const struct phy_ops sun8i_a83t_dphy_ops = {
	.configure	= sun8i_a83t_dphy_configure,
	.power_on	= sun8i_a83t_dphy_power_on,
	.power_off	= sun8i_a83t_dphy_power_off,
};

int sun8i_a83t_dphy_register(struct sun8i_a83t_mipi_csi2_device *csi2_dev)
{
	struct device *dev = csi2_dev->dev;
	struct phy_provider *phy_provider;

	csi2_dev->dphy = devm_phy_create(dev, NULL, &sun8i_a83t_dphy_ops);
	if (IS_ERR(csi2_dev->dphy)) {
		dev_err(dev, "failed to create D-PHY\n");
		return PTR_ERR(csi2_dev->dphy);
	}

	phy_set_drvdata(csi2_dev->dphy, csi2_dev);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register D-PHY provider\n");
		return PTR_ERR(phy_provider);
	}

	return 0;
}
