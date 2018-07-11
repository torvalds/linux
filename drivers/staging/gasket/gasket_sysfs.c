// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "gasket_sysfs.h"

#include "gasket_core.h"
#include "gasket_logging.h"

/*
 * Pair of kernel device and user-specified pointer. Used in lookups in sysfs
 * "show" functions to return user data.
 */

struct gasket_sysfs_mapping {
	/*
	 * The device bound to this mapping. If this is NULL, then this mapping
	 * is free.
	 */
	struct device *device;

	/* Legacy device struct, if used by this mapping's driver. */
	struct device *legacy_device;

	/* The Gasket descriptor for this device. */
	struct gasket_dev *gasket_dev;

	/* This device's set of sysfs attributes/nodes. */
	struct gasket_sysfs_attribute *attributes;

	/* The number of live elements in "attributes". */
	int attribute_count;

	/* Protects structure from simultaneous access. */
	struct mutex mutex;

	/* Tracks active users of this mapping. */
	struct kref refcount;
};

/*
 * Data needed to manage users of this sysfs utility.
 * Currently has a fixed size; if space is a concern, this can be dynamically
 * allocated.
 */
/*
 * 'Global' (file-scoped) list of mappings between devices and gasket_data
 * pointers. This removes the requirement to have a gasket_sysfs_data
 * handle in all files.
 */
static struct gasket_sysfs_mapping dev_mappings[GASKET_SYSFS_NUM_MAPPINGS];

/*
 * Callback when a mapping's refcount goes to zero.
 * @ref: The reference count of the containing sysfs mapping.
 */
static void release_entry(struct kref *ref)
{
	/* All work is done after the return from kref_put. */
}

/*
 * Looks up mapping information for the given device.
 * @device: The device whose mapping to look for.
 *
 * Looks up the requested device and takes a reference and returns it if found,
 * and returns NULL otherwise.
 */
static struct gasket_sysfs_mapping *get_mapping(struct device *device)
{
	int i;

	if (!device) {
		gasket_nodev_error("Received NULL device!");
		return NULL;
	}

	for (i = 0; i < GASKET_SYSFS_NUM_MAPPINGS; i++) {
		mutex_lock(&dev_mappings[i].mutex);
		if (dev_mappings[i].device == device ||
		    dev_mappings[i].legacy_device == device) {
			kref_get(&dev_mappings[i].refcount);
			mutex_unlock(&dev_mappings[i].mutex);
			return &dev_mappings[i];
		}
		mutex_unlock(&dev_mappings[i].mutex);
	}

	gasket_nodev_info("Mapping to device %s not found.", device->kobj.name);
	return NULL;
}

/*
 * Returns a reference to a mapping.
 * @mapping: The mapping we're returning.
 *
 * Decrements the refcount for the given mapping (if valid). If the refcount is
 * zero, then it cleans up the mapping - in this function as opposed to the
 * kref_put callback, due to a potential deadlock.
 *
 * Although put_mapping_n exists, this function is left here (as an implicit
 * put_mapping_n(..., 1) for convenience.
 */
static void put_mapping(struct gasket_sysfs_mapping *mapping)
{
	int i;
	int num_files_to_remove = 0;
	struct device_attribute *files_to_remove;
	struct device *device;
	struct device *legacy_device;

	if (!mapping) {
		gasket_nodev_info("Mapping should not be NULL.");
		return;
	}

	mutex_lock(&mapping->mutex);
	if (refcount_read(&mapping->refcount.refcount) == 0)
		gasket_nodev_error("Refcount is already 0!");
	if (kref_put(&mapping->refcount, release_entry)) {
		gasket_nodev_info("Removing Gasket sysfs mapping, device %s",
				  mapping->device->kobj.name);
		/*
		 * We can't remove the sysfs nodes in the kref callback, since
		 * device_remove_file() blocks until the node is free.
		 * Readers/writers of sysfs nodes, though, will be blocked on
		 * the mapping mutex, resulting in deadlock. To fix this, the
		 * sysfs nodes are removed outside the lock.
		 */
		device = mapping->device;
		legacy_device = mapping->legacy_device;
		num_files_to_remove = mapping->attribute_count;
		files_to_remove = kcalloc(num_files_to_remove,
					  sizeof(*files_to_remove),
					  GFP_KERNEL);
		for (i = 0; i < num_files_to_remove; i++)
			files_to_remove[i] = mapping->attributes[i].attr;

		kfree(mapping->attributes);
		mapping->attributes = NULL;
		mapping->attribute_count = 0;
		mapping->device = NULL;
		mapping->gasket_dev = NULL;
	}
	mutex_unlock(&mapping->mutex);

	if (num_files_to_remove != 0) {
		for (i = 0; i < num_files_to_remove; ++i) {
			device_remove_file(device, &files_to_remove[i]);
			if (legacy_device)
				device_remove_file(
					legacy_device, &files_to_remove[i]);
		}
		kfree(files_to_remove);
	}
}

/*
 * Returns a reference N times.
 * @mapping: The mapping to return.
 *
 * In higher-level resource acquire/release function pairs, the release function
 * will need to release a mapping 2x - once for the refcount taken in the
 * release function itself, and once for the count taken in the acquire call.
 */
static void put_mapping_n(struct gasket_sysfs_mapping *mapping, int times)
{
	int i;

	for (i = 0; i < times; i++)
		put_mapping(mapping);
}

void gasket_sysfs_init(void)
{
	int i;

	for (i = 0; i < GASKET_SYSFS_NUM_MAPPINGS; i++) {
		dev_mappings[i].device = NULL;
		mutex_init(&dev_mappings[i].mutex);
	}
}

int gasket_sysfs_create_mapping(
	struct device *device, struct gasket_dev *gasket_dev)
{
	struct gasket_sysfs_mapping *mapping;
	int map_idx = -1;

	/*
	 * We need a function-level mutex to protect against the same device
	 * being added [multiple times] simultaneously.
	 */
	static DEFINE_MUTEX(function_mutex);

	mutex_lock(&function_mutex);

	gasket_nodev_info(
		"Creating sysfs entries for device pointer 0x%p.", device);

	/* Check that the device we're adding hasn't already been added. */
	mapping = get_mapping(device);
	if (mapping) {
		gasket_nodev_error(
			"Attempting to re-initialize sysfs mapping for device "
			"0x%p.", device);
		put_mapping(mapping);
		mutex_unlock(&function_mutex);
		return -EINVAL;
	}

	/* Find the first empty entry in the array. */
	for (map_idx = 0; map_idx < GASKET_SYSFS_NUM_MAPPINGS; ++map_idx) {
		mutex_lock(&dev_mappings[map_idx].mutex);
		if (!dev_mappings[map_idx].device)
			/* Break with the mutex held! */
			break;
		mutex_unlock(&dev_mappings[map_idx].mutex);
	}

	if (map_idx == GASKET_SYSFS_NUM_MAPPINGS) {
		gasket_nodev_error("All mappings have been exhausted!");
		mutex_unlock(&function_mutex);
		return -ENOMEM;
	}

	gasket_nodev_info(
		"Creating sysfs mapping for device %s.", device->kobj.name);

	mapping = &dev_mappings[map_idx];
	kref_init(&mapping->refcount);
	mapping->device = device;
	mapping->gasket_dev = gasket_dev;
	mapping->attributes = kcalloc(GASKET_SYSFS_MAX_NODES,
				      sizeof(*mapping->attributes),
				      GFP_KERNEL);
	mapping->attribute_count = 0;
	if (!mapping->attributes) {
		gasket_nodev_error("Unable to allocate sysfs attribute array.");
		mutex_unlock(&mapping->mutex);
		mutex_unlock(&function_mutex);
		return -ENOMEM;
	}

	mutex_unlock(&mapping->mutex);
	mutex_unlock(&function_mutex);

	/* Don't decrement the refcount here! One open count keeps it alive! */
	return 0;
}

int gasket_sysfs_create_entries(
	struct device *device, const struct gasket_sysfs_attribute *attrs)
{
	int i;
	int ret;
	struct gasket_sysfs_mapping *mapping = get_mapping(device);

	if (!mapping) {
		gasket_nodev_error(
			"Creating entries for device 0x%p without first "
			"initializing mapping.",
			device);
		return -EINVAL;
	}

	mutex_lock(&mapping->mutex);
	for (i = 0; strcmp(attrs[i].attr.attr.name, GASKET_ARRAY_END_MARKER);
		i++) {
		if (mapping->attribute_count == GASKET_SYSFS_MAX_NODES) {
			gasket_nodev_error(
				"Maximum number of sysfs nodes reached for "
				"device.");
			mutex_unlock(&mapping->mutex);
			put_mapping(mapping);
			return -ENOMEM;
		}

		ret = device_create_file(device, &attrs[i].attr);
		if (ret) {
			gasket_nodev_error("Unable to create device entries");
			mutex_unlock(&mapping->mutex);
			put_mapping(mapping);
			return ret;
		}

		if (mapping->legacy_device) {
			ret = device_create_file(mapping->legacy_device,
						 &attrs[i].attr);
			if (ret) {
				gasket_log_error(
					mapping->gasket_dev,
					"Unable to create legacy sysfs entries;"
					" rc: %d",
					ret);
				mutex_unlock(&mapping->mutex);
				put_mapping(mapping);
				return ret;
			}
		}

		mapping->attributes[mapping->attribute_count] = attrs[i];
		++mapping->attribute_count;
	}

	mutex_unlock(&mapping->mutex);
	put_mapping(mapping);
	return 0;
}
EXPORT_SYMBOL(gasket_sysfs_create_entries);

void gasket_sysfs_remove_mapping(struct device *device)
{
	struct gasket_sysfs_mapping *mapping = get_mapping(device);

	if (!mapping) {
		gasket_nodev_error(
			"Attempted to remove non-existent sysfs mapping to "
			"device 0x%p",
			device);
		return;
	}

	put_mapping_n(mapping, 2);
}

struct gasket_dev *gasket_sysfs_get_device_data(struct device *device)
{
	struct gasket_sysfs_mapping *mapping = get_mapping(device);

	if (!mapping) {
		gasket_nodev_error("device %p not registered.", device);
		return NULL;
	}

	return mapping->gasket_dev;
}
EXPORT_SYMBOL(gasket_sysfs_get_device_data);

void gasket_sysfs_put_device_data(struct device *device, struct gasket_dev *dev)
{
	struct gasket_sysfs_mapping *mapping = get_mapping(device);

	if (!mapping)
		return;

	/* See comment of put_mapping_n() for why the '2' is necessary. */
	put_mapping_n(mapping, 2);
}
EXPORT_SYMBOL(gasket_sysfs_put_device_data);

struct gasket_sysfs_attribute *gasket_sysfs_get_attr(
	struct device *device, struct device_attribute *attr)
{
	int i;
	int num_attrs;
	struct gasket_sysfs_mapping *mapping = get_mapping(device);
	struct gasket_sysfs_attribute *attrs = NULL;

	if (!mapping)
		return NULL;

	attrs = mapping->attributes;
	num_attrs = mapping->attribute_count;
	for (i = 0; i < num_attrs; ++i) {
		if (!strcmp(attrs[i].attr.attr.name, attr->attr.name))
			return &attrs[i];
	}

	gasket_nodev_error("Unable to find match for device_attribute %s",
			   attr->attr.name);
	return NULL;
}
EXPORT_SYMBOL(gasket_sysfs_get_attr);

void gasket_sysfs_put_attr(
	struct device *device, struct gasket_sysfs_attribute *attr)
{
	int i;
	int num_attrs;
	struct gasket_sysfs_mapping *mapping = get_mapping(device);
	struct gasket_sysfs_attribute *attrs = NULL;

	if (!mapping)
		return;

	attrs = mapping->attributes;
	num_attrs = mapping->attribute_count;
	for (i = 0; i < num_attrs; ++i) {
		if (&attrs[i] == attr) {
			put_mapping_n(mapping, 2);
			return;
		}
	}

	gasket_nodev_error(
		"Unable to put unknown attribute: %s", attr->attr.attr.name);
}
EXPORT_SYMBOL(gasket_sysfs_put_attr);

ssize_t gasket_sysfs_register_show(
	struct device *device, struct device_attribute *attr, char *buf)
{
	ulong reg_address, reg_bar, reg_value;
	struct gasket_sysfs_mapping *mapping;
	struct gasket_dev *gasket_dev;
	struct gasket_sysfs_attribute *gasket_attr;

	mapping = get_mapping(device);
	if (!mapping) {
		gasket_nodev_info("Device driver may have been removed.");
		return 0;
	}

	gasket_dev = mapping->gasket_dev;
	if (!gasket_dev) {
		gasket_nodev_error(
			"No sysfs mapping found for device 0x%p", device);
		put_mapping(mapping);
		return 0;
	}

	gasket_attr = gasket_sysfs_get_attr(device, attr);
	if (!gasket_attr) {
		put_mapping(mapping);
		return 0;
	}

	reg_address = gasket_attr->data.bar_address.offset;
	reg_bar = gasket_attr->data.bar_address.bar;
	reg_value = gasket_dev_read_64(gasket_dev, reg_bar, reg_address);

	gasket_sysfs_put_attr(device, gasket_attr);
	put_mapping(mapping);
	return snprintf(buf, PAGE_SIZE, "0x%lX\n", reg_value);
}
EXPORT_SYMBOL(gasket_sysfs_register_show);

ssize_t gasket_sysfs_register_store(
	struct device *device, struct device_attribute *attr, const char *buf,
	size_t count)
{
	ulong parsed_value = 0;
	struct gasket_sysfs_mapping *mapping;
	struct gasket_dev *gasket_dev;
	struct gasket_sysfs_attribute *gasket_attr;

	if (count < 3 || buf[0] != '0' || buf[1] != 'x') {
		gasket_nodev_error(
			"sysfs register write format: \"0x<hex value>\".");
		return -EINVAL;
	}

	if (kstrtoul(buf, 16, &parsed_value) != 0) {
		gasket_nodev_error(
			"Unable to parse input as 64-bit hex value: %s.", buf);
		return -EINVAL;
	}

	mapping = get_mapping(device);
	if (!mapping) {
		gasket_nodev_info("Device driver may have been removed.");
		return 0;
	}

	gasket_dev = mapping->gasket_dev;
	if (!gasket_dev) {
		gasket_nodev_info("Device driver may have been removed.");
		return 0;
	}

	gasket_attr = gasket_sysfs_get_attr(device, attr);
	if (!gasket_attr) {
		put_mapping(mapping);
		return count;
	}

	gasket_dev_write_64(gasket_dev, parsed_value,
			    gasket_attr->data.bar_address.bar,
			    gasket_attr->data.bar_address.offset);

	if (gasket_attr->write_callback)
		gasket_attr->write_callback(
			gasket_dev, gasket_attr, parsed_value);

	gasket_sysfs_put_attr(device, gasket_attr);
	put_mapping(mapping);
	return count;
}
EXPORT_SYMBOL(gasket_sysfs_register_store);
