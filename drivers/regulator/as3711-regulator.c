/*
 * AS3711 PMIC regulator driver, using DCDC Step Down and LDO supplies
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 * Author: Guennadi Liakhovetski, <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License as
 * published by the Free Software Foundation
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/mfd/as3711.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

struct as3711_regulator_info {
	struct regulator_desc	desc;
};

struct as3711_regulator {
	struct as3711_regulator_info *reg_info;
};

/*
 * The regulator API supports 4 modes of operataion: FAST, NORMAL, IDLE and
 * STANDBY. We map them in the following way to AS3711 SD1-4 DCDC modes:
 * FAST:	sdX_fast=1
 * NORMAL:	low_noise=1
 * IDLE:	low_noise=0
 */

static int as3711_set_mode_sd(struct regulator_dev *rdev, unsigned int mode)
{
	unsigned int fast_bit = rdev->desc->enable_mask,
		low_noise_bit = fast_bit << 4;
	u8 val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = fast_bit | low_noise_bit;
		break;
	case REGULATOR_MODE_NORMAL:
		val = low_noise_bit;
		break;
	case REGULATOR_MODE_IDLE:
		val = 0;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, AS3711_SD_CONTROL_1,
				  low_noise_bit | fast_bit, val);
}

static unsigned int as3711_get_mode_sd(struct regulator_dev *rdev)
{
	unsigned int fast_bit = rdev->desc->enable_mask,
		low_noise_bit = fast_bit << 4, mask = fast_bit | low_noise_bit;
	unsigned int val;
	int ret = regmap_read(rdev->regmap, AS3711_SD_CONTROL_1, &val);

	if (ret < 0)
		return ret;

	if ((val & mask) == mask)
		return REGULATOR_MODE_FAST;

	if ((val & mask) == low_noise_bit)
		return REGULATOR_MODE_NORMAL;

	if (!(val & mask))
		return REGULATOR_MODE_IDLE;

	return -EINVAL;
}

static const struct regulator_ops as3711_sd_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_mode		= as3711_get_mode_sd,
	.set_mode		= as3711_set_mode_sd,
};

static const struct regulator_ops as3711_aldo_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_ops as3711_dldo_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_linear_range as3711_sd_ranges[] = {
	REGULATOR_LINEAR_RANGE(612500, 0x1, 0x40, 12500),
	REGULATOR_LINEAR_RANGE(1425000, 0x41, 0x70, 25000),
	REGULATOR_LINEAR_RANGE(2650000, 0x71, 0x7f, 50000),
};

static const struct regulator_linear_range as3711_aldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0, 0xf, 50000),
	REGULATOR_LINEAR_RANGE(1800000, 0x10, 0x1f, 100000),
};

static const struct regulator_linear_range as3711_dldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000, 0, 0x10, 50000),
	REGULATOR_LINEAR_RANGE(1750000, 0x20, 0x3f, 50000),
};

#define AS3711_REG(_id, _en_reg, _en_bit, _vmask, _sfx)			   \
	[AS3711_REGULATOR_ ## _id] = {					   \
	.desc = {							   \
		.name = "as3711-regulator-" # _id,			   \
		.id = AS3711_REGULATOR_ ## _id,				   \
		.n_voltages = (_vmask + 1),				   \
		.ops = &as3711_ ## _sfx ## _ops,			   \
		.type = REGULATOR_VOLTAGE,				   \
		.owner = THIS_MODULE,					   \
		.vsel_reg = AS3711_ ## _id ## _VOLTAGE,			   \
		.vsel_mask = _vmask,					   \
		.enable_reg = AS3711_ ## _en_reg,			   \
		.enable_mask = BIT(_en_bit),				   \
		.linear_ranges = as3711_ ## _sfx ## _ranges,		   \
		.n_linear_ranges = ARRAY_SIZE(as3711_ ## _sfx ## _ranges), \
	},								   \
}

static struct as3711_regulator_info as3711_reg_info[] = {
	AS3711_REG(SD_1, SD_CONTROL, 0, 0x7f, sd),
	AS3711_REG(SD_2, SD_CONTROL, 1, 0x7f, sd),
	AS3711_REG(SD_3, SD_CONTROL, 2, 0x7f, sd),
	AS3711_REG(SD_4, SD_CONTROL, 3, 0x7f, sd),
	AS3711_REG(LDO_1, LDO_1_VOLTAGE, 7, 0x1f, aldo),
	AS3711_REG(LDO_2, LDO_2_VOLTAGE, 7, 0x1f, aldo),
	AS3711_REG(LDO_3, LDO_3_VOLTAGE, 7, 0x3f, dldo),
	AS3711_REG(LDO_4, LDO_4_VOLTAGE, 7, 0x3f, dldo),
	AS3711_REG(LDO_5, LDO_5_VOLTAGE, 7, 0x3f, dldo),
	AS3711_REG(LDO_6, LDO_6_VOLTAGE, 7, 0x3f, dldo),
	AS3711_REG(LDO_7, LDO_7_VOLTAGE, 7, 0x3f, dldo),
	AS3711_REG(LDO_8, LDO_8_VOLTAGE, 7, 0x3f, dldo),
	/* StepUp output voltage depends on supplying regulator */
};

#define AS3711_REGULATOR_NUM ARRAY_SIZE(as3711_reg_info)

static struct of_regulator_match
as3711_regulator_matches[AS3711_REGULATOR_NUM] = {
	[AS3711_REGULATOR_SD_1] = { .name = "sd1" },
	[AS3711_REGULATOR_SD_2] = { .name = "sd2" },
	[AS3711_REGULATOR_SD_3] = { .name = "sd3" },
	[AS3711_REGULATOR_SD_4] = { .name = "sd4" },
	[AS3711_REGULATOR_LDO_1] = { .name = "ldo1" },
	[AS3711_REGULATOR_LDO_2] = { .name = "ldo2" },
	[AS3711_REGULATOR_LDO_3] = { .name = "ldo3" },
	[AS3711_REGULATOR_LDO_4] = { .name = "ldo4" },
	[AS3711_REGULATOR_LDO_5] = { .name = "ldo5" },
	[AS3711_REGULATOR_LDO_6] = { .name = "ldo6" },
	[AS3711_REGULATOR_LDO_7] = { .name = "ldo7" },
	[AS3711_REGULATOR_LDO_8] = { .name = "ldo8" },
};

static int as3711_regulator_parse_dt(struct device *dev,
				struct device_node **of_node, const int count)
{
	struct as3711_regulator_pdata *pdata = dev_get_platdata(dev);
	struct device_node *regulators =
		of_get_child_by_name(dev->parent->of_node, "regulators");
	struct of_regulator_match *match;
	int ret, i;

	if (!regulators) {
		dev_err(dev, "regulator node not found\n");
		return -ENODEV;
	}

	ret = of_regulator_match(dev->parent, regulators,
				 as3711_regulator_matches, count);
	of_node_put(regulators);
	if (ret < 0) {
		dev_err(dev, "Error parsing regulator init data: %d\n", ret);
		return ret;
	}

	for (i = 0, match = as3711_regulator_matches; i < count; i++, match++)
		if (match->of_node) {
			pdata->init_data[i] = match->init_data;
			of_node[i] = match->of_node;
		}

	return 0;
}

static int as3711_regulator_probe(struct platform_device *pdev)
{
	struct as3711_regulator_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct as3711 *as3711 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = {.dev = &pdev->dev,};
	struct as3711_regulator *reg = NULL;
	struct as3711_regulator *regs;
	struct device_node *of_node[AS3711_REGULATOR_NUM] = {};
	struct regulator_dev *rdev;
	struct as3711_regulator_info *ri;
	int ret;
	int id;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data...\n");
		return -ENODEV;
	}

	if (pdev->dev.parent->of_node) {
		ret = as3711_regulator_parse_dt(&pdev->dev, of_node, AS3711_REGULATOR_NUM);
		if (ret < 0) {
			dev_err(&pdev->dev, "DT parsing failed: %d\n", ret);
			return ret;
		}
	}

	regs = devm_kzalloc(&pdev->dev, AS3711_REGULATOR_NUM *
			sizeof(struct as3711_regulator), GFP_KERNEL);
	if (!regs)
		return -ENOMEM;

	for (id = 0, ri = as3711_reg_info; id < AS3711_REGULATOR_NUM; ++id, ri++) {
		reg = &regs[id];
		reg->reg_info = ri;

		config.init_data = pdata->init_data[id];
		config.driver_data = reg;
		config.regmap = as3711->regmap;
		config.of_node = of_node[id];

		rdev = devm_regulator_register(&pdev->dev, &ri->desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register regulator %s\n",
				ri->desc.name);
			return PTR_ERR(rdev);
		}
	}
	platform_set_drvdata(pdev, regs);
	return 0;
}

static struct platform_driver as3711_regulator_driver = {
	.driver	= {
		.name	= "as3711-regulator",
	},
	.probe		= as3711_regulator_probe,
};

static int __init as3711_regulator_init(void)
{
	return platform_driver_register(&as3711_regulator_driver);
}
subsys_initcall(as3711_regulator_init);

static void __exit as3711_regulator_exit(void)
{
	platform_driver_unregister(&as3711_regulator_driver);
}
module_exit(as3711_regulator_exit);

MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_DESCRIPTION("AS3711 regulator driver");
MODULE_ALIAS("platform:as3711-regulator");
MODULE_LICENSE("GPL v2");
