// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2012 Samsung Electronics Co., Ltd
 *                http://www.samsung.com
 * Copyright 2025 Linaro Ltd.
 *
 * Samsung SxM I2C driver
 */

#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mpa01.h>
#include <linux/mfd/samsung/s2mps11.h>
#include <linux/mfd/samsung/s2mps13.h>
#include <linux/mfd/samsung/s2mps14.h>
#include <linux/mfd/samsung/s2mps15.h>
#include <linux/mfd/samsung/s2mpu02.h>
#include <linux/mfd/samsung/s5m8767.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include "sec-core.h"

struct sec_pmic_i2c_platform_data {
	const struct regmap_config *regmap_cfg;
	int device_type;
};

static bool s2mpa01_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case S2MPA01_REG_INT1M:
	case S2MPA01_REG_INT2M:
	case S2MPA01_REG_INT3M:
		return false;
	default:
		return true;
	}
}

static bool s2mps11_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case S2MPS11_REG_INT1M:
	case S2MPS11_REG_INT2M:
	case S2MPS11_REG_INT3M:
		return false;
	default:
		return true;
	}
}

static bool s2mpu02_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case S2MPU02_REG_INT1M:
	case S2MPU02_REG_INT2M:
	case S2MPU02_REG_INT3M:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config s2dos05_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct regmap_config s2mpa01_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPA01_REG_LDO_OVCB4,
	.volatile_reg = s2mpa01_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s2mps11_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPS11_REG_L38CTRL,
	.volatile_reg = s2mps11_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s2mps13_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPS13_REG_LDODSCH5,
	.volatile_reg = s2mps11_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s2mps14_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPS14_REG_LDODSCH3,
	.volatile_reg = s2mps11_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s2mps15_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPS15_REG_LDODSCH4,
	.volatile_reg = s2mps11_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s2mpu02_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S2MPU02_REG_DVSDATA,
	.volatile_reg = s2mpu02_volatile,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config s2mpu05_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct regmap_config s5m8767_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = S5M8767_REG_LDO28CTRL,
	.volatile_reg = s2mps11_volatile,
	.cache_type = REGCACHE_FLAT,
};

static int sec_pmic_i2c_probe(struct i2c_client *client)
{
	const struct sec_pmic_i2c_platform_data *pdata;
	struct regmap *regmap_pmic;

	pdata = device_get_match_data(&client->dev);
	if (!pdata)
		return dev_err_probe(&client->dev, -ENODEV,
				     "Unsupported device type\n");

	regmap_pmic = devm_regmap_init_i2c(client, pdata->regmap_cfg);
	if (IS_ERR(regmap_pmic))
		return dev_err_probe(&client->dev, PTR_ERR(regmap_pmic),
				     "regmap init failed\n");

	return sec_pmic_probe(&client->dev, pdata->device_type, client->irq,
			      regmap_pmic, client);
}

static void sec_pmic_i2c_shutdown(struct i2c_client *i2c)
{
	sec_pmic_shutdown(&i2c->dev);
}

static const struct sec_pmic_i2c_platform_data s2dos05_data = {
	.regmap_cfg = &s2dos05_regmap_config,
	.device_type = S2DOS05
};

static const struct sec_pmic_i2c_platform_data s2mpa01_data = {
	.regmap_cfg = &s2mpa01_regmap_config,
	.device_type = S2MPA01,
};

static const struct sec_pmic_i2c_platform_data s2mps11_data = {
	.regmap_cfg = &s2mps11_regmap_config,
	.device_type = S2MPS11X,
};

static const struct sec_pmic_i2c_platform_data s2mps13_data = {
	.regmap_cfg = &s2mps13_regmap_config,
	.device_type = S2MPS13X,
};

static const struct sec_pmic_i2c_platform_data s2mps14_data = {
	.regmap_cfg = &s2mps14_regmap_config,
	.device_type = S2MPS14X,
};

static const struct sec_pmic_i2c_platform_data s2mps15_data = {
	.regmap_cfg = &s2mps15_regmap_config,
	.device_type = S2MPS15X,
};

static const struct sec_pmic_i2c_platform_data s2mpu02_data = {
	.regmap_cfg = &s2mpu02_regmap_config,
	.device_type = S2MPU02,
};

static const struct sec_pmic_i2c_platform_data s2mpu05_data = {
	.regmap_cfg = &s2mpu05_regmap_config,
	.device_type = S2MPU05,
};

static const struct sec_pmic_i2c_platform_data s5m8767_data = {
	.regmap_cfg = &s5m8767_regmap_config,
	.device_type = S5M8767X,
};

static const struct of_device_id sec_pmic_i2c_of_match[] = {
	{ .compatible = "samsung,s2dos05", .data = &s2dos05_data, },
	{ .compatible = "samsung,s2mpa01-pmic", .data = &s2mpa01_data, },
	{ .compatible = "samsung,s2mps11-pmic", .data = &s2mps11_data, },
	{ .compatible = "samsung,s2mps13-pmic", .data = &s2mps13_data, },
	{ .compatible = "samsung,s2mps14-pmic", .data = &s2mps14_data, },
	{ .compatible = "samsung,s2mps15-pmic", .data = &s2mps15_data, },
	{ .compatible = "samsung,s2mpu02-pmic", .data = &s2mpu02_data, },
	{ .compatible = "samsung,s2mpu05-pmic", .data = &s2mpu05_data, },
	{ .compatible = "samsung,s5m8767-pmic", .data = &s5m8767_data, },
	{ },
};
MODULE_DEVICE_TABLE(of, sec_pmic_i2c_of_match);

static struct i2c_driver sec_pmic_i2c_driver = {
	.driver = {
		.name = "sec-pmic-i2c",
		.pm = pm_sleep_ptr(&sec_pmic_pm_ops),
		.of_match_table = sec_pmic_i2c_of_match,
	},
	.probe = sec_pmic_i2c_probe,
	.shutdown = sec_pmic_i2c_shutdown,
};
module_i2c_driver(sec_pmic_i2c_driver);

MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_AUTHOR("André Draszik <andre.draszik@linaro.org>");
MODULE_DESCRIPTION("I2C driver for the Samsung S5M");
MODULE_LICENSE("GPL");
