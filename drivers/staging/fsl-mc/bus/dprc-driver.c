/*
 * Freescale data path resource container (DPRC) driver
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 * Author: German Rivera <German.Rivera@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include "../include/mc-private.h"
#include "../include/mc-sys.h"
#include <linux/module.h>
#include <linux/slab.h>
#include "dprc-cmd.h"

struct dprc_child_objs {
	int child_count;
	struct dprc_obj_desc *child_array;
};

static int __fsl_mc_device_remove_if_not_in_mc(struct device *dev, void *data)
{
	int i;
	struct dprc_child_objs *objs;
	struct fsl_mc_device *mc_dev;

	WARN_ON(!dev);
	WARN_ON(!data);
	mc_dev = to_fsl_mc_device(dev);
	objs = data;

	for (i = 0; i < objs->child_count; i++) {
		struct dprc_obj_desc *obj_desc = &objs->child_array[i];

		if (strlen(obj_desc->type) != 0 &&
		    FSL_MC_DEVICE_MATCH(mc_dev, obj_desc))
			break;
	}

	if (i == objs->child_count)
		fsl_mc_device_remove(mc_dev);

	return 0;
}

static int __fsl_mc_device_remove(struct device *dev, void *data)
{
	WARN_ON(!dev);
	WARN_ON(data);
	fsl_mc_device_remove(to_fsl_mc_device(dev));
	return 0;
}

/**
 * dprc_remove_devices - Removes devices for objects removed from a DPRC
 *
 * @mc_bus_dev: pointer to the fsl-mc device that represents a DPRC object
 * @obj_desc_array: array of object descriptors for child objects currently
 * present in the DPRC in the MC.
 * @num_child_objects_in_mc: number of entries in obj_desc_array
 *
 * Synchronizes the state of the Linux bus driver with the actual state of
 * the MC by removing devices that represent MC objects that have
 * been dynamically removed in the physical DPRC.
 */
static void dprc_remove_devices(struct fsl_mc_device *mc_bus_dev,
				struct dprc_obj_desc *obj_desc_array,
				int num_child_objects_in_mc)
{
	if (num_child_objects_in_mc != 0) {
		/*
		 * Remove child objects that are in the DPRC in Linux,
		 * but not in the MC:
		 */
		struct dprc_child_objs objs;

		objs.child_count = num_child_objects_in_mc;
		objs.child_array = obj_desc_array;
		device_for_each_child(&mc_bus_dev->dev, &objs,
				      __fsl_mc_device_remove_if_not_in_mc);
	} else {
		/*
		 * There are no child objects for this DPRC in the MC.
		 * So, remove all the child devices from Linux:
		 */
		device_for_each_child(&mc_bus_dev->dev, NULL,
				      __fsl_mc_device_remove);
	}
}

static int __fsl_mc_device_match(struct device *dev, void *data)
{
	struct dprc_obj_desc *obj_desc = data;
	struct fsl_mc_device *mc_dev = to_fsl_mc_device(dev);

	return FSL_MC_DEVICE_MATCH(mc_dev, obj_desc);
}

static struct fsl_mc_device *fsl_mc_device_lookup(struct dprc_obj_desc
								*obj_desc,
						  struct fsl_mc_device
								*mc_bus_dev)
{
	struct device *dev;

	dev = device_find_child(&mc_bus_dev->dev, obj_desc,
				__fsl_mc_device_match);

	return dev ? to_fsl_mc_device(dev) : NULL;
}

/**
 * dprc_add_new_devices - Adds devices to the logical bus for a DPRC
 *
 * @mc_bus_dev: pointer to the fsl-mc device that represents a DPRC object
 * @obj_desc_array: array of device descriptors for child devices currently
 * present in the physical DPRC.
 * @num_child_objects_in_mc: number of entries in obj_desc_array
 *
 * Synchronizes the state of the Linux bus driver with the actual
 * state of the MC by adding objects that have been newly discovered
 * in the physical DPRC.
 */
static void dprc_add_new_devices(struct fsl_mc_device *mc_bus_dev,
				 struct dprc_obj_desc *obj_desc_array,
				 int num_child_objects_in_mc)
{
	int error;
	int i;

	for (i = 0; i < num_child_objects_in_mc; i++) {
		struct fsl_mc_device *child_dev;
		struct fsl_mc_io *mc_io = NULL;
		struct dprc_obj_desc *obj_desc = &obj_desc_array[i];

		if (strlen(obj_desc->type) == 0)
			continue;

		/*
		 * Check if device is already known to Linux:
		 */
		child_dev = fsl_mc_device_lookup(obj_desc, mc_bus_dev);
		if (child_dev)
			continue;

		error = fsl_mc_device_add(obj_desc, mc_io, &mc_bus_dev->dev,
					  &child_dev);
		if (error < 0) {
			if (mc_io)
				fsl_destroy_mc_io(mc_io);

			continue;
		}
	}
}

/**
 * dprc_scan_objects - Discover objects in a DPRC
 *
 * @mc_bus_dev: pointer to the fsl-mc device that represents a DPRC object
 *
 * Detects objects added and removed from a DPRC and synchronizes the
 * state of the Linux bus driver, MC by adding and removing
 * devices accordingly.
 */
int dprc_scan_objects(struct fsl_mc_device *mc_bus_dev)
{
	int num_child_objects;
	int dprc_get_obj_failures;
	int error;
	struct dprc_obj_desc *child_obj_desc_array = NULL;

	error = dprc_get_obj_count(mc_bus_dev->mc_io,
				   mc_bus_dev->mc_handle,
				   &num_child_objects);
	if (error < 0) {
		dev_err(&mc_bus_dev->dev, "dprc_get_obj_count() failed: %d\n",
			error);
		return error;
	}

	if (num_child_objects != 0) {
		int i;

		child_obj_desc_array =
		    devm_kmalloc_array(&mc_bus_dev->dev, num_child_objects,
				       sizeof(*child_obj_desc_array),
				       GFP_KERNEL);
		if (!child_obj_desc_array)
			return -ENOMEM;

		/*
		 * Discover objects currently present in the physical DPRC:
		 */
		dprc_get_obj_failures = 0;
		for (i = 0; i < num_child_objects; i++) {
			struct dprc_obj_desc *obj_desc =
			    &child_obj_desc_array[i];

			error = dprc_get_obj(mc_bus_dev->mc_io,
					     mc_bus_dev->mc_handle,
					     i, obj_desc);
			if (error < 0) {
				dev_err(&mc_bus_dev->dev,
					"dprc_get_obj(i=%d) failed: %d\n",
					i, error);
				/*
				 * Mark the obj entry as "invalid", by using the
				 * empty string as obj type:
				 */
				obj_desc->type[0] = '\0';
				obj_desc->id = error;
				dprc_get_obj_failures++;
				continue;
			}

			dev_dbg(&mc_bus_dev->dev,
				"Discovered object: type %s, id %d\n",
				obj_desc->type, obj_desc->id);
		}

		if (dprc_get_obj_failures != 0) {
			dev_err(&mc_bus_dev->dev,
				"%d out of %d devices could not be retrieved\n",
				dprc_get_obj_failures, num_child_objects);
		}
	}

	dprc_remove_devices(mc_bus_dev, child_obj_desc_array,
			    num_child_objects);

	dprc_add_new_devices(mc_bus_dev, child_obj_desc_array,
			     num_child_objects);

	if (child_obj_desc_array)
		devm_kfree(&mc_bus_dev->dev, child_obj_desc_array);

	return 0;
}
EXPORT_SYMBOL_GPL(dprc_scan_objects);

/**
 * dprc_scan_container - Scans a physical DPRC and synchronizes Linux bus state
 *
 * @mc_bus_dev: pointer to the fsl-mc device that represents a DPRC object
 *
 * Scans the physical DPRC and synchronizes the state of the Linux
 * bus driver with the actual state of the MC by adding and removing
 * devices as appropriate.
 */
int dprc_scan_container(struct fsl_mc_device *mc_bus_dev)
{
	int error;
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_bus_dev);

	/*
	 * Discover objects in the DPRC:
	 */
	mutex_lock(&mc_bus->scan_mutex);
	error = dprc_scan_objects(mc_bus_dev);
	mutex_unlock(&mc_bus->scan_mutex);
	return error;
}
EXPORT_SYMBOL_GPL(dprc_scan_container);

/**
 * dprc_probe - callback invoked when a DPRC is being bound to this driver
 *
 * @mc_dev: Pointer to fsl-mc device representing a DPRC
 *
 * It opens the physical DPRC in the MC.
 * It scans the DPRC to discover the MC objects contained in it.
 */
static int dprc_probe(struct fsl_mc_device *mc_dev)
{
	int error;
	size_t region_size;
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_dev);

	if (WARN_ON(strcmp(mc_dev->obj_desc.type, "dprc") != 0))
		return -EINVAL;

	if (!mc_dev->mc_io) {
		/*
		 * This is a child DPRC:
		 */
		if (WARN_ON(mc_dev->obj_desc.region_count == 0))
			return -EINVAL;

		region_size = mc_dev->regions[0].end -
			      mc_dev->regions[0].start + 1;

		error = fsl_create_mc_io(&mc_dev->dev,
					 mc_dev->regions[0].start,
					 region_size,
					 0, &mc_dev->mc_io);
		if (error < 0)
			return error;
	}

	error = dprc_open(mc_dev->mc_io, mc_dev->obj_desc.id,
			  &mc_dev->mc_handle);
	if (error < 0) {
		dev_err(&mc_dev->dev, "dprc_open() failed: %d\n", error);
		goto error_cleanup_mc_io;
	}

	mutex_init(&mc_bus->scan_mutex);

	/*
	 * Discover MC objects in DPRC object:
	 */
	error = dprc_scan_container(mc_dev);
	if (error < 0)
		goto error_cleanup_open;

	dev_info(&mc_dev->dev, "DPRC device bound to driver");
	return 0;

error_cleanup_open:
	(void)dprc_close(mc_dev->mc_io, mc_dev->mc_handle);

error_cleanup_mc_io:
	fsl_destroy_mc_io(mc_dev->mc_io);
	return error;
}

/**
 * dprc_remove - callback invoked when a DPRC is being unbound from this driver
 *
 * @mc_dev: Pointer to fsl-mc device representing the DPRC
 *
 * It removes the DPRC's child objects from Linux (not from the MC) and
 * closes the DPRC device in the MC.
 */
static int dprc_remove(struct fsl_mc_device *mc_dev)
{
	int error;

	if (WARN_ON(strcmp(mc_dev->obj_desc.type, "dprc") != 0))
		return -EINVAL;
	if (WARN_ON(!mc_dev->mc_io))
		return -EINVAL;

	device_for_each_child(&mc_dev->dev, NULL, __fsl_mc_device_remove);
	error = dprc_close(mc_dev->mc_io, mc_dev->mc_handle);
	if (error < 0)
		dev_err(&mc_dev->dev, "dprc_close() failed: %d\n", error);

	dev_info(&mc_dev->dev, "DPRC device unbound from driver");
	return 0;
}

static const struct fsl_mc_device_match_id match_id_table[] = {
	{
	 .vendor = FSL_MC_VENDOR_FREESCALE,
	 .obj_type = "dprc",
	 .ver_major = DPRC_VER_MAJOR,
	 .ver_minor = DPRC_VER_MINOR},
	{.vendor = 0x0},
};

static struct fsl_mc_driver dprc_driver = {
	.driver = {
		   .name = FSL_MC_DPRC_DRIVER_NAME,
		   .owner = THIS_MODULE,
		   .pm = NULL,
		   },
	.match_id_table = match_id_table,
	.probe = dprc_probe,
	.remove = dprc_remove,
};

int __init dprc_driver_init(void)
{
	return fsl_mc_driver_register(&dprc_driver);
}

void __exit dprc_driver_exit(void)
{
	fsl_mc_driver_unregister(&dprc_driver);
}
