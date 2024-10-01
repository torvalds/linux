// SPDX-License-Identifier: GPL-2.0-or-later
//
// max77826-regulator.c  - regulator driver for Maxim MAX77826
//
// Author: Iskren Chernev <iskren.chernev@gmail.com>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

enum max77826_registers {
	MAX77826_REG_INT_SRC = 0x00,
	MAX77826_REG_SYS_INT,
	MAX77826_REG_INT1,
	MAX77826_REG_INT2,
	MAX77826_REG_BB_INT,
	MAX77826_REG_INT_SRC_M,
	MAX77826_REG_TOPSYS_INT_M,
	MAX77826_REG_INT1_M,
	MAX77826_REG_INT2_M,
	MAX77826_REG_BB_INT_M,
	MAX77826_REG_TOPSYS_STAT,
	MAX77826_REG_STAT1,
	MAX77826_REG_STAT2,
	MAX77826_REG_BB_STAT,
	/* 0x0E - 0x0F: Reserved */
	MAX77826_REG_LDO_OPMD1 = 0x10,
	MAX77826_REG_LDO_OPMD2,
	MAX77826_REG_LDO_OPMD3,
	MAX77826_REG_LDO_OPMD4,
	MAX77826_REG_B_BB_OPMD,
	/* 0x15 - 0x1F: Reserved */
	MAX77826_REG_LDO1_CFG = 0x20,
	MAX77826_REG_LDO2_CFG,
	MAX77826_REG_LDO3_CFG,
	MAX77826_REG_LDO4_CFG,
	MAX77826_REG_LDO5_CFG,
	MAX77826_REG_LDO6_CFG,
	MAX77826_REG_LDO7_CFG,
	MAX77826_REG_LDO8_CFG,
	MAX77826_REG_LDO9_CFG,
	MAX77826_REG_LDO10_CFG,
	MAX77826_REG_LDO11_CFG,
	MAX77826_REG_LDO12_CFG,
	MAX77826_REG_LDO13_CFG,
	MAX77826_REG_LDO14_CFG,
	MAX77826_REG_LDO15_CFG,
	/* 0x2F: Reserved */
	MAX77826_REG_BUCK_CFG = 0x30,
	MAX77826_REG_BUCK_VOUT,
	MAX77826_REG_BB_CFG,
	MAX77826_REG_BB_VOUT,
	/* 0x34 - 0x3F: Reserved */
	MAX77826_REG_BUCK_SS_FREQ = 0x40,
	MAX77826_REG_UVLO_FALL,
	/* 0x42 - 0xCE: Reserved */
	MAX77826_REG_DEVICE_ID = 0xCF,
};

enum max77826_regulators {
	MAX77826_LDO1 = 0,
	MAX77826_LDO2,
	MAX77826_LDO3,
	MAX77826_LDO4,
	MAX77826_LDO5,
	MAX77826_LDO6,
	MAX77826_LDO7,
	MAX77826_LDO8,
	MAX77826_LDO9,
	MAX77826_LDO10,
	MAX77826_LDO11,
	MAX77826_LDO12,
	MAX77826_LDO13,
	MAX77826_LDO14,
	MAX77826_LDO15,
	MAX77826_BUCK,
	MAX77826_BUCKBOOST,
	MAX77826_MAX_REGULATORS,
};

#define MAX77826_MASK_LDO		0x7f
#define MAX77826_MASK_BUCK		0xff
#define MAX77826_MASK_BUCKBOOST		0x7f
#define MAX77826_BUCK_RAMP_DELAY	12500

/* values in mV */
/* for LDO1-3 */
#define MAX77826_NMOS_LDO_VOLT_MIN	600000
#define MAX77826_NMOS_LDO_VOLT_MAX	2187500
#define MAX77826_NMOS_LDO_VOLT_STEP	12500

/* for LDO4-15 */
#define MAX77826_PMOS_LDO_VOLT_MIN	800000
#define MAX77826_PMOS_LDO_VOLT_MAX	3975000
#define MAX77826_PMOS_LDO_VOLT_STEP	25000

/* for BUCK */
#define MAX77826_BUCK_VOLT_MIN		500000
#define MAX77826_BUCK_VOLT_MAX		1800000
#define MAX77826_BUCK_VOLT_STEP		6250

/* for BUCKBOOST */
#define MAX77826_BUCKBOOST_VOLT_MIN	2600000
#define MAX77826_BUCKBOOST_VOLT_MAX	4187500
#define MAX77826_BUCKBOOST_VOLT_STEP	12500
#define MAX77826_VOLT_RANGE(_type)					\
	((MAX77826_ ## _type ## _VOLT_MAX -				\
	  MAX77826_ ## _type ## _VOLT_MIN) /				\
	 MAX77826_ ## _type ## _VOLT_STEP + 1)

#define MAX77826_LDO(_id, _type)					\
	[MAX77826_LDO ## _id] = {					\
		.id = MAX77826_LDO ## _id,				\
		.name = "LDO"#_id,					\
		.of_match = of_match_ptr("LDO"#_id),			\
		.regulators_node = "regulators",			\
		.ops = &max77826_most_ops,				\
		.min_uV = MAX77826_ ## _type ## _LDO_VOLT_MIN,		\
		.uV_step = MAX77826_ ## _type ## _LDO_VOLT_STEP,	\
		.n_voltages = MAX77826_VOLT_RANGE(_type ## _LDO),	\
		.enable_reg = MAX77826_REG_LDO_OPMD1 + (_id - 1) / 4,	\
		.enable_mask = BIT(((_id - 1) % 4) * 2 + 1),		\
		.vsel_reg = MAX77826_REG_LDO1_CFG + (_id - 1),		\
		.vsel_mask = MAX77826_MASK_LDO,				\
		.owner = THIS_MODULE,					\
	}

#define MAX77826_BUCK(_idx, _id, _ops)					\
	[MAX77826_ ## _id] = {						\
		.id = MAX77826_ ## _id,					\
		.name = #_id,						\
		.of_match = of_match_ptr(#_id),				\
		.regulators_node = "regulators",			\
		.ops = &_ops,						\
		.min_uV =  MAX77826_ ## _id ## _VOLT_MIN,		\
		.uV_step = MAX77826_ ## _id ## _VOLT_STEP,		\
		.n_voltages = MAX77826_VOLT_RANGE(_id),			\
		.enable_reg = MAX77826_REG_B_BB_OPMD,			\
		.enable_mask = BIT(_idx * 2 + 1),			\
		.vsel_reg = MAX77826_REG_BUCK_VOUT + _idx * 2,		\
		.vsel_mask = MAX77826_MASK_ ## _id,			\
		.owner = THIS_MODULE,					\
	}



struct max77826_regulator_info {
	struct regmap *regmap;
};

static const struct regmap_config max77826_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77826_REG_DEVICE_ID,
};

static int max77826_set_voltage_time_sel(struct regulator_dev *,
				unsigned int old_selector,
				unsigned int new_selector);

static const struct regulator_ops max77826_most_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
};

static const struct regulator_ops max77826_buck_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= max77826_set_voltage_time_sel,
};

static const struct regulator_desc max77826_regulators_desc[] = {
	MAX77826_LDO(1, NMOS),
	MAX77826_LDO(2, NMOS),
	MAX77826_LDO(3, NMOS),
	MAX77826_LDO(4, PMOS),
	MAX77826_LDO(5, PMOS),
	MAX77826_LDO(6, PMOS),
	MAX77826_LDO(7, PMOS),
	MAX77826_LDO(8, PMOS),
	MAX77826_LDO(9, PMOS),
	MAX77826_LDO(10, PMOS),
	MAX77826_LDO(11, PMOS),
	MAX77826_LDO(12, PMOS),
	MAX77826_LDO(13, PMOS),
	MAX77826_LDO(14, PMOS),
	MAX77826_LDO(15, PMOS),
	MAX77826_BUCK(0, BUCK, max77826_buck_ops),
	MAX77826_BUCK(1, BUCKBOOST, max77826_most_ops),
};

static int max77826_set_voltage_time_sel(struct regulator_dev *rdev,
				unsigned int old_selector,
				unsigned int new_selector)
{
	if (new_selector > old_selector) {
		return DIV_ROUND_UP(MAX77826_BUCK_VOLT_STEP *
				(new_selector - old_selector),
				MAX77826_BUCK_RAMP_DELAY);
	}

	return 0;
}

static int max77826_read_device_id(struct regmap *regmap, struct device *dev)
{
	unsigned int device_id;
	int res;

	res = regmap_read(regmap, MAX77826_REG_DEVICE_ID, &device_id);
	if (!res)
		dev_dbg(dev, "DEVICE_ID: 0x%x\n", device_id);

	return res;
}

static int max77826_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max77826_regulator_info *info;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	struct regmap *regmap;
	int i;

	info = devm_kzalloc(dev, sizeof(struct max77826_regulator_info),
				GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &max77826_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to allocate regmap!\n");
		return PTR_ERR(regmap);
	}

	info->regmap = regmap;
	i2c_set_clientdata(client, info);

	config.dev = dev;
	config.regmap = regmap;
	config.driver_data = info;

	for (i = 0; i < MAX77826_MAX_REGULATORS; i++) {
		rdev = devm_regulator_register(dev,
					       &max77826_regulators_desc[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "Failed to register regulator!\n");
			return PTR_ERR(rdev);
		}
	}

	return max77826_read_device_id(regmap, dev);
}

static const struct of_device_id __maybe_unused max77826_of_match[] = {
	{ .compatible = "maxim,max77826" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, max77826_of_match);

static const struct i2c_device_id max77826_id[] = {
	{ "max77826-regulator" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, max77826_id);

static struct i2c_driver max77826_regulator_driver = {
	.driver = {
		.name = "max77826",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(max77826_of_match),
	},
	.probe = max77826_i2c_probe,
	.id_table = max77826_id,
};
module_i2c_driver(max77826_regulator_driver);

MODULE_AUTHOR("Iskren Chernev <iskren.chernev@gmail.com>");
MODULE_DESCRIPTION("MAX77826 PMIC regulator driver");
MODULE_LICENSE("GPL");
