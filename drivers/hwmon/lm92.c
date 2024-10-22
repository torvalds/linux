// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lm92 - Hardware monitoring driver
 * Copyright (C) 2005-2008  Jean Delvare <jdelvare@suse.de>
 *
 * Based on the lm90 driver, with some ideas taken from the lm_sensors
 * lm92 driver as well.
 *
 * The LM92 is a sensor chip made by National Semiconductor. It reports
 * its own temperature with a 0.0625 deg resolution and a 0.33 deg
 * accuracy. Complete datasheet can be obtained from National's website
 * at:
 *   http://www.national.com/pf/LM/LM92.html
 *
 * This driver also supports the MAX6635 sensor chip made by Maxim.
 * This chip is compatible with the LM92, but has a lesser accuracy
 * (1.0 deg). Complete datasheet can be obtained from Maxim's website
 * at:
 *   http://www.maxim-ic.com/quick_view2.cfm/qv_pk/3074
 *
 * Since the LM92 was the first chipset supported by this driver, most
 * comments will refer to this chipset, but are actually general and
 * concern all supported chipsets, unless mentioned otherwise.
 *
 * Support could easily be added for the National Semiconductor LM76
 * and Maxim MAX6633 and MAX6634 chips, which are mostly compatible
 * with the LM92.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/*
 * The LM92 and MAX6635 have 2 two-state pins for address selection,
 * resulting in 4 possible addresses.
 */
static const unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4b,
						I2C_CLIENT_END };
/* The LM92 registers */
#define LM92_REG_CONFIG			0x01 /* 8-bit, RW */
#define LM92_REG_TEMP			0x00 /* 16-bit, RO */
#define LM92_REG_TEMP_HYST		0x02 /* 16-bit, RW */
#define LM92_REG_TEMP_CRIT		0x03 /* 16-bit, RW */
#define LM92_REG_TEMP_LOW		0x04 /* 16-bit, RW */
#define LM92_REG_TEMP_HIGH		0x05 /* 16-bit, RW */
#define LM92_REG_MAN_ID			0x07 /* 16-bit, RO, LM92 only */

/*
 * The LM92 uses signed 13-bit values with LSB = 0.0625 degree Celsius,
 * left-justified in 16-bit registers. No rounding is done, with such
 * a resolution it's just not worth it. Note that the MAX6635 doesn't
 * make use of the 4 lower bits for limits (i.e. effective resolution
 * for limits is 1 degree Celsius).
 */
static inline int TEMP_FROM_REG(s16 reg)
{
	return reg / 8 * 625 / 10;
}

static inline s16 TEMP_TO_REG(long val, int resolution)
{
	val = clamp_val(val, -60000, 160000);
	return DIV_ROUND_CLOSEST(val << (resolution - 9), 1000) << (16 - resolution);
}

/* Alarm flags are stored in the 3 LSB of the temperature register */
static inline u8 ALARMS_FROM_REG(s16 reg)
{
	return reg & 0x0007;
}

/* Client data (each client gets its own) */
struct lm92_data {
	struct regmap *regmap;
	struct mutex update_lock;
	int resolution;
};

static int lm92_temp_read(struct lm92_data *data, u32 attr, int channel, long *val)
{
	int reg = -1, hyst_reg = -1, alarm_bit = 0;
	struct regmap *regmap = data->regmap;
	u32 temp;
	int ret;

	switch (attr) {
	case hwmon_temp_input:
		reg = LM92_REG_TEMP;
		break;
	case hwmon_temp_min:
		reg = LM92_REG_TEMP_LOW;
		break;
	case hwmon_temp_max:
		reg = LM92_REG_TEMP_HIGH;
		break;
	case hwmon_temp_crit:
		reg = LM92_REG_TEMP_CRIT;
		break;
	case hwmon_temp_min_hyst:
		hyst_reg = LM92_REG_TEMP_LOW;
		break;
	case hwmon_temp_max_hyst:
		hyst_reg = LM92_REG_TEMP_HIGH;
		break;
	case hwmon_temp_crit_hyst:
		hyst_reg = LM92_REG_TEMP_CRIT;
		break;
	case hwmon_temp_min_alarm:
		alarm_bit = 0;
		break;
	case hwmon_temp_max_alarm:
		alarm_bit = 1;
		break;
	case hwmon_temp_crit_alarm:
		alarm_bit = 2;
		break;
	default:
		return -EOPNOTSUPP;
	}
	if (reg >= 0) {
		ret = regmap_read(regmap, reg, &temp);
		if (ret < 0)
			return ret;
		*val = TEMP_FROM_REG(temp);
	} else if (hyst_reg >= 0) {
		u32 regs[2] = { hyst_reg, LM92_REG_TEMP_HYST };
		u16 regvals[2];

		ret = regmap_multi_reg_read(regmap, regs, regvals, 2);
		if (ret)
			return ret;
		if (attr == hwmon_temp_min_hyst)
			*val = TEMP_FROM_REG(regvals[0]) + TEMP_FROM_REG(regvals[1]);
		else
			*val = TEMP_FROM_REG(regvals[0]) - TEMP_FROM_REG(regvals[1]);
	} else {
		ret = regmap_read(regmap, LM92_REG_TEMP, &temp);
		if (ret)
			return ret;
		*val = !!(temp & BIT(alarm_bit));
	}
	return 0;
}

static int lm92_chip_read(struct lm92_data *data, u32 attr, long *val)
{
	u32 temp;
	int ret;

	switch (attr) {
	case hwmon_chip_alarms:
		ret = regmap_read(data->regmap, LM92_REG_TEMP, &temp);
		if (ret)
			return ret;
		*val = ALARMS_FROM_REG(temp);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int lm92_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
		     int channel, long *val)
{
	struct lm92_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_chip:
		return lm92_chip_read(data, attr, val);
	case hwmon_temp:
		return lm92_temp_read(data, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int lm92_temp_write(struct lm92_data *data, u32 attr, long val)
{
	struct regmap *regmap = data->regmap;
	int reg, err;
	u32 temp;

	switch (attr) {
	case hwmon_temp_min:
		reg = LM92_REG_TEMP_LOW;
		break;
	case hwmon_temp_max:
		reg = LM92_REG_TEMP_HIGH;
		break;
	case hwmon_temp_crit:
		reg = LM92_REG_TEMP_CRIT;
		break;
	case hwmon_temp_crit_hyst:
		val = clamp_val(val, -120000, 220000);
		mutex_lock(&data->update_lock);
		err = regmap_read(regmap, LM92_REG_TEMP_CRIT, &temp);
		if (err)
			goto unlock;
		val = TEMP_TO_REG(TEMP_FROM_REG(temp) - val, data->resolution);
		err = regmap_write(regmap, LM92_REG_TEMP_HYST, val);
unlock:
		mutex_unlock(&data->update_lock);
		return err;
	default:
		return -EOPNOTSUPP;
	}
	return regmap_write(regmap, reg, TEMP_TO_REG(val, data->resolution));
}

static int lm92_write(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long val)
{
	struct lm92_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		return lm92_temp_write(data, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t lm92_is_visible(const void *_data, enum hwmon_sensor_types type,
			       u32 attr, int channel)
{
	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_alarms:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_min:
		case hwmon_temp_max:
		case hwmon_temp_crit:
		case hwmon_temp_crit_hyst:
			return 0644;
		case hwmon_temp_input:
		case hwmon_temp_min_hyst:
		case hwmon_temp_max_hyst:
		case hwmon_temp_min_alarm:
		case hwmon_temp_max_alarm:
		case hwmon_temp_crit_alarm:
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

static const struct hwmon_channel_info * const lm92_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_ALARMS),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT |
			   HWMON_T_MIN | HWMON_T_MIN_HYST |
			   HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST |
			   HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM |
			   HWMON_T_CRIT_ALARM),
	NULL
};

static const struct hwmon_ops lm92_hwmon_ops = {
	.is_visible = lm92_is_visible,
	.read = lm92_read,
	.write = lm92_write,
};

static const struct hwmon_chip_info lm92_chip_info = {
	.ops = &lm92_hwmon_ops,
	.info = lm92_info,
};

/*
 * Detection and registration
 */

static int lm92_init_client(struct regmap *regmap)
{
	return regmap_clear_bits(regmap, LM92_REG_CONFIG, 0x01);
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm92_detect(struct i2c_client *new_client,
		       struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	u8 config_addr = LM92_REG_CONFIG;
	u8 man_id_addr = LM92_REG_MAN_ID;
	int i, regval;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA
					    | I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	/*
	 * Register values repeat with multiples of 8.
	 * Read twice to improve detection accuracy.
	 */
	for (i = 0; i < 2; i++) {
		regval = i2c_smbus_read_word_data(new_client, man_id_addr);
		if (regval != 0x0180)
			return -ENODEV;
		regval = i2c_smbus_read_byte_data(new_client, config_addr);
		if (regval < 0 || (regval & 0xe0))
			return -ENODEV;
		config_addr += 8;
		man_id_addr += 8;
	}

	strscpy(info->type, "lm92", I2C_NAME_SIZE);

	return 0;
}

/* regmap */

static int lm92_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	int ret;

	if (reg == LM92_REG_CONFIG)
		ret = i2c_smbus_read_byte_data(context, reg);
	else
		ret = i2c_smbus_read_word_swapped(context, reg);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}

static int lm92_reg_write(void *context, unsigned int reg, unsigned int val)
{
	if (reg == LM92_REG_CONFIG)
		return i2c_smbus_write_byte_data(context, LM92_REG_CONFIG, val);

	return i2c_smbus_write_word_swapped(context, reg, val);
}

static bool lm92_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return reg == LM92_REG_TEMP;
}

static bool lm92_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	return reg >= LM92_REG_CONFIG;
}

static const struct regmap_config lm92_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = LM92_REG_TEMP_HIGH,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = lm92_regmap_is_volatile,
	.writeable_reg = lm92_regmap_is_writeable,
};

static const struct regmap_bus lm92_regmap_bus = {
	.reg_write = lm92_reg_write,
	.reg_read = lm92_reg_read,
};

static int lm92_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct lm92_data *data;
	struct regmap *regmap;
	int err;

	regmap = devm_regmap_init(dev, &lm92_regmap_bus, client,
				  &lm92_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	data = devm_kzalloc(dev, sizeof(struct lm92_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = regmap;
	data->resolution = (unsigned long)i2c_get_match_data(client);
	mutex_init(&data->update_lock);

	/* Initialize the chipset */
	err = lm92_init_client(regmap);
	if (err)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data,
							 &lm92_chip_info, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/*
 * Module and driver stuff
 */

/* .driver_data is limit register resolution */ 
static const struct i2c_device_id lm92_id[] = {
	{ "lm92", 13 },
	{ "max6635", 9 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm92_id);

static struct i2c_driver lm92_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm92",
	},
	.probe		= lm92_probe,
	.id_table	= lm92_id,
	.detect		= lm92_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm92_driver);

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("LM92/MAX6635 driver");
MODULE_LICENSE("GPL");
