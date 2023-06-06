// SPDX-License-Identifier: GPL-2.0+
//
// pv88080-regulator.c - Regulator device driver for PV88080
// Copyright (C) 2016  Powerventure Semiconductor Ltd.

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/regulator/of_regulator.h>
#include "pv88080-regulator.h"

#define PV88080_MAX_REGULATORS	4

/* PV88080 REGULATOR IDs */
enum {
	/* BUCKs */
	PV88080_ID_BUCK1,
	PV88080_ID_BUCK2,
	PV88080_ID_BUCK3,
	PV88080_ID_HVBUCK,
};

enum pv88080_types {
	TYPE_PV88080_AA,
	TYPE_PV88080_BA,
};

struct pv88080_regulator {
	struct regulator_desc desc;
	unsigned int mode_reg;
	unsigned int conf2;
	unsigned int conf5;
};

struct pv88080 {
	struct device *dev;
	struct regmap *regmap;
	struct regulator_dev *rdev[PV88080_MAX_REGULATORS];
	unsigned long type;
	const struct pv88080_compatible_regmap *regmap_config;
};

struct pv88080_buck_voltage {
	int min_uV;
	int max_uV;
	int uV_step;
};

struct pv88080_buck_regmap {
	/* REGS */
	int buck_enable_reg;
	int buck_vsel_reg;
	int buck_mode_reg;
	int buck_limit_reg;
	int buck_vdac_range_reg;
	int buck_vrange_gain_reg;
	/* MASKS */
	int buck_enable_mask;
	int buck_vsel_mask;
	int buck_limit_mask;
};

struct pv88080_compatible_regmap {
	/* BUCK1, 2, 3 */
	struct pv88080_buck_regmap buck_regmap[PV88080_MAX_REGULATORS-1];
	/* HVBUCK */
	int hvbuck_enable_reg;
	int hvbuck_vsel_reg;
	int hvbuck_enable_mask;
	int hvbuck_vsel_mask;
};

static const struct regmap_config pv88080_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

/* Current limits array (in uA) for BUCK1, BUCK2, BUCK3.
 * Entry indexes corresponds to register values.
 */

static const unsigned int pv88080_buck1_limits[] = {
	3230000, 5130000, 6960000, 8790000
};

static const unsigned int pv88080_buck23_limits[] = {
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

static const struct pv88080_compatible_regmap pv88080_aa_regs = {
	/* BUCK1 */
	.buck_regmap[0] = {
		.buck_enable_reg      = PV88080AA_REG_BUCK1_CONF0,
		.buck_vsel_reg        = PV88080AA_REG_BUCK1_CONF0,
		.buck_mode_reg        = PV88080AA_REG_BUCK1_CONF1,
		.buck_limit_reg       = PV88080AA_REG_BUCK1_CONF1,
		.buck_vdac_range_reg  = PV88080AA_REG_BUCK1_CONF2,
		.buck_vrange_gain_reg = PV88080AA_REG_BUCK1_CONF5,
		.buck_enable_mask     = PV88080_BUCK1_EN,
		.buck_vsel_mask       = PV88080_VBUCK1_MASK,
		.buck_limit_mask      = PV88080_BUCK1_ILIM_MASK,
	},
	/* BUCK2 */
	.buck_regmap[1] = {
		.buck_enable_reg      = PV88080AA_REG_BUCK2_CONF0,
		.buck_vsel_reg        = PV88080AA_REG_BUCK2_CONF0,
		.buck_mode_reg        = PV88080AA_REG_BUCK2_CONF1,
		.buck_limit_reg	      = PV88080AA_REG_BUCK2_CONF1,
		.buck_vdac_range_reg  = PV88080AA_REG_BUCK2_CONF2,
		.buck_vrange_gain_reg = PV88080AA_REG_BUCK2_CONF5,
		.buck_enable_mask	  = PV88080_BUCK2_EN,
		.buck_vsel_mask       = PV88080_VBUCK2_MASK,
		.buck_limit_mask      = PV88080_BUCK2_ILIM_MASK,
	},
	/* BUCK3 */
	.buck_regmap[2] = {
		.buck_enable_reg	  = PV88080AA_REG_BUCK3_CONF0,
		.buck_vsel_reg        = PV88080AA_REG_BUCK3_CONF0,
		.buck_mode_reg        = PV88080AA_REG_BUCK3_CONF1,
		.buck_limit_reg	      = PV88080AA_REG_BUCK3_CONF1,
		.buck_vdac_range_reg  = PV88080AA_REG_BUCK3_CONF2,
		.buck_vrange_gain_reg = PV88080AA_REG_BUCK3_CONF5,
		.buck_enable_mask	  = PV88080_BUCK3_EN,
		.buck_vsel_mask       = PV88080_VBUCK3_MASK,
		.buck_limit_mask      = PV88080_BUCK3_ILIM_MASK,
	},
	/* HVBUCK */
	.hvbuck_enable_reg	      = PV88080AA_REG_HVBUCK_CONF2,
	.hvbuck_vsel_reg          = PV88080AA_REG_HVBUCK_CONF1,
	.hvbuck_enable_mask       = PV88080_HVBUCK_EN,
	.hvbuck_vsel_mask         = PV88080_VHVBUCK_MASK,
};

static const struct pv88080_compatible_regmap pv88080_ba_regs = {
	/* BUCK1 */
	.buck_regmap[0] = {
		.buck_enable_reg	  = PV88080BA_REG_BUCK1_CONF0,
		.buck_vsel_reg        = PV88080BA_REG_BUCK1_CONF0,
		.buck_mode_reg        = PV88080BA_REG_BUCK1_CONF1,
		.buck_limit_reg       = PV88080BA_REG_BUCK1_CONF1,
		.buck_vdac_range_reg  = PV88080BA_REG_BUCK1_CONF2,
		.buck_vrange_gain_reg = PV88080BA_REG_BUCK1_CONF5,
		.buck_enable_mask     = PV88080_BUCK1_EN,
		.buck_vsel_mask       = PV88080_VBUCK1_MASK,
		.buck_limit_mask	  = PV88080_BUCK1_ILIM_MASK,
	},
	/* BUCK2 */
	.buck_regmap[1] = {
		.buck_enable_reg	  = PV88080BA_REG_BUCK2_CONF0,
		.buck_vsel_reg        = PV88080BA_REG_BUCK2_CONF0,
		.buck_mode_reg        = PV88080BA_REG_BUCK2_CONF1,
		.buck_limit_reg	      = PV88080BA_REG_BUCK2_CONF1,
		.buck_vdac_range_reg  = PV88080BA_REG_BUCK2_CONF2,
		.buck_vrange_gain_reg = PV88080BA_REG_BUCK2_CONF5,
		.buck_enable_mask	  = PV88080_BUCK2_EN,
		.buck_vsel_mask       = PV88080_VBUCK2_MASK,
		.buck_limit_mask	  = PV88080_BUCK2_ILIM_MASK,
	},
	/* BUCK3 */
	.buck_regmap[2] = {
		.buck_enable_reg	  = PV88080BA_REG_BUCK3_CONF0,
		.buck_vsel_reg        = PV88080BA_REG_BUCK3_CONF0,
		.buck_mode_reg        = PV88080BA_REG_BUCK3_CONF1,
		.buck_limit_reg	      = PV88080BA_REG_BUCK3_CONF1,
		.buck_vdac_range_reg  = PV88080BA_REG_BUCK3_CONF2,
		.buck_vrange_gain_reg = PV88080BA_REG_BUCK3_CONF5,
		.buck_enable_mask	  = PV88080_BUCK3_EN,
		.buck_vsel_mask       = PV88080_VBUCK3_MASK,
		.buck_limit_mask	  = PV88080_BUCK3_ILIM_MASK,
	},
	/* HVBUCK */
	.hvbuck_enable_reg	      = PV88080BA_REG_HVBUCK_CONF2,
	.hvbuck_vsel_reg          = PV88080BA_REG_HVBUCK_CONF1,
	.hvbuck_enable_mask       = PV88080_HVBUCK_EN,
	.hvbuck_vsel_mask		  = PV88080_VHVBUCK_MASK,
};

#ifdef CONFIG_OF
static const struct of_device_id pv88080_dt_ids[] = {
	{ .compatible = "pvs,pv88080",    .data = (void *)TYPE_PV88080_AA },
	{ .compatible = "pvs,pv88080-aa", .data = (void *)TYPE_PV88080_AA },
	{ .compatible = "pvs,pv88080-ba", .data = (void *)TYPE_PV88080_BA },
	{},
};
MODULE_DEVICE_TABLE(of, pv88080_dt_ids);
#endif

static unsigned int pv88080_buck_get_mode(struct regulator_dev *rdev)
{
	struct pv88080_regulator *info = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret, mode = 0;

	ret = regmap_read(rdev->regmap, info->mode_reg, &data);
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

	return regmap_update_bits(rdev->regmap, info->mode_reg,
					PV88080_BUCK1_MODE_MASK, val);
}

static const struct regulator_ops pv88080_buck_ops = {
	.get_mode = pv88080_buck_get_mode,
	.set_mode = pv88080_buck_set_mode,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_current_limit = regulator_set_current_limit_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
};

static const struct regulator_ops pv88080_hvbuck_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
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
		.curr_table = limits_array, \
		.n_current_limits = ARRAY_SIZE(limits_array), \
	},\
}

#define PV88080_HVBUCK(chip, regl_name, min, step, max) \
{\
	.desc	=	{\
		.id = chip##_ID_##regl_name,\
		.name = __stringify(chip##_##regl_name),\
		.of_match = of_match_ptr(#regl_name),\
		.regulators_node = of_match_ptr("regulators"),\
		.type = REGULATOR_VOLTAGE,\
		.owner = THIS_MODULE,\
		.ops = &pv88080_hvbuck_ops,\
		.min_uV = min, \
		.uV_step = step, \
		.n_voltages = ((max) - (min))/(step) + 1, \
	},\
}

static struct pv88080_regulator pv88080_regulator_info[] = {
	PV88080_BUCK(PV88080, BUCK1, 600000, 6250, 1393750,
		pv88080_buck1_limits),
	PV88080_BUCK(PV88080, BUCK2, 600000, 6250, 1393750,
		pv88080_buck23_limits),
	PV88080_BUCK(PV88080, BUCK3, 600000, 6250, 1393750,
		pv88080_buck23_limits),
	PV88080_HVBUCK(PV88080, HVBUCK, 0, 5000, 1275000),
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
			if (chip->rdev[i] != NULL)
				regulator_notifier_call_chain(chip->rdev[i],
					REGULATOR_EVENT_UNDER_VOLTAGE,
					NULL);
		}

		err = regmap_write(chip->regmap, PV88080_REG_EVENT_A,
			PV88080_E_VDD_FLT);
		if (err < 0)
			goto error_i2c;

		ret = IRQ_HANDLED;
	}

	if (reg_val & PV88080_E_OVER_TEMP) {
		for (i = 0; i < PV88080_MAX_REGULATORS; i++) {
			if (chip->rdev[i] != NULL)
				regulator_notifier_call_chain(chip->rdev[i],
					REGULATOR_EVENT_OVER_TEMP,
					NULL);
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
static int pv88080_i2c_probe(struct i2c_client *i2c)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(i2c);
	struct regulator_init_data *init_data = dev_get_platdata(&i2c->dev);
	struct pv88080 *chip;
	const struct pv88080_compatible_regmap *regmap_config;
	const struct of_device_id *match;
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

	if (i2c->dev.of_node) {
		match = of_match_node(pv88080_dt_ids, i2c->dev.of_node);
		if (!match) {
			dev_err(chip->dev, "Failed to get of_match_node\n");
			return -EINVAL;
		}
		chip->type = (unsigned long)match->data;
	} else {
		chip->type = id->driver_data;
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

	switch (chip->type) {
	case TYPE_PV88080_AA:
		chip->regmap_config = &pv88080_aa_regs;
		break;
	case TYPE_PV88080_BA:
		chip->regmap_config = &pv88080_ba_regs;
		break;
	}

	regmap_config = chip->regmap_config;
	config.dev = chip->dev;
	config.regmap = chip->regmap;

	/* Registeration for BUCK1, 2, 3 */
	for (i = 0; i < PV88080_MAX_REGULATORS-1; i++) {
		if (init_data)
			config.init_data = &init_data[i];

		pv88080_regulator_info[i].desc.csel_reg
			= regmap_config->buck_regmap[i].buck_limit_reg;
		pv88080_regulator_info[i].desc.csel_mask
			= regmap_config->buck_regmap[i].buck_limit_mask;
		pv88080_regulator_info[i].mode_reg
			= regmap_config->buck_regmap[i].buck_mode_reg;
		pv88080_regulator_info[i].conf2
			= regmap_config->buck_regmap[i].buck_vdac_range_reg;
		pv88080_regulator_info[i].conf5
			= regmap_config->buck_regmap[i].buck_vrange_gain_reg;
		pv88080_regulator_info[i].desc.enable_reg
			= regmap_config->buck_regmap[i].buck_enable_reg;
		pv88080_regulator_info[i].desc.enable_mask
			= regmap_config->buck_regmap[i].buck_enable_mask;
		pv88080_regulator_info[i].desc.vsel_reg
			= regmap_config->buck_regmap[i].buck_vsel_reg;
		pv88080_regulator_info[i].desc.vsel_mask
			= regmap_config->buck_regmap[i].buck_vsel_mask;

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

	pv88080_regulator_info[PV88080_ID_HVBUCK].desc.enable_reg
		= regmap_config->hvbuck_enable_reg;
	pv88080_regulator_info[PV88080_ID_HVBUCK].desc.enable_mask
		= regmap_config->hvbuck_enable_mask;
	pv88080_regulator_info[PV88080_ID_HVBUCK].desc.vsel_reg
		= regmap_config->hvbuck_vsel_reg;
	pv88080_regulator_info[PV88080_ID_HVBUCK].desc.vsel_mask
		= regmap_config->hvbuck_vsel_mask;

	/* Registeration for HVBUCK */
	if (init_data)
		config.init_data = &init_data[PV88080_ID_HVBUCK];

	config.driver_data = (void *)&pv88080_regulator_info[PV88080_ID_HVBUCK];
	chip->rdev[PV88080_ID_HVBUCK] = devm_regulator_register(chip->dev,
		&pv88080_regulator_info[PV88080_ID_HVBUCK].desc, &config);
	if (IS_ERR(chip->rdev[PV88080_ID_HVBUCK])) {
		dev_err(chip->dev, "Failed to register PV88080 regulator\n");
		return PTR_ERR(chip->rdev[PV88080_ID_HVBUCK]);
	}

	return 0;
}

static const struct i2c_device_id pv88080_i2c_id[] = {
	{ "pv88080",    TYPE_PV88080_AA },
	{ "pv88080-aa", TYPE_PV88080_AA },
	{ "pv88080-ba", TYPE_PV88080_BA },
	{},
};
MODULE_DEVICE_TABLE(i2c, pv88080_i2c_id);

static struct i2c_driver pv88080_regulator_driver = {
	.driver = {
		.name = "pv88080",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(pv88080_dt_ids),
	},
	.probe_new = pv88080_i2c_probe,
	.id_table = pv88080_i2c_id,
};

module_i2c_driver(pv88080_regulator_driver);

MODULE_AUTHOR("James Ban <James.Ban.opensource@diasemi.com>");
MODULE_DESCRIPTION("Regulator device driver for Powerventure PV88080");
MODULE_LICENSE("GPL");
