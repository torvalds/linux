// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * reg-virtual-consumer.c
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>

struct virtual_consumer_data {
	struct mutex lock;
	struct regulator *regulator;
	bool enabled;
	int min_uV;
	int max_uV;
	int min_uA;
	int max_uA;
	unsigned int mode;
};

static void update_voltage_constraints(struct device *dev,
				       struct virtual_consumer_data *data)
{
	int ret;

	if (data->min_uV && data->max_uV
	    && data->min_uV <= data->max_uV) {
		dev_dbg(dev, "Requesting %d-%duV\n",
			data->min_uV, data->max_uV);
		ret = regulator_set_voltage(data->regulator,
					data->min_uV, data->max_uV);
		if (ret != 0) {
			dev_err(dev,
				"regulator_set_voltage() failed: %d\n", ret);
			return;
		}
	}

	if (data->min_uV && data->max_uV && !data->enabled) {
		dev_dbg(dev, "Enabling regulator\n");
		ret = regulator_enable(data->regulator);
		if (ret == 0)
			data->enabled = true;
		else
			dev_err(dev, "regulator_enable() failed: %d\n",
				ret);
	}

	if (!(data->min_uV && data->max_uV) && data->enabled) {
		dev_dbg(dev, "Disabling regulator\n");
		ret = regulator_disable(data->regulator);
		if (ret == 0)
			data->enabled = false;
		else
			dev_err(dev, "regulator_disable() failed: %d\n",
				ret);
	}
}

static void update_current_limit_constraints(struct device *dev,
					  struct virtual_consumer_data *data)
{
	int ret;

	if (data->max_uA
	    && data->min_uA <= data->max_uA) {
		dev_dbg(dev, "Requesting %d-%duA\n",
			data->min_uA, data->max_uA);
		ret = regulator_set_current_limit(data->regulator,
					data->min_uA, data->max_uA);
		if (ret != 0) {
			dev_err(dev,
				"regulator_set_current_limit() failed: %d\n",
				ret);
			return;
		}
	}

	if (data->max_uA && !data->enabled) {
		dev_dbg(dev, "Enabling regulator\n");
		ret = regulator_enable(data->regulator);
		if (ret == 0)
			data->enabled = true;
		else
			dev_err(dev, "regulator_enable() failed: %d\n",
				ret);
	}

	if (!(data->min_uA && data->max_uA) && data->enabled) {
		dev_dbg(dev, "Disabling regulator\n");
		ret = regulator_disable(data->regulator);
		if (ret == 0)
			data->enabled = false;
		else
			dev_err(dev, "regulator_disable() failed: %d\n",
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

	if (kstrtol(buf, 10, &val) != 0)
		return count;

	mutex_lock(&data->lock);

	data->min_uV = val;
	update_voltage_constraints(dev, data);

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

	if (kstrtol(buf, 10, &val) != 0)
		return count;

	mutex_lock(&data->lock);

	data->max_uV = val;
	update_voltage_constraints(dev, data);

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

	if (kstrtol(buf, 10, &val) != 0)
		return count;

	mutex_lock(&data->lock);

	data->min_uA = val;
	update_current_limit_constraints(dev, data);

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

	if (kstrtol(buf, 10, &val) != 0)
		return count;

	mutex_lock(&data->lock);

	data->max_uA = val;
	update_current_limit_constraints(dev, data);

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

	/*
	 * sysfs_streq() doesn't need the \n's, but we add them so the strings
	 * will be shared with show_mode(), above.
	 */
	if (sysfs_streq(buf, "fast\n"))
		mode = REGULATOR_MODE_FAST;
	else if (sysfs_streq(buf, "normal\n"))
		mode = REGULATOR_MODE_NORMAL;
	else if (sysfs_streq(buf, "idle\n"))
		mode = REGULATOR_MODE_IDLE;
	else if (sysfs_streq(buf, "standby\n"))
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

static DEVICE_ATTR(min_microvolts, 0664, show_min_uV, set_min_uV);
static DEVICE_ATTR(max_microvolts, 0664, show_max_uV, set_max_uV);
static DEVICE_ATTR(min_microamps, 0664, show_min_uA, set_min_uA);
static DEVICE_ATTR(max_microamps, 0664, show_max_uA, set_max_uA);
static DEVICE_ATTR(mode, 0664, show_mode, set_mode);

static struct attribute *regulator_virtual_attributes[] = {
	&dev_attr_min_microvolts.attr,
	&dev_attr_max_microvolts.attr,
	&dev_attr_min_microamps.attr,
	&dev_attr_max_microamps.attr,
	&dev_attr_mode.attr,
	NULL
};

static const struct attribute_group regulator_virtual_attr_group = {
	.attrs	= regulator_virtual_attributes,
};

#ifdef CONFIG_OF
static const struct of_device_id regulator_virtual_consumer_of_match[] = {
	{ .compatible = "regulator-virtual-consumer" },
	{},
};
MODULE_DEVICE_TABLE(of, regulator_virtual_consumer_of_match);
#endif

static int regulator_virtual_probe(struct platform_device *pdev)
{
	char *reg_id = dev_get_platdata(&pdev->dev);
	struct virtual_consumer_data *drvdata;
	static bool warned;
	int ret;

	if (!warned) {
		warned = true;
		pr_warn("**********************************************************\n");
		pr_warn("**   NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE   **\n");
		pr_warn("**                                                      **\n");
		pr_warn("** regulator-virtual-consumer is only for testing and   **\n");
		pr_warn("** debugging.  Do not use it in a production kernel.    **\n");
		pr_warn("**                                                      **\n");
		pr_warn("**   NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE   **\n");
		pr_warn("**********************************************************\n");
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct virtual_consumer_data),
			       GFP_KERNEL);
	if (drvdata == NULL)
		return -ENOMEM;

	/*
	 * This virtual consumer does not have any hardware-defined supply
	 * name, so just allow the regulator to be specified in a property
	 * named "default-supply" when we're being probed from devicetree.
	 */
	if (!reg_id && pdev->dev.of_node)
		reg_id = "default";

	mutex_init(&drvdata->lock);

	drvdata->regulator = devm_regulator_get(&pdev->dev, reg_id);
	if (IS_ERR(drvdata->regulator))
		return dev_err_probe(&pdev->dev, PTR_ERR(drvdata->regulator),
				     "Failed to obtain supply '%s'\n",
				     reg_id);

	ret = sysfs_create_group(&pdev->dev.kobj,
				 &regulator_virtual_attr_group);
	if (ret != 0) {
		dev_err(&pdev->dev,
			"Failed to create attribute group: %d\n", ret);
		return ret;
	}

	drvdata->mode = regulator_get_mode(drvdata->regulator);

	platform_set_drvdata(pdev, drvdata);

	return 0;
}

static int regulator_virtual_remove(struct platform_device *pdev)
{
	struct virtual_consumer_data *drvdata = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &regulator_virtual_attr_group);

	if (drvdata->enabled)
		regulator_disable(drvdata->regulator);

	return 0;
}

static struct platform_driver regulator_virtual_consumer_driver = {
	.probe		= regulator_virtual_probe,
	.remove		= regulator_virtual_remove,
	.driver		= {
		.name		= "reg-virt-consumer",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(regulator_virtual_consumer_of_match),
	},
};

module_platform_driver(regulator_virtual_consumer_driver);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("Virtual regulator consumer");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:reg-virt-consumer");
