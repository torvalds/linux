// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices ADP5585 I/O expander, PWM controller and keypad controller
 *
 * Copyright 2022 NXP
 * Copyright 2024 Ideas on Board Oy
 */

#include <linux/array_size.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mfd/adp5585.h>
#include <linux/mfd/core.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/types.h>

static const struct mfd_cell adp5585_devs[] = {
	{ .name = "adp5585-gpio", },
	{ .name = "adp5585-pwm", },
};

static const struct regmap_range adp5585_volatile_ranges[] = {
	regmap_reg_range(ADP5585_ID, ADP5585_GPI_STATUS_B),
};

static const struct regmap_access_table adp5585_volatile_regs = {
	.yes_ranges = adp5585_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(adp5585_volatile_ranges),
};

/*
 * Chip variants differ in the default configuration of pull-up and pull-down
 * resistors, and therefore have different default register values:
 *
 * - The -00, -01 and -03 variants (collectively referred to as
 *   ADP5585_REGMAP_00) have pull-up on all GPIO pins by default.
 * - The -02 variant has no default pull-up or pull-down resistors.
 * - The -04 variant has default pull-down resistors on all GPIO pins.
 */

static const u8 adp5585_regmap_defaults_00[ADP5585_MAX_REG + 1] = {
	/* 0x00 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x18 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const u8 adp5585_regmap_defaults_02[ADP5585_MAX_REG + 1] = {
	/* 0x00 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3,
	/* 0x18 */ 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const u8 adp5585_regmap_defaults_04[ADP5585_MAX_REG + 1] = {
	/* 0x00 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x08 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55,
	/* 0x18 */ 0x05, 0x55, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x30 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x38 */ 0x00, 0x00, 0x00, 0x00, 0x00,
};

enum adp5585_regmap_type {
	ADP5585_REGMAP_00,
	ADP5585_REGMAP_02,
	ADP5585_REGMAP_04,
};

static const struct regmap_config adp5585_regmap_configs[] = {
	[ADP5585_REGMAP_00] = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = ADP5585_MAX_REG,
		.volatile_table = &adp5585_volatile_regs,
		.cache_type = REGCACHE_MAPLE,
		.reg_defaults_raw = adp5585_regmap_defaults_00,
		.num_reg_defaults_raw = sizeof(adp5585_regmap_defaults_00),
	},
	[ADP5585_REGMAP_02] = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = ADP5585_MAX_REG,
		.volatile_table = &adp5585_volatile_regs,
		.cache_type = REGCACHE_MAPLE,
		.reg_defaults_raw = adp5585_regmap_defaults_02,
		.num_reg_defaults_raw = sizeof(adp5585_regmap_defaults_02),
	},
	[ADP5585_REGMAP_04] = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = ADP5585_MAX_REG,
		.volatile_table = &adp5585_volatile_regs,
		.cache_type = REGCACHE_MAPLE,
		.reg_defaults_raw = adp5585_regmap_defaults_04,
		.num_reg_defaults_raw = sizeof(adp5585_regmap_defaults_04),
	},
};

static int adp5585_i2c_probe(struct i2c_client *i2c)
{
	const struct regmap_config *regmap_config;
	struct adp5585_dev *adp5585;
	unsigned int id;
	int ret;

	adp5585 = devm_kzalloc(&i2c->dev, sizeof(*adp5585), GFP_KERNEL);
	if (!adp5585)
		return -ENOMEM;

	i2c_set_clientdata(i2c, adp5585);

	regmap_config = i2c_get_match_data(i2c);
	adp5585->regmap = devm_regmap_init_i2c(i2c, regmap_config);
	if (IS_ERR(adp5585->regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(adp5585->regmap),
				     "Failed to initialize register map\n");

	ret = regmap_read(adp5585->regmap, ADP5585_ID, &id);
	if (ret)
		return dev_err_probe(&i2c->dev, ret,
				     "Failed to read device ID\n");

	if ((id & ADP5585_MAN_ID_MASK) != ADP5585_MAN_ID_VALUE)
		return dev_err_probe(&i2c->dev, -ENODEV,
				     "Invalid device ID 0x%02x\n", id);

	ret = devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO,
				   adp5585_devs, ARRAY_SIZE(adp5585_devs),
				   NULL, 0, NULL);
	if (ret)
		return dev_err_probe(&i2c->dev, ret,
				     "Failed to add child devices\n");

	return 0;
}

static int adp5585_suspend(struct device *dev)
{
	struct adp5585_dev *adp5585 = dev_get_drvdata(dev);

	regcache_cache_only(adp5585->regmap, true);

	return 0;
}

static int adp5585_resume(struct device *dev)
{
	struct adp5585_dev *adp5585 = dev_get_drvdata(dev);

	regcache_cache_only(adp5585->regmap, false);
	regcache_mark_dirty(adp5585->regmap);

	return regcache_sync(adp5585->regmap);
}

static DEFINE_SIMPLE_DEV_PM_OPS(adp5585_pm, adp5585_suspend, adp5585_resume);

static const struct of_device_id adp5585_of_match[] = {
	{
		.compatible = "adi,adp5585-00",
		.data = &adp5585_regmap_configs[ADP5585_REGMAP_00],
	}, {
		.compatible = "adi,adp5585-01",
		.data = &adp5585_regmap_configs[ADP5585_REGMAP_00],
	}, {
		.compatible = "adi,adp5585-02",
		.data = &adp5585_regmap_configs[ADP5585_REGMAP_02],
	}, {
		.compatible = "adi,adp5585-03",
		.data = &adp5585_regmap_configs[ADP5585_REGMAP_00],
	}, {
		.compatible = "adi,adp5585-04",
		.data = &adp5585_regmap_configs[ADP5585_REGMAP_04],
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, adp5585_of_match);

static struct i2c_driver adp5585_i2c_driver = {
	.driver = {
		.name = "adp5585",
		.of_match_table = adp5585_of_match,
		.pm = pm_sleep_ptr(&adp5585_pm),
	},
	.probe = adp5585_i2c_probe,
};
module_i2c_driver(adp5585_i2c_driver);

MODULE_DESCRIPTION("ADP5585 core driver");
MODULE_AUTHOR("Haibo Chen <haibo.chen@nxp.com>");
MODULE_LICENSE("GPL");
