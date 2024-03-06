// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Texas Instruments TMP103 SMBus temperature sensor driver
 * Copyright (C) 2014 Heiko Schocher <hs@denx.de>
 *
 * Based on:
 * Texas Instruments TMP102 SMBus temperature sensor driver
 *
 * Copyright (C) 2010 Steven King <sfking@fdwdc.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/regmap.h>

#define TMP103_TEMP_REG		0x00
#define TMP103_CONF_REG		0x01
#define TMP103_TLOW_REG		0x02
#define TMP103_THIGH_REG	0x03

#define TMP103_CONF_M0		0x01
#define TMP103_CONF_M1		0x02
#define TMP103_CONF_LC		0x04
#define TMP103_CONF_FL		0x08
#define TMP103_CONF_FH		0x10
#define TMP103_CONF_CR0		0x20
#define TMP103_CONF_CR1		0x40
#define TMP103_CONF_ID		0x80
#define TMP103_CONF_SD		(TMP103_CONF_M1)
#define TMP103_CONF_SD_MASK	(TMP103_CONF_M0 | TMP103_CONF_M1)

#define TMP103_CONFIG		(TMP103_CONF_CR1 | TMP103_CONF_M1)
#define TMP103_CONFIG_MASK	(TMP103_CONF_CR0 | TMP103_CONF_CR1 | \
				 TMP103_CONF_M0 | TMP103_CONF_M1)

static inline int tmp103_reg_to_mc(s8 val)
{
	return val * 1000;
}

static inline u8 tmp103_mc_to_reg(int val)
{
	return DIV_ROUND_CLOSEST(val, 1000);
}

static int tmp103_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *temp)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	unsigned int regval;
	int err, reg;

	switch (attr) {
	case hwmon_temp_input:
		reg = TMP103_TEMP_REG;
		break;
	case hwmon_temp_min:
		reg = TMP103_TLOW_REG;
		break;
	case hwmon_temp_max:
		reg = TMP103_THIGH_REG;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = regmap_read(regmap, reg, &regval);
	if (err < 0)
		return err;

	*temp = tmp103_reg_to_mc(regval);

	return 0;
}

static int tmp103_write(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long temp)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	int reg;

	switch (attr) {
	case hwmon_temp_min:
		reg = TMP103_TLOW_REG;
		break;
	case hwmon_temp_max:
		reg = TMP103_THIGH_REG;
		break;
	default:
		return -EOPNOTSUPP;
	}

	temp = clamp_val(temp, -55000, 127000);
	return regmap_write(regmap, reg, tmp103_mc_to_reg(temp));
}

static umode_t tmp103_is_visible(const void *data, enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	case hwmon_temp_min:
	case hwmon_temp_max:
		return 0644;
	default:
		return 0;
	}
}

static const struct hwmon_channel_info * const tmp103_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MIN),
	NULL
};

static const struct hwmon_ops tmp103_hwmon_ops = {
	.is_visible = tmp103_is_visible,
	.read = tmp103_read,
	.write = tmp103_write,
};

static const struct hwmon_chip_info tmp103_chip_info = {
	.ops = &tmp103_hwmon_ops,
	.info = tmp103_info,
};

static bool tmp103_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return reg == TMP103_TEMP_REG;
}

static const struct regmap_config tmp103_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TMP103_THIGH_REG,
	.volatile_reg = tmp103_regmap_is_volatile,
};

static int tmp103_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_i2c(client, &tmp103_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(regmap);
	}

	ret = regmap_update_bits(regmap, TMP103_CONF_REG, TMP103_CONFIG_MASK,
				 TMP103_CONFIG);
	if (ret < 0) {
		dev_err(&client->dev, "error writing config register\n");
		return ret;
	}

	i2c_set_clientdata(client, regmap);
	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 regmap,
							 &tmp103_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static int tmp103_suspend(struct device *dev)
{
	struct regmap *regmap = dev_get_drvdata(dev);

	return regmap_update_bits(regmap, TMP103_CONF_REG,
				  TMP103_CONF_SD_MASK, 0);
}

static int tmp103_resume(struct device *dev)
{
	struct regmap *regmap = dev_get_drvdata(dev);

	return regmap_update_bits(regmap, TMP103_CONF_REG,
				  TMP103_CONF_SD_MASK, TMP103_CONF_SD);
}

static DEFINE_SIMPLE_DEV_PM_OPS(tmp103_dev_pm_ops, tmp103_suspend, tmp103_resume);

static const struct i2c_device_id tmp103_id[] = {
	{ "tmp103", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tmp103_id);

static const struct of_device_id __maybe_unused tmp103_of_match[] = {
	{ .compatible = "ti,tmp103" },
	{ },
};
MODULE_DEVICE_TABLE(of, tmp103_of_match);

static struct i2c_driver tmp103_driver = {
	.driver = {
		.name	= "tmp103",
		.of_match_table = of_match_ptr(tmp103_of_match),
		.pm	= pm_sleep_ptr(&tmp103_dev_pm_ops),
	},
	.probe		= tmp103_probe,
	.id_table	= tmp103_id,
};

module_i2c_driver(tmp103_driver);

MODULE_AUTHOR("Heiko Schocher <hs@denx.de>");
MODULE_DESCRIPTION("Texas Instruments TMP103 temperature sensor driver");
MODULE_LICENSE("GPL");
