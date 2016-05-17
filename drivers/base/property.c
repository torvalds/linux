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

#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/property.h>
#include <linux/etherdevice.h>
#include <linux/phy.h>

static inline bool is_pset_node(struct fwnode_handle *fwnode)
{
	return !IS_ERR_OR_NULL(fwnode) && fwnode->type == FWNODE_PDATA;
}

static inline struct property_set *to_pset_node(struct fwnode_handle *fwnode)
{
	return is_pset_node(fwnode) ?
		container_of(fwnode, struct property_set, fwnode) : NULL;
}

static struct property_entry *pset_prop_get(struct property_set *pset,
					    const char *name)
{
	struct property_entry *prop;

	if (!pset || !pset->properties)
		return NULL;

	for (prop = pset->properties; prop->name; prop++)
		if (!strcmp(name, prop->name))
			return prop;

	return NULL;
}

static void *pset_prop_find(struct property_set *pset, const char *propname,
			    size_t length)
{
	struct property_entry *prop;
	void *pointer;

	prop = pset_prop_get(pset, propname);
	if (!prop)
		return ERR_PTR(-EINVAL);
	if (prop->is_array)
		pointer = prop->pointer.raw_data;
	else
		pointer = &prop->value.raw_data;
	if (!pointer)
		return ERR_PTR(-ENODATA);
	if (length > prop->length)
		return ERR_PTR(-EOVERFLOW);
	return pointer;
}

static int pset_prop_read_u8_array(struct property_set *pset,
				   const char *propname,
				   u8 *values, size_t nval)
{
	void *pointer;
	size_t length = nval * sizeof(*values);

	pointer = pset_prop_find(pset, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(values, pointer, length);
	return 0;
}

static int pset_prop_read_u16_array(struct property_set *pset,
				    const char *propname,
				    u16 *values, size_t nval)
{
	void *pointer;
	size_t length = nval * sizeof(*values);

	pointer = pset_prop_find(pset, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(values, pointer, length);
	return 0;
}

static int pset_prop_read_u32_array(struct property_set *pset,
				    const char *propname,
				    u32 *values, size_t nval)
{
	void *pointer;
	size_t length = nval * sizeof(*values);

	pointer = pset_prop_find(pset, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(values, pointer, length);
	return 0;
}

static int pset_prop_read_u64_array(struct property_set *pset,
				    const char *propname,
				    u64 *values, size_t nval)
{
	void *pointer;
	size_t length = nval * sizeof(*values);

	pointer = pset_prop_find(pset, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(values, pointer, length);
	return 0;
}

static int pset_prop_count_elems_of_size(struct property_set *pset,
					 const char *propname, size_t length)
{
	struct property_entry *prop;

	prop = pset_prop_get(pset, propname);
	if (!prop)
		return -EINVAL;

	return prop->length / length;
}

static int pset_prop_read_string_array(struct property_set *pset,
				       const char *propname,
				       const char **strings, size_t nval)
{
	void *pointer;
	size_t length = nval * sizeof(*strings);

	pointer = pset_prop_find(pset, propname, length);
	if (IS_ERR(pointer))
		return PTR_ERR(pointer);

	memcpy(strings, pointer, length);
	return 0;
}

static int pset_prop_read_string(struct property_set *pset,
				 const char *propname, const char **strings)
{
	struct property_entry *prop;
	const char **pointer;

	prop = pset_prop_get(pset, propname);
	if (!prop)
		return -EINVAL;
	if (!prop->is_string)
		return -EILSEQ;
	if (prop->is_array) {
		pointer = prop->pointer.str;
		if (!pointer)
			return -ENODATA;
	} else {
		pointer = &prop->value.str;
		if (*pointer && strnlen(*pointer, prop->length) >= prop->length)
			return -EILSEQ;
	}

	*strings = *pointer;
	return 0;
}

static inline struct fwnode_handle *dev_fwnode(struct device *dev)
{
	return IS_ENABLED(CONFIG_OF) && dev->of_node ?
		&dev->of_node->fwnode : dev->fwnode;
}

/**
 * device_property_present - check if a property of a device is present
 * @dev: Device whose property is being checked
 * @propname: Name of the property
 *
 * Check if property @propname is present in the device firmware description.
 */
bool device_property_present(struct device *dev, const char *propname)
{
	return fwnode_property_present(dev_fwnode(dev), propname);
}
EXPORT_SYMBOL_GPL(device_property_present);

static bool __fwnode_property_present(struct fwnode_handle *fwnode,
				      const char *propname)
{
	if (is_of_node(fwnode))
		return of_property_read_bool(to_of_node(fwnode), propname);
	else if (is_acpi_node(fwnode))
		return !acpi_node_prop_get(fwnode, propname, NULL);
	else if (is_pset_node(fwnode))
		return !!pset_prop_get(to_pset_node(fwnode), propname);
	return false;
}

/**
 * fwnode_property_present - check if a property of a firmware node is present
 * @fwnode: Firmware node whose property to check
 * @propname: Name of the property
 */
bool fwnode_property_present(struct fwnode_handle *fwnode, const char *propname)
{
	bool ret;

	ret = __fwnode_property_present(fwnode, propname);
	if (ret == false && !IS_ERR_OR_NULL(fwnode) &&
	    !IS_ERR_OR_NULL(fwnode->secondary))
		ret = __fwnode_property_present(fwnode->secondary, propname);
	return ret;
}
EXPORT_SYMBOL_GPL(fwnode_property_present);

/**
 * device_property_read_u8_array - return a u8 array property of a device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Function reads an array of u8 properties with @propname from the device
 * firmware description and stores them to @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_u8_array(struct device *dev, const char *propname,
				  u8 *val, size_t nval)
{
	return fwnode_property_read_u8_array(dev_fwnode(dev), propname, val, nval);
}
EXPORT_SYMBOL_GPL(device_property_read_u8_array);

/**
 * device_property_read_u16_array - return a u16 array property of a device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Function reads an array of u16 properties with @propname from the device
 * firmware description and stores them to @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_u16_array(struct device *dev, const char *propname,
				   u16 *val, size_t nval)
{
	return fwnode_property_read_u16_array(dev_fwnode(dev), propname, val, nval);
}
EXPORT_SYMBOL_GPL(device_property_read_u16_array);

/**
 * device_property_read_u32_array - return a u32 array property of a device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Function reads an array of u32 properties with @propname from the device
 * firmware description and stores them to @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_u32_array(struct device *dev, const char *propname,
				   u32 *val, size_t nval)
{
	return fwnode_property_read_u32_array(dev_fwnode(dev), propname, val, nval);
}
EXPORT_SYMBOL_GPL(device_property_read_u32_array);

/**
 * device_property_read_u64_array - return a u64 array property of a device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Function reads an array of u64 properties with @propname from the device
 * firmware description and stores them to @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_u64_array(struct device *dev, const char *propname,
				   u64 *val, size_t nval)
{
	return fwnode_property_read_u64_array(dev_fwnode(dev), propname, val, nval);
}
EXPORT_SYMBOL_GPL(device_property_read_u64_array);

/**
 * device_property_read_string_array - return a string array property of device
 * @dev: Device to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Function reads an array of string properties with @propname from the device
 * firmware description and stores them to @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO or %-EILSEQ if the property is not an array of strings,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_string_array(struct device *dev, const char *propname,
				      const char **val, size_t nval)
{
	return fwnode_property_read_string_array(dev_fwnode(dev), propname, val, nval);
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
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_string(struct device *dev, const char *propname,
				const char **val)
{
	return fwnode_property_read_string(dev_fwnode(dev), propname, val);
}
EXPORT_SYMBOL_GPL(device_property_read_string);

/**
 * device_property_match_string - find a string in an array and return index
 * @dev: Device to get the property of
 * @propname: Name of the property holding the array
 * @string: String to look for
 *
 * Find a given string in a string array and if it is found return the
 * index back.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of strings,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_match_string(struct device *dev, const char *propname,
				 const char *string)
{
	return fwnode_property_match_string(dev_fwnode(dev), propname, string);
}
EXPORT_SYMBOL_GPL(device_property_match_string);

#define OF_DEV_PROP_READ_ARRAY(node, propname, type, val, nval)				\
	(val) ? of_property_read_##type##_array((node), (propname), (val), (nval))	\
	      : of_property_count_elems_of_size((node), (propname), sizeof(type))

#define PSET_PROP_READ_ARRAY(node, propname, type, val, nval)				\
	(val) ? pset_prop_read_##type##_array((node), (propname), (val), (nval))	\
	      : pset_prop_count_elems_of_size((node), (propname), sizeof(type))

#define FWNODE_PROP_READ(_fwnode_, _propname_, _type_, _proptype_, _val_, _nval_)	\
({											\
	int _ret_;									\
	if (is_of_node(_fwnode_))							\
		_ret_ = OF_DEV_PROP_READ_ARRAY(to_of_node(_fwnode_), _propname_,	\
					       _type_, _val_, _nval_);			\
	else if (is_acpi_node(_fwnode_))						\
		_ret_ = acpi_node_prop_read(_fwnode_, _propname_, _proptype_,		\
					    _val_, _nval_);				\
	else if (is_pset_node(_fwnode_)) 						\
		_ret_ = PSET_PROP_READ_ARRAY(to_pset_node(_fwnode_), _propname_,	\
					     _type_, _val_, _nval_);			\
	else										\
		_ret_ = -ENXIO;								\
	_ret_;										\
})

#define FWNODE_PROP_READ_ARRAY(_fwnode_, _propname_, _type_, _proptype_, _val_, _nval_)	\
({											\
	int _ret_;									\
	_ret_ = FWNODE_PROP_READ(_fwnode_, _propname_, _type_, _proptype_,		\
				 _val_, _nval_);					\
	if (_ret_ == -EINVAL && !IS_ERR_OR_NULL(_fwnode_) &&				\
	    !IS_ERR_OR_NULL(_fwnode_->secondary))					\
		_ret_ = FWNODE_PROP_READ(_fwnode_->secondary, _propname_, _type_,	\
				_proptype_, _val_, _nval_);				\
	_ret_;										\
})

/**
 * fwnode_property_read_u8_array - return a u8 array property of firmware node
 * @fwnode: Firmware node to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u8 properties with @propname from @fwnode and stores them to
 * @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
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
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u16 properties with @propname from @fwnode and store them to
 * @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
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
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u32 properties with @propname from @fwnode store them to
 * @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
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
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u64 properties with @propname from @fwnode and store them to
 * @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
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

static int __fwnode_property_read_string_array(struct fwnode_handle *fwnode,
					       const char *propname,
					       const char **val, size_t nval)
{
	if (is_of_node(fwnode))
		return val ?
			of_property_read_string_array(to_of_node(fwnode),
						      propname, val, nval) :
			of_property_count_strings(to_of_node(fwnode), propname);
	else if (is_acpi_node(fwnode))
		return acpi_node_prop_read(fwnode, propname, DEV_PROP_STRING,
					   val, nval);
	else if (is_pset_node(fwnode))
		return val ?
			pset_prop_read_string_array(to_pset_node(fwnode),
						    propname, val, nval) :
			pset_prop_count_elems_of_size(to_pset_node(fwnode),
						      propname,
						      sizeof(const char *));
	return -ENXIO;
}

static int __fwnode_property_read_string(struct fwnode_handle *fwnode,
					 const char *propname, const char **val)
{
	if (is_of_node(fwnode))
		return of_property_read_string(to_of_node(fwnode), propname, val);
	else if (is_acpi_node(fwnode))
		return acpi_node_prop_read(fwnode, propname, DEV_PROP_STRING,
					   val, 1);
	else if (is_pset_node(fwnode))
		return pset_prop_read_string(to_pset_node(fwnode), propname, val);
	return -ENXIO;
}

/**
 * fwnode_property_read_string_array - return string array property of a node
 * @fwnode: Firmware node to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an string list property @propname from the given firmware node and store
 * them to @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
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
	int ret;

	ret = __fwnode_property_read_string_array(fwnode, propname, val, nval);
	if (ret == -EINVAL && !IS_ERR_OR_NULL(fwnode) &&
	    !IS_ERR_OR_NULL(fwnode->secondary))
		ret = __fwnode_property_read_string_array(fwnode->secondary,
							  propname, val, nval);
	return ret;
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
	int ret;

	ret = __fwnode_property_read_string(fwnode, propname, val);
	if (ret == -EINVAL && !IS_ERR_OR_NULL(fwnode) &&
	    !IS_ERR_OR_NULL(fwnode->secondary))
		ret = __fwnode_property_read_string(fwnode->secondary,
						    propname, val);
	return ret;
}
EXPORT_SYMBOL_GPL(fwnode_property_read_string);

/**
 * fwnode_property_match_string - find a string in an array and return index
 * @fwnode: Firmware node to get the property of
 * @propname: Name of the property holding the array
 * @string: String to look for
 *
 * Find a given string in a string array and if it is found return the
 * index back.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of strings,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_match_string(struct fwnode_handle *fwnode,
	const char *propname, const char *string)
{
	const char **values;
	int nval, ret;

	nval = fwnode_property_read_string_array(fwnode, propname, NULL, 0);
	if (nval < 0)
		return nval;

	if (nval == 0)
		return -ENODATA;

	values = kcalloc(nval, sizeof(*values), GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	ret = fwnode_property_read_string_array(fwnode, propname, values, nval);
	if (ret < 0)
		goto out;

	ret = match_string(values, nval, string);
	if (ret < 0)
		ret = -ENODATA;
out:
	kfree(values);
	return ret;
}
EXPORT_SYMBOL_GPL(fwnode_property_match_string);

/**
 * pset_free_set - releases memory allocated for copied property set
 * @pset: Property set to release
 *
 * Function takes previously copied property set and releases all the
 * memory allocated to it.
 */
static void pset_free_set(struct property_set *pset)
{
	const struct property_entry *prop;
	size_t i, nval;

	if (!pset)
		return;

	for (prop = pset->properties; prop->name; prop++) {
		if (prop->is_array) {
			if (prop->is_string && prop->pointer.str) {
				nval = prop->length / sizeof(const char *);
				for (i = 0; i < nval; i++)
					kfree(prop->pointer.str[i]);
			}
			kfree(prop->pointer.raw_data);
		} else if (prop->is_string) {
			kfree(prop->value.str);
		}
		kfree(prop->name);
	}

	kfree(pset->properties);
	kfree(pset);
}

static int pset_copy_entry(struct property_entry *dst,
			   const struct property_entry *src)
{
	const char **d, **s;
	size_t i, nval;

	dst->name = kstrdup(src->name, GFP_KERNEL);
	if (!dst->name)
		return -ENOMEM;

	if (src->is_array) {
		if (!src->length)
			return -ENODATA;

		if (src->is_string) {
			nval = src->length / sizeof(const char *);
			dst->pointer.str = kcalloc(nval, sizeof(const char *),
						   GFP_KERNEL);
			if (!dst->pointer.str)
				return -ENOMEM;

			d = dst->pointer.str;
			s = src->pointer.str;
			for (i = 0; i < nval; i++) {
				d[i] = kstrdup(s[i], GFP_KERNEL);
				if (!d[i] && s[i])
					return -ENOMEM;
			}
		} else {
			dst->pointer.raw_data = kmemdup(src->pointer.raw_data,
							src->length, GFP_KERNEL);
			if (!dst->pointer.raw_data)
				return -ENOMEM;
		}
	} else if (src->is_string) {
		dst->value.str = kstrdup(src->value.str, GFP_KERNEL);
		if (!dst->value.str && src->value.str)
			return -ENOMEM;
	} else {
		dst->value.raw_data = src->value.raw_data;
	}

	dst->length = src->length;
	dst->is_array = src->is_array;
	dst->is_string = src->is_string;

	return 0;
}

/**
 * pset_copy_set - copies property set
 * @pset: Property set to copy
 *
 * This function takes a deep copy of the given property set and returns
 * pointer to the copy. Call device_free_property_set() to free resources
 * allocated in this function.
 *
 * Return: Pointer to the new property set or error pointer.
 */
static struct property_set *pset_copy_set(const struct property_set *pset)
{
	const struct property_entry *entry;
	struct property_set *p;
	size_t i, n = 0;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	while (pset->properties[n].name)
		n++;

	p->properties = kcalloc(n + 1, sizeof(*entry), GFP_KERNEL);
	if (!p->properties) {
		kfree(p);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < n; i++) {
		int ret = pset_copy_entry(&p->properties[i],
					  &pset->properties[i]);
		if (ret) {
			pset_free_set(p);
			return ERR_PTR(ret);
		}
	}

	return p;
}

/**
 * device_remove_property_set - Remove properties from a device object.
 * @dev: Device whose properties to remove.
 *
 * The function removes properties previously associated to the device
 * secondary firmware node with device_add_property_set(). Memory allocated
 * to the properties will also be released.
 */
void device_remove_property_set(struct device *dev)
{
	struct fwnode_handle *fwnode;

	fwnode = dev_fwnode(dev);
	if (!fwnode)
		return;
	/*
	 * Pick either primary or secondary node depending which one holds
	 * the pset. If there is no real firmware node (ACPI/DT) primary
	 * will hold the pset.
	 */
	if (is_pset_node(fwnode)) {
		set_primary_fwnode(dev, NULL);
		pset_free_set(to_pset_node(fwnode));
	} else {
		fwnode = fwnode->secondary;
		if (!IS_ERR(fwnode) && is_pset_node(fwnode)) {
			set_secondary_fwnode(dev, NULL);
			pset_free_set(to_pset_node(fwnode));
		}
	}
}
EXPORT_SYMBOL_GPL(device_remove_property_set);

/**
 * device_add_property_set - Add a collection of properties to a device object.
 * @dev: Device to add properties to.
 * @pset: Collection of properties to add.
 *
 * Associate a collection of device properties represented by @pset with @dev
 * as its secondary firmware node. The function takes a copy of @pset.
 */
int device_add_property_set(struct device *dev, const struct property_set *pset)
{
	struct property_set *p;

	if (!pset)
		return -EINVAL;

	p = pset_copy_set(pset);
	if (IS_ERR(p))
		return PTR_ERR(p);

	p->fwnode.type = FWNODE_PDATA;
	set_secondary_fwnode(dev, &p->fwnode);
	return 0;
}
EXPORT_SYMBOL_GPL(device_add_property_set);

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

		node = of_get_next_available_child(dev->of_node, to_of_node(child));
		if (node)
			return &node->fwnode;
	} else if (IS_ENABLED(CONFIG_ACPI)) {
		return acpi_get_next_subnode(dev, child);
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
		of_node_put(to_of_node(fwnode));
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

bool device_dma_supported(struct device *dev)
{
	/* For DT, this is always supported.
	 * For ACPI, this depends on CCA, which
	 * is determined by the acpi_dma_supported().
	 */
	if (IS_ENABLED(CONFIG_OF) && dev->of_node)
		return true;

	return acpi_dma_supported(ACPI_COMPANION(dev));
}
EXPORT_SYMBOL_GPL(device_dma_supported);

enum dev_dma_attr device_get_dma_attr(struct device *dev)
{
	enum dev_dma_attr attr = DEV_DMA_NOT_SUPPORTED;

	if (IS_ENABLED(CONFIG_OF) && dev->of_node) {
		if (of_dma_is_coherent(dev->of_node))
			attr = DEV_DMA_COHERENT;
		else
			attr = DEV_DMA_NON_COHERENT;
	} else
		attr = acpi_get_dma_attr(ACPI_COMPANION(dev));

	return attr;
}
EXPORT_SYMBOL_GPL(device_get_dma_attr);

/**
 * device_get_phy_mode - Get phy mode for given device
 * @dev:	Pointer to the given device
 *
 * The function gets phy interface string from property 'phy-mode' or
 * 'phy-connection-type', and return its index in phy_modes table, or errno in
 * error case.
 */
int device_get_phy_mode(struct device *dev)
{
	const char *pm;
	int err, i;

	err = device_property_read_string(dev, "phy-mode", &pm);
	if (err < 0)
		err = device_property_read_string(dev,
						  "phy-connection-type", &pm);
	if (err < 0)
		return err;

	for (i = 0; i < PHY_INTERFACE_MODE_MAX; i++)
		if (!strcasecmp(pm, phy_modes(i)))
			return i;

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(device_get_phy_mode);

static void *device_get_mac_addr(struct device *dev,
				 const char *name, char *addr,
				 int alen)
{
	int ret = device_property_read_u8_array(dev, name, addr, alen);

	if (ret == 0 && alen == ETH_ALEN && is_valid_ether_addr(addr))
		return addr;
	return NULL;
}

/**
 * device_get_mac_address - Get the MAC for a given device
 * @dev:	Pointer to the device
 * @addr:	Address of buffer to store the MAC in
 * @alen:	Length of the buffer pointed to by addr, should be ETH_ALEN
 *
 * Search the firmware node for the best MAC address to use.  'mac-address' is
 * checked first, because that is supposed to contain to "most recent" MAC
 * address. If that isn't set, then 'local-mac-address' is checked next,
 * because that is the default address.  If that isn't set, then the obsolete
 * 'address' is checked, just in case we're using an old device tree.
 *
 * Note that the 'address' property is supposed to contain a virtual address of
 * the register set, but some DTS files have redefined that property to be the
 * MAC address.
 *
 * All-zero MAC addresses are rejected, because those could be properties that
 * exist in the firmware tables, but were not updated by the firmware.  For
 * example, the DTS could define 'mac-address' and 'local-mac-address', with
 * zero MAC addresses.  Some older U-Boots only initialized 'local-mac-address'.
 * In this case, the real MAC is in 'local-mac-address', and 'mac-address'
 * exists but is all zeros.
*/
void *device_get_mac_address(struct device *dev, char *addr, int alen)
{
	char *res;

	res = device_get_mac_addr(dev, "mac-address", addr, alen);
	if (res)
		return res;

	res = device_get_mac_addr(dev, "local-mac-address", addr, alen);
	if (res)
		return res;

	return device_get_mac_addr(dev, "address", addr, alen);
}
EXPORT_SYMBOL(device_get_mac_address);
