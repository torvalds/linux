/*
 * adt7410.c - Part of lm_sensors, Linux kernel modules for hardware
 *	 monitoring
 * This driver handles the ADT7410 and compatible digital temperature sensors.
 * Hartmut Knaack <knaack.h@gmx.de> 2012-07-22
 * based on lm75.c by Frodo Looijaard <frodol@dds.nl>
 * and adt7410.c from iio-staging by Sonic Zhang <sonic.zhang@analog.com>
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
#include <linux/delay.h>

/*
 * ADT7410 registers definition
 */

#define ADT7410_TEMPERATURE		0
#define ADT7410_STATUS			2
#define ADT7410_CONFIG			3
#define ADT7410_T_ALARM_HIGH		4
#define ADT7410_T_ALARM_LOW		6
#define ADT7410_T_CRIT			8
#define ADT7410_T_HYST			0xA

/*
 * ADT7410 status
 */
#define ADT7410_STAT_T_LOW		(1 << 4)
#define ADT7410_STAT_T_HIGH		(1 << 5)
#define ADT7410_STAT_T_CRIT		(1 << 6)
#define ADT7410_STAT_NOT_RDY		(1 << 7)

/*
 * ADT7410 config
 */
#define ADT7410_FAULT_QUEUE_MASK	(1 << 0 | 1 << 1)
#define ADT7410_CT_POLARITY		(1 << 2)
#define ADT7410_INT_POLARITY		(1 << 3)
#define ADT7410_EVENT_MODE		(1 << 4)
#define ADT7410_MODE_MASK		(1 << 5 | 1 << 6)
#define ADT7410_FULL			(0 << 5 | 0 << 6)
#define ADT7410_PD			(1 << 5 | 1 << 6)
#define ADT7410_RESOLUTION		(1 << 7)

/*
 * ADT7410 masks
 */
#define ADT7410_T13_VALUE_MASK			0xFFF8
#define ADT7410_T_HYST_MASK			0xF

/* straight from the datasheet */
#define ADT7410_TEMP_MIN (-55000)
#define ADT7410_TEMP_MAX 150000

enum adt7410_type {		/* keep sorted in alphabetical order */
	adt7410,
};

static const u8 ADT7410_REG_TEMP[4] = {
	ADT7410_TEMPERATURE,		/* input */
	ADT7410_T_ALARM_HIGH,		/* high */
	ADT7410_T_ALARM_LOW,		/* low */
	ADT7410_T_CRIT,			/* critical */
};

/* Each client has this additional data */
struct adt7410_data {
	struct device		*hwmon_dev;
	struct mutex		update_lock;
	u8			config;
	u8			oldconfig;
	bool			valid;		/* true if registers valid */
	unsigned long		last_updated;	/* In jiffies */
	s16			temp[4];	/* Register values,
						   0 = input
						   1 = high
						   2 = low
						   3 = critical */
	u8			hyst;		/* hysteresis offset */
};

/*
 * adt7410 register access by I2C
 */
static int adt7410_temp_ready(struct i2c_client *client)
{
	int i, status;

	for (i = 0; i < 6; i++) {
		status = i2c_smbus_read_byte_data(client, ADT7410_STATUS);
		if (status < 0)
			return status;
		if (!(status & ADT7410_STAT_NOT_RDY))
			return 0;
		msleep(60);
	}
	return -ETIMEDOUT;
}

static struct adt7410_data *adt7410_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7410_data *data = i2c_get_clientdata(client);
	struct adt7410_data *ret = data;
	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		int i, status;

		dev_dbg(&client->dev, "Starting update\n");

		status = adt7410_temp_ready(client); /* check for new value */
		if (unlikely(status)) {
			ret = ERR_PTR(status);
			goto abort;
		}
		for (i = 0; i < ARRAY_SIZE(data->temp); i++) {
			status = i2c_smbus_read_word_swapped(client,
							ADT7410_REG_TEMP[i]);
			if (unlikely(status < 0)) {
				dev_dbg(dev,
					"Failed to read value: reg %d, error %d\n",
					ADT7410_REG_TEMP[i], status);
				ret = ERR_PTR(status);
				goto abort;
			}
			data->temp[i] = status;
		}
		status = i2c_smbus_read_byte_data(client, ADT7410_T_HYST);
		if (unlikely(status < 0)) {
			dev_dbg(dev,
				"Failed to read value: reg %d, error %d\n",
				ADT7410_T_HYST, status);
			ret = ERR_PTR(status);
			goto abort;
		}
		data->hyst = status;
		data->last_updated = jiffies;
		data->valid = true;
	}

abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static s16 ADT7410_TEMP_TO_REG(long temp)
{
	return DIV_ROUND_CLOSEST(clamp_val(temp, ADT7410_TEMP_MIN,
					   ADT7410_TEMP_MAX) * 128, 1000);
}

static int ADT7410_REG_TO_TEMP(struct adt7410_data *data, s16 reg)
{
	/* in 13 bit mode, bits 0-2 are status flags - mask them out */
	if (!(data->config & ADT7410_RESOLUTION))
		reg &= ADT7410_T13_VALUE_MASK;
	/*
	 * temperature is stored in twos complement format, in steps of
	 * 1/128°C
	 */
	return DIV_ROUND_CLOSEST(reg * 1000, 128);
}

/*-----------------------------------------------------------------------*/

/* sysfs attributes for hwmon */

static ssize_t adt7410_show_temp(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct adt7410_data *data = adt7410_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", ADT7410_REG_TO_TEMP(data,
		       data->temp[attr->index]));
}

static ssize_t adt7410_set_temp(struct device *dev,
				struct device_attribute *da,
				const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7410_data *data = i2c_get_clientdata(client);
	int nr = attr->index;
	long temp;
	int ret;

	ret = kstrtol(buf, 10, &temp);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);
	data->temp[nr] = ADT7410_TEMP_TO_REG(temp);
	ret = i2c_smbus_write_word_swapped(client, ADT7410_REG_TEMP[nr],
					   data->temp[nr]);
	if (ret)
		count = ret;
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t adt7410_show_t_hyst(struct device *dev,
				   struct device_attribute *da,
				   char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct adt7410_data *data;
	int nr = attr->index;
	int hyst;

	data = adt7410_update_device(dev);
	if (IS_ERR(data))
		return PTR_ERR(data);
	hyst = (data->hyst & ADT7410_T_HYST_MASK) * 1000;

	/*
	 * hysteresis is stored as a 4 bit offset in the device, convert it
	 * to an absolute value
	 */
	if (nr == 2)	/* min has positive offset, others have negative */
		hyst = -hyst;
	return sprintf(buf, "%d\n",
		       ADT7410_REG_TO_TEMP(data, data->temp[nr]) - hyst);
}

static ssize_t adt7410_set_t_hyst(struct device *dev,
				  struct device_attribute *da,
				  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7410_data *data = i2c_get_clientdata(client);
	int limit, ret;
	long hyst;

	ret = kstrtol(buf, 10, &hyst);
	if (ret)
		return ret;
	/* convert absolute hysteresis value to a 4 bit delta value */
	limit = ADT7410_REG_TO_TEMP(data, data->temp[1]);
	hyst = clamp_val(hyst, ADT7410_TEMP_MIN, ADT7410_TEMP_MAX);
	data->hyst = clamp_val(DIV_ROUND_CLOSEST(limit - hyst, 1000), 0,
			       ADT7410_T_HYST_MASK);
	ret = i2c_smbus_write_byte_data(client, ADT7410_T_HYST, data->hyst);
	if (ret)
		return ret;

	return count;
}

static ssize_t adt7410_show_alarm(struct device *dev,
				  struct device_attribute *da,
				  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	int ret;

	ret = i2c_smbus_read_byte_data(client, ADT7410_STATUS);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", !!(ret & attr->index));
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, adt7410_show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO,
			  adt7410_show_temp, adt7410_set_temp, 1);
static SENSOR_DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO,
			  adt7410_show_temp, adt7410_set_temp, 2);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IWUSR | S_IRUGO,
			  adt7410_show_temp, adt7410_set_temp, 3);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IWUSR | S_IRUGO,
			  adt7410_show_t_hyst, adt7410_set_t_hyst, 1);
static SENSOR_DEVICE_ATTR(temp1_min_hyst, S_IRUGO,
			  adt7410_show_t_hyst, NULL, 2);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO,
			  adt7410_show_t_hyst, NULL, 3);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, adt7410_show_alarm,
			  NULL, ADT7410_STAT_T_LOW);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, adt7410_show_alarm,
			  NULL, ADT7410_STAT_T_HIGH);
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, adt7410_show_alarm,
			  NULL, ADT7410_STAT_T_CRIT);

static struct attribute *adt7410_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_min_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group adt7410_group = {
	.attrs = adt7410_attributes,
};

/*-----------------------------------------------------------------------*/

/* device probe and removal */

static int adt7410_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adt7410_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(struct adt7410_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* configure as specified */
	ret = i2c_smbus_read_byte_data(client, ADT7410_CONFIG);
	if (ret < 0) {
		dev_dbg(&client->dev, "Can't read config? %d\n", ret);
		return ret;
	}
	data->oldconfig = ret;
	/*
	 * Set to 16 bit resolution, continous conversion and comparator mode.
	 */
	ret &= ~ADT7410_MODE_MASK;
	data->config = ret | ADT7410_FULL | ADT7410_RESOLUTION |
			ADT7410_EVENT_MODE;
	if (data->config != data->oldconfig) {
		ret = i2c_smbus_write_byte_data(client, ADT7410_CONFIG,
						data->config);
		if (ret)
			return ret;
	}
	dev_dbg(&client->dev, "Config %02x\n", data->config);

	/* Register sysfs hooks */
	ret = sysfs_create_group(&client->dev.kobj, &adt7410_group);
	if (ret)
		goto exit_restore;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	dev_info(&client->dev, "sensor '%s'\n", client->name);

	return 0;

exit_remove:
	sysfs_remove_group(&client->dev.kobj, &adt7410_group);
exit_restore:
	i2c_smbus_write_byte_data(client, ADT7410_CONFIG, data->oldconfig);
	return ret;
}

static int adt7410_remove(struct i2c_client *client)
{
	struct adt7410_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &adt7410_group);
	if (data->oldconfig != data->config)
		i2c_smbus_write_byte_data(client, ADT7410_CONFIG,
					  data->oldconfig);
	return 0;
}

static const struct i2c_device_id adt7410_ids[] = {
	{ "adt7410", adt7410, },
	{ "adt7420", adt7410, },
	{ /* LIST END */ }
};
MODULE_DEVICE_TABLE(i2c, adt7410_ids);

#ifdef CONFIG_PM_SLEEP
static int adt7410_suspend(struct device *dev)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7410_data *data = i2c_get_clientdata(client);

	ret = i2c_smbus_write_byte_data(client, ADT7410_CONFIG,
					data->config | ADT7410_PD);
	return ret;
}

static int adt7410_resume(struct device *dev)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7410_data *data = i2c_get_clientdata(client);

	ret = i2c_smbus_write_byte_data(client, ADT7410_CONFIG, data->config);
	return ret;
}

static SIMPLE_DEV_PM_OPS(adt7410_dev_pm_ops, adt7410_suspend, adt7410_resume);

#define ADT7410_DEV_PM_OPS (&adt7410_dev_pm_ops)
#else
#define ADT7410_DEV_PM_OPS NULL
#endif /* CONFIG_PM */

static struct i2c_driver adt7410_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adt7410",
		.pm	= ADT7410_DEV_PM_OPS,
	},
	.probe		= adt7410_probe,
	.remove		= adt7410_remove,
	.id_table	= adt7410_ids,
	.address_list	= I2C_ADDRS(0x48, 0x49, 0x4a, 0x4b),
};

module_i2c_driver(adt7410_driver);

MODULE_AUTHOR("Hartmut Knaack");
MODULE_DESCRIPTION("ADT7410/ADT7420 driver");
MODULE_LICENSE("GPL");
