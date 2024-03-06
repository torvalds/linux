// SPDX-License-Identifier: GPL-2.0+
//
// pv88090-regulator.c - Regulator device driver for PV88090
// Copyright (C) 2015  Powerventure Semiconductor Ltd.

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
#include "pv88090-regulator.h"

#define PV88090_MAX_REGULATORS	5

/* PV88090 REGULATOR IDs */
enum {
	/* BUCKs */
	PV88090_ID_BUCK1,
	PV88090_ID_BUCK2,
	PV88090_ID_BUCK3,

	/* LDOs */
	PV88090_ID_LDO1,
	PV88090_ID_LDO2,
};

struct pv88090_regulator {
	struct regulator_desc desc;
	unsigned int conf;
	unsigned int conf2;
};

struct pv88090 {
	struct device *dev;
	struct regmap *regmap;
	struct regulator_dev *rdev[PV88090_MAX_REGULATORS];
};

struct pv88090_buck_voltage {
	int min_uV;
	int max_uV;
	int uV_step;
};

static const struct regmap_config pv88090_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

/* Current limits array (in uA) for BUCK1, BUCK2, BUCK3.
 *  Entry indexes corresponds to register values.
 */

static const unsigned int pv88090_buck1_limits[] = {
	 220000,  440000,  660000,  880000, 1100000, 1320000, 1540000, 1760000,
	1980000, 2200000, 2420000, 2640000, 2860000, 3080000, 3300000, 3520000,
	3740000, 3960000, 4180000, 4400000, 4620000, 4840000, 5060000, 5280000,
	5500000, 5720000, 5940000, 6160000, 6380000, 6600000, 6820000, 7040000
};

static const unsigned int pv88090_buck23_limits[] = {
	1496000, 2393000, 3291000, 4189000
};

static const struct pv88090_buck_voltage pv88090_buck_vol[3] = {
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
	{
		.min_uV = 1250000,
		.max_uV = 2837500,
		.uV_step = 12500,
	},
};

static unsigned int pv88090_buck_get_mode(struct regulator_dev *rdev)
{
	struct pv88090_regulator *info = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret, mode = 0;

	ret = regmap_read(rdev->regmap, info->conf, &data);
	if (ret < 0)
		return ret;

	switch (data & PV88090_BUCK1_MODE_MASK) {
	case PV88090_BUCK_MODE_SYNC:
		mode = REGULATOR_MODE_FAST;
		break;
	case PV88090_BUCK_MODE_AUTO:
		mode = REGULATOR_MODE_NORMAL;
		break;
	case PV88090_BUCK_MODE_SLEEP:
		mode = REGULATOR_MODE_STANDBY;
		break;
	}

	return mode;
}

static int pv88090_buck_set_mode(struct regulator_dev *rdev,
					unsigned int mode)
{
	struct pv88090_regulator *info = rdev_get_drvdata(rdev);
	int val = 0;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = PV88090_BUCK_MODE_SYNC;
		break;
	case REGULATOR_MODE_NORMAL:
		val = PV88090_BUCK_MODE_AUTO;
		break;
	case REGULATOR_MODE_STANDBY:
		val = PV88090_BUCK_MODE_SLEEP;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, info->conf,
					PV88090_BUCK1_MODE_MASK, val);
}

static const struct regulator_ops pv88090_buck_ops = {
	.get_mode = pv88090_buck_get_mode,
	.set_mode = pv88090_buck_set_mode,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_current_limit = regulator_set_current_limit_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
};

static const struct regulator_ops pv88090_ldo_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
};

#define PV88090_BUCK(chip, regl_name, min, step, max, limits_array) \
{\
	.desc	=	{\
		.id = chip##_ID_##regl_name,\
		.name = __stringify(chip##_##regl_name),\
		.of_match = of_match_ptr(#regl_name),\
		.regulators_node = of_match_ptr("regulators"),\
		.type = REGULATOR_VOLTAGE,\
		.owner = THIS_MODULE,\
		.ops = &pv88090_buck_ops,\
		.min_uV = min, \
		.uV_step = step, \
		.n_voltages = ((max) - (min))/(step) + 1, \
		.enable_reg = PV88090_REG_##regl_name##_CONF0, \
		.enable_mask = PV88090_##regl_name##_EN, \
		.vsel_reg = PV88090_REG_##regl_name##_CONF0, \
		.vsel_mask = PV88090_V##regl_name##_MASK, \
		.curr_table = limits_array, \
		.n_current_limits = ARRAY_SIZE(limits_array), \
		.csel_reg = PV88090_REG_##regl_name##_CONF1, \
		.csel_mask = PV88090_##regl_name##_ILIM_MASK, \
	},\
	.conf = PV88090_REG_##regl_name##_CONF1, \
	.conf2 = PV88090_REG_##regl_name##_CONF2, \
}

#define PV88090_LDO(chip, regl_name, min, step, max) \
{\
	.desc	=	{\
		.id = chip##_ID_##regl_name,\
		.name = __stringify(chip##_##regl_name),\
		.of_match = of_match_ptr(#regl_name),\
		.regulators_node = of_match_ptr("regulators"),\
		.type = REGULATOR_VOLTAGE,\
		.owner = THIS_MODULE,\
		.ops = &pv88090_ldo_ops,\
		.min_uV = min, \
		.uV_step = step, \
		.n_voltages = ((max) - (min))/(step) + 1, \
		.enable_reg = PV88090_REG_##regl_name##_CONT, \
		.enable_mask = PV88090_##regl_name##_EN, \
		.vsel_reg = PV88090_REG_##regl_name##_CONT, \
		.vsel_mask = PV88090_V##regl_name##_MASK, \
	},\
}

static struct pv88090_regulator pv88090_regulator_info[] = {
	PV88090_BUCK(PV88090, BUCK1, 600000, 6250, 1393750,
		pv88090_buck1_limits),
	PV88090_BUCK(PV88090, BUCK2, 600000, 6250, 1393750,
		pv88090_buck23_limits),
	PV88090_BUCK(PV88090, BUCK3, 600000, 6250, 1393750,
		pv88090_buck23_limits),
	PV88090_LDO(PV88090, LDO1, 1200000, 50000, 4350000),
	PV88090_LDO(PV88090, LDO2,  650000, 25000, 2225000),
};

static irqreturn_t pv88090_irq_handler(int irq, void *data)
{
	struct pv88090 *chip = data;
	int i, reg_val, err, ret = IRQ_NONE;

	err = regmap_read(chip->regmap, PV88090_REG_EVENT_A, &reg_val);
	if (err < 0)
		goto error_i2c;

	if (reg_val & PV88090_E_VDD_FLT) {
		for (i = 0; i < PV88090_MAX_REGULATORS; i++) {
			if (chip->rdev[i] != NULL)
				regulator_notifier_call_chain(chip->rdev[i],
					REGULATOR_EVENT_UNDER_VOLTAGE,
					NULL);
		}

		err = regmap_write(chip->regmap, PV88090_REG_EVENT_A,
			PV88090_E_VDD_FLT);
		if (err < 0)
			goto error_i2c;

		ret = IRQ_HANDLED;
	}

	if (reg_val & PV88090_E_OVER_TEMP) {
		for (i = 0; i < PV88090_MAX_REGULATORS; i++) {
			if (chip->rdev[i] != NULL)
				regulator_notifier_call_chain(chip->rdev[i],
					REGULATOR_EVENT_OVER_TEMP,
					NULL);
		}

		err = regmap_write(chip->regmap, PV88090_REG_EVENT_A,
			PV88090_E_OVER_TEMP);
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
static int pv88090_i2c_probe(struct i2c_client *i2c)
{
	struct regulator_init_data *init_data = dev_get_platdata(&i2c->dev);
	struct pv88090 *chip;
	struct regulator_config config = { };
	int error, i, ret = 0;
	unsigned int conf2, range, index;

	chip = devm_kzalloc(&i2c->dev, sizeof(struct pv88090), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &i2c->dev;
	chip->regmap = devm_regmap_init_i2c(i2c, &pv88090_regmap_config);
	if (IS_ERR(chip->regmap)) {
		error = PTR_ERR(chip->regmap);
		dev_err(chip->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	i2c_set_clientdata(i2c, chip);

	if (i2c->irq != 0) {
		ret = regmap_write(chip->regmap, PV88090_REG_MASK_A, 0xFF);
		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to mask A reg: %d\n", ret);
			return ret;
		}

		ret = regmap_write(chip->regmap, PV88090_REG_MASK_B, 0xFF);
		if (ret < 0) {
			dev_err(chip->dev,
				"Failed to mask B reg: %d\n", ret);
			return ret;
		}

		ret = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL,
					pv88090_irq_handler,
					IRQF_TRIGGER_LOW|IRQF_ONESHOT,
					"pv88090", chip);
		if (ret != 0) {
			dev_err(chip->dev, "Failed to request IRQ: %d\n",
				i2c->irq);
			return ret;
		}

		ret = regmap_update_bits(chip->regmap, PV88090_REG_MASK_A,
			PV88090_M_VDD_FLT | PV88090_M_OVER_TEMP, 0);
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

	for (i = 0; i < PV88090_MAX_REGULATORS; i++) {
		if (init_data)
			config.init_data = &init_data[i];

		if (i == PV88090_ID_BUCK2 || i == PV88090_ID_BUCK3) {
			ret = regmap_read(chip->regmap,
				pv88090_regulator_info[i].conf2, &conf2);
			if (ret < 0)
				return ret;

			conf2 = (conf2 >> PV88090_BUCK_VDAC_RANGE_SHIFT) &
				PV88090_BUCK_VDAC_RANGE_MASK;

			ret = regmap_read(chip->regmap,
				PV88090_REG_BUCK_FOLD_RANGE, &range);
			if (ret < 0)
				return ret;

			range = (range >>
				 (PV88090_BUCK_VRANGE_GAIN_SHIFT + i - 1)) &
				PV88090_BUCK_VRANGE_GAIN_MASK;
			index = ((range << 1) | conf2);
			if (index > PV88090_ID_BUCK3) {
				dev_err(chip->dev,
					"Invalid index(%d)\n", index);
				return -EINVAL;
			}

			pv88090_regulator_info[i].desc.min_uV
				= pv88090_buck_vol[index].min_uV;
			pv88090_regulator_info[i].desc.uV_step
				= pv88090_buck_vol[index].uV_step;
			pv88090_regulator_info[i].desc.n_voltages
				= ((pv88090_buck_vol[index].max_uV)
				- (pv88090_buck_vol[index].min_uV))
				/(pv88090_buck_vol[index].uV_step) + 1;
		}

		config.driver_data = (void *)&pv88090_regulator_info[i];
		chip->rdev[i] = devm_regulator_register(chip->dev,
			&pv88090_regulator_info[i].desc, &config);
		if (IS_ERR(chip->rdev[i])) {
			dev_err(chip->dev,
				"Failed to register PV88090 regulator\n");
			return PTR_ERR(chip->rdev[i]);
		}
	}

	return 0;
}

static const struct i2c_device_id pv88090_i2c_id[] = {
	{"pv88090", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, pv88090_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id pv88090_dt_ids[] = {
	{ .compatible = "pvs,pv88090", .data = &pv88090_i2c_id[0] },
	{},
};
MODULE_DEVICE_TABLE(of, pv88090_dt_ids);
#endif

static struct i2c_driver pv88090_regulator_driver = {
	.driver = {
		.name = "pv88090",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(pv88090_dt_ids),
	},
	.probe = pv88090_i2c_probe,
	.id_table = pv88090_i2c_id,
};

module_i2c_driver(pv88090_regulator_driver);

MODULE_AUTHOR("James Ban <James.Ban.opensource@diasemi.com>");
MODULE_DESCRIPTION("Regulator device driver for Powerventure PV88090");
MODULE_LICENSE("GPL");
