// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#define PVTMON_CONTROL0					0x00
#define PVTMON_CONTROL0_SEL_MASK			0x0000000e
#define PVTMON_CONTROL0_SEL_TEMP_MONITOR		0x00000000
#define PVTMON_CONTROL0_SEL_TEST_MODE			0x0000000e
#define PVTMON_STATUS					0x08

static int ns_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	void __iomem *pvtmon = thermal_zone_device_priv(tz);
	int offset = thermal_zone_get_offset(tz);
	int slope = thermal_zone_get_slope(tz);
	u32 val;

	val = readl(pvtmon + PVTMON_CONTROL0);
	if ((val & PVTMON_CONTROL0_SEL_MASK) != PVTMON_CONTROL0_SEL_TEMP_MONITOR) {
		/* Clear current mode selection */
		val &= ~PVTMON_CONTROL0_SEL_MASK;

		/* Set temp monitor mode (it's the default actually) */
		val |= PVTMON_CONTROL0_SEL_TEMP_MONITOR;

		writel(val, pvtmon + PVTMON_CONTROL0);
	}

	val = readl(pvtmon + PVTMON_STATUS);
	*temp = slope * val + offset;

	return 0;
}

static const struct thermal_zone_device_ops ns_thermal_ops = {
	.get_temp = ns_thermal_get_temp,
};

static int ns_thermal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct thermal_zone_device *tz;
	void __iomem *pvtmon;

	pvtmon = of_iomap(dev_of_node(dev), 0);
	if (WARN_ON(!pvtmon))
		return -ENOENT;

	tz = devm_thermal_of_zone_register(dev, 0,
					   pvtmon,
					   &ns_thermal_ops);
	if (IS_ERR(tz)) {
		iounmap(pvtmon);
		return PTR_ERR(tz);
	}

	platform_set_drvdata(pdev, pvtmon);

	return 0;
}

static void ns_thermal_remove(struct platform_device *pdev)
{
	void __iomem *pvtmon = platform_get_drvdata(pdev);

	iounmap(pvtmon);
}

static const struct of_device_id ns_thermal_of_match[] = {
	{ .compatible = "brcm,ns-thermal", },
	{},
};
MODULE_DEVICE_TABLE(of, ns_thermal_of_match);

static struct platform_driver ns_thermal_driver = {
	.probe		= ns_thermal_probe,
	.remove		= ns_thermal_remove,
	.driver = {
		.name = "ns-thermal",
		.of_match_table = ns_thermal_of_match,
	},
};
module_platform_driver(ns_thermal_driver);

MODULE_AUTHOR("Rafał Miłecki <rafal@milecki.pl>");
MODULE_DESCRIPTION("Northstar thermal driver");
MODULE_LICENSE("GPL v2");
