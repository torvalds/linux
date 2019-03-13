/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Set of common sysfs utilities.
 *
 * Copyright (C) 2018 Google, Inc.
 */

/* The functions described here are a set of utilities to allow each file in the
 * Gasket driver framework to manage their own set of sysfs entries, instead of
 * centralizing all that work in one file.
 *
 * The goal of these utilities is to allow for sysfs entries to be easily
 * created without causing a proliferation of sysfs "show" functions. This
 * requires O(N) string lookups during show function execution, but as reading
 * sysfs entries is rarely performance-critical, this is likely acceptible.
 */
#ifndef __GASKET_SYSFS_H__
#define __GASKET_SYSFS_H__

#include "gasket_constants.h"
#include "gasket_core.h"
#include <linux/device.h>
#include <linux/stringify.h>
#include <linux/sysfs.h>

/* The maximum number of mappings/devices a driver needs to support. */
#define GASKET_SYSFS_NUM_MAPPINGS (GASKET_FRAMEWORK_DESC_MAX * GASKET_DEV_MAX)

/* The maximum number of sysfs nodes in a directory.
 */
#define GASKET_SYSFS_MAX_NODES 196

/* End markers for sysfs struct arrays. */
#define GASKET_ARRAY_END_TOKEN GASKET_RESERVED_ARRAY_END
#define GASKET_ARRAY_END_MARKER __stringify(GASKET_ARRAY_END_TOKEN)

/*
 * Terminator struct for a gasket_sysfs_attr array. Must be at the end of
 * all gasket_sysfs_attribute arrays.
 */
#define GASKET_END_OF_ATTR_ARRAY                                               \
	{                                                                      \
		.attr = __ATTR_NULL,				\
		.data.attr_type = 0,				\
	}

/*
 * Pairing of sysfs attribute and user data.
 * Used in lookups in sysfs "show" functions to return attribute metadata.
 */
struct gasket_sysfs_attribute {
	/* The underlying sysfs device attribute associated with this data. */
	struct device_attribute attr;

	/* User-specified data to associate with the attribute. */
	union {
		struct bar_address_ {
			ulong bar;
			ulong offset;
		} bar_address;
		uint attr_type;
	} data;

	/*
	 * Function pointer to a callback to be invoked when this attribute is
	 * written (if so configured). The arguments are to the Gasket device
	 * pointer, the enclosing gasket_attr structure, and the value written.
	 * The callback should perform any logging necessary, as errors cannot
	 * be returned from the callback.
	 */
	void (*write_callback)(struct gasket_dev *dev,
			       struct gasket_sysfs_attribute *attr,
			       ulong value);
};

#define GASKET_SYSFS_RO(_name, _show_function, _attr_type)                     \
	{                                                                      \
		.attr = __ATTR(_name, S_IRUGO, _show_function, NULL),          \
		.data.attr_type = _attr_type                                   \
	}

/* Initializes the Gasket sysfs subsystem.
 *
 * Description: Performs one-time initialization. Must be called before usage
 * at [Gasket] module load time.
 */
void gasket_sysfs_init(void);

/*
 * Create an entry in mapping_data between a device and a Gasket device.
 * @device: Device struct to map to.
 * @gasket_dev: The dev struct associated with the driver controlling @device.
 *
 * Description: This function maps a gasket_dev* to a device*. This mapping can
 * be used in sysfs_show functions to get a handle to the gasket_dev struct
 * controlling the device node.
 *
 * If this function is not called before gasket_sysfs_create_entries, a warning
 * will be logged.
 */
int gasket_sysfs_create_mapping(struct device *device,
				struct gasket_dev *gasket_dev);

/*
 * Creates bulk entries in sysfs.
 * @device: Kernel device structure.
 * @attrs: List of attributes/sysfs entries to create.
 *
 * Description: Creates each sysfs entry described in "attrs". Can be called
 * multiple times for a given @device. If the gasket_dev specified in
 * gasket_sysfs_create_mapping had a legacy device, the entries will be created
 * for it, as well.
 */
int gasket_sysfs_create_entries(struct device *device,
				const struct gasket_sysfs_attribute *attrs);

/*
 * Removes a device mapping from the global table.
 * @device: Device to unmap.
 *
 * Description: Removes the device->Gasket device mapping from the internal
 * table.
 */
void gasket_sysfs_remove_mapping(struct device *device);

/*
 * User data lookup based on kernel device structure.
 * @device: Kernel device structure.
 *
 * Description: Returns the user data associated with "device" in a prior call
 * to gasket_sysfs_create_entries. Returns NULL if no mapping can be found.
 * Upon success, this call take a reference to internal sysfs data that must be
 * released with gasket_sysfs_put_device_data. While this reference is held, the
 * underlying device sysfs information/structure will remain valid/will not be
 * deleted.
 */
struct gasket_dev *gasket_sysfs_get_device_data(struct device *device);

/*
 * Releases a references to internal data.
 * @device: Kernel device structure.
 * @dev: Gasket device descriptor (returned by gasket_sysfs_get_device_data).
 */
void gasket_sysfs_put_device_data(struct device *device,
				  struct gasket_dev *gasket_dev);

/*
 * Gasket-specific attribute lookup.
 * @device: Kernel device structure.
 * @attr: Device attribute to look up.
 *
 * Returns the Gasket sysfs attribute associated with the kernel device
 * attribute and device structure itself. Upon success, this call will take a
 * reference to internal sysfs data that must be released with a call to
 * gasket_sysfs_put_attr. While this reference is held, the underlying device
 * sysfs information/structure will remain valid/will not be deleted.
 */
struct gasket_sysfs_attribute *
gasket_sysfs_get_attr(struct device *device, struct device_attribute *attr);

/*
 * Releases a references to internal data.
 * @device: Kernel device structure.
 * @attr: Gasket sysfs attribute descriptor (returned by
 *        gasket_sysfs_get_attr).
 */
void gasket_sysfs_put_attr(struct device *device,
			   struct gasket_sysfs_attribute *attr);

/*
 * Write to a register sysfs node.
 * @buf: NULL-terminated data being written.
 * @count: number of bytes in the "buf" argument.
 */
ssize_t gasket_sysfs_register_store(struct device *device,
				    struct device_attribute *attr,
				    const char *buf, size_t count);

#endif /* __GASKET_SYSFS_H__ */
