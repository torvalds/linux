// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright 2013 Matthew Garrett <mjg59@srcf.ucam.org>
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

static ssize_t irst_show_wakeup_events(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct acpi_device *acpi;
	unsigned long long value;
	acpi_status status;

	acpi = to_acpi_device(dev);

	status = acpi_evaluate_integer(acpi->handle, "GFFS", NULL, &value);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	return sprintf(buf, "%lld\n", value);
}

static ssize_t irst_store_wakeup_events(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct acpi_device *acpi;
	acpi_status status;
	unsigned long value;
	int error;

	acpi = to_acpi_device(dev);

	error = kstrtoul(buf, 0, &value);
	if (error)
		return error;

	status = acpi_execute_simple_method(acpi->handle, "SFFS", value);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	return count;
}

static struct device_attribute irst_wakeup_attr = {
	.attr = { .name = "wakeup_events", .mode = 0600 },
	.show = irst_show_wakeup_events,
	.store = irst_store_wakeup_events
};

static ssize_t irst_show_wakeup_time(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct acpi_device *acpi;
	unsigned long long value;
	acpi_status status;

	acpi = to_acpi_device(dev);

	status = acpi_evaluate_integer(acpi->handle, "GFTV", NULL, &value);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	return sprintf(buf, "%lld\n", value);
}

static ssize_t irst_store_wakeup_time(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct acpi_device *acpi;
	acpi_status status;
	unsigned long value;
	int error;

	acpi = to_acpi_device(dev);

	error = kstrtoul(buf, 0, &value);
	if (error)
		return error;

	status = acpi_execute_simple_method(acpi->handle, "SFTV", value);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	return count;
}

static struct device_attribute irst_timeout_attr = {
	.attr = { .name = "wakeup_time", .mode = 0600 },
	.show = irst_show_wakeup_time,
	.store = irst_store_wakeup_time
};

static int irst_add(struct acpi_device *acpi)
{
	int error;

	error = device_create_file(&acpi->dev, &irst_timeout_attr);
	if (unlikely(error))
		return error;

	error = device_create_file(&acpi->dev, &irst_wakeup_attr);
	if (unlikely(error))
		device_remove_file(&acpi->dev, &irst_timeout_attr);

	return error;
}

static void irst_remove(struct acpi_device *acpi)
{
	device_remove_file(&acpi->dev, &irst_wakeup_attr);
	device_remove_file(&acpi->dev, &irst_timeout_attr);
}

static const struct acpi_device_id irst_ids[] = {
	{"INT3392", 0},
	{"", 0}
};

static struct acpi_driver irst_driver = {
	.name = "intel_rapid_start",
	.class = "intel_rapid_start",
	.ids = irst_ids,
	.ops = {
		.add = irst_add,
		.remove = irst_remove,
	},
};

module_acpi_driver(irst_driver);

MODULE_DEVICE_TABLE(acpi, irst_ids);
