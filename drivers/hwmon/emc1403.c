// SPDX-License-Identifier: GPL-2.0-only
/*
 * emc1403.c - SMSC Thermal Driver
 *
 * Copyright (C) 2008 Intel Corp
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/util_macros.h>

#define THERMAL_PID_REG		0xfd
#define THERMAL_SMSC_ID_REG	0xfe
#define THERMAL_REVISION_REG	0xff

enum emc1403_chip { emc1402, emc1403, emc1404, emc1428 };

struct thermal_data {
	enum emc1403_chip chip;
	struct regmap *regmap;
	struct mutex mutex;
};

static ssize_t power_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int retval;

	retval = regmap_read(data->regmap, 0x03, &val);
	if (retval < 0)
		return retval;
	return sprintf(buf, "%d\n", !!(val & BIT(6)));
}

static ssize_t power_state_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct thermal_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int retval;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	retval = regmap_update_bits(data->regmap, 0x03, BIT(6),
				    val ? BIT(6) : 0);
	if (retval < 0)
		return retval;
	return count;
}

static DEVICE_ATTR_RW(power_state);

static struct attribute *emc1403_attrs[] = {
	&dev_attr_power_state.attr,
	NULL
};
ATTRIBUTE_GROUPS(emc1403);

static int emc1403_detect(struct i2c_client *client,
			struct i2c_board_info *info)
{
	int id;
	/* Check if thermal chip is SMSC and EMC1403 or EMC1423 */

	id = i2c_smbus_read_byte_data(client, THERMAL_SMSC_ID_REG);
	if (id != 0x5d)
		return -ENODEV;

	id = i2c_smbus_read_byte_data(client, THERMAL_PID_REG);
	switch (id) {
	case 0x20:
		strscpy(info->type, "emc1402", I2C_NAME_SIZE);
		break;
	case 0x21:
		strscpy(info->type, "emc1403", I2C_NAME_SIZE);
		break;
	case 0x22:
		strscpy(info->type, "emc1422", I2C_NAME_SIZE);
		break;
	case 0x23:
		strscpy(info->type, "emc1423", I2C_NAME_SIZE);
		break;
	case 0x25:
		strscpy(info->type, "emc1404", I2C_NAME_SIZE);
		break;
	case 0x27:
		strscpy(info->type, "emc1424", I2C_NAME_SIZE);
		break;
	case 0x29:
		strscpy(info->type, "emc1428", I2C_NAME_SIZE);
		break;
	case 0x59:
		strscpy(info->type, "emc1438", I2C_NAME_SIZE);
		break;
	case 0x60:
		strscpy(info->type, "emc1442", I2C_NAME_SIZE);
		break;
	default:
		return -ENODEV;
	}

	id = i2c_smbus_read_byte_data(client, THERMAL_REVISION_REG);
	if (id < 0x01 || id > 0x04)
		return -ENODEV;

	return 0;
}

static bool emc1403_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00:	/* internal diode high byte */
	case 0x01:	/* external diode 1 high byte */
	case 0x02:	/* status */
	case 0x10:	/* external diode 1 low byte */
	case 0x1b:	/* external diode fault */
	case 0x23:	/* external diode 2 high byte */
	case 0x24:	/* external diode 2 low byte */
	case 0x29:	/* internal diode low byte */
	case 0x2a:	/* externl diode 3 high byte */
	case 0x2b:	/* external diode 3 low byte */
	case 0x35:	/* high limit status */
	case 0x36:	/* low limit status */
	case 0x37:	/* therm limit status */
	case 0x41:	/* external diode 4 high byte */
	case 0x42:	/* external diode 4 low byte */
	case 0x43:	/* external diode 5 high byte */
	case 0x44:	/* external diode 5 low byte */
	case 0x45:	/* external diode 6 high byte */
	case 0x46:	/* external diode 6 low byte */
	case 0x47:	/* external diode 7 high byte */
	case 0x48:	/* external diode 7 low byte */
		return true;
	default:
		return false;
	}
}

static const struct regmap_config emc1403_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = emc1403_regmap_is_volatile,
};

enum emc1403_reg_map {temp_min, temp_max, temp_crit, temp_input};

static u8 ema1403_temp_map[] = {
	[hwmon_temp_min] = temp_min,
	[hwmon_temp_max] = temp_max,
	[hwmon_temp_crit] = temp_crit,
	[hwmon_temp_input] = temp_input,
};

static u8 emc1403_temp_regs[][4] = {
	[0] = {
		[temp_min] = 0x06,
		[temp_max] = 0x05,
		[temp_crit] = 0x20,
		[temp_input] = 0x00,
	},
	[1] = {
		[temp_min] = 0x08,
		[temp_max] = 0x07,
		[temp_crit] = 0x19,
		[temp_input] = 0x01,
	},
	[2] = {
		[temp_min] = 0x16,
		[temp_max] = 0x15,
		[temp_crit] = 0x1a,
		[temp_input] = 0x23,
	},
	[3] = {
		[temp_min] = 0x2d,
		[temp_max] = 0x2c,
		[temp_crit] = 0x30,
		[temp_input] = 0x2a,
	},
	[4] = {
		[temp_min] = 0x51,
		[temp_max] = 0x50,
		[temp_crit] = 0x64,
		[temp_input] = 0x41,
	},
	[5] = {
		[temp_min] = 0x55,
		[temp_max] = 0x54,
		[temp_crit] = 0x65,
		[temp_input] = 0x43
	},
	[6] = {
		[temp_min] = 0x59,
		[temp_max] = 0x58,
		[temp_crit] = 0x66,
		[temp_input] = 0x45,
	},
	[7] = {
		[temp_min] = 0x5d,
		[temp_max] = 0x5c,
		[temp_crit] = 0x67,
		[temp_input] = 0x47,
	},
};

static s8 emc1403_temp_regs_low[][4] = {
	[0] = {
		[temp_min] = -1,
		[temp_max] = -1,
		[temp_crit] = -1,
		[temp_input] = 0x29,
	},
	[1] = {
		[temp_min] = 0x14,
		[temp_max] = 0x13,
		[temp_crit] = -1,
		[temp_input] = 0x10,
	},
	[2] = {
		[temp_min] = 0x18,
		[temp_max] = 0x17,
		[temp_crit] = -1,
		[temp_input] = 0x24,
	},
	[3] = {
		[temp_min] = 0x2f,
		[temp_max] = 0x2e,
		[temp_crit] = -1,
		[temp_input] = 0x2b,
	},
	[4] = {
		[temp_min] = 0x53,
		[temp_max] = 0x52,
		[temp_crit] = -1,
		[temp_input] = 0x42,
	},
	[5] = {
		[temp_min] = 0x57,
		[temp_max] = 0x56,
		[temp_crit] = -1,
		[temp_input] = 0x44,
	},
	[6] = {
		[temp_min] = 0x5b,
		[temp_max] = 0x5a,
		[temp_crit] = -1,
		[temp_input] = 0x46,
	},
	[7] = {
		[temp_min] = 0x5f,
		[temp_max] = 0x5e,
		[temp_crit] = -1,
		[temp_input] = 0x48,
	},
};

static int __emc1403_get_temp(struct thermal_data *data, int channel,
			      enum emc1403_reg_map map, long *val)
{
	unsigned int regvalh;
	unsigned int regvall = 0;
	int ret;
	s8 reg;

	ret = regmap_read(data->regmap, emc1403_temp_regs[channel][map], &regvalh);
	if (ret < 0)
		return ret;

	reg = emc1403_temp_regs_low[channel][map];
	if (reg >= 0) {
		ret = regmap_read(data->regmap, reg, &regvall);
		if (ret < 0)
			return ret;
	}

	if (data->chip == emc1428)
		*val = sign_extend32((regvalh << 3) | (regvall >> 5), 10) * 125;
	else
		*val = ((regvalh << 3) | (regvall >> 5)) * 125;

	return 0;
}

static int emc1403_get_temp(struct thermal_data *data, int channel,
			    enum emc1403_reg_map map, long *val)
{
	int ret;

	mutex_lock(&data->mutex);
	ret = __emc1403_get_temp(data, channel, map, val);
	mutex_unlock(&data->mutex);

	return ret;
}

static int emc1403_get_hyst(struct thermal_data *data, int channel,
			    enum emc1403_reg_map map, long *val)
{
	int hyst, ret;
	long limit;

	mutex_lock(&data->mutex);
	ret = __emc1403_get_temp(data, channel, map, &limit);
	if (ret < 0)
		goto unlock;
	ret = regmap_read(data->regmap, 0x21, &hyst);
	if (ret < 0)
		goto unlock;
	if (map == temp_min)
		*val = limit + hyst * 1000;
	else
		*val = limit - hyst * 1000;
unlock:
	mutex_unlock(&data->mutex);
	return ret;
}

static int emc1403_temp_read(struct thermal_data *data, u32 attr, int channel, long *val)
{
	unsigned int regval;
	int ret;

	switch (attr) {
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
	case hwmon_temp_input:
		ret = emc1403_get_temp(data, channel, ema1403_temp_map[attr], val);
		break;
	case hwmon_temp_min_hyst:
		ret = emc1403_get_hyst(data, channel, temp_min, val);
		break;
	case hwmon_temp_max_hyst:
		ret = emc1403_get_hyst(data, channel, temp_max, val);
		break;
	case hwmon_temp_crit_hyst:
		ret = emc1403_get_hyst(data, channel, temp_crit, val);
		break;
	case hwmon_temp_min_alarm:
		if (data->chip == emc1402) {
			ret = regmap_read(data->regmap, 0x02, &regval);
			if (ret < 0)
				break;
			*val = !!(regval & BIT(5 - 2 * channel));
		} else {
			ret = regmap_read(data->regmap, 0x36, &regval);
			if (ret < 0)
				break;
			*val = !!(regval & BIT(channel));
		}
		break;
	case hwmon_temp_max_alarm:
		if (data->chip == emc1402) {
			ret = regmap_read(data->regmap, 0x02, &regval);
			if (ret < 0)
				break;
			*val = !!(regval & BIT(6 - 2 * channel));
		} else {
			ret = regmap_read(data->regmap, 0x35, &regval);
			if (ret < 0)
				break;
			*val = !!(regval & BIT(channel));
		}
		break;
	case hwmon_temp_crit_alarm:
		if (data->chip == emc1402) {
			ret = regmap_read(data->regmap, 0x02, &regval);
			if (ret < 0)
				break;
			*val = !!(regval & BIT(channel));
		} else {
			ret = regmap_read(data->regmap, 0x37, &regval);
			if (ret < 0)
				break;
			*val = !!(regval & BIT(channel));
		}
		break;
	case hwmon_temp_fault:
		ret = regmap_read(data->regmap, 0x1b, &regval);
		if (ret < 0)
			break;
		*val = !!(regval & BIT(channel));
		break;
	default:
		return -EOPNOTSUPP;
	}
	return ret;
}

static int emc1403_get_convrate(struct thermal_data *data, long *val)
{
	unsigned int convrate;
	int ret;

	ret = regmap_read(data->regmap, 0x04, &convrate);
	if (ret < 0)
		return ret;
	if (convrate > 10)
		convrate = 4;

	*val = 16000 >> convrate;
	return 0;
}

static int emc1403_chip_read(struct thermal_data *data, u32 attr, long *val)
{
	switch (attr) {
	case hwmon_chip_update_interval:
		return emc1403_get_convrate(data, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int emc1403_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct thermal_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		return emc1403_temp_read(data, attr, channel, val);
	case hwmon_chip:
		return emc1403_chip_read(data, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int emc1403_set_hyst(struct thermal_data *data, long val)
{
	int hyst, ret;
	long limit;

	if (data->chip == emc1428)
		val = clamp_val(val, -128000, 127000);
	else
		val = clamp_val(val, 0, 255000);

	mutex_lock(&data->mutex);
	ret = __emc1403_get_temp(data, 0, temp_crit, &limit);
	if (ret < 0)
		goto unlock;

	hyst = limit - val;
	if (data->chip == emc1428)
		hyst = clamp_val(DIV_ROUND_CLOSEST(hyst, 1000), 0, 127);
	else
		hyst = clamp_val(DIV_ROUND_CLOSEST(hyst, 1000), 0, 255);
	ret = regmap_write(data->regmap, 0x21, hyst);
unlock:
	mutex_unlock(&data->mutex);
	return ret;
}

static int emc1403_set_temp(struct thermal_data *data, int channel,
			    enum emc1403_reg_map map, long val)
{
	unsigned int regval;
	int ret;
	u8 regh;
	s8 regl;

	regh = emc1403_temp_regs[channel][map];
	regl = emc1403_temp_regs_low[channel][map];

	mutex_lock(&data->mutex);
	if (regl >= 0) {
		if (data->chip == emc1428)
			val = clamp_val(val, -128000, 127875);
		else
			val = clamp_val(val, 0, 255875);
		regval = DIV_ROUND_CLOSEST(val, 125);
		ret = regmap_write(data->regmap, regh, (regval >> 3) & 0xff);
		if (ret < 0)
			goto unlock;
		ret = regmap_write(data->regmap, regl, (regval & 0x07) << 5);
	} else {
		if (data->chip == emc1428)
			val = clamp_val(val, -128000, 127000);
		else
			val = clamp_val(val, 0, 255000);
		regval = DIV_ROUND_CLOSEST(val, 1000);
		ret = regmap_write(data->regmap, regh, regval);
	}
unlock:
	mutex_unlock(&data->mutex);
	return ret;
}

static int emc1403_temp_write(struct thermal_data *data, u32 attr, int channel, long val)
{
	switch (attr) {
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
		return emc1403_set_temp(data, channel, ema1403_temp_map[attr], val);
	case hwmon_temp_crit_hyst:
		return emc1403_set_hyst(data, val);
	default:
		return -EOPNOTSUPP;
	}
}

/* Lookup table for temperature conversion times in msec */
static const u16 ina3221_conv_time[] = {
	16000, 8000, 4000, 2000, 1000, 500, 250, 125, 62, 31, 16
};

static int emc1403_set_convrate(struct thermal_data *data, unsigned int interval)
{
	int convrate;

	convrate = find_closest_descending(interval, ina3221_conv_time,
					   ARRAY_SIZE(ina3221_conv_time));
	return regmap_write(data->regmap, 0x04, convrate);
}

static int emc1403_chip_write(struct thermal_data *data, u32 attr, long val)
{
	switch (attr) {
	case hwmon_chip_update_interval:
		return emc1403_set_convrate(data, clamp_val(val, 0, 100000));
	default:
		return -EOPNOTSUPP;
	}
}

static int emc1403_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct thermal_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		return emc1403_temp_write(data, attr, channel, val);
	case hwmon_chip:
		return emc1403_chip_write(data, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t emc1403_temp_is_visible(const void *_data, u32 attr, int channel)
{
	const struct thermal_data *data = _data;

	if (data->chip == emc1402 && channel > 1)
		return 0;
	if (data->chip == emc1403 && channel > 2)
		return 0;
	if (data->chip != emc1428 && channel > 3)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_fault:
	case hwmon_temp_min_hyst:
	case hwmon_temp_max_hyst:
		return 0444;
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
		return 0644;
	case hwmon_temp_crit_hyst:
		if (channel == 0)
			return 0644;
		return 0444;
	default:
		return 0;
	}
}

static umode_t emc1403_chip_is_visible(const void *_data, u32 attr)
{
	switch (attr) {
	case hwmon_chip_update_interval:
		return 0644;
	default:
		return 0;
	}
}

static umode_t emc1403_is_visible(const void *data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		return emc1403_temp_is_visible(data, attr, channel);
	case hwmon_chip:
		return emc1403_chip_is_visible(data, attr);
	default:
		return 0;
	}
}

static const struct hwmon_channel_info * const emc1403_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MIN_HYST | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MIN_HYST | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MIN_HYST | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MIN_HYST | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MIN_HYST | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MIN_HYST | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MIN_HYST | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MIN_HYST | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM | HWMON_T_FAULT
			   ),
	NULL
};

static const struct hwmon_ops emc1403_hwmon_ops = {
	.is_visible = emc1403_is_visible,
	.read = emc1403_read,
	.write = emc1403_write,
};

static const struct hwmon_chip_info emc1403_chip_info = {
	.ops = &emc1403_hwmon_ops,
	.info = emc1403_info,
};

/* Last digit of chip name indicates number of channels */
static const struct i2c_device_id emc1403_idtable[] = {
	{ "emc1402", emc1402 },
	{ "emc1403", emc1403 },
	{ "emc1404", emc1404 },
	{ "emc1412", emc1402 },
	{ "emc1413", emc1403 },
	{ "emc1414", emc1404 },
	{ "emc1422", emc1402 },
	{ "emc1423", emc1403 },
	{ "emc1424", emc1404 },
	{ "emc1428", emc1428 },
	{ "emc1438", emc1428 },
	{ "emc1442", emc1402 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, emc1403_idtable);

static int emc1403_probe(struct i2c_client *client)
{
	struct thermal_data *data;
	struct device *hwmon_dev;
	const struct i2c_device_id *id = i2c_match_id(emc1403_idtable, client);

	data = devm_kzalloc(&client->dev, sizeof(struct thermal_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->chip = id->driver_data;
	data->regmap = devm_regmap_init_i2c(client, &emc1403_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	mutex_init(&data->mutex);

	hwmon_dev = devm_hwmon_device_register_with_info(&client->dev,
							 client->name, data,
							 &emc1403_chip_info,
							 emc1403_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const unsigned short emc1403_address_list[] = {
	0x18, 0x1c, 0x29, 0x3c, 0x4c, 0x4d, 0x5c, I2C_CLIENT_END
};

static struct i2c_driver sensor_emc1403 = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "emc1403",
	},
	.detect = emc1403_detect,
	.probe = emc1403_probe,
	.id_table = emc1403_idtable,
	.address_list = emc1403_address_list,
};

module_i2c_driver(sensor_emc1403);

MODULE_AUTHOR("Kalhan Trisal <kalhan.trisal@intel.com");
MODULE_DESCRIPTION("emc1403 Thermal Driver");
MODULE_LICENSE("GPL v2");
