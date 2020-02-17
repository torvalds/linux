/*
 * pv88060-regulator.c - Regulator device driver for PV88060
 * Copyright (C) 2015  Powerventure Semiconductor Ltd.
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
#include "pv88060-regulator.h"

#define PV88060_MAX_REGULATORS	14

/* PV88060 REGULATOR IDs */
enum {
	/* BUCKs */
	PV88060_ID_BUCK1,

	/* LDOs */
	PV88060_ID_LDO1,
	PV88060_ID_LDO2,
	PV88060_ID_LDO3,
	PV88060_ID_LDO4,
	PV88060_ID_LDO5,
	PV88060_ID_LDO6,
	PV88060_ID_LDO7,

	/* SWTs */
	PV88060_ID_SW1,
	PV88060_ID_SW2,
	PV88060_ID_SW3,
	PV88060_ID_SW4,
	PV88060_ID_SW5,
	PV88060_ID_SW6,
};

struct pv88060_regulator {
	struct regulator_desc desc;
	/* Current limiting */
	unsigned	n_current_limits;
	const int	*current_limits;
	unsigned int limit_mask;
	unsigned int conf;		/* buck configuration register */
};

struct pv88060 {
	struct device *dev;
	struct regmap *regmap;
	struct regulator_dev *rdev[PV88060_MAX_REGULATORS];
};

static const struct regmap_config pv88060_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

/* Current limits array (in uA) for BUCK1
 * Entry indexes corresponds to register values.
 */

static const int pv88060_buck1_limits[] = {
	1496000, 2393000, 3291000, 4189000
};

static unsigned int pv88060_buck_get_mode(struct regulator_dev *rdev)
{
	struct pv88060_regulator *info = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret, mode = 0;

	ret = regmap_read(rdev->regmap, info->conf, &data);
	if (ret < 0)
		return ret;

	switch (data & PV88060_BUCK_MODE_MASK) {
	case PV88060_BUCK_MODE_SYNC:
		mode = REGULATOR_MODE_FAST;
		break;
	case PV88060_BUCK_MODE_AUTO:
		mode = REGULATOR_MODE_NORMAL;
		break;
	case PV88060_BUCK_MODE_SLEEP:
		mode = REGULATOR_MODE_STANDBY;
		break;
	}

	return mode;
}

static int pv88060_buck_set_mode(struct regulator_dev *rdev,
					unsigned int mode)
{
	struct pv88060_regulator *info = rdev_get_drvdata(rdev);
	int val = 0;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = PV88060_BUCK_MODE_SYNC;
		break;
	case REGULATOR_MODE_NORMAL:
		val = PV88060_BUCK_MODE_AUTO;
		break;
	case REGULATOR_MODE_STANDBY:
		val = PV88060_BUCK_MODE_SLEEP;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, info->conf,
					PV88060_BUCK_MODE_MASK, val);
}

static int pv88060_set_current_limit(struct regulator_dev *rdev, int min,
				    int max)
{
	struct pv88060_regulator *info = rdev_get_drvdata(rdev);
	int i;

	/* search for closest to maximum */
	for (i = info->n_current_limits - 1; i >= 0; i--) {
		if (min <= info->current_limits[i]
			&& max >= info->current_limits[i]) {
			return regmap_update_bits(rdev->regmap,
				info->conf,
				info->limit_mask,
				i << PV88060_BUCK_ILIM_SHIFT);
		}
	}

	return -EINVAL;
}

static int pv88060_get_current_limit(struct regulator_dev *rdev)
{
	struct pv88060_regulator *info = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret;

	ret = regmap_read(rdev->regmap, info->conf, &data);
	if (ret < 0)
		return ret;

	data = (data & info->limit_mask) >> PV88060_BUCK_ILIM_SHIFT;
	return info->current_limits[data];
}

static const struct regulator_ops pv88060_buck_ops = {
	.get_mode = pv88060_buck_get_mode,
	.set_mode = pv88060_buck_set_mode,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_current_limit = pv88060_set_current_limit,
	.get_current_limit = pv88060_get_current_limit,
};

static const struct regulator_ops pv88060_ldo_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
};

#define PV88060_BUCK(chip, regl_name, min, step, max, limits_array) \
{\
	.desc	=	{\
		.id = chip##_ID_##regl_name,\
		.name = __stringify(chip##_##regl_name),\
		.of_match = of_match_ptr(#regl_name),\
		.regulators_node = of_match_ptr("regulators"),\
		.type = REGULATOR_VOLTAGE,\
		.owner = THIS_MODULE,\
		.ops = &pv88060_buck_ops,\
		.min_uV = min,\
		.uV_step = step,\
		.n_voltages = ((max) - (min))/(step) + 1,\
		.enable_reg = PV88060_REG_##regl_name##_CONF0,\
		.enable_mask = PV88060_BUCK_EN, \
		.vsel_reg = PV88060_REG_##regl_name##_CONF0,\
		.vsel_mask = PV88060_VBUCK_MASK,\
	},\
	.current_limits = limits_array,\
	.n_current_limits = ARRAY_SIZE(limits_array),\
	.limit_mask = PV88060_BUCK_ILIM_MASK, \
	.conf = PV88060_REG_##regl_name##_CONF1,\
}

#define PV88060_LDO(chip, regl_name, min, step, max) \
{\
	.desc	=	{\
		.id = chip##_ID_##regl_name,\
		.name = __stringify(chip##_##regl_name),\
		.of_match = of_match_ptr(#regl_name),\
		.regulators_node = of_match_ptr("regulators"),\
		.type = REGULATOR_VOLTAGE,\
		.owner = THIS_MODULE,\
		.ops = &pv88060_ldo_ops,\
		.min_uV = min, \
		.uV_step = step, \
		.n_voltages = (step) ? ((max - min) / step + 1) : 1, \
		.enable_reg = PV88060_REG_##regl_name##_CONF, \
		.enable_mask = PV88060_LDO_EN, \
		.vsel_reg = PV88060_REG_##regl_name##_CONF, \
		.vsel_mask = PV88060_VLDO_MASK, \
	},\
}

#define PV88060_SW(chip, regl_name, max) \
{\
	.desc	=	{\
		.id = chip##_ID_##regl_name,\
		.name = __stringify(chip##_##regl_name),\
		.of_match = of_match_ptr(#regl_name),\
		.regulators_node = of_match_ptr("regulators"),\
		.type = REGULATOR_VOLTAGE,\
		.owner = THIS_MODULE,\
		.ops = &pv88060_ldo_ops,\
		.min_uV = max,\
		.uV_step = 0,\
		.n_voltages = 1,\
		.enable_reg = PV88060_REG_##regl_name##_CONF,\
		.enable_mask = PV88060_SW_EN,\
	},\
}

static const struct pv88060_regulator pv88060_regulator_info[] = {
	PV88060_BUCK(PV88060, BUCK1, 2800000, 12500, 4387500,
		pv88060_buck1_limits),
	PV88060_LDO(PV88060, LDO1, 1200000, 50000, 3350000),
	PV88060_LDO(PV88060, LDO2, 1200000, 50000, 3350000),
	PV88060_LDO(PV88060, LDO3, 1200000, 50000, 3350000),
	PV88060_LDO(PV88060, LDO4, 1200000, 50000, 3350000),
	PV88060_LDO(PV88060, LDO5, 1200000, 50000, 3350000),
	PV88060_LDO(PV88060, LDO6, 1200000, 50000, 3350000),
	PV88060_LDO(PV88060, LDO7, 1200000, 50000, 3350000),
	PV88060_SW(PV88060, SW1, 5000000),
	PV88060_SW(PV88060, SW2, 5000000),
	PV88060_SW(PV88060, SW3, 5000000),
	PV88060_SW(PV88060, SW4, 5000000),
	PV88060_SW(PV88060, SW5, 5000000),
	PV88060_SW(PV88060, SW6, 5000000),
};

static irqreturn_t pv88060_irq_handler(int irq, void *data)
{
	struct pv88060 *chip = data;
	int i, reg_val, err, ret = IRQ_NONE;

	err = regmap_read(chip->regmap, PV88060_REG_EVENT_A, &reg_val);
	if (err < 0)
		goto error_i2c;

	if (reg_val & PV88060_E_VDD_FLT) {
		for (i = 0; i < PV88060_MAX_REGULATORS; i++) {
			if (chip->rdev[i] != NULL) {
				regulator_notifier_call_chain(chip->rdev[i],
					REGULATOR_EVENT_UNDER_VOLTAGE,
					NULL);
			}
		}

		err = regmap_write(chip->regmap, PV88060_REG_EVENT_A,
			PV88060_E_VDD_FLT);
		if (err < 0)
			goto error_i2c;

		ret = IRQ_HANDLED;
	}

	if (reg_val & PV88060_E_OVER_TEMP) {
		for (i = 0; i < PV88060_MAX_REGULATORS; i++) {
			if (chip->rdev[i] != NULL) {
				regulator_notifier_call_chain(chip->rdev[i],
					REGULATOR_EVENT_OVER_TEMP,
					NULL);
			}
		}

		err = regmap_write(chip->regmap, PV88060_REG_EVENT_A,
			PV88060_E_OVER_TEMP);
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
static int pv88060_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct regulator_init_data *init_data = dev_get_platdata(&i2c->dev);
	struct pv88060 *chip;
	struct regulator_config config = { };
	int error, i, ret = 0;

	chip = devm_kzalloc(&i2c->dev, sizeof(struct pv88060), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &i2c->dev;
	chip->regmap = devm_regmap_init_i2c(i2c, &pv88060_regmap_config);
	if (IS_ERR(chip->regmap)) {
		error = PTR_ERR(chip->regmap);
		dev_err(chip->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	i2c_set_clientdata(i2c, chip);

	if (i2c->irq != 0) {
		ret = regmap_write(chip->regmap, PV88060_REG_MASK_A, 0xFF);
		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to mask A reg: %d\n", ret);
			return ret;
		}

		ret = regmap_write(chip->regmap, PV88060_REG_MASK_B, 0xFF);
		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to mask B reg: %d\n", ret);
			return ret;
		}

		ret = regmap_write(chip->regmap, PV88060_REG_MASK_C, 0xFF);
		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to mask C reg: %d\n", ret);
			return ret;
		}

		ret = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL,
					pv88060_irq_handler,
					IRQF_TRIGGER_LOW|IRQF_ONESHOT,
					"pv88060", chip);
		if (ret != 0) {
			dev_err(chip->dev, "Failed to request IRQ: %d\n",
				i2c->irq);
			return ret;
		}

		ret = regmap_update_bits(chip->regmap, PV88060_REG_MASK_A,
			PV88060_M_VDD_FLT | PV88060_M_OVER_TEMP, 0);
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

	for (i = 0; i < PV88060_MAX_REGULATORS; i++) {
		if (init_data)
			config.init_data = &init_data[i];

		config.driver_data = (void *)&pv88060_regulator_info[i];
		chip->rdev[i] = devm_regulator_register(chip->dev,
			&pv88060_regulator_info[i].desc, &config);
		if (IS_ERR(chip->rdev[i])) {
			dev_err(chip->dev,
				"Failed to register PV88060 regulator\n");
			return PTR_ERR(chip->rdev[i]);
		}
	}

	return 0;
}

static const struct i2c_device_id pv88060_i2c_id[] = {
	{"pv88060", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, pv88060_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id pv88060_dt_ids[] = {
	{ .compatible = "pvs,pv88060", .data = &pv88060_i2c_id[0] },
	{},
};
MODULE_DEVICE_TABLE(of, pv88060_dt_ids);
#endif

static struct i2c_driver pv88060_regulator_driver = {
	.driver = {
		.name = "pv88060",
		.of_match_table = of_match_ptr(pv88060_dt_ids),
	},
	.probe = pv88060_i2c_probe,
	.id_table = pv88060_i2c_id,
};

module_i2c_driver(pv88060_regulator_driver);

MODULE_AUTHOR("James Ban <James.Ban.opensource@diasemi.com>");
MODULE_DESCRIPTION("Regulator device driver for Powerventure PV88060");
MODULE_LICENSE("GPL");
