/*
 * Driver for the Freescale Semiconductor MC13783 adc.
 *
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2009 Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/mfd/mc13783.h>
#include <linux/platform_device.h>
#include <linux/hwmon-sysfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hwmon.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>

#define MC13783_ADC_NAME	"mc13783-adc"

struct mc13783_adc_priv {
	struct mc13783 *mc13783;
	struct device *hwmon_dev;
};

static ssize_t mc13783_adc_show_name(struct device *dev, struct device_attribute
			      *devattr, char *buf)
{
	return sprintf(buf, "mc13783_adc\n");
}

static int mc13783_adc_read(struct device *dev,
		struct device_attribute *devattr, unsigned int *val)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mc13783_adc_priv *priv = platform_get_drvdata(pdev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	unsigned int channel = attr->index;
	unsigned int sample[4];
	int ret;

	ret = mc13783_adc_do_conversion(priv->mc13783,
			MC13783_ADC_MODE_MULT_CHAN,
			channel, sample);
	if (ret)
		return ret;

	channel &= 0x7;

	*val = (sample[channel % 4] >> (channel > 3 ? 14 : 2)) & 0x3ff;

	return 0;
}

static ssize_t mc13783_adc_read_bp(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	unsigned val;
	int ret = mc13783_adc_read(dev, devattr, &val);

	if (ret)
		return ret;

	/*
	 * BP (channel 2) reports with offset 2.4V to the actual value to fit
	 * the input range of the ADC.  unit = 2.25mV = 9/4 mV.
	 */
	val = DIV_ROUND_CLOSEST(val * 9, 4) + 2400;

	return sprintf(buf, "%u\n", val);
}

static ssize_t mc13783_adc_read_gp(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	unsigned val;
	int ret = mc13783_adc_read(dev, devattr, &val);

	if (ret)
		return ret;

	/*
	 * input range is [0, 2.3V], val has 10 bits, so each bit
	 * is worth 9/4 mV.
	 */
	val = DIV_ROUND_CLOSEST(val * 9, 4);

	return sprintf(buf, "%u\n", val);
}

static DEVICE_ATTR(name, S_IRUGO, mc13783_adc_show_name, NULL);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, mc13783_adc_read_bp, NULL, 2);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, mc13783_adc_read_gp, NULL, 5);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, mc13783_adc_read_gp, NULL, 6);
static SENSOR_DEVICE_ATTR(in7_input, S_IRUGO, mc13783_adc_read_gp, NULL, 7);
static SENSOR_DEVICE_ATTR(in8_input, S_IRUGO, mc13783_adc_read_gp, NULL, 8);
static SENSOR_DEVICE_ATTR(in9_input, S_IRUGO, mc13783_adc_read_gp, NULL, 9);
static SENSOR_DEVICE_ATTR(in10_input, S_IRUGO, mc13783_adc_read_gp, NULL, 10);
static SENSOR_DEVICE_ATTR(in11_input, S_IRUGO, mc13783_adc_read_gp, NULL, 11);
static SENSOR_DEVICE_ATTR(in12_input, S_IRUGO, mc13783_adc_read_gp, NULL, 12);
static SENSOR_DEVICE_ATTR(in13_input, S_IRUGO, mc13783_adc_read_gp, NULL, 13);
static SENSOR_DEVICE_ATTR(in14_input, S_IRUGO, mc13783_adc_read_gp, NULL, 14);
static SENSOR_DEVICE_ATTR(in15_input, S_IRUGO, mc13783_adc_read_gp, NULL, 15);

static struct attribute *mc13783_attr[] = {
	&dev_attr_name.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,
	NULL
};

static const struct attribute_group mc13783_group = {
	.attrs = mc13783_attr,
};

/* last four channels may be occupied by the touchscreen */
static struct attribute *mc13783_attr_ts[] = {
	&sensor_dev_attr_in12_input.dev_attr.attr,
	&sensor_dev_attr_in13_input.dev_attr.attr,
	&sensor_dev_attr_in14_input.dev_attr.attr,
	&sensor_dev_attr_in15_input.dev_attr.attr,
	NULL
};

static const struct attribute_group mc13783_group_ts = {
	.attrs = mc13783_attr_ts,
};

static int mc13783_adc_use_touchscreen(struct platform_device *pdev)
{
	struct mc13783_adc_priv *priv = platform_get_drvdata(pdev);
	unsigned flags = mc13783_get_flags(priv->mc13783);

	return flags & MC13783_USE_TOUCHSCREEN;
}

static int __init mc13783_adc_probe(struct platform_device *pdev)
{
	struct mc13783_adc_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mc13783 = dev_get_drvdata(pdev->dev.parent);

	platform_set_drvdata(pdev, priv);

	/* Register sysfs hooks */
	ret = sysfs_create_group(&pdev->dev.kobj, &mc13783_group);
	if (ret)
		goto out_err_create1;

	if (!mc13783_adc_use_touchscreen(pdev)) {
		ret = sysfs_create_group(&pdev->dev.kobj, &mc13783_group_ts);
		if (ret)
			goto out_err_create2;
	}

	priv->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(priv->hwmon_dev)) {
		ret = PTR_ERR(priv->hwmon_dev);
		dev_err(&pdev->dev,
				"hwmon_device_register failed with %d.\n", ret);
		goto out_err_register;
	}


	return 0;

out_err_register:

	if (!mc13783_adc_use_touchscreen(pdev))
		sysfs_remove_group(&pdev->dev.kobj, &mc13783_group_ts);
out_err_create2:

	sysfs_remove_group(&pdev->dev.kobj, &mc13783_group);
out_err_create1:

	platform_set_drvdata(pdev, NULL);
	kfree(priv);

	return ret;
}

static int __devexit mc13783_adc_remove(struct platform_device *pdev)
{
	struct mc13783_adc_priv *priv = platform_get_drvdata(pdev);

	hwmon_device_unregister(priv->hwmon_dev);

	if (!mc13783_adc_use_touchscreen(pdev))
		sysfs_remove_group(&pdev->dev.kobj, &mc13783_group_ts);

	sysfs_remove_group(&pdev->dev.kobj, &mc13783_group);

	platform_set_drvdata(pdev, NULL);
	kfree(priv);

	return 0;
}

static struct platform_driver mc13783_adc_driver = {
	.remove 	= __devexit_p(mc13783_adc_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= MC13783_ADC_NAME,
	},
};

static int __init mc13783_adc_init(void)
{
	return platform_driver_probe(&mc13783_adc_driver, mc13783_adc_probe);
}

static void __exit mc13783_adc_exit(void)
{
	platform_driver_unregister(&mc13783_adc_driver);
}

module_init(mc13783_adc_init);
module_exit(mc13783_adc_exit);

MODULE_DESCRIPTION("MC13783 ADC driver");
MODULE_AUTHOR("Luotao Fu <l.fu@pengutronix.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" MC13783_ADC_NAME);
