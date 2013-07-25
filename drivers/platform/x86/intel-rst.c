/*
 *  Copyright 2013 Matthew Garrett <mjg59@srcf.ucam.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <acpi/acpi_drivers.h>

MODULE_LICENSE("GPL");

static ssize_t irst_show_wakeup_events(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct acpi_device *acpi;
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *result;
	acpi_status status;

	acpi = to_acpi_device(dev);

	status = acpi_evaluate_object(acpi->handle, "GFFS", NULL, &output);
	if (!ACPI_SUCCESS(status))
		return -EINVAL;

	result = output.pointer;

	if (result->type != ACPI_TYPE_INTEGER) {
		kfree(result);
		return -EINVAL;
	}

	return sprintf(buf, "%lld\n", result->integer.value);
}

static ssize_t irst_store_wakeup_events(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct acpi_device *acpi;
	struct acpi_object_list input;
	union acpi_object param;
	acpi_status status;
	unsigned long value;
	int error;

	acpi = to_acpi_device(dev);

	error = kstrtoul(buf, 0, &value);

	if (error)
		return error;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = value;

	input.count = 1;
	input.pointer = &param;

	status = acpi_evaluate_object(acpi->handle, "SFFS", &input, NULL);

	if (!ACPI_SUCCESS(status))
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
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *result;
	acpi_status status;

	acpi = to_acpi_device(dev);

	status = acpi_evaluate_object(acpi->handle, "GFTV", NULL, &output);
	if (!ACPI_SUCCESS(status))
		return -EINVAL;

	result = output.pointer;

	if (result->type != ACPI_TYPE_INTEGER) {
		kfree(result);
		return -EINVAL;
	}

	return sprintf(buf, "%lld\n", result->integer.value);
}

static ssize_t irst_store_wakeup_time(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct acpi_device *acpi;
	struct acpi_object_list input;
	union acpi_object param;
	acpi_status status;
	unsigned long value;
	int error;

	acpi = to_acpi_device(dev);

	error = kstrtoul(buf, 0, &value);

	if (error)
		return error;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = value;

	input.count = 1;
	input.pointer = &param;

	status = acpi_evaluate_object(acpi->handle, "SFTV", &input, NULL);

	if (!ACPI_SUCCESS(status))
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
	int error = 0;

	error = device_create_file(&acpi->dev, &irst_timeout_attr);
	if (error)
		goto out;

	error = device_create_file(&acpi->dev, &irst_wakeup_attr);
	if (error)
		goto out_timeout;

	return 0;

out_timeout:
	device_remove_file(&acpi->dev, &irst_timeout_attr);
out:
	return error;
}

static int irst_remove(struct acpi_device *acpi)
{
	device_remove_file(&acpi->dev, &irst_wakeup_attr);
	device_remove_file(&acpi->dev, &irst_timeout_attr);

	return 0;
}

static const struct acpi_device_id irst_ids[] = {
	{"INT3392", 0},
	{"", 0}
};

static struct acpi_driver irst_driver = {
	.owner = THIS_MODULE,
	.name = "intel_rapid_start",
	.class = "intel_rapid_start",
	.ids = irst_ids,
	.ops = {
		.add = irst_add,
		.remove = irst_remove,
	},
};

static int irst_init(void)
{
	return acpi_bus_register_driver(&irst_driver);
}

static void irst_exit(void)
{
	acpi_bus_unregister_driver(&irst_driver);
}

module_init(irst_init);
module_exit(irst_exit);

MODULE_DEVICE_TABLE(acpi, irst_ids);
