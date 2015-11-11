/*
 * Dove thermal sensor driver
 *
 * Copyright (C) 2013 Andrew Lunn <andrew@lunn.ch>
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
#include <linux/platform_device.h>
#include <linux/thermal.h>

#define DOVE_THERMAL_TEMP_OFFSET	1
#define DOVE_THERMAL_TEMP_MASK		0x1FF

/* Dove Thermal Manager Control and Status Register */
#define PMU_TM_DISABLE_OFFS		0
#define PMU_TM_DISABLE_MASK		(0x1 << PMU_TM_DISABLE_OFFS)

/* Dove Theraml Diode Control 0 Register */
#define PMU_TDC0_SW_RST_MASK		(0x1 << 1)
#define PMU_TDC0_SEL_VCAL_OFFS		5
#define PMU_TDC0_SEL_VCAL_MASK		(0x3 << PMU_TDC0_SEL_VCAL_OFFS)
#define PMU_TDC0_REF_CAL_CNT_OFFS	11
#define PMU_TDC0_REF_CAL_CNT_MASK	(0x1FF << PMU_TDC0_REF_CAL_CNT_OFFS)
#define PMU_TDC0_AVG_NUM_OFFS		25
#define PMU_TDC0_AVG_NUM_MASK		(0x7 << PMU_TDC0_AVG_NUM_OFFS)

/* Dove Thermal Diode Control 1 Register */
#define PMU_TEMP_DIOD_CTRL1_REG		0x04
#define PMU_TDC1_TEMP_VALID_MASK	(0x1 << 10)

/* Dove Thermal Sensor Dev Structure */
struct dove_thermal_priv {
	void __iomem *sensor;
	void __iomem *control;
};

static int dove_init_sensor(const struct dove_thermal_priv *priv)
{
	u32 reg;
	u32 i;

	/* Configure the Diode Control Register #0 */
	reg = readl_relaxed(priv->control);

	/* Use average of 2 */
	reg &= ~PMU_TDC0_AVG_NUM_MASK;
	reg |= (0x1 << PMU_TDC0_AVG_NUM_OFFS);

	/* Reference calibration value */
	reg &= ~PMU_TDC0_REF_CAL_CNT_MASK;
	reg |= (0x0F1 << PMU_TDC0_REF_CAL_CNT_OFFS);

	/* Set the high level reference for calibration */
	reg &= ~PMU_TDC0_SEL_VCAL_MASK;
	reg |= (0x2 << PMU_TDC0_SEL_VCAL_OFFS);
	writel(reg, priv->control);

	/* Reset the sensor */
	reg = readl_relaxed(priv->control);
	writel((reg | PMU_TDC0_SW_RST_MASK), priv->control);
	writel(reg, priv->control);

	/* Enable the sensor */
	reg = readl_relaxed(priv->sensor);
	reg &= ~PMU_TM_DISABLE_MASK;
	writel(reg, priv->sensor);

	/* Poll the sensor for the first reading */
	for (i = 0; i < 1000000; i++) {
		reg = readl_relaxed(priv->sensor);
		if (reg & DOVE_THERMAL_TEMP_MASK)
			break;
	}

	if (i == 1000000)
		return -EIO;

	return 0;
}

static int dove_get_temp(struct thermal_zone_device *thermal,
			  int *temp)
{
	unsigned long reg;
	struct dove_thermal_priv *priv = thermal->devdata;

	/* Valid check */
	reg = readl_relaxed(priv->control + PMU_TEMP_DIOD_CTRL1_REG);
	if ((reg & PMU_TDC1_TEMP_VALID_MASK) == 0x0) {
		dev_err(&thermal->device,
			"Temperature sensor reading not valid\n");
		return -EIO;
	}

	/*
	 * Calculate temperature. According to Marvell internal
	 * documentation the formula for this is:
	 * Celsius = (322-reg)/1.3625
	 */
	reg = readl_relaxed(priv->sensor);
	reg = (reg >> DOVE_THERMAL_TEMP_OFFSET) & DOVE_THERMAL_TEMP_MASK;
	*temp = ((3220000000UL - (10000000UL * reg)) / 13625);

	return 0;
}

static struct thermal_zone_device_ops ops = {
	.get_temp = dove_get_temp,
};

static const struct of_device_id dove_thermal_id_table[] = {
	{ .compatible = "marvell,dove-thermal" },
	{}
};

static int dove_thermal_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *thermal = NULL;
	struct dove_thermal_priv *priv;
	struct resource *res;
	int ret;

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

	ret = dove_init_sensor(priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize sensor\n");
		return ret;
	}

	thermal = thermal_zone_device_register("dove_thermal", 0, 0,
					       priv, &ops, NULL, 0, 0);
	if (IS_ERR(thermal)) {
		dev_err(&pdev->dev,
			"Failed to register thermal zone device\n");
		return PTR_ERR(thermal);
	}

	platform_set_drvdata(pdev, thermal);

	return 0;
}

static int dove_thermal_exit(struct platform_device *pdev)
{
	struct thermal_zone_device *dove_thermal =
		platform_get_drvdata(pdev);

	thermal_zone_device_unregister(dove_thermal);

	return 0;
}

MODULE_DEVICE_TABLE(of, dove_thermal_id_table);

static struct platform_driver dove_thermal_driver = {
	.probe = dove_thermal_probe,
	.remove = dove_thermal_exit,
	.driver = {
		.name = "dove_thermal",
		.of_match_table = dove_thermal_id_table,
	},
};

module_platform_driver(dove_thermal_driver);

MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch>");
MODULE_DESCRIPTION("Dove thermal driver");
MODULE_LICENSE("GPL");
