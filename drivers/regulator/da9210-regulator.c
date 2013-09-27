/*
 * da9210-regulator.c - Regulator device driver for DA9210
 * Copyright (C) 2013  Dialog Semiconductor Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>

#include "da9210-regulator.h"

struct da9210 {
	struct regulator_dev *rdev;
	struct regmap *regmap;
};

static const struct regmap_config da9210_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int da9210_set_current_limit(struct regulator_dev *rdev, int min_uA,
				    int max_uA);
static int da9210_get_current_limit(struct regulator_dev *rdev);

static struct regulator_ops da9210_buck_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_current_limit = da9210_set_current_limit,
	.get_current_limit = da9210_get_current_limit,
};

/* Default limits measured in millivolts and milliamps */
#define DA9210_MIN_MV		300
#define DA9210_MAX_MV		1570
#define DA9210_STEP_MV		10

/* Current limits for buck (uA) indices corresponds with register values */
static const int da9210_buck_limits[] = {
	1600000, 1800000, 2000000, 2200000, 2400000, 2600000, 2800000, 3000000,
	3200000, 3400000, 3600000, 3800000, 4000000, 4200000, 4400000, 4600000
};

static const struct regulator_desc da9210_reg = {
	.name = "DA9210",
	.id = 0,
	.ops = &da9210_buck_ops,
	.type = REGULATOR_VOLTAGE,
	.n_voltages = ((DA9210_MAX_MV - DA9210_MIN_MV) / DA9210_STEP_MV) + 1,
	.min_uV = (DA9210_MIN_MV * 1000),
	.uV_step = (DA9210_STEP_MV * 1000),
	.vsel_reg = DA9210_REG_VBUCK_A,
	.vsel_mask = DA9210_VBUCK_MASK,
	.enable_reg = DA9210_REG_BUCK_CONT,
	.enable_mask = DA9210_BUCK_EN,
	.owner = THIS_MODULE,
};

static int da9210_set_current_limit(struct regulator_dev *rdev, int min_uA,
				    int max_uA)
{
	struct da9210 *chip = rdev_get_drvdata(rdev);
	unsigned int sel;
	int i;

	/* search for closest to maximum */
	for (i = ARRAY_SIZE(da9210_buck_limits)-1; i >= 0; i--) {
		if (min_uA <= da9210_buck_limits[i] &&
		    max_uA >= da9210_buck_limits[i]) {
			sel = i;
			sel = sel << DA9210_BUCK_ILIM_SHIFT;
			return regmap_update_bits(chip->regmap,
						  DA9210_REG_BUCK_ILIM,
						  DA9210_BUCK_ILIM_MASK, sel);
		}
	}

	return -EINVAL;
}

static int da9210_get_current_limit(struct regulator_dev *rdev)
{
	struct da9210 *chip = rdev_get_drvdata(rdev);
	unsigned int data;
	unsigned int sel;
	int ret;

	ret = regmap_read(chip->regmap, DA9210_REG_BUCK_ILIM, &data);
	if (ret < 0)
		return ret;

	/* select one of 16 values: 0000 (1600mA) to 1111 (4600mA) */
	sel = (data & DA9210_BUCK_ILIM_MASK) >> DA9210_BUCK_ILIM_SHIFT;

	return da9210_buck_limits[sel];
}

/*
 * I2C driver interface functions
 */
static int da9210_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct da9210 *chip;
	struct da9210_pdata *pdata = i2c->dev.platform_data;
	struct regulator_dev *rdev = NULL;
	struct regulator_config config = { };
	int error;

	chip = devm_kzalloc(&i2c->dev, sizeof(struct da9210), GFP_KERNEL);
	if (NULL == chip) {
		dev_err(&i2c->dev,
			"Cannot kzalloc memory for regulator structure\n");
		return -ENOMEM;
	}

	chip->regmap = devm_regmap_init_i2c(i2c, &da9210_regmap_config);
	if (IS_ERR(chip->regmap)) {
		error = PTR_ERR(chip->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	config.dev = &i2c->dev;
	if (pdata)
		config.init_data = &pdata->da9210_constraints;
	config.driver_data = chip;
	config.regmap = chip->regmap;

	rdev = regulator_register(&da9210_reg, &config);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "Failed to register DA9210 regulator\n");
		return PTR_ERR(rdev);
	}

	chip->rdev = rdev;

	i2c_set_clientdata(i2c, chip);

	return 0;
}

static int da9210_i2c_remove(struct i2c_client *i2c)
{
	struct da9210 *chip = i2c_get_clientdata(i2c);
	regulator_unregister(chip->rdev);
	return 0;
}

static const struct i2c_device_id da9210_i2c_id[] = {
	{"da9210", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, da9210_i2c_id);

static struct i2c_driver da9210_regulator_driver = {
	.driver = {
		.name = "da9210",
		.owner = THIS_MODULE,
	},
	.probe = da9210_i2c_probe,
	.remove = da9210_i2c_remove,
	.id_table = da9210_i2c_id,
};

module_i2c_driver(da9210_regulator_driver);

MODULE_AUTHOR("S Twiss <stwiss.opensource@diasemi.com>");
MODULE_DESCRIPTION("Regulator device driver for Dialog DA9210");
MODULE_LICENSE("GPL v2");
