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

static const unsigned short normal_i2c[] = {
	0x18, 0x19, 0x1a, 0x29, 0x2a, 0x2b, 0x4c, 0x4d, 0x4e, I2C_CLIENT_END };

/*
 * The MAX1619 registers
 */

#define MAX1619_REG_R_MAN_ID		0xFE
#define MAX1619_REG_R_CHIP_ID		0xFF
#define MAX1619_REG_R_CONFIG		0x03
#define MAX1619_REG_W_CONFIG		0x09
#define MAX1619_REG_R_CONVRATE		0x04
#define MAX1619_REG_W_CONVRATE		0x0A
#define MAX1619_REG_R_STATUS		0x02
#define MAX1619_REG_R_LOCAL_TEMP	0x00
#define MAX1619_REG_R_REMOTE_TEMP	0x01
#define MAX1619_REG_R_REMOTE_HIGH	0x07
#define MAX1619_REG_W_REMOTE_HIGH	0x0D
#define MAX1619_REG_R_REMOTE_LOW	0x08
#define MAX1619_REG_W_REMOTE_LOW	0x0E
#define MAX1619_REG_R_REMOTE_CRIT	0x10
#define MAX1619_REG_W_REMOTE_CRIT	0x12
#define MAX1619_REG_R_TCRIT_HYST	0x11
#define MAX1619_REG_W_TCRIT_HYST	0x13

/*
 * Conversions
 */

static int temp_from_reg(int val)
{
	return (val & 0x80 ? val-0x100 : val) * 1000;
}

static int temp_to_reg(int val)
{
	return (val < 0 ? val+0x100*1000 : val) / 1000;
}

enum temp_index {
	t_input1 = 0,
	t_input2,
	t_low2,
	t_high2,
	t_crit2,
	t_hyst2,
	t_num_regs
};

/*
 * Client data (each client gets its own)
 */

struct max1619_data {
	struct i2c_client *client;
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	u8 temp[t_num_regs];	/* index with enum temp_index */
	u8 alarms;
};

static const u8 regs_read[t_num_regs] = {
	[t_input1] = MAX1619_REG_R_LOCAL_TEMP,
	[t_input2] = MAX1619_REG_R_REMOTE_TEMP,
	[t_low2] = MAX1619_REG_R_REMOTE_LOW,
	[t_high2] = MAX1619_REG_R_REMOTE_HIGH,
	[t_crit2] = MAX1619_REG_R_REMOTE_CRIT,
	[t_hyst2] = MAX1619_REG_R_TCRIT_HYST,
};

static const u8 regs_write[t_num_regs] = {
	[t_low2] = MAX1619_REG_W_REMOTE_LOW,
	[t_high2] = MAX1619_REG_W_REMOTE_HIGH,
	[t_crit2] = MAX1619_REG_W_REMOTE_CRIT,
	[t_hyst2] = MAX1619_REG_W_TCRIT_HYST,
};

static struct max1619_data *max1619_update_device(struct device *dev)
{
	struct max1619_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int config, i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		dev_dbg(&client->dev, "Updating max1619 data.\n");
		for (i = 0; i < t_num_regs; i++)
			data->temp[i] = i2c_smbus_read_byte_data(client,
					regs_read[i]);
		data->alarms = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_STATUS);
		/* If OVERT polarity is low, reverse alarm bit */
		config = i2c_smbus_read_byte_data(client, MAX1619_REG_R_CONFIG);
		if (!(config & 0x20))
			data->alarms ^= 0x02;

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Sysfs stuff
 */

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max1619_data *data = max1619_update_device(dev);

	return sprintf(buf, "%d\n", temp_from_reg(data->temp[attr->index]));
}

static ssize_t set_temp(struct device *dev, struct device_attribute *devattr,
			   const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max1619_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp[attr->index] = temp_to_reg(val);
	i2c_smbus_write_byte_data(client, regs_write[attr->index],
				  data->temp[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_alarms(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct max1619_data *data = max1619_update_device(dev);
	return sprintf(buf, "%d\n", data->alarms);
}

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct max1619_data *data = max1619_update_device(dev);
	return sprintf(buf, "%d\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, t_input1);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, t_input2);
static SENSOR_DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_temp, set_temp,
			  t_low2);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp, set_temp,
			  t_high2);
static SENSOR_DEVICE_ATTR(temp2_crit, S_IWUSR | S_IRUGO, show_temp, set_temp,
			  t_crit2);
static SENSOR_DEVICE_ATTR(temp2_crit_hyst, S_IWUSR | S_IRUGO, show_temp,
			  set_temp, t_hyst2);

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);
static SENSOR_DEVICE_ATTR(temp2_crit_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_min_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_alarm, NULL, 4);

static struct attribute *max1619_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_hyst.dev_attr.attr,

	&dev_attr_alarms.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(max1619);

/* Return 0 if detection is successful, -ENODEV otherwise */
static int max1619_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	u8 reg_config, reg_convrate, reg_status, man_id, chip_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* detection */
	reg_config = i2c_smbus_read_byte_data(client, MAX1619_REG_R_CONFIG);
	reg_convrate = i2c_smbus_read_byte_data(client, MAX1619_REG_R_CONVRATE);
	reg_status = i2c_smbus_read_byte_data(client, MAX1619_REG_R_STATUS);
	if ((reg_config & 0x03) != 0x00
	 || reg_convrate > 0x07 || (reg_status & 0x61) != 0x00) {
		dev_dbg(&adapter->dev, "MAX1619 detection failed at 0x%02x\n",
			client->addr);
		return -ENODEV;
	}

	/* identification */
	man_id = i2c_smbus_read_byte_data(client, MAX1619_REG_R_MAN_ID);
	chip_id = i2c_smbus_read_byte_data(client, MAX1619_REG_R_CHIP_ID);
	if (man_id != 0x4D || chip_id != 0x04) {
		dev_info(&adapter->dev,
			 "Unsupported chip (man_id=0x%02X, chip_id=0x%02X).\n",
			 man_id, chip_id);
		return -ENODEV;
	}

	strlcpy(info->type, "max1619", I2C_NAME_SIZE);

	return 0;
}

static void max1619_init_client(struct i2c_client *client)
{
	u8 config;

	/*
	 * Start the conversions.
	 */
	i2c_smbus_write_byte_data(client, MAX1619_REG_W_CONVRATE,
				  5); /* 2 Hz */
	config = i2c_smbus_read_byte_data(client, MAX1619_REG_R_CONFIG);
	if (config & 0x40)
		i2c_smbus_write_byte_data(client, MAX1619_REG_W_CONFIG,
					  config & 0xBF); /* run */
}

static int max1619_probe(struct i2c_client *new_client,
			 const struct i2c_device_id *id)
{
	struct max1619_data *data;
	struct device *hwmon_dev;

	data = devm_kzalloc(&new_client->dev, sizeof(struct max1619_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = new_client;
	mutex_init(&data->update_lock);

	/* Initialize the MAX1619 chip */
	max1619_init_client(new_client);

	hwmon_dev = devm_hwmon_device_register_with_groups(&new_client->dev,
							   new_client->name,
							   data,
							   max1619_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id max1619_id[] = {
	{ "max1619", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max1619_id);

static struct i2c_driver max1619_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "max1619",
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
