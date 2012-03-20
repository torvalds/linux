/*
 * drivers/hwmon/wm8350-hwmon.c - Wolfson Microelectronics WM8350 PMIC
 *                                  hardware monitoring features.
 *
 * Copyright (C) 2009 Wolfson Microelectronics plc
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#include <linux/mfd/wm8350/core.h>
#include <linux/mfd/wm8350/comparator.h>

static ssize_t show_name(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "wm8350\n");
}

static const char *input_names[] = {
	[WM8350_AUXADC_USB]  = "USB",
	[WM8350_AUXADC_LINE] = "Line",
	[WM8350_AUXADC_BATT] = "Battery",
};


static ssize_t show_voltage(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct wm8350 *wm8350 = dev_get_drvdata(dev);
	int channel = to_sensor_dev_attr(attr)->index;
	int val;

	val = wm8350_read_auxadc(wm8350, channel, 0, 0) * WM8350_AUX_COEFF;
	val = DIV_ROUND_CLOSEST(val, 1000);

	return sprintf(buf, "%d\n", val);
}

static ssize_t show_label(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	int channel = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%s\n", input_names[channel]);
}

#define WM8350_NAMED_VOLTAGE(id, name) \
	static SENSOR_DEVICE_ATTR(in##id##_input, S_IRUGO, show_voltage,\
				  NULL, name);		\
	static SENSOR_DEVICE_ATTR(in##id##_label, S_IRUGO, show_label,	\
				  NULL, name)

static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

WM8350_NAMED_VOLTAGE(0, WM8350_AUXADC_USB);
WM8350_NAMED_VOLTAGE(1, WM8350_AUXADC_BATT);
WM8350_NAMED_VOLTAGE(2, WM8350_AUXADC_LINE);

static struct attribute *wm8350_attributes[] = {
	&dev_attr_name.attr,

	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_label.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_label.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_label.dev_attr.attr,

	NULL,
};

static const struct attribute_group wm8350_attr_group = {
	.attrs	= wm8350_attributes,
};

static int __devinit wm8350_hwmon_probe(struct platform_device *pdev)
{
	struct wm8350 *wm8350 = platform_get_drvdata(pdev);
	int ret;

	ret = sysfs_create_group(&pdev->dev.kobj, &wm8350_attr_group);
	if (ret)
		goto err;

	wm8350->hwmon.classdev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(wm8350->hwmon.classdev)) {
		ret = PTR_ERR(wm8350->hwmon.classdev);
		goto err_group;
	}

	return 0;

err_group:
	sysfs_remove_group(&pdev->dev.kobj, &wm8350_attr_group);
err:
	return ret;
}

static int __devexit wm8350_hwmon_remove(struct platform_device *pdev)
{
	struct wm8350 *wm8350 = platform_get_drvdata(pdev);

	hwmon_device_unregister(wm8350->hwmon.classdev);
	sysfs_remove_group(&pdev->dev.kobj, &wm8350_attr_group);

	return 0;
}

static struct platform_driver wm8350_hwmon_driver = {
	.probe = wm8350_hwmon_probe,
	.remove = __devexit_p(wm8350_hwmon_remove),
	.driver = {
		.name = "wm8350-hwmon",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(wm8350_hwmon_driver);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("WM8350 Hardware Monitoring");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8350-hwmon");
