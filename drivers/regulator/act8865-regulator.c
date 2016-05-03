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
 * ACT8600 Global Register Map.
 */
#define ACT8600_SYS_MODE	0x00
#define ACT8600_SYS_CTRL	0x01
#define ACT8600_DCDC1_VSET	0x10
#define ACT8600_DCDC1_CTRL	0x12
#define ACT8600_DCDC2_VSET	0x20
#define ACT8600_DCDC2_CTRL	0x22
#define ACT8600_DCDC3_VSET	0x30
#define ACT8600_DCDC3_CTRL	0x32
#define ACT8600_SUDCDC4_VSET	0x40
#define ACT8600_SUDCDC4_CTRL	0x41
#define ACT8600_LDO5_VSET	0x50
#define ACT8600_LDO5_CTRL	0x51
#define ACT8600_LDO6_VSET	0x60
#define ACT8600_LDO6_CTRL	0x61
#define ACT8600_LDO7_VSET	0x70
#define ACT8600_LDO7_CTRL	0x71
#define ACT8600_LDO8_VSET	0x80
#define ACT8600_LDO8_CTRL	0x81
#define ACT8600_LDO910_CTRL	0x91
#define ACT8600_APCH0		0xA1
#define ACT8600_APCH1		0xA8
#define ACT8600_APCH2		0xA9
#define ACT8600_APCH_STAT	0xAA
#define ACT8600_OTG0		0xB0
#define ACT8600_OTG1		0xB2

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
#define	ACT8846_GLB_OFF_CTRL	0xc3
#define	ACT8846_OFF_SYSMASK	0x18

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
#define	ACT8865_MSTROFF		0x20

/*
 * Field Definitions.
 */
#define	ACT8865_ENA		0x80	/* ON - [7] */
#define	ACT8865_VSEL_MASK	0x3F	/* VSET - [5:0] */


#define ACT8600_LDO10_ENA		0x40	/* ON - [6] */
#define ACT8600_SUDCDC_VSEL_MASK	0xFF	/* SUDCDC VSET - [7:0] */

/*
 * ACT8865 voltage number
 */
#define	ACT8865_VOLTAGE_NUM	64
#define ACT8600_SUDCDC_VOLTAGE_NUM	255

struct act8865 {
	struct regmap *regmap;
	int off_reg;
	int off_mask;
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

static const struct regulator_linear_range act8600_sudcdc_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(3000000, 0, 63, 0),
	REGULATOR_LINEAR_RANGE(3000000, 64, 159, 100000),
	REGULATOR_LINEAR_RANGE(12600000, 160, 191, 200000),
	REGULATOR_LINEAR_RANGE(19000000, 191, 255, 400000),
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

static struct regulator_ops act8865_ldo_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

#define ACT88xx_REG(_name, _family, _id, _vsel_reg, _supply)		\
	[_family##_ID_##_id] = {					\
		.name			= _name,			\
		.supply_name		= _supply,			\
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

static const struct regulator_desc act8600_regulators[] = {
	ACT88xx_REG("DCDC1", ACT8600, DCDC1, VSET, "vp1"),
	ACT88xx_REG("DCDC2", ACT8600, DCDC2, VSET, "vp2"),
	ACT88xx_REG("DCDC3", ACT8600, DCDC3, VSET, "vp3"),
	{
		.name = "SUDCDC_REG4",
		.id = ACT8600_ID_SUDCDC4,
		.ops = &act8865_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8600_SUDCDC_VOLTAGE_NUM,
		.linear_ranges = act8600_sudcdc_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8600_sudcdc_voltage_ranges),
		.vsel_reg = ACT8600_SUDCDC4_VSET,
		.vsel_mask = ACT8600_SUDCDC_VSEL_MASK,
		.enable_reg = ACT8600_SUDCDC4_CTRL,
		.enable_mask = ACT8865_ENA,
		.owner = THIS_MODULE,
	},
	ACT88xx_REG("LDO5", ACT8600, LDO5, VSET, "inl"),
	ACT88xx_REG("LDO6", ACT8600, LDO6, VSET, "inl"),
	ACT88xx_REG("LDO7", ACT8600, LDO7, VSET, "inl"),
	ACT88xx_REG("LDO8", ACT8600, LDO8, VSET, "inl"),
	{
		.name = "LDO_REG9",
		.id = ACT8600_ID_LDO9,
		.ops = &act8865_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.fixed_uV = 3300000,
		.enable_reg = ACT8600_LDO910_CTRL,
		.enable_mask = ACT8865_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG10",
		.id = ACT8600_ID_LDO10,
		.ops = &act8865_ldo_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.fixed_uV = 1200000,
		.enable_reg = ACT8600_LDO910_CTRL,
		.enable_mask = ACT8600_LDO10_ENA,
		.owner = THIS_MODULE,
	},
};

static const struct regulator_desc act8846_regulators[] = {
	ACT88xx_REG("REG1", ACT8846, REG1, VSET, "vp1"),
	ACT88xx_REG("REG2", ACT8846, REG2, VSET0, "vp2"),
	ACT88xx_REG("REG3", ACT8846, REG3, VSET0, "vp3"),
	ACT88xx_REG("REG4", ACT8846, REG4, VSET0, "vp4"),
	ACT88xx_REG("REG5", ACT8846, REG5, VSET, "inl1"),
	ACT88xx_REG("REG6", ACT8846, REG6, VSET, "inl1"),
	ACT88xx_REG("REG7", ACT8846, REG7, VSET, "inl1"),
	ACT88xx_REG("REG8", ACT8846, REG8, VSET, "inl2"),
	ACT88xx_REG("REG9", ACT8846, REG9, VSET, "inl2"),
	ACT88xx_REG("REG10", ACT8846, REG10, VSET, "inl3"),
	ACT88xx_REG("REG11", ACT8846, REG11, VSET, "inl3"),
	ACT88xx_REG("REG12", ACT8846, REG12, VSET, "inl3"),
};

static const struct regulator_desc act8865_regulators[] = {
	ACT88xx_REG("DCDC_REG1", ACT8865, DCDC1, VSET1, "vp1"),
	ACT88xx_REG("DCDC_REG2", ACT8865, DCDC2, VSET1, "vp2"),
	ACT88xx_REG("DCDC_REG3", ACT8865, DCDC3, VSET1, "vp3"),
	ACT88xx_REG("LDO_REG1", ACT8865, LDO1, VSET, "inl45"),
	ACT88xx_REG("LDO_REG2", ACT8865, LDO2, VSET, "inl45"),
	ACT88xx_REG("LDO_REG3", ACT8865, LDO3, VSET, "inl67"),
	ACT88xx_REG("LDO_REG4", ACT8865, LDO4, VSET, "inl67"),
};

static const struct regulator_desc act8865_alt_regulators[] = {
	ACT88xx_REG("DCDC_REG1", ACT8865, DCDC1, VSET2, "vp1"),
	ACT88xx_REG("DCDC_REG2", ACT8865, DCDC2, VSET2, "vp2"),
	ACT88xx_REG("DCDC_REG3", ACT8865, DCDC3, VSET2, "vp3"),
	ACT88xx_REG("LDO_REG1", ACT8865, LDO1, VSET, "inl45"),
	ACT88xx_REG("LDO_REG2", ACT8865, LDO2, VSET, "inl45"),
	ACT88xx_REG("LDO_REG3", ACT8865, LDO3, VSET, "inl67"),
	ACT88xx_REG("LDO_REG4", ACT8865, LDO4, VSET, "inl67"),
};

#ifdef CONFIG_OF
static const struct of_device_id act8865_dt_ids[] = {
	{ .compatible = "active-semi,act8600", .data = (void *)ACT8600 },
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

static struct of_regulator_match act8600_matches[] = {
	[ACT8600_ID_DCDC1]	= { .name = "DCDC_REG1"},
	[ACT8600_ID_DCDC2]	= { .name = "DCDC_REG2"},
	[ACT8600_ID_DCDC3]	= { .name = "DCDC_REG3"},
	[ACT8600_ID_SUDCDC4]	= { .name = "SUDCDC_REG4"},
	[ACT8600_ID_LDO5]	= { .name = "LDO_REG5"},
	[ACT8600_ID_LDO6]	= { .name = "LDO_REG6"},
	[ACT8600_ID_LDO7]	= { .name = "LDO_REG7"},
	[ACT8600_ID_LDO8]	= { .name = "LDO_REG8"},
	[ACT8600_ID_LDO9]	= { .name = "LDO_REG9"},
	[ACT8600_ID_LDO10]	= { .name = "LDO_REG10"},
};

static int act8865_pdata_from_dt(struct device *dev,
				 struct device_node **of_node,
				 struct act8865_platform_data *pdata,
				 unsigned long type)
{
	int matched, i, num_matches;
	struct device_node *np;
	struct act8865_regulator_data *regulator;
	struct of_regulator_match *matches;

	np = of_get_child_by_name(dev->of_node, "regulators");
	if (!np) {
		dev_err(dev, "missing 'regulators' subnode in DT\n");
		return -EINVAL;
	}

	switch (type) {
	case ACT8600:
		matches = act8600_matches;
		num_matches = ARRAY_SIZE(act8600_matches);
		break;
	case ACT8846:
		matches = act8846_matches;
		num_matches = ARRAY_SIZE(act8846_matches);
		break;
	case ACT8865:
		matches = act8865_matches;
		num_matches = ARRAY_SIZE(act8865_matches);
		break;
	default:
		dev_err(dev, "invalid device id %lu\n", type);
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
		regulator->init_data = matches[i].init_data;
		of_node[i] = matches[i].of_node;
		regulator++;
	}

	return 0;
}
#else
static inline int act8865_pdata_from_dt(struct device *dev,
					struct device_node **of_node,
					struct act8865_platform_data *pdata,
					unsigned long type)
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
			return pdata->regulators[i].init_data;
	}

	return NULL;
}

static struct i2c_client *act8865_i2c_client;
static void act8865_power_off(void)
{
	struct act8865 *act8865;

	act8865 = i2c_get_clientdata(act8865_i2c_client);
	regmap_write(act8865->regmap, act8865->off_reg, act8865->off_mask);
	while (1);
}

static int act8865_pmic_probe(struct i2c_client *client,
			      const struct i2c_device_id *i2c_id)
{
	const struct regulator_desc *regulators;
	struct act8865_platform_data pdata_of, *pdata;
	struct device *dev = &client->dev;
	struct device_node **of_node;
	int i, ret, num_regulators;
	struct act8865 *act8865;
	unsigned long type;
	int off_reg, off_mask;
	int voltage_select = 0;

	pdata = dev_get_platdata(dev);

	if (dev->of_node && !pdata) {
		const struct of_device_id *id;

		id = of_match_device(of_match_ptr(act8865_dt_ids), dev);
		if (!id)
			return -ENODEV;

		type = (unsigned long) id->data;

		voltage_select = !!of_get_property(dev->of_node,
						   "active-semi,vsel-high",
						   NULL);
	} else {
		type = i2c_id->driver_data;
	}

	switch (type) {
	case ACT8600:
		regulators = act8600_regulators;
		num_regulators = ARRAY_SIZE(act8600_regulators);
		off_reg = -1;
		off_mask = -1;
		break;
	case ACT8846:
		regulators = act8846_regulators;
		num_regulators = ARRAY_SIZE(act8846_regulators);
		off_reg = ACT8846_GLB_OFF_CTRL;
		off_mask = ACT8846_OFF_SYSMASK;
		break;
	case ACT8865:
		if (voltage_select) {
			regulators = act8865_alt_regulators;
			num_regulators = ARRAY_SIZE(act8865_alt_regulators);
		} else {
			regulators = act8865_regulators;
			num_regulators = ARRAY_SIZE(act8865_regulators);
		}
		off_reg = ACT8865_SYS_CTRL;
		off_mask = ACT8865_MSTROFF;
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
		ret = act8865_pdata_from_dt(dev, of_node, &pdata_of, type);
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

	if (of_device_is_system_power_controller(dev->of_node)) {
		if (!pm_power_off && (off_reg > 0)) {
			act8865_i2c_client = client;
			act8865->off_reg = off_reg;
			act8865->off_mask = off_mask;
			pm_power_off = act8865_power_off;
		} else {
			dev_err(dev, "Failed to set poweroff capability, already defined\n");
		}
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
	{ .name = "act8600", .driver_data = ACT8600 },
	{ .name = "act8846", .driver_data = ACT8846 },
	{ .name = "act8865", .driver_data = ACT8865 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, act8865_ids);

static struct i2c_driver act8865_pmic_driver = {
	.driver	= {
		.name	= "act8865",
	},
	.probe		= act8865_pmic_probe,
	.id_table	= act8865_ids,
};

module_i2c_driver(act8865_pmic_driver);

MODULE_DESCRIPTION("active-semi act88xx voltage regulator driver");
MODULE_AUTHOR("Wenyou Yang <wenyou.yang@atmel.com>");
MODULE_LICENSE("GPL v2");
