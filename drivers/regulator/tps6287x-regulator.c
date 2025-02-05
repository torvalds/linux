// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Axis Communications AB
 *
 * Driver for Texas Instruments TPS6287x PMIC.
 * Datasheet: https://www.ti.com/lit/ds/symlink/tps62873.pdf
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/bitfield.h>
#include <linux/linear_range.h>

#define TPS6287X_VSET		0x00
#define TPS6287X_CTRL1		0x01
#define TPS6287X_CTRL1_VRAMP	GENMASK(1, 0)
#define TPS6287X_CTRL1_FPWMEN	BIT(4)
#define TPS6287X_CTRL1_SWEN	BIT(5)
#define TPS6287X_CTRL2		0x02
#define TPS6287X_CTRL2_VRANGE	GENMASK(3, 2)
#define TPS6287X_CTRL3		0x03
#define TPS6287X_STATUS		0x04

static const struct regmap_config tps6287x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TPS6287X_STATUS,
};

static const struct linear_range tps6287x_voltage_ranges[] = {
	LINEAR_RANGE(400000, 0, 0xFF, 1250),
	LINEAR_RANGE(400000, 0, 0xFF, 2500),
	LINEAR_RANGE(400000, 0, 0xFF, 5000),
	LINEAR_RANGE(800000, 0, 0xFF, 10000),
};

static const unsigned int tps6287x_voltage_range_sel[] = {
	0x0, 0x1, 0x2, 0x3
};

static const unsigned int tps6287x_voltage_range_prefix[] = {
	0x000, 0x100, 0x200, 0x300
};

static const unsigned int tps6287x_ramp_table[] = {
	10000, 5000, 1250, 500
};

struct tps6287x_reg_data {
	int range;
};

static int tps6287x_best_range(struct regulator_config *config, const struct regulator_desc *desc)
{
	const struct linear_range *r;
	int i;

	if (!config->init_data->constraints.apply_uV)
		return -1;

	for (i = 0; i < desc->n_linear_ranges; i++) {
		r = &desc->linear_ranges[i];
		if (r->min <= config->init_data->constraints.min_uV &&
		    config->init_data->constraints.max_uV <= linear_range_get_max_value(r))
			return i;
	}
	return -1;
}

static int tps6287x_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	unsigned int val;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	case REGULATOR_MODE_FAST:
		val = TPS6287X_CTRL1_FPWMEN;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, TPS6287X_CTRL1,
				  TPS6287X_CTRL1_FPWMEN, val);
}

static unsigned int tps6287x_get_mode(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, TPS6287X_CTRL1, &val);
	if (ret < 0)
		return 0;

	return (val & TPS6287X_CTRL1_FPWMEN) ? REGULATOR_MODE_FAST :
	    REGULATOR_MODE_NORMAL;
}

static unsigned int tps6287x_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case REGULATOR_MODE_NORMAL:
	case REGULATOR_MODE_FAST:
		return mode;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static int tps6287x_map_voltage(struct regulator_dev *rdev, int min_uV, int max_uV)
{
	struct tps6287x_reg_data *data = (struct tps6287x_reg_data *)rdev->reg_data;
	struct linear_range selected_range;
	int selector, voltage;

	if (!data || data->range == -1)
		return regulator_map_voltage_pickable_linear_range(rdev, min_uV, max_uV);

	selected_range = rdev->desc->linear_ranges[data->range];
	selector = DIV_ROUND_UP(min_uV - selected_range.min, selected_range.step);
	if (selector < selected_range.min_sel || selector > selected_range.max_sel)
		return -EINVAL;

	selector |= tps6287x_voltage_range_prefix[data->range];
	voltage = rdev->desc->ops->list_voltage(rdev, selector);
	if (voltage < min_uV || voltage > max_uV)
		return -EINVAL;

	return selector;
}

static const struct regulator_ops tps6287x_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.set_mode = tps6287x_set_mode,
	.get_mode = tps6287x_get_mode,
	.is_enabled = regulator_is_enabled_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_pickable_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_pickable_regmap,
	.list_voltage = regulator_list_voltage_pickable_linear_range,
	.map_voltage = tps6287x_map_voltage,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
};

static const struct regulator_desc tps6287x_reg = {
	.name = "tps6287x",
	.owner = THIS_MODULE,
	.ops = &tps6287x_regulator_ops,
	.of_map_mode = tps6287x_of_map_mode,
	.type = REGULATOR_VOLTAGE,
	.enable_reg = TPS6287X_CTRL1,
	.enable_mask = TPS6287X_CTRL1_SWEN,
	.vsel_reg = TPS6287X_VSET,
	.vsel_mask = 0xFF,
	.vsel_range_reg = TPS6287X_CTRL2,
	.vsel_range_mask = TPS6287X_CTRL2_VRANGE,
	.range_applied_by_vsel = true,
	.ramp_reg = TPS6287X_CTRL1,
	.ramp_mask = TPS6287X_CTRL1_VRAMP,
	.ramp_delay_table = tps6287x_ramp_table,
	.n_ramp_values = ARRAY_SIZE(tps6287x_ramp_table),
	.n_voltages = 256 * ARRAY_SIZE(tps6287x_voltage_ranges),
	.linear_ranges = tps6287x_voltage_ranges,
	.n_linear_ranges = ARRAY_SIZE(tps6287x_voltage_ranges),
	.linear_range_selectors_bitfield = tps6287x_voltage_range_sel,
};

static int tps6287x_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct regulator_config config = {};
	struct tps6287x_reg_data *reg_data;
	struct regulator_dev *rdev;

	reg_data = devm_kzalloc(dev, sizeof(struct tps6287x_reg_data), GFP_KERNEL);

	if (!reg_data)
		return -ENOMEM;

	config.regmap = devm_regmap_init_i2c(i2c, &tps6287x_regmap_config);
	if (IS_ERR(config.regmap)) {
		dev_err(dev, "Failed to init i2c\n");
		return PTR_ERR(config.regmap);
	}

	config.dev = dev;
	config.of_node = dev->of_node;
	config.init_data = of_get_regulator_init_data(dev, dev->of_node,
						      &tps6287x_reg);

	reg_data->range = tps6287x_best_range(&config, &tps6287x_reg);

	rdev = devm_regulator_register(dev, &tps6287x_reg, &config);
	if (IS_ERR(rdev)) {
		dev_err(dev, "Failed to register regulator\n");
		return PTR_ERR(rdev);
	}

	rdev->reg_data = (void *)reg_data;
	dev_dbg(dev, "Probed regulator\n");

	return 0;
}

static const struct of_device_id tps6287x_dt_ids[] = {
	{ .compatible = "ti,tps62870", },
	{ .compatible = "ti,tps62871", },
	{ .compatible = "ti,tps62872", },
	{ .compatible = "ti,tps62873", },
	{ }
};

MODULE_DEVICE_TABLE(of, tps6287x_dt_ids);

static const struct i2c_device_id tps6287x_i2c_id[] = {
	{ "tps62870" },
	{ "tps62871" },
	{ "tps62872" },
	{ "tps62873" },
	{}
};

MODULE_DEVICE_TABLE(i2c, tps6287x_i2c_id);

static struct i2c_driver tps6287x_regulator_driver = {
	.driver = {
		.name = "tps6287x",
		.of_match_table = tps6287x_dt_ids,
	},
	.probe = tps6287x_i2c_probe,
	.id_table = tps6287x_i2c_id,
};

module_i2c_driver(tps6287x_regulator_driver);

MODULE_AUTHOR("Mårten Lindahl <marten.lindahl@axis.com>");
MODULE_DESCRIPTION("Regulator driver for TI TPS6287X PMIC");
MODULE_LICENSE("GPL");
