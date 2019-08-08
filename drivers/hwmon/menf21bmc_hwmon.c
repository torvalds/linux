// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  MEN 14F021P00 Board Management Controller (BMC) hwmon driver.
 *
 *  This is the core hwmon driver of the MEN 14F021P00 BMC.
 *  The BMC monitors the board voltages which can be access with this
 *  driver through sysfs.
 *
 *  Copyright (C) 2014 MEN Mikro Elektronik Nuernberg GmbH
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>

#define DRV_NAME  "menf21bmc_hwmon"

#define BMC_VOLT_COUNT	5
#define MENF21BMC_V33	0
#define MENF21BMC_V5	1
#define MENF21BMC_V12	2
#define MENF21BMC_V5_SB	3
#define MENF21BMC_VBAT	4

#define IDX_TO_VOLT_MIN_CMD(idx) (0x40 + idx)
#define IDX_TO_VOLT_MAX_CMD(idx) (0x50 + idx)
#define IDX_TO_VOLT_INP_CMD(idx) (0x60 + idx)

struct menf21bmc_hwmon {
	bool valid;
	struct i2c_client *i2c_client;
	unsigned long last_update;
	int in_val[BMC_VOLT_COUNT];
	int in_min[BMC_VOLT_COUNT];
	int in_max[BMC_VOLT_COUNT];
};

static const char *const input_names[] = {
	[MENF21BMC_V33]		= "MON_3_3V",
	[MENF21BMC_V5]		= "MON_5V",
	[MENF21BMC_V12]		= "MON_12V",
	[MENF21BMC_V5_SB]	= "5V_STANDBY",
	[MENF21BMC_VBAT]	= "VBAT"
};

static struct menf21bmc_hwmon *menf21bmc_hwmon_update(struct device *dev)
{
	int i;
	int val;
	struct menf21bmc_hwmon *drv_data = dev_get_drvdata(dev);
	struct menf21bmc_hwmon *data_ret = drv_data;

	if (time_after(jiffies, drv_data->last_update + HZ)
	    || !drv_data->valid) {
		for (i = 0; i < BMC_VOLT_COUNT; i++) {
			val = i2c_smbus_read_word_data(drv_data->i2c_client,
						       IDX_TO_VOLT_INP_CMD(i));
			if (val < 0) {
				data_ret = ERR_PTR(val);
				goto abort;
			}
			drv_data->in_val[i] = val;
		}
		drv_data->last_update = jiffies;
		drv_data->valid = true;
	}
abort:
	return data_ret;
}

static int menf21bmc_hwmon_get_volt_limits(struct menf21bmc_hwmon *drv_data)
{
	int i, val;

	for (i = 0; i < BMC_VOLT_COUNT; i++) {
		val = i2c_smbus_read_word_data(drv_data->i2c_client,
					       IDX_TO_VOLT_MIN_CMD(i));
		if (val < 0)
			return val;

		drv_data->in_min[i] = val;

		val = i2c_smbus_read_word_data(drv_data->i2c_client,
					       IDX_TO_VOLT_MAX_CMD(i));
		if (val < 0)
			return val;

		drv_data->in_max[i] = val;
	}
	return 0;
}

static ssize_t
label_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return sprintf(buf, "%s\n", input_names[attr->index]);
}

static ssize_t
in_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct menf21bmc_hwmon *drv_data = menf21bmc_hwmon_update(dev);

	if (IS_ERR(drv_data))
		return PTR_ERR(drv_data);

	return sprintf(buf, "%d\n", drv_data->in_val[attr->index]);
}

static ssize_t
min_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct menf21bmc_hwmon *drv_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", drv_data->in_min[attr->index]);
}

static ssize_t
max_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct menf21bmc_hwmon *drv_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", drv_data->in_max[attr->index]);
}

static SENSOR_DEVICE_ATTR_RO(in0_input, in, 0);
static SENSOR_DEVICE_ATTR_RO(in0_min, min, 0);
static SENSOR_DEVICE_ATTR_RO(in0_max, max, 0);
static SENSOR_DEVICE_ATTR_RO(in0_label, label, 0);
static SENSOR_DEVICE_ATTR_RO(in1_input, in, 1);
static SENSOR_DEVICE_ATTR_RO(in1_min, min, 1);
static SENSOR_DEVICE_ATTR_RO(in1_max, max, 1);
static SENSOR_DEVICE_ATTR_RO(in1_label, label, 1);
static SENSOR_DEVICE_ATTR_RO(in2_input, in, 2);
static SENSOR_DEVICE_ATTR_RO(in2_min, min, 2);
static SENSOR_DEVICE_ATTR_RO(in2_max, max, 2);
static SENSOR_DEVICE_ATTR_RO(in2_label, label, 2);
static SENSOR_DEVICE_ATTR_RO(in3_input, in, 3);
static SENSOR_DEVICE_ATTR_RO(in3_min, min, 3);
static SENSOR_DEVICE_ATTR_RO(in3_max, max, 3);
static SENSOR_DEVICE_ATTR_RO(in3_label, label, 3);
static SENSOR_DEVICE_ATTR_RO(in4_input, in, 4);
static SENSOR_DEVICE_ATTR_RO(in4_min, min, 4);
static SENSOR_DEVICE_ATTR_RO(in4_max, max, 4);
static SENSOR_DEVICE_ATTR_RO(in4_label, label, 4);

static struct attribute *menf21bmc_hwmon_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_label.dev_attr.attr,

	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_label.dev_attr.attr,

	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_label.dev_attr.attr,

	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_label.dev_attr.attr,

	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_label.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(menf21bmc_hwmon);

static int menf21bmc_hwmon_probe(struct platform_device *pdev)
{
	int ret;
	struct menf21bmc_hwmon *drv_data;
	struct i2c_client *i2c_client = to_i2c_client(pdev->dev.parent);
	struct device *hwmon_dev;

	drv_data = devm_kzalloc(&pdev->dev, sizeof(struct menf21bmc_hwmon),
				GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->i2c_client = i2c_client;

	ret = menf21bmc_hwmon_get_volt_limits(drv_data);
	if (ret) {
		dev_err(&pdev->dev, "failed to read sensor limits");
		return ret;
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(&pdev->dev,
						   "menf21bmc", drv_data,
						   menf21bmc_hwmon_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(&pdev->dev, "MEN 14F021P00 BMC hwmon device enabled");

	return 0;
}

static struct platform_driver menf21bmc_hwmon = {
	.probe		= menf21bmc_hwmon_probe,
	.driver		= {
		.name		= DRV_NAME,
	},
};

module_platform_driver(menf21bmc_hwmon);

MODULE_AUTHOR("Andreas Werner <andreas.werner@men.de>");
MODULE_DESCRIPTION("MEN 14F021P00 BMC hwmon");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:menf21bmc_hwmon");
