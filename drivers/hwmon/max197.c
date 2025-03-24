// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim MAX197 A/D Converter driver
 *
 * Copyright (c) 2012 Savoir-faire Linux Inc.
 *          Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * For further information, see the Documentation/hwmon/max197.rst file.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/platform_device.h>
#include <linux/platform_data/max197.h>

#define MAX199_LIMIT	4000		/* 4V */
#define MAX197_LIMIT	10000		/* 10V */

#define MAX197_NUM_CH	8		/* 8 Analog Input Channels */

/* Control byte format */
#define MAX197_BIP	(1 << 3)	/* Bipolarity */
#define MAX197_RNG	(1 << 4)	/* Full range */

#define MAX197_SCALE	12207		/* Scale coefficient for raw data */

/* List of supported chips */
enum max197_chips { max197, max199 };

/**
 * struct max197_data - device instance specific data
 * @pdata:		Platform data.
 * @hwmon_dev:		The hwmon device.
 * @lock:		Read/Write mutex.
 * @limit:		Max range value (10V for MAX197, 4V for MAX199).
 * @scale:		Need to scale.
 * @ctrl_bytes:		Channels control byte.
 */
struct max197_data {
	struct max197_platform_data *pdata;
	struct device *hwmon_dev;
	struct mutex lock;
	int limit;
	bool scale;
	u8 ctrl_bytes[MAX197_NUM_CH];
};

static inline void max197_set_unipolarity(struct max197_data *data, int channel)
{
	data->ctrl_bytes[channel] &= ~MAX197_BIP;
}

static inline void max197_set_bipolarity(struct max197_data *data, int channel)
{
	data->ctrl_bytes[channel] |= MAX197_BIP;
}

static inline void max197_set_half_range(struct max197_data *data, int channel)
{
	data->ctrl_bytes[channel] &= ~MAX197_RNG;
}

static inline void max197_set_full_range(struct max197_data *data, int channel)
{
	data->ctrl_bytes[channel] |= MAX197_RNG;
}

static inline bool max197_is_bipolar(struct max197_data *data, int channel)
{
	return data->ctrl_bytes[channel] & MAX197_BIP;
}

static inline bool max197_is_full_range(struct max197_data *data, int channel)
{
	return data->ctrl_bytes[channel] & MAX197_RNG;
}

/* Function called on read access on in{0,1,2,3,4,5,6,7}_{min,max} */
static ssize_t max197_show_range(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct max197_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	int channel = attr->index;
	bool is_min = attr->nr;
	int range;

	if (mutex_lock_interruptible(&data->lock))
		return -ERESTARTSYS;

	range = max197_is_full_range(data, channel) ?
		data->limit : data->limit / 2;
	if (is_min) {
		if (max197_is_bipolar(data, channel))
			range = -range;
		else
			range = 0;
	}

	mutex_unlock(&data->lock);

	return sprintf(buf, "%d\n", range);
}

/* Function called on write access on in{0,1,2,3,4,5,6,7}_{min,max} */
static ssize_t max197_store_range(struct device *dev,
				  struct device_attribute *devattr,
				  const char *buf, size_t count)
{
	struct max197_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	int channel = attr->index;
	bool is_min = attr->nr;
	long value;
	int half = data->limit / 2;
	int full = data->limit;

	if (kstrtol(buf, 10, &value))
		return -EINVAL;

	if (is_min) {
		if (value <= -full)
			value = -full;
		else if (value < 0)
			value = -half;
		else
			value = 0;
	} else {
		if (value >= full)
			value = full;
		else
			value = half;
	}

	if (mutex_lock_interruptible(&data->lock))
		return -ERESTARTSYS;

	if (value == 0) {
		/* We can deduce only the polarity */
		max197_set_unipolarity(data, channel);
	} else if (value == -half) {
		max197_set_bipolarity(data, channel);
		max197_set_half_range(data, channel);
	} else if (value == -full) {
		max197_set_bipolarity(data, channel);
		max197_set_full_range(data, channel);
	} else if (value == half) {
		/* We can deduce only the range */
		max197_set_half_range(data, channel);
	} else if (value == full) {
		/* We can deduce only the range */
		max197_set_full_range(data, channel);
	}

	mutex_unlock(&data->lock);

	return count;
}

/* Function called on read access on in{0,1,2,3,4,5,6,7}_input */
static ssize_t max197_show_input(struct device *dev,
				 struct device_attribute *devattr,
				 char *buf)
{
	struct max197_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int channel = attr->index;
	s32 value;
	int ret;

	if (mutex_lock_interruptible(&data->lock))
		return -ERESTARTSYS;

	ret = data->pdata->convert(data->ctrl_bytes[channel]);
	if (ret < 0) {
		dev_err(dev, "conversion failed\n");
		goto unlock;
	}
	value = ret;

	/*
	 * Coefficient to apply on raw value.
	 * See Table 1. Full Scale and Zero Scale in the MAX197 datasheet.
	 */
	if (data->scale) {
		value *= MAX197_SCALE;
		if (max197_is_full_range(data, channel))
			value *= 2;
		value /= 10000;
	}

	ret = sprintf(buf, "%d\n", value);

unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	return sprintf(buf, "%s\n", pdev->name);
}

#define MAX197_SENSOR_DEVICE_ATTR_CH(chan)				\
	static SENSOR_DEVICE_ATTR(in##chan##_input, S_IRUGO,		\
				  max197_show_input, NULL, chan);	\
	static SENSOR_DEVICE_ATTR_2(in##chan##_min, S_IRUGO | S_IWUSR,	\
				    max197_show_range,			\
				    max197_store_range,			\
				    true, chan);			\
	static SENSOR_DEVICE_ATTR_2(in##chan##_max, S_IRUGO | S_IWUSR,	\
				    max197_show_range,			\
				    max197_store_range,			\
				    false, chan)

#define MAX197_SENSOR_DEV_ATTR_IN(chan)					\
	&sensor_dev_attr_in##chan##_input.dev_attr.attr,		\
	&sensor_dev_attr_in##chan##_max.dev_attr.attr,			\
	&sensor_dev_attr_in##chan##_min.dev_attr.attr

static DEVICE_ATTR_RO(name);

MAX197_SENSOR_DEVICE_ATTR_CH(0);
MAX197_SENSOR_DEVICE_ATTR_CH(1);
MAX197_SENSOR_DEVICE_ATTR_CH(2);
MAX197_SENSOR_DEVICE_ATTR_CH(3);
MAX197_SENSOR_DEVICE_ATTR_CH(4);
MAX197_SENSOR_DEVICE_ATTR_CH(5);
MAX197_SENSOR_DEVICE_ATTR_CH(6);
MAX197_SENSOR_DEVICE_ATTR_CH(7);

static const struct attribute_group max197_sysfs_group = {
	.attrs = (struct attribute *[]) {
		&dev_attr_name.attr,
		MAX197_SENSOR_DEV_ATTR_IN(0),
		MAX197_SENSOR_DEV_ATTR_IN(1),
		MAX197_SENSOR_DEV_ATTR_IN(2),
		MAX197_SENSOR_DEV_ATTR_IN(3),
		MAX197_SENSOR_DEV_ATTR_IN(4),
		MAX197_SENSOR_DEV_ATTR_IN(5),
		MAX197_SENSOR_DEV_ATTR_IN(6),
		MAX197_SENSOR_DEV_ATTR_IN(7),
		NULL
	},
};

static int max197_probe(struct platform_device *pdev)
{
	int ch, ret;
	struct max197_data *data;
	struct max197_platform_data *pdata = dev_get_platdata(&pdev->dev);
	enum max197_chips chip = platform_get_device_id(pdev)->driver_data;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "no platform data supplied\n");
		return -EINVAL;
	}

	if (pdata->convert == NULL) {
		dev_err(&pdev->dev, "no convert function supplied\n");
		return -EINVAL;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(struct max197_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdata = pdata;
	mutex_init(&data->lock);

	if (chip == max197) {
		data->limit = MAX197_LIMIT;
		data->scale = true;
	} else {
		data->limit = MAX199_LIMIT;
		data->scale = false;
	}

	for (ch = 0; ch < MAX197_NUM_CH; ch++)
		data->ctrl_bytes[ch] = (u8) ch;

	platform_set_drvdata(pdev, data);

	ret = sysfs_create_group(&pdev->dev.kobj, &max197_sysfs_group);
	if (ret) {
		dev_err(&pdev->dev, "sysfs create group failed\n");
		return ret;
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		dev_err(&pdev->dev, "hwmon device register failed\n");
		goto error;
	}

	return 0;

error:
	sysfs_remove_group(&pdev->dev.kobj, &max197_sysfs_group);
	return ret;
}

static void max197_remove(struct platform_device *pdev)
{
	struct max197_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &max197_sysfs_group);
}

static const struct platform_device_id max197_device_ids[] = {
	{ "max197", max197 },
	{ "max199", max199 },
	{ }
};
MODULE_DEVICE_TABLE(platform, max197_device_ids);

static struct platform_driver max197_driver = {
	.driver = {
		.name = "max197",
	},
	.probe = max197_probe,
	.remove = max197_remove,
	.id_table = max197_device_ids,
};
module_platform_driver(max197_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Savoir-faire Linux Inc. <kernel@savoirfairelinux.com>");
MODULE_DESCRIPTION("Maxim MAX197 A/D Converter driver");
