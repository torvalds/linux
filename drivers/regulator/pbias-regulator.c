/*
 * pbias-regulator.c
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Balaji T K <balajitk@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>

struct pbias_reg_info {
	u32 enable;
	u32 enable_mask;
	u32 vmode;
	unsigned int enable_time;
	char *name;
};

struct pbias_regulator_data {
	struct regulator_desc desc;
	void __iomem *pbias_addr;
	unsigned int pbias_reg;
	struct regulator_dev *dev;
	struct regmap *syscon;
	const struct pbias_reg_info *info;
	int voltage;
};

static int pbias_regulator_set_voltage(struct regulator_dev *dev,
			int min_uV, int max_uV, unsigned *selector)
{
	struct pbias_regulator_data *data = rdev_get_drvdata(dev);
	const struct pbias_reg_info *info = data->info;
	int ret, vmode;

	if (min_uV <= 1800000)
		vmode = 0;
	else if (min_uV > 1800000)
		vmode = info->vmode;

	ret = regmap_update_bits(data->syscon, data->pbias_reg,
						info->vmode, vmode);

	return ret;
}

static int pbias_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct pbias_regulator_data *data = rdev_get_drvdata(rdev);
	const struct pbias_reg_info *info = data->info;
	int value, voltage;

	regmap_read(data->syscon, data->pbias_reg, &value);
	value &= info->vmode;

	voltage = value ? 3000000 : 1800000;

	return voltage;
}

static int pbias_regulator_enable(struct regulator_dev *rdev)
{
	struct pbias_regulator_data *data = rdev_get_drvdata(rdev);
	const struct pbias_reg_info *info = data->info;
	int ret;

	ret = regmap_update_bits(data->syscon, data->pbias_reg,
					info->enable_mask, info->enable);

	return ret;
}

static int pbias_regulator_disable(struct regulator_dev *rdev)
{
	struct pbias_regulator_data *data = rdev_get_drvdata(rdev);
	const struct pbias_reg_info *info = data->info;
	int ret;

	ret = regmap_update_bits(data->syscon, data->pbias_reg,
						info->enable_mask, 0);
	return ret;
}

static int pbias_regulator_is_enable(struct regulator_dev *rdev)
{
	struct pbias_regulator_data *data = rdev_get_drvdata(rdev);
	const struct pbias_reg_info *info = data->info;
	int value;

	regmap_read(data->syscon, data->pbias_reg, &value);

	return (value & info->enable_mask) == info->enable_mask;
}

static struct regulator_ops pbias_regulator_voltage_ops = {
	.set_voltage	= pbias_regulator_set_voltage,
	.get_voltage	= pbias_regulator_get_voltage,
	.enable		= pbias_regulator_enable,
	.disable	= pbias_regulator_disable,
	.is_enabled	= pbias_regulator_is_enable,
};

static const struct pbias_reg_info pbias_mmc_omap2430 = {
	.enable = BIT(1),
	.enable_mask = BIT(1),
	.vmode = BIT(0),
	.enable_time = 100,
	.name = "pbias_mmc_omap2430"
};

static const struct pbias_reg_info pbias_sim_omap3 = {
	.enable = BIT(9),
	.enable_mask = BIT(9),
	.vmode = BIT(8),
	.enable_time = 100,
	.name = "pbias_sim_omap3"
};

static const struct pbias_reg_info pbias_mmc_omap4 = {
	.enable = BIT(26) | BIT(22),
	.enable_mask = BIT(26) | BIT(25) | BIT(22),
	.vmode = BIT(21),
	.enable_time = 100,
	.name = "pbias_mmc_omap4"
};

static const struct pbias_reg_info pbias_mmc_omap5 = {
	.enable = BIT(27) | BIT(26),
	.enable_mask = BIT(27) | BIT(25) | BIT(26),
	.vmode = BIT(21),
	.enable_time = 100,
	.name = "pbias_mmc_omap5"
};

static struct of_regulator_match pbias_matches[] = {
	{ .name = "pbias_mmc_omap2430", .driver_data = (void *)&pbias_mmc_omap2430},
	{ .name = "pbias_sim_omap3", .driver_data = (void *)&pbias_sim_omap3},
	{ .name = "pbias_mmc_omap4", .driver_data = (void *)&pbias_mmc_omap4},
	{ .name = "pbias_mmc_omap5", .driver_data = (void *)&pbias_mmc_omap5},
};
#define PBIAS_NUM_REGS	ARRAY_SIZE(pbias_matches)

static const struct of_device_id pbias_of_match[] = {
	{ .compatible = "ti,pbias-omap", },
	{},
};
MODULE_DEVICE_TABLE(of, pbias_of_match);

static int pbias_regulator_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct pbias_regulator_data *drvdata;
	struct resource *res;
	struct regulator_config cfg = { };
	struct regmap *syscon;
	const struct pbias_reg_info *info;
	int ret = 0;
	int count, idx, data_idx = 0;

	count = of_regulator_match(&pdev->dev, np, pbias_matches,
						PBIAS_NUM_REGS);
	if (count < 0)
		return count;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct pbias_regulator_data)
			       * count, GFP_KERNEL);
	if (drvdata == NULL) {
		dev_err(&pdev->dev, "Failed to allocate device data\n");
		return -ENOMEM;
	}

	syscon = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(syscon))
		return PTR_ERR(syscon);

	cfg.dev = &pdev->dev;

	for (idx = 0; idx < PBIAS_NUM_REGS && data_idx < count; idx++) {
		if (!pbias_matches[idx].init_data ||
			!pbias_matches[idx].of_node)
			continue;

		info = pbias_matches[idx].driver_data;
		if (!info)
			return -ENODEV;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res)
			return -EINVAL;

		drvdata[data_idx].pbias_reg = res->start;
		drvdata[data_idx].syscon = syscon;
		drvdata[data_idx].info = info;
		drvdata[data_idx].desc.name = info->name;
		drvdata[data_idx].desc.owner = THIS_MODULE;
		drvdata[data_idx].desc.type = REGULATOR_VOLTAGE;
		drvdata[data_idx].desc.ops = &pbias_regulator_voltage_ops;
		drvdata[data_idx].desc.n_voltages = 2;
		drvdata[data_idx].desc.enable_time = info->enable_time;

		cfg.init_data = pbias_matches[idx].init_data;
		cfg.driver_data = &drvdata[data_idx];
		cfg.of_node = pbias_matches[idx].of_node;

		drvdata[data_idx].dev = devm_regulator_register(&pdev->dev,
					&drvdata[data_idx].desc, &cfg);
		if (IS_ERR(drvdata[data_idx].dev)) {
			ret = PTR_ERR(drvdata[data_idx].dev);
			dev_err(&pdev->dev,
				"Failed to register regulator: %d\n", ret);
			goto err_regulator;
		}
		data_idx++;
	}

	platform_set_drvdata(pdev, drvdata);

err_regulator:
	return ret;
}

static struct platform_driver pbias_regulator_driver = {
	.probe		= pbias_regulator_probe,
	.driver		= {
		.name		= "pbias-regulator",
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(pbias_of_match),
	},
};

module_platform_driver(pbias_regulator_driver);

MODULE_AUTHOR("Balaji T K <balajitk@ti.com>");
MODULE_DESCRIPTION("pbias voltage regulator");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pbias-regulator");
