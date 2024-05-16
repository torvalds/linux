// SPDX-License-Identifier: GPL-2.0+
//
// mp5416.c  - regulator driver for mps mp5416
//
// Copyright 2020 Monolithic Power Systems, Inc
//
// Author: Saravanan Sekar <sravanhome@gmail.com>

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#define MP5416_REG_CTL0			0x00
#define MP5416_REG_CTL1			0x01
#define MP5416_REG_CTL2			0x02
#define MP5416_REG_ILIM			0x03
#define MP5416_REG_BUCK1		0x04
#define MP5416_REG_BUCK2		0x05
#define MP5416_REG_BUCK3		0x06
#define MP5416_REG_BUCK4		0x07
#define MP5416_REG_LDO1			0x08
#define MP5416_REG_LDO2			0x09
#define MP5416_REG_LDO3			0x0a
#define MP5416_REG_LDO4			0x0b

#define MP5416_REGULATOR_EN		BIT(7)
#define MP5416_MASK_VSET		0x7f
#define MP5416_MASK_BUCK1_ILIM		0xc0
#define MP5416_MASK_BUCK2_ILIM		0x0c
#define MP5416_MASK_BUCK3_ILIM		0x30
#define MP5416_MASK_BUCK4_ILIM		0x03
#define MP5416_MASK_DVS_SLEWRATE	0xc0

/* values in uV */
#define MP5416_VOLT1_MIN		600000
#define MP5416_VOLT1_MAX		2187500
#define MP5416_VOLT1_STEP		12500
#define MP5416_VOLT2_MIN		800000
#define MP5416_VOLT2_MAX		3975000
#define MP5416_VOLT2_STEP		25000

#define MP5416_VOLT1_RANGE \
	((MP5416_VOLT1_MAX - MP5416_VOLT1_MIN)/MP5416_VOLT1_STEP + 1)
#define MP5416_VOLT2_RANGE \
	((MP5416_VOLT2_MAX - MP5416_VOLT2_MIN)/MP5416_VOLT2_STEP + 1)

#define MP5416BUCK(_name, _id, _ilim, _dreg, _dval, _vsel)		\
	[MP5416_BUCK ## _id] = {					\
		.id = MP5416_BUCK ## _id,				\
		.name = _name,						\
		.of_match = _name,					\
		.regulators_node = "regulators",			\
		.ops = &mp5416_buck_ops,				\
		.min_uV = MP5416_VOLT ##_vsel## _MIN,			\
		.uV_step = MP5416_VOLT ##_vsel## _STEP,			\
		.n_voltages = MP5416_VOLT ##_vsel## _RANGE,		\
		.curr_table = _ilim,					\
		.n_current_limits = ARRAY_SIZE(_ilim),			\
		.csel_reg = MP5416_REG_ILIM,				\
		.csel_mask = MP5416_MASK_BUCK ## _id ##_ILIM,		\
		.vsel_reg = MP5416_REG_BUCK ## _id,			\
		.vsel_mask = MP5416_MASK_VSET,				\
		.enable_reg = MP5416_REG_BUCK ## _id,			\
		.enable_mask = MP5416_REGULATOR_EN,			\
		.ramp_reg = MP5416_REG_CTL2,				\
		.ramp_mask = MP5416_MASK_DVS_SLEWRATE,			\
		.ramp_delay_table = mp5416_buck_ramp_table,		\
		.n_ramp_values = ARRAY_SIZE(mp5416_buck_ramp_table),	\
		.active_discharge_on	= _dval,			\
		.active_discharge_reg	= _dreg,			\
		.active_discharge_mask	= _dval,			\
		.owner			= THIS_MODULE,			\
	}

#define MP5416LDO(_name, _id, _dval)					\
	[MP5416_LDO ## _id] = {						\
		.id = MP5416_LDO ## _id,				\
		.name = _name,						\
		.of_match = _name,					\
		.regulators_node = "regulators",			\
		.ops = &mp5416_ldo_ops,					\
		.min_uV = MP5416_VOLT2_MIN,				\
		.uV_step = MP5416_VOLT2_STEP,				\
		.n_voltages = MP5416_VOLT2_RANGE,			\
		.vsel_reg = MP5416_REG_LDO ##_id,			\
		.vsel_mask = MP5416_MASK_VSET,				\
		.enable_reg = MP5416_REG_LDO ##_id,			\
		.enable_mask = MP5416_REGULATOR_EN,			\
		.active_discharge_on	= _dval,			\
		.active_discharge_reg	= MP5416_REG_CTL2,		\
		.active_discharge_mask	= _dval,			\
		.owner			= THIS_MODULE,			\
	}

enum mp5416_regulators {
	MP5416_BUCK1,
	MP5416_BUCK2,
	MP5416_BUCK3,
	MP5416_BUCK4,
	MP5416_LDO1,
	MP5416_LDO2,
	MP5416_LDO3,
	MP5416_LDO4,
	MP5416_MAX_REGULATORS,
};

static const struct regmap_config mp5416_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x0d,
};

/* Current limits array (in uA)
 * ILIM1 & ILIM3
 */
static const unsigned int mp5416_I_limits1[] = {
	3800000, 4600000, 5600000, 6800000
};

/* ILIM2 & ILIM4 */
static const unsigned int mp5416_I_limits2[] = {
	2200000, 3200000, 4200000, 5200000
};

/*
 * DVS ramp rate BUCK1 to BUCK4
 * 00: 32mV/us
 * 01: 16mV/us
 * 10: 8mV/us
 * 11: 4mV/us
 */
static const unsigned int mp5416_buck_ramp_table[] = {
	32000, 16000, 8000, 4000
};

static const struct regulator_ops mp5416_ldo_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_active_discharge	= regulator_set_active_discharge_regmap,
};

static const struct regulator_ops mp5416_buck_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_active_discharge	= regulator_set_active_discharge_regmap,
	.get_current_limit	= regulator_get_current_limit_regmap,
	.set_current_limit	= regulator_set_current_limit_regmap,
	.set_ramp_delay		= regulator_set_ramp_delay_regmap,
};

static struct regulator_desc mp5416_regulators_desc[MP5416_MAX_REGULATORS] = {
	MP5416BUCK("buck1", 1, mp5416_I_limits1, MP5416_REG_CTL1, BIT(0), 1),
	MP5416BUCK("buck2", 2, mp5416_I_limits2, MP5416_REG_CTL1, BIT(1), 2),
	MP5416BUCK("buck3", 3, mp5416_I_limits1, MP5416_REG_CTL1, BIT(2), 1),
	MP5416BUCK("buck4", 4, mp5416_I_limits2, MP5416_REG_CTL2, BIT(5), 2),
	MP5416LDO("ldo1", 1, BIT(4)),
	MP5416LDO("ldo2", 2, BIT(3)),
	MP5416LDO("ldo3", 3, BIT(2)),
	MP5416LDO("ldo4", 4, BIT(1)),
};

static struct regulator_desc mp5496_regulators_desc[MP5416_MAX_REGULATORS] = {
	MP5416BUCK("buck1", 1, mp5416_I_limits1, MP5416_REG_CTL1, BIT(0), 1),
	MP5416BUCK("buck2", 2, mp5416_I_limits2, MP5416_REG_CTL1, BIT(1), 1),
	MP5416BUCK("buck3", 3, mp5416_I_limits1, MP5416_REG_CTL1, BIT(2), 1),
	MP5416BUCK("buck4", 4, mp5416_I_limits2, MP5416_REG_CTL2, BIT(5), 1),
	MP5416LDO("ldo1", 1, BIT(4)),
	MP5416LDO("ldo2", 2, BIT(3)),
	MP5416LDO("ldo3", 3, BIT(2)),
	MP5416LDO("ldo4", 4, BIT(1)),
};

static int mp5416_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regulator_config config = { NULL, };
	static const struct regulator_desc *desc;
	struct regulator_dev *rdev;
	struct regmap *regmap;
	int i;

	regmap = devm_regmap_init_i2c(client, &mp5416_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to allocate regmap!\n");
		return PTR_ERR(regmap);
	}

	desc = i2c_get_match_data(client);
	if (!desc)
		return -ENODEV;

	config.dev = dev;
	config.regmap = regmap;

	for (i = 0; i < MP5416_MAX_REGULATORS; i++) {
		rdev = devm_regulator_register(dev,
					       &desc[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "Failed to register regulator!\n");
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct of_device_id mp5416_of_match[] = {
	{ .compatible = "mps,mp5416", .data = &mp5416_regulators_desc },
	{ .compatible = "mps,mp5496", .data = &mp5496_regulators_desc },
	{}
};
MODULE_DEVICE_TABLE(of, mp5416_of_match);

static const struct i2c_device_id mp5416_id[] = {
	{ "mp5416", (kernel_ulong_t)&mp5416_regulators_desc },
	{ "mp5496", (kernel_ulong_t)&mp5496_regulators_desc },
	{}
};
MODULE_DEVICE_TABLE(i2c, mp5416_id);

static struct i2c_driver mp5416_regulator_driver = {
	.driver = {
		.name = "mp5416",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(mp5416_of_match),
	},
	.probe = mp5416_i2c_probe,
	.id_table = mp5416_id,
};
module_i2c_driver(mp5416_regulator_driver);

MODULE_AUTHOR("Saravanan Sekar <sravanhome@gmail.com>");
MODULE_DESCRIPTION("MP5416 PMIC regulator driver");
MODULE_LICENSE("GPL");
