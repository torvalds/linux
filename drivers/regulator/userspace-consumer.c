// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * userspace-consumer.c
 *
 * Copyright 2009 CompuLab, Ltd.
 *
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Based of virtual consumer driver:
 *   Copyright 2008 Wolfson Microelectronics PLC.
 *   Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/userspace-consumer.h>
#include <linux/slab.h>

struct userspace_consumer_data {
	const char *name;

	struct mutex lock;
	bool enabled;
	bool no_autoswitch;

	int num_supplies;
	struct regulator_bulk_data *supplies;
};

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct userspace_consumer_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct userspace_consumer_data *data = dev_get_drvdata(dev);

	if (data->enabled)
		return sprintf(buf, "enabled\n");

	return sprintf(buf, "disabled\n");
}

static ssize_t state_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct userspace_consumer_data *data = dev_get_drvdata(dev);
	bool enabled;
	int ret;

	/*
	 * sysfs_streq() doesn't need the \n's, but we add them so the strings
	 * will be shared with show_state(), above.
	 */
	if (sysfs_streq(buf, "enabled\n") || sysfs_streq(buf, "1"))
		enabled = true;
	else if (sysfs_streq(buf, "disabled\n") || sysfs_streq(buf, "0"))
		enabled = false;
	else {
		dev_err(dev, "Configuring invalid mode\n");
		return count;
	}

	mutex_lock(&data->lock);
	if (enabled != data->enabled) {
		if (enabled)
			ret = regulator_bulk_enable(data->num_supplies,
						    data->supplies);
		else
			ret = regulator_bulk_disable(data->num_supplies,
						     data->supplies);

		if (ret == 0)
			data->enabled = enabled;
		else
			dev_err(dev, "Failed to configure state: %d\n", ret);
	}
	mutex_unlock(&data->lock);

	return count;
}

static DEVICE_ATTR_RO(name);
static DEVICE_ATTR_RW(state);

static struct attribute *attributes[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	NULL,
};

static umode_t attr_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct userspace_consumer_data *data = dev_get_drvdata(dev);

	/* If a name hasn't been set, don't bother with the attribute */
	if (attr == &dev_attr_name.attr && !data->name)
		return 0;

	return attr->mode;
}

static const struct attribute_group attr_group = {
	.attrs	= attributes,
	.is_visible =  attr_visible,
};

static int regulator_userspace_consumer_probe(struct platform_device *pdev)
{
	struct regulator_userspace_consumer_data tmpdata;
	struct regulator_userspace_consumer_data *pdata;
	struct userspace_consumer_data *drvdata;
	int ret;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		if (!pdev->dev.of_node)
			return -EINVAL;

		pdata = &tmpdata;
		memset(pdata, 0, sizeof(*pdata));

		pdata->no_autoswitch = true;
		pdata->num_supplies = 1;
		pdata->supplies = devm_kzalloc(&pdev->dev, sizeof(*pdata->supplies), GFP_KERNEL);
		if (!pdata->supplies)
			return -ENOMEM;
		pdata->supplies[0].supply = "vout";
	}

	if (pdata->num_supplies < 1) {
		dev_err(&pdev->dev, "At least one supply required\n");
		return -EINVAL;
	}

	drvdata = devm_kzalloc(&pdev->dev,
			       sizeof(struct userspace_consumer_data),
			       GFP_KERNEL);
	if (drvdata == NULL)
		return -ENOMEM;

	drvdata->name = pdata->name;
	drvdata->num_supplies = pdata->num_supplies;
	drvdata->supplies = pdata->supplies;
	drvdata->no_autoswitch = pdata->no_autoswitch;

	mutex_init(&drvdata->lock);

	ret = devm_regulator_bulk_get_exclusive(&pdev->dev, drvdata->num_supplies,
						drvdata->supplies);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get supplies: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, drvdata);

	ret = sysfs_create_group(&pdev->dev.kobj, &attr_group);
	if (ret != 0)
		return ret;

	if (pdata->init_on && !pdata->no_autoswitch) {
		ret = regulator_bulk_enable(drvdata->num_supplies,
					    drvdata->supplies);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to set initial state: %d\n", ret);
			goto err_enable;
		}
	}

	ret = regulator_is_enabled(pdata->supplies[0].consumer);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get regulator status\n");
		goto err_enable;
	}
	drvdata->enabled = !!ret;

	return 0;

err_enable:
	sysfs_remove_group(&pdev->dev.kobj, &attr_group);

	return ret;
}

static void regulator_userspace_consumer_remove(struct platform_device *pdev)
{
	struct userspace_consumer_data *data = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &attr_group);

	if (data->enabled && !data->no_autoswitch)
		regulator_bulk_disable(data->num_supplies, data->supplies);
}

static const struct of_device_id regulator_userspace_consumer_of_match[] = {
	{ .compatible = "regulator-output", },
	{},
};
MODULE_DEVICE_TABLE(of, regulator_userspace_consumer_of_match);

static struct platform_driver regulator_userspace_consumer_driver = {
	.probe		= regulator_userspace_consumer_probe,
	.remove_new	= regulator_userspace_consumer_remove,
	.driver		= {
		.name		= "reg-userspace-consumer",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table	= regulator_userspace_consumer_of_match,
	},
};

module_platform_driver(regulator_userspace_consumer_driver);

MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("Userspace consumer for voltage and current regulators");
MODULE_LICENSE("GPL");
