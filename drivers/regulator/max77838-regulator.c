// SPDX-License-Identifier: GPL-2.0-or-later
//
// regulator driver for Maxim MAX77838
//
// based on max77826-regulator.c
//
// Copyright (c) 2025, Ivaylo Ivanov <ivo.ivanov.ivanov1@gmail.com>

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

enum max77838_registers {
	MAX77838_REG_DEVICE_ID = 0x00,
	MAX77838_REG_TOPSYS_STAT,
	MAX77838_REG_STAT,
	MAX77838_REG_EN,
	MAX77838_REG_GPIO_PD_CTRL,
	MAX77838_REG_UVLO_CFG1,
	/* 0x06 - 0x0B: reserved */
	MAX77838_REG_I2C_CFG = 0x0C,
	/* 0x0D - 0x0F: reserved */
	MAX77838_REG_LDO1_CFG = 0x10,
	MAX77838_REG_LDO2_CFG,
	MAX77838_REG_LDO3_CFG,
	MAX77838_REG_LDO4_CFG,
	/* 0x14 - 0x1F: reserved */
	MAX77838_REG_BUCK_CFG1 = 0x20,
	MAX77838_REG_BUCK_VOUT,
};

enum max77838_regulators {
	MAX77838_LDO1 = 0,
	MAX77838_LDO2,
	MAX77838_LDO3,
	MAX77838_LDO4,
	MAX77838_BUCK,
	MAX77838_MAX_REGULATORS,
};

#define MAX77838_MASK_LDO		0x7f
#define MAX77838_MASK_BUCK		0xff

#define MAX77838_LDO1_EN		BIT(0)
#define MAX77838_LDO2_EN		BIT(1)
#define MAX77838_LDO3_EN		BIT(2)
#define MAX77838_LDO4_EN		BIT(3)
#define MAX77838_BUCK_EN		BIT(4)

#define MAX77838_BUCK_AD		BIT(3)
#define MAX77838_LDO_AD			BIT(7)

#define MAX77838_LDO_VOLT_MIN		600000
#define MAX77838_LDO_VOLT_MAX		3775000
#define MAX77838_LDO_VOLT_STEP		25000

#define MAX77838_BUCK_VOLT_MIN		500000
#define MAX77838_BUCK_VOLT_MAX		2093750
#define MAX77838_BUCK_VOLT_STEP		6250

#define MAX77838_VOLT_RANGE(_type)				\
	((MAX77838_ ## _type ## _VOLT_MAX -			\
	  MAX77838_ ## _type ## _VOLT_MIN) /			\
	  MAX77838_ ## _type ## _VOLT_STEP + 1)

#define MAX77838_LDO(_id)							\
	[MAX77838_LDO ## _id] = {						\
		.id = MAX77838_LDO ## _id,					\
		.name = "ldo"#_id,						\
		.of_match = of_match_ptr("ldo"#_id),				\
		.regulators_node = "regulators",				\
		.ops = &max77838_regulator_ops,					\
		.min_uV = MAX77838_LDO_VOLT_MIN,				\
		.uV_step = MAX77838_LDO_VOLT_STEP,				\
		.n_voltages = MAX77838_VOLT_RANGE(LDO),				\
		.enable_reg = MAX77838_REG_EN,					\
		.enable_mask = MAX77838_LDO ## _id ## _EN,			\
		.vsel_reg = MAX77838_REG_LDO ## _id ## _CFG,			\
		.vsel_mask = MAX77838_MASK_LDO,					\
		.active_discharge_off = 0,					\
		.active_discharge_on = MAX77838_LDO_AD,				\
		.active_discharge_mask = MAX77838_LDO_AD,			\
		.active_discharge_reg = MAX77838_REG_LDO ## _id ## _CFG,	\
		.owner = THIS_MODULE,						\
	}

#define MAX77838_BUCK_DESC					\
	[MAX77838_BUCK] = {					\
		.id = MAX77838_BUCK,				\
		.name = "buck",					\
		.of_match = of_match_ptr("buck"),		\
		.regulators_node = "regulators",		\
		.ops = &max77838_regulator_ops,			\
		.min_uV = MAX77838_BUCK_VOLT_MIN,		\
		.uV_step = MAX77838_BUCK_VOLT_STEP,		\
		.n_voltages = MAX77838_VOLT_RANGE(BUCK),	\
		.enable_reg = MAX77838_REG_EN,			\
		.enable_mask = MAX77838_BUCK_EN,		\
		.vsel_reg = MAX77838_REG_BUCK_VOUT,		\
		.vsel_mask = MAX77838_MASK_BUCK,		\
		.active_discharge_off = 0,			\
		.active_discharge_on = MAX77838_BUCK_AD,	\
		.active_discharge_mask = MAX77838_BUCK_AD,	\
		.active_discharge_reg = MAX77838_REG_BUCK_CFG1,	\
		.owner = THIS_MODULE,				\
	}

struct max77838_regulator_info {
	struct regmap *regmap;
};

static const struct regmap_config max77838_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77838_REG_BUCK_VOUT,
};

static const struct regulator_ops max77838_regulator_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_active_discharge	= regulator_set_active_discharge_regmap,
};

static const struct regulator_desc max77838_regulators_desc[] = {
	MAX77838_LDO(1),
	MAX77838_LDO(2),
	MAX77838_LDO(3),
	MAX77838_LDO(4),
	MAX77838_BUCK_DESC,
};

static int max77838_read_device_id(struct regmap *regmap, struct device *dev)
{
	unsigned int device_id;
	int ret;

	ret = regmap_read(regmap, MAX77838_REG_DEVICE_ID, &device_id);
	if (!ret)
		dev_dbg(dev, "DEVICE_ID: 0x%x\n", device_id);

	return ret;
}

static int max77838_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max77838_regulator_info *info;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	struct regmap *regmap;
	int i;

	info = devm_kzalloc(dev, sizeof(struct max77838_regulator_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &max77838_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to allocate regmap!\n");
		return PTR_ERR(regmap);
	}

	info->regmap = regmap;
	i2c_set_clientdata(client, info);

	config.dev = dev;
	config.regmap = regmap;
	config.driver_data = info;

	for (i = 0; i < MAX77838_MAX_REGULATORS; i++) {
		rdev = devm_regulator_register(dev,
					       &max77838_regulators_desc[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "Failed to register regulator!\n");
			return PTR_ERR(rdev);
		}
	}

	return max77838_read_device_id(regmap, dev);
}

static const struct of_device_id __maybe_unused max77838_of_match[] = {
	{ .compatible = "maxim,max77838" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, max77838_of_match);

static const struct i2c_device_id max77838_id[] = {
	{ "max77838-regulator" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, max77838_id);

static struct i2c_driver max77838_regulator_driver = {
	.driver = {
		.name = "max77838",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(max77838_of_match),
	},
	.probe = max77838_i2c_probe,
	.id_table = max77838_id,
};
module_i2c_driver(max77838_regulator_driver);

MODULE_AUTHOR("Ivaylo Ivanov <ivo.ivanov.ivanov1@gmail.com>");
MODULE_DESCRIPTION("MAX77838 PMIC regulator driver");
MODULE_LICENSE("GPL");
