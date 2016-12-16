/*
 * pv88080-regulator.c - Regulator device driver for PV88080
 * Copyright (C) 2016  Powerventure Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/regulator/of_regulator.h>
#include "pv88080-regulator.h"

#define PV88080_MAX_REGULATORS	3

/* PV88080 REGULATOR IDs */
enum {
	/* BUCKs */
	PV88080_ID_BUCK1,
	PV88080_ID_BUCK2,
	PV88080_ID_BUCK3,
};

struct pv88080_regulator {
	struct regulator_desc desc;
	/* Current limiting */
	unsigned int n_current_limits;
	const int *current_limits;
	unsigned int limit_mask;
	unsigned int conf;
	unsigned int conf2;
	unsigned int conf5;
};

struct pv88080 {
	struct device *dev;
	struct regmap *regmap;
	struct regulator_dev *rdev[PV88080_MAX_REGULATORS];
};

struct pv88080_buck_voltage {
	int min_uV;
	int max_uV;
	int uV_step;
};

static const struct regmap_config pv88080_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

/* Current limits array (in uA) for BUCK1, BUCK2, BUCK3.
 * Entry indexes corresponds to register values.
 */

static const int pv88080_buck1_limits[] = {
	3230000, 5130000, 6960000, 8790000
};

static const int pv88080_buck23_limits[] = {
	1496000, 2393000, 3291000, 4189000
};

static const struct pv88080_buck_voltage pv88080_buck_vol[2] = {
	{
		.min_uV = 600000,
		.max_uV = 1393750,
		.uV_step = 6250,
	},
	{
		.min_uV = 1400000,
		.max_uV = 2193750,
		.uV_step = 6250,
	},
};

static unsigned int pv88080_buck_get_mode(struct regulator_dev *rdev)
{
	struct pv88080_regulator *info = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret, mode = 0;

	ret = regmap_read(rdev->regmap, info->conf, &data);
	if (ret < 0)
		return ret;

	switch (data & PV88080_BUCK1_MODE_MASK) {
	case PV88080_BUCK_MODE_SYNC:
		mode = REGULATOR_MODE_FAST;
		break;
	case PV88080_BUCK_MODE_AUTO:
		mode = REGULATOR_MODE_NORMAL;
		break;
	case PV88080_BUCK_MODE_SLEEP:
		mode = REGULATOR_MODE_STANDBY;
		break;
	default:
		return -EINVAL;
	}

	return mode;
}

static int pv88080_buck_set_mode(struct regulator_dev *rdev,
					unsigned int mode)
{
	struct pv88080_regulator *info = rdev_get_drvdata(rdev);
	int val = 0;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = PV88080_BUCK_MODE_SYNC;
		break;
	case REGULATOR_MODE_NORMAL:
		val = PV88080_BUCK_MODE_AUTO;
		break;
	case REGULATOR_MODE_STANDBY:
		val = PV88080_BUCK_MODE_SLEEP;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, info->conf,
					PV88080_BUCK1_MODE_MASK, val);
}

static int pv88080_set_current_limit(struct regulator_dev *rdev, int min,
				    int max)
{
	struct pv88080_regulator *info = rdev_get_drvdata(rdev);
	int i;

	/* search for closest to maximum */
	for (i = info->n_current_limits; i >= 0; i--) {
		if (min <= info->current_limits[i]
			&& max >= info->current_limits[i]) {
				return regmap_update_bits(rdev->regmap,
					info->conf,
					info->limit_mask,
					i << PV88080_BUCK1_ILIM_SHIFT);
		}
	}

	return -EINVAL;
}

static int pv88080_get_current_limit(struct regulator_dev *rdev)
{
	struct pv88080_regulator *info = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret;

	ret = regmap_read(rdev->regmap, info->conf, &data);
	if (ret < 0)
		return ret;

	data = (data & info->limit_mask) >> PV88080_BUCK1_ILIM_SHIFT;
	return info->current_limits[data];
}

static struct regulator_ops pv88080_buck_ops = {
	.get_mode = pv88080_buck_get_mode,
	.set_mode = pv88080_buck_set_mode,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_current_limit = pv88080_set_current_limit,
	.get_current_limit = pv88080_get_current_limit,
};

#define PV88080_BUCK(chip, regl_name, min, step, max, limits_array) \
{\
	.desc	=	{\
		.id = chip##_ID_##regl_name,\
		.name = __stringify(chip##_##regl_name),\
		.of_match = of_match_ptr(#regl_name),\
		.regulators_node = of_match_ptr("regulators"),\
		.type = REGULATOR_VOLTAGE,\
		.owner = THIS_MODULE,\
		.ops = &pv88080_buck_ops,\
		.min_uV = min, \
		.uV_step = step, \
		.n_voltages = ((max) - (min))/(step) + 1, \
		.enable_reg = PV88080_REG_##regl_name##_CONF0, \
		.enable_mask = PV88080_##regl_name##_EN, \
		.vsel_reg = PV88080_REG_##regl_name##_CONF0, \
		.vsel_mask = PV88080_V##regl_name##_MASK, \
	},\
	.current_limits = limits_array, \
	.n_current_limits = ARRAY_SIZE(limits_array), \
	.limit_mask = PV88080_##regl_name##_ILIM_MASK, \
	.conf = PV88080_REG_##regl_name##_CONF1, \
	.conf2 = PV88080_REG_##regl_name##_CONF2, \
	.conf5 = PV88080_REG_##regl_name##_CONF5, \
}

static struct pv88080_regulator pv88080_regulator_info[] = {
	PV88080_BUCK(PV88080, BUCK1, 600000, 6250, 1393750,
		pv88080_buck1_limits),
	PV88080_BUCK(PV88080, BUCK2, 600000, 6250, 1393750,
		pv88080_buck23_limits),
	PV88080_BUCK(PV88080, BUCK3, 600000, 6250, 1393750,
		pv88080_buck23_limits),
};

static irqreturn_t pv88080_irq_handler(int irq, void *data)
{
	struct pv88080 *chip = data;
	int i, reg_val, err, ret = IRQ_NONE;

	err = regmap_read(chip->regmap, PV88080_REG_EVENT_A, &reg_val);
	if (err < 0)
		goto error_i2c;

	if (reg_val & PV88080_E_VDD_FLT) {
		for (i = 0; i < PV88080_MAX_REGULATORS; i++) {
			if (chip->rdev[i] != NULL) {
				regulator_notifier_call_chain(chip->rdev[i],
					REGULATOR_EVENT_UNDER_VOLTAGE,
					NULL);
			}
		}

		err = regmap_write(chip->regmap, PV88080_REG_EVENT_A,
			PV88080_E_VDD_FLT);
		if (err < 0)
			goto error_i2c;

		ret = IRQ_HANDLED;
	}

	if (reg_val & PV88080_E_OVER_TEMP) {
		for (i = 0; i < PV88080_MAX_REGULATORS; i++) {
			if (chip->rdev[i] != NULL) {
				regulator_notifier_call_chain(chip->rdev[i],
					REGULATOR_EVENT_OVER_TEMP,
					NULL);
			}
		}

		err = regmap_write(chip->regmap, PV88080_REG_EVENT_A,
			PV88080_E_OVER_TEMP);
		if (err < 0)
			goto error_i2c;

		ret = IRQ_HANDLED;
	}

	return ret;

error_i2c:
	dev_err(chip->dev, "I2C error : %d\n", err);
	return IRQ_NONE;
}

/*
 * I2C driver interface functions
 */
static int pv88080_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct regulator_init_data *init_data = dev_get_platdata(&i2c->dev);
	struct pv88080 *chip;
	struct regulator_config config = { };
	int i, error, ret;
	unsigned int conf2, conf5;

	chip = devm_kzalloc(&i2c->dev, sizeof(struct pv88080), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &i2c->dev;
	chip->regmap = devm_regmap_init_i2c(i2c, &pv88080_regmap_config);
	if (IS_ERR(chip->regmap)) {
		error = PTR_ERR(chip->regmap);
		dev_err(chip->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	i2c_set_clientdata(i2c, chip);

	if (i2c->irq != 0) {
		ret = regmap_write(chip->regmap, PV88080_REG_MASK_A, 0xFF);
		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to mask A reg: %d\n", ret);
			return ret;
		}
		ret = regmap_write(chip->regmap, PV88080_REG_MASK_B, 0xFF);
		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to mask B reg: %d\n", ret);
			return ret;
		}
		ret = regmap_write(chip->regmap, PV88080_REG_MASK_C, 0xFF);
		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to mask C reg: %d\n", ret);
			return ret;
		}

		ret = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL,
					pv88080_irq_handler,
					IRQF_TRIGGER_LOW|IRQF_ONESHOT,
					"pv88080", chip);
		if (ret != 0) {
			dev_err(chip->dev, "Failed to request IRQ: %d\n",
				i2c->irq);
			return ret;
		}

		ret = regmap_update_bits(chip->regmap, PV88080_REG_MASK_A,
			PV88080_M_VDD_FLT | PV88080_M_OVER_TEMP, 0);
		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to update mask reg: %d\n", ret);
			return ret;
		}

	} else {
		dev_warn(chip->dev, "No IRQ configured\n");
	}

	config.dev = chip->dev;
	config.regmap = chip->regmap;

	for (i = 0; i < PV88080_MAX_REGULATORS; i++) {
		if (init_data)
			config.init_data = &init_data[i];

		ret = regmap_read(chip->regmap,
			pv88080_regulator_info[i].conf2, &conf2);
		if (ret < 0)
			return ret;

		conf2 = ((conf2 >> PV88080_BUCK_VDAC_RANGE_SHIFT) &
			PV88080_BUCK_VDAC_RANGE_MASK);

		ret = regmap_read(chip->regmap,
			pv88080_regulator_info[i].conf5, &conf5);
		if (ret < 0)
			return ret;

		conf5 = ((conf5 >> PV88080_BUCK_VRANGE_GAIN_SHIFT) &
			PV88080_BUCK_VRANGE_GAIN_MASK);

		pv88080_regulator_info[i].desc.min_uV =
			pv88080_buck_vol[conf2].min_uV * (conf5+1);
		pv88080_regulator_info[i].desc.uV_step =
			pv88080_buck_vol[conf2].uV_step * (conf5+1);
		pv88080_regulator_info[i].desc.n_voltages =
			((pv88080_buck_vol[conf2].max_uV * (conf5+1))
			- (pv88080_regulator_info[i].desc.min_uV))
			/(pv88080_regulator_info[i].desc.uV_step) + 1;

		config.driver_data = (void *)&pv88080_regulator_info[i];
		chip->rdev[i] = devm_regulator_register(chip->dev,
			&pv88080_regulator_info[i].desc, &config);
		if (IS_ERR(chip->rdev[i])) {
			dev_err(chip->dev,
				"Failed to register PV88080 regulator\n");
			return PTR_ERR(chip->rdev[i]);
		}
	}

	return 0;
}

static const struct i2c_device_id pv88080_i2c_id[] = {
	{"pv88080", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, pv88080_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id pv88080_dt_ids[] = {
	{ .compatible = "pvs,pv88080", .data = &pv88080_i2c_id[0] },
	{},
};
MODULE_DEVICE_TABLE(of, pv88080_dt_ids);
#endif

static struct i2c_driver pv88080_regulator_driver = {
	.driver = {
		.name = "pv88080",
		.of_match_table = of_match_ptr(pv88080_dt_ids),
	},
	.probe = pv88080_i2c_probe,
	.id_table = pv88080_i2c_id,
};

module_i2c_driver(pv88080_regulator_driver);

MODULE_AUTHOR("James Ban <James.Ban.opensource@diasemi.com>");
MODULE_DESCRIPTION("Regulator device driver for Powerventure PV88080");
MODULE_LICENSE("GPL");
