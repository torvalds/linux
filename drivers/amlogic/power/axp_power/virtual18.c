/*
 * reg-virtual-consumer.c
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct virtual_consumer_data {
	struct mutex lock;
	struct regulator *regulator;
	int enabled;
	int min_uV;
	int max_uV;
	int min_uA;
	int max_uA;
	unsigned int mode;
};

static void update_voltage_constraints(struct virtual_consumer_data *data)
{
	int ret;

	if (data->min_uV && data->max_uV
	    && data->min_uV <= data->max_uV) {
		ret = regulator_set_voltage(data->regulator,
					    data->min_uV, data->max_uV);
		if (ret != 0) {
			printk(KERN_ERR "regulator_set_voltage() failed: %d\n",
			       ret);
			return;
		}
	}

	if (data->min_uV && data->max_uV && !data->enabled) {
		ret = regulator_enable(data->regulator);
		if (ret == 0)
			data->enabled = 1;
		else
			printk(KERN_ERR "regulator_enable() failed: %d\n",
				ret);
	}

	if (!(data->min_uV && data->max_uV) && data->enabled) {
		ret = regulator_disable(data->regulator);
		if (ret == 0)
			data->enabled = 0;
		else
			printk(KERN_ERR "regulator_disable() failed: %d\n",
				ret);
	}
}

static void update_current_limit_constraints(struct virtual_consumer_data
						*data)
{
	int ret;

	if (data->max_uA
	    && data->min_uA <= data->max_uA) {
		ret = regulator_set_current_limit(data->regulator,
					data->min_uA, data->max_uA);
		if (ret != 0) {
			pr_err("regulator_set_current_limit() failed: %d\n",
			       ret);
			return;
		}
	}

	if (data->max_uA && !data->enabled) {
		ret = regulator_enable(data->regulator);
		if (ret == 0)
			data->enabled = 1;
		else
			printk(KERN_ERR "regulator_enable() failed: %d\n",
				ret);
	}

	if (!(data->min_uA && data->max_uA) && data->enabled) {
		ret = regulator_disable(data->regulator);
		if (ret == 0)
			data->enabled = 0;
		else
			printk(KERN_ERR "regulator_disable() failed: %d\n",
				ret);
	}
}

static ssize_t show_min_uV(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct virtual_consumer_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->min_uV);
}

static ssize_t set_min_uV(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct virtual_consumer_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) != 0)
		return count;

	mutex_lock(&data->lock);

	data->min_uV = val;
	update_voltage_constraints(data);

	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_max_uV(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct virtual_consumer_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->max_uV);
}

static ssize_t set_max_uV(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct virtual_consumer_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) != 0)
		return count;

	mutex_lock(&data->lock);

	data->max_uV = val;
	update_voltage_constraints(data);

	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_min_uA(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct virtual_consumer_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->min_uA);
}

static ssize_t set_min_uA(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct virtual_consumer_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) != 0)
		return count;

	mutex_lock(&data->lock);

	data->min_uA = val;
	update_current_limit_constraints(data);

	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_max_uA(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct virtual_consumer_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->max_uA);
}

static ssize_t set_max_uA(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct virtual_consumer_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) != 0)
		return count;

	mutex_lock(&data->lock);

	data->max_uA = val;
	update_current_limit_constraints(data);

	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_mode(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct virtual_consumer_data *data = dev_get_drvdata(dev);

	switch (data->mode) {
	case REGULATOR_MODE_FAST:
		return sprintf(buf, "fast\n");
	case REGULATOR_MODE_NORMAL:
		return sprintf(buf, "normal\n");
	case REGULATOR_MODE_IDLE:
		return sprintf(buf, "idle\n");
	case REGULATOR_MODE_STANDBY:
		return sprintf(buf, "standby\n");
	default:
		return sprintf(buf, "unknown\n");
	}
}

static ssize_t set_mode(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct virtual_consumer_data *data = dev_get_drvdata(dev);
	unsigned int mode;
	int ret;

	if (strncmp(buf, "fast", strlen("fast")) == 0)
		mode = REGULATOR_MODE_FAST;
	else if (strncmp(buf, "normal", strlen("normal")) == 0)
		mode = REGULATOR_MODE_NORMAL;
	else if (strncmp(buf, "idle", strlen("idle")) == 0)
		mode = REGULATOR_MODE_IDLE;
	else if (strncmp(buf, "standby", strlen("standby")) == 0)
		mode = REGULATOR_MODE_STANDBY;
	else {
		dev_err(dev, "Configuring invalid mode\n");
		return count;
	}

	mutex_lock(&data->lock);
	ret = regulator_set_mode(data->regulator, mode);
	if (ret == 0)
		data->mode = mode;
	else
		dev_err(dev, "Failed to configure mode: %d\n", ret);
	mutex_unlock(&data->lock);

	return count;
}

static DEVICE_ATTR(min_microvolts, 0666, show_min_uV, set_min_uV);
static DEVICE_ATTR(max_microvolts, 0666, show_max_uV, set_max_uV);
static DEVICE_ATTR(min_microamps, 0666, show_min_uA, set_min_uA);
static DEVICE_ATTR(max_microamps, 0666, show_max_uA, set_max_uA);
static DEVICE_ATTR(mode, 0666, show_mode, set_mode);

struct device_attribute *attributes_virtual[] = {
	&dev_attr_min_microvolts,
	&dev_attr_max_microvolts,
	&dev_attr_min_microamps,
	&dev_attr_max_microamps,
	&dev_attr_mode,
};

static int regulator_virtual_consumer_probe(struct platform_device *pdev)
{
	char *reg_id = pdev->dev.platform_data;
	struct virtual_consumer_data *drvdata;
	int ret, i;

	drvdata = kzalloc(sizeof(struct virtual_consumer_data), GFP_KERNEL);
	if (drvdata == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	mutex_init(&drvdata->lock);

	//drvdata->regulator = regulator_get(&pdev->dev, reg_id);
	drvdata->regulator = regulator_get(NULL, reg_id);
	//drvdata->regulator = regulator_get(NULL, "axp20_analog/fm");
	if (IS_ERR(drvdata->regulator)) {
		ret = PTR_ERR(drvdata->regulator);
		goto err;
	}
	
	for (i = 0; i < ARRAY_SIZE(attributes_virtual); i++) {
		ret = device_create_file(&pdev->dev, attributes_virtual[i]);
		if (ret != 0)
			goto err;
	}

	drvdata->mode = regulator_get_mode(drvdata->regulator);

	platform_set_drvdata(pdev, drvdata);

	return 0;

err:
	for (i = 0; i < ARRAY_SIZE(attributes_virtual); i++)
		device_remove_file(&pdev->dev, attributes_virtual[i]);
	kfree(drvdata);
	return ret;
}

static int regulator_virtual_consumer_remove(struct platform_device *pdev)
{
	struct virtual_consumer_data *drvdata = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes_virtual); i++)
		device_remove_file(&pdev->dev, attributes_virtual[i]);
	if (drvdata->enabled)
		regulator_disable(drvdata->regulator);
	regulator_put(drvdata->regulator);

	kfree(drvdata);

	return 0;
}

static struct platform_driver regulator_virtual_consumer_driver[] = {
	{
		.probe		= regulator_virtual_consumer_probe,
		.remove		= regulator_virtual_consumer_remove,
		.driver		= {
			.name		= "reg-18-cs-ldo2",
		},
	},{
		.probe		= regulator_virtual_consumer_probe,
		.remove		= regulator_virtual_consumer_remove,
		.driver		= {
			.name		= "reg-18-cs-ldo3",
		},
	},{
		.probe		= regulator_virtual_consumer_probe,
		.remove		= regulator_virtual_consumer_remove,
		.driver		= {
			.name		= "reg-18-cs-ldo4",
		},
	},{
		.probe		= regulator_virtual_consumer_probe,
		.remove		= regulator_virtual_consumer_remove,
		.driver		= {
			.name		= "reg-18-cs-ldo5",
		},
	},{
		.probe		= regulator_virtual_consumer_probe,
		.remove		= regulator_virtual_consumer_remove,
		.driver		= {
			.name		= "reg-18-cs-buck1",
		},
	},{
		.probe		= regulator_virtual_consumer_probe,
		.remove		= regulator_virtual_consumer_remove,
		.driver		= {
			.name		= "reg-18-cs-buck2",
		},
	},{
		.probe		= regulator_virtual_consumer_probe,
		.remove		= regulator_virtual_consumer_remove,
		.driver		= {
			.name		= "reg-18-cs-buck3",
		},
	},{
		.probe		= regulator_virtual_consumer_probe,
		.remove		= regulator_virtual_consumer_remove,
		.driver		= {
			.name		= "reg-18-cs-sw1",
		},
	},{
		.probe		= regulator_virtual_consumer_probe,
		.remove		= regulator_virtual_consumer_remove,
		.driver		= {
			.name		= "reg-18-cs-sw2",
		},
	},
};


static int __init regulator_virtual_consumer_init(void)
{
	int j,ret;
	for (j = 0; j < ARRAY_SIZE(regulator_virtual_consumer_driver); j++){ 
		ret =  platform_driver_register(&regulator_virtual_consumer_driver[j]);
		if (ret)
			goto creat_drivers_failed;
	}
	return ret;
		
creat_drivers_failed:
	while (j--)
		platform_driver_unregister(&regulator_virtual_consumer_driver[j]);
	return ret;
}
module_init(regulator_virtual_consumer_init);

static void __exit regulator_virtual_consumer_exit(void)
{
	int j;
	for (j = ARRAY_SIZE(regulator_virtual_consumer_driver) - 1; j >= 0; j--){ 
			platform_driver_unregister(&regulator_virtual_consumer_driver[j]);
	}
}
module_exit(regulator_virtual_consumer_exit);

MODULE_AUTHOR("Donglu Zhang Krosspower");
MODULE_DESCRIPTION("Virtual regulator consumer");
MODULE_LICENSE("GPL");
