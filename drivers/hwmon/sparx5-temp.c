// SPDX-License-Identifier: GPL-2.0-or-later
/* Sparx5 SoC temperature sensor driver
 *
 * Copyright (C) 2020 Lars Povlsen <lars.povlsen@microchip.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define TEMP_CTRL		0
#define TEMP_CFG		4
#define  TEMP_CFG_CYCLES	GENMASK(24, 15)
#define  TEMP_CFG_ENA		BIT(0)
#define TEMP_STAT		8
#define  TEMP_STAT_VALID	BIT(12)
#define  TEMP_STAT_TEMP		GENMASK(11, 0)

struct s5_hwmon {
	void __iomem *base;
	struct clk *clk;
};

static void s5_temp_enable(struct s5_hwmon *hwmon)
{
	u32 val = readl(hwmon->base + TEMP_CFG);
	u32 clk = clk_get_rate(hwmon->clk) / USEC_PER_SEC;

	val &= ~TEMP_CFG_CYCLES;
	val |= FIELD_PREP(TEMP_CFG_CYCLES, clk);
	val |= TEMP_CFG_ENA;

	writel(val, hwmon->base + TEMP_CFG);
}

static int s5_read(struct device *dev, enum hwmon_sensor_types type,
		   u32 attr, int channel, long *temp)
{
	struct s5_hwmon *hwmon = dev_get_drvdata(dev);
	int rc = 0, value;
	u32 stat;

	switch (attr) {
	case hwmon_temp_input:
		stat = readl_relaxed(hwmon->base + TEMP_STAT);
		if (!(stat & TEMP_STAT_VALID))
			return -EAGAIN;
		value = stat & TEMP_STAT_TEMP;
		/*
		 * From register documentation:
		 * Temp(C) = TEMP_SENSOR_STAT.TEMP / 4096 * 352.2 - 109.4
		 */
		value = DIV_ROUND_CLOSEST(value * 3522, 4096) - 1094;
		/*
		 * Scale down by 10 from above and multiply by 1000 to
		 * have millidegrees as specified by the hwmon sysfs
		 * interface.
		 */
		value *= 100;
		*temp = value;
		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}

	return rc;
}

static umode_t s5_is_visible(const void *_data, enum hwmon_sensor_types type,
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

static const struct hwmon_channel_info *s5_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops s5_hwmon_ops = {
	.is_visible = s5_is_visible,
	.read = s5_read,
};

static const struct hwmon_chip_info s5_chip_info = {
	.ops = &s5_hwmon_ops,
	.info = s5_info,
};

static int s5_temp_probe(struct platform_device *pdev)
{
	struct device *hwmon_dev;
	struct s5_hwmon *hwmon;

	hwmon = devm_kzalloc(&pdev->dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	hwmon->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hwmon->base))
		return PTR_ERR(hwmon->base);

	hwmon->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(hwmon->clk))
		return PTR_ERR(hwmon->clk);

	s5_temp_enable(hwmon);

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							 "s5_temp",
							 hwmon,
							 &s5_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id s5_temp_match[] = {
	{ .compatible = "microchip,sparx5-temp" },
	{},
};
MODULE_DEVICE_TABLE(of, s5_temp_match);

static struct platform_driver s5_temp_driver = {
	.probe = s5_temp_probe,
	.driver = {
		.name = "sparx5-temp",
		.of_match_table = s5_temp_match,
	},
};

module_platform_driver(s5_temp_driver);

MODULE_AUTHOR("Lars Povlsen <lars.povlsen@microchip.com>");
MODULE_DESCRIPTION("Sparx5 SoC temperature sensor driver");
MODULE_LICENSE("GPL");
