// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Texas Instruments / National Semiconductor LM95234
 *
 * Copyright (c) 2013, 2014 Guenter Roeck <linux@roeck-us.net>
 *
 * Derived from lm95241.c
 * Copyright (C) 2008, 2010 Davide Rizzo <elpa.rizzo@gmail.com>
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/util_macros.h>

#define DRVNAME "lm95234"

enum chips { lm95233, lm95234 };

static const unsigned short normal_i2c[] = {
	0x18, 0x2a, 0x2b, 0x4d, 0x4e, I2C_CLIENT_END };

/* LM95234 registers */
#define LM95234_REG_MAN_ID		0xFE
#define LM95234_REG_CHIP_ID		0xFF
#define LM95234_REG_STATUS		0x02
#define LM95234_REG_CONFIG		0x03
#define LM95234_REG_CONVRATE		0x04
#define LM95234_REG_ENABLE		0x05
#define LM95234_REG_FILTER		0x06
#define LM95234_REG_STS_FAULT		0x07
#define LM95234_REG_STS_TCRIT1		0x08
#define LM95234_REG_STS_TCRIT2		0x09
#define LM95234_REG_TEMPH(x)		((x) + 0x10)
#define LM95234_REG_TEMPL(x)		((x) + 0x20)
#define LM95234_REG_UTEMPH(x)		((x) + 0x19)	/* Remote only */
#define LM95234_REG_UTEMPL(x)		((x) + 0x29)
#define LM95234_REG_REM_MODEL		0x30
#define LM95234_REG_REM_MODEL_STS	0x38
#define LM95234_REG_OFFSET(x)		((x) + 0x31)	/* Remote only */
#define LM95234_REG_TCRIT1(x)		((x) + 0x40)
#define LM95234_REG_TCRIT2(x)		((x) + 0x49)	/* Remote channel 1,2 */
#define LM95234_REG_TCRIT_HYST		0x5a

#define NATSEMI_MAN_ID			0x01
#define LM95233_CHIP_ID			0x89
#define LM95234_CHIP_ID			0x79

/* Client data (each client gets its own) */
struct lm95234_data {
	struct regmap *regmap;
	struct mutex update_lock;
	enum chips type;
};

static int lm95234_read_temp(struct regmap *regmap, int index, long *t)
{
	unsigned int regs[2];
	int temp = 0, ret;
	u8 regvals[2];

	if (index) {
		regs[0] = LM95234_REG_UTEMPH(index - 1);
		regs[1] = LM95234_REG_UTEMPL(index - 1);
		ret = regmap_multi_reg_read(regmap, regs, regvals, 2);
		if (ret)
			return ret;
		temp = (regvals[0] << 8) | regvals[1];
	}
	/*
	 * Read signed temperature if unsigned temperature is 0,
	 * or if this is the local sensor.
	 */
	if (!temp) {
		regs[0] = LM95234_REG_TEMPH(index);
		regs[1] = LM95234_REG_TEMPL(index);
		ret = regmap_multi_reg_read(regmap, regs, regvals, 2);
		if (ret)
			return ret;
		temp = (regvals[0] << 8) | regvals[1];
		temp = sign_extend32(temp, 15);
	}
	*t = DIV_ROUND_CLOSEST(temp * 125, 32);
	return 0;
}

static int lm95234_hyst_get(struct regmap *regmap, int reg, long *val)
{
	unsigned int regs[2] = {reg, LM95234_REG_TCRIT_HYST};
	u8 regvals[2];
	int ret;

	ret = regmap_multi_reg_read(regmap, regs, regvals, 2);
	if (ret)
		return ret;
	*val = (regvals[0] - regvals[1]) * 1000;
	return 0;
}

static ssize_t lm95234_hyst_set(struct lm95234_data *data, long val)
{
	u32 tcrit;
	int ret;

	mutex_lock(&data->update_lock);

	ret = regmap_read(data->regmap, LM95234_REG_TCRIT1(0), &tcrit);
	if (ret)
		goto unlock;

	val = DIV_ROUND_CLOSEST(clamp_val(val, -255000, 255000), 1000);
	val = clamp_val((int)tcrit - val, 0, 31);

	ret = regmap_write(data->regmap, LM95234_REG_TCRIT_HYST, val);
unlock:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int lm95234_crit_reg(int channel)
{
	if (channel == 1 || channel == 2)
		return LM95234_REG_TCRIT2(channel - 1);
	return LM95234_REG_TCRIT1(channel);
}

static int lm95234_temp_write(struct device *dev, u32 attr, int channel, long val)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;

	switch (attr) {
	case hwmon_temp_enable:
		if (val && val != 1)
			return -EINVAL;
		return regmap_update_bits(regmap, LM95234_REG_ENABLE,
					  BIT(channel), val ? BIT(channel) : 0);
	case hwmon_temp_type:
		if (val != 1 && val != 2)
			return -EINVAL;
		return regmap_update_bits(regmap, LM95234_REG_REM_MODEL,
					  BIT(channel),
					  val == 1 ? BIT(channel) : 0);
	case hwmon_temp_offset:
		val = DIV_ROUND_CLOSEST(clamp_val(val, -64000, 63500), 500);
		return regmap_write(regmap, LM95234_REG_OFFSET(channel - 1), val);
	case hwmon_temp_max:
		val = clamp_val(val, 0, channel == 1 ? 127000 : 255000);
		val = DIV_ROUND_CLOSEST(val, 1000);
		return regmap_write(regmap, lm95234_crit_reg(channel), val);
	case hwmon_temp_max_hyst:
		return lm95234_hyst_set(data, val);
	case hwmon_temp_crit:
		val = DIV_ROUND_CLOSEST(clamp_val(val, 0, 255000), 1000);
		return regmap_write(regmap, LM95234_REG_TCRIT1(channel), val);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int lm95234_alarm_reg(int channel)
{
	if (channel == 1 || channel == 2)
		return LM95234_REG_STS_TCRIT2;
	return LM95234_REG_STS_TCRIT1;
}

static int lm95234_temp_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u32 regval, mask;
	int ret;

	switch (attr) {
	case hwmon_temp_enable:
		ret = regmap_read(regmap, LM95234_REG_ENABLE, &regval);
		if (ret)
			return ret;
		*val = !!(regval & BIT(channel));
		break;
	case hwmon_temp_input:
		return lm95234_read_temp(regmap, channel, val);
	case hwmon_temp_max_alarm:
		ret =  regmap_read(regmap, lm95234_alarm_reg(channel), &regval);
		if (ret)
			return ret;
		*val = !!(regval & BIT(channel));
		break;
	case hwmon_temp_crit_alarm:
		ret =  regmap_read(regmap, LM95234_REG_STS_TCRIT1, &regval);
		if (ret)
			return ret;
		*val = !!(regval & BIT(channel));
		break;
	case hwmon_temp_crit_hyst:
		return lm95234_hyst_get(regmap, LM95234_REG_TCRIT1(channel), val);
	case hwmon_temp_type:
		ret = regmap_read(regmap, LM95234_REG_REM_MODEL, &regval);
		if (ret)
			return ret;
		*val = (regval & BIT(channel)) ? 1 : 2;
		break;
	case hwmon_temp_offset:
		ret = regmap_read(regmap, LM95234_REG_OFFSET(channel - 1), &regval);
		if (ret)
			return ret;
		*val = sign_extend32(regval, 7) * 500;
		break;
	case hwmon_temp_fault:
		ret = regmap_read(regmap, LM95234_REG_STS_FAULT, &regval);
		if (ret)
			return ret;
		mask = (BIT(0) | BIT(1)) << ((channel - 1) << 1);
		*val = !!(regval & mask);
		break;
	case hwmon_temp_max:
		ret = regmap_read(regmap, lm95234_crit_reg(channel), &regval);
		if (ret)
			return ret;
		*val = regval * 1000;
		break;
	case hwmon_temp_max_hyst:
		return lm95234_hyst_get(regmap, lm95234_crit_reg(channel), val);
	case hwmon_temp_crit:
		ret = regmap_read(regmap, LM95234_REG_TCRIT1(channel), &regval);
		if (ret)
			return ret;
		*val = regval * 1000;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static u16 update_intervals[] = { 143, 364, 1000, 2500 };

static int lm95234_chip_write(struct device *dev, u32 attr, long val)
{
	struct lm95234_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_chip_update_interval:
		val = find_closest(val, update_intervals, ARRAY_SIZE(update_intervals));
		return regmap_write(data->regmap, LM95234_REG_CONVRATE, val);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int lm95234_chip_read(struct device *dev, u32 attr, long *val)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	u32 convrate;
	int ret;

	switch (attr) {
	case hwmon_chip_update_interval:
		ret = regmap_read(data->regmap, LM95234_REG_CONVRATE, &convrate);
		if (ret)
			return ret;

		*val = update_intervals[convrate & 0x03];
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int lm95234_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_chip:
		return lm95234_chip_write(dev, attr, val);
	case hwmon_temp:
		return lm95234_temp_write(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int lm95234_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return lm95234_chip_read(dev, attr, val);
	case hwmon_temp:
		return lm95234_temp_read(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t lm95234_is_visible(const void *_data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	const struct lm95234_data *data = _data;

	if (data->type == lm95233 && channel > 2)
		return 0;

	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return 0644;
		default:
			break;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_max_alarm:
			return 0444;
		case hwmon_temp_crit_alarm:
		case hwmon_temp_crit_hyst:
			return (channel && channel < 3) ? 0444 : 0;
		case hwmon_temp_type:
		case hwmon_temp_offset:
			return channel ? 0644 : 0;
		case hwmon_temp_fault:
			return channel ? 0444 : 0;
		case hwmon_temp_max:
		case hwmon_temp_enable:
			return 0644;
		case hwmon_temp_max_hyst:
			return channel ? 0444 : 0644;
		case hwmon_temp_crit:
			return (channel && channel < 3) ? 0644 : 0;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info * const lm95234_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_MAX_ALARM | HWMON_T_ENABLE,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_MAX_ALARM | HWMON_T_FAULT | HWMON_T_TYPE |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST |
			   HWMON_T_CRIT_ALARM | HWMON_T_OFFSET | HWMON_T_ENABLE,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_MAX_ALARM | HWMON_T_FAULT | HWMON_T_TYPE |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST |
			   HWMON_T_CRIT_ALARM | HWMON_T_OFFSET | HWMON_T_ENABLE,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_MAX_ALARM | HWMON_T_FAULT | HWMON_T_TYPE |
			   HWMON_T_OFFSET | HWMON_T_ENABLE,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_MAX_ALARM | HWMON_T_FAULT | HWMON_T_TYPE |
			   HWMON_T_OFFSET | HWMON_T_ENABLE),
	NULL
};

static const struct hwmon_ops lm95234_hwmon_ops = {
	.is_visible = lm95234_is_visible,
	.read = lm95234_read,
	.write = lm95234_write,
};

static const struct hwmon_chip_info lm95234_chip_info = {
	.ops = &lm95234_hwmon_ops,
	.info = lm95234_info,
};

static bool lm95234_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LM95234_REG_TEMPH(0) ... LM95234_REG_TEMPH(4):
	case LM95234_REG_TEMPL(0) ... LM95234_REG_TEMPL(4):
	case LM95234_REG_UTEMPH(0) ... LM95234_REG_UTEMPH(3):
	case LM95234_REG_UTEMPL(0) ... LM95234_REG_UTEMPL(3):
	case LM95234_REG_STS_FAULT:
	case LM95234_REG_STS_TCRIT1:
	case LM95234_REG_STS_TCRIT2:
	case LM95234_REG_REM_MODEL_STS:
		return true;
	default:
		return false;
	}
}

static bool lm95234_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LM95234_REG_CONFIG ... LM95234_REG_FILTER:
	case LM95234_REG_REM_MODEL ... LM95234_REG_OFFSET(3):
	case LM95234_REG_TCRIT1(0) ... LM95234_REG_TCRIT1(4):
	case LM95234_REG_TCRIT2(0) ... LM95234_REG_TCRIT2(1):
	case LM95234_REG_TCRIT_HYST:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config lm95234_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = lm95234_writeable_reg,
	.volatile_reg = lm95234_volatile_reg,
	.cache_type = REGCACHE_MAPLE,
};

static int lm95234_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int address = client->addr;
	u8 config_mask, model_mask;
	int mfg_id, chip_id, val;
	const char *name;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	mfg_id = i2c_smbus_read_byte_data(client, LM95234_REG_MAN_ID);
	if (mfg_id != NATSEMI_MAN_ID)
		return -ENODEV;

	chip_id = i2c_smbus_read_byte_data(client, LM95234_REG_CHIP_ID);
	switch (chip_id) {
	case LM95233_CHIP_ID:
		if (address != 0x18 && address != 0x2a && address != 0x2b)
			return -ENODEV;
		config_mask = 0xbf;
		model_mask = 0xf9;
		name = "lm95233";
		break;
	case LM95234_CHIP_ID:
		if (address != 0x18 && address != 0x4d && address != 0x4e)
			return -ENODEV;
		config_mask = 0xbc;
		model_mask = 0xe1;
		name = "lm95234";
		break;
	default:
		return -ENODEV;
	}

	val = i2c_smbus_read_byte_data(client, LM95234_REG_STATUS);
	if (val & 0x30)
		return -ENODEV;

	val = i2c_smbus_read_byte_data(client, LM95234_REG_CONFIG);
	if (val & config_mask)
		return -ENODEV;

	val = i2c_smbus_read_byte_data(client, LM95234_REG_CONVRATE);
	if (val & 0xfc)
		return -ENODEV;

	val = i2c_smbus_read_byte_data(client, LM95234_REG_REM_MODEL);
	if (val & model_mask)
		return -ENODEV;

	val = i2c_smbus_read_byte_data(client, LM95234_REG_REM_MODEL_STS);
	if (val & model_mask)
		return -ENODEV;

	strscpy(info->type, name, I2C_NAME_SIZE);
	return 0;
}

static int lm95234_init_client(struct device *dev, struct regmap *regmap)
{
	u32 val, model;
	int ret;

	/* start conversion if necessary */
	ret = regmap_clear_bits(regmap, LM95234_REG_CONFIG, 0x40);
	if (ret)
		return ret;

	/* If diode type status reports an error, try to fix it */
	ret = regmap_read(regmap, LM95234_REG_REM_MODEL_STS, &val);
	if (ret < 0)
		return ret;
	ret = regmap_read(regmap, LM95234_REG_REM_MODEL, &model);
	if (ret < 0)
		return ret;
	if (model & val) {
		dev_notice(dev,
			   "Fixing remote diode type misconfiguration (0x%x)\n",
			   val);
		ret = regmap_write(regmap, LM95234_REG_REM_MODEL, model & ~val);
	}
	return ret;
}

static int lm95234_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct lm95234_data *data;
	struct device *hwmon_dev;
	struct regmap *regmap;
	int err;

	data = devm_kzalloc(dev, sizeof(struct lm95234_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->type = (uintptr_t)i2c_get_match_data(client);

	regmap = devm_regmap_init_i2c(client, &lm95234_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	data->regmap = regmap;
	mutex_init(&data->update_lock);

	/* Initialize the LM95234 chip */
	err = lm95234_init_client(dev, regmap);
	if (err < 0)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data, &lm95234_chip_info, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/* Driver data (common to all clients) */
static const struct i2c_device_id lm95234_id[] = {
	{ "lm95233", lm95233 },
	{ "lm95234", lm95234 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm95234_id);

static struct i2c_driver lm95234_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= DRVNAME,
	},
	.probe		= lm95234_probe,
	.id_table	= lm95234_id,
	.detect		= lm95234_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm95234_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("LM95233/LM95234 sensor driver");
MODULE_LICENSE("GPL");
