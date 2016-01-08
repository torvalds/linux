/**
 * emac-rockchip.c - Rockchip EMAC specific glue layer
 *
 * Copyright (C) 2014 Romain Perier <romain.perier@gmail.com>
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

#include <linux/etherdevice.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include "emac.h"

#define DRV_NAME        "rockchip_emac"
#define DRV_VERSION     "1.0"

#define GRF_MODE_MII		(1UL << 0)
#define GRF_MODE_RMII		(0UL << 0)
#define GRF_SPEED_10M		(0UL << 1)
#define GRF_SPEED_100M		(1UL << 1)
#define GRF_SPEED_ENABLE_BIT	(1UL << 17)
#define GRF_MODE_ENABLE_BIT	(1UL << 16)

struct emac_rockchip_soc_data {
	int grf_offset;
};

struct rockchip_priv_data {
	struct arc_emac_priv emac;
	struct regmap *grf;
	const struct emac_rockchip_soc_data *soc_data;
	struct regulator *regulator;
	struct clk *refclk;
};

static void emac_rockchip_set_mac_speed(void *priv, unsigned int speed)
{
	struct rockchip_priv_data *emac = priv;
	u32 data;
	int err = 0;

	/* write-enable bits */
	data = GRF_SPEED_ENABLE_BIT;

	switch(speed) {
	case 10:
		data |= GRF_SPEED_10M;
		break;
	case 100:
		data |= GRF_SPEED_100M;
		break;
	default:
		pr_err("speed %u not supported\n", speed);
		return;
	}

	err = regmap_write(emac->grf, emac->soc_data->grf_offset, data);
	if (err)
		pr_err("unable to apply speed %u to grf (%d)\n", speed, err);
}

static const struct emac_rockchip_soc_data emac_rockchip_dt_data[] = {
	{ .grf_offset = 0x154 }, /* rk3066 */
	{ .grf_offset = 0x0a4 }, /* rk3188 */
};

static const struct of_device_id emac_rockchip_dt_ids[] = {
	{ .compatible = "rockchip,rk3066-emac", .data = &emac_rockchip_dt_data[0] },
	{ .compatible = "rockchip,rk3188-emac", .data = &emac_rockchip_dt_data[1] },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, emac_rockchip_dt_ids);

static int emac_rockchip_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct net_device *ndev;
	struct rockchip_priv_data *priv;
	const struct of_device_id *match;
	u32 data;
	int err, interface;

	if (!pdev->dev.of_node)
		return -ENODEV;

	ndev = alloc_etherdev(sizeof(struct rockchip_priv_data));
	if (!ndev)
		return -ENOMEM;
	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, dev);

	priv = netdev_priv(ndev);
	priv->emac.drv_name = DRV_NAME;
	priv->emac.drv_version = DRV_VERSION;
	priv->emac.set_mac_speed = emac_rockchip_set_mac_speed;

	interface = of_get_phy_mode(dev->of_node);

	/* RK3066 and RK3188 SoCs only support RMII */
	if (interface != PHY_INTERFACE_MODE_RMII) {
		dev_err(dev, "unsupported phy interface mode %d\n", interface);
		err = -ENOTSUPP;
		goto out_netdev;
	}

	priv->grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
	if (IS_ERR(priv->grf)) {
		dev_err(dev, "failed to retrieve global register file (%ld)\n", PTR_ERR(priv->grf));
		err = PTR_ERR(priv->grf);
		goto out_netdev;
	}

	match = of_match_node(emac_rockchip_dt_ids, dev->of_node);
	priv->soc_data = match->data;

	priv->emac.clk = devm_clk_get(dev, "hclk");
	if (IS_ERR(priv->emac.clk)) {
		dev_err(dev, "failed to retrieve host clock (%ld)\n", PTR_ERR(priv->emac.clk));
		err = PTR_ERR(priv->emac.clk);
		goto out_netdev;
	}

	priv->refclk = devm_clk_get(dev, "macref");
	if (IS_ERR(priv->refclk)) {
		dev_err(dev, "failed to retrieve reference clock (%ld)\n", PTR_ERR(priv->refclk));
		err = PTR_ERR(priv->refclk);
		goto out_netdev;
	}

	err = clk_prepare_enable(priv->refclk);
	if (err) {
		dev_err(dev, "failed to enable reference clock (%d)\n", err);
		goto out_netdev;
	}

	/* Optional regulator for PHY */
	priv->regulator = devm_regulator_get_optional(dev, "phy");
	if (IS_ERR(priv->regulator)) {
		if (PTR_ERR(priv->regulator) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_err(dev, "no regulator found\n");
		priv->regulator = NULL;
	}

	if (priv->regulator) {
		err = regulator_enable(priv->regulator);
		if (err) {
			dev_err(dev, "failed to enable phy-supply (%d)\n", err);
			goto out_clk_disable;
		}
	}

	/* write-enable bits */
	data = GRF_MODE_ENABLE_BIT | GRF_SPEED_ENABLE_BIT;

	data |= GRF_SPEED_100M;
	data |= GRF_MODE_RMII;

	err = regmap_write(priv->grf, priv->soc_data->grf_offset, data);
	if (err) {
		dev_err(dev, "unable to apply initial settings to grf (%d)\n", err);
		goto out_regulator_disable;
	}

	/* RMII interface needs always a rate of 50MHz */
	err = clk_set_rate(priv->refclk, 50000000);
	if (err)
		dev_err(dev, "failed to change reference clock rate (%d)\n", err);

	err = arc_emac_probe(ndev, interface);
	if (err) {
		dev_err(dev, "failed to probe arc emac (%d)\n", err);
		goto out_regulator_disable;
	}

	return 0;

out_regulator_disable:
	if (priv->regulator)
		regulator_disable(priv->regulator);
out_clk_disable:
	clk_disable_unprepare(priv->refclk);
out_netdev:
	free_netdev(ndev);
	return err;
}

static int emac_rockchip_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct rockchip_priv_data *priv = netdev_priv(ndev);
	int err;

	err = arc_emac_remove(ndev);

	clk_disable_unprepare(priv->refclk);

	if (priv->regulator)
		regulator_disable(priv->regulator);

	free_netdev(ndev);
	return err;
}

static struct platform_driver emac_rockchip_driver = {
	.probe = emac_rockchip_probe,
	.remove = emac_rockchip_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table  = emac_rockchip_dt_ids,
	},
};

module_platform_driver(emac_rockchip_driver);

MODULE_AUTHOR("Romain Perier <romain.perier@gmail.com>");
MODULE_DESCRIPTION("Rockchip EMAC platform driver");
MODULE_LICENSE("GPL");
