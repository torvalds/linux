/*
 * property.c - Unified device property interface.
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/property.h>
#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/of.h>

/**
 * device_property_present - check if a property of a device is present
 * @dev: Device whose property is being checked
 * @propname: Name of the property
 *
 * Check if property @propname is present in the device firmware description.
 */
bool device_property_present(struct device *dev, const char *propname)
{
	if (IS_ENABLED(CONFIG_OF) && dev->of_node)
		return of_property_read_bool(dev->of_node, propname);

	return !acpi_dev_prop_get(ACPI_COMPANION(dev), propname, NULL);
}
EXPORT_SYMBOL_GPL(device_property_present);

/**
 * fwnode_property_present - check if a property of a firmware node is present
 * @fwnode: Firmware node whose property to check
 * @propname: Name of the property
 */
bool fwnode_property_present(struct fwnode_handle *fwnode, const char *propname)
{
	if (is_of_node(fwnode))
		return of_property_read_bool(of_node(fwnode), propname);
	else if (is_acpi_node(fwnode))
		return !acpi_dev_prop_get(acpi_node(fwnode), propname, NULL);

	return false;
}
EXPORT_SYMBOL_GPL(fwnode_property_present);

#define OF_DEV_PROP_READ_ARRAY(node, propname, type, val, nval) \
	(val) ? of_property_read_##type##_array((node), (propname), (val), (nval)) \
	      : of_property_count_elems_of_size((node), (propname), sizeof(type))

#define DEV_PROP_READ_ARRAY(_dev_, _propname_, _type_, _proptype_, _val_, _nval_) \
	IS_ENABLED(CONFIG_OF) && _dev_->of_node ? \
		(OF_DEV_PROP_READ_ARRAY(_dev_->of_node, _propname_, _type_, \
					_val_, _nval_)) : \
		acpi_dev_prop_read(ACPI_COMPANION(_dev_), _propname_, \
				   _proptype_, _val_, _nval_)

/**
 * device_property_read_u8_array - return a u8 array property of a device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The values are stored here
 * @nval: Size of the @val array
 *
 * Function reads an array of u8 properties with @propname from the device
 * firmware description and stores them to @val if found.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 */
int device_property_read_u8_array(struct device *dev, const char *propname,
				  u8 *val, size_t nval)
{
	return DEV_PROP_READ_ARRAY(dev, propname, u8, DEV_PROP_U8, val, nval);
}
EXPORT_SYMBOL_GPL(device_property_read_u8_array);

/**
 * device_property_read_u16_array - return a u16 array property of a device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The values are stored here
 * @nval: Size of the @val array
 *
 * Function reads an array of u16 properties with @propname from the device
 * firmware description and stores them to @val if found.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 */
int device_property_read_u16_array(struct device *dev, const char *propname,
				   u16 *val, size_t nval)
{
	return DEV_PROP_READ_ARRAY(dev, propname, u16, DEV_PROP_U16, val, nval);
}
EXPORT_SYMBOL_GPL(device_property_read_u16_array);

/**
 * device_property_read_u32_array - return a u32 array property of a device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The values are stored here
 * @nval: Size of the @val array
 *
 * Function reads an array of u32 properties with @propname from the device
 * firmware description and stores them to @val if found.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 */
int device_property_read_u32_array(struct device *dev, const char *propname,
				   u32 *val, size_t nval)
{
	return DEV_PROP_READ_ARRAY(dev, propname, u32, DEV_PROP_U32, val, nval);
}
EXPORT_SYMBOL_GPL(device_property_read_u32_array);

/**
 * device_property_read_u64_array - return a u64 array property of a device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The values are stored here
 * @nval: Size of the @val array
 *
 * Function reads an array of u64 properties with @propname from the device
 * firmware description and stores them to @val if found.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 */
int device_property_read_u64_array(struct device *dev, const char *propname,
				   u64 *val, size_t nval)
{
	return DEV_PROP_READ_ARRAY(dev, propname, u64, DEV_PROP_U64, val, nval);
}
EXPORT_SYMBOL_GPL(device_property_read_u64_array);

/**
 * device_property_read_string_array - return a string array property of device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The values are stored here
 * @nval: Size of the @val array
 *
 * Function reads an array of string properties with @propname from the device
 * firmware description and stores them to @val if found.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO or %-EILSEQ if the property is not an array of strings,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 */
int device_property_read_string_array(struct device *dev, const char *propname,
				      const char **val, size_t nval)
{
	return IS_ENABLED(CONFIG_OF) && dev->of_node ?
		of_property_read_string_array(dev->of_node, propname, val, nval) :
		acpi_dev_prop_read(ACPI_COMPANION(dev), propname,
				   DEV_PROP_STRING, val, nval);
}
EXPORT_SYMBOL_GPL(device_property_read_string_array);

/**
 * device_property_read_string - return a string property of a device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The value is stored here
 *
 * Function reads property @propname from the device firmware description and
 * stores the value into @val if found. The value is checked to be a string.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO or %-EILSEQ if the property type is not a string.
 */
int device_property_read_string(struct device *dev, const char *propname,
				const char **val)
{
	return IS_ENABLED(CONFIG_OF) && dev->of_node ?
		of_property_read_string(dev->of_node, propname, val) :
		acpi_dev_prop_read(ACPI_COMPANION(dev), propname,
				   DEV_PROP_STRING, val, 1);
}
EXPORT_SYMBOL_GPL(device_property_read_string);

#define FWNODE_PROP_READ_ARRAY(_fwnode_, _propname_, _type_, _proptype_, _val_, _nval_) \
({ \
	int _ret_; \
	if (is_of_node(_fwnode_)) \
		_ret_ = OF_DEV_PROP_READ_ARRAY(of_node(_fwnode_), _propname_, \
					       _type_, _val_, _nval_); \
	else if (is_acpi_node(_fwnode_)) \
		_ret_ = acpi_dev_prop_read(acpi_node(_fwnode_), _propname_, \
					   _proptype_, _val_, _nval_); \
	else \
		_ret_ = -ENXIO; \
	_ret_; \
})

/**
 * fwnode_property_read_u8_array - return a u8 array property of firmware node
 * @fwnode: Firmware node to get the property of
 * @propname: Name of the property
 * @val: The values are stored here
 * @nval: Size of the @val array
 *
 * Read an array of u8 properties with @propname from @fwnode and stores them to
 * @val if found.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_u8_array(struct fwnode_handle *fwnode,
				  const char *propname, u8 *val, size_t nval)
{
	return FWNODE_PROP_READ_ARRAY(fwnode, propname, u8, DEV_PROP_U8,
				      val, nval);
}
EXPORT_SYMBOL_GPL(fwnode_property_read_u8_array);

/**
 * fwnode_property_read_u16_array - return a u16 array property of firmware node
 * @fwnode: Firmware node to get the property of
 * @propname: Name of the property
 * @val: The values are stored here
 * @nval: Size of the @val array
 *
 * Read an array of u16 properties with @propname from @fwnode and store them to
 * @val if found.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_u16_array(struct fwnode_handle *fwnode,
				   const char *propname, u16 *val, size_t nval)
{
	return FWNODE_PROP_READ_ARRAY(fwnode, propname, u16, DEV_PROP_U16,
				      val, nval);
}
EXPORT_SYMBOL_GPL(fwnode_property_read_u16_array);

/**
 * fwnode_property_read_u32_array - return a u32 array property of firmware node
 * @fwnode: Firmware node to get the property of
 * @propname: Name of the property
 * @val: The values are stored here
 * @nval: Size of the @val array
 *
 * Read an array of u32 properties with @propname from @fwnode store them to
 * @val if found.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_u32_array(struct fwnode_handle *fwnode,
				   const char *propname, u32 *val, size_t nval)
{
	return FWNODE_PROP_READ_ARRAY(fwnode, propname, u32, DEV_PROP_U32,
				      val, nval);
}
EXPORT_SYMBOL_GPL(fwnode_property_read_u32_array);

/**
 * fwnode_property_read_u64_array - return a u64 array property firmware node
 * @fwnode: Firmware node to get the property of
 * @propname: Name of the property
 * @val: The values are stored here
 * @nval: Size of the @val array
 *
 * Read an array of u64 properties with @propname from @fwnode and store them to
 * @val if found.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_u64_array(struct fwnode_handle *fwnode,
				   const char *propname, u64 *val, size_t nval)
{
	return FWNODE_PROP_READ_ARRAY(fwnode, propname, u64, DEV_PROP_U64,
				      val, nval);
}
EXPORT_SYMBOL_GPL(fwnode_property_read_u64_array);

/**
 * fwnode_property_read_string_array - return string array property of a node
 * @fwnode: Firmware node to get the property of
 * @propname: Name of the property
 * @val: The values are stored here
 * @nval: Size of the @val array
 *
 * Read an string list property @propname from the given firmware node and store
 * them to @val if found.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of strings,
 *	   %-EOVERFLOW if the size of the property is not as expected,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_string_array(struct fwnode_handle *fwnode,
				      const char *propname, const char **val,
				      size_t nval)
{
	if (is_of_node(fwnode))
		return of_property_read_string_array(of_node(fwnode), propname,
						     val, nval);
	else if (is_acpi_node(fwnode))
		return acpi_dev_prop_read(acpi_node(fwnode), propname,
					  DEV_PROP_STRING, val, nval);

	return -ENXIO;
}
EXPORT_SYMBOL_GPL(fwnode_property_read_string_array);

/**
 * fwnode_property_read_string - return a string property of a firmware node
 * @fwnode: Firmware node to get the property of
 * @propname: Name of the property
 * @val: The value is stored here
 *
 * Read property @propname from the given firmware node and store the value into
 * @val if found.  The value is checked to be a string.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO or %-EILSEQ if the property is not a string,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_string(struct fwnode_handle *fwnode,
				const char *propname, const char **val)
{
	if (is_of_node(fwnode))
		return of_property_read_string(of_node(fwnode),propname, val);
	else if (is_acpi_node(fwnode))
		return acpi_dev_prop_read(acpi_node(fwnode), propname,
					  DEV_PROP_STRING, val, 1);

	return -ENXIO;
}
EXPORT_SYMBOL_GPL(fwnode_property_read_string);

/**
 * device_get_next_child_node - Return the next child node handle for a device
 * @dev: Device to find the next child node for.
 * @child: Handle to one of the device's child nodes or a null handle.
 */
struct fwnode_handle *device_get_next_child_node(struct device *dev,
						 struct fwnode_handle *child)
{
	if (IS_ENABLED(CONFIG_OF) && dev->of_node) {
		struct device_node *node;

		node = of_get_next_available_child(dev->of_node, of_node(child));
		if (node)
			return &node->fwnode;
	} else if (IS_ENABLED(CONFIG_ACPI)) {
		struct acpi_device *node;

		node = acpi_get_next_child(dev, acpi_node(child));
		if (node)
			return acpi_fwnode_handle(node);
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(device_get_next_child_node);

/**
 * fwnode_handle_put - Drop reference to a device node
 * @fwnode: Pointer to the device node to drop the reference to.
 *
 * This has to be used when terminating device_for_each_child_node() iteration
 * with break or return to prevent stale device node references from being left
 * behind.
 */
void fwnode_handle_put(struct fwnode_handle *fwnode)
{
	if (is_of_node(fwnode))
		of_node_put(of_node(fwnode));
}
EXPORT_SYMBOL_GPL(fwnode_handle_put);

/**
 * device_get_child_node_count - return the number of child nodes for device
 * @dev: Device to cound the child nodes for
 */
unsigned int device_get_child_node_count(struct device *dev)
{
	struct fwnode_handle *child;
	unsigned int count = 0;

	device_for_each_child_node(dev, child)
		count++;

	return count;
}
EXPORT_SYMBOL_GPL(device_get_child_node_count);
