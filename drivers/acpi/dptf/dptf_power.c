/*
 * dptf_power:  DPTF platform power driver
 * Copyright (c) 2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>

/*
 * Presentation of attributes which are defined for INT3407. They are:
 * PMAX : Maximum platform powe
 * PSRC : Platform power source
 * ARTG : Adapter rating
 * CTYP : Charger type
 * PBSS : Battery steady power
 */
#define DPTF_POWER_SHOW(name, object) \
static ssize_t name##_show(struct device *dev,\
			   struct device_attribute *attr,\
			   char *buf)\
{\
	struct platform_device *pdev = to_platform_device(dev);\
	struct acpi_device *acpi_dev = platform_get_drvdata(pdev);\
	unsigned long long val;\
	acpi_status status;\
\
	status = acpi_evaluate_integer(acpi_dev->handle, #object,\
				       NULL, &val);\
	if (ACPI_SUCCESS(status))\
		return sprintf(buf, "%d\n", (int)val);\
	else \
		return -EINVAL;\
}

DPTF_POWER_SHOW(max_platform_power_mw, PMAX)
DPTF_POWER_SHOW(platform_power_source, PSRC)
DPTF_POWER_SHOW(adapter_rating_mw, ARTG)
DPTF_POWER_SHOW(battery_steady_power_mw, PBSS)
DPTF_POWER_SHOW(charger_type, CTYP)

static DEVICE_ATTR_RO(max_platform_power_mw);
static DEVICE_ATTR_RO(platform_power_source);
static DEVICE_ATTR_RO(adapter_rating_mw);
static DEVICE_ATTR_RO(battery_steady_power_mw);
static DEVICE_ATTR_RO(charger_type);

static struct attribute *dptf_power_attrs[] = {
	&dev_attr_max_platform_power_mw.attr,
	&dev_attr_platform_power_source.attr,
	&dev_attr_adapter_rating_mw.attr,
	&dev_attr_battery_steady_power_mw.attr,
	&dev_attr_charger_type.attr,
	NULL
};

static struct attribute_group dptf_power_attribute_group = {
	.attrs = dptf_power_attrs,
	.name = "dptf_power"
};

static int dptf_power_add(struct platform_device *pdev)
{
	struct acpi_device *acpi_dev;
	acpi_status status;
	unsigned long long ptype;
	int result;

	acpi_dev = ACPI_COMPANION(&(pdev->dev));
	if (!acpi_dev)
		return -ENODEV;

	status = acpi_evaluate_integer(acpi_dev->handle, "PTYP", NULL, &ptype);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	if (ptype != 0x11)
		return -ENODEV;

	result = sysfs_create_group(&pdev->dev.kobj,
				    &dptf_power_attribute_group);
	if (result)
		return result;

	platform_set_drvdata(pdev, acpi_dev);

	return 0;
}

static int dptf_power_remove(struct platform_device *pdev)
{

	sysfs_remove_group(&pdev->dev.kobj, &dptf_power_attribute_group);

	return 0;
}

static const struct acpi_device_id int3407_device_ids[] = {
	{"INT3407", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, int3407_device_ids);

static struct platform_driver dptf_power_driver = {
	.probe = dptf_power_add,
	.remove = dptf_power_remove,
	.driver = {
		.name = "DPTF Platform Power",
		.acpi_match_table = int3407_device_ids,
	},
};

module_platform_driver(dptf_power_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ACPI DPTF platform power driver");
