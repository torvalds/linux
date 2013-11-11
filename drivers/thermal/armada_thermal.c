/*
 * Marvell Armada 370/XP thermal sensor driver
 *
 * Copyright (C) 2013 Marvell
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/thermal.h>

#define THERMAL_VALID_OFFSET		9
#define THERMAL_VALID_MASK		0x1
#define THERMAL_TEMP_OFFSET		10
#define THERMAL_TEMP_MASK		0x1ff

/* Thermal Manager Control and Status Register */
#define PMU_TDC0_SW_RST_MASK		(0x1 << 1)
#define PMU_TM_DISABLE_OFFS		0
#define PMU_TM_DISABLE_MASK		(0x1 << PMU_TM_DISABLE_OFFS)
#define PMU_TDC0_REF_CAL_CNT_OFFS	11
#define PMU_TDC0_REF_CAL_CNT_MASK	(0x1ff << PMU_TDC0_REF_CAL_CNT_OFFS)
#define PMU_TDC0_OTF_CAL_MASK		(0x1 << 30)
#define PMU_TDC0_START_CAL_MASK		(0x1 << 25)

struct armada_thermal_ops;

/* Marvell EBU Thermal Sensor Dev Structure */
struct armada_thermal_priv {
	void __iomem *sensor;
	void __iomem *control;
	struct armada_thermal_ops *ops;
};

struct armada_thermal_ops {
	/* Initialize the sensor */
	void (*init_sensor)(struct armada_thermal_priv *);

	/* Test for a valid sensor value (optional) */
	bool (*is_valid)(struct armada_thermal_priv *);
};

static void armadaxp_init_sensor(struct armada_thermal_priv *priv)
{
	unsigned long reg;

	reg = readl_relaxed(priv->control);
	reg |= PMU_TDC0_OTF_CAL_MASK;
	writel(reg, priv->control);

	/* Reference calibration value */
	reg &= ~PMU_TDC0_REF_CAL_CNT_MASK;
	reg |= (0xf1 << PMU_TDC0_REF_CAL_CNT_OFFS);
	writel(reg, priv->control);

	/* Reset the sensor */
	reg = readl_relaxed(priv->control);
	writel((reg | PMU_TDC0_SW_RST_MASK), priv->control);

	writel(reg, priv->control);

	/* Enable the sensor */
	reg = readl_relaxed(priv->sensor);
	reg &= ~PMU_TM_DISABLE_MASK;
	writel(reg, priv->sensor);
}

static void armada370_init_sensor(struct armada_thermal_priv *priv)
{
	unsigned long reg;

	reg = readl_relaxed(priv->control);
	reg |= PMU_TDC0_OTF_CAL_MASK;
	writel(reg, priv->control);

	/* Reference calibration value */
	reg &= ~PMU_TDC0_REF_CAL_CNT_MASK;
	reg |= (0xf1 << PMU_TDC0_REF_CAL_CNT_OFFS);
	writel(reg, priv->control);

	reg &= ~PMU_TDC0_START_CAL_MASK;
	writel(reg, priv->control);

	mdelay(10);
}

static bool armada_is_valid(struct armada_thermal_priv *priv)
{
	unsigned long reg = readl_relaxed(priv->sensor);

	return (reg >> THERMAL_VALID_OFFSET) & THERMAL_VALID_MASK;
}

static int armada_get_temp(struct thermal_zone_device *thermal,
			  unsigned long *temp)
{
	struct armada_thermal_priv *priv = thermal->devdata;
	unsigned long reg;

	/* Valid check */
	if (priv->ops->is_valid && !priv->ops->is_valid(priv)) {
		dev_err(&thermal->device,
			"Temperature sensor reading not valid\n");
		return -EIO;
	}

	reg = readl_relaxed(priv->sensor);
	reg = (reg >> THERMAL_TEMP_OFFSET) & THERMAL_TEMP_MASK;
	*temp = (3153000000UL - (10000000UL*reg)) / 13825;
	return 0;
}

static struct thermal_zone_device_ops ops = {
	.get_temp = armada_get_temp,
};

static const struct armada_thermal_ops armadaxp_ops = {
	.init_sensor = armadaxp_init_sensor,
};

static const struct armada_thermal_ops armada370_ops = {
	.is_valid = armada_is_valid,
	.init_sensor = armada370_init_sensor,
};

static const struct of_device_id armada_thermal_id_table[] = {
	{
		.compatible = "marvell,armadaxp-thermal",
		.data       = &armadaxp_ops,
	},
	{
		.compatible = "marvell,armada370-thermal",
		.data       = &armada370_ops,
	},
	{
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, armada_thermal_id_table);

static int armada_thermal_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *thermal;
	const struct of_device_id *match;
	struct armada_thermal_priv *priv;
	struct resource *res;

	match = of_match_device(armada_thermal_id_table, &pdev->dev);
	if (!match)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->sensor = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->sensor))
		return PTR_ERR(priv->sensor);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->control = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->control))
		return PTR_ERR(priv->control);

	priv->ops = (struct armada_thermal_ops *)match->data;
	priv->ops->init_sensor(priv);

	thermal = thermal_zone_device_register("armada_thermal", 0, 0,
					       priv, &ops, NULL, 0, 0);
	if (IS_ERR(thermal)) {
		dev_err(&pdev->dev,
			"Failed to register thermal zone device\n");
		return PTR_ERR(thermal);
	}

	platform_set_drvdata(pdev, thermal);

	return 0;
}

static int armada_thermal_exit(struct platform_device *pdev)
{
	struct thermal_zone_device *armada_thermal =
		platform_get_drvdata(pdev);

	thermal_zone_device_unregister(armada_thermal);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver armada_thermal_driver = {
	.probe = armada_thermal_probe,
	.remove = armada_thermal_exit,
	.driver = {
		.name = "armada_thermal",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(armada_thermal_id_table),
	},
};

module_platform_driver(armada_thermal_driver);

MODULE_AUTHOR("Ezequiel Garcia <ezequiel.garcia@free-electrons.com>");
MODULE_DESCRIPTION("Armada 370/XP thermal driver");
MODULE_LICENSE("GPL v2");
