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
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
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
	const struct attribute_group *groups[3];
	struct mutex update_lock;
};

static int lm95234_read_temp(struct regmap *regmap, int index, int *t)
{
	int temp = 0, ret;
	u32 val;

	if (index) {
		ret = regmap_read(regmap, LM95234_REG_UTEMPH(index - 1), &val);
		if (ret)
			return ret;
		temp = val << 8;
		ret = regmap_read(regmap, LM95234_REG_UTEMPL(index - 1), &val);
		if (ret)
			return ret;
		temp |= val;
	}
	/*
	 * Read signed temperature if unsigned temperature is 0,
	 * or if this is the local sensor.
	 */
	if (!temp) {
		ret = regmap_read(regmap, LM95234_REG_TEMPH(index), &val);
		if (ret)
			return ret;
		temp = val << 8;
		ret = regmap_read(regmap, LM95234_REG_TEMPL(index), &val);
		if (ret)
			return ret;
		temp = sign_extend32(temp | val, 15);
	}
	*t = DIV_ROUND_CLOSEST(temp * 125, 32);
	return 0;
}

static ssize_t temp_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret, temp;

	ret = lm95234_read_temp(data->regmap, index, &temp);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", temp);
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	u8 mask = to_sensor_dev_attr_2(attr)->index;
	u8 reg = to_sensor_dev_attr_2(attr)->nr;
	int ret;
	u32 val;

	ret = regmap_read(data->regmap, reg, &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", !!(val & mask));
}

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	u8 mask = to_sensor_dev_attr(attr)->index;
	u32 val;
	int ret;

	ret = regmap_read(data->regmap, LM95234_REG_REM_MODEL, &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%s\n", val & mask ? "1" : "2");
}

static ssize_t type_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	u8 mask = to_sensor_dev_attr(attr)->index;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	if (val != 1 && val != 2)
		return -EINVAL;

	ret = regmap_update_bits(data->regmap, LM95234_REG_REM_MODEL,
				 mask, val == 1 ? mask : 0);
	if (ret)
		return ret;
	return count;
}

static ssize_t tcrit2_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret;
	u32 tcrit2;

	ret = regmap_read(data->regmap, LM95234_REG_TCRIT2(index), &tcrit2);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", tcrit2 * 1000);
}

static ssize_t tcrit2_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	val = DIV_ROUND_CLOSEST(clamp_val(val, 0, (index ? 255 : 127) * 1000),
				1000);

	ret = regmap_write(data->regmap, LM95234_REG_TCRIT2(index), val);
	if (ret)
		return ret;
	return count;
}

static ssize_t tcrit_hyst_show(struct lm95234_data *data, char *buf, int reg)
{
	u32 thyst, tcrit;
	int ret;

	mutex_lock(&data->update_lock);
	ret = regmap_read(data->regmap, reg, &tcrit);
	if (ret)
		goto unlock;
	ret = regmap_read(data->regmap, LM95234_REG_TCRIT_HYST, &thyst);
unlock:
	mutex_unlock(&data->update_lock);
	if (ret)
		return ret;

	/* Result can be negative, so be careful with unsigned operands */
	return sysfs_emit(buf, "%d\n", ((int)tcrit - (int)thyst) * 1000);
}

static ssize_t tcrit2_hyst_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;

	return tcrit_hyst_show(data, buf, LM95234_REG_TCRIT2(index));
}

static ssize_t tcrit1_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret;
	u32 val;

	ret = regmap_read(data->regmap, LM95234_REG_TCRIT1(index), &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", val * 1000);
}

static ssize_t tcrit1_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	val = DIV_ROUND_CLOSEST(clamp_val(val, 0, 255000), 1000);

	ret = regmap_write(data->regmap, LM95234_REG_TCRIT1(index), val);
	if (ret)
		return ret;

	return count;
}

static ssize_t tcrit1_hyst_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;

	return tcrit_hyst_show(data, buf, LM95234_REG_TCRIT1(index));
}

static ssize_t tcrit1_hyst_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	u32 tcrit;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	mutex_lock(&data->update_lock);

	ret = regmap_read(data->regmap, LM95234_REG_TCRIT1(index), &tcrit);
	if (ret)
		goto unlock;

	val = DIV_ROUND_CLOSEST(clamp_val(val, -255000, 255000), 1000);
	val = clamp_val((int)tcrit - val, 0, 31);

	ret = regmap_write(data->regmap, LM95234_REG_TCRIT_HYST, val);
unlock:
	mutex_unlock(&data->update_lock);
	if (ret)
		return ret;

	return count;
}

static ssize_t offset_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	u32 offset;
	int ret;

	ret = regmap_read(data->regmap, LM95234_REG_OFFSET(index), &offset);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", sign_extend32(offset, 7) * 500);
}

static ssize_t offset_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	/* Accuracy is 1/2 degrees C */
	val = DIV_ROUND_CLOSEST(clamp_val(val, -64000, 63500), 500);

	ret = regmap_write(data->regmap, LM95234_REG_OFFSET(index), val);
	if (ret < 0)
		return ret;

	return count;
}

static u16 update_intervals[] = { 143, 364, 1000, 2500 };

static ssize_t update_interval_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	u32 convrate;
	int ret;

	ret = regmap_read(data->regmap, LM95234_REG_CONVRATE, &convrate);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", update_intervals[convrate & 0x03]);
}

static ssize_t update_interval_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	val = find_closest(val, update_intervals, ARRAY_SIZE(update_intervals));
	ret = regmap_write(data->regmap, LM95234_REG_CONVRATE, val);
	if (ret)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp, 2);
static SENSOR_DEVICE_ATTR_RO(temp4_input, temp, 3);
static SENSOR_DEVICE_ATTR_RO(temp5_input, temp, 4);

static SENSOR_DEVICE_ATTR_2_RO(temp2_fault, alarm, LM95234_REG_STS_FAULT, BIT(0) | BIT(1));
static SENSOR_DEVICE_ATTR_2_RO(temp3_fault, alarm, LM95234_REG_STS_FAULT, BIT(2) | BIT(3));
static SENSOR_DEVICE_ATTR_2_RO(temp4_fault, alarm, LM95234_REG_STS_FAULT, BIT(4) | BIT(5));
static SENSOR_DEVICE_ATTR_2_RO(temp5_fault, alarm, LM95234_REG_STS_FAULT, BIT(6) | BIT(7));

static SENSOR_DEVICE_ATTR_RW(temp2_type, type, BIT(1));
static SENSOR_DEVICE_ATTR_RW(temp3_type, type, BIT(2));
static SENSOR_DEVICE_ATTR_RW(temp4_type, type, BIT(3));
static SENSOR_DEVICE_ATTR_RW(temp5_type, type, BIT(4));

static SENSOR_DEVICE_ATTR_RW(temp1_max, tcrit1, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_max, tcrit2, 0);
static SENSOR_DEVICE_ATTR_RW(temp3_max, tcrit2, 1);
static SENSOR_DEVICE_ATTR_RW(temp4_max, tcrit1, 3);
static SENSOR_DEVICE_ATTR_RW(temp5_max, tcrit1, 4);

static SENSOR_DEVICE_ATTR_RW(temp1_max_hyst, tcrit1_hyst, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_max_hyst, tcrit2_hyst, 0);
static SENSOR_DEVICE_ATTR_RO(temp3_max_hyst, tcrit2_hyst, 1);
static SENSOR_DEVICE_ATTR_RO(temp4_max_hyst, tcrit1_hyst, 3);
static SENSOR_DEVICE_ATTR_RO(temp5_max_hyst, tcrit1_hyst, 4);

static SENSOR_DEVICE_ATTR_2_RO(temp1_max_alarm, alarm, LM95234_REG_STS_TCRIT1, BIT(0));
static SENSOR_DEVICE_ATTR_2_RO(temp2_max_alarm, alarm, LM95234_REG_STS_TCRIT2, BIT(1));
static SENSOR_DEVICE_ATTR_2_RO(temp3_max_alarm, alarm, LM95234_REG_STS_TCRIT2, BIT(2));
static SENSOR_DEVICE_ATTR_2_RO(temp4_max_alarm, alarm, LM95234_REG_STS_TCRIT1, BIT(3));
static SENSOR_DEVICE_ATTR_2_RO(temp5_max_alarm, alarm, LM95234_REG_STS_TCRIT1, BIT(4));

static SENSOR_DEVICE_ATTR_RW(temp2_crit, tcrit1, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_crit, tcrit1, 2);

static SENSOR_DEVICE_ATTR_RO(temp2_crit_hyst, tcrit1_hyst, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_crit_hyst, tcrit1_hyst, 2);

static SENSOR_DEVICE_ATTR_2_RO(temp2_crit_alarm, alarm, LM95234_REG_STS_TCRIT1, BIT(1));
static SENSOR_DEVICE_ATTR_2_RO(temp3_crit_alarm, alarm, LM95234_REG_STS_TCRIT1, BIT(2));

static SENSOR_DEVICE_ATTR_RW(temp2_offset, offset, 0);
static SENSOR_DEVICE_ATTR_RW(temp3_offset, offset, 1);
static SENSOR_DEVICE_ATTR_RW(temp4_offset, offset, 2);
static SENSOR_DEVICE_ATTR_RW(temp5_offset, offset, 3);

static DEVICE_ATTR_RW(update_interval);

static struct attribute *lm95234_common_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_type.dev_attr.attr,
	&sensor_dev_attr_temp3_type.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_offset.dev_attr.attr,
	&sensor_dev_attr_temp3_offset.dev_attr.attr,
	&dev_attr_update_interval.attr,
	NULL
};

static const struct attribute_group lm95234_common_group = {
	.attrs = lm95234_common_attrs,
};

static struct attribute *lm95234_attrs[] = {
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,
	&sensor_dev_attr_temp4_fault.dev_attr.attr,
	&sensor_dev_attr_temp5_fault.dev_attr.attr,
	&sensor_dev_attr_temp4_type.dev_attr.attr,
	&sensor_dev_attr_temp5_type.dev_attr.attr,
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp5_max.dev_attr.attr,
	&sensor_dev_attr_temp4_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp5_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp4_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_offset.dev_attr.attr,
	&sensor_dev_attr_temp5_offset.dev_attr.attr,
	NULL
};

static const struct attribute_group lm95234_group = {
	.attrs = lm95234_attrs,
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
	enum chips type = (uintptr_t)i2c_get_match_data(client);
	struct device *dev = &client->dev;
	struct lm95234_data *data;
	struct device *hwmon_dev;
	struct regmap *regmap;
	int err;

	data = devm_kzalloc(dev, sizeof(struct lm95234_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &lm95234_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	data->regmap = regmap;
	mutex_init(&data->update_lock);

	/* Initialize the LM95234 chip */
	err = lm95234_init_client(dev, regmap);
	if (err < 0)
		return err;

	data->groups[0] = &lm95234_common_group;
	if (type == lm95234)
		data->groups[1] = &lm95234_group;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
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
