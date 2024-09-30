// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * max1619.c - Part of lm_sensors, Linux kernel modules for hardware
 *             monitoring
 * Copyright (C) 2003-2004 Oleksij Rempel <bug-track@fisher-privat.net>
 *                         Jean Delvare <jdelvare@suse.de>
 *
 * Based on the lm90 driver. The MAX1619 is a sensor chip made by Maxim.
 * It reports up to two temperatures (its own plus up to
 * one external one). Complete datasheet can be
 * obtained from Maxim's website at:
 *   http://pdfserv.maxim-ic.com/en/ds/MAX1619.pdf
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/util_macros.h>

static const unsigned short normal_i2c[] = {
	0x18, 0x19, 0x1a, 0x29, 0x2a, 0x2b, 0x4c, 0x4d, 0x4e, I2C_CLIENT_END };

#define MAX1619_REG_LOCAL_TEMP		0x00
#define MAX1619_REG_REMOTE_TEMP		0x01
#define MAX1619_REG_STATUS		0x02
#define MAX1619_REG_CONFIG		0x03
#define MAX1619_REG_CONVRATE		0x04
#define MAX1619_REG_REMOTE_HIGH		0x07
#define MAX1619_REG_REMOTE_LOW		0x08
#define MAX1619_REG_REMOTE_CRIT		0x10
#define MAX1619_REG_REMOTE_CRIT_HYST	0x11
#define MAX1619_REG_MAN_ID		0xFE
#define MAX1619_REG_CHIP_ID		0xFF

static int get_alarms(struct regmap *regmap)
{
	static u32 regs[2] = { MAX1619_REG_STATUS, MAX1619_REG_CONFIG };
	u8 regdata[2];
	int ret;

	ret = regmap_multi_reg_read(regmap, regs, regdata, 2);
	if (ret)
		return ret;

	/* OVERT status bit may be reversed */
	if (!(regdata[1] & 0x20))
		regdata[0] ^= 0x02;

	return regdata[0] & 0x1e;
}

static int max1619_temp_read(struct regmap *regmap, u32 attr, int channel, long *val)
{
	int reg = -1, alarm_bit = 0;
	u32 temp;
	int ret;

	switch (attr) {
	case hwmon_temp_input:
		reg = channel ? MAX1619_REG_REMOTE_TEMP : MAX1619_REG_LOCAL_TEMP;
		break;
	case hwmon_temp_min:
		reg = MAX1619_REG_REMOTE_LOW;
		break;
	case hwmon_temp_max:
		reg = MAX1619_REG_REMOTE_HIGH;
		break;
	case hwmon_temp_crit:
		reg = MAX1619_REG_REMOTE_CRIT;
		break;
	case hwmon_temp_crit_hyst:
		reg = MAX1619_REG_REMOTE_CRIT_HYST;
		break;
	case hwmon_temp_min_alarm:
		alarm_bit = 3;
		break;
	case hwmon_temp_max_alarm:
		alarm_bit = 4;
		break;
	case hwmon_temp_crit_alarm:
		alarm_bit = 1;
		break;
	case hwmon_temp_fault:
		alarm_bit = 2;
		break;
	default:
		return -EOPNOTSUPP;
	}
	if (reg >= 0) {
		ret = regmap_read(regmap, reg, &temp);
		if (ret < 0)
			return ret;
		*val = sign_extend32(temp, 7) * 1000;
	} else {
		ret = get_alarms(regmap);
		if (ret < 0)
			return ret;
		*val = !!(ret & BIT(alarm_bit));
	}
	return 0;
}

static u16 update_intervals[] = { 16000, 8000, 4000, 2000, 1000, 500, 250, 125 };

static int max1619_chip_read(struct regmap *regmap, u32 attr, long *val)
{
	int alarms, ret;
	u32 regval;

	switch (attr) {
	case hwmon_chip_update_interval:
		ret = regmap_read(regmap, MAX1619_REG_CONVRATE, &regval);
		if (ret < 0)
			return ret;
		*val = update_intervals[regval & 7];
		break;
	case hwmon_chip_alarms:
		alarms = get_alarms(regmap);
		if (alarms < 0)
			return alarms;
		*val = alarms;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int max1619_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct regmap *regmap = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_chip:
		return max1619_chip_read(regmap, attr, val);
	case hwmon_temp:
		return max1619_temp_read(regmap, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int max1619_chip_write(struct regmap *regmap, u32 attr, long val)
{
	switch (attr) {
	case hwmon_chip_update_interval:
		val = find_closest_descending(val, update_intervals, ARRAY_SIZE(update_intervals));
		return regmap_write(regmap, MAX1619_REG_CONVRATE, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int max1619_temp_write(struct regmap *regmap,
			      u32 attr, int channel, long val)
{
	int reg;

	switch (attr) {
	case hwmon_temp_min:
		reg = MAX1619_REG_REMOTE_LOW;
		break;
	case hwmon_temp_max:
		reg = MAX1619_REG_REMOTE_HIGH;
		break;
	case hwmon_temp_crit:
		reg = MAX1619_REG_REMOTE_CRIT;
		break;
	case hwmon_temp_crit_hyst:
		reg = MAX1619_REG_REMOTE_CRIT_HYST;
		break;
	default:
		return -EOPNOTSUPP;
	}
	val = DIV_ROUND_CLOSEST(clamp_val(val, -128000, 127000), 1000);
	return regmap_write(regmap, reg, val);
}

static int max1619_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct regmap *regmap = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_chip:
		return max1619_chip_write(regmap, attr, val);
	case hwmon_temp:
		return max1619_temp_write(regmap, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t max1619_is_visible(const void *_data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return 0644;
		case hwmon_chip_alarms:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		case hwmon_temp_min:
		case hwmon_temp_max:
		case hwmon_temp_crit:
		case hwmon_temp_crit_hyst:
			return 0644;
		case hwmon_temp_min_alarm:
		case hwmon_temp_max_alarm:
		case hwmon_temp_crit_alarm:
		case hwmon_temp_fault:
			return 0444;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info * const max1619_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_ALARMS | HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST |
			   HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM |
			   HWMON_T_CRIT_ALARM | HWMON_T_FAULT),
	NULL
};

static const struct hwmon_ops max1619_hwmon_ops = {
	.is_visible = max1619_is_visible,
	.read = max1619_read,
	.write = max1619_write,
};

static const struct hwmon_chip_info max1619_chip_info = {
	.ops = &max1619_hwmon_ops,
	.info = max1619_info,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int max1619_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int regval;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	regval = i2c_smbus_read_byte_data(client, MAX1619_REG_CONFIG);
	if (regval < 0 || (regval & 0x03))
		return -ENODEV;
	regval = i2c_smbus_read_byte_data(client, MAX1619_REG_CONVRATE);
	if (regval < 0 || regval > 0x07)
		return -ENODEV;
	regval = i2c_smbus_read_byte_data(client, MAX1619_REG_STATUS);
	if (regval < 0 || (regval & 0x61))
		return -ENODEV;

	regval = i2c_smbus_read_byte_data(client, MAX1619_REG_MAN_ID);
	if (regval != 0x4d)
		return -ENODEV;
	regval = i2c_smbus_read_byte_data(client, MAX1619_REG_CHIP_ID);
	if (regval != 0x04)
		return -ENODEV;

	strscpy(info->type, "max1619", I2C_NAME_SIZE);

	return 0;
}

static int max1619_init_chip(struct regmap *regmap)
{
	int ret;

	ret = regmap_write(regmap, MAX1619_REG_CONVRATE, 5);	/* 2 Hz */
	if (ret)
		return ret;

	/* Start conversions */
	return regmap_clear_bits(regmap, MAX1619_REG_CONFIG, 0x40);
}

/* regmap */

static int max1619_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(context, reg);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}

static int max1619_reg_write(void *context, unsigned int reg, unsigned int val)
{
	int offset = reg < MAX1619_REG_REMOTE_CRIT ? 6 : 2;

	return i2c_smbus_write_byte_data(context, reg + offset, val);
}

static bool max1619_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return reg <= MAX1619_REG_STATUS;
}

static bool max1619_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	return reg > MAX1619_REG_STATUS && reg <= MAX1619_REG_REMOTE_CRIT_HYST;
}

static const struct regmap_config max1619_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX1619_REG_REMOTE_CRIT_HYST,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = max1619_regmap_is_volatile,
	.writeable_reg = max1619_regmap_is_writeable,
};

static const struct regmap_bus max1619_regmap_bus = {
	.reg_write = max1619_reg_write,
	.reg_read = max1619_reg_read,
};

static int max1619_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init(dev, &max1619_regmap_bus, client,
				  &max1619_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = max1619_init_chip(regmap);
	if (ret)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 regmap, &max1619_chip_info, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id max1619_id[] = {
	{ "max1619" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max1619_id);

#ifdef CONFIG_OF
static const struct of_device_id max1619_of_match[] = {
	{ .compatible = "maxim,max1619", },
	{},
};

MODULE_DEVICE_TABLE(of, max1619_of_match);
#endif

static struct i2c_driver max1619_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "max1619",
		.of_match_table = of_match_ptr(max1619_of_match),
	},
	.probe		= max1619_probe,
	.id_table	= max1619_id,
	.detect		= max1619_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(max1619_driver);

MODULE_AUTHOR("Oleksij Rempel <bug-track@fisher-privat.net>, Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("MAX1619 sensor driver");
MODULE_LICENSE("GPL");
