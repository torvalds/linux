/*
 * max8907-regulator.c -- support regulators in max8907
 *
 * Copyright (C) 2010 Gyungoh Yoo <jack.yoo@maxim-ic.com>
 * Copyright (C) 2010-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * Portions based on drivers/regulator/tps65910-regulator.c,
 *     Copyright 2010 Texas Instruments Inc.
 *     Author: Graeme Gregory <gg@slimlogic.co.uk>
 *     Author: Jorge Eduardo Candelaria <jedu@slimlogic.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max8907.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define MAX8907_II2RR_VERSION_MASK	0xF0
#define MAX8907_II2RR_VERSION_REV_A	0x00
#define MAX8907_II2RR_VERSION_REV_B	0x10
#define MAX8907_II2RR_VERSION_REV_C	0x30

struct max8907_regulator {
	struct regulator_desc desc[MAX8907_NUM_REGULATORS];
};

#define REG_MBATT() \
	[MAX8907_MBATT] = { \
		.name = "MBATT", \
		.supply_name = "mbatt", \
		.id = MAX8907_MBATT, \
		.ops = &max8907_mbatt_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
	}

#define REG_LDO(ids, supply, base, min, max, step) \
	[MAX8907_##ids] = { \
		.name = #ids, \
		.supply_name = supply, \
		.id = MAX8907_##ids, \
		.n_voltages = ((max) - (min)) / (step) + 1, \
		.ops = &max8907_ldo_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		.min_uV = (min), \
		.uV_step = (step), \
		.vsel_reg = (base) + MAX8907_VOUT, \
		.vsel_mask = 0x3f, \
		.enable_reg = (base) + MAX8907_CTL, \
		.enable_mask = MAX8907_MASK_LDO_EN, \
	}

#define REG_FIXED(ids, supply, voltage) \
	[MAX8907_##ids] = { \
		.name = #ids, \
		.supply_name = supply, \
		.id = MAX8907_##ids, \
		.n_voltages = 1, \
		.ops = &max8907_fixed_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		.min_uV = (voltage), \
	}

#define REG_OUT5V(ids, supply, base, voltage) \
	[MAX8907_##ids] = { \
		.name = #ids, \
		.supply_name = supply, \
		.id = MAX8907_##ids, \
		.n_voltages = 1, \
		.ops = &max8907_out5v_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		.min_uV = (voltage), \
		.enable_reg = (base), \
		.enable_mask = MAX8907_MASK_OUT5V_EN, \
	}

#define REG_BBAT(ids, supply, base, min, max, step) \
	[MAX8907_##ids] = { \
		.name = #ids, \
		.supply_name = supply, \
		.id = MAX8907_##ids, \
		.n_voltages = ((max) - (min)) / (step) + 1, \
		.ops = &max8907_bbat_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		.min_uV = (min), \
		.uV_step = (step), \
		.vsel_reg = (base), \
		.vsel_mask = MAX8907_MASK_VBBATTCV, \
	}

#define LDO_750_50(id, supply, base) REG_LDO(id, supply, (base), \
			750000, 3900000, 50000)
#define LDO_650_25(id, supply, base) REG_LDO(id, supply, (base), \
			650000, 2225000, 25000)

static struct regulator_ops max8907_mbatt_ops = {
};

static struct regulator_ops max8907_ldo_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static struct regulator_ops max8907_ldo_hwctl_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static struct regulator_ops max8907_fixed_ops = {
	.list_voltage = regulator_list_voltage_linear,
};

static struct regulator_ops max8907_out5v_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static struct regulator_ops max8907_out5v_hwctl_ops = {
	.list_voltage = regulator_list_voltage_linear,
};

static struct regulator_ops max8907_bbat_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static struct regulator_desc max8907_regulators[] = {
	REG_MBATT(),
	REG_LDO(SD1, "in-v1", MAX8907_REG_SDCTL1, 650000, 2225000, 25000),
	REG_LDO(SD2, "in-v2", MAX8907_REG_SDCTL2, 637500, 1425000, 12500),
	REG_LDO(SD3, "in-v3", MAX8907_REG_SDCTL3, 750000, 3900000, 50000),
	LDO_750_50(LDO1, "in1", MAX8907_REG_LDOCTL1),
	LDO_650_25(LDO2, "in2", MAX8907_REG_LDOCTL2),
	LDO_650_25(LDO3, "in3", MAX8907_REG_LDOCTL3),
	LDO_750_50(LDO4, "in4", MAX8907_REG_LDOCTL4),
	LDO_750_50(LDO5, "in5", MAX8907_REG_LDOCTL5),
	LDO_750_50(LDO6, "in6", MAX8907_REG_LDOCTL6),
	LDO_750_50(LDO7, "in7", MAX8907_REG_LDOCTL7),
	LDO_750_50(LDO8, "in8", MAX8907_REG_LDOCTL8),
	LDO_750_50(LDO9, "in9", MAX8907_REG_LDOCTL9),
	LDO_750_50(LDO10, "in10", MAX8907_REG_LDOCTL10),
	LDO_750_50(LDO11, "in11", MAX8907_REG_LDOCTL11),
	LDO_750_50(LDO12, "in12", MAX8907_REG_LDOCTL12),
	LDO_750_50(LDO13, "in13", MAX8907_REG_LDOCTL13),
	LDO_750_50(LDO14, "in14", MAX8907_REG_LDOCTL14),
	LDO_750_50(LDO15, "in15", MAX8907_REG_LDOCTL15),
	LDO_750_50(LDO16, "in16", MAX8907_REG_LDOCTL16),
	LDO_650_25(LDO17, "in17", MAX8907_REG_LDOCTL17),
	LDO_650_25(LDO18, "in18", MAX8907_REG_LDOCTL18),
	LDO_750_50(LDO19, "in19", MAX8907_REG_LDOCTL19),
	LDO_750_50(LDO20, "in20", MAX8907_REG_LDOCTL20),
	REG_OUT5V(OUT5V, "mbatt", MAX8907_REG_OUT5VEN, 5000000),
	REG_OUT5V(OUT33V, "mbatt",  MAX8907_REG_OUT33VEN, 3300000),
	REG_BBAT(BBAT, "MBATT", MAX8907_REG_BBAT_CNFG,
						2400000, 3000000, 200000),
	REG_FIXED(SDBY, "MBATT", 1200000),
	REG_FIXED(VRTC, "MBATT", 3300000),
};

#ifdef CONFIG_OF

#define MATCH(_name, _id) \
	[MAX8907_##_id] = { \
		.name = #_name, \
		.driver_data = (void *)&max8907_regulators[MAX8907_##_id], \
	}

static struct of_regulator_match max8907_matches[] = {
	MATCH(mbatt, MBATT),
	MATCH(sd1, SD1),
	MATCH(sd2, SD2),
	MATCH(sd3, SD3),
	MATCH(ldo1, LDO1),
	MATCH(ldo2, LDO2),
	MATCH(ldo3, LDO3),
	MATCH(ldo4, LDO4),
	MATCH(ldo5, LDO5),
	MATCH(ldo6, LDO6),
	MATCH(ldo7, LDO7),
	MATCH(ldo8, LDO8),
	MATCH(ldo9, LDO9),
	MATCH(ldo10, LDO10),
	MATCH(ldo11, LDO11),
	MATCH(ldo12, LDO12),
	MATCH(ldo13, LDO13),
	MATCH(ldo14, LDO14),
	MATCH(ldo15, LDO15),
	MATCH(ldo16, LDO16),
	MATCH(ldo17, LDO17),
	MATCH(ldo18, LDO18),
	MATCH(ldo19, LDO19),
	MATCH(ldo20, LDO20),
	MATCH(out5v, OUT5V),
	MATCH(out33v, OUT33V),
	MATCH(bbat, BBAT),
	MATCH(sdby, SDBY),
	MATCH(vrtc, VRTC),
};

static int max8907_regulator_parse_dt(struct platform_device *pdev)
{
	struct device_node *np, *regulators;
	int ret;

	np = pdev->dev.parent->of_node;
	if (!np)
		return 0;

	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_err(&pdev->dev, "regulators node not found\n");
		return -EINVAL;
	}

	ret = of_regulator_match(&pdev->dev, regulators, max8907_matches,
				 ARRAY_SIZE(max8907_matches));
	of_node_put(regulators);
	if (ret < 0) {
		dev_err(&pdev->dev, "Error parsing regulator init data: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static inline struct regulator_init_data *match_init_data(int index)
{
	return max8907_matches[index].init_data;
}

static inline struct device_node *match_of_node(int index)
{
	return max8907_matches[index].of_node;
}
#else
static int max8907_regulator_parse_dt(struct platform_device *pdev)
{
	return 0;
}

static inline struct regulator_init_data *match_init_data(int index)
{
	return NULL;
}

static inline struct device_node *match_of_node(int index)
{
	return NULL;
}
#endif

static int max8907_regulator_probe(struct platform_device *pdev)
{
	struct max8907 *max8907 = dev_get_drvdata(pdev->dev.parent);
	struct max8907_platform_data *pdata = dev_get_platdata(max8907->dev);
	int ret;
	struct max8907_regulator *pmic;
	unsigned int val;
	int i;
	struct regulator_config config = {};
	struct regulator_init_data *idata;
	const char *mbatt_rail_name = NULL;

	ret = max8907_regulator_parse_dt(pdev);
	if (ret)
		return ret;

	pmic = devm_kzalloc(&pdev->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	platform_set_drvdata(pdev, pmic);

	memcpy(pmic->desc, max8907_regulators, sizeof(pmic->desc));

	/* Backwards compatibility with MAX8907B; SD1 uses different voltages */
	regmap_read(max8907->regmap_gen, MAX8907_REG_II2RR, &val);
	if ((val & MAX8907_II2RR_VERSION_MASK) ==
	    MAX8907_II2RR_VERSION_REV_B) {
		pmic->desc[MAX8907_SD1].min_uV = 637500;
		pmic->desc[MAX8907_SD1].uV_step = 12500;
		pmic->desc[MAX8907_SD1].n_voltages =
						(1425000 - 637500) / 12500 + 1;
	}

	for (i = 0; i < MAX8907_NUM_REGULATORS; i++) {
		struct regulator_dev *rdev;

		config.dev = pdev->dev.parent;
		if (pdata)
			idata = pdata->init_data[i];
		else
			idata = match_init_data(i);
		config.init_data = idata;
		config.driver_data = pmic;
		config.regmap = max8907->regmap_gen;
		config.of_node = match_of_node(i);

		switch (pmic->desc[i].id) {
		case MAX8907_MBATT:
			if (idata && idata->constraints.name)
				mbatt_rail_name = idata->constraints.name;
			else
				mbatt_rail_name = pmic->desc[i].name;
			break;
		case MAX8907_BBAT:
		case MAX8907_SDBY:
		case MAX8907_VRTC:
			idata->supply_regulator = mbatt_rail_name;
			break;
		}

		if (pmic->desc[i].ops == &max8907_ldo_ops) {
			regmap_read(config.regmap, pmic->desc[i].enable_reg,
				    &val);
			if ((val & MAX8907_MASK_LDO_SEQ) !=
			    MAX8907_MASK_LDO_SEQ)
				pmic->desc[i].ops = &max8907_ldo_hwctl_ops;
		} else if (pmic->desc[i].ops == &max8907_out5v_ops) {
			regmap_read(config.regmap, pmic->desc[i].enable_reg,
				    &val);
			if ((val & (MAX8907_MASK_OUT5V_VINEN |
						MAX8907_MASK_OUT5V_ENSRC)) !=
			    MAX8907_MASK_OUT5V_ENSRC)
				pmic->desc[i].ops = &max8907_out5v_hwctl_ops;
		}

		rdev = devm_regulator_register(&pdev->dev,
						&pmic->desc[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,
				"failed to register %s regulator\n",
				pmic->desc[i].name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static struct platform_driver max8907_regulator_driver = {
	.driver = {
		   .name = "max8907-regulator",
		   },
	.probe = max8907_regulator_probe,
};

static int __init max8907_regulator_init(void)
{
	return platform_driver_register(&max8907_regulator_driver);
}

subsys_initcall(max8907_regulator_init);

static void __exit max8907_reg_exit(void)
{
	platform_driver_unregister(&max8907_regulator_driver);
}

module_exit(max8907_reg_exit);

MODULE_DESCRIPTION("MAX8907 regulator driver");
MODULE_AUTHOR("Gyungoh Yoo <jack.yoo@maxim-ic.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:max8907-regulator");
