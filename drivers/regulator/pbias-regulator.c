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
	u32 disable_val;
	u32 vmode;
	unsigned int enable_time;
	char *name;
	const unsigned int *pbias_volt_table;
	int n_voltages;
};

struct pbias_of_data {
	unsigned int offset;
};

static const unsigned int pbias_volt_table_3_0V[] = {
	1800000,
	3000000
};

static const unsigned int pbias_volt_table_3_3V[] = {
	1800000,
	3300000
};

static const struct regulator_ops pbias_regulator_voltage_ops = {
	.list_voltage = regulator_list_voltage_table,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct pbias_reg_info pbias_mmc_omap2430 = {
	.enable = BIT(1),
	.enable_mask = BIT(1),
	.vmode = BIT(0),
	.disable_val = 0,
	.enable_time = 100,
	.pbias_volt_table = pbias_volt_table_3_0V,
	.n_voltages = 2,
	.name = "pbias_mmc_omap2430"
};

static const struct pbias_reg_info pbias_sim_omap3 = {
	.enable = BIT(9),
	.enable_mask = BIT(9),
	.vmode = BIT(8),
	.enable_time = 100,
	.pbias_volt_table = pbias_volt_table_3_0V,
	.n_voltages = 2,
	.name = "pbias_sim_omap3"
};

static const struct pbias_reg_info pbias_mmc_omap4 = {
	.enable = BIT(26) | BIT(22),
	.enable_mask = BIT(26) | BIT(25) | BIT(22),
	.disable_val = BIT(25),
	.vmode = BIT(21),
	.enable_time = 100,
	.pbias_volt_table = pbias_volt_table_3_0V,
	.n_voltages = 2,
	.name = "pbias_mmc_omap4"
};

static const struct pbias_reg_info pbias_mmc_omap5 = {
	.enable = BIT(27) | BIT(26),
	.enable_mask = BIT(27) | BIT(25) | BIT(26),
	.disable_val = BIT(25),
	.vmode = BIT(21),
	.enable_time = 100,
	.pbias_volt_table = pbias_volt_table_3_3V,
	.n_voltages = 2,
	.name = "pbias_mmc_omap5"
};

static struct of_regulator_match pbias_matches[] = {
	{ .name = "pbias_mmc_omap2430", .driver_data = (void *)&pbias_mmc_omap2430},
	{ .name = "pbias_sim_omap3", .driver_data = (void *)&pbias_sim_omap3},
	{ .name = "pbias_mmc_omap4", .driver_data = (void *)&pbias_mmc_omap4},
	{ .name = "pbias_mmc_omap5", .driver_data = (void *)&pbias_mmc_omap5},
};
#define PBIAS_NUM_REGS	ARRAY_SIZE(pbias_matches)

/* Offset from SCM general area (and syscon) base */

static const struct pbias_of_data pbias_of_data_omap2 = {
	.offset = 0x230,
};

static const struct pbias_of_data pbias_of_data_omap3 = {
	.offset = 0x2b0,
};

static const struct pbias_of_data pbias_of_data_omap4 = {
	.offset = 0x60,
};

static const struct pbias_of_data pbias_of_data_omap5 = {
	.offset = 0x60,
};

static const struct pbias_of_data pbias_of_data_dra7 = {
	.offset = 0xe00,
};

static const struct of_device_id pbias_of_match[] = {
	{ .compatible = "ti,pbias-omap", },
	{ .compatible = "ti,pbias-omap2", .data = &pbias_of_data_omap2, },
	{ .compatible = "ti,pbias-omap3", .data = &pbias_of_data_omap3, },
	{ .compatible = "ti,pbias-omap4", .data = &pbias_of_data_omap4, },
	{ .compatible = "ti,pbias-omap5", .data = &pbias_of_data_omap5, },
	{ .compatible = "ti,pbias-dra7", .data = &pbias_of_data_dra7, },
	{},
};
MODULE_DEVICE_TABLE(of, pbias_of_match);

static int pbias_regulator_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct regulator_config cfg = { };
	struct regulator_desc *desc;
	struct regulator_dev *rdev;
	struct regmap *syscon;
	const struct pbias_reg_info *info;
	int ret, count, idx;
	const struct pbias_of_data *data;
	unsigned int offset;

	count = of_regulator_match(&pdev->dev, np, pbias_matches,
						PBIAS_NUM_REGS);
	if (count < 0)
		return count;

	desc = devm_kcalloc(&pdev->dev, count, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	syscon = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(syscon))
		return PTR_ERR(syscon);

	data = of_device_get_match_data(&pdev->dev);
	if (data) {
		offset = data->offset;
	} else {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res)
			return -EINVAL;

		offset = res->start;
		dev_WARN(&pdev->dev,
			 "using legacy dt data for pbias offset\n");
	}

	cfg.regmap = syscon;
	cfg.dev = &pdev->dev;

	for (idx = 0; idx < PBIAS_NUM_REGS && count; idx++) {
		if (!pbias_matches[idx].init_data ||
			!pbias_matches[idx].of_node)
			continue;

		info = pbias_matches[idx].driver_data;
		if (!info)
			return -ENODEV;

		desc->name = info->name;
		desc->owner = THIS_MODULE;
		desc->type = REGULATOR_VOLTAGE;
		desc->ops = &pbias_regulator_voltage_ops;
		desc->volt_table = info->pbias_volt_table;
		desc->n_voltages = info->n_voltages;
		desc->enable_time = info->enable_time;
		desc->vsel_reg = offset;
		desc->vsel_mask = info->vmode;
		desc->enable_reg = offset;
		desc->enable_mask = info->enable_mask;
		desc->enable_val = info->enable;
		desc->disable_val = info->disable_val;

		cfg.init_data = pbias_matches[idx].init_data;
		cfg.of_node = pbias_matches[idx].of_node;

		rdev = devm_regulator_register(&pdev->dev, desc, &cfg);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(&pdev->dev,
				"Failed to register regulator: %d\n", ret);
			return ret;
		}
		desc++;
		count--;
	}

	return 0;
}

static struct platform_driver pbias_regulator_driver = {
	.probe		= pbias_regulator_probe,
	.driver		= {
		.name		= "pbias-regulator",
		.of_match_table = of_match_ptr(pbias_of_match),
	},
};

module_platform_driver(pbias_regulator_driver);

MODULE_AUTHOR("Balaji T K <balajitk@ti.com>");
MODULE_DESCRIPTION("pbias voltage regulator");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pbias-regulator");
