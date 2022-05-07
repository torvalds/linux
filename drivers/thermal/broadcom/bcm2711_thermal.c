// SPDX-License-Identifier: GPL-2.0+
/*
 * Broadcom AVS RO thermal sensor driver
 *
 * based on brcmstb_thermal
 *
 * Copyright (C) 2020 Stefan Wahren
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>

#include "../thermal_hwmon.h"

#define AVS_RO_TEMP_STATUS		0x200
#define AVS_RO_TEMP_STATUS_VALID_MSK	(BIT(16) | BIT(10))
#define AVS_RO_TEMP_STATUS_DATA_MSK	GENMASK(9, 0)

struct bcm2711_thermal_priv {
	struct regmap *regmap;
	struct thermal_zone_device *thermal;
};

static int bcm2711_get_temp(void *data, int *temp)
{
	struct bcm2711_thermal_priv *priv = data;
	int slope = thermal_zone_get_slope(priv->thermal);
	int offset = thermal_zone_get_offset(priv->thermal);
	u32 val;
	int ret;
	long t;

	ret = regmap_read(priv->regmap, AVS_RO_TEMP_STATUS, &val);
	if (ret)
		return ret;

	if (!(val & AVS_RO_TEMP_STATUS_VALID_MSK))
		return -EIO;

	val &= AVS_RO_TEMP_STATUS_DATA_MSK;

	/* Convert a HW code to a temperature reading (millidegree celsius) */
	t = slope * val + offset;

	*temp = t < 0 ? 0 : t;

	return 0;
}

static const struct thermal_zone_of_device_ops bcm2711_thermal_of_ops = {
	.get_temp	= bcm2711_get_temp,
};

static const struct of_device_id bcm2711_thermal_id_table[] = {
	{ .compatible = "brcm,bcm2711-thermal" },
	{},
};
MODULE_DEVICE_TABLE(of, bcm2711_thermal_id_table);

static int bcm2711_thermal_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *thermal;
	struct bcm2711_thermal_priv *priv;
	struct device *dev = &pdev->dev;
	struct device_node *parent;
	struct regmap *regmap;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* get regmap from syscon node */
	parent = of_get_parent(dev->of_node); /* parent should be syscon node */
	regmap = syscon_node_to_regmap(parent);
	of_node_put(parent);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "failed to get regmap: %d\n", ret);
		return ret;
	}
	priv->regmap = regmap;

	thermal = devm_thermal_zone_of_sensor_register(dev, 0, priv,
						       &bcm2711_thermal_of_ops);
	if (IS_ERR(thermal)) {
		ret = PTR_ERR(thermal);
		dev_err(dev, "could not register sensor: %d\n", ret);
		return ret;
	}

	priv->thermal = thermal;

	thermal->tzp->no_hwmon = false;
	ret = thermal_add_hwmon_sysfs(thermal);
	if (ret)
		return ret;

	return 0;
}

static struct platform_driver bcm2711_thermal_driver = {
	.probe = bcm2711_thermal_probe,
	.driver = {
		.name = "bcm2711_thermal",
		.of_match_table = bcm2711_thermal_id_table,
	},
};
module_platform_driver(bcm2711_thermal_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stefan Wahren");
MODULE_DESCRIPTION("Broadcom AVS RO thermal sensor driver");
