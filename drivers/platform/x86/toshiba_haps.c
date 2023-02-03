// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Toshiba HDD Active Protection Sensor (HAPS) driver
 *
 * Copyright (C) 2014 Azael Avalos <coproscefalo@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/acpi.h>

MODULE_AUTHOR("Azael Avalos <coproscefalo@gmail.com>");
MODULE_DESCRIPTION("Toshiba HDD Active Protection Sensor");
MODULE_LICENSE("GPL");

struct toshiba_haps_dev {
	struct acpi_device *acpi_dev;

	int protection_level;
};

static struct toshiba_haps_dev *toshiba_haps;

/* HAPS functions */
static int toshiba_haps_reset_protection(acpi_handle handle)
{
	acpi_status status;

	status = acpi_evaluate_object(handle, "RSSS", NULL, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to reset the HDD protection\n");
		return -EIO;
	}

	return 0;
}

static int toshiba_haps_protection_level(acpi_handle handle, int level)
{
	acpi_status status;

	status = acpi_execute_simple_method(handle, "PTLV", level);
	if (ACPI_FAILURE(status)) {
		pr_err("Error while setting the protection level\n");
		return -EIO;
	}

	pr_debug("HDD protection level set to: %d\n", level);

	return 0;
}

/* sysfs files */
static ssize_t protection_level_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct toshiba_haps_dev *haps = dev_get_drvdata(dev);

	return sprintf(buf, "%i\n", haps->protection_level);
}

static ssize_t protection_level_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct toshiba_haps_dev *haps = dev_get_drvdata(dev);
	int level;
	int ret;

	ret = kstrtoint(buf, 0, &level);
	if (ret)
		return ret;
	/*
	 * Check for supported levels, which can be:
	 * 0 - Disabled | 1 - Low | 2 - Medium | 3 - High
	 */
	if (level < 0 || level > 3)
		return -EINVAL;

	/* Set the sensor level */
	ret = toshiba_haps_protection_level(haps->acpi_dev->handle, level);
	if (ret != 0)
		return ret;

	haps->protection_level = level;

	return count;
}
static DEVICE_ATTR_RW(protection_level);

static ssize_t reset_protection_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct toshiba_haps_dev *haps = dev_get_drvdata(dev);
	int reset;
	int ret;

	ret = kstrtoint(buf, 0, &reset);
	if (ret)
		return ret;
	/* The only accepted value is 1 */
	if (reset != 1)
		return -EINVAL;

	/* Reset the protection interface */
	ret = toshiba_haps_reset_protection(haps->acpi_dev->handle);
	if (ret != 0)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(reset_protection);

static struct attribute *haps_attributes[] = {
	&dev_attr_protection_level.attr,
	&dev_attr_reset_protection.attr,
	NULL,
};

static const struct attribute_group haps_attr_group = {
	.attrs = haps_attributes,
};

/*
 * ACPI stuff
 */
static void toshiba_haps_notify(struct acpi_device *device, u32 event)
{
	pr_debug("Received event: 0x%x\n", event);

	acpi_bus_generate_netlink_event(device->pnp.device_class,
					dev_name(&device->dev),
					event, 0);
}

static void toshiba_haps_remove(struct acpi_device *device)
{
	sysfs_remove_group(&device->dev.kobj, &haps_attr_group);

	if (toshiba_haps)
		toshiba_haps = NULL;
}

/* Helper function */
static int toshiba_haps_available(acpi_handle handle)
{
	acpi_status status;
	u64 hdd_present;

	/*
	 * A non existent device as well as having (only)
	 * Solid State Drives can cause the call to fail.
	 */
	status = acpi_evaluate_integer(handle, "_STA", NULL, &hdd_present);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI call to query HDD protection failed\n");
		return 0;
	}

	if (!hdd_present) {
		pr_info("HDD protection not available or using SSD\n");
		return 0;
	}

	return 1;
}

static int toshiba_haps_add(struct acpi_device *acpi_dev)
{
	struct toshiba_haps_dev *haps;
	int ret;

	if (toshiba_haps)
		return -EBUSY;

	if (!toshiba_haps_available(acpi_dev->handle))
		return -ENODEV;

	pr_info("Toshiba HDD Active Protection Sensor device\n");

	haps = kzalloc(sizeof(struct toshiba_haps_dev), GFP_KERNEL);
	if (!haps)
		return -ENOMEM;

	haps->acpi_dev = acpi_dev;
	haps->protection_level = 2;
	acpi_dev->driver_data = haps;
	dev_set_drvdata(&acpi_dev->dev, haps);

	/* Set the protection level, currently at level 2 (Medium) */
	ret = toshiba_haps_protection_level(acpi_dev->handle, 2);
	if (ret != 0)
		return ret;

	ret = sysfs_create_group(&acpi_dev->dev.kobj, &haps_attr_group);
	if (ret)
		return ret;

	toshiba_haps = haps;

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int toshiba_haps_suspend(struct device *device)
{
	struct toshiba_haps_dev *haps;
	int ret;

	haps = acpi_driver_data(to_acpi_device(device));

	/* Deactivate the protection on suspend */
	ret = toshiba_haps_protection_level(haps->acpi_dev->handle, 0);

	return ret;
}

static int toshiba_haps_resume(struct device *device)
{
	struct toshiba_haps_dev *haps;
	int ret;

	haps = acpi_driver_data(to_acpi_device(device));

	/* Set the stored protection level */
	ret = toshiba_haps_protection_level(haps->acpi_dev->handle,
					    haps->protection_level);

	/* Reset the protection on resume */
	ret = toshiba_haps_reset_protection(haps->acpi_dev->handle);
	if (ret != 0)
		return ret;

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(toshiba_haps_pm,
			 toshiba_haps_suspend, toshiba_haps_resume);

static const struct acpi_device_id haps_device_ids[] = {
	{"TOS620A", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, haps_device_ids);

static struct acpi_driver toshiba_haps_driver = {
	.name = "Toshiba HAPS",
	.owner = THIS_MODULE,
	.ids = haps_device_ids,
	.flags = ACPI_DRIVER_ALL_NOTIFY_EVENTS,
	.ops = {
		.add =		toshiba_haps_add,
		.remove =	toshiba_haps_remove,
		.notify =	toshiba_haps_notify,
	},
	.drv.pm = &toshiba_haps_pm,
};

module_acpi_driver(toshiba_haps_driver);
