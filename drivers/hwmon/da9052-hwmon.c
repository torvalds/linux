/*
 * HWMON Driver for Dialog DA9052
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <linux/mfd/da9052/da9052.h>
#include <linux/mfd/da9052/reg.h>

struct da9052_hwmon {
	struct da9052	*da9052;
	struct mutex	hwmon_lock;
};

static const char * const input_names[] = {
	[DA9052_ADC_VDDOUT]	=	"VDDOUT",
	[DA9052_ADC_ICH]	=	"CHARGING CURRENT",
	[DA9052_ADC_TBAT]	=	"BATTERY TEMP",
	[DA9052_ADC_VBAT]	=	"BATTERY VOLTAGE",
	[DA9052_ADC_IN4]	=	"ADC IN4",
	[DA9052_ADC_IN5]	=	"ADC IN5",
	[DA9052_ADC_IN6]	=	"ADC IN6",
	[DA9052_ADC_TJUNC]	=	"BATTERY JUNCTION TEMP",
	[DA9052_ADC_VBBAT]	=	"BACK-UP BATTERY VOLTAGE",
};

/* Conversion function for VDDOUT and VBAT */
static inline int volt_reg_to_mv(int value)
{
	return DIV_ROUND_CLOSEST(value * 2000, 1023) + 2500;
}

/* Conversion function for ADC channels 4, 5 and 6 */
static inline int input_reg_to_mv(int value)
{
	return DIV_ROUND_CLOSEST(value * 2500, 1023);
}

/* Conversion function for VBBAT */
static inline int vbbat_reg_to_mv(int value)
{
	return DIV_ROUND_CLOSEST(value * 5000, 1023);
}

static inline int da9052_enable_vddout_channel(struct da9052 *da9052)
{
	return da9052_reg_update(da9052, DA9052_ADC_CONT_REG,
				 DA9052_ADCCONT_AUTOVDDEN,
				 DA9052_ADCCONT_AUTOVDDEN);
}

static inline int da9052_disable_vddout_channel(struct da9052 *da9052)
{
	return da9052_reg_update(da9052, DA9052_ADC_CONT_REG,
				 DA9052_ADCCONT_AUTOVDDEN, 0);
}

static ssize_t da9052_read_vddout(struct device *dev,
				  struct device_attribute *devattr, char *buf)
{
	struct da9052_hwmon *hwmon = dev_get_drvdata(dev);
	int ret, vdd;

	mutex_lock(&hwmon->hwmon_lock);

	ret = da9052_enable_vddout_channel(hwmon->da9052);
	if (ret < 0)
		goto hwmon_err;

	vdd = da9052_reg_read(hwmon->da9052, DA9052_VDD_RES_REG);
	if (vdd < 0) {
		ret = vdd;
		goto hwmon_err_release;
	}

	ret = da9052_disable_vddout_channel(hwmon->da9052);
	if (ret < 0)
		goto hwmon_err;

	mutex_unlock(&hwmon->hwmon_lock);
	return sprintf(buf, "%d\n", volt_reg_to_mv(vdd));

hwmon_err_release:
	da9052_disable_vddout_channel(hwmon->da9052);
hwmon_err:
	mutex_unlock(&hwmon->hwmon_lock);
	return ret;
}

static ssize_t da9052_read_ich(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct da9052_hwmon *hwmon = dev_get_drvdata(dev);
	int ret;

	ret = da9052_reg_read(hwmon->da9052, DA9052_ICHG_AV_REG);
	if (ret < 0)
		return ret;

	/* Equivalent to 3.9mA/bit in register ICHG_AV */
	return sprintf(buf, "%d\n", DIV_ROUND_CLOSEST(ret * 39, 10));
}

static ssize_t da9052_read_tbat(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct da9052_hwmon *hwmon = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", da9052_adc_read_temp(hwmon->da9052));
}

static ssize_t da9052_read_vbat(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct da9052_hwmon *hwmon = dev_get_drvdata(dev);
	int ret;

	ret = da9052_adc_manual_read(hwmon->da9052, DA9052_ADC_VBAT);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", volt_reg_to_mv(ret));
}

static ssize_t da9052_read_misc_channel(struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct da9052_hwmon *hwmon = dev_get_drvdata(dev);
	int channel = to_sensor_dev_attr(devattr)->index;
	int ret;

	ret = da9052_adc_manual_read(hwmon->da9052, channel);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", input_reg_to_mv(ret));
}

static ssize_t da9052_read_tjunc(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct da9052_hwmon *hwmon = dev_get_drvdata(dev);
	int tjunc;
	int toffset;

	tjunc = da9052_reg_read(hwmon->da9052, DA9052_TJUNC_RES_REG);
	if (tjunc < 0)
		return tjunc;

	toffset = da9052_reg_read(hwmon->da9052, DA9052_T_OFFSET_REG);
	if (toffset < 0)
		return toffset;

	/*
	 * Degrees celsius = 1.708 * (TJUNC_RES - T_OFFSET) - 108.8
	 * T_OFFSET is a trim value used to improve accuracy of the result
	 */
	return sprintf(buf, "%d\n", 1708 * (tjunc - toffset) - 108800);
}

static ssize_t da9052_read_vbbat(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct da9052_hwmon *hwmon = dev_get_drvdata(dev);
	int ret;

	ret = da9052_adc_manual_read(hwmon->da9052, DA9052_ADC_VBBAT);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", vbbat_reg_to_mv(ret));
}

static ssize_t show_label(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%s\n",
		       input_names[to_sensor_dev_attr(devattr)->index]);
}

static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, da9052_read_vddout, NULL,
			  DA9052_ADC_VDDOUT);
static SENSOR_DEVICE_ATTR(in0_label, S_IRUGO, show_label, NULL,
			  DA9052_ADC_VDDOUT);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, da9052_read_vbat, NULL,
			  DA9052_ADC_VBAT);
static SENSOR_DEVICE_ATTR(in3_label, S_IRUGO, show_label, NULL,
			  DA9052_ADC_VBAT);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, da9052_read_misc_channel, NULL,
			  DA9052_ADC_IN4);
static SENSOR_DEVICE_ATTR(in4_label, S_IRUGO, show_label, NULL,
			  DA9052_ADC_IN4);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, da9052_read_misc_channel, NULL,
			  DA9052_ADC_IN5);
static SENSOR_DEVICE_ATTR(in5_label, S_IRUGO, show_label, NULL,
			  DA9052_ADC_IN5);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, da9052_read_misc_channel, NULL,
			  DA9052_ADC_IN6);
static SENSOR_DEVICE_ATTR(in6_label, S_IRUGO, show_label, NULL,
			  DA9052_ADC_IN6);
static SENSOR_DEVICE_ATTR(in9_input, S_IRUGO, da9052_read_vbbat, NULL,
			  DA9052_ADC_VBBAT);
static SENSOR_DEVICE_ATTR(in9_label, S_IRUGO, show_label, NULL,
			  DA9052_ADC_VBBAT);

static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO, da9052_read_ich, NULL,
			  DA9052_ADC_ICH);
static SENSOR_DEVICE_ATTR(curr1_label, S_IRUGO, show_label, NULL,
			  DA9052_ADC_ICH);

static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, da9052_read_tbat, NULL,
			  DA9052_ADC_TBAT);
static SENSOR_DEVICE_ATTR(temp2_label, S_IRUGO, show_label, NULL,
			  DA9052_ADC_TBAT);
static SENSOR_DEVICE_ATTR(temp8_input, S_IRUGO, da9052_read_tjunc, NULL,
			  DA9052_ADC_TJUNC);
static SENSOR_DEVICE_ATTR(temp8_label, S_IRUGO, show_label, NULL,
			  DA9052_ADC_TJUNC);

static struct attribute *da9052_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_label.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_label.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_label.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_label.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in6_label.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in9_label.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_label.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_label.dev_attr.attr,
	&sensor_dev_attr_temp8_input.dev_attr.attr,
	&sensor_dev_attr_temp8_label.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(da9052);

static int da9052_hwmon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da9052_hwmon *hwmon;
	struct device *hwmon_dev;

	hwmon = devm_kzalloc(dev, sizeof(struct da9052_hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	mutex_init(&hwmon->hwmon_lock);
	hwmon->da9052 = dev_get_drvdata(pdev->dev.parent);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, "da9052",
							   hwmon,
							   da9052_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver da9052_hwmon_driver = {
	.probe = da9052_hwmon_probe,
	.driver = {
		.name = "da9052-hwmon",
	},
};

module_platform_driver(da9052_hwmon_driver);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("DA9052 HWMON driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9052-hwmon");
