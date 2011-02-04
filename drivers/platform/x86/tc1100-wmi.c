/*
 *  HP Compaq TC1100 Tablet WMI Extras Driver
 *
 *  Copyright (C) 2007 Carlos Corbacho <carlos@strangeworlds.co.uk>
 *  Copyright (C) 2004 Jamey Hicks <jamey.hicks@hp.com>
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <linux/platform_device.h>

#define GUID "C364AC71-36DB-495A-8494-B439D472A505"

#define TC1100_INSTANCE_WIRELESS		1
#define TC1100_INSTANCE_JOGDIAL		2

#define TC1100_LOGPREFIX "tc1100-wmi: "
#define TC1100_INFO KERN_INFO TC1100_LOGPREFIX

MODULE_AUTHOR("Jamey Hicks, Carlos Corbacho");
MODULE_DESCRIPTION("HP Compaq TC1100 Tablet WMI Extras");
MODULE_LICENSE("GPL");
MODULE_ALIAS("wmi:C364AC71-36DB-495A-8494-B439D472A505");

static int tc1100_probe(struct platform_device *device);
static int tc1100_remove(struct platform_device *device);
static int tc1100_suspend(struct platform_device *device, pm_message_t state);
static int tc1100_resume(struct platform_device *device);

static struct platform_driver tc1100_driver = {
	.driver = {
		.name = "tc1100-wmi",
		.owner = THIS_MODULE,
	},
	.probe = tc1100_probe,
	.remove = tc1100_remove,
	.suspend = tc1100_suspend,
	.resume = tc1100_resume,
};

static struct platform_device *tc1100_device;

struct tc1100_data {
	u32 wireless;
	u32 jogdial;
};

static struct tc1100_data suspend_data;

/* --------------------------------------------------------------------------
				Device Management
   -------------------------------------------------------------------------- */

static int get_state(u32 *out, u8 instance)
{
	u32 tmp;
	acpi_status status;
	struct acpi_buffer result = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;

	if (!out)
		return -EINVAL;

	if (instance > 2)
		return -ENODEV;

	status = wmi_query_block(GUID, instance, &result);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	obj = (union acpi_object *) result.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER) {
		tmp = obj->integer.value;
	} else {
		tmp = 0;
	}

	if (result.length > 0 && result.pointer)
		kfree(result.pointer);

	switch (instance) {
	case TC1100_INSTANCE_WIRELESS:
		*out = (tmp == 3) ? 1 : 0;
		return 0;
	case TC1100_INSTANCE_JOGDIAL:
		*out = (tmp == 1) ? 0 : 1;
		return 0;
	default:
		return -ENODEV;
	}
}

static int set_state(u32 *in, u8 instance)
{
	u32 value;
	acpi_status status;
	struct acpi_buffer input;

	if (!in)
		return -EINVAL;

	if (instance > 2)
		return -ENODEV;

	switch (instance) {
	case TC1100_INSTANCE_WIRELESS:
		value = (*in) ? 1 : 2;
		break;
	case TC1100_INSTANCE_JOGDIAL:
		value = (*in) ? 0 : 1;
		break;
	default:
		return -ENODEV;
	}

	input.length = sizeof(u32);
	input.pointer = &value;

	status = wmi_set_block(GUID, instance, &input);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return 0;
}

/* --------------------------------------------------------------------------
				FS Interface (/sys)
   -------------------------------------------------------------------------- */

/*
 * Read/ write bool sysfs macro
 */
#define show_set_bool(value, instance) \
static ssize_t \
show_bool_##value(struct device *dev, struct device_attribute *attr, \
	char *buf) \
{ \
	u32 result; \
	acpi_status status = get_state(&result, instance); \
	if (ACPI_SUCCESS(status)) \
		return sprintf(buf, "%d\n", result); \
	return sprintf(buf, "Read error\n"); \
} \
\
static ssize_t \
set_bool_##value(struct device *dev, struct device_attribute *attr, \
	const char *buf, size_t count) \
{ \
	u32 tmp = simple_strtoul(buf, NULL, 10); \
	acpi_status status = set_state(&tmp, instance); \
		if (ACPI_FAILURE(status)) \
			return -EINVAL; \
	return count; \
} \
static DEVICE_ATTR(value, S_IRUGO | S_IWUSR, \
	show_bool_##value, set_bool_##value);

show_set_bool(wireless, TC1100_INSTANCE_WIRELESS);
show_set_bool(jogdial, TC1100_INSTANCE_JOGDIAL);

static void remove_fs(void)
{
	device_remove_file(&tc1100_device->dev, &dev_attr_wireless);
	device_remove_file(&tc1100_device->dev, &dev_attr_jogdial);
}

static int add_fs(void)
{
	int ret;

	ret = device_create_file(&tc1100_device->dev, &dev_attr_wireless);
	if (ret)
		goto add_sysfs_error;

	ret = device_create_file(&tc1100_device->dev, &dev_attr_jogdial);
	if (ret)
		goto add_sysfs_error;

	return ret;

add_sysfs_error:
	remove_fs();
	return ret;
}

/* --------------------------------------------------------------------------
				Driver Model
   -------------------------------------------------------------------------- */

static int tc1100_probe(struct platform_device *device)
{
	int result = 0;

	result = add_fs();
	return result;
}


static int tc1100_remove(struct platform_device *device)
{
	remove_fs();
	return 0;
}

static int tc1100_suspend(struct platform_device *dev, pm_message_t state)
{
	int ret;

	ret = get_state(&suspend_data.wireless, TC1100_INSTANCE_WIRELESS);
	if (ret)
		return ret;

	ret = get_state(&suspend_data.jogdial, TC1100_INSTANCE_JOGDIAL);
	if (ret)
		return ret;

	return ret;
}

static int tc1100_resume(struct platform_device *dev)
{
	int ret;

	ret = set_state(&suspend_data.wireless, TC1100_INSTANCE_WIRELESS);
	if (ret)
		return ret;

	ret = set_state(&suspend_data.jogdial, TC1100_INSTANCE_JOGDIAL);
	if (ret)
		return ret;

	return ret;
}

static int __init tc1100_init(void)
{
	int result = 0;

	if (!wmi_has_guid(GUID))
		return -ENODEV;

	result = platform_driver_register(&tc1100_driver);
	if (result)
		return result;

	tc1100_device = platform_device_alloc("tc1100-wmi", -1);
	platform_device_add(tc1100_device);

	printk(TC1100_INFO "HP Compaq TC1100 Tablet WMI Extras loaded\n");

	return result;
}

static void __exit tc1100_exit(void)
{
	platform_device_del(tc1100_device);
	platform_driver_unregister(&tc1100_driver);

	printk(TC1100_INFO "HP Compaq TC1100 Tablet WMI Extras unloaded\n");
}

module_init(tc1100_init);
module_exit(tc1100_exit);
