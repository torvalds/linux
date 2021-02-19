// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018 Google, Inc. */
#include "gasket_sysfs.h"

#include "gasket_core.h"

#include <linux/device.h>
#include <linux/printk.h>

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

/* Callback when a mapping's refcount goes to zero. */
static void release_entry(struct kref *ref)
{
	/* All work is done after the return from kref_put. */
}

/* Look up mapping information for the given device. */
static struct gasket_sysfs_mapping *get_mapping(struct device *device)
{
	int i;

	for (i = 0; i < GASKET_SYSFS_NUM_MAPPINGS; i++) {
		mutex_lock(&dev_mappings[i].mutex);
		if (dev_mappings[i].device == device) {
			kref_get(&dev_mappings[i].refcount);
			mutex_unlock(&dev_mappings[i].mutex);
			return &dev_mappings[i];
		}
		mutex_unlock(&dev_mappings[i].mutex);
	}

	dev_dbg(device, "%s: Mapping to device %s not found\n",
		__func__, device->kobj.name);
	return NULL;
}

/* Put a reference to a mapping. */
static void put_mapping(struct gasket_sysfs_mapping *mapping)
{
	int i;
	int num_files_to_remove = 0;
	struct device_attribute *files_to_remove;
	struct device *device;

	if (!mapping) {
		pr_debug("%s: Mapping should not be NULL\n", __func__);
		return;
	}

	mutex_lock(&mapping->mutex);
	if (kref_put(&mapping->refcount, release_entry)) {
		dev_dbg(mapping->device, "Removing Gasket sysfs mapping\n");
		/*
		 * We can't remove the sysfs nodes in the kref callback, since
		 * device_remove_file() blocks until the node is free.
		 * Readers/writers of sysfs nodes, though, will be blocked on
		 * the mapping mutex, resulting in deadlock. To fix this, the
		 * sysfs nodes are removed outside the lock.
		 */
		device = mapping->device;
		num_files_to_remove = mapping->attribute_count;
		files_to_remove = kcalloc(num_files_to_remove,
					  sizeof(*files_to_remove),
					  GFP_KERNEL);
		if (files_to_remove)
			for (i = 0; i < num_files_to_remove; i++)
				files_to_remove[i] =
				    mapping->attributes[i].attr;
		else
			num_files_to_remove = 0;

		kfree(mapping->attributes);
		mapping->attributes = NULL;
		mapping->attribute_count = 0;
		put_device(mapping->device);
		mapping->device = NULL;
		mapping->gasket_dev = NULL;
	}
	mutex_unlock(&mapping->mutex);

	if (num_files_to_remove != 0) {
		for (i = 0; i < num_files_to_remove; ++i)
			device_remove_file(device, &files_to_remove[i]);
		kfree(files_to_remove);
	}
}

/*
 * Put a reference to a mapping N times.
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

int gasket_sysfs_create_mapping(struct device *device,
				struct gasket_dev *gasket_dev)
{
	struct gasket_sysfs_mapping *mapping;
	int map_idx = -1;

	/*
	 * We need a function-level mutex to protect against the same device
	 * being added [multiple times] simultaneously.
	 */
	static DEFINE_MUTEX(function_mutex);

	mutex_lock(&function_mutex);
	dev_dbg(device, "Creating sysfs entries for device\n");

	/* Check that the device we're adding hasn't already been added. */
	mapping = get_mapping(device);
	if (mapping) {
		dev_err(device,
			"Attempting to re-initialize sysfs mapping for device\n");
		put_mapping(mapping);
		mutex_unlock(&function_mutex);
		return -EBUSY;
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
		dev_err(device, "All mappings have been exhausted\n");
		mutex_unlock(&function_mutex);
		return -ENOMEM;
	}

	dev_dbg(device, "Creating sysfs mapping for device %s\n",
		device->kobj.name);

	mapping = &dev_mappings[map_idx];
	mapping->attributes = kcalloc(GASKET_SYSFS_MAX_NODES,
				      sizeof(*mapping->attributes),
				      GFP_KERNEL);
	if (!mapping->attributes) {
		dev_dbg(device, "Unable to allocate sysfs attribute array\n");
		mutex_unlock(&mapping->mutex);
		mutex_unlock(&function_mutex);
		return -ENOMEM;
	}

	kref_init(&mapping->refcount);
	mapping->device = get_device(device);
	mapping->gasket_dev = gasket_dev;
	mapping->attribute_count = 0;
	mutex_unlock(&mapping->mutex);
	mutex_unlock(&function_mutex);

	/* Don't decrement the refcount here! One open count keeps it alive! */
	return 0;
}

int gasket_sysfs_create_entries(struct device *device,
				const struct gasket_sysfs_attribute *attrs)
{
	int i;
	int ret;
	struct gasket_sysfs_mapping *mapping = get_mapping(device);

	if (!mapping) {
		dev_dbg(device,
			"Creating entries for device without first initializing mapping\n");
		return -EINVAL;
	}

	mutex_lock(&mapping->mutex);
	for (i = 0; attrs[i].attr.attr.name; i++) {
		if (mapping->attribute_count == GASKET_SYSFS_MAX_NODES) {
			dev_err(device,
				"Maximum number of sysfs nodes reached for device\n");
			mutex_unlock(&mapping->mutex);
			put_mapping(mapping);
			return -ENOMEM;
		}

		ret = device_create_file(device, &attrs[i].attr);
		if (ret) {
			dev_dbg(device, "Unable to create device entries\n");
			mutex_unlock(&mapping->mutex);
			put_mapping(mapping);
			return ret;
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
		dev_err(device,
			"Attempted to remove non-existent sysfs mapping to device\n");
		return;
	}

	put_mapping_n(mapping, 2);
}

struct gasket_dev *gasket_sysfs_get_device_data(struct device *device)
{
	struct gasket_sysfs_mapping *mapping = get_mapping(device);

	if (!mapping) {
		dev_err(device, "device not registered\n");
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

struct gasket_sysfs_attribute *
gasket_sysfs_get_attr(struct device *device, struct device_attribute *attr)
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

	dev_err(device, "Unable to find match for device_attribute %s\n",
		attr->attr.name);
	return NULL;
}
EXPORT_SYMBOL(gasket_sysfs_get_attr);

void gasket_sysfs_put_attr(struct device *device,
			   struct gasket_sysfs_attribute *attr)
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

	dev_err(device, "Unable to put unknown attribute: %s\n",
		attr->attr.attr.name);
	put_mapping(mapping);
}
EXPORT_SYMBOL(gasket_sysfs_put_attr);

ssize_t gasket_sysfs_register_store(struct device *device,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	ulong parsed_value = 0;
	struct gasket_sysfs_mapping *mapping;
	struct gasket_dev *gasket_dev;
	struct gasket_sysfs_attribute *gasket_attr;

	if (count < 3 || buf[0] != '0' || buf[1] != 'x') {
		dev_err(device,
			"sysfs register write format: \"0x<hex value>\"\n");
		return -EINVAL;
	}

	if (kstrtoul(buf, 16, &parsed_value) != 0) {
		dev_err(device,
			"Unable to parse input as 64-bit hex value: %s\n", buf);
		return -EINVAL;
	}

	mapping = get_mapping(device);
	if (!mapping) {
		dev_err(device, "Device driver may have been removed\n");
		return 0;
	}

	gasket_dev = mapping->gasket_dev;
	if (!gasket_dev) {
		dev_err(device, "Device driver may have been removed\n");
		put_mapping(mapping);
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
		gasket_attr->write_callback(gasket_dev, gasket_attr,
					    parsed_value);

	gasket_sysfs_put_attr(device, gasket_attr);
	put_mapping(mapping);
	return count;
}
EXPORT_SYMBOL(gasket_sysfs_register_store);
