// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * adt7x10.c - Part of lm_sensors, Linux kernel modules for hardware
 *	 monitoring
 * This driver handles the ADT7410 and compatible digital temperature sensors.
 * Hartmut Knaack <knaack.h@gmx.de> 2012-07-22
 * based on lm75.c by Frodo Looijaard <frodol@dds.nl>
 * and adt7410.c from iio-staging by Sonic Zhang <sonic.zhang@analog.com>
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
#include <linux/regmap.h>

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
	struct regmap		*regmap;
	const char		*name;
	struct device		*hwmon_dev;
	struct mutex		update_lock;
	u8			config;
	u8			oldconfig;
	bool			valid;		/* true if temperature valid */
};

static const u8 ADT7X10_REG_TEMP[4] = {
	ADT7X10_TEMPERATURE,		/* input */
	ADT7X10_T_ALARM_HIGH,		/* high */
	ADT7X10_T_ALARM_LOW,		/* low */
	ADT7X10_T_CRIT,			/* critical */
};

static irqreturn_t adt7x10_irq_handler(int irq, void *private)
{
	struct device *dev = private;
	struct adt7x10_data *d = dev_get_drvdata(dev);
	unsigned int status;
	int ret;

	ret = regmap_read(d->regmap, ADT7X10_STATUS, &status);
	if (ret < 0)
		return IRQ_HANDLED;

	if (status & ADT7X10_STAT_T_HIGH)
		sysfs_notify(&dev->kobj, NULL, "temp1_max_alarm");
	if (status & ADT7X10_STAT_T_LOW)
		sysfs_notify(&dev->kobj, NULL, "temp1_min_alarm");
	if (status & ADT7X10_STAT_T_CRIT)
		sysfs_notify(&dev->kobj, NULL, "temp1_crit_alarm");

	return IRQ_HANDLED;
}

static int adt7x10_temp_ready(struct regmap *regmap)
{
	unsigned int status;
	int i, ret;

	for (i = 0; i < 6; i++) {
		ret = regmap_read(regmap, ADT7X10_STATUS, &status);
		if (ret < 0)
			return ret;
		if (!(status & ADT7X10_STAT_NOT_RDY))
			return 0;
		msleep(60);
	}
	return -ETIMEDOUT;
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

static ssize_t adt7x10_temp_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct adt7x10_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	mutex_lock(&data->update_lock);
	if (attr->index == 0 && !data->valid) {
		/* wait for valid temperature */
		ret = adt7x10_temp_ready(data->regmap);
		if (ret) {
			mutex_unlock(&data->update_lock);
			return ret;
		}
		data->valid = true;
	}
	mutex_unlock(&data->update_lock);

	ret = regmap_read(data->regmap, ADT7X10_REG_TEMP[attr->index], &val);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", ADT7X10_REG_TO_TEMP(data, val));
}

static ssize_t adt7x10_temp_store(struct device *dev,
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
	ret = regmap_write(data->regmap, ADT7X10_REG_TEMP[nr],
			   ADT7X10_TEMP_TO_REG(temp));
	mutex_unlock(&data->update_lock);
	return ret ? : count;
}

static ssize_t adt7x10_t_hyst_show(struct device *dev,
				   struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct adt7x10_data *data = dev_get_drvdata(dev);
	int nr = attr->index;
	int hyst, temp, ret;

	mutex_lock(&data->update_lock);
	ret = regmap_read(data->regmap, ADT7X10_T_HYST, &hyst);
	if (ret) {
		mutex_unlock(&data->update_lock);
		return ret;
	}

	ret = regmap_read(data->regmap, ADT7X10_REG_TEMP[nr], &temp);
	mutex_unlock(&data->update_lock);
	if (ret)
		return ret;

	hyst = (hyst & ADT7X10_T_HYST_MASK) * 1000;

	/*
	 * hysteresis is stored as a 4 bit offset in the device, convert it
	 * to an absolute value
	 */
	if (nr == 2)	/* min has positive offset, others have negative */
		hyst = -hyst;

	return sprintf(buf, "%d\n", ADT7X10_REG_TO_TEMP(data, temp) - hyst);
}

static ssize_t adt7x10_t_hyst_store(struct device *dev,
				    struct device_attribute *da,
				    const char *buf, size_t count)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int limit, ret;
	long hyst;

	ret = kstrtol(buf, 10, &hyst);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);

	/* convert absolute hysteresis value to a 4 bit delta value */
	ret = regmap_read(data->regmap, ADT7X10_T_ALARM_HIGH, &regval);
	if (ret < 0)
		goto abort;

	limit = ADT7X10_REG_TO_TEMP(data, regval);

	hyst = clamp_val(hyst, ADT7X10_TEMP_MIN, ADT7X10_TEMP_MAX);
	regval = clamp_val(DIV_ROUND_CLOSEST(limit - hyst, 1000), 0,
			   ADT7X10_T_HYST_MASK);
	ret = regmap_write(data->regmap, ADT7X10_T_HYST, regval);
abort:
	mutex_unlock(&data->update_lock);
	return ret ? : count;
}

static ssize_t adt7x10_alarm_show(struct device *dev,
				  struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct adt7x10_data *data = dev_get_drvdata(dev);
	unsigned int status;
	int ret;

	ret = regmap_read(data->regmap, ADT7X10_STATUS, &status);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", !!(status & attr->index));
}

static ssize_t name_show(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, adt7x10_temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_max, adt7x10_temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_min, adt7x10_temp, 2);
static SENSOR_DEVICE_ATTR_RW(temp1_crit, adt7x10_temp, 3);
static SENSOR_DEVICE_ATTR_RW(temp1_max_hyst, adt7x10_t_hyst, 1);
static SENSOR_DEVICE_ATTR_RO(temp1_min_hyst, adt7x10_t_hyst, 2);
static SENSOR_DEVICE_ATTR_RO(temp1_crit_hyst, adt7x10_t_hyst, 3);
static SENSOR_DEVICE_ATTR_RO(temp1_min_alarm, adt7x10_alarm,
			     ADT7X10_STAT_T_LOW);
static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, adt7x10_alarm,
			     ADT7X10_STAT_T_HIGH);
static SENSOR_DEVICE_ATTR_RO(temp1_crit_alarm, adt7x10_alarm,
			     ADT7X10_STAT_T_CRIT);
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
		  struct regmap *regmap)
{
	struct adt7x10_data *data;
	unsigned int config;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = regmap;
	data->name = name;

	dev_set_drvdata(dev, data);
	mutex_init(&data->update_lock);

	/* configure as specified */
	ret = regmap_read(regmap, ADT7X10_CONFIG, &config);
	if (ret < 0) {
		dev_dbg(dev, "Can't read config? %d\n", ret);
		return ret;
	}
	data->oldconfig = config;

	/*
	 * Set to 16 bit resolution, continous conversion and comparator mode.
	 */
	data->config = data->oldconfig;
	data->config &= ~(ADT7X10_MODE_MASK | ADT7X10_CT_POLARITY |
			ADT7X10_INT_POLARITY);
	data->config |= ADT7X10_FULL | ADT7X10_RESOLUTION | ADT7X10_EVENT_MODE;

	if (data->config != data->oldconfig) {
		ret = regmap_write(regmap, ADT7X10_CONFIG, data->config);
		if (ret)
			return ret;
	}
	dev_dbg(dev, "Config %02x\n", data->config);

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
	regmap_write(regmap, ADT7X10_CONFIG, data->oldconfig);
	return ret;
}
EXPORT_SYMBOL_GPL(adt7x10_probe);

void adt7x10_remove(struct device *dev, int irq)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	if (irq > 0)
		free_irq(irq, dev);

	hwmon_device_unregister(data->hwmon_dev);
	if (data->name)
		device_remove_file(dev, &dev_attr_name);
	sysfs_remove_group(&dev->kobj, &adt7x10_group);
	if (data->oldconfig != data->config)
		regmap_write(data->regmap, ADT7X10_CONFIG, data->oldconfig);
}
EXPORT_SYMBOL_GPL(adt7x10_remove);

#ifdef CONFIG_PM_SLEEP

static int adt7x10_suspend(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	return regmap_write(data->regmap, ADT7X10_CONFIG,
			    data->config | ADT7X10_PD);
}

static int adt7x10_resume(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	return regmap_write(data->regmap, ADT7X10_CONFIG, data->config);
}

SIMPLE_DEV_PM_OPS(adt7x10_dev_pm_ops, adt7x10_suspend, adt7x10_resume);
EXPORT_SYMBOL_GPL(adt7x10_dev_pm_ops);

#endif /* CONFIG_PM_SLEEP */

MODULE_AUTHOR("Hartmut Knaack");
MODULE_DESCRIPTION("ADT7410/ADT7420, ADT7310/ADT7320 common code");
MODULE_LICENSE("GPL");
