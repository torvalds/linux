// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Ltd.
 */

#include <linux/bitfield.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define MAX20411_UV_STEP		6250
#define MAX20411_BASE_UV		243750
#define MAX20411_MIN_SEL		41 /* 0.5V */
#define MAX20411_MAX_SEL		165 /* 1.275V */
#define MAX20411_VID_OFFSET		0x7
#define MAX20411_VID_MASK		0xff
#define MAX20411_SLEW_OFFSET		0x6
#define MAX20411_SLEW_DVS_MASK		0xc
#define MAX20411_SLEW_SR_MASK		0x3

struct max20411 {
	struct device *dev;
	struct device_node *of_node;
	struct regulator_desc desc;
	struct regulator_dev *rdev;
	struct regmap *regmap;
};

static const unsigned int max20411_slew_rates[] = { 13100, 6600, 3300, 1600 };

static int max20411_enable_time(struct regulator_dev *rdev)
{
	int voltage, rate, ret;
	unsigned int val;

	/* get voltage */
	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret)
		return ret;

	val &= rdev->desc->vsel_mask;
	voltage = regulator_list_voltage_linear(rdev, val);

	/* get rate */
	ret = regmap_read(rdev->regmap, MAX20411_SLEW_OFFSET, &val);
	if (ret)
		return ret;

	val = FIELD_GET(MAX20411_SLEW_SR_MASK, val);
	rate = max20411_slew_rates[val];

	return DIV_ROUND_UP(voltage, rate);
}

static const struct regmap_config max20411_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= 0xe,
};

static const struct regulator_ops max20411_ops = {
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable_time		= max20411_enable_time,
};

static const struct regulator_desc max20411_desc = {
	.ops = &max20411_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.supply_name = "vin",
	.name = "max20411",

	/*
	 * voltage = 0.24375V + selector * 6.25mV
	 * with valid selector between 41 to 165 (0.5V to 1.275V)
	 */
	.min_uV = MAX20411_BASE_UV,
	.uV_step = MAX20411_UV_STEP,
	.linear_min_sel = MAX20411_MIN_SEL,
	.n_voltages = MAX20411_MAX_SEL + 1,

	.vsel_reg = MAX20411_VID_OFFSET,
	.vsel_mask = MAX20411_VID_MASK,

	.ramp_reg = MAX20411_SLEW_OFFSET,
	.ramp_mask = MAX20411_SLEW_DVS_MASK,
	.ramp_delay_table = max20411_slew_rates,
	.n_ramp_values = ARRAY_SIZE(max20411_slew_rates),
};

static int max20411_probe(struct i2c_client *client)
{
	struct regulator_init_data *init_data;
	struct device *dev = &client->dev;
	struct regulator_config cfg = {};
	struct max20411 *max20411;

	max20411 = devm_kzalloc(dev, sizeof(*max20411), GFP_KERNEL);
	if (!max20411)
		return -ENOMEM;

	max20411->regmap = devm_regmap_init_i2c(client, &max20411_regmap_config);
	if (IS_ERR(max20411->regmap)) {
		dev_err(dev, "Failed to allocate regmap!\n");
		return PTR_ERR(max20411->regmap);
	}

	max20411->dev = dev;
	max20411->of_node = dev->of_node;

	max20411->desc = max20411_desc;
	init_data = of_get_regulator_init_data(max20411->dev, max20411->of_node, &max20411->desc);
	if (!init_data)
		return -ENODATA;

	cfg.dev = max20411->dev;
	cfg.init_data = init_data;
	cfg.of_node = max20411->of_node;
	cfg.driver_data = max20411;

	cfg.ena_gpiod = gpiod_get(max20411->dev, "enable", GPIOD_ASIS);
	if (IS_ERR(cfg.ena_gpiod))
		return dev_err_probe(dev, PTR_ERR(cfg.ena_gpiod),
				     "unable to acquire enable gpio\n");

	max20411->rdev = devm_regulator_register(max20411->dev, &max20411->desc, &cfg);
	if (IS_ERR(max20411->rdev))
		dev_err(max20411->dev, "Failed to register regulator\n");

	return PTR_ERR_OR_ZERO(max20411->rdev);
}

static const struct of_device_id of_max20411_match_tbl[] = {
	{ .compatible = "maxim,max20411", },
	{ },
};
MODULE_DEVICE_TABLE(of, of_max20411_match_tbl);

static const struct i2c_device_id max20411_id[] = {
	{ "max20411" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max20411_id);

static struct i2c_driver max20411_i2c_driver = {
	.driver	= {
		.name = "max20411",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table	= of_max20411_match_tbl,
	},
	.probe = max20411_probe,
	.id_table = max20411_id,
};
module_i2c_driver(max20411_i2c_driver);

MODULE_DESCRIPTION("Maxim MAX20411 High-Efficiency Single Step-Down Converter driver");
MODULE_LICENSE("GPL");
