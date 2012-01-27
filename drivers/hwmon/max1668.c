/*
    Copyright (c) 2011 David George <david.george@ska.ac.za>

    based on adm1021.c
    some credit to Christoph Scheurer, but largely a rewrite

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

/* Addresses to scan */
static unsigned short max1668_addr_list[] = {
	0x18, 0x19, 0x1a, 0x29, 0x2a, 0x2b, 0x4c, 0x4d, 0x4e, I2C_CLIENT_END };

/* max1668 registers */

#define MAX1668_REG_TEMP(nr)	(nr)
#define MAX1668_REG_STAT1	0x05
#define MAX1668_REG_STAT2	0x06
#define MAX1668_REG_MAN_ID	0xfe
#define MAX1668_REG_DEV_ID	0xff

/* limits */

/* write high limits */
#define MAX1668_REG_LIMH_WR(nr)	(0x13 + 2 * (nr))
/* write low limits */
#define MAX1668_REG_LIML_WR(nr)	(0x14 + 2 * (nr))
/* read high limits */
#define MAX1668_REG_LIMH_RD(nr)	(0x08 + 2 * (nr))
/* read low limits */
#define MAX1668_REG_LIML_RD(nr)	(0x09 + 2 * (nr))

/* manufacturer and device ID Constants */
#define MAN_ID_MAXIM		0x4d
#define DEV_ID_MAX1668		0x3
#define DEV_ID_MAX1805		0x5
#define DEV_ID_MAX1989		0xb

/* read only mode module parameter */
static bool read_only;
module_param(read_only, bool, 0);
MODULE_PARM_DESC(read_only, "Don't set any values, read only mode");

enum chips { max1668, max1805, max1989 };

struct max1668_data {
	struct device *hwmon_dev;
	enum chips type;

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* 1x local and 4x remote */
	s8 temp_max[5];
	s8 temp_min[5];
	s8 temp[5];
	u16 alarms;
};

static struct max1668_data *max1668_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max1668_data *data = i2c_get_clientdata(client);
	struct max1668_data *ret = data;
	s32 val;
	int i;

	mutex_lock(&data->update_lock);

	if (data->valid && !time_after(jiffies,
			data->last_updated + HZ + HZ / 2))
		goto abort;

	for (i = 0; i < 5; i++) {
		val = i2c_smbus_read_byte_data(client, MAX1668_REG_TEMP(i));
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp[i] = (s8) val;

		val = i2c_smbus_read_byte_data(client, MAX1668_REG_LIMH_RD(i));
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp_max[i] = (s8) val;

		val = i2c_smbus_read_byte_data(client, MAX1668_REG_LIML_RD(i));
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp_min[i] = (s8) val;
	}

	val = i2c_smbus_read_byte_data(client, MAX1668_REG_STAT1);
	if (unlikely(val < 0)) {
		ret = ERR_PTR(val);
		goto abort;
	}
	data->alarms = val << 8;

	val = i2c_smbus_read_byte_data(client, MAX1668_REG_STAT2);
	if (unlikely(val < 0)) {
		ret = ERR_PTR(val);
		goto abort;
	}
	data->alarms |= val;

	data->last_updated = jiffies;
	data->valid = 1;
abort:
	mutex_unlock(&data->update_lock);

	return ret;
}

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max1668_data *data = max1668_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->temp[index] * 1000);
}

static ssize_t show_temp_max(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max1668_data *data = max1668_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->temp_max[index] * 1000);
}

static ssize_t show_temp_min(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max1668_data *data = max1668_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->temp_min[index] * 1000);
}

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct max1668_data *data = max1668_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%u\n", (data->alarms >> index) & 0x1);
}

static ssize_t show_fault(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max1668_data *data = max1668_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%u\n",
		       (data->alarms & (1 << 12)) && data->temp[index] == 127);
}

static ssize_t set_temp_max(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct max1668_data *data = i2c_get_clientdata(client);
	long temp;
	int ret;

	ret = kstrtol(buf, 10, &temp);
	if (ret < 0)
		return ret;

	mutex_lock(&data->update_lock);
	data->temp_max[index] = SENSORS_LIMIT(temp/1000, -128, 127);
	if (i2c_smbus_write_byte_data(client,
					MAX1668_REG_LIMH_WR(index),
					data->temp_max[index]))
		count = -EIO;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_temp_min(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct max1668_data *data = i2c_get_clientdata(client);
	long temp;
	int ret;

	ret = kstrtol(buf, 10, &temp);
	if (ret < 0)
		return ret;

	mutex_lock(&data->update_lock);
	data->temp_min[index] = SENSORS_LIMIT(temp/1000, -128, 127);
	if (i2c_smbus_write_byte_data(client,
					MAX1668_REG_LIML_WR(index),
					data->temp_max[index]))
		count = -EIO;
	mutex_unlock(&data->update_lock);

	return count;
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, show_temp_max,
				set_temp_max, 0);
static SENSOR_DEVICE_ATTR(temp1_min, S_IRUGO, show_temp_min,
				set_temp_min, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_max, S_IRUGO, show_temp_max,
				set_temp_max, 1);
static SENSOR_DEVICE_ATTR(temp2_min, S_IRUGO, show_temp_min,
				set_temp_min, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_max, S_IRUGO, show_temp_max,
				set_temp_max, 2);
static SENSOR_DEVICE_ATTR(temp3_min, S_IRUGO, show_temp_min,
				set_temp_min, 2);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, show_temp, NULL, 3);
static SENSOR_DEVICE_ATTR(temp4_max, S_IRUGO, show_temp_max,
				set_temp_max, 3);
static SENSOR_DEVICE_ATTR(temp4_min, S_IRUGO, show_temp_min,
				set_temp_min, 3);
static SENSOR_DEVICE_ATTR(temp5_input, S_IRUGO, show_temp, NULL, 4);
static SENSOR_DEVICE_ATTR(temp5_max, S_IRUGO, show_temp_max,
				set_temp_max, 4);
static SENSOR_DEVICE_ATTR(temp5_min, S_IRUGO, show_temp_min,
				set_temp_min, 4);

static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 14);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, show_alarm, NULL, 13);
static SENSOR_DEVICE_ATTR(temp2_min_alarm, S_IRUGO, show_alarm, NULL, 7);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(temp3_min_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp3_max_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(temp4_min_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp4_max_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp5_min_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp5_max_alarm, S_IRUGO, show_alarm, NULL, 0);

static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_fault, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_fault, S_IRUGO, show_fault, NULL, 2);
static SENSOR_DEVICE_ATTR(temp4_fault, S_IRUGO, show_fault, NULL, 3);
static SENSOR_DEVICE_ATTR(temp5_fault, S_IRUGO, show_fault, NULL, 4);

/* Attributes common to MAX1668, MAX1989 and MAX1805 */
static struct attribute *max1668_attribute_common[] = {
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,

	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_min_alarm.dev_attr.attr,

	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	NULL
};

/* Attributes not present on MAX1805 */
static struct attribute *max1668_attribute_unique[] = {
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp4_min.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp5_max.dev_attr.attr,
	&sensor_dev_attr_temp5_min.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,

	&sensor_dev_attr_temp4_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_min_alarm.dev_attr.attr,

	&sensor_dev_attr_temp4_fault.dev_attr.attr,
	&sensor_dev_attr_temp5_fault.dev_attr.attr,
	NULL
};

static umode_t max1668_attribute_mode(struct kobject *kobj,
				     struct attribute *attr, int index)
{
	umode_t ret = S_IRUGO;
	if (read_only)
		return ret;
	if (attr == &sensor_dev_attr_temp1_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp2_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp3_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp4_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp5_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp1_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp2_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp3_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp4_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp5_min.dev_attr.attr)
		ret |= S_IWUSR;
	return ret;
}

static const struct attribute_group max1668_group_common = {
	.attrs = max1668_attribute_common,
	.is_visible = max1668_attribute_mode
};

static const struct attribute_group max1668_group_unique = {
	.attrs = max1668_attribute_unique,
	.is_visible = max1668_attribute_mode
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int max1668_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	const char *type_name;
	int man_id, dev_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Check for unsupported part */
	man_id = i2c_smbus_read_byte_data(client, MAX1668_REG_MAN_ID);
	if (man_id != MAN_ID_MAXIM)
		return -ENODEV;

	dev_id = i2c_smbus_read_byte_data(client, MAX1668_REG_DEV_ID);
	if (dev_id < 0)
		return -ENODEV;

	type_name = NULL;
	if (dev_id == DEV_ID_MAX1668)
		type_name = "max1668";
	else if (dev_id == DEV_ID_MAX1805)
		type_name = "max1805";
	else if (dev_id == DEV_ID_MAX1989)
		type_name = "max1989";

	if (!type_name)
		return -ENODEV;

	strlcpy(info->type, type_name, I2C_NAME_SIZE);

	return 0;
}

static int max1668_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct max1668_data *data;
	int err;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	data = kzalloc(sizeof(struct max1668_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->type = id->driver_data;
	mutex_init(&data->update_lock);

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &max1668_group_common);
	if (err)
		goto error_free;

	if (data->type == max1668 || data->type == max1989) {
		err = sysfs_create_group(&client->dev.kobj,
					 &max1668_group_unique);
		if (err)
			goto error_sysrem0;
	}

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto error_sysrem1;
	}

	return 0;

error_sysrem1:
	if (data->type == max1668 || data->type == max1989)
		sysfs_remove_group(&client->dev.kobj, &max1668_group_unique);
error_sysrem0:
	sysfs_remove_group(&client->dev.kobj, &max1668_group_common);
error_free:
	kfree(data);
	return err;
}

static int max1668_remove(struct i2c_client *client)
{
	struct max1668_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	if (data->type == max1668 || data->type == max1989)
		sysfs_remove_group(&client->dev.kobj, &max1668_group_unique);

	sysfs_remove_group(&client->dev.kobj, &max1668_group_common);

	kfree(data);
	return 0;
}

static const struct i2c_device_id max1668_id[] = {
	{ "max1668", max1668 },
	{ "max1805", max1805 },
	{ "max1989", max1989 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max1668_id);

/* This is the driver that will be inserted */
static struct i2c_driver max1668_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		  .name	= "max1668",
		  },
	.probe = max1668_probe,
	.remove	= max1668_remove,
	.id_table = max1668_id,
	.detect	= max1668_detect,
	.address_list = max1668_addr_list,
};

static int __init sensors_max1668_init(void)
{
	return i2c_add_driver(&max1668_driver);
}

static void __exit sensors_max1668_exit(void)
{
	i2c_del_driver(&max1668_driver);
}

MODULE_AUTHOR("David George <david.george@ska.ac.za>");
MODULE_DESCRIPTION("MAX1668 remote temperature sensor driver");
MODULE_LICENSE("GPL");

module_init(sensors_max1668_init)
module_exit(sensors_max1668_exit)
