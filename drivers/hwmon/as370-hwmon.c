// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics AS370 SoC Hardware Monitoring Driver
 *
 * Copyright (C) 2018 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 */

#include <linux/bitops.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#define CTRL		0x0
#define  PD		BIT(0)
#define  EN		BIT(1)
#define  T_SEL		BIT(2)
#define  V_SEL		BIT(3)
#define  NMOS_SEL	BIT(8)
#define  PMOS_SEL	BIT(9)
#define STS		0x4
#define  BN_MASK	GENMASK(11, 0)
#define  EOC		BIT(12)

struct as370_hwmon {
	void __iomem *base;
};

static void init_pvt(struct as370_hwmon *hwmon)
{
	u32 val;
	void __iomem *addr = hwmon->base + CTRL;

	val = PD;
	writel_relaxed(val, addr);
	val |= T_SEL;
	writel_relaxed(val, addr);
	val |= EN;
	writel_relaxed(val, addr);
	val &= ~PD;
	writel_relaxed(val, addr);
}

static int as370_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, long *temp)
{
	int val;
	struct as370_hwmon *hwmon = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_input:
		val = readl_relaxed(hwmon->base + STS) & BN_MASK;
		*temp = DIV_ROUND_CLOSEST(val * 251802, 4096) - 85525;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static umode_t
as370_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
		       u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	default:
		return 0;
	}
}

static const struct hwmon_channel_info * const as370_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops as370_hwmon_ops = {
	.is_visible = as370_hwmon_is_visible,
	.read = as370_hwmon_read,
};

static const struct hwmon_chip_info as370_chip_info = {
	.ops = &as370_hwmon_ops,
	.info = as370_hwmon_info,
};

static int as370_hwmon_probe(struct platform_device *pdev)
{
	struct device *hwmon_dev;
	struct as370_hwmon *hwmon;
	struct device *dev = &pdev->dev;

	hwmon = devm_kzalloc(dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	hwmon->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hwmon->base))
		return PTR_ERR(hwmon->base);

	init_pvt(hwmon);

	hwmon_dev = devm_hwmon_device_register_with_info(dev,
							 "as370",
							 hwmon,
							 &as370_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id as370_hwmon_match[] = {
	{ .compatible = "syna,as370-hwmon" },
	{},
};
MODULE_DEVICE_TABLE(of, as370_hwmon_match);

static struct platform_driver as370_hwmon_driver = {
	.probe = as370_hwmon_probe,
	.driver = {
		.name = "as370-hwmon",
		.of_match_table = as370_hwmon_match,
	},
};
module_platform_driver(as370_hwmon_driver);

MODULE_AUTHOR("Jisheng Zhang<jszhang@kernel.org>");
MODULE_DESCRIPTION("Synaptics AS370 SoC hardware monitor");
MODULE_LICENSE("GPL v2");
