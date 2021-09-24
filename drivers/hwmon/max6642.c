// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for +/-1 degree C, SMBus-Compatible Remote/Local Temperature Sensor
 * with Overtemperature Alarm
 *
 * Copyright (C) 2011 AppearTV AS
 *
 * Derived from:
 *
 *  Based on the max1619 driver.
 *  Copyright (C) 2003-2004 Oleksij Rempel <bug-track@fisher-privat.net>
 *                          Jean Delvare <jdelvare@suse.de>
 *
 * The MAX6642 is a sensor chip made by Maxim.
 * It reports up to two temperatures (its own plus up to
 * one external one). Complete datasheet can be
 * obtained from Maxim's website at:
 *   http://datasheets.maxim-ic.com/en/ds/MAX6642.pdf
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
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, I2C_CLIENT_END };

/*
 * The MAX6642 registers
 */

#define MAX6642_REG_R_MAN_ID		0xFE
#define MAX6642_REG_R_CONFIG		0x03
#define MAX6642_REG_W_CONFIG		0x09
#define MAX6642_REG_R_STATUS		0x02
#define MAX6642_REG_R_LOCAL_TEMP	0x00
#define MAX6642_REG_R_LOCAL_TEMPL	0x11
#define MAX6642_REG_R_LOCAL_HIGH	0x05
#define MAX6642_REG_W_LOCAL_HIGH	0x0B
#define MAX6642_REG_R_REMOTE_TEMP	0x01
#define MAX6642_REG_R_REMOTE_TEMPL	0x10
#define MAX6642_REG_R_REMOTE_HIGH	0x07
#define MAX6642_REG_W_REMOTE_HIGH	0x0D

/*
 * Conversions
 */

static int temp_from_reg10(int val)
{
	return val * 250;
}

static int temp_from_reg(int val)
{
	return val * 1000;
}

static int temp_to_reg(int val)
{
	return val / 1000;
}

/*
 * Client data (each client gets its own)
 */

struct max6642_data {
	struct i2c_client *client;
	struct mutex update_lock;
	bool valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	u16 temp_input[2]; /* local/remote */
	u16 temp_high[2]; /* local/remote */
	u8 alarms;
};

/*
 * Real code
 */

static void max6642_init_client(struct max6642_data *data,
				struct i2c_client *client)
{
	u8 config;

	/*
	 * Start the conversions.
	 */
	config = i2c_smbus_read_byte_data(client, MAX6642_REG_R_CONFIG);
	if (config & 0x40)
		i2c_smbus_write_byte_data(client, MAX6642_REG_W_CONFIG,
					  config & 0xBF); /* run */

	data->temp_high[0] = i2c_smbus_read_byte_data(client,
				MAX6642_REG_R_LOCAL_HIGH);
	data->temp_high[1] = i2c_smbus_read_byte_data(client,
				MAX6642_REG_R_REMOTE_HIGH);
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int max6642_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	u8 reg_config, reg_status, man_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* identification */
	man_id = i2c_smbus_read_byte_data(client, MAX6642_REG_R_MAN_ID);
	if (man_id != 0x4D)
		return -ENODEV;

	/* sanity check */
	if (i2c_smbus_read_byte_data(client, 0x04) != 0x4D
	    || i2c_smbus_read_byte_data(client, 0x06) != 0x4D
	    || i2c_smbus_read_byte_data(client, 0xff) != 0x4D)
		return -ENODEV;

	/*
	 * We read the config and status register, the 4 lower bits in the
	 * config register should be zero and bit 5, 3, 1 and 0 should be
	 * zero in the status register.
	 */
	reg_config = i2c_smbus_read_byte_data(client, MAX6642_REG_R_CONFIG);
	if ((reg_config & 0x0f) != 0x00)
		return -ENODEV;

	/* in between, another round of sanity checks */
	if (i2c_smbus_read_byte_data(client, 0x04) != reg_config
	    || i2c_smbus_read_byte_data(client, 0x06) != reg_config
	    || i2c_smbus_read_byte_data(client, 0xff) != reg_config)
		return -ENODEV;

	reg_status = i2c_smbus_read_byte_data(client, MAX6642_REG_R_STATUS);
	if ((reg_status & 0x2b) != 0x00)
		return -ENODEV;

	strlcpy(info->type, "max6642", I2C_NAME_SIZE);

	return 0;
}

static struct max6642_data *max6642_update_device(struct device *dev)
{
	struct max6642_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u16 val, tmp;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		dev_dbg(dev, "Updating max6642 data.\n");
		val = i2c_smbus_read_byte_data(client,
					MAX6642_REG_R_LOCAL_TEMPL);
		tmp = (val >> 6) & 3;
		val = i2c_smbus_read_byte_data(client,
					MAX6642_REG_R_LOCAL_TEMP);
		val = (val << 2) | tmp;
		data->temp_input[0] = val;
		val = i2c_smbus_read_byte_data(client,
					MAX6642_REG_R_REMOTE_TEMPL);
		tmp = (val >> 6) & 3;
		val = i2c_smbus_read_byte_data(client,
					MAX6642_REG_R_REMOTE_TEMP);
		val = (val << 2) | tmp;
		data->temp_input[1] = val;
		data->alarms = i2c_smbus_read_byte_data(client,
					MAX6642_REG_R_STATUS);

		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Sysfs stuff
 */

static ssize_t temp_max10_show(struct device *dev,
			       struct device_attribute *dev_attr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct max6642_data *data = max6642_update_device(dev);

	return sprintf(buf, "%d\n",
		       temp_from_reg10(data->temp_input[attr->index]));
}

static ssize_t temp_max_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *attr2 = to_sensor_dev_attr_2(attr);
	struct max6642_data *data = max6642_update_device(dev);

	return sprintf(buf, "%d\n", temp_from_reg(data->temp_high[attr2->nr]));
}

static ssize_t temp_max_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct sensor_device_attribute_2 *attr2 = to_sensor_dev_attr_2(attr);
	struct max6642_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_high[attr2->nr] = clamp_val(temp_to_reg(val), 0, 255);
	i2c_smbus_write_byte_data(data->client, attr2->index,
				  data->temp_high[attr2->nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct max6642_data *data = max6642_update_device(dev);
	return sprintf(buf, "%d\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp_max10, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp_max10, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp1_max, temp_max, 0,
			       MAX6642_REG_W_LOCAL_HIGH);
static SENSOR_DEVICE_ATTR_2_RW(temp2_max, temp_max, 1,
			       MAX6642_REG_W_REMOTE_HIGH);
static SENSOR_DEVICE_ATTR_RO(temp2_fault, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, alarm, 6);
static SENSOR_DEVICE_ATTR_RO(temp2_max_alarm, alarm, 4);

static struct attribute *max6642_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,

	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(max6642);

static int max6642_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max6642_data *data;
	struct device *hwmon_dev;

	data = devm_kzalloc(dev, sizeof(struct max6642_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	/* Initialize the MAX6642 chip */
	max6642_init_client(data, client);

	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev,
							   client->name, data,
							   max6642_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id max6642_id[] = {
	{ "max6642", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max6642_id);

static struct i2c_driver max6642_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "max6642",
	},
	.probe_new	= max6642_probe,
	.id_table	= max6642_id,
	.detect		= max6642_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(max6642_driver);

MODULE_AUTHOR("Per Dalen <per.dalen@appeartv.com>");
MODULE_DESCRIPTION("MAX6642 sensor driver");
MODULE_LICENSE("GPL");
