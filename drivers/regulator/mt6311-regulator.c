/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Henry Chen <henryc.chen@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/mt6311.h>
#include <linux/slab.h>
#include "mt6311-regulator.h"

static const struct regmap_config mt6311_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MT6311_FQMTR_CON4,
};

/* Default limits measured in millivolts and milliamps */
#define MT6311_MIN_UV		600000
#define MT6311_MAX_UV		1393750
#define MT6311_STEP_UV		6250

static const struct regulator_linear_range buck_volt_range[] = {
	REGULATOR_LINEAR_RANGE(MT6311_MIN_UV, 0, 0x7f, MT6311_STEP_UV),
};

static const struct regulator_ops mt6311_buck_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_ops mt6311_ldo_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

#define MT6311_BUCK(_id) \
{\
	.name = #_id,\
	.ops = &mt6311_buck_ops,\
	.of_match = of_match_ptr(#_id),\
	.regulators_node = of_match_ptr("regulators"),\
	.type = REGULATOR_VOLTAGE,\
	.id = MT6311_ID_##_id,\
	.n_voltages = (MT6311_MAX_UV - MT6311_MIN_UV) / MT6311_STEP_UV + 1,\
	.min_uV = MT6311_MIN_UV,\
	.uV_step = MT6311_STEP_UV,\
	.owner = THIS_MODULE,\
	.linear_ranges = buck_volt_range, \
	.n_linear_ranges = ARRAY_SIZE(buck_volt_range), \
	.enable_reg = MT6311_VDVFS11_CON9,\
	.enable_mask = MT6311_PMIC_VDVFS11_EN_MASK,\
	.vsel_reg = MT6311_VDVFS11_CON12,\
	.vsel_mask = MT6311_PMIC_VDVFS11_VOSEL_MASK,\
}

#define MT6311_LDO(_id) \
{\
	.name = #_id,\
	.ops = &mt6311_ldo_ops,\
	.of_match = of_match_ptr(#_id),\
	.regulators_node = of_match_ptr("regulators"),\
	.type = REGULATOR_VOLTAGE,\
	.id = MT6311_ID_##_id,\
	.owner = THIS_MODULE,\
	.enable_reg = MT6311_LDO_CON3,\
	.enable_mask = MT6311_PMIC_RG_VBIASN_EN_MASK,\
}

static const struct regulator_desc mt6311_regulators[] = {
	MT6311_BUCK(VDVFS),
	MT6311_LDO(VBIASN),
};

/*
 * I2C driver interface functions
 */
static int mt6311_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regmap *regmap;
	int i, ret;
	unsigned int data;

	regmap = devm_regmap_init_i2c(i2c, &mt6311_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = regmap_read(regmap, MT6311_SWCID, &data);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read DEVICE_ID reg: %d\n", ret);
		return ret;
	}

	switch (data) {
	case MT6311_E1_CID_CODE:
	case MT6311_E2_CID_CODE:
	case MT6311_E3_CID_CODE:
		break;
	default:
		dev_err(&i2c->dev, "Unsupported device id = 0x%x.\n", data);
		return -ENODEV;
	}

	for (i = 0; i < MT6311_MAX_REGULATORS; i++) {
		config.dev = &i2c->dev;
		config.regmap = regmap;

		rdev = devm_regulator_register(&i2c->dev,
			&mt6311_regulators[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(&i2c->dev,
				"Failed to register MT6311 regulator\n");
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct i2c_device_id mt6311_i2c_id[] = {
	{"mt6311", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, mt6311_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id mt6311_dt_ids[] = {
	{ .compatible = "mediatek,mt6311-regulator",
	  .data = &mt6311_i2c_id[0] },
	{},
};
MODULE_DEVICE_TABLE(of, mt6311_dt_ids);
#endif

static struct i2c_driver mt6311_regulator_driver = {
	.driver = {
		.name = "mt6311",
		.of_match_table = of_match_ptr(mt6311_dt_ids),
	},
	.probe = mt6311_i2c_probe,
	.id_table = mt6311_i2c_id,
};

module_i2c_driver(mt6311_regulator_driver);

MODULE_AUTHOR("Henry Chen <henryc.chen@mediatek.com>");
MODULE_DESCRIPTION("Regulator device driver for Mediatek MT6311");
MODULE_LICENSE("GPL v2");
