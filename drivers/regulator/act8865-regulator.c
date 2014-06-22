/*
 * act8865-regulator.c - Voltage regulation for the active-semi ACT8865
 * http://www.active-semi.com/sheets/ACT8865_Datasheet.pdf
 *
 * Copyright (C) 2013 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/act8865.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>

/*
 * ACT8865 Global Register Map.
 */
#define	ACT8865_SYS_MODE	0x00
#define	ACT8865_SYS_CTRL	0x01
#define	ACT8865_DCDC1_VSET1	0x20
#define	ACT8865_DCDC1_VSET2	0x21
#define	ACT8865_DCDC1_CTRL	0x22
#define	ACT8865_DCDC2_VSET1	0x30
#define	ACT8865_DCDC2_VSET2	0x31
#define	ACT8865_DCDC2_CTRL	0x32
#define	ACT8865_DCDC3_VSET1	0x40
#define	ACT8865_DCDC3_VSET2	0x41
#define	ACT8865_DCDC3_CTRL	0x42
#define	ACT8865_LDO1_VSET	0x50
#define	ACT8865_LDO1_CTRL	0x51
#define	ACT8865_LDO2_VSET	0x54
#define	ACT8865_LDO2_CTRL	0x55
#define	ACT8865_LDO3_VSET	0x60
#define	ACT8865_LDO3_CTRL	0x61
#define	ACT8865_LDO4_VSET	0x64
#define	ACT8865_LDO4_CTRL	0x65

/*
 * Field Definitions.
 */
#define	ACT8865_ENA		0x80	/* ON - [7] */
#define	ACT8865_VSEL_MASK	0x3F	/* VSET - [5:0] */

/*
 * ACT8865 voltage number
 */
#define	ACT8865_VOLTAGE_NUM	64

struct act8865 {
	struct regmap *regmap;
};

static const struct regmap_config act8865_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct regulator_linear_range act8865_volatge_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 23, 25000),
	REGULATOR_LINEAR_RANGE(1200000, 24, 47, 50000),
	REGULATOR_LINEAR_RANGE(2400000, 48, 63, 100000),
};

static struct regulator_ops act8865_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_desc act8865_reg[] = {
	{
		.name = "DCDC_REG1",
		.id = ACT8865_ID_DCDC1,
		.ops = &act8865_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8865_VOLTAGE_NUM,
		.linear_ranges = act8865_volatge_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8865_volatge_ranges),
		.vsel_reg = ACT8865_DCDC1_VSET1,
		.vsel_mask = ACT8865_VSEL_MASK,
		.enable_reg = ACT8865_DCDC1_CTRL,
		.enable_mask = ACT8865_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC_REG2",
		.id = ACT8865_ID_DCDC2,
		.ops = &act8865_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8865_VOLTAGE_NUM,
		.linear_ranges = act8865_volatge_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8865_volatge_ranges),
		.vsel_reg = ACT8865_DCDC2_VSET1,
		.vsel_mask = ACT8865_VSEL_MASK,
		.enable_reg = ACT8865_DCDC2_CTRL,
		.enable_mask = ACT8865_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC_REG3",
		.id = ACT8865_ID_DCDC3,
		.ops = &act8865_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8865_VOLTAGE_NUM,
		.linear_ranges = act8865_volatge_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8865_volatge_ranges),
		.vsel_reg = ACT8865_DCDC3_VSET1,
		.vsel_mask = ACT8865_VSEL_MASK,
		.enable_reg = ACT8865_DCDC3_CTRL,
		.enable_mask = ACT8865_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG1",
		.id = ACT8865_ID_LDO1,
		.ops = &act8865_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8865_VOLTAGE_NUM,
		.linear_ranges = act8865_volatge_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8865_volatge_ranges),
		.vsel_reg = ACT8865_LDO1_VSET,
		.vsel_mask = ACT8865_VSEL_MASK,
		.enable_reg = ACT8865_LDO1_CTRL,
		.enable_mask = ACT8865_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG2",
		.id = ACT8865_ID_LDO2,
		.ops = &act8865_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8865_VOLTAGE_NUM,
		.linear_ranges = act8865_volatge_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8865_volatge_ranges),
		.vsel_reg = ACT8865_LDO2_VSET,
		.vsel_mask = ACT8865_VSEL_MASK,
		.enable_reg = ACT8865_LDO2_CTRL,
		.enable_mask = ACT8865_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG3",
		.id = ACT8865_ID_LDO3,
		.ops = &act8865_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8865_VOLTAGE_NUM,
		.linear_ranges = act8865_volatge_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8865_volatge_ranges),
		.vsel_reg = ACT8865_LDO3_VSET,
		.vsel_mask = ACT8865_VSEL_MASK,
		.enable_reg = ACT8865_LDO3_CTRL,
		.enable_mask = ACT8865_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG4",
		.id = ACT8865_ID_LDO4,
		.ops = &act8865_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8865_VOLTAGE_NUM,
		.linear_ranges = act8865_volatge_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8865_volatge_ranges),
		.vsel_reg = ACT8865_LDO4_VSET,
		.vsel_mask = ACT8865_VSEL_MASK,
		.enable_reg = ACT8865_LDO4_CTRL,
		.enable_mask = ACT8865_ENA,
		.owner = THIS_MODULE,
	},
};

#ifdef CONFIG_OF
static const struct of_device_id act8865_dt_ids[] = {
	{ .compatible = "active-semi,act8865" },
	{ }
};
MODULE_DEVICE_TABLE(of, act8865_dt_ids);

static struct of_regulator_match act8865_matches[] = {
	[ACT8865_ID_DCDC1]	= { .name = "DCDC_REG1"},
	[ACT8865_ID_DCDC2]	= { .name = "DCDC_REG2"},
	[ACT8865_ID_DCDC3]	= { .name = "DCDC_REG3"},
	[ACT8865_ID_LDO1]	= { .name = "LDO_REG1"},
	[ACT8865_ID_LDO2]	= { .name = "LDO_REG2"},
	[ACT8865_ID_LDO3]	= { .name = "LDO_REG3"},
	[ACT8865_ID_LDO4]	= { .name = "LDO_REG4"},
};

static int act8865_pdata_from_dt(struct device *dev,
				 struct device_node **of_node,
				 struct act8865_platform_data *pdata)
{
	int matched, i;
	struct device_node *np;
	struct act8865_regulator_data *regulator;

	np = of_get_child_by_name(dev->of_node, "regulators");
	if (!np) {
		dev_err(dev, "missing 'regulators' subnode in DT\n");
		return -EINVAL;
	}

	matched = of_regulator_match(dev, np,
				act8865_matches, ARRAY_SIZE(act8865_matches));
	of_node_put(np);
	if (matched <= 0)
		return matched;

	pdata->regulators = devm_kzalloc(dev,
				sizeof(struct act8865_regulator_data) *
				ARRAY_SIZE(act8865_matches), GFP_KERNEL);
	if (!pdata->regulators)
		return -ENOMEM;

	pdata->num_regulators = matched;
	regulator = pdata->regulators;

	for (i = 0; i < ARRAY_SIZE(act8865_matches); i++) {
		regulator->id = i;
		regulator->name = act8865_matches[i].name;
		regulator->platform_data = act8865_matches[i].init_data;
		of_node[i] = act8865_matches[i].of_node;
		regulator++;
	}

	return 0;
}
#else
static inline int act8865_pdata_from_dt(struct device *dev,
					struct device_node **of_node,
					struct act8865_platform_data *pdata)
{
	return 0;
}
#endif

static struct regulator_init_data
*act8865_get_init_data(int id, struct act8865_platform_data *pdata)
{
	int i;

	if (!pdata)
		return NULL;

	for (i = 0; i < pdata->num_regulators; i++) {
		if (pdata->regulators[i].id == id)
			return pdata->regulators[i].platform_data;
	}

	return NULL;
}

static int act8865_pmic_probe(struct i2c_client *client,
			   const struct i2c_device_id *i2c_id)
{
	struct regulator_dev *rdev;
	struct device *dev = &client->dev;
	struct act8865_platform_data *pdata = dev_get_platdata(dev);
	struct regulator_config config = { };
	struct act8865 *act8865;
	struct device_node *of_node[ACT8865_REG_NUM];
	int i;
	int ret = -EINVAL;
	int error;

	if (dev->of_node && !pdata) {
		const struct of_device_id *id;
		struct act8865_platform_data pdata_of;

		id = of_match_device(of_match_ptr(act8865_dt_ids), dev);
		if (!id)
			return -ENODEV;

		ret = act8865_pdata_from_dt(dev, of_node, &pdata_of);
		if (ret < 0)
			return ret;

		pdata = &pdata_of;
	}

	if (pdata->num_regulators > ACT8865_REG_NUM) {
		dev_err(dev, "Too many regulators found!\n");
		return -EINVAL;
	}

	act8865 = devm_kzalloc(dev, sizeof(struct act8865), GFP_KERNEL);
	if (!act8865)
		return -ENOMEM;

	act8865->regmap = devm_regmap_init_i2c(client, &act8865_regmap_config);
	if (IS_ERR(act8865->regmap)) {
		error = PTR_ERR(act8865->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	/* Finally register devices */
	for (i = 0; i < ACT8865_REG_NUM; i++) {
		const struct regulator_desc *desc = &act8865_reg[i];

		config.dev = dev;
		config.init_data = act8865_get_init_data(desc->id, pdata);
		config.of_node = of_node[i];
		config.driver_data = act8865;
		config.regmap = act8865->regmap;

		rdev = devm_regulator_register(&client->dev, desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "failed to register %s\n", desc->name);
			return PTR_ERR(rdev);
		}
	}

	i2c_set_clientdata(client, act8865);

	return 0;
}

static const struct i2c_device_id act8865_ids[] = {
	{ "act8865", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, act8865_ids);

static struct i2c_driver act8865_pmic_driver = {
	.driver	= {
		.name	= "act8865",
		.owner	= THIS_MODULE,
	},
	.probe		= act8865_pmic_probe,
	.id_table	= act8865_ids,
};

module_i2c_driver(act8865_pmic_driver);

MODULE_DESCRIPTION("active-semi act8865 voltage regulator driver");
MODULE_AUTHOR("Wenyou Yang <wenyou.yang@atmel.com>");
MODULE_LICENSE("GPL v2");
