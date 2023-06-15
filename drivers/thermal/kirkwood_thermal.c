// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kirkwood thermal sensor driver
 *
 * Copyright (C) 2012 Nobuhiro Iwamatsu <iwamatsu@nigauri.org>
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#define KIRKWOOD_THERMAL_VALID_OFFSET	9
#define KIRKWOOD_THERMAL_VALID_MASK	0x1
#define KIRKWOOD_THERMAL_TEMP_OFFSET	10
#define KIRKWOOD_THERMAL_TEMP_MASK	0x1FF

/* Kirkwood Thermal Sensor Dev Structure */
struct kirkwood_thermal_priv {
	void __iomem *sensor;
};

static int kirkwood_get_temp(struct thermal_zone_device *thermal,
			  int *temp)
{
	unsigned long reg;
	struct kirkwood_thermal_priv *priv = thermal_zone_device_priv(thermal);

	reg = readl_relaxed(priv->sensor);

	/* Valid check */
	if (!((reg >> KIRKWOOD_THERMAL_VALID_OFFSET) &
	    KIRKWOOD_THERMAL_VALID_MASK))
		return -EIO;

	/*
	 * Calculate temperature. According to Marvell internal
	 * documentation the formula for this is:
	 * Celsius = (322-reg)/1.3625
	 */
	reg = (reg >> KIRKWOOD_THERMAL_TEMP_OFFSET) &
		KIRKWOOD_THERMAL_TEMP_MASK;
	*temp = ((3220000000UL - (10000000UL * reg)) / 13625);

	return 0;
}

static struct thermal_zone_device_ops ops = {
	.get_temp = kirkwood_get_temp,
};

static const struct of_device_id kirkwood_thermal_id_table[] = {
	{ .compatible = "marvell,kirkwood-thermal" },
	{}
};

static int kirkwood_thermal_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *thermal = NULL;
	struct kirkwood_thermal_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->sensor = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(priv->sensor))
		return PTR_ERR(priv->sensor);

	thermal = thermal_zone_device_register("kirkwood_thermal", 0, 0,
					       priv, &ops, NULL, 0, 0);
	if (IS_ERR(thermal)) {
		dev_err(&pdev->dev,
			"Failed to register thermal zone device\n");
		return PTR_ERR(thermal);
	}
	ret = thermal_zone_device_enable(thermal);
	if (ret) {
		thermal_zone_device_unregister(thermal);
		dev_err(&pdev->dev, "Failed to enable thermal zone device\n");
		return ret;
	}

	platform_set_drvdata(pdev, thermal);

	return 0;
}

static int kirkwood_thermal_exit(struct platform_device *pdev)
{
	struct thermal_zone_device *kirkwood_thermal =
		platform_get_drvdata(pdev);

	thermal_zone_device_unregister(kirkwood_thermal);

	return 0;
}

MODULE_DEVICE_TABLE(of, kirkwood_thermal_id_table);

static struct platform_driver kirkwood_thermal_driver = {
	.probe = kirkwood_thermal_probe,
	.remove = kirkwood_thermal_exit,
	.driver = {
		.name = "kirkwood_thermal",
		.of_match_table = kirkwood_thermal_id_table,
	},
};

module_platform_driver(kirkwood_thermal_driver);

MODULE_AUTHOR("Nobuhiro Iwamatsu <iwamatsu@nigauri.org>");
MODULE_DESCRIPTION("kirkwood thermal driver");
MODULE_LICENSE("GPL");
