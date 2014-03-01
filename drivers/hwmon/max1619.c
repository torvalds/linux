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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

/*
 * Functions declaration
 */

static int max1619_probe(struct i2c_client *client,
			 const struct i2c_device_id *id);
static int max1619_detect(struct i2c_client *client,
			  struct i2c_board_info *info);
static void max1619_init_client(struct i2c_client *client);
static int max1619_remove(struct i2c_client *client);
static struct max1619_data *max1619_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */

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
	.remove		= max1619_remove,
	.id_table	= max1619_id,
	.detect		= max1619_detect,
	.address_list	= normal_i2c,
};

/*
 * Client data (each client gets its own)
 */

struct max1619_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	u8 temp_input1; /* local */
	u8 temp_input2, temp_low2, temp_high2; /* remote */
	u8 temp_crit2;
	u8 temp_hyst2;
	u8 alarms;
};

/*
 * Sysfs stuff
 */

#define show_temp(value) \
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, \
			    char *buf) \
{ \
	struct max1619_data *data = max1619_update_device(dev); \
	return sprintf(buf, "%d\n", temp_from_reg(data->value)); \
}
show_temp(temp_input1);
show_temp(temp_input2);
show_temp(temp_low2);
show_temp(temp_high2);
show_temp(temp_crit2);
show_temp(temp_hyst2);

#define set_temp2(value, reg) \
static ssize_t set_##value(struct device *dev, struct device_attribute *attr, \
			   const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct max1619_data *data = i2c_get_clientdata(client); \
	long val; \
	int err = kstrtol(buf, 10, &val); \
	if (err) \
		return err; \
\
	mutex_lock(&data->update_lock); \
	data->value = temp_to_reg(val); \
	i2c_smbus_write_byte_data(client, reg, data->value); \
	mutex_unlock(&data->update_lock); \
	return count; \
}

set_temp2(temp_low2, MAX1619_REG_W_REMOTE_LOW);
set_temp2(temp_high2, MAX1619_REG_W_REMOTE_HIGH);
set_temp2(temp_crit2, MAX1619_REG_W_REMOTE_CRIT);
set_temp2(temp_hyst2, MAX1619_REG_W_TCRIT_HYST);

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

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp_input1, NULL);
static DEVICE_ATTR(temp2_input, S_IRUGO, show_temp_input2, NULL);
static DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_temp_low2,
	set_temp_low2);
static DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp_high2,
	set_temp_high2);
static DEVICE_ATTR(temp2_crit, S_IWUSR | S_IRUGO, show_temp_crit2,
	set_temp_crit2);
static DEVICE_ATTR(temp2_crit_hyst, S_IWUSR | S_IRUGO, show_temp_hyst2,
	set_temp_hyst2);
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);
static SENSOR_DEVICE_ATTR(temp2_crit_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_min_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_alarm, NULL, 4);

static struct attribute *max1619_attributes[] = {
	&dev_attr_temp1_input.attr,
	&dev_attr_temp2_input.attr,
	&dev_attr_temp2_min.attr,
	&dev_attr_temp2_max.attr,
	&dev_attr_temp2_crit.attr,
	&dev_attr_temp2_crit_hyst.attr,

	&dev_attr_alarms.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group max1619_group = {
	.attrs = max1619_attributes,
};

/*
 * Real code
 */

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

static int max1619_probe(struct i2c_client *new_client,
			 const struct i2c_device_id *id)
{
	struct max1619_data *data;
	int err;

	data = devm_kzalloc(&new_client->dev, sizeof(struct max1619_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(new_client, data);
	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Initialize the MAX1619 chip */
	max1619_init_client(new_client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&new_client->dev.kobj, &max1619_group);
	if (err)
		return err;

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&new_client->dev.kobj, &max1619_group);
	return err;
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

static int max1619_remove(struct i2c_client *client)
{
	struct max1619_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &max1619_group);

	return 0;
}

static struct max1619_data *max1619_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max1619_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		dev_dbg(&client->dev, "Updating max1619 data.\n");
		data->temp_input1 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_LOCAL_TEMP);
		data->temp_input2 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_REMOTE_TEMP);
		data->temp_high2 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_REMOTE_HIGH);
		data->temp_low2 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_REMOTE_LOW);
		data->temp_crit2 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_REMOTE_CRIT);
		data->temp_hyst2 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_TCRIT_HYST);
		data->alarms = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_STATUS);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

module_i2c_driver(max1619_driver);

MODULE_AUTHOR("Oleksij Rempel <bug-track@fisher-privat.net>, Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("MAX1619 sensor driver");
MODULE_LICENSE("GPL");
