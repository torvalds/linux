/*
 * Support for the FTS Systemmonitoring Chip "Teutates"
 *
 * Copyright (C) 2016 Fujitsu Technology Solutions GmbH,
 *		  Thilo Cestonaro <thilo.cestonaro@ts.fujitsu.com>
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
 *
 */
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>

#define FTS_DEVICE_ID_REG		0x0000
#define FTS_DEVICE_REVISION_REG		0x0001
#define FTS_DEVICE_STATUS_REG		0x0004
#define FTS_SATELLITE_STATUS_REG	0x0005
#define FTS_EVENT_STATUS_REG		0x0006
#define FTS_GLOBAL_CONTROL_REG		0x0007

#define FTS_DEVICE_DETECT_REG_1		0x0C
#define FTS_DEVICE_DETECT_REG_2		0x0D
#define FTS_DEVICE_DETECT_REG_3		0x0E

#define FTS_SENSOR_EVENT_REG		0x0010

#define FTS_FAN_EVENT_REG		0x0014
#define FTS_FAN_PRESENT_REG		0x0015

#define FTS_POWER_ON_TIME_COUNTER_A	0x007A
#define FTS_POWER_ON_TIME_COUNTER_B	0x007B
#define FTS_POWER_ON_TIME_COUNTER_C	0x007C

#define FTS_PAGE_SELECT_REG		0x007F

#define FTS_WATCHDOG_TIME_PRESET	0x000B
#define FTS_WATCHDOG_CONTROL		0x5081

#define FTS_NO_FAN_SENSORS		0x08
#define FTS_NO_TEMP_SENSORS		0x10
#define FTS_NO_VOLT_SENSORS		0x04

static const unsigned short normal_i2c[] = { 0x73, I2C_CLIENT_END };

static const struct i2c_device_id fts_id[] = {
	{ "ftsteutates", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, fts_id);

enum WATCHDOG_RESOLUTION {
	seconds = 1,
	minutes = 60
};

struct fts_data {
	struct i2c_client *client;
	/* update sensor data lock */
	struct mutex update_lock;
	/* read/write register lock */
	struct mutex access_lock;
	unsigned long last_updated; /* in jiffies */
	struct watchdog_device wdd;
	enum WATCHDOG_RESOLUTION resolution;
	bool valid; /* false until following fields are valid */

	u8 volt[FTS_NO_VOLT_SENSORS];

	u8 temp_input[FTS_NO_TEMP_SENSORS];
	u8 temp_alarm;

	u8 fan_present;
	u8 fan_input[FTS_NO_FAN_SENSORS]; /* in rps */
	u8 fan_source[FTS_NO_FAN_SENSORS];
	u8 fan_alarm;
};

#define FTS_REG_FAN_INPUT(idx) ((idx) + 0x20)
#define FTS_REG_FAN_SOURCE(idx) ((idx) + 0x30)
#define FTS_REG_FAN_CONTROL(idx) (((idx) << 16) + 0x4881)

#define FTS_REG_TEMP_INPUT(idx) ((idx) + 0x40)
#define FTS_REG_TEMP_CONTROL(idx) (((idx) << 16) + 0x0681)

#define FTS_REG_VOLT(idx) ((idx) + 0x18)

/*****************************************************************************/
/* I2C Helper functions							     */
/*****************************************************************************/
static int fts_read_byte(struct i2c_client *client, unsigned short reg)
{
	int ret;
	unsigned char page = reg >> 8;
	struct fts_data *data = dev_get_drvdata(&client->dev);

	mutex_lock(&data->access_lock);

	dev_dbg(&client->dev, "page select - page: 0x%.02x\n", page);
	ret = i2c_smbus_write_byte_data(client, FTS_PAGE_SELECT_REG, page);
	if (ret < 0)
		goto error;

	reg &= 0xFF;
	ret = i2c_smbus_read_byte_data(client, reg);
	dev_dbg(&client->dev, "read - reg: 0x%.02x: val: 0x%.02x\n", reg, ret);

error:
	mutex_unlock(&data->access_lock);
	return ret;
}

static int fts_write_byte(struct i2c_client *client, unsigned short reg,
			  unsigned char value)
{
	int ret;
	unsigned char page = reg >> 8;
	struct fts_data *data = dev_get_drvdata(&client->dev);

	mutex_lock(&data->access_lock);

	dev_dbg(&client->dev, "page select - page: 0x%.02x\n", page);
	ret = i2c_smbus_write_byte_data(client, FTS_PAGE_SELECT_REG, page);
	if (ret < 0)
		goto error;

	reg &= 0xFF;
	dev_dbg(&client->dev,
		"write - reg: 0x%.02x: val: 0x%.02x\n", reg, value);
	ret = i2c_smbus_write_byte_data(client, reg, value);

error:
	mutex_unlock(&data->access_lock);
	return ret;
}

/*****************************************************************************/
/* Data Updater Helper function						     */
/*****************************************************************************/
static int fts_update_device(struct fts_data *data)
{
	int i;
	int err = 0;

	mutex_lock(&data->update_lock);
	if (!time_after(jiffies, data->last_updated + 2 * HZ) && data->valid)
		goto exit;

	err = fts_read_byte(data->client, FTS_DEVICE_STATUS_REG);
	if (err < 0)
		goto exit;

	data->valid = !!(err & 0x02); /* Data not ready yet */
	if (unlikely(!data->valid)) {
		err = -EAGAIN;
		goto exit;
	}

	err = fts_read_byte(data->client, FTS_FAN_PRESENT_REG);
	if (err < 0)
		goto exit;
	data->fan_present = err;

	err = fts_read_byte(data->client, FTS_FAN_EVENT_REG);
	if (err < 0)
		goto exit;
	data->fan_alarm = err;

	for (i = 0; i < FTS_NO_FAN_SENSORS; i++) {
		if (data->fan_present & BIT(i)) {
			err = fts_read_byte(data->client, FTS_REG_FAN_INPUT(i));
			if (err < 0)
				goto exit;
			data->fan_input[i] = err;

			err = fts_read_byte(data->client,
					    FTS_REG_FAN_SOURCE(i));
			if (err < 0)
				goto exit;
			data->fan_source[i] = err;
		} else {
			data->fan_input[i] = 0;
			data->fan_source[i] = 0;
		}
	}

	err = fts_read_byte(data->client, FTS_SENSOR_EVENT_REG);
	if (err < 0)
		goto exit;
	data->temp_alarm = err;

	for (i = 0; i < FTS_NO_TEMP_SENSORS; i++) {
		err = fts_read_byte(data->client, FTS_REG_TEMP_INPUT(i));
		if (err < 0)
			goto exit;
		data->temp_input[i] = err;
	}

	for (i = 0; i < FTS_NO_VOLT_SENSORS; i++) {
		err = fts_read_byte(data->client, FTS_REG_VOLT(i));
		if (err < 0)
			goto exit;
		data->volt[i] = err;
	}
	data->last_updated = jiffies;
	err = 0;
exit:
	mutex_unlock(&data->update_lock);
	return err;
}

/*****************************************************************************/
/* Watchdog functions							     */
/*****************************************************************************/
static int fts_wd_set_resolution(struct fts_data *data,
				 enum WATCHDOG_RESOLUTION resolution)
{
	int ret;

	if (data->resolution == resolution)
		return 0;

	ret = fts_read_byte(data->client, FTS_WATCHDOG_CONTROL);
	if (ret < 0)
		return ret;

	if ((resolution == seconds && ret & BIT(1)) ||
	    (resolution == minutes && (ret & BIT(1)) == 0)) {
		data->resolution = resolution;
		return 0;
	}

	if (resolution == seconds)
		ret |= BIT(1);
	else
		ret &= ~BIT(1);

	ret = fts_write_byte(data->client, FTS_WATCHDOG_CONTROL, ret);
	if (ret < 0)
		return ret;

	data->resolution = resolution;
	return ret;
}

static int fts_wd_set_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	struct fts_data *data;
	enum WATCHDOG_RESOLUTION resolution = seconds;
	int ret;

	data = watchdog_get_drvdata(wdd);
	/* switch watchdog resolution to minutes if timeout does not fit
	 * into a byte
	 */
	if (timeout > 0xFF) {
		timeout = DIV_ROUND_UP(timeout, 60) * 60;
		resolution = minutes;
	}

	ret = fts_wd_set_resolution(data, resolution);
	if (ret < 0)
		return ret;

	wdd->timeout = timeout;
	return 0;
}

static int fts_wd_start(struct watchdog_device *wdd)
{
	struct fts_data *data = watchdog_get_drvdata(wdd);

	return fts_write_byte(data->client, FTS_WATCHDOG_TIME_PRESET,
			      wdd->timeout / (u8)data->resolution);
}

static int fts_wd_stop(struct watchdog_device *wdd)
{
	struct fts_data *data;

	data = watchdog_get_drvdata(wdd);
	return fts_write_byte(data->client, FTS_WATCHDOG_TIME_PRESET, 0);
}

static const struct watchdog_info fts_wd_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "FTS Teutates Hardware Watchdog",
};

static const struct watchdog_ops fts_wd_ops = {
	.owner = THIS_MODULE,
	.start = fts_wd_start,
	.stop = fts_wd_stop,
	.set_timeout = fts_wd_set_timeout,
};

static int fts_watchdog_init(struct fts_data *data)
{
	int timeout, ret;

	watchdog_set_drvdata(&data->wdd, data);

	timeout = fts_read_byte(data->client, FTS_WATCHDOG_TIME_PRESET);
	if (timeout < 0)
		return timeout;

	/* watchdog not running, set timeout to a default of 60 sec. */
	if (timeout == 0) {
		ret = fts_wd_set_resolution(data, seconds);
		if (ret < 0)
			return ret;
		data->wdd.timeout = 60;
	} else {
		ret = fts_read_byte(data->client, FTS_WATCHDOG_CONTROL);
		if (ret < 0)
			return ret;

		data->resolution = ret & BIT(1) ? seconds : minutes;
		data->wdd.timeout = timeout * (u8)data->resolution;
		set_bit(WDOG_HW_RUNNING, &data->wdd.status);
	}

	/* Register our watchdog part */
	data->wdd.info = &fts_wd_info;
	data->wdd.ops = &fts_wd_ops;
	data->wdd.parent = &data->client->dev;
	data->wdd.min_timeout = 1;

	/* max timeout 255 minutes. */
	data->wdd.max_hw_heartbeat_ms = 0xFF * 60 * MSEC_PER_SEC;

	return watchdog_register_device(&data->wdd);
}

/*****************************************************************************/
/* SysFS handler functions						     */
/*****************************************************************************/
static ssize_t show_in_value(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	int err;

	err = fts_update_device(data);
	if (err < 0)
		return err;

	return sprintf(buf, "%u\n", data->volt[index]);
}

static ssize_t show_temp_value(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	int err;

	err = fts_update_device(data);
	if (err < 0)
		return err;

	return sprintf(buf, "%u\n", data->temp_input[index]);
}

static ssize_t show_temp_fault(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	int err;

	err = fts_update_device(data);
	if (err < 0)
		return err;

	/* 00h Temperature = Sensor Error */
	return sprintf(buf, "%d\n", data->temp_input[index] == 0);
}

static ssize_t show_temp_alarm(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	int err;

	err = fts_update_device(data);
	if (err < 0)
		return err;

	return sprintf(buf, "%u\n", !!(data->temp_alarm & BIT(index)));
}

static ssize_t
clear_temp_alarm(struct device *dev, struct device_attribute *devattr,
		 const char *buf, size_t count)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	long ret;

	ret = fts_update_device(data);
	if (ret < 0)
		return ret;

	if (kstrtoul(buf, 10, &ret) || ret != 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	ret = fts_read_byte(data->client, FTS_REG_TEMP_CONTROL(index));
	if (ret < 0)
		goto error;

	ret = fts_write_byte(data->client, FTS_REG_TEMP_CONTROL(index),
			     ret | 0x1);
	if (ret < 0)
		goto error;

	data->valid = false;
	ret = count;
error:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t show_fan_value(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	int err;

	err = fts_update_device(data);
	if (err < 0)
		return err;

	return sprintf(buf, "%u\n", data->fan_input[index]);
}

static ssize_t show_fan_source(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	int err;

	err = fts_update_device(data);
	if (err < 0)
		return err;

	return sprintf(buf, "%u\n", data->fan_source[index]);
}

static ssize_t show_fan_alarm(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	int err;

	err = fts_update_device(data);
	if (err < 0)
		return err;

	return sprintf(buf, "%d\n", !!(data->fan_alarm & BIT(index)));
}

static ssize_t
clear_fan_alarm(struct device *dev, struct device_attribute *devattr,
		const char *buf, size_t count)
{
	struct fts_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	long ret;

	ret = fts_update_device(data);
	if (ret < 0)
		return ret;

	if (kstrtoul(buf, 10, &ret) || ret != 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	ret = fts_read_byte(data->client, FTS_REG_FAN_CONTROL(index));
	if (ret < 0)
		goto error;

	ret = fts_write_byte(data->client, FTS_REG_FAN_CONTROL(index),
			     ret | 0x1);
	if (ret < 0)
		goto error;

	data->valid = false;
	ret = count;
error:
	mutex_unlock(&data->update_lock);
	return ret;
}

/*****************************************************************************/
/* SysFS structs							     */
/*****************************************************************************/

/* Temprature sensors */
static SENSOR_DEVICE_ATTR(temp1_input,  S_IRUGO, show_temp_value, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input,  S_IRUGO, show_temp_value, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_input,  S_IRUGO, show_temp_value, NULL, 2);
static SENSOR_DEVICE_ATTR(temp4_input,  S_IRUGO, show_temp_value, NULL, 3);
static SENSOR_DEVICE_ATTR(temp5_input,  S_IRUGO, show_temp_value, NULL, 4);
static SENSOR_DEVICE_ATTR(temp6_input,  S_IRUGO, show_temp_value, NULL, 5);
static SENSOR_DEVICE_ATTR(temp7_input,  S_IRUGO, show_temp_value, NULL, 6);
static SENSOR_DEVICE_ATTR(temp8_input,  S_IRUGO, show_temp_value, NULL, 7);
static SENSOR_DEVICE_ATTR(temp9_input,  S_IRUGO, show_temp_value, NULL, 8);
static SENSOR_DEVICE_ATTR(temp10_input, S_IRUGO, show_temp_value, NULL, 9);
static SENSOR_DEVICE_ATTR(temp11_input, S_IRUGO, show_temp_value, NULL, 10);
static SENSOR_DEVICE_ATTR(temp12_input, S_IRUGO, show_temp_value, NULL, 11);
static SENSOR_DEVICE_ATTR(temp13_input, S_IRUGO, show_temp_value, NULL, 12);
static SENSOR_DEVICE_ATTR(temp14_input, S_IRUGO, show_temp_value, NULL, 13);
static SENSOR_DEVICE_ATTR(temp15_input, S_IRUGO, show_temp_value, NULL, 14);
static SENSOR_DEVICE_ATTR(temp16_input, S_IRUGO, show_temp_value, NULL, 15);

static SENSOR_DEVICE_ATTR(temp1_fault,  S_IRUGO, show_temp_fault, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_fault,  S_IRUGO, show_temp_fault, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_fault,  S_IRUGO, show_temp_fault, NULL, 2);
static SENSOR_DEVICE_ATTR(temp4_fault,  S_IRUGO, show_temp_fault, NULL, 3);
static SENSOR_DEVICE_ATTR(temp5_fault,  S_IRUGO, show_temp_fault, NULL, 4);
static SENSOR_DEVICE_ATTR(temp6_fault,  S_IRUGO, show_temp_fault, NULL, 5);
static SENSOR_DEVICE_ATTR(temp7_fault,  S_IRUGO, show_temp_fault, NULL, 6);
static SENSOR_DEVICE_ATTR(temp8_fault,  S_IRUGO, show_temp_fault, NULL, 7);
static SENSOR_DEVICE_ATTR(temp9_fault,  S_IRUGO, show_temp_fault, NULL, 8);
static SENSOR_DEVICE_ATTR(temp10_fault, S_IRUGO, show_temp_fault, NULL, 9);
static SENSOR_DEVICE_ATTR(temp11_fault, S_IRUGO, show_temp_fault, NULL, 10);
static SENSOR_DEVICE_ATTR(temp12_fault, S_IRUGO, show_temp_fault, NULL, 11);
static SENSOR_DEVICE_ATTR(temp13_fault, S_IRUGO, show_temp_fault, NULL, 12);
static SENSOR_DEVICE_ATTR(temp14_fault, S_IRUGO, show_temp_fault, NULL, 13);
static SENSOR_DEVICE_ATTR(temp15_fault, S_IRUGO, show_temp_fault, NULL, 14);
static SENSOR_DEVICE_ATTR(temp16_fault, S_IRUGO, show_temp_fault, NULL, 15);

static SENSOR_DEVICE_ATTR(temp1_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 0);
static SENSOR_DEVICE_ATTR(temp2_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 1);
static SENSOR_DEVICE_ATTR(temp3_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 2);
static SENSOR_DEVICE_ATTR(temp4_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 3);
static SENSOR_DEVICE_ATTR(temp5_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 4);
static SENSOR_DEVICE_ATTR(temp6_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 5);
static SENSOR_DEVICE_ATTR(temp7_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 6);
static SENSOR_DEVICE_ATTR(temp8_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 7);
static SENSOR_DEVICE_ATTR(temp9_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 8);
static SENSOR_DEVICE_ATTR(temp10_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 9);
static SENSOR_DEVICE_ATTR(temp11_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 10);
static SENSOR_DEVICE_ATTR(temp12_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 11);
static SENSOR_DEVICE_ATTR(temp13_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 12);
static SENSOR_DEVICE_ATTR(temp14_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 13);
static SENSOR_DEVICE_ATTR(temp15_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 14);
static SENSOR_DEVICE_ATTR(temp16_alarm, S_IRUGO | S_IWUSR, show_temp_alarm,
			  clear_temp_alarm, 15);

static struct attribute *fts_temp_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,
	&sensor_dev_attr_temp6_input.dev_attr.attr,
	&sensor_dev_attr_temp7_input.dev_attr.attr,
	&sensor_dev_attr_temp8_input.dev_attr.attr,
	&sensor_dev_attr_temp9_input.dev_attr.attr,
	&sensor_dev_attr_temp10_input.dev_attr.attr,
	&sensor_dev_attr_temp11_input.dev_attr.attr,
	&sensor_dev_attr_temp12_input.dev_attr.attr,
	&sensor_dev_attr_temp13_input.dev_attr.attr,
	&sensor_dev_attr_temp14_input.dev_attr.attr,
	&sensor_dev_attr_temp15_input.dev_attr.attr,
	&sensor_dev_attr_temp16_input.dev_attr.attr,

	&sensor_dev_attr_temp1_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	&sensor_dev_attr_temp4_fault.dev_attr.attr,
	&sensor_dev_attr_temp5_fault.dev_attr.attr,
	&sensor_dev_attr_temp6_fault.dev_attr.attr,
	&sensor_dev_attr_temp7_fault.dev_attr.attr,
	&sensor_dev_attr_temp8_fault.dev_attr.attr,
	&sensor_dev_attr_temp9_fault.dev_attr.attr,
	&sensor_dev_attr_temp10_fault.dev_attr.attr,
	&sensor_dev_attr_temp11_fault.dev_attr.attr,
	&sensor_dev_attr_temp12_fault.dev_attr.attr,
	&sensor_dev_attr_temp13_fault.dev_attr.attr,
	&sensor_dev_attr_temp14_fault.dev_attr.attr,
	&sensor_dev_attr_temp15_fault.dev_attr.attr,
	&sensor_dev_attr_temp16_fault.dev_attr.attr,

	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_alarm.dev_attr.attr,
	&sensor_dev_attr_temp6_alarm.dev_attr.attr,
	&sensor_dev_attr_temp7_alarm.dev_attr.attr,
	&sensor_dev_attr_temp8_alarm.dev_attr.attr,
	&sensor_dev_attr_temp9_alarm.dev_attr.attr,
	&sensor_dev_attr_temp10_alarm.dev_attr.attr,
	&sensor_dev_attr_temp11_alarm.dev_attr.attr,
	&sensor_dev_attr_temp12_alarm.dev_attr.attr,
	&sensor_dev_attr_temp13_alarm.dev_attr.attr,
	&sensor_dev_attr_temp14_alarm.dev_attr.attr,
	&sensor_dev_attr_temp15_alarm.dev_attr.attr,
	&sensor_dev_attr_temp16_alarm.dev_attr.attr,
	NULL
};

/* Fans */
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan_value, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan_value, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_input, S_IRUGO, show_fan_value, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_input, S_IRUGO, show_fan_value, NULL, 3);
static SENSOR_DEVICE_ATTR(fan5_input, S_IRUGO, show_fan_value, NULL, 4);
static SENSOR_DEVICE_ATTR(fan6_input, S_IRUGO, show_fan_value, NULL, 5);
static SENSOR_DEVICE_ATTR(fan7_input, S_IRUGO, show_fan_value, NULL, 6);
static SENSOR_DEVICE_ATTR(fan8_input, S_IRUGO, show_fan_value, NULL, 7);

static SENSOR_DEVICE_ATTR(fan1_source, S_IRUGO, show_fan_source, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_source, S_IRUGO, show_fan_source, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_source, S_IRUGO, show_fan_source, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_source, S_IRUGO, show_fan_source, NULL, 3);
static SENSOR_DEVICE_ATTR(fan5_source, S_IRUGO, show_fan_source, NULL, 4);
static SENSOR_DEVICE_ATTR(fan6_source, S_IRUGO, show_fan_source, NULL, 5);
static SENSOR_DEVICE_ATTR(fan7_source, S_IRUGO, show_fan_source, NULL, 6);
static SENSOR_DEVICE_ATTR(fan8_source, S_IRUGO, show_fan_source, NULL, 7);

static SENSOR_DEVICE_ATTR(fan1_alarm, S_IRUGO | S_IWUSR,
			 show_fan_alarm, clear_fan_alarm, 0);
static SENSOR_DEVICE_ATTR(fan2_alarm, S_IRUGO | S_IWUSR,
			 show_fan_alarm, clear_fan_alarm, 1);
static SENSOR_DEVICE_ATTR(fan3_alarm, S_IRUGO | S_IWUSR,
			 show_fan_alarm, clear_fan_alarm, 2);
static SENSOR_DEVICE_ATTR(fan4_alarm, S_IRUGO | S_IWUSR,
			 show_fan_alarm, clear_fan_alarm, 3);
static SENSOR_DEVICE_ATTR(fan5_alarm, S_IRUGO | S_IWUSR,
			 show_fan_alarm, clear_fan_alarm, 4);
static SENSOR_DEVICE_ATTR(fan6_alarm, S_IRUGO | S_IWUSR,
			 show_fan_alarm, clear_fan_alarm, 5);
static SENSOR_DEVICE_ATTR(fan7_alarm, S_IRUGO | S_IWUSR,
			 show_fan_alarm, clear_fan_alarm, 6);
static SENSOR_DEVICE_ATTR(fan8_alarm, S_IRUGO | S_IWUSR,
			 show_fan_alarm, clear_fan_alarm, 7);

static struct attribute *fts_fan_attrs[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan5_input.dev_attr.attr,
	&sensor_dev_attr_fan6_input.dev_attr.attr,
	&sensor_dev_attr_fan7_input.dev_attr.attr,
	&sensor_dev_attr_fan8_input.dev_attr.attr,

	&sensor_dev_attr_fan1_source.dev_attr.attr,
	&sensor_dev_attr_fan2_source.dev_attr.attr,
	&sensor_dev_attr_fan3_source.dev_attr.attr,
	&sensor_dev_attr_fan4_source.dev_attr.attr,
	&sensor_dev_attr_fan5_source.dev_attr.attr,
	&sensor_dev_attr_fan6_source.dev_attr.attr,
	&sensor_dev_attr_fan7_source.dev_attr.attr,
	&sensor_dev_attr_fan8_source.dev_attr.attr,

	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan3_alarm.dev_attr.attr,
	&sensor_dev_attr_fan4_alarm.dev_attr.attr,
	&sensor_dev_attr_fan5_alarm.dev_attr.attr,
	&sensor_dev_attr_fan6_alarm.dev_attr.attr,
	&sensor_dev_attr_fan7_alarm.dev_attr.attr,
	&sensor_dev_attr_fan8_alarm.dev_attr.attr,
	NULL
};

/* Voltages */
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, show_in_value, NULL, 0);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, show_in_value, NULL, 1);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, show_in_value, NULL, 2);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, show_in_value, NULL, 3);
static struct attribute *fts_voltage_attrs[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	NULL
};

static const struct attribute_group fts_voltage_attr_group = {
	.attrs = fts_voltage_attrs
};

static const struct attribute_group fts_temp_attr_group = {
	.attrs = fts_temp_attrs
};

static const struct attribute_group fts_fan_attr_group = {
	.attrs = fts_fan_attrs
};

static const struct attribute_group *fts_attr_groups[] = {
	&fts_voltage_attr_group,
	&fts_temp_attr_group,
	&fts_fan_attr_group,
	NULL
};

/*****************************************************************************/
/* Module initialization / remove functions				     */
/*****************************************************************************/
static int fts_detect(struct i2c_client *client,
		      struct i2c_board_info *info)
{
	int val;

	/* detection works with revsion greater or equal to 0x2b */
	val = i2c_smbus_read_byte_data(client, FTS_DEVICE_REVISION_REG);
	if (val < 0x2b)
		return -ENODEV;

	/* Device Detect Regs must have 0x17 0x34 and 0x54 */
	val = i2c_smbus_read_byte_data(client, FTS_DEVICE_DETECT_REG_1);
	if (val != 0x17)
		return -ENODEV;

	val = i2c_smbus_read_byte_data(client, FTS_DEVICE_DETECT_REG_2);
	if (val != 0x34)
		return -ENODEV;

	val = i2c_smbus_read_byte_data(client, FTS_DEVICE_DETECT_REG_3);
	if (val != 0x54)
		return -ENODEV;

	/*
	 * 0x10 == Baseboard Management Controller, 0x01 == Teutates
	 * Device ID Reg needs to be 0x11
	 */
	val = i2c_smbus_read_byte_data(client, FTS_DEVICE_ID_REG);
	if (val != 0x11)
		return -ENODEV;

	strlcpy(info->type, fts_id[0].name, I2C_NAME_SIZE);
	info->flags = 0;
	return 0;
}

static int fts_remove(struct i2c_client *client)
{
	struct fts_data *data = dev_get_drvdata(&client->dev);

	watchdog_unregister_device(&data->wdd);
	return 0;
}

static int fts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	u8 revision;
	struct fts_data *data;
	int err;
	s8 deviceid;
	struct device *hwmon_dev;

	if (client->addr != 0x73)
		return -ENODEV;

	/* Baseboard Management Controller check */
	deviceid = i2c_smbus_read_byte_data(client, FTS_DEVICE_ID_REG);
	if (deviceid > 0 && (deviceid & 0xF0) == 0x10) {
		switch (deviceid & 0x0F) {
		case 0x01:
			break;
		default:
			dev_dbg(&client->dev,
				"No Baseboard Management Controller\n");
			return -ENODEV;
		}
	} else {
		dev_dbg(&client->dev, "No fujitsu board\n");
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev, sizeof(struct fts_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->update_lock);
	mutex_init(&data->access_lock);
	data->client = client;
	dev_set_drvdata(&client->dev, data);

	err = i2c_smbus_read_byte_data(client, FTS_DEVICE_REVISION_REG);
	if (err < 0)
		return err;
	revision = err;

	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev,
							   "ftsteutates",
							   data,
							   fts_attr_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	err = fts_watchdog_init(data);
	if (err)
		return err;

	dev_info(&client->dev, "Detected FTS Teutates chip, revision: %d.%d\n",
		 (revision & 0xF0) >> 4, revision & 0x0F);
	return 0;
}

/*****************************************************************************/
/* Module Details							     */
/*****************************************************************************/
static struct i2c_driver fts_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "ftsteutates",
	},
	.id_table = fts_id,
	.probe = fts_probe,
	.remove = fts_remove,
	.detect = fts_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(fts_driver);

MODULE_AUTHOR("Thilo Cestonaro <thilo.cestonaro@ts.fujitsu.com>");
MODULE_DESCRIPTION("FTS Teutates driver");
MODULE_LICENSE("GPL");
