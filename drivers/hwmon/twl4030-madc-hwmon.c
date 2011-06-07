/*
 *
 * TWL4030 MADC Hwmon driver-This driver monitors the real time
 * conversion of analog signals like battery temperature,
 * battery type, battery level etc. User can ask for the conversion on a
 * particular channel using the sysfs nodes.
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * J Keerthy <j-keerthy@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c/twl.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl4030-madc.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/stddef.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <linux/types.h>

/*
 * sysfs hook function
 */
static ssize_t madc_read(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct twl4030_madc_request req;
	long val;

	req.channels = (1 << attr->index);
	req.method = TWL4030_MADC_SW2;
	req.func_cb = NULL;
	val = twl4030_madc_conversion(&req);
	if (val < 0)
		return val;

	return sprintf(buf, "%d\n", req.rbuf[attr->index]);
}

/* sysfs nodes to read individual channels from user side */
static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, madc_read, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, madc_read, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, madc_read, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, madc_read, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, madc_read, NULL, 4);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, madc_read, NULL, 5);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, madc_read, NULL, 6);
static SENSOR_DEVICE_ATTR(in7_input, S_IRUGO, madc_read, NULL, 7);
static SENSOR_DEVICE_ATTR(in8_input, S_IRUGO, madc_read, NULL, 8);
static SENSOR_DEVICE_ATTR(in9_input, S_IRUGO, madc_read, NULL, 9);
static SENSOR_DEVICE_ATTR(curr10_input, S_IRUGO, madc_read, NULL, 10);
static SENSOR_DEVICE_ATTR(in11_input, S_IRUGO, madc_read, NULL, 11);
static SENSOR_DEVICE_ATTR(in12_input, S_IRUGO, madc_read, NULL, 12);
static SENSOR_DEVICE_ATTR(in15_input, S_IRUGO, madc_read, NULL, 15);

static struct attribute *twl4030_madc_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_curr10_input.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,
	&sensor_dev_attr_in12_input.dev_attr.attr,
	&sensor_dev_attr_in15_input.dev_attr.attr,
	NULL
};

static const struct attribute_group twl4030_madc_group = {
	.attrs = twl4030_madc_attributes,
};

static int __devinit twl4030_madc_hwmon_probe(struct platform_device *pdev)
{
	int ret;
	struct device *hwmon;

	ret = sysfs_create_group(&pdev->dev.kobj, &twl4030_madc_group);
	if (ret)
		goto err_sysfs;
	hwmon = hwmon_device_register(&pdev->dev);
	if (IS_ERR(hwmon)) {
		dev_err(&pdev->dev, "hwmon_device_register failed.\n");
		ret = PTR_ERR(hwmon);
		goto err_reg;
	}

	return 0;

err_reg:
	sysfs_remove_group(&pdev->dev.kobj, &twl4030_madc_group);
err_sysfs:

	return ret;
}

static int __devexit twl4030_madc_hwmon_remove(struct platform_device *pdev)
{
	hwmon_device_unregister(&pdev->dev);
	sysfs_remove_group(&pdev->dev.kobj, &twl4030_madc_group);

	return 0;
}

static struct platform_driver twl4030_madc_hwmon_driver = {
	.probe = twl4030_madc_hwmon_probe,
	.remove = __exit_p(twl4030_madc_hwmon_remove),
	.driver = {
		   .name = "twl4030_madc_hwmon",
		   .owner = THIS_MODULE,
		   },
};

static int __init twl4030_madc_hwmon_init(void)
{
	return platform_driver_register(&twl4030_madc_hwmon_driver);
}

module_init(twl4030_madc_hwmon_init);

static void __exit twl4030_madc_hwmon_exit(void)
{
	platform_driver_unregister(&twl4030_madc_hwmon_driver);
}

module_exit(twl4030_madc_hwmon_exit);

MODULE_DESCRIPTION("TWL4030 ADC Hwmon driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("J Keerthy");
MODULE_ALIAS("platform:twl4030_madc_hwmon");
