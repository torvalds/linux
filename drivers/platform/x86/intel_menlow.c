/*
 *  intel_menlow.c - Intel menlow Driver for thermal management extension
 *
 *  Copyright (C) 2008 Intel Corp
 *  Copyright (C) 2008 Sujith Thomas <sujith.thomas@intel.com>
 *  Copyright (C) 2008 Zhang Rui <rui.zhang@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
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
 *
 *  This driver creates the sys I/F for programming the sensors.
 *  It also implements the driver for intel menlow memory controller (hardware
 *  id is INT0002) which makes use of the platform specific ACPI methods
 *  to get/set bandwidth.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/thermal.h>
#include <linux/acpi.h>

MODULE_AUTHOR("Thomas Sujith");
MODULE_AUTHOR("Zhang Rui");
MODULE_DESCRIPTION("Intel Menlow platform specific driver");
MODULE_LICENSE("GPL");

/*
 * Memory controller device control
 */

#define MEMORY_GET_BANDWIDTH "GTHS"
#define MEMORY_SET_BANDWIDTH "STHS"
#define MEMORY_ARG_CUR_BANDWIDTH 1
#define MEMORY_ARG_MAX_BANDWIDTH 0

static void intel_menlow_unregister_sensor(void);

/*
 * GTHS returning 'n' would mean that [0,n-1] states are supported
 * In that case max_cstate would be n-1
 * GTHS returning '0' would mean that no bandwidth control states are supported
 */
static int memory_get_max_bandwidth(struct thermal_cooling_device *cdev,
				    unsigned long *max_state)
{
	struct acpi_device *device = cdev->devdata;
	acpi_handle handle = device->handle;
	unsigned long long value;
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status = AE_OK;

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = MEMORY_ARG_MAX_BANDWIDTH;
	status = acpi_evaluate_integer(handle, MEMORY_GET_BANDWIDTH,
				       &arg_list, &value);
	if (ACPI_FAILURE(status))
		return -EFAULT;

	if (!value)
		return -EINVAL;

	*max_state = value - 1;
	return 0;
}

static int memory_get_cur_bandwidth(struct thermal_cooling_device *cdev,
				    unsigned long *value)
{
	struct acpi_device *device = cdev->devdata;
	acpi_handle handle = device->handle;
	unsigned long long result;
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status = AE_OK;

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = MEMORY_ARG_CUR_BANDWIDTH;
	status = acpi_evaluate_integer(handle, MEMORY_GET_BANDWIDTH,
				       &arg_list, &result);
	if (ACPI_FAILURE(status))
		return -EFAULT;

	*value = result;
	return 0;
}

static int memory_set_cur_bandwidth(struct thermal_cooling_device *cdev,
				    unsigned long state)
{
	struct acpi_device *device = cdev->devdata;
	acpi_handle handle = device->handle;
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status;
	unsigned long long temp;
	unsigned long max_state;

	if (memory_get_max_bandwidth(cdev, &max_state))
		return -EFAULT;

	if (state > max_state)
		return -EINVAL;

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = state;

	status =
	    acpi_evaluate_integer(handle, MEMORY_SET_BANDWIDTH, &arg_list,
				  &temp);

	pr_info("Bandwidth value was %ld: status is %d\n", state, status);
	if (ACPI_FAILURE(status))
		return -EFAULT;

	return 0;
}

static struct thermal_cooling_device_ops memory_cooling_ops = {
	.get_max_state = memory_get_max_bandwidth,
	.get_cur_state = memory_get_cur_bandwidth,
	.set_cur_state = memory_set_cur_bandwidth,
};

/*
 * Memory Device Management
 */
static int intel_menlow_memory_add(struct acpi_device *device)
{
	int result = -ENODEV;
	struct thermal_cooling_device *cdev;

	if (!device)
		return -EINVAL;

	if (!acpi_has_method(device->handle, MEMORY_GET_BANDWIDTH))
		goto end;

	if (!acpi_has_method(device->handle, MEMORY_SET_BANDWIDTH))
		goto end;

	cdev = thermal_cooling_device_register("Memory controller", device,
					       &memory_cooling_ops);
	if (IS_ERR(cdev)) {
		result = PTR_ERR(cdev);
		goto end;
	}

	device->driver_data = cdev;
	result = sysfs_create_link(&device->dev.kobj,
				&cdev->device.kobj, "thermal_cooling");
	if (result)
		goto unregister;

	result = sysfs_create_link(&cdev->device.kobj,
				&device->dev.kobj, "device");
	if (result) {
		sysfs_remove_link(&device->dev.kobj, "thermal_cooling");
		goto unregister;
	}

 end:
	return result;

 unregister:
	thermal_cooling_device_unregister(cdev);
	return result;

}

static int intel_menlow_memory_remove(struct acpi_device *device)
{
	struct thermal_cooling_device *cdev = acpi_driver_data(device);

	if (!device || !cdev)
		return -EINVAL;

	sysfs_remove_link(&device->dev.kobj, "thermal_cooling");
	sysfs_remove_link(&cdev->device.kobj, "device");
	thermal_cooling_device_unregister(cdev);

	return 0;
}

static const struct acpi_device_id intel_menlow_memory_ids[] = {
	{"INT0002", 0},
	{"", 0},
};

static struct acpi_driver intel_menlow_memory_driver = {
	.name = "intel_menlow_thermal_control",
	.ids = intel_menlow_memory_ids,
	.ops = {
		.add = intel_menlow_memory_add,
		.remove = intel_menlow_memory_remove,
		},
};

/*
 * Sensor control on menlow platform
 */

#define THERMAL_AUX0 0
#define THERMAL_AUX1 1
#define GET_AUX0 "GAX0"
#define GET_AUX1 "GAX1"
#define SET_AUX0 "SAX0"
#define SET_AUX1 "SAX1"

struct intel_menlow_attribute {
	struct device_attribute attr;
	struct device *device;
	acpi_handle handle;
	struct list_head node;
};

static LIST_HEAD(intel_menlow_attr_list);
static DEFINE_MUTEX(intel_menlow_attr_lock);

/*
 * sensor_get_auxtrip - get the current auxtrip value from sensor
 * @name: Thermalzone name
 * @auxtype : AUX0/AUX1
 * @buf: syfs buffer
 */
static int sensor_get_auxtrip(acpi_handle handle, int index,
							unsigned long long *value)
{
	acpi_status status;

	if ((index != 0 && index != 1) || !value)
		return -EINVAL;

	status = acpi_evaluate_integer(handle, index ? GET_AUX1 : GET_AUX0,
				       NULL, value);
	if (ACPI_FAILURE(status))
		return -EIO;

	return 0;
}

/*
 * sensor_set_auxtrip - set the new auxtrip value to sensor
 * @name: Thermalzone name
 * @auxtype : AUX0/AUX1
 * @buf: syfs buffer
 */
static int sensor_set_auxtrip(acpi_handle handle, int index, int value)
{
	acpi_status status;
	union acpi_object arg = {
		ACPI_TYPE_INTEGER
	};
	struct acpi_object_list args = {
		1, &arg
	};
	unsigned long long temp;

	if (index != 0 && index != 1)
		return -EINVAL;

	status = acpi_evaluate_integer(handle, index ? GET_AUX0 : GET_AUX1,
				       NULL, &temp);
	if (ACPI_FAILURE(status))
		return -EIO;
	if ((index && value < temp) || (!index && value > temp))
		return -EINVAL;

	arg.integer.value = value;
	status = acpi_evaluate_integer(handle, index ? SET_AUX1 : SET_AUX0,
				       &args, &temp);
	if (ACPI_FAILURE(status))
		return -EIO;

	/* do we need to check the return value of SAX0/SAX1 ? */

	return 0;
}

#define to_intel_menlow_attr(_attr)	\
	container_of(_attr, struct intel_menlow_attribute, attr)

static ssize_t aux_show(struct device *dev, struct device_attribute *dev_attr,
			char *buf, int idx)
{
	struct intel_menlow_attribute *attr = to_intel_menlow_attr(dev_attr);
	unsigned long long value;
	int result;

	result = sensor_get_auxtrip(attr->handle, idx, &value);

	return result ? result : sprintf(buf, "%lu", DECI_KELVIN_TO_CELSIUS(value));
}

static ssize_t aux0_show(struct device *dev,
			 struct device_attribute *dev_attr, char *buf)
{
	return aux_show(dev, dev_attr, buf, 0);
}

static ssize_t aux1_show(struct device *dev,
			 struct device_attribute *dev_attr, char *buf)
{
	return aux_show(dev, dev_attr, buf, 1);
}

static ssize_t aux_store(struct device *dev, struct device_attribute *dev_attr,
			 const char *buf, size_t count, int idx)
{
	struct intel_menlow_attribute *attr = to_intel_menlow_attr(dev_attr);
	int value;
	int result;

	/*Sanity check; should be a positive integer */
	if (!sscanf(buf, "%d", &value))
		return -EINVAL;

	if (value < 0)
		return -EINVAL;

	result = sensor_set_auxtrip(attr->handle, idx, 
				    CELSIUS_TO_DECI_KELVIN(value));
	return result ? result : count;
}

static ssize_t aux0_store(struct device *dev,
			  struct device_attribute *dev_attr,
			  const char *buf, size_t count)
{
	return aux_store(dev, dev_attr, buf, count, 0);
}

static ssize_t aux1_store(struct device *dev,
			  struct device_attribute *dev_attr,
			  const char *buf, size_t count)
{
	return aux_store(dev, dev_attr, buf, count, 1);
}

/* BIOS can enable/disable the thermal user application in dabney platform */
#define BIOS_ENABLED "\\_TZ.GSTS"
static ssize_t bios_enabled_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	acpi_status status;
	unsigned long long bios_enabled;

	status = acpi_evaluate_integer(NULL, BIOS_ENABLED, NULL, &bios_enabled);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return sprintf(buf, "%s\n", bios_enabled ? "enabled" : "disabled");
}

static int intel_menlow_add_one_attribute(char *name, umode_t mode, void *show,
					  void *store, struct device *dev,
					  acpi_handle handle)
{
	struct intel_menlow_attribute *attr;
	int result;

	attr = kzalloc(sizeof(struct intel_menlow_attribute), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	sysfs_attr_init(&attr->attr.attr); /* That is consistent naming :D */
	attr->attr.attr.name = name;
	attr->attr.attr.mode = mode;
	attr->attr.show = show;
	attr->attr.store = store;
	attr->device = dev;
	attr->handle = handle;

	result = device_create_file(dev, &attr->attr);
	if (result) {
		kfree(attr);
		return result;
	}

	mutex_lock(&intel_menlow_attr_lock);
	list_add_tail(&attr->node, &intel_menlow_attr_list);
	mutex_unlock(&intel_menlow_attr_lock);

	return 0;
}

static acpi_status intel_menlow_register_sensor(acpi_handle handle, u32 lvl,
						void *context, void **rv)
{
	acpi_status status;
	acpi_handle dummy;
	struct thermal_zone_device *thermal;
	int result;

	result = acpi_bus_get_private_data(handle, (void **)&thermal);
	if (result)
		return 0;

	/* _TZ must have the AUX0/1 methods */
	status = acpi_get_handle(handle, GET_AUX0, &dummy);
	if (ACPI_FAILURE(status))
		return (status == AE_NOT_FOUND) ? AE_OK : status;

	status = acpi_get_handle(handle, SET_AUX0, &dummy);
	if (ACPI_FAILURE(status))
		return (status == AE_NOT_FOUND) ? AE_OK : status;

	result = intel_menlow_add_one_attribute("aux0", 0644,
						aux0_show, aux0_store,
						&thermal->device, handle);
	if (result)
		return AE_ERROR;

	status = acpi_get_handle(handle, GET_AUX1, &dummy);
	if (ACPI_FAILURE(status))
		goto aux1_not_found;

	status = acpi_get_handle(handle, SET_AUX1, &dummy);
	if (ACPI_FAILURE(status))
		goto aux1_not_found;

	result = intel_menlow_add_one_attribute("aux1", 0644,
						aux1_show, aux1_store,
						&thermal->device, handle);
	if (result) {
		intel_menlow_unregister_sensor();
		return AE_ERROR;
	}

	/*
	 * create the "dabney_enabled" attribute which means the user app
	 * should be loaded or not
	 */

	result = intel_menlow_add_one_attribute("bios_enabled", 0444,
						bios_enabled_show, NULL,
						&thermal->device, handle);
	if (result) {
		intel_menlow_unregister_sensor();
		return AE_ERROR;
	}

	return AE_OK;

 aux1_not_found:
	if (status == AE_NOT_FOUND)
		return AE_OK;

	intel_menlow_unregister_sensor();
	return status;
}

static void intel_menlow_unregister_sensor(void)
{
	struct intel_menlow_attribute *pos, *next;

	mutex_lock(&intel_menlow_attr_lock);
	list_for_each_entry_safe(pos, next, &intel_menlow_attr_list, node) {
		list_del(&pos->node);
		device_remove_file(pos->device, &pos->attr);
		kfree(pos);
	}
	mutex_unlock(&intel_menlow_attr_lock);

	return;
}

static int __init intel_menlow_module_init(void)
{
	int result = -ENODEV;
	acpi_status status;
	unsigned long long enable;

	if (acpi_disabled)
		return result;

	/* Looking for the \_TZ.GSTS method */
	status = acpi_evaluate_integer(NULL, BIOS_ENABLED, NULL, &enable);
	if (ACPI_FAILURE(status) || !enable)
		return -ENODEV;

	/* Looking for ACPI device MEM0 with hardware id INT0002 */
	result = acpi_bus_register_driver(&intel_menlow_memory_driver);
	if (result)
		return result;

	/* Looking for sensors in each ACPI thermal zone */
	status = acpi_walk_namespace(ACPI_TYPE_THERMAL, ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX,
				     intel_menlow_register_sensor, NULL, NULL, NULL);
	if (ACPI_FAILURE(status)) {
		acpi_bus_unregister_driver(&intel_menlow_memory_driver);
		return -ENODEV;
	}

	return 0;
}

static void __exit intel_menlow_module_exit(void)
{
	acpi_bus_unregister_driver(&intel_menlow_memory_driver);
	intel_menlow_unregister_sensor();
}

module_init(intel_menlow_module_init);
module_exit(intel_menlow_module_exit);
