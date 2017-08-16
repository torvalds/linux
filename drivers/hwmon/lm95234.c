/*
 * Driver for Texas Instruments / National Semiconductor LM95234
 *
 * Copyright (c) 2013, 2014 Guenter Roeck <linux@roeck-us.net>
 *
 * Derived from lm95241.c
 * Copyright (C) 2008, 2010 Davide Rizzo <elpa.rizzo@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

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
	struct i2c_client *client;
	const struct attribute_group *groups[3];
	struct mutex update_lock;
	unsigned long last_updated, interval;	/* in jiffies */
	bool valid;		/* false until following fields are valid */
	/* registers values */
	int temp[5];		/* temperature (signed) */
	u32 status;		/* fault/alarm status */
	u8 tcrit1[5];		/* critical temperature limit */
	u8 tcrit2[2];		/* high temperature limit */
	s8 toffset[4];		/* remote temperature offset */
	u8 thyst;		/* common hysteresis */

	u8 sensor_type;		/* temperature sensor type */
};

static int lm95234_read_temp(struct i2c_client *client, int index, int *t)
{
	int val;
	u16 temp = 0;

	if (index) {
		val = i2c_smbus_read_byte_data(client,
					       LM95234_REG_UTEMPH(index - 1));
		if (val < 0)
			return val;
		temp = val << 8;
		val = i2c_smbus_read_byte_data(client,
					       LM95234_REG_UTEMPL(index - 1));
		if (val < 0)
			return val;
		temp |= val;
		*t = temp;
	}
	/*
	 * Read signed temperature if unsigned temperature is 0,
	 * or if this is the local sensor.
	 */
	if (!temp) {
		val = i2c_smbus_read_byte_data(client,
					       LM95234_REG_TEMPH(index));
		if (val < 0)
			return val;
		temp = val << 8;
		val = i2c_smbus_read_byte_data(client,
					       LM95234_REG_TEMPL(index));
		if (val < 0)
			return val;
		temp |= val;
		*t = (s16)temp;
	}
	return 0;
}

static u16 update_intervals[] = { 143, 364, 1000, 2500 };

/* Fill value cache. Must be called with update lock held. */

static int lm95234_fill_cache(struct lm95234_data *data,
			      struct i2c_client *client)
{
	int i, ret;

	ret = i2c_smbus_read_byte_data(client, LM95234_REG_CONVRATE);
	if (ret < 0)
		return ret;

	data->interval = msecs_to_jiffies(update_intervals[ret & 0x03]);

	for (i = 0; i < ARRAY_SIZE(data->tcrit1); i++) {
		ret = i2c_smbus_read_byte_data(client, LM95234_REG_TCRIT1(i));
		if (ret < 0)
			return ret;
		data->tcrit1[i] = ret;
	}
	for (i = 0; i < ARRAY_SIZE(data->tcrit2); i++) {
		ret = i2c_smbus_read_byte_data(client, LM95234_REG_TCRIT2(i));
		if (ret < 0)
			return ret;
		data->tcrit2[i] = ret;
	}
	for (i = 0; i < ARRAY_SIZE(data->toffset); i++) {
		ret = i2c_smbus_read_byte_data(client, LM95234_REG_OFFSET(i));
		if (ret < 0)
			return ret;
		data->toffset[i] = ret;
	}

	ret = i2c_smbus_read_byte_data(client, LM95234_REG_TCRIT_HYST);
	if (ret < 0)
		return ret;
	data->thyst = ret;

	ret = i2c_smbus_read_byte_data(client, LM95234_REG_REM_MODEL);
	if (ret < 0)
		return ret;
	data->sensor_type = ret;

	return 0;
}

static int lm95234_update_device(struct lm95234_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + data->interval) ||
	    !data->valid) {
		int i;

		if (!data->valid) {
			ret = lm95234_fill_cache(data, client);
			if (ret < 0)
				goto abort;
		}

		data->valid = false;
		for (i = 0; i < ARRAY_SIZE(data->temp); i++) {
			ret = lm95234_read_temp(client, i, &data->temp[i]);
			if (ret < 0)
				goto abort;
		}

		ret = i2c_smbus_read_byte_data(client, LM95234_REG_STS_FAULT);
		if (ret < 0)
			goto abort;
		data->status = ret;

		ret = i2c_smbus_read_byte_data(client, LM95234_REG_STS_TCRIT1);
		if (ret < 0)
			goto abort;
		data->status |= ret << 8;

		ret = i2c_smbus_read_byte_data(client, LM95234_REG_STS_TCRIT2);
		if (ret < 0)
			goto abort;
		data->status |= ret << 16;

		data->last_updated = jiffies;
		data->valid = true;
	}
	ret = 0;
abort:
	mutex_unlock(&data->update_lock);

	return ret;
}

static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);

	if (ret)
		return ret;

	return sprintf(buf, "%d\n",
		       DIV_ROUND_CLOSEST(data->temp[index] * 125, 32));
}

static ssize_t show_alarm(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	u32 mask = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);

	if (ret)
		return ret;

	return sprintf(buf, "%u", !!(data->status & mask));
}

static ssize_t show_type(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	u8 mask = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);

	if (ret)
		return ret;

	return sprintf(buf, data->sensor_type & mask ? "1\n" : "2\n");
}

static ssize_t set_type(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	unsigned long val;
	u8 mask = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);

	if (ret)
		return ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	if (val != 1 && val != 2)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	if (val == 1)
		data->sensor_type |= mask;
	else
		data->sensor_type &= ~mask;
	data->valid = false;
	i2c_smbus_write_byte_data(data->client, LM95234_REG_REM_MODEL,
				  data->sensor_type);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_tcrit2(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);

	if (ret)
		return ret;

	return sprintf(buf, "%u", data->tcrit2[index] * 1000);
}

static ssize_t set_tcrit2(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	long val;
	int ret = lm95234_update_device(data);

	if (ret)
		return ret;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), 0, index ? 255 : 127);

	mutex_lock(&data->update_lock);
	data->tcrit2[index] = val;
	i2c_smbus_write_byte_data(data->client, LM95234_REG_TCRIT2(index), val);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_tcrit2_hyst(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);

	if (ret)
		return ret;

	/* Result can be negative, so be careful with unsigned operands */
	return sprintf(buf, "%d",
		       ((int)data->tcrit2[index] - (int)data->thyst) * 1000);
}

static ssize_t show_tcrit1(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%u", data->tcrit1[index] * 1000);
}

static ssize_t set_tcrit1(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);
	long val;

	if (ret)
		return ret;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), 0, 255);

	mutex_lock(&data->update_lock);
	data->tcrit1[index] = val;
	i2c_smbus_write_byte_data(data->client, LM95234_REG_TCRIT1(index), val);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_tcrit1_hyst(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);

	if (ret)
		return ret;

	/* Result can be negative, so be careful with unsigned operands */
	return sprintf(buf, "%d",
		       ((int)data->tcrit1[index] - (int)data->thyst) * 1000);
}

static ssize_t set_tcrit1_hyst(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);
	long val;

	if (ret)
		return ret;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	val = DIV_ROUND_CLOSEST(val, 1000);
	val = clamp_val((int)data->tcrit1[index] - val, 0, 31);

	mutex_lock(&data->update_lock);
	data->thyst = val;
	i2c_smbus_write_byte_data(data->client, LM95234_REG_TCRIT_HYST, val);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_offset(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);

	if (ret)
		return ret;

	return sprintf(buf, "%d", data->toffset[index] * 500);
}

static ssize_t set_offset(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int ret = lm95234_update_device(data);
	long val;

	if (ret)
		return ret;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	/* Accuracy is 1/2 degrees C */
	val = clamp_val(DIV_ROUND_CLOSEST(val, 500), -128, 127);

	mutex_lock(&data->update_lock);
	data->toffset[index] = val;
	i2c_smbus_write_byte_data(data->client, LM95234_REG_OFFSET(index), val);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t update_interval_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int ret = lm95234_update_device(data);

	if (ret)
		return ret;

	return sprintf(buf, "%lu\n",
		       DIV_ROUND_CLOSEST(data->interval * 1000, HZ));
}

static ssize_t update_interval_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct lm95234_data *data = dev_get_drvdata(dev);
	int ret = lm95234_update_device(data);
	unsigned long val;
	u8 regval;

	if (ret)
		return ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	for (regval = 0; regval < 3; regval++) {
		if (val <= update_intervals[regval])
			break;
	}

	mutex_lock(&data->update_lock);
	data->interval = msecs_to_jiffies(update_intervals[regval]);
	i2c_smbus_write_byte_data(data->client, LM95234_REG_CONVRATE, regval);
	mutex_unlock(&data->update_lock);

	return count;
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, show_temp, NULL, 3);
static SENSOR_DEVICE_ATTR(temp5_input, S_IRUGO, show_temp, NULL, 4);

static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL,
			  BIT(0) | BIT(1));
static SENSOR_DEVICE_ATTR(temp3_fault, S_IRUGO, show_alarm, NULL,
			  BIT(2) | BIT(3));
static SENSOR_DEVICE_ATTR(temp4_fault, S_IRUGO, show_alarm, NULL,
			  BIT(4) | BIT(5));
static SENSOR_DEVICE_ATTR(temp5_fault, S_IRUGO, show_alarm, NULL,
			  BIT(6) | BIT(7));

static SENSOR_DEVICE_ATTR(temp2_type, S_IWUSR | S_IRUGO, show_type, set_type,
			  BIT(1));
static SENSOR_DEVICE_ATTR(temp3_type, S_IWUSR | S_IRUGO, show_type, set_type,
			  BIT(2));
static SENSOR_DEVICE_ATTR(temp4_type, S_IWUSR | S_IRUGO, show_type, set_type,
			  BIT(3));
static SENSOR_DEVICE_ATTR(temp5_type, S_IWUSR | S_IRUGO, show_type, set_type,
			  BIT(4));

static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_tcrit1,
			  set_tcrit1, 0);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_tcrit2,
			  set_tcrit2, 0);
static SENSOR_DEVICE_ATTR(temp3_max, S_IWUSR | S_IRUGO, show_tcrit2,
			  set_tcrit2, 1);
static SENSOR_DEVICE_ATTR(temp4_max, S_IWUSR | S_IRUGO, show_tcrit1,
			  set_tcrit1, 3);
static SENSOR_DEVICE_ATTR(temp5_max, S_IWUSR | S_IRUGO, show_tcrit1,
			  set_tcrit1, 4);

static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IWUSR | S_IRUGO, show_tcrit1_hyst,
			  set_tcrit1_hyst, 0);
static SENSOR_DEVICE_ATTR(temp2_max_hyst, S_IRUGO, show_tcrit2_hyst, NULL, 0);
static SENSOR_DEVICE_ATTR(temp3_max_hyst, S_IRUGO, show_tcrit2_hyst, NULL, 1);
static SENSOR_DEVICE_ATTR(temp4_max_hyst, S_IRUGO, show_tcrit1_hyst, NULL, 3);
static SENSOR_DEVICE_ATTR(temp5_max_hyst, S_IRUGO, show_tcrit1_hyst, NULL, 4);

static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL,
			  BIT(0 + 8));
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_alarm, NULL,
			  BIT(1 + 16));
static SENSOR_DEVICE_ATTR(temp3_max_alarm, S_IRUGO, show_alarm, NULL,
			  BIT(2 + 16));
static SENSOR_DEVICE_ATTR(temp4_max_alarm, S_IRUGO, show_alarm, NULL,
			  BIT(3 + 8));
static SENSOR_DEVICE_ATTR(temp5_max_alarm, S_IRUGO, show_alarm, NULL,
			  BIT(4 + 8));

static SENSOR_DEVICE_ATTR(temp2_crit, S_IWUSR | S_IRUGO, show_tcrit1,
			  set_tcrit1, 1);
static SENSOR_DEVICE_ATTR(temp3_crit, S_IWUSR | S_IRUGO, show_tcrit1,
			  set_tcrit1, 2);

static SENSOR_DEVICE_ATTR(temp2_crit_hyst, S_IRUGO, show_tcrit1_hyst, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_crit_hyst, S_IRUGO, show_tcrit1_hyst, NULL, 2);

static SENSOR_DEVICE_ATTR(temp2_crit_alarm, S_IRUGO, show_alarm, NULL,
			  BIT(1 + 8));
static SENSOR_DEVICE_ATTR(temp3_crit_alarm, S_IRUGO, show_alarm, NULL,
			  BIT(2 + 8));

static SENSOR_DEVICE_ATTR(temp2_offset, S_IWUSR | S_IRUGO, show_offset,
			  set_offset, 0);
static SENSOR_DEVICE_ATTR(temp3_offset, S_IWUSR | S_IRUGO, show_offset,
			  set_offset, 1);
static SENSOR_DEVICE_ATTR(temp4_offset, S_IWUSR | S_IRUGO, show_offset,
			  set_offset, 2);
static SENSOR_DEVICE_ATTR(temp5_offset, S_IWUSR | S_IRUGO, show_offset,
			  set_offset, 3);

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

	strlcpy(info->type, name, I2C_NAME_SIZE);
	return 0;
}

static int lm95234_init_client(struct i2c_client *client)
{
	int val, model;

	/* start conversion if necessary */
	val = i2c_smbus_read_byte_data(client, LM95234_REG_CONFIG);
	if (val < 0)
		return val;
	if (val & 0x40)
		i2c_smbus_write_byte_data(client, LM95234_REG_CONFIG,
					  val & ~0x40);

	/* If diode type status reports an error, try to fix it */
	val = i2c_smbus_read_byte_data(client, LM95234_REG_REM_MODEL_STS);
	if (val < 0)
		return val;
	model = i2c_smbus_read_byte_data(client, LM95234_REG_REM_MODEL);
	if (model < 0)
		return model;
	if (model & val) {
		dev_notice(&client->dev,
			   "Fixing remote diode type misconfiguration (0x%x)\n",
			   val);
		i2c_smbus_write_byte_data(client, LM95234_REG_REM_MODEL,
					  model & ~val);
	}
	return 0;
}

static int lm95234_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct lm95234_data *data;
	struct device *hwmon_dev;
	int err;

	data = devm_kzalloc(dev, sizeof(struct lm95234_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	/* Initialize the LM95234 chip */
	err = lm95234_init_client(client);
	if (err < 0)
		return err;

	data->groups[0] = &lm95234_common_group;
	if (id->driver_data == lm95234)
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
