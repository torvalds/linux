/*
 * drivers/hwmon/wm831x-hwmon.c - Wolfson Microelectronics WM831x PMIC
 *                                hardware monitoring features.
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
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/slab.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/auxadc.h>

static const char * const input_names[] = {
	[WM831X_AUX_SYSVDD]    = "SYSVDD",
	[WM831X_AUX_USB]       = "USB",
	[WM831X_AUX_BKUP_BATT] = "Backup battery",
	[WM831X_AUX_BATT]      = "Battery",
	[WM831X_AUX_WALL]      = "WALL",
	[WM831X_AUX_CHIP_TEMP] = "PMIC",
	[WM831X_AUX_BATT_TEMP] = "Battery",
};

static ssize_t show_voltage(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct wm831x *wm831x = dev_get_drvdata(dev);
	int channel = to_sensor_dev_attr(attr)->index;
	int ret;

	ret = wm831x_auxadc_read_uv(wm831x, channel);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", DIV_ROUND_CLOSEST(ret, 1000));
}

static ssize_t show_chip_temp(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct wm831x *wm831x = dev_get_drvdata(dev);
	int channel = to_sensor_dev_attr(attr)->index;
	int ret;

	ret = wm831x_auxadc_read(wm831x, channel);
	if (ret < 0)
		return ret;

	/* Degrees celsius = (512.18-ret) / 1.0983 */
	ret = 512180 - (ret * 1000);
	ret = DIV_ROUND_CLOSEST(ret * 10000, 10983);

	return sprintf(buf, "%d\n", ret);
}

static ssize_t show_label(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	int channel = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%s\n", input_names[channel]);
}

#define WM831X_VOLTAGE(id, name) \
	static SENSOR_DEVICE_ATTR(in##id##_input, S_IRUGO, show_voltage, \
				  NULL, name)

#define WM831X_NAMED_VOLTAGE(id, name) \
	WM831X_VOLTAGE(id, name); \
	static SENSOR_DEVICE_ATTR(in##id##_label, S_IRUGO, show_label,	\
				  NULL, name)

WM831X_VOLTAGE(0, WM831X_AUX_AUX1);
WM831X_VOLTAGE(1, WM831X_AUX_AUX2);
WM831X_VOLTAGE(2, WM831X_AUX_AUX3);
WM831X_VOLTAGE(3, WM831X_AUX_AUX4);

WM831X_NAMED_VOLTAGE(4, WM831X_AUX_SYSVDD);
WM831X_NAMED_VOLTAGE(5, WM831X_AUX_USB);
WM831X_NAMED_VOLTAGE(6, WM831X_AUX_BATT);
WM831X_NAMED_VOLTAGE(7, WM831X_AUX_WALL);
WM831X_NAMED_VOLTAGE(8, WM831X_AUX_BKUP_BATT);

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_chip_temp, NULL,
			  WM831X_AUX_CHIP_TEMP);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, show_label, NULL,
			  WM831X_AUX_CHIP_TEMP);
/*
 * Report as a voltage since conversion depends on external components
 * and that's what the ABI wants.
 */
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_voltage, NULL,
			  WM831X_AUX_BATT_TEMP);
static SENSOR_DEVICE_ATTR(temp2_label, S_IRUGO, show_label, NULL,
			  WM831X_AUX_BATT_TEMP);

static struct attribute *wm831x_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,

	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_label.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_label.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in6_label.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in7_label.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in8_label.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_label.dev_attr.attr,

	NULL
};

ATTRIBUTE_GROUPS(wm831x);

static int wm831x_hwmon_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct device *hwmon_dev;

	hwmon_dev = devm_hwmon_device_register_with_groups(&pdev->dev, "wm831x",
							   wm831x,
							   wm831x_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver wm831x_hwmon_driver = {
	.probe = wm831x_hwmon_probe,
	.driver = {
		.name = "wm831x-hwmon",
	},
};

module_platform_driver(wm831x_hwmon_driver);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("WM831x Hardware Monitoring");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-hwmon");
