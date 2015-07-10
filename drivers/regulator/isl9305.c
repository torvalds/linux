/*
 * isl9305 - Intersil ISL9305 DCDC regulator
 *
 * Copyright 2014 Linaro Ltd
 *
 * Author: Mark Brown <broonie@kernel.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/platform_data/isl9305.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

/*
 * Registers
 */
#define ISL9305_DCD1OUT          0x0
#define ISL9305_DCD2OUT          0x1
#define ISL9305_LDO1OUT          0x2
#define ISL9305_LDO2OUT          0x3
#define ISL9305_DCD_PARAMETER    0x4
#define ISL9305_SYSTEM_PARAMETER 0x5
#define ISL9305_DCD_SRCTL        0x6

#define ISL9305_MAX_REG ISL9305_DCD_SRCTL

/*
 * DCD_PARAMETER
 */
#define ISL9305_DCD_PHASE   0x40
#define ISL9305_DCD2_ULTRA  0x20
#define ISL9305_DCD1_ULTRA  0x10
#define ISL9305_DCD2_BLD    0x08
#define ISL9305_DCD1_BLD    0x04
#define ISL9305_DCD2_MODE   0x02
#define ISL9305_DCD1_MODE   0x01

/*
 * SYSTEM_PARAMETER
 */
#define ISL9305_I2C_EN      0x40
#define ISL9305_DCDPOR_MASK 0x30
#define ISL9305_LDO2_EN     0x08
#define ISL9305_LDO1_EN     0x04
#define ISL9305_DCD2_EN     0x02
#define ISL9305_DCD1_EN     0x01

/*
 * DCD_SRCTL
 */
#define ISL9305_DCD2SR_MASK 0xc0
#define ISL9305_DCD1SR_MASK 0x07

static const struct regulator_ops isl9305_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
};

static const struct regulator_desc isl9305_regulators[] = {
	[ISL9305_DCD1] = {
		.name =		"DCD1",
		.of_match =	of_match_ptr("dcd1"),
		.regulators_node = of_match_ptr("regulators"),
		.n_voltages =	0x70,
		.min_uV =	825000,
		.uV_step =	25000,
		.vsel_reg =	ISL9305_DCD1OUT,
		.vsel_mask =	0x7f,
		.enable_reg =	ISL9305_SYSTEM_PARAMETER,
		.enable_mask =	ISL9305_DCD1_EN,
		.supply_name =	"VINDCD1",
		.ops =		&isl9305_ops,
	},
	[ISL9305_DCD2] = {
		.name =		"DCD2",
		.of_match =	of_match_ptr("dcd2"),
		.regulators_node = of_match_ptr("regulators"),
		.n_voltages =	0x70,
		.min_uV =	825000,
		.uV_step =	25000,
		.vsel_reg =	ISL9305_DCD2OUT,
		.vsel_mask =	0x7f,
		.enable_reg =	ISL9305_SYSTEM_PARAMETER,
		.enable_mask =	ISL9305_DCD2_EN,
		.supply_name =	"VINDCD2",
		.ops =		&isl9305_ops,
	},
	[ISL9305_LDO1] = {
		.name =		"LDO1",
		.of_match =	of_match_ptr("ldo1"),
		.regulators_node = of_match_ptr("regulators"),
		.n_voltages =	0x37,
		.min_uV =	900000,
		.uV_step =	50000,
		.vsel_reg =	ISL9305_LDO1OUT,
		.vsel_mask =	0x3f,
		.enable_reg =	ISL9305_SYSTEM_PARAMETER,
		.enable_mask =	ISL9305_LDO1_EN,
		.supply_name =	"VINLDO1",
		.ops =		&isl9305_ops,
	},
	[ISL9305_LDO2] = {
		.name =		"LDO2",
		.of_match =	of_match_ptr("ldo2"),
		.regulators_node = of_match_ptr("regulators"),
		.n_voltages =	0x37,
		.min_uV =	900000,
		.uV_step =	50000,
		.vsel_reg =	ISL9305_LDO2OUT,
		.vsel_mask =	0x3f,
		.enable_reg =	ISL9305_SYSTEM_PARAMETER,
		.enable_mask =	ISL9305_LDO2_EN,
		.supply_name =	"VINLDO2",
		.ops =		&isl9305_ops,
	},
};

static const struct regmap_config isl9305_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = ISL9305_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
};

static int isl9305_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct regulator_config config = { };
	struct isl9305_pdata *pdata = i2c->dev.platform_data;
	struct regulator_dev *rdev;
	struct regmap *regmap;
	int i, ret;

	regmap = devm_regmap_init_i2c(i2c, &isl9305_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to create regmap: %d\n", ret);
		return ret;
	}

	config.dev = &i2c->dev;

	for (i = 0; i < ARRAY_SIZE(isl9305_regulators); i++) {
		if (pdata)
			config.init_data = pdata->init_data[i];
		else
			config.init_data = NULL;

		rdev = devm_regulator_register(&i2c->dev,
					       &isl9305_regulators[i],
					       &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(&i2c->dev, "Failed to register %s: %d\n",
				isl9305_regulators[i].name, ret);
			return ret;
		}
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id isl9305_dt_ids[] = {
	{ .compatible = "isl,isl9305" }, /* for backward compat., don't use */
	{ .compatible = "isil,isl9305" },
	{ .compatible = "isl,isl9305h" }, /* for backward compat., don't use */
	{ .compatible = "isil,isl9305h" },
	{},
};
#endif

static const struct i2c_device_id isl9305_i2c_id[] = {
	{ "isl9305", },
	{ "isl9305h", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isl9305_i2c_id);

static struct i2c_driver isl9305_regulator_driver = {
	.driver = {
		.name = "isl9305",
		.of_match_table	= of_match_ptr(isl9305_dt_ids),
	},
	.probe = isl9305_i2c_probe,
	.id_table = isl9305_i2c_id,
};

module_i2c_driver(isl9305_regulator_driver);

MODULE_AUTHOR("Mark Brown");
MODULE_DESCRIPTION("Intersil ISL9305 DCDC regulator");
MODULE_LICENSE("GPL");
