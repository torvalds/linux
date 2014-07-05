/*
 * act8865-regulator.c - Voltage regulation for active-semi ACT88xx PMUs
 *
 * http://www.active-semi.com/products/power-management-units/act88xx/
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
 * ACT8846 Global Register Map.
 */
#define	ACT8846_SYS0		0x00
#define	ACT8846_SYS1		0x01
#define	ACT8846_REG1_VSET	0x10
#define	ACT8846_REG1_CTRL	0x12
#define	ACT8846_REG2_VSET0	0x20
#define	ACT8846_REG2_VSET1	0x21
#define	ACT8846_REG2_CTRL	0x22
#define	ACT8846_REG3_VSET0	0x30
#define	ACT8846_REG3_VSET1	0x31
#define	ACT8846_REG3_CTRL	0x32
#define	ACT8846_REG4_VSET0	0x40
#define	ACT8846_REG4_VSET1	0x41
#define	ACT8846_REG4_CTRL	0x42
#define	ACT8846_REG5_VSET	0x50
#define	ACT8846_REG5_CTRL	0x51
#define	ACT8846_REG6_VSET	0x58
#define	ACT8846_REG6_CTRL	0x59
#define	ACT8846_REG7_VSET	0x60
#define	ACT8846_REG7_CTRL	0x61
#define	ACT8846_REG8_VSET	0x68
#define	ACT8846_REG8_CTRL	0x69
#define	ACT8846_REG9_VSET	0x70
#define	ACT8846_REG9_CTRL	0x71
#define	ACT8846_REG10_VSET	0x80
#define	ACT8846_REG10_CTRL	0x81
#define	ACT8846_REG11_VSET	0x90
#define	ACT8846_REG11_CTRL	0x91
#define	ACT8846_REG12_VSET	0xa0
#define	ACT8846_REG12_CTRL	0xa1
#define	ACT8846_REG13_CTRL	0xb1

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

static const struct regulator_linear_range act8865_voltage_ranges[] = {
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

#define ACT88xx_REG(_name, _family, _id, _vsel_reg)			\
	[_family##_ID_##_id] = {					\
		.name			= _name,			\
		.id			= _family##_ID_##_id,		\
		.type			= REGULATOR_VOLTAGE,		\
		.ops			= &act8865_ops,			\
		.n_voltages		= ACT8865_VOLTAGE_NUM,		\
		.linear_ranges		= act8865_voltage_ranges,	\
		.n_linear_ranges	= ARRAY_SIZE(act8865_voltage_ranges), \
		.vsel_reg		= _family##_##_id##_##_vsel_reg, \
		.vsel_mask		= ACT8865_VSEL_MASK,		\
		.enable_reg		= _family##_##_id##_CTRL,	\
		.enable_mask		= ACT8865_ENA,			\
		.owner			= THIS_MODULE,			\
	}

static const struct regulator_desc act8846_regulators[] = {
	ACT88xx_REG("REG1", ACT8846, REG1, VSET),
	ACT88xx_REG("REG2", ACT8846, REG2, VSET0),
	ACT88xx_REG("REG3", ACT8846, REG3, VSET0),
	ACT88xx_REG("REG4", ACT8846, REG4, VSET0),
	ACT88xx_REG("REG5", ACT8846, REG5, VSET),
	ACT88xx_REG("REG6", ACT8846, REG6, VSET),
	ACT88xx_REG("REG7", ACT8846, REG7, VSET),
	ACT88xx_REG("REG8", ACT8846, REG8, VSET),
	ACT88xx_REG("REG9", ACT8846, REG9, VSET),
	ACT88xx_REG("REG10", ACT8846, REG10, VSET),
	ACT88xx_REG("REG11", ACT8846, REG11, VSET),
	ACT88xx_REG("REG12", ACT8846, REG12, VSET),
};

static const struct regulator_desc act8865_regulators[] = {
	ACT88xx_REG("DCDC_REG1", ACT8865, DCDC1, VSET1),
	ACT88xx_REG("DCDC_REG2", ACT8865, DCDC2, VSET1),
	ACT88xx_REG("DCDC_REG3", ACT8865, DCDC3, VSET1),
	ACT88xx_REG("LDO_REG1", ACT8865, LDO1, VSET),
	ACT88xx_REG("LDO_REG2", ACT8865, LDO2, VSET),
	ACT88xx_REG("LDO_REG3", ACT8865, LDO3, VSET),
	ACT88xx_REG("LDO_REG4", ACT8865, LDO4, VSET),
};

#ifdef CONFIG_OF
static const struct of_device_id act8865_dt_ids[] = {
	{ .compatible = "active-semi,act8846", .data = (void *)ACT8846 },
	{ .compatible = "active-semi,act8865", .data = (void *)ACT8865 },
	{ }
};
MODULE_DEVICE_TABLE(of, act8865_dt_ids);

static struct of_regulator_match act8846_matches[] = {
	[ACT8846_ID_REG1]	= { .name = "REG1" },
	[ACT8846_ID_REG2]	= { .name = "REG2" },
	[ACT8846_ID_REG3]	= { .name = "REG3" },
	[ACT8846_ID_REG4]	= { .name = "REG4" },
	[ACT8846_ID_REG5]	= { .name = "REG5" },
	[ACT8846_ID_REG6]	= { .name = "REG6" },
	[ACT8846_ID_REG7]	= { .name = "REG7" },
	[ACT8846_ID_REG8]	= { .name = "REG8" },
	[ACT8846_ID_REG9]	= { .name = "REG9" },
	[ACT8846_ID_REG10]	= { .name = "REG10" },
	[ACT8846_ID_REG11]	= { .name = "REG11" },
	[ACT8846_ID_REG12]	= { .name = "REG12" },
};

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
				 struct act8865_platform_data *pdata,
				 struct of_regulator_match *matches,
				 int num_matches)
{
	int matched, i;
	struct device_node *np;
	struct act8865_regulator_data *regulator;

	np = of_get_child_by_name(dev->of_node, "regulators");
	if (!np) {
		dev_err(dev, "missing 'regulators' subnode in DT\n");
		return -EINVAL;
	}

	matched = of_regulator_match(dev, np, matches, num_matches);
	of_node_put(np);
	if (matched <= 0)
		return matched;

	pdata->regulators = devm_kzalloc(dev,
					 sizeof(struct act8865_regulator_data) *
					 num_matches, GFP_KERNEL);
	if (!pdata->regulators)
		return -ENOMEM;

	pdata->num_regulators = num_matches;
	regulator = pdata->regulators;

	for (i = 0; i < num_matches; i++) {
		regulator->id = i;
		regulator->name = matches[i].name;
		regulator->platform_data = matches[i].init_data;
		of_node[i] = matches[i].of_node;
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
	static const struct regulator_desc *regulators;
	struct act8865_platform_data pdata_of, *pdata;
	struct of_regulator_match *matches;
	struct device *dev = &client->dev;
	struct device_node **of_node;
	int i, ret, num_regulators;
	struct act8865 *act8865;
	unsigned long type;

	pdata = dev_get_platdata(dev);

	if (dev->of_node && !pdata) {
		const struct of_device_id *id;

		id = of_match_device(of_match_ptr(act8865_dt_ids), dev);
		if (!id)
			return -ENODEV;

		type = (unsigned long) id->data;
	} else {
		type = i2c_id->driver_data;
	}

	switch (type) {
	case ACT8846:
		matches = act8846_matches;
		regulators = act8846_regulators;
		num_regulators = ARRAY_SIZE(act8846_regulators);
		break;
	case ACT8865:
		matches = act8865_matches;
		regulators = act8865_regulators;
		num_regulators = ARRAY_SIZE(act8865_regulators);
		break;
	default:
		dev_err(dev, "invalid device id %lu\n", type);
		return -EINVAL;
	}

	of_node = devm_kzalloc(dev, sizeof(struct device_node *) *
			       num_regulators, GFP_KERNEL);
	if (!of_node)
		return -ENOMEM;

	if (dev->of_node && !pdata) {
		ret = act8865_pdata_from_dt(dev, of_node, &pdata_of, matches,
					    num_regulators);
		if (ret < 0)
			return ret;

		pdata = &pdata_of;
	}

	if (pdata->num_regulators > num_regulators) {
		dev_err(dev, "too many regulators: %d\n",
			pdata->num_regulators);
		return -EINVAL;
	}

	act8865 = devm_kzalloc(dev, sizeof(struct act8865), GFP_KERNEL);
	if (!act8865)
		return -ENOMEM;

	act8865->regmap = devm_regmap_init_i2c(client, &act8865_regmap_config);
	if (IS_ERR(act8865->regmap)) {
		ret = PTR_ERR(act8865->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	/* Finally register devices */
	for (i = 0; i < num_regulators; i++) {
		const struct regulator_desc *desc = &regulators[i];
		struct regulator_config config = { };
		struct regulator_dev *rdev;

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
	devm_kfree(dev, of_node);

	return 0;
}

static const struct i2c_device_id act8865_ids[] = {
	{ .name = "act8846", .driver_data = ACT8846 },
	{ .name = "act8865", .driver_data = ACT8865 },
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

MODULE_DESCRIPTION("active-semi act88xx voltage regulator driver");
MODULE_AUTHOR("Wenyou Yang <wenyou.yang@atmel.com>");
MODULE_LICENSE("GPL v2");
