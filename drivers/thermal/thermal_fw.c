// SPDX-License-Identifier: GPL-2.0
/*
 *  thermal-fw.c - Thermal components creation from firmware description
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/fwnode.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

/**
 * thermal_fwnode_cooling_device_register() - register an thermal cooling device
 * @np:		a pointer to a device tree node.
 * @of_index:	a cooling device index in the cooling controller
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:		standard thermal cooling devices callbacks.
 *
 * This function will register a cooling device with device tree node reference.
 * This interface function adds a new thermal cooling device (fan/processor/...)
 * to /sys/class/thermal/ folder as cooling_device[0-*]. It tries to bind itself
 * to all the thermal zone devices registered at the same time.
 *
 * Return: a pointer to the created struct thermal_cooling_device or an
 * ERR_PTR. Caller must check return value with IS_ERR*() helpers.
 */
struct thermal_cooling_device *
thermal_fwnode_cooling_device_register(struct fwnode_handle *fwnode, int fwn_index,
				       const char *type, void *devdata,
				       const struct thermal_cooling_device_ops *ops)
{
	struct thermal_cooling_device *cdev;

	cdev = __thermal_cooling_device_register(type, devdata, ops);
	if (IS_ERR(cdev))
		return cdev;

	cdev->np = (struct device_node *)fwnode;
	cdev->of_index = fwn_index;
	thermal_cooling_device_init_complete(cdev);

	return cdev;
}
EXPORT_SYMBOL_GPL(thermal_fwnode_cooling_device_register);

static struct thermal_cooling_device *
__devm_thermal_fwnode_cooling_device_register(struct device *dev, struct fwnode_handle *fwnode,
					      int fwn_index, const char *type, void *devdata,
					      const struct thermal_cooling_device_ops *ops)
{
	struct thermal_cooling_device **ptr, *tcd;

	ptr = devres_alloc(thermal_cooling_device_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	tcd = thermal_fwnode_cooling_device_register(fwnode, fwn_index, type, devdata, ops);
	if (IS_ERR(tcd)) {
		devres_free(ptr);
		return tcd;
	}

	*ptr = tcd;
	devres_add(dev, ptr);

	return tcd;
}

/**
 * devm_thermal_fwnode_cooling_device_register() - register a thermal cooling device
 * @dev:	a valid struct device pointer of a sensor device.
 * @fw_index:	a cooling device index in the cooling controller
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:	standard thermal cooling devices callbacks.
 *
 * This function will register a cooling device with a firmware node reference.
 * This interface function adds a new thermal cooling device (fan/processor/...)
 * to /sys/class/thermal/ folder as cooling_device[0-*]. It tries to bind itself
 * to all the thermal zone devices registered at the same time.
 *
 * Return: a pointer to the created struct thermal_cooling_device or an
 * ERR_PTR. Caller must check return value with IS_ERR*() helpers.
 */
struct thermal_cooling_device *
devm_thermal_fwnode_cooling_device_register(struct device *dev, int fwn_index,
					    const char *type, void *devdata,
					    const struct thermal_cooling_device_ops *ops)
{
	return __devm_thermal_fwnode_cooling_device_register(dev, dev_fwnode(dev), fwn_index,
							     type, devdata, ops);
}
EXPORT_SYMBOL_GPL(devm_thermal_fwnode_cooling_device_register);


