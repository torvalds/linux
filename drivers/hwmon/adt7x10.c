/*
 * adt7x10.c - Part of lm_sensors, Linux kernel modules for hardware
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
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "adt7x10.h"

/*
 * ADT7X10 status
 */
#define ADT7X10_STAT_T_LOW		(1 << 4)
#define ADT7X10_STAT_T_HIGH		(1 << 5)
#define ADT7X10_STAT_T_CRIT		(1 << 6)
#define ADT7X10_STAT_NOT_RDY		(1 << 7)

/*
 * ADT7X10 config
 */
#define ADT7X10_FAULT_QUEUE_MASK	(1 << 0 | 1 << 1)
#define ADT7X10_CT_POLARITY		(1 << 2)
#define ADT7X10_INT_POLARITY		(1 << 3)
#define ADT7X10_EVENT_MODE		(1 << 4)
#define ADT7X10_MODE_MASK		(1 << 5 | 1 << 6)
#define ADT7X10_FULL			(0 << 5 | 0 << 6)
#define ADT7X10_PD			(1 << 5 | 1 << 6)
#define ADT7X10_RESOLUTION		(1 << 7)

/*
 * ADT7X10 masks
 */
#define ADT7X10_T13_VALUE_MASK		0xFFF8
#define ADT7X10_T_HYST_MASK		0xF

/* straight from the datasheet */
#define ADT7X10_TEMP_MIN (-55000)
#define ADT7X10_TEMP_MAX 150000

/* Each client has this additional data */
struct adt7x10_data {
	const struct adt7x10_ops *ops;
	const char		*name;
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

static int adt7x10_read_byte(struct device *dev, u8 reg)
{
	struct adt7x10_data *d = dev_get_drvdata(dev);
	return d->ops->read_byte(dev, reg);
}

static int adt7x10_write_byte(struct device *dev, u8 reg, u8 data)
{
	struct adt7x10_data *d = dev_get_drvdata(dev);
	return d->ops->write_byte(dev, reg, data);
}

static int adt7x10_read_word(struct device *dev, u8 reg)
{
	struct adt7x10_data *d = dev_get_drvdata(dev);
	return d->ops->read_word(dev, reg);
}

static int adt7x10_write_word(struct device *dev, u8 reg, u16 data)
{
	struct adt7x10_data *d = dev_get_drvdata(dev);
	return d->ops->write_word(dev, reg, data);
}

static const u8 ADT7X10_REG_TEMP[4] = {
	ADT7X10_TEMPERATURE,		/* input */
	ADT7X10_T_ALARM_HIGH,		/* high */
	ADT7X10_T_ALARM_LOW,		/* low */
	ADT7X10_T_CRIT,			/* critical */
};

static irqreturn_t adt7x10_irq_handler(int irq, void *private)
{
	struct device *dev = private;
	int status;

	status = adt7x10_read_byte(dev, ADT7X10_STATUS);
	if (status < 0)
		return IRQ_HANDLED;

	if (status & ADT7X10_STAT_T_HIGH)
		sysfs_notify(&dev->kobj, NULL, "temp1_max_alarm");
	if (status & ADT7X10_STAT_T_LOW)
		sysfs_notify(&dev->kobj, NULL, "temp1_min_alarm");
	if (status & ADT7X10_STAT_T_CRIT)
		sysfs_notify(&dev->kobj, NULL, "temp1_crit_alarm");

	return IRQ_HANDLED;
}

static int adt7x10_temp_ready(struct device *dev)
{
	int i, status;

	for (i = 0; i < 6; i++) {
		status = adt7x10_read_byte(dev, ADT7X10_STATUS);
		if (status < 0)
			return status;
		if (!(status & ADT7X10_STAT_NOT_RDY))
			return 0;
		msleep(60);
	}
	return -ETIMEDOUT;
}

static int adt7x10_update_temp(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		int temp;

		dev_dbg(dev, "Starting update\n");

		ret = adt7x10_temp_ready(dev); /* check for new value */
		if (ret)
			goto abort;

		temp = adt7x10_read_word(dev, ADT7X10_REG_TEMP[0]);
		if (temp < 0) {
			ret = temp;
			dev_dbg(dev, "Failed to read value: reg %d, error %d\n",
				ADT7X10_REG_TEMP[0], ret);
			goto abort;
		}
		data->temp[0] = temp;
		data->last_updated = jiffies;
		data->valid = true;
	}

abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int adt7x10_fill_cache(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);
	int ret;
	int i;

	for (i = 1; i < ARRAY_SIZE(data->temp); i++) {
		ret = adt7x10_read_word(dev, ADT7X10_REG_TEMP[i]);
		if (ret < 0) {
			dev_dbg(dev, "Failed to read value: reg %d, error %d\n",
				ADT7X10_REG_TEMP[i], ret);
			return ret;
		}
		data->temp[i] = ret;
	}

	ret = adt7x10_read_byte(dev, ADT7X10_T_HYST);
	if (ret < 0) {
		dev_dbg(dev, "Failed to read value: reg %d, error %d\n",
				ADT7X10_T_HYST, ret);
		return ret;
	}
	data->hyst = ret;

	return 0;
}

static s16 ADT7X10_TEMP_TO_REG(long temp)
{
	return DIV_ROUND_CLOSEST(clamp_val(temp, ADT7X10_TEMP_MIN,
					       ADT7X10_TEMP_MAX) * 128, 1000);
}

static int ADT7X10_REG_TO_TEMP(struct adt7x10_data *data, s16 reg)
{
	/* in 13 bit mode, bits 0-2 are status flags - mask them out */
	if (!(data->config & ADT7X10_RESOLUTION))
		reg &= ADT7X10_T13_VALUE_MASK;
	/*
	 * temperature is stored in twos complement format, in steps of
	 * 1/128°C
	 */
	return DIV_ROUND_CLOSEST(reg * 1000, 128);
}

/*-----------------------------------------------------------------------*/

/* sysfs attributes for hwmon */

static ssize_t adt7x10_show_temp(struct device *dev,
				 struct device_attribute *da,
				 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct adt7x10_data *data = dev_get_drvdata(dev);


	if (attr->index == 0) {
		int ret;

		ret = adt7x10_update_temp(dev);
		if (ret)
			return ret;
	}

	return sprintf(buf, "%d\n", ADT7X10_REG_TO_TEMP(data,
		       data->temp[attr->index]));
}

static ssize_t adt7x10_set_temp(struct device *dev,
				struct device_attribute *da,
				const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct adt7x10_data *data = dev_get_drvdata(dev);
	int nr = attr->index;
	long temp;
	int ret;

	ret = kstrtol(buf, 10, &temp);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);
	data->temp[nr] = ADT7X10_TEMP_TO_REG(temp);
	ret = adt7x10_write_word(dev, ADT7X10_REG_TEMP[nr], data->temp[nr]);
	if (ret)
		count = ret;
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t adt7x10_show_t_hyst(struct device *dev,
				   struct device_attribute *da,
				   char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct adt7x10_data *data = dev_get_drvdata(dev);
	int nr = attr->index;
	int hyst;

	hyst = (data->hyst & ADT7X10_T_HYST_MASK) * 1000;

	/*
	 * hysteresis is stored as a 4 bit offset in the device, convert it
	 * to an absolute value
	 */
	if (nr == 2)	/* min has positive offset, others have negative */
		hyst = -hyst;
	return sprintf(buf, "%d\n",
		       ADT7X10_REG_TO_TEMP(data, data->temp[nr]) - hyst);
}

static ssize_t adt7x10_set_t_hyst(struct device *dev,
				  struct device_attribute *da,
				  const char *buf, size_t count)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);
	int limit, ret;
	long hyst;

	ret = kstrtol(buf, 10, &hyst);
	if (ret)
		return ret;
	/* convert absolute hysteresis value to a 4 bit delta value */
	limit = ADT7X10_REG_TO_TEMP(data, data->temp[1]);
	hyst = clamp_val(hyst, ADT7X10_TEMP_MIN, ADT7X10_TEMP_MAX);
	data->hyst = clamp_val(DIV_ROUND_CLOSEST(limit - hyst, 1000),
				   0, ADT7X10_T_HYST_MASK);
	ret = adt7x10_write_byte(dev, ADT7X10_T_HYST, data->hyst);
	if (ret)
		return ret;

	return count;
}

static ssize_t adt7x10_show_alarm(struct device *dev,
				  struct device_attribute *da,
				  char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	int ret;

	ret = adt7x10_read_byte(dev, ADT7X10_STATUS);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", !!(ret & attr->index));
}

static ssize_t name_show(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, adt7x10_show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO,
			  adt7x10_show_temp, adt7x10_set_temp, 1);
static SENSOR_DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO,
			  adt7x10_show_temp, adt7x10_set_temp, 2);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IWUSR | S_IRUGO,
			  adt7x10_show_temp, adt7x10_set_temp, 3);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IWUSR | S_IRUGO,
			  adt7x10_show_t_hyst, adt7x10_set_t_hyst, 1);
static SENSOR_DEVICE_ATTR(temp1_min_hyst, S_IRUGO,
			  adt7x10_show_t_hyst, NULL, 2);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO,
			  adt7x10_show_t_hyst, NULL, 3);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, adt7x10_show_alarm,
			  NULL, ADT7X10_STAT_T_LOW);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, adt7x10_show_alarm,
			  NULL, ADT7X10_STAT_T_HIGH);
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, adt7x10_show_alarm,
			  NULL, ADT7X10_STAT_T_CRIT);
static DEVICE_ATTR_RO(name);

static struct attribute *adt7x10_attributes[] = {
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

static const struct attribute_group adt7x10_group = {
	.attrs = adt7x10_attributes,
};

int adt7x10_probe(struct device *dev, const char *name, int irq,
		  const struct adt7x10_ops *ops)
{
	struct adt7x10_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ops = ops;
	data->name = name;

	dev_set_drvdata(dev, data);
	mutex_init(&data->update_lock);

	/* configure as specified */
	ret = adt7x10_read_byte(dev, ADT7X10_CONFIG);
	if (ret < 0) {
		dev_dbg(dev, "Can't read config? %d\n", ret);
		return ret;
	}
	data->oldconfig = ret;

	/*
	 * Set to 16 bit resolution, continous conversion and comparator mode.
	 */
	data->config = data->oldconfig;
	data->config &= ~(ADT7X10_MODE_MASK | ADT7X10_CT_POLARITY |
			ADT7X10_INT_POLARITY);
	data->config |= ADT7X10_FULL | ADT7X10_RESOLUTION | ADT7X10_EVENT_MODE;

	if (data->config != data->oldconfig) {
		ret = adt7x10_write_byte(dev, ADT7X10_CONFIG, data->config);
		if (ret)
			return ret;
	}
	dev_dbg(dev, "Config %02x\n", data->config);

	ret = adt7x10_fill_cache(dev);
	if (ret)
		goto exit_restore;

	/* Register sysfs hooks */
	ret = sysfs_create_group(&dev->kobj, &adt7x10_group);
	if (ret)
		goto exit_restore;

	/*
	 * The I2C device will already have it's own 'name' attribute, but for
	 * the SPI device we need to register it. name will only be non NULL if
	 * the device doesn't register the 'name' attribute on its own.
	 */
	if (name) {
		ret = device_create_file(dev, &dev_attr_name);
		if (ret)
			goto exit_remove;
	}

	data->hwmon_dev = hwmon_device_register(dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		goto exit_remove_name;
	}

	if (irq > 0) {
		ret = request_threaded_irq(irq, NULL, adt7x10_irq_handler,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				dev_name(dev), dev);
		if (ret)
			goto exit_hwmon_device_unregister;
	}

	return 0;

exit_hwmon_device_unregister:
	hwmon_device_unregister(data->hwmon_dev);
exit_remove_name:
	if (name)
		device_remove_file(dev, &dev_attr_name);
exit_remove:
	sysfs_remove_group(&dev->kobj, &adt7x10_group);
exit_restore:
	adt7x10_write_byte(dev, ADT7X10_CONFIG, data->oldconfig);
	return ret;
}
EXPORT_SYMBOL_GPL(adt7x10_probe);

int adt7x10_remove(struct device *dev, int irq)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	if (irq > 0)
		free_irq(irq, dev);

	hwmon_device_unregister(data->hwmon_dev);
	if (data->name)
		device_remove_file(dev, &dev_attr_name);
	sysfs_remove_group(&dev->kobj, &adt7x10_group);
	if (data->oldconfig != data->config)
		adt7x10_write_byte(dev, ADT7X10_CONFIG, data->oldconfig);
	return 0;
}
EXPORT_SYMBOL_GPL(adt7x10_remove);

#ifdef CONFIG_PM_SLEEP

static int adt7x10_suspend(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	return adt7x10_write_byte(dev, ADT7X10_CONFIG,
		data->config | ADT7X10_PD);
}

static int adt7x10_resume(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	return adt7x10_write_byte(dev, ADT7X10_CONFIG, data->config);
}

SIMPLE_DEV_PM_OPS(adt7x10_dev_pm_ops, adt7x10_suspend, adt7x10_resume);
EXPORT_SYMBOL_GPL(adt7x10_dev_pm_ops);

#endif /* CONFIG_PM_SLEEP */

MODULE_AUTHOR("Hartmut Knaack");
MODULE_DESCRIPTION("ADT7410/ADT7420, ADT7310/ADT7320 common code");
MODULE_LICENSE("GPL");
