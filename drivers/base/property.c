// SPDX-License-Identifier: GPL-2.0
/*
 * property.c - Unified device property interface.
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/property.h>
#include <linux/etherdevice.h>
#include <linux/phy.h>

struct fwyesde_handle *dev_fwyesde(struct device *dev)
{
	return IS_ENABLED(CONFIG_OF) && dev->of_yesde ?
		&dev->of_yesde->fwyesde : dev->fwyesde;
}
EXPORT_SYMBOL_GPL(dev_fwyesde);

/**
 * device_property_present - check if a property of a device is present
 * @dev: Device whose property is being checked
 * @propname: Name of the property
 *
 * Check if property @propname is present in the device firmware description.
 */
bool device_property_present(struct device *dev, const char *propname)
{
	return fwyesde_property_present(dev_fwyesde(dev), propname);
}
EXPORT_SYMBOL_GPL(device_property_present);

/**
 * fwyesde_property_present - check if a property of a firmware yesde is present
 * @fwyesde: Firmware yesde whose property to check
 * @propname: Name of the property
 */
bool fwyesde_property_present(const struct fwyesde_handle *fwyesde,
			     const char *propname)
{
	bool ret;

	ret = fwyesde_call_bool_op(fwyesde, property_present, propname);
	if (ret == false && !IS_ERR_OR_NULL(fwyesde) &&
	    !IS_ERR_OR_NULL(fwyesde->secondary))
		ret = fwyesde_call_bool_op(fwyesde->secondary, property_present,
					 propname);
	return ret;
}
EXPORT_SYMBOL_GPL(fwyesde_property_present);

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
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO if the property is yest an array of numbers,
 *	   %-EOVERFLOW if the size of the property is yest as expected.
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int device_property_read_u8_array(struct device *dev, const char *propname,
				  u8 *val, size_t nval)
{
	return fwyesde_property_read_u8_array(dev_fwyesde(dev), propname, val, nval);
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
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO if the property is yest an array of numbers,
 *	   %-EOVERFLOW if the size of the property is yest as expected.
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int device_property_read_u16_array(struct device *dev, const char *propname,
				   u16 *val, size_t nval)
{
	return fwyesde_property_read_u16_array(dev_fwyesde(dev), propname, val, nval);
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
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO if the property is yest an array of numbers,
 *	   %-EOVERFLOW if the size of the property is yest as expected.
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int device_property_read_u32_array(struct device *dev, const char *propname,
				   u32 *val, size_t nval)
{
	return fwyesde_property_read_u32_array(dev_fwyesde(dev), propname, val, nval);
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
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO if the property is yest an array of numbers,
 *	   %-EOVERFLOW if the size of the property is yest as expected.
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int device_property_read_u64_array(struct device *dev, const char *propname,
				   u64 *val, size_t nval)
{
	return fwyesde_property_read_u64_array(dev_fwyesde(dev), propname, val, nval);
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
 * Return: number of values read on success if @val is yesn-NULL,
 *	   number of values available on success if @val is NULL,
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO or %-EILSEQ if the property is yest an array of strings,
 *	   %-EOVERFLOW if the size of the property is yest as expected.
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int device_property_read_string_array(struct device *dev, const char *propname,
				      const char **val, size_t nval)
{
	return fwyesde_property_read_string_array(dev_fwyesde(dev), propname, val, nval);
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
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO or %-EILSEQ if the property type is yest a string.
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int device_property_read_string(struct device *dev, const char *propname,
				const char **val)
{
	return fwyesde_property_read_string(dev_fwyesde(dev), propname, val);
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
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO if the property is yest an array of strings,
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int device_property_match_string(struct device *dev, const char *propname,
				 const char *string)
{
	return fwyesde_property_match_string(dev_fwyesde(dev), propname, string);
}
EXPORT_SYMBOL_GPL(device_property_match_string);

static int fwyesde_property_read_int_array(const struct fwyesde_handle *fwyesde,
					  const char *propname,
					  unsigned int elem_size, void *val,
					  size_t nval)
{
	int ret;

	ret = fwyesde_call_int_op(fwyesde, property_read_int_array, propname,
				 elem_size, val, nval);
	if (ret == -EINVAL && !IS_ERR_OR_NULL(fwyesde) &&
	    !IS_ERR_OR_NULL(fwyesde->secondary))
		ret = fwyesde_call_int_op(
			fwyesde->secondary, property_read_int_array, propname,
			elem_size, val, nval);

	return ret;
}

/**
 * fwyesde_property_read_u8_array - return a u8 array property of firmware yesde
 * @fwyesde: Firmware yesde to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u8 properties with @propname from @fwyesde and stores them to
 * @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO if the property is yest an array of numbers,
 *	   %-EOVERFLOW if the size of the property is yest as expected,
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int fwyesde_property_read_u8_array(const struct fwyesde_handle *fwyesde,
				  const char *propname, u8 *val, size_t nval)
{
	return fwyesde_property_read_int_array(fwyesde, propname, sizeof(u8),
					      val, nval);
}
EXPORT_SYMBOL_GPL(fwyesde_property_read_u8_array);

/**
 * fwyesde_property_read_u16_array - return a u16 array property of firmware yesde
 * @fwyesde: Firmware yesde to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u16 properties with @propname from @fwyesde and store them to
 * @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO if the property is yest an array of numbers,
 *	   %-EOVERFLOW if the size of the property is yest as expected,
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int fwyesde_property_read_u16_array(const struct fwyesde_handle *fwyesde,
				   const char *propname, u16 *val, size_t nval)
{
	return fwyesde_property_read_int_array(fwyesde, propname, sizeof(u16),
					      val, nval);
}
EXPORT_SYMBOL_GPL(fwyesde_property_read_u16_array);

/**
 * fwyesde_property_read_u32_array - return a u32 array property of firmware yesde
 * @fwyesde: Firmware yesde to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u32 properties with @propname from @fwyesde store them to
 * @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO if the property is yest an array of numbers,
 *	   %-EOVERFLOW if the size of the property is yest as expected,
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int fwyesde_property_read_u32_array(const struct fwyesde_handle *fwyesde,
				   const char *propname, u32 *val, size_t nval)
{
	return fwyesde_property_read_int_array(fwyesde, propname, sizeof(u32),
					      val, nval);
}
EXPORT_SYMBOL_GPL(fwyesde_property_read_u32_array);

/**
 * fwyesde_property_read_u64_array - return a u64 array property firmware yesde
 * @fwyesde: Firmware yesde to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u64 properties with @propname from @fwyesde and store them to
 * @val if found.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO if the property is yest an array of numbers,
 *	   %-EOVERFLOW if the size of the property is yest as expected,
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int fwyesde_property_read_u64_array(const struct fwyesde_handle *fwyesde,
				   const char *propname, u64 *val, size_t nval)
{
	return fwyesde_property_read_int_array(fwyesde, propname, sizeof(u64),
					      val, nval);
}
EXPORT_SYMBOL_GPL(fwyesde_property_read_u64_array);

/**
 * fwyesde_property_read_string_array - return string array property of a yesde
 * @fwyesde: Firmware yesde to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an string list property @propname from the given firmware yesde and store
 * them to @val if found.
 *
 * Return: number of values read on success if @val is yesn-NULL,
 *	   number of values available on success if @val is NULL,
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO or %-EILSEQ if the property is yest an array of strings,
 *	   %-EOVERFLOW if the size of the property is yest as expected,
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int fwyesde_property_read_string_array(const struct fwyesde_handle *fwyesde,
				      const char *propname, const char **val,
				      size_t nval)
{
	int ret;

	ret = fwyesde_call_int_op(fwyesde, property_read_string_array, propname,
				 val, nval);
	if (ret == -EINVAL && !IS_ERR_OR_NULL(fwyesde) &&
	    !IS_ERR_OR_NULL(fwyesde->secondary))
		ret = fwyesde_call_int_op(fwyesde->secondary,
					 property_read_string_array, propname,
					 val, nval);
	return ret;
}
EXPORT_SYMBOL_GPL(fwyesde_property_read_string_array);

/**
 * fwyesde_property_read_string - return a string property of a firmware yesde
 * @fwyesde: Firmware yesde to get the property of
 * @propname: Name of the property
 * @val: The value is stored here
 *
 * Read property @propname from the given firmware yesde and store the value into
 * @val if found.  The value is checked to be a string.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO or %-EILSEQ if the property is yest a string,
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int fwyesde_property_read_string(const struct fwyesde_handle *fwyesde,
				const char *propname, const char **val)
{
	int ret = fwyesde_property_read_string_array(fwyesde, propname, val, 1);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(fwyesde_property_read_string);

/**
 * fwyesde_property_match_string - find a string in an array and return index
 * @fwyesde: Firmware yesde to get the property of
 * @propname: Name of the property holding the array
 * @string: String to look for
 *
 * Find a given string in a string array and if it is found return the
 * index back.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are yest valid,
 *	   %-ENODATA if the property does yest have a value,
 *	   %-EPROTO if the property is yest an array of strings,
 *	   %-ENXIO if yes suitable firmware interface is present.
 */
int fwyesde_property_match_string(const struct fwyesde_handle *fwyesde,
	const char *propname, const char *string)
{
	const char **values;
	int nval, ret;

	nval = fwyesde_property_read_string_array(fwyesde, propname, NULL, 0);
	if (nval < 0)
		return nval;

	if (nval == 0)
		return -ENODATA;

	values = kcalloc(nval, sizeof(*values), GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	ret = fwyesde_property_read_string_array(fwyesde, propname, values, nval);
	if (ret < 0)
		goto out;

	ret = match_string(values, nval, string);
	if (ret < 0)
		ret = -ENODATA;
out:
	kfree(values);
	return ret;
}
EXPORT_SYMBOL_GPL(fwyesde_property_match_string);

/**
 * fwyesde_property_get_reference_args() - Find a reference with arguments
 * @fwyesde:	Firmware yesde where to look for the reference
 * @prop:	The name of the property
 * @nargs_prop:	The name of the property telling the number of
 *		arguments in the referred yesde. NULL if @nargs is kyeswn,
 *		otherwise @nargs is igyesred. Only relevant on OF.
 * @nargs:	Number of arguments. Igyesred if @nargs_prop is yesn-NULL.
 * @index:	Index of the reference, from zero onwards.
 * @args:	Result structure with reference and integer arguments.
 *
 * Obtain a reference based on a named property in an fwyesde, with
 * integer arguments.
 *
 * Caller is responsible to call fwyesde_handle_put() on the returned
 * args->fwyesde pointer.
 *
 * Returns: %0 on success
 *	    %-ENOENT when the index is out of bounds, the index has an empty
 *		     reference or the property was yest found
 *	    %-EINVAL on parse error
 */
int fwyesde_property_get_reference_args(const struct fwyesde_handle *fwyesde,
				       const char *prop, const char *nargs_prop,
				       unsigned int nargs, unsigned int index,
				       struct fwyesde_reference_args *args)
{
	return fwyesde_call_int_op(fwyesde, get_reference_args, prop, nargs_prop,
				  nargs, index, args);
}
EXPORT_SYMBOL_GPL(fwyesde_property_get_reference_args);

/**
 * fwyesde_find_reference - Find named reference to a fwyesde_handle
 * @fwyesde: Firmware yesde where to look for the reference
 * @name: The name of the reference
 * @index: Index of the reference
 *
 * @index can be used when the named reference holds a table of references.
 *
 * Returns pointer to the reference fwyesde, or ERR_PTR. Caller is responsible to
 * call fwyesde_handle_put() on the returned fwyesde pointer.
 */
struct fwyesde_handle *fwyesde_find_reference(const struct fwyesde_handle *fwyesde,
					    const char *name,
					    unsigned int index)
{
	struct fwyesde_reference_args args;
	int ret;

	ret = fwyesde_property_get_reference_args(fwyesde, name, NULL, 0, index,
						 &args);
	return ret ? ERR_PTR(ret) : args.fwyesde;
}
EXPORT_SYMBOL_GPL(fwyesde_find_reference);

/**
 * device_remove_properties - Remove properties from a device object.
 * @dev: Device whose properties to remove.
 *
 * The function removes properties previously associated to the device
 * firmware yesde with device_add_properties(). Memory allocated to the
 * properties will also be released.
 */
void device_remove_properties(struct device *dev)
{
	struct fwyesde_handle *fwyesde = dev_fwyesde(dev);

	if (!fwyesde)
		return;

	if (is_software_yesde(fwyesde->secondary)) {
		fwyesde_remove_software_yesde(fwyesde->secondary);
		set_secondary_fwyesde(dev, NULL);
	}
}
EXPORT_SYMBOL_GPL(device_remove_properties);

/**
 * device_add_properties - Add a collection of properties to a device object.
 * @dev: Device to add properties to.
 * @properties: Collection of properties to add.
 *
 * Associate a collection of device properties represented by @properties with
 * @dev. The function takes a copy of @properties.
 *
 * WARNING: The callers should yest use this function if it is kyeswn that there
 * is yes real firmware yesde associated with @dev! In that case the callers
 * should create a software yesde and assign it to @dev directly.
 */
int device_add_properties(struct device *dev,
			  const struct property_entry *properties)
{
	struct fwyesde_handle *fwyesde;

	fwyesde = fwyesde_create_software_yesde(properties, NULL);
	if (IS_ERR(fwyesde))
		return PTR_ERR(fwyesde);

	set_secondary_fwyesde(dev, fwyesde);
	return 0;
}
EXPORT_SYMBOL_GPL(device_add_properties);

/**
 * fwyesde_get_name - Return the name of a yesde
 * @fwyesde: The firmware yesde
 *
 * Returns a pointer to the yesde name.
 */
const char *fwyesde_get_name(const struct fwyesde_handle *fwyesde)
{
	return fwyesde_call_ptr_op(fwyesde, get_name);
}

/**
 * fwyesde_get_name_prefix - Return the prefix of yesde for printing purposes
 * @fwyesde: The firmware yesde
 *
 * Returns the prefix of a yesde, intended to be printed right before the yesde.
 * The prefix works also as a separator between the yesdes.
 */
const char *fwyesde_get_name_prefix(const struct fwyesde_handle *fwyesde)
{
	return fwyesde_call_ptr_op(fwyesde, get_name_prefix);
}

/**
 * fwyesde_get_parent - Return parent firwmare yesde
 * @fwyesde: Firmware whose parent is retrieved
 *
 * Return parent firmware yesde of the given yesde if possible or %NULL if yes
 * parent was available.
 */
struct fwyesde_handle *fwyesde_get_parent(const struct fwyesde_handle *fwyesde)
{
	return fwyesde_call_ptr_op(fwyesde, get_parent);
}
EXPORT_SYMBOL_GPL(fwyesde_get_parent);

/**
 * fwyesde_get_next_parent - Iterate to the yesde's parent
 * @fwyesde: Firmware whose parent is retrieved
 *
 * This is like fwyesde_get_parent() except that it drops the refcount
 * on the passed yesde, making it suitable for iterating through a
 * yesde's parents.
 *
 * Returns a yesde pointer with refcount incremented, use
 * fwyesde_handle_yesde() on it when done.
 */
struct fwyesde_handle *fwyesde_get_next_parent(struct fwyesde_handle *fwyesde)
{
	struct fwyesde_handle *parent = fwyesde_get_parent(fwyesde);

	fwyesde_handle_put(fwyesde);

	return parent;
}
EXPORT_SYMBOL_GPL(fwyesde_get_next_parent);

/**
 * fwyesde_count_parents - Return the number of parents a yesde has
 * @fwyesde: The yesde the parents of which are to be counted
 *
 * Returns the number of parents a yesde has.
 */
unsigned int fwyesde_count_parents(const struct fwyesde_handle *fwyesde)
{
	struct fwyesde_handle *__fwyesde;
	unsigned int count;

	__fwyesde = fwyesde_get_parent(fwyesde);

	for (count = 0; __fwyesde; count++)
		__fwyesde = fwyesde_get_next_parent(__fwyesde);

	return count;
}
EXPORT_SYMBOL_GPL(fwyesde_count_parents);

/**
 * fwyesde_get_nth_parent - Return an nth parent of a yesde
 * @fwyesde: The yesde the parent of which is requested
 * @depth: Distance of the parent from the yesde
 *
 * Returns the nth parent of a yesde. If there is yes parent at the requested
 * @depth, %NULL is returned. If @depth is 0, the functionality is equivalent to
 * fwyesde_handle_get(). For @depth == 1, it is fwyesde_get_parent() and so on.
 *
 * The caller is responsible for calling fwyesde_handle_put() for the returned
 * yesde.
 */
struct fwyesde_handle *fwyesde_get_nth_parent(struct fwyesde_handle *fwyesde,
					    unsigned int depth)
{
	unsigned int i;

	fwyesde_handle_get(fwyesde);

	for (i = 0; i < depth && fwyesde; i++)
		fwyesde = fwyesde_get_next_parent(fwyesde);

	return fwyesde;
}
EXPORT_SYMBOL_GPL(fwyesde_get_nth_parent);

/**
 * fwyesde_get_next_child_yesde - Return the next child yesde handle for a yesde
 * @fwyesde: Firmware yesde to find the next child yesde for.
 * @child: Handle to one of the yesde's child yesdes or a %NULL handle.
 */
struct fwyesde_handle *
fwyesde_get_next_child_yesde(const struct fwyesde_handle *fwyesde,
			   struct fwyesde_handle *child)
{
	return fwyesde_call_ptr_op(fwyesde, get_next_child_yesde, child);
}
EXPORT_SYMBOL_GPL(fwyesde_get_next_child_yesde);

/**
 * fwyesde_get_next_available_child_yesde - Return the next
 * available child yesde handle for a yesde
 * @fwyesde: Firmware yesde to find the next child yesde for.
 * @child: Handle to one of the yesde's child yesdes or a %NULL handle.
 */
struct fwyesde_handle *
fwyesde_get_next_available_child_yesde(const struct fwyesde_handle *fwyesde,
				     struct fwyesde_handle *child)
{
	struct fwyesde_handle *next_child = child;

	if (!fwyesde)
		return NULL;

	do {
		next_child = fwyesde_get_next_child_yesde(fwyesde, next_child);

		if (!next_child || fwyesde_device_is_available(next_child))
			break;
	} while (next_child);

	return next_child;
}
EXPORT_SYMBOL_GPL(fwyesde_get_next_available_child_yesde);

/**
 * device_get_next_child_yesde - Return the next child yesde handle for a device
 * @dev: Device to find the next child yesde for.
 * @child: Handle to one of the device's child yesdes or a null handle.
 */
struct fwyesde_handle *device_get_next_child_yesde(struct device *dev,
						 struct fwyesde_handle *child)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct fwyesde_handle *fwyesde = NULL;

	if (dev->of_yesde)
		fwyesde = &dev->of_yesde->fwyesde;
	else if (adev)
		fwyesde = acpi_fwyesde_handle(adev);

	return fwyesde_get_next_child_yesde(fwyesde, child);
}
EXPORT_SYMBOL_GPL(device_get_next_child_yesde);

/**
 * fwyesde_get_named_child_yesde - Return first matching named child yesde handle
 * @fwyesde: Firmware yesde to find the named child yesde for.
 * @childname: String to match child yesde name against.
 */
struct fwyesde_handle *
fwyesde_get_named_child_yesde(const struct fwyesde_handle *fwyesde,
			    const char *childname)
{
	return fwyesde_call_ptr_op(fwyesde, get_named_child_yesde, childname);
}
EXPORT_SYMBOL_GPL(fwyesde_get_named_child_yesde);

/**
 * device_get_named_child_yesde - Return first matching named child yesde handle
 * @dev: Device to find the named child yesde for.
 * @childname: String to match child yesde name against.
 */
struct fwyesde_handle *device_get_named_child_yesde(struct device *dev,
						  const char *childname)
{
	return fwyesde_get_named_child_yesde(dev_fwyesde(dev), childname);
}
EXPORT_SYMBOL_GPL(device_get_named_child_yesde);

/**
 * fwyesde_handle_get - Obtain a reference to a device yesde
 * @fwyesde: Pointer to the device yesde to obtain the reference to.
 *
 * Returns the fwyesde handle.
 */
struct fwyesde_handle *fwyesde_handle_get(struct fwyesde_handle *fwyesde)
{
	if (!fwyesde_has_op(fwyesde, get))
		return fwyesde;

	return fwyesde_call_ptr_op(fwyesde, get);
}
EXPORT_SYMBOL_GPL(fwyesde_handle_get);

/**
 * fwyesde_handle_put - Drop reference to a device yesde
 * @fwyesde: Pointer to the device yesde to drop the reference to.
 *
 * This has to be used when terminating device_for_each_child_yesde() iteration
 * with break or return to prevent stale device yesde references from being left
 * behind.
 */
void fwyesde_handle_put(struct fwyesde_handle *fwyesde)
{
	fwyesde_call_void_op(fwyesde, put);
}
EXPORT_SYMBOL_GPL(fwyesde_handle_put);

/**
 * fwyesde_device_is_available - check if a device is available for use
 * @fwyesde: Pointer to the fwyesde of the device.
 */
bool fwyesde_device_is_available(const struct fwyesde_handle *fwyesde)
{
	return fwyesde_call_bool_op(fwyesde, device_is_available);
}
EXPORT_SYMBOL_GPL(fwyesde_device_is_available);

/**
 * device_get_child_yesde_count - return the number of child yesdes for device
 * @dev: Device to cound the child yesdes for
 */
unsigned int device_get_child_yesde_count(struct device *dev)
{
	struct fwyesde_handle *child;
	unsigned int count = 0;

	device_for_each_child_yesde(dev, child)
		count++;

	return count;
}
EXPORT_SYMBOL_GPL(device_get_child_yesde_count);

bool device_dma_supported(struct device *dev)
{
	/* For DT, this is always supported.
	 * For ACPI, this depends on CCA, which
	 * is determined by the acpi_dma_supported().
	 */
	if (IS_ENABLED(CONFIG_OF) && dev->of_yesde)
		return true;

	return acpi_dma_supported(ACPI_COMPANION(dev));
}
EXPORT_SYMBOL_GPL(device_dma_supported);

enum dev_dma_attr device_get_dma_attr(struct device *dev)
{
	enum dev_dma_attr attr = DEV_DMA_NOT_SUPPORTED;

	if (IS_ENABLED(CONFIG_OF) && dev->of_yesde) {
		if (of_dma_is_coherent(dev->of_yesde))
			attr = DEV_DMA_COHERENT;
		else
			attr = DEV_DMA_NON_COHERENT;
	} else
		attr = acpi_get_dma_attr(ACPI_COMPANION(dev));

	return attr;
}
EXPORT_SYMBOL_GPL(device_get_dma_attr);

/**
 * fwyesde_get_phy_mode - Get phy mode for given firmware yesde
 * @fwyesde:	Pointer to the given yesde
 *
 * The function gets phy interface string from property 'phy-mode' or
 * 'phy-connection-type', and return its index in phy_modes table, or erryes in
 * error case.
 */
int fwyesde_get_phy_mode(struct fwyesde_handle *fwyesde)
{
	const char *pm;
	int err, i;

	err = fwyesde_property_read_string(fwyesde, "phy-mode", &pm);
	if (err < 0)
		err = fwyesde_property_read_string(fwyesde,
						  "phy-connection-type", &pm);
	if (err < 0)
		return err;

	for (i = 0; i < PHY_INTERFACE_MODE_MAX; i++)
		if (!strcasecmp(pm, phy_modes(i)))
			return i;

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(fwyesde_get_phy_mode);

/**
 * device_get_phy_mode - Get phy mode for given device
 * @dev:	Pointer to the given device
 *
 * The function gets phy interface string from property 'phy-mode' or
 * 'phy-connection-type', and return its index in phy_modes table, or erryes in
 * error case.
 */
int device_get_phy_mode(struct device *dev)
{
	return fwyesde_get_phy_mode(dev_fwyesde(dev));
}
EXPORT_SYMBOL_GPL(device_get_phy_mode);

static void *fwyesde_get_mac_addr(struct fwyesde_handle *fwyesde,
				 const char *name, char *addr,
				 int alen)
{
	int ret = fwyesde_property_read_u8_array(fwyesde, name, addr, alen);

	if (ret == 0 && alen == ETH_ALEN && is_valid_ether_addr(addr))
		return addr;
	return NULL;
}

/**
 * fwyesde_get_mac_address - Get the MAC from the firmware yesde
 * @fwyesde:	Pointer to the firmware yesde
 * @addr:	Address of buffer to store the MAC in
 * @alen:	Length of the buffer pointed to by addr, should be ETH_ALEN
 *
 * Search the firmware yesde for the best MAC address to use.  'mac-address' is
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
 * exist in the firmware tables, but were yest updated by the firmware.  For
 * example, the DTS could define 'mac-address' and 'local-mac-address', with
 * zero MAC addresses.  Some older U-Boots only initialized 'local-mac-address'.
 * In this case, the real MAC is in 'local-mac-address', and 'mac-address'
 * exists but is all zeros.
*/
void *fwyesde_get_mac_address(struct fwyesde_handle *fwyesde, char *addr, int alen)
{
	char *res;

	res = fwyesde_get_mac_addr(fwyesde, "mac-address", addr, alen);
	if (res)
		return res;

	res = fwyesde_get_mac_addr(fwyesde, "local-mac-address", addr, alen);
	if (res)
		return res;

	return fwyesde_get_mac_addr(fwyesde, "address", addr, alen);
}
EXPORT_SYMBOL(fwyesde_get_mac_address);

/**
 * device_get_mac_address - Get the MAC for a given device
 * @dev:	Pointer to the device
 * @addr:	Address of buffer to store the MAC in
 * @alen:	Length of the buffer pointed to by addr, should be ETH_ALEN
 */
void *device_get_mac_address(struct device *dev, char *addr, int alen)
{
	return fwyesde_get_mac_address(dev_fwyesde(dev), addr, alen);
}
EXPORT_SYMBOL(device_get_mac_address);

/**
 * fwyesde_irq_get - Get IRQ directly from a fwyesde
 * @fwyesde:	Pointer to the firmware yesde
 * @index:	Zero-based index of the IRQ
 *
 * Returns Linux IRQ number on success. Other values are determined
 * accordingly to acpi_/of_ irq_get() operation.
 */
int fwyesde_irq_get(struct fwyesde_handle *fwyesde, unsigned int index)
{
	struct device_yesde *of_yesde = to_of_yesde(fwyesde);
	struct resource res;
	int ret;

	if (IS_ENABLED(CONFIG_OF) && of_yesde)
		return of_irq_get(of_yesde, index);

	ret = acpi_irq_get(ACPI_HANDLE_FWNODE(fwyesde), index, &res);
	if (ret)
		return ret;

	return res.start;
}
EXPORT_SYMBOL(fwyesde_irq_get);

/**
 * fwyesde_graph_get_next_endpoint - Get next endpoint firmware yesde
 * @fwyesde: Pointer to the parent firmware yesde
 * @prev: Previous endpoint yesde or %NULL to get the first
 *
 * Returns an endpoint firmware yesde pointer or %NULL if yes more endpoints
 * are available.
 */
struct fwyesde_handle *
fwyesde_graph_get_next_endpoint(const struct fwyesde_handle *fwyesde,
			       struct fwyesde_handle *prev)
{
	return fwyesde_call_ptr_op(fwyesde, graph_get_next_endpoint, prev);
}
EXPORT_SYMBOL_GPL(fwyesde_graph_get_next_endpoint);

/**
 * fwyesde_graph_get_port_parent - Return the device fwyesde of a port endpoint
 * @endpoint: Endpoint firmware yesde of the port
 *
 * Return: the firmware yesde of the device the @endpoint belongs to.
 */
struct fwyesde_handle *
fwyesde_graph_get_port_parent(const struct fwyesde_handle *endpoint)
{
	struct fwyesde_handle *port, *parent;

	port = fwyesde_get_parent(endpoint);
	parent = fwyesde_call_ptr_op(port, graph_get_port_parent);

	fwyesde_handle_put(port);

	return parent;
}
EXPORT_SYMBOL_GPL(fwyesde_graph_get_port_parent);

/**
 * fwyesde_graph_get_remote_port_parent - Return fwyesde of a remote device
 * @fwyesde: Endpoint firmware yesde pointing to the remote endpoint
 *
 * Extracts firmware yesde of a remote device the @fwyesde points to.
 */
struct fwyesde_handle *
fwyesde_graph_get_remote_port_parent(const struct fwyesde_handle *fwyesde)
{
	struct fwyesde_handle *endpoint, *parent;

	endpoint = fwyesde_graph_get_remote_endpoint(fwyesde);
	parent = fwyesde_graph_get_port_parent(endpoint);

	fwyesde_handle_put(endpoint);

	return parent;
}
EXPORT_SYMBOL_GPL(fwyesde_graph_get_remote_port_parent);

/**
 * fwyesde_graph_get_remote_port - Return fwyesde of a remote port
 * @fwyesde: Endpoint firmware yesde pointing to the remote endpoint
 *
 * Extracts firmware yesde of a remote port the @fwyesde points to.
 */
struct fwyesde_handle *
fwyesde_graph_get_remote_port(const struct fwyesde_handle *fwyesde)
{
	return fwyesde_get_next_parent(fwyesde_graph_get_remote_endpoint(fwyesde));
}
EXPORT_SYMBOL_GPL(fwyesde_graph_get_remote_port);

/**
 * fwyesde_graph_get_remote_endpoint - Return fwyesde of a remote endpoint
 * @fwyesde: Endpoint firmware yesde pointing to the remote endpoint
 *
 * Extracts firmware yesde of a remote endpoint the @fwyesde points to.
 */
struct fwyesde_handle *
fwyesde_graph_get_remote_endpoint(const struct fwyesde_handle *fwyesde)
{
	return fwyesde_call_ptr_op(fwyesde, graph_get_remote_endpoint);
}
EXPORT_SYMBOL_GPL(fwyesde_graph_get_remote_endpoint);

/**
 * fwyesde_graph_get_remote_yesde - get remote parent yesde for given port/endpoint
 * @fwyesde: pointer to parent fwyesde_handle containing graph port/endpoint
 * @port_id: identifier of the parent port yesde
 * @endpoint_id: identifier of the endpoint yesde
 *
 * Return: Remote fwyesde handle associated with remote endpoint yesde linked
 *	   to @yesde. Use fwyesde_yesde_put() on it when done.
 */
struct fwyesde_handle *
fwyesde_graph_get_remote_yesde(const struct fwyesde_handle *fwyesde, u32 port_id,
			     u32 endpoint_id)
{
	struct fwyesde_handle *endpoint = NULL;

	while ((endpoint = fwyesde_graph_get_next_endpoint(fwyesde, endpoint))) {
		struct fwyesde_endpoint fwyesde_ep;
		struct fwyesde_handle *remote;
		int ret;

		ret = fwyesde_graph_parse_endpoint(endpoint, &fwyesde_ep);
		if (ret < 0)
			continue;

		if (fwyesde_ep.port != port_id || fwyesde_ep.id != endpoint_id)
			continue;

		remote = fwyesde_graph_get_remote_port_parent(endpoint);
		if (!remote)
			return NULL;

		return fwyesde_device_is_available(remote) ? remote : NULL;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(fwyesde_graph_get_remote_yesde);

/**
 * fwyesde_graph_get_endpoint_by_id - get endpoint by port and endpoint numbers
 * @fwyesde: parent fwyesde_handle containing the graph
 * @port: identifier of the port yesde
 * @endpoint: identifier of the endpoint yesde under the port yesde
 * @flags: fwyesde lookup flags
 *
 * Return the fwyesde handle of the local endpoint corresponding the port and
 * endpoint IDs or NULL if yest found.
 *
 * If FWNODE_GRAPH_ENDPOINT_NEXT is passed in @flags and the specified endpoint
 * has yest been found, look for the closest endpoint ID greater than the
 * specified one and return the endpoint that corresponds to it, if present.
 *
 * Do yest return endpoints that belong to disabled devices, unless
 * FWNODE_GRAPH_DEVICE_DISABLED is passed in @flags.
 *
 * The returned endpoint needs to be released by calling fwyesde_handle_put() on
 * it when it is yest needed any more.
 */
struct fwyesde_handle *
fwyesde_graph_get_endpoint_by_id(const struct fwyesde_handle *fwyesde,
				u32 port, u32 endpoint, unsigned long flags)
{
	struct fwyesde_handle *ep = NULL, *best_ep = NULL;
	unsigned int best_ep_id = 0;
	bool endpoint_next = flags & FWNODE_GRAPH_ENDPOINT_NEXT;
	bool enabled_only = !(flags & FWNODE_GRAPH_DEVICE_DISABLED);

	while ((ep = fwyesde_graph_get_next_endpoint(fwyesde, ep))) {
		struct fwyesde_endpoint fwyesde_ep = { 0 };
		int ret;

		if (enabled_only) {
			struct fwyesde_handle *dev_yesde;
			bool available;

			dev_yesde = fwyesde_graph_get_remote_port_parent(ep);
			available = fwyesde_device_is_available(dev_yesde);
			fwyesde_handle_put(dev_yesde);
			if (!available)
				continue;
		}

		ret = fwyesde_graph_parse_endpoint(ep, &fwyesde_ep);
		if (ret < 0)
			continue;

		if (fwyesde_ep.port != port)
			continue;

		if (fwyesde_ep.id == endpoint)
			return ep;

		if (!endpoint_next)
			continue;

		/*
		 * If the endpoint that has just been found is yest the first
		 * matching one and the ID of the one found previously is closer
		 * to the requested endpoint ID, skip it.
		 */
		if (fwyesde_ep.id < endpoint ||
		    (best_ep && best_ep_id < fwyesde_ep.id))
			continue;

		fwyesde_handle_put(best_ep);
		best_ep = fwyesde_handle_get(ep);
		best_ep_id = fwyesde_ep.id;
	}

	return best_ep;
}
EXPORT_SYMBOL_GPL(fwyesde_graph_get_endpoint_by_id);

/**
 * fwyesde_graph_parse_endpoint - parse common endpoint yesde properties
 * @fwyesde: pointer to endpoint fwyesde_handle
 * @endpoint: pointer to the fwyesde endpoint data structure
 *
 * Parse @fwyesde representing a graph endpoint yesde and store the
 * information in @endpoint. The caller must hold a reference to
 * @fwyesde.
 */
int fwyesde_graph_parse_endpoint(const struct fwyesde_handle *fwyesde,
				struct fwyesde_endpoint *endpoint)
{
	memset(endpoint, 0, sizeof(*endpoint));

	return fwyesde_call_int_op(fwyesde, graph_parse_endpoint, endpoint);
}
EXPORT_SYMBOL(fwyesde_graph_parse_endpoint);

const void *device_get_match_data(struct device *dev)
{
	return fwyesde_call_ptr_op(dev_fwyesde(dev), device_get_match_data, dev);
}
EXPORT_SYMBOL_GPL(device_get_match_data);
