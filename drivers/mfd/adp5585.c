// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices ADP5585 I/O expander, PWM controller and keypad controller
 *
 * Copyright 2022 NXP
 * Copyright 2024 Ideas on Board Oy
 * Copyright 2025 Analog Devices Inc.
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

enum {
	ADP5585_DEV_GPIO,
	ADP5585_DEV_PWM,
	ADP5585_DEV_MAX
};

static const struct mfd_cell adp5585_devs[ADP5585_DEV_MAX] = {
	MFD_CELL_NAME("adp5585-gpio"),
	MFD_CELL_NAME("adp5585-pwm"),
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

static const u8 *adp5585_regmap_defaults[ADP5585_MAX] = {
	[ADP5585_00] = adp5585_regmap_defaults_00,
	[ADP5585_01] = adp5585_regmap_defaults_00,
	[ADP5585_02] = adp5585_regmap_defaults_02,
	[ADP5585_03] = adp5585_regmap_defaults_00,
	[ADP5585_04] = adp5585_regmap_defaults_04,
};

static const struct regmap_config adp5585_regmap_config_template = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ADP5585_MAX_REG,
	.volatile_table = &adp5585_volatile_regs,
	.cache_type = REGCACHE_MAPLE,
	.num_reg_defaults_raw = ADP5585_MAX_REG + 1,
};

static struct regmap_config *adp5585_fill_regmap_config(const struct adp5585_dev *adp5585)
{
	struct regmap_config *regmap_config;

	regmap_config = devm_kmemdup(adp5585->dev, &adp5585_regmap_config_template,
				     sizeof(*regmap_config), GFP_KERNEL);
	if (!regmap_config)
		return ERR_PTR(-ENOMEM);

	regmap_config->reg_defaults_raw = adp5585_regmap_defaults[adp5585->variant];
	return regmap_config;
}

static int adp5585_add_devices(struct device *dev)
{
	int ret;

	if (device_property_present(dev, "#pwm-cells")) {
		ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO,
					   &adp5585_devs[ADP5585_DEV_PWM], 1, NULL, 0, NULL);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to add PWM device\n");
	}

	if (device_property_present(dev, "#gpio-cells")) {
		ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO,
					   &adp5585_devs[ADP5585_DEV_GPIO], 1, NULL, 0, NULL);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to add GPIO device\n");
	}

	return 0;
}

static void adp5585_osc_disable(void *data)
{
	const struct adp5585_dev *adp5585 = data;

	regmap_write(adp5585->regmap, ADP5585_GENERAL_CFG, 0);
}

static int adp5585_i2c_probe(struct i2c_client *i2c)
{
	struct regmap_config *regmap_config;
	struct adp5585_dev *adp5585;
	unsigned int id;
	int ret;

	adp5585 = devm_kzalloc(&i2c->dev, sizeof(*adp5585), GFP_KERNEL);
	if (!adp5585)
		return -ENOMEM;

	i2c_set_clientdata(i2c, adp5585);
	adp5585->dev = &i2c->dev;

	adp5585->variant = (enum adp5585_variant)(uintptr_t)i2c_get_match_data(i2c);
	if (!adp5585->variant)
		return -ENODEV;

	regmap_config = adp5585_fill_regmap_config(adp5585);
	if (IS_ERR(regmap_config))
		return PTR_ERR(regmap_config);

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

	/*
	 * Enable the internal oscillator, as it's shared between multiple
	 * functions.
	 */
	ret = regmap_set_bits(adp5585->regmap, ADP5585_GENERAL_CFG, ADP5585_OSC_EN);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&i2c->dev, adp5585_osc_disable, adp5585);
	if (ret)
		return ret;

	return adp5585_add_devices(&i2c->dev);
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
		.data = (void *)ADP5585_00,
	}, {
		.compatible = "adi,adp5585-01",
		.data = (void *)ADP5585_01,
	}, {
		.compatible = "adi,adp5585-02",
		.data = (void *)ADP5585_02,
	}, {
		.compatible = "adi,adp5585-03",
		.data = (void *)ADP5585_03,
	}, {
		.compatible = "adi,adp5585-04",
		.data = (void *)ADP5585_04,
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
