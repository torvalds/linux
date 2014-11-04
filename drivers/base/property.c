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
