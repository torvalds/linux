/*
 * da9211-regulator.c - Regulator device driver for DA9211
 * Copyright (C) 2014  Dialog Semiconductor Ltd.
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
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/regulator/da9211.h>
#include "da9211-regulator.h"

#define DA9211_BUCK_MODE_SLEEP	1
#define DA9211_BUCK_MODE_SYNC	2
#define DA9211_BUCK_MODE_AUTO	3

/* DA9211 REGULATOR IDs */
#define DA9211_ID_BUCKA	0
#define DA9211_ID_BUCKB	1

struct da9211 {
	struct device *dev;
	struct regmap *regmap;
	struct da9211_pdata *pdata;
	struct regulator_dev *rdev[DA9211_MAX_REGULATORS];
	int num_regulator;
	int chip_irq;
};

static const struct regmap_range_cfg da9211_regmap_range[] = {
	{
		.selector_reg = DA9211_REG_PAGE_CON,
		.selector_mask  = DA9211_REG_PAGE_MASK,
		.selector_shift = DA9211_REG_PAGE_SHIFT,
		.window_start = 0,
		.window_len = 256,
		.range_min = 0,
		.range_max = 2*256,
	},
};

static const struct regmap_config da9211_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 2 * 256,
	.ranges = da9211_regmap_range,
	.num_ranges = ARRAY_SIZE(da9211_regmap_range),
};

/* Default limits measured in millivolts and milliamps */
#define DA9211_MIN_MV		300
#define DA9211_MAX_MV		1570
#define DA9211_STEP_MV		10

/* Current limits for buck (uA) indices corresponds with register values */
static const int da9211_current_limits[] = {
	2000000, 2200000, 2400000, 2600000, 2800000, 3000000, 3200000, 3400000,
	3600000, 3800000, 4000000, 4200000, 4400000, 4600000, 4800000, 5000000
};

static unsigned int da9211_buck_get_mode(struct regulator_dev *rdev)
{
	int id = rdev_get_id(rdev);
	struct da9211 *chip = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret, mode = 0;

	ret = regmap_read(chip->regmap, DA9211_REG_BUCKA_CONF+id, &data);
	if (ret < 0)
		return ret;

	switch (data & 0x03) {
	case DA9211_BUCK_MODE_SYNC:
		mode = REGULATOR_MODE_FAST;
		break;
	case DA9211_BUCK_MODE_AUTO:
		mode = REGULATOR_MODE_NORMAL;
		break;
	case DA9211_BUCK_MODE_SLEEP:
		mode = REGULATOR_MODE_STANDBY;
		break;
	}

	return mode;
}

static int da9211_buck_set_mode(struct regulator_dev *rdev,
					unsigned int mode)
{
	int id = rdev_get_id(rdev);
	struct da9211 *chip = rdev_get_drvdata(rdev);
	int val = 0;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = DA9211_BUCK_MODE_SYNC;
		break;
	case REGULATOR_MODE_NORMAL:
		val = DA9211_BUCK_MODE_AUTO;
		break;
	case REGULATOR_MODE_STANDBY:
		val = DA9211_BUCK_MODE_SLEEP;
		break;
	}

	return regmap_update_bits(chip->regmap, DA9211_REG_BUCKA_CONF+id,
					0x03, val);
}

static int da9211_set_current_limit(struct regulator_dev *rdev, int min,
				    int max)
{
	int id = rdev_get_id(rdev);
	struct da9211 *chip = rdev_get_drvdata(rdev);
	int i;

	/* search for closest to maximum */
	for (i = ARRAY_SIZE(da9211_current_limits)-1; i >= 0; i--) {
		if (min <= da9211_current_limits[i] &&
		    max >= da9211_current_limits[i]) {
				return regmap_update_bits(chip->regmap,
					DA9211_REG_BUCK_ILIM,
					(0x0F << id*4), (i << id*4));
		}
	}

	return -EINVAL;
}

static int da9211_get_current_limit(struct regulator_dev *rdev)
{
	int id = rdev_get_id(rdev);
	struct da9211 *chip = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret;

	ret = regmap_read(chip->regmap, DA9211_REG_BUCK_ILIM, &data);
	if (ret < 0)
		return ret;

	/* select one of 16 values: 0000 (2000mA) to 1111 (5000mA) */
	data = (data >> id*4) & 0x0F;
	return da9211_current_limits[data];
}

static struct regulator_ops da9211_buck_ops = {
	.get_mode = da9211_buck_get_mode,
	.set_mode = da9211_buck_set_mode,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_current_limit = da9211_set_current_limit,
	.get_current_limit = da9211_get_current_limit,
};

#define DA9211_BUCK(_id) \
{\
	.name = #_id,\
	.ops = &da9211_buck_ops,\
	.type = REGULATOR_VOLTAGE,\
	.id = DA9211_ID_##_id,\
	.n_voltages = (DA9211_MAX_MV - DA9211_MIN_MV) / DA9211_STEP_MV + 1,\
	.min_uV = (DA9211_MIN_MV * 1000),\
	.uV_step = (DA9211_STEP_MV * 1000),\
	.enable_reg = DA9211_REG_BUCKA_CONT + DA9211_ID_##_id,\
	.enable_mask = DA9211_BUCKA_EN,\
	.vsel_reg = DA9211_REG_VBUCKA_A + DA9211_ID_##_id * 2,\
	.vsel_mask = DA9211_VBUCK_MASK,\
	.owner = THIS_MODULE,\
}

static struct regulator_desc da9211_regulators[] = {
	DA9211_BUCK(BUCKA),
	DA9211_BUCK(BUCKB),
};

static irqreturn_t da9211_irq_handler(int irq, void *data)
{
	struct da9211 *chip = data;
	int reg_val, err, ret = IRQ_NONE;

	err = regmap_read(chip->regmap, DA9211_REG_EVENT_B, &reg_val);
	if (err < 0)
		goto error_i2c;

	if (reg_val & DA9211_E_OV_CURR_A) {
		regulator_notifier_call_chain(chip->rdev[0],
			REGULATOR_EVENT_OVER_CURRENT,
			rdev_get_drvdata(chip->rdev[0]));

		err = regmap_write(chip->regmap, DA9211_REG_EVENT_B,
			DA9211_E_OV_CURR_A);
		if (err < 0)
			goto error_i2c;

		ret = IRQ_HANDLED;
	}

	if (reg_val & DA9211_E_OV_CURR_B) {
		regulator_notifier_call_chain(chip->rdev[1],
			REGULATOR_EVENT_OVER_CURRENT,
			rdev_get_drvdata(chip->rdev[1]));

		err = regmap_write(chip->regmap, DA9211_REG_EVENT_B,
			DA9211_E_OV_CURR_B);
		if (err < 0)
			goto error_i2c;

		ret = IRQ_HANDLED;
	}

	return ret;

error_i2c:
	dev_err(chip->dev, "I2C error : %d\n", err);
	return IRQ_NONE;
}

static int da9211_regulator_init(struct da9211 *chip)
{
	struct regulator_config config = { };
	int i, ret;
	unsigned int data;

	ret = regmap_read(chip->regmap, DA9211_REG_CONFIG_E, &data);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read CONTROL_E reg: %d\n", ret);
		return ret;
	}

	data &= DA9211_SLAVE_SEL;
	/* If configuration for 1/2 bucks is different between platform data
	 * and the register, driver should exit.
	 */
	if ((chip->pdata->num_buck == 2 && data == 0x40)
		|| (chip->pdata->num_buck == 1 && data == 0x00)) {
		if (data == 0)
			chip->num_regulator = 1;
		else
			chip->num_regulator = 2;
	} else {
		dev_err(chip->dev, "Configuration is mismatched\n");
		return -EINVAL;
	}

	for (i = 0; i < chip->num_regulator; i++) {
		if (chip->pdata)
			config.init_data =
				&(chip->pdata->init_data[i]);

		config.dev = chip->dev;
		config.driver_data = chip;
		config.regmap = chip->regmap;

		chip->rdev[i] = devm_regulator_register(chip->dev,
			&da9211_regulators[i], &config);
		if (IS_ERR(chip->rdev[i])) {
			dev_err(chip->dev,
				"Failed to register DA9211 regulator\n");
			return PTR_ERR(chip->rdev[i]);
		}

		if (chip->chip_irq != 0) {
			ret = regmap_update_bits(chip->regmap,
				DA9211_REG_MASK_B, DA9211_M_OV_CURR_A << i, 1);
			if (ret < 0) {
				dev_err(chip->dev,
					"Failed to update mask reg: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}
/*
 * I2C driver interface functions
 */
static int da9211_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct da9211 *chip;
	int error, ret;

	chip = devm_kzalloc(&i2c->dev, sizeof(struct da9211), GFP_KERNEL);

	chip->dev = &i2c->dev;
	chip->regmap = devm_regmap_init_i2c(i2c, &da9211_regmap_config);
	if (IS_ERR(chip->regmap)) {
		error = PTR_ERR(chip->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	i2c_set_clientdata(i2c, chip);

	chip->pdata = i2c->dev.platform_data;
	if (!chip->pdata) {
		dev_err(&i2c->dev, "No platform init data supplied\n");
		return -ENODEV;
	}

	chip->chip_irq = i2c->irq;

	if (chip->chip_irq != 0) {
		ret = devm_request_threaded_irq(chip->dev, chip->chip_irq, NULL,
					da9211_irq_handler,
					IRQF_TRIGGER_LOW|IRQF_ONESHOT,
					"da9211", chip);
		if (ret != 0) {
			dev_err(chip->dev, "Failed to request IRQ: %d\n",
				chip->chip_irq);
			return ret;
		}
	} else {
		dev_warn(chip->dev, "No IRQ configured\n");
	}

	ret = da9211_regulator_init(chip);

	if (ret < 0)
		dev_err(&i2c->dev, "Failed to initialize regulator: %d\n", ret);

	return ret;
}

static const struct i2c_device_id da9211_i2c_id[] = {
	{"da9211", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, da9211_i2c_id);

static struct i2c_driver da9211_regulator_driver = {
	.driver = {
		.name = "da9211",
		.owner = THIS_MODULE,
	},
	.probe = da9211_i2c_probe,
	.id_table = da9211_i2c_id,
};

module_i2c_driver(da9211_regulator_driver);

MODULE_AUTHOR("James Ban <James.Ban.opensource@diasemi.com>");
MODULE_DESCRIPTION("Regulator device driver for Dialog DA9211");
MODULE_LICENSE("GPL v2");
