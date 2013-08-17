/*
 * g760a - Driver for the Global Mixed-mode Technology Inc. G760A
 *	   fan speed PWM controller chip
 *
 * Copyright (C) 2007  Herbert Valerio Riedel <hvr@gnu.org>
 *
 * Complete datasheet is available at GMT's website:
 * http://www.gmt.com.tw/product/datasheet/EDS-760A.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

static const struct i2c_device_id g760a_id[] = {
	{ "g760a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, g760a_id);

enum g760a_regs {
	G760A_REG_SET_CNT = 0x00,
	G760A_REG_ACT_CNT = 0x01,
	G760A_REG_FAN_STA = 0x02
};

#define G760A_REG_FAN_STA_RPM_OFF 0x1 /* +/-20% off */
#define G760A_REG_FAN_STA_RPM_LOW 0x2 /* below 1920rpm */

/* register data is read (and cached) at most once per second */
#define G760A_UPDATE_INTERVAL (HZ)

struct g760a_data {
	struct i2c_client *client;
	struct device *hwmon_dev;
	struct mutex update_lock;

	/* board specific parameters */
	u32 clk; /* default 32kHz */
	u16 fan_div; /* default P=2 */

	/* g760a register cache */
	unsigned int valid:1;
	unsigned long last_updated; /* In jiffies */

	u8 set_cnt; /* PWM (period) count number; 0xff stops fan */
	u8 act_cnt; /*   formula: cnt = (CLK * 30)/(rpm * P) */
	u8 fan_sta; /* bit 0: set when actual fan speed more than 20%
		     *   outside requested fan speed
		     * bit 1: set when fan speed below 1920 rpm
		     */
};

#define G760A_DEFAULT_CLK 32768
#define G760A_DEFAULT_FAN_DIV 2

#define PWM_FROM_CNT(cnt)	(0xff-(cnt))
#define PWM_TO_CNT(pwm)		(0xff-(pwm))

static inline unsigned int rpm_from_cnt(u8 val, u32 clk, u16 div)
{
	return ((val == 0x00) ? 0 : ((clk*30)/(val*div)));
}

/* new-style driver model */
static int g760a_probe(struct i2c_client *client,
			const struct i2c_device_id *id);
static int g760a_remove(struct i2c_client *client);

static struct i2c_driver g760a_driver = {
	.driver = {
		.name	= "g760a",
	},
	.probe	  = g760a_probe,
	.remove	  = g760a_remove,
	.id_table = g760a_id,
};

/* read/write wrappers */
static int g760a_read_value(struct i2c_client *client, enum g760a_regs reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int g760a_write_value(struct i2c_client *client, enum g760a_regs reg,
			     u16 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

/*
 * sysfs attributes
 */

static struct g760a_data *g760a_update_client(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct g760a_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + G760A_UPDATE_INTERVAL)
	    || !data->valid) {
		dev_dbg(&client->dev, "Starting g760a update\n");

		data->set_cnt = g760a_read_value(client, G760A_REG_SET_CNT);
		data->act_cnt = g760a_read_value(client, G760A_REG_ACT_CNT);
		data->fan_sta = g760a_read_value(client, G760A_REG_FAN_STA);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static ssize_t show_fan(struct device *dev, struct device_attribute *da,
			char *buf)
{
	struct g760a_data *data = g760a_update_client(dev);
	unsigned int rpm = 0;

	mutex_lock(&data->update_lock);
	if (!(data->fan_sta & G760A_REG_FAN_STA_RPM_LOW))
		rpm = rpm_from_cnt(data->act_cnt, data->clk, data->fan_div);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", rpm);
}

static ssize_t show_fan_alarm(struct device *dev, struct device_attribute *da,
			      char *buf)
{
	struct g760a_data *data = g760a_update_client(dev);

	int fan_alarm = (data->fan_sta & G760A_REG_FAN_STA_RPM_OFF) ? 1 : 0;

	return sprintf(buf, "%d\n", fan_alarm);
}

static ssize_t get_pwm(struct device *dev, struct device_attribute *da,
		       char *buf)
{
	struct g760a_data *data = g760a_update_client(dev);

	return sprintf(buf, "%d\n", PWM_FROM_CNT(data->set_cnt));
}

static ssize_t set_pwm(struct device *dev, struct device_attribute *da,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct g760a_data *data = g760a_update_client(dev);
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->set_cnt = PWM_TO_CNT(SENSORS_LIMIT(val, 0, 255));
	g760a_write_value(client, G760A_REG_SET_CNT, data->set_cnt);
	mutex_unlock(&data->update_lock);

	return count;
}

static DEVICE_ATTR(pwm1, S_IWUSR | S_IRUGO, get_pwm, set_pwm);
static DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL);
static DEVICE_ATTR(fan1_alarm, S_IRUGO, show_fan_alarm, NULL);

static struct attribute *g760a_attributes[] = {
	&dev_attr_pwm1.attr,
	&dev_attr_fan1_input.attr,
	&dev_attr_fan1_alarm.attr,
	NULL
};

static const struct attribute_group g760a_group = {
	.attrs = g760a_attributes,
};

/*
 * new-style driver model code
 */

static int g760a_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct g760a_data *data;
	int err;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	data = kzalloc(sizeof(struct g760a_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);

	data->client = client;
	mutex_init(&data->update_lock);

	/* setup default configuration for now */
	data->fan_div = G760A_DEFAULT_FAN_DIV;
	data->clk = G760A_DEFAULT_CLK;

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &g760a_group);
	if (err)
		goto error_sysfs_create_group;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto error_hwmon_device_register;
	}

	return 0;

error_hwmon_device_register:
	sysfs_remove_group(&client->dev.kobj, &g760a_group);
error_sysfs_create_group:
	kfree(data);

	return err;
}

static int g760a_remove(struct i2c_client *client)
{
	struct g760a_data *data = i2c_get_clientdata(client);
	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &g760a_group);
	kfree(data);

	return 0;
}

module_i2c_driver(g760a_driver);

MODULE_AUTHOR("Herbert Valerio Riedel <hvr@gnu.org>");
MODULE_DESCRIPTION("GMT G760A driver");
MODULE_LICENSE("GPL");
