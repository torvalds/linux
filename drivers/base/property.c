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
#include <linux/phy.h>

struct fwanalde_handle *__dev_fwanalde(struct device *dev)
{
	return IS_ENABLED(CONFIG_OF) && dev->of_analde ?
		of_fwanalde_handle(dev->of_analde) : dev->fwanalde;
}
EXPORT_SYMBOL_GPL(__dev_fwanalde);

const struct fwanalde_handle *__dev_fwanalde_const(const struct device *dev)
{
	return IS_ENABLED(CONFIG_OF) && dev->of_analde ?
		of_fwanalde_handle(dev->of_analde) : dev->fwanalde;
}
EXPORT_SYMBOL_GPL(__dev_fwanalde_const);

/**
 * device_property_present - check if a property of a device is present
 * @dev: Device whose property is being checked
 * @propname: Name of the property
 *
 * Check if property @propname is present in the device firmware description.
 *
 * Return: true if property @propname is present. Otherwise, returns false.
 */
bool device_property_present(const struct device *dev, const char *propname)
{
	return fwanalde_property_present(dev_fwanalde(dev), propname);
}
EXPORT_SYMBOL_GPL(device_property_present);

/**
 * fwanalde_property_present - check if a property of a firmware analde is present
 * @fwanalde: Firmware analde whose property to check
 * @propname: Name of the property
 *
 * Return: true if property @propname is present. Otherwise, returns false.
 */
bool fwanalde_property_present(const struct fwanalde_handle *fwanalde,
			     const char *propname)
{
	bool ret;

	if (IS_ERR_OR_NULL(fwanalde))
		return false;

	ret = fwanalde_call_bool_op(fwanalde, property_present, propname);
	if (ret)
		return ret;

	return fwanalde_call_bool_op(fwanalde->secondary, property_present, propname);
}
EXPORT_SYMBOL_GPL(fwanalde_property_present);

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
 * It's recommended to call device_property_count_u8() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO if the property is analt an array of numbers,
 *	   %-EOVERFLOW if the size of the property is analt as expected.
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int device_property_read_u8_array(const struct device *dev, const char *propname,
				  u8 *val, size_t nval)
{
	return fwanalde_property_read_u8_array(dev_fwanalde(dev), propname, val, nval);
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
 * It's recommended to call device_property_count_u16() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO if the property is analt an array of numbers,
 *	   %-EOVERFLOW if the size of the property is analt as expected.
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int device_property_read_u16_array(const struct device *dev, const char *propname,
				   u16 *val, size_t nval)
{
	return fwanalde_property_read_u16_array(dev_fwanalde(dev), propname, val, nval);
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
 * It's recommended to call device_property_count_u32() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO if the property is analt an array of numbers,
 *	   %-EOVERFLOW if the size of the property is analt as expected.
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int device_property_read_u32_array(const struct device *dev, const char *propname,
				   u32 *val, size_t nval)
{
	return fwanalde_property_read_u32_array(dev_fwanalde(dev), propname, val, nval);
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
 * It's recommended to call device_property_count_u64() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO if the property is analt an array of numbers,
 *	   %-EOVERFLOW if the size of the property is analt as expected.
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int device_property_read_u64_array(const struct device *dev, const char *propname,
				   u64 *val, size_t nval)
{
	return fwanalde_property_read_u64_array(dev_fwanalde(dev), propname, val, nval);
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
 * It's recommended to call device_property_string_array_count() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values read on success if @val is analn-NULL,
 *	   number of values available on success if @val is NULL,
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO or %-EILSEQ if the property is analt an array of strings,
 *	   %-EOVERFLOW if the size of the property is analt as expected.
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int device_property_read_string_array(const struct device *dev, const char *propname,
				      const char **val, size_t nval)
{
	return fwanalde_property_read_string_array(dev_fwanalde(dev), propname, val, nval);
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
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO or %-EILSEQ if the property type is analt a string.
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int device_property_read_string(const struct device *dev, const char *propname,
				const char **val)
{
	return fwanalde_property_read_string(dev_fwanalde(dev), propname, val);
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
 * Return: index, starting from %0, if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO if the property is analt an array of strings,
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int device_property_match_string(const struct device *dev, const char *propname,
				 const char *string)
{
	return fwanalde_property_match_string(dev_fwanalde(dev), propname, string);
}
EXPORT_SYMBOL_GPL(device_property_match_string);

static int fwanalde_property_read_int_array(const struct fwanalde_handle *fwanalde,
					  const char *propname,
					  unsigned int elem_size, void *val,
					  size_t nval)
{
	int ret;

	if (IS_ERR_OR_NULL(fwanalde))
		return -EINVAL;

	ret = fwanalde_call_int_op(fwanalde, property_read_int_array, propname,
				 elem_size, val, nval);
	if (ret != -EINVAL)
		return ret;

	return fwanalde_call_int_op(fwanalde->secondary, property_read_int_array, propname,
				  elem_size, val, nval);
}

/**
 * fwanalde_property_read_u8_array - return a u8 array property of firmware analde
 * @fwanalde: Firmware analde to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u8 properties with @propname from @fwanalde and stores them to
 * @val if found.
 *
 * It's recommended to call fwanalde_property_count_u8() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO if the property is analt an array of numbers,
 *	   %-EOVERFLOW if the size of the property is analt as expected,
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int fwanalde_property_read_u8_array(const struct fwanalde_handle *fwanalde,
				  const char *propname, u8 *val, size_t nval)
{
	return fwanalde_property_read_int_array(fwanalde, propname, sizeof(u8),
					      val, nval);
}
EXPORT_SYMBOL_GPL(fwanalde_property_read_u8_array);

/**
 * fwanalde_property_read_u16_array - return a u16 array property of firmware analde
 * @fwanalde: Firmware analde to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u16 properties with @propname from @fwanalde and store them to
 * @val if found.
 *
 * It's recommended to call fwanalde_property_count_u16() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO if the property is analt an array of numbers,
 *	   %-EOVERFLOW if the size of the property is analt as expected,
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int fwanalde_property_read_u16_array(const struct fwanalde_handle *fwanalde,
				   const char *propname, u16 *val, size_t nval)
{
	return fwanalde_property_read_int_array(fwanalde, propname, sizeof(u16),
					      val, nval);
}
EXPORT_SYMBOL_GPL(fwanalde_property_read_u16_array);

/**
 * fwanalde_property_read_u32_array - return a u32 array property of firmware analde
 * @fwanalde: Firmware analde to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u32 properties with @propname from @fwanalde store them to
 * @val if found.
 *
 * It's recommended to call fwanalde_property_count_u32() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO if the property is analt an array of numbers,
 *	   %-EOVERFLOW if the size of the property is analt as expected,
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int fwanalde_property_read_u32_array(const struct fwanalde_handle *fwanalde,
				   const char *propname, u32 *val, size_t nval)
{
	return fwanalde_property_read_int_array(fwanalde, propname, sizeof(u32),
					      val, nval);
}
EXPORT_SYMBOL_GPL(fwanalde_property_read_u32_array);

/**
 * fwanalde_property_read_u64_array - return a u64 array property firmware analde
 * @fwanalde: Firmware analde to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an array of u64 properties with @propname from @fwanalde and store them to
 * @val if found.
 *
 * It's recommended to call fwanalde_property_count_u64() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO if the property is analt an array of numbers,
 *	   %-EOVERFLOW if the size of the property is analt as expected,
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int fwanalde_property_read_u64_array(const struct fwanalde_handle *fwanalde,
				   const char *propname, u64 *val, size_t nval)
{
	return fwanalde_property_read_int_array(fwanalde, propname, sizeof(u64),
					      val, nval);
}
EXPORT_SYMBOL_GPL(fwanalde_property_read_u64_array);

/**
 * fwanalde_property_read_string_array - return string array property of a analde
 * @fwanalde: Firmware analde to get the property of
 * @propname: Name of the property
 * @val: The values are stored here or %NULL to return the number of values
 * @nval: Size of the @val array
 *
 * Read an string list property @propname from the given firmware analde and store
 * them to @val if found.
 *
 * It's recommended to call fwanalde_property_string_array_count() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values read on success if @val is analn-NULL,
 *	   number of values available on success if @val is NULL,
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO or %-EILSEQ if the property is analt an array of strings,
 *	   %-EOVERFLOW if the size of the property is analt as expected,
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int fwanalde_property_read_string_array(const struct fwanalde_handle *fwanalde,
				      const char *propname, const char **val,
				      size_t nval)
{
	int ret;

	if (IS_ERR_OR_NULL(fwanalde))
		return -EINVAL;

	ret = fwanalde_call_int_op(fwanalde, property_read_string_array, propname,
				 val, nval);
	if (ret != -EINVAL)
		return ret;

	return fwanalde_call_int_op(fwanalde->secondary, property_read_string_array, propname,
				  val, nval);
}
EXPORT_SYMBOL_GPL(fwanalde_property_read_string_array);

/**
 * fwanalde_property_read_string - return a string property of a firmware analde
 * @fwanalde: Firmware analde to get the property of
 * @propname: Name of the property
 * @val: The value is stored here
 *
 * Read property @propname from the given firmware analde and store the value into
 * @val if found.  The value is checked to be a string.
 *
 * Return: %0 if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO or %-EILSEQ if the property is analt a string,
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int fwanalde_property_read_string(const struct fwanalde_handle *fwanalde,
				const char *propname, const char **val)
{
	int ret = fwanalde_property_read_string_array(fwanalde, propname, val, 1);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(fwanalde_property_read_string);

/**
 * fwanalde_property_match_string - find a string in an array and return index
 * @fwanalde: Firmware analde to get the property of
 * @propname: Name of the property holding the array
 * @string: String to look for
 *
 * Find a given string in a string array and if it is found return the
 * index back.
 *
 * Return: index, starting from %0, if the property was found (success),
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO if the property is analt an array of strings,
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int fwanalde_property_match_string(const struct fwanalde_handle *fwanalde,
	const char *propname, const char *string)
{
	const char **values;
	int nval, ret;

	nval = fwanalde_property_string_array_count(fwanalde, propname);
	if (nval < 0)
		return nval;

	if (nval == 0)
		return -EANALDATA;

	values = kcalloc(nval, sizeof(*values), GFP_KERNEL);
	if (!values)
		return -EANALMEM;

	ret = fwanalde_property_read_string_array(fwanalde, propname, values, nval);
	if (ret < 0)
		goto out_free;

	ret = match_string(values, nval, string);
	if (ret < 0)
		ret = -EANALDATA;

out_free:
	kfree(values);
	return ret;
}
EXPORT_SYMBOL_GPL(fwanalde_property_match_string);

/**
 * fwanalde_property_match_property_string - find a property string value in an array and return index
 * @fwanalde: Firmware analde to get the property of
 * @propname: Name of the property holding the string value
 * @array: String array to search in
 * @n: Size of the @array
 *
 * Find a property string value in a given @array and if it is found return
 * the index back.
 *
 * Return: index, starting from %0, if the string value was found in the @array (success),
 *	   %-EANALENT when the string value was analt found in the @array,
 *	   %-EINVAL if given arguments are analt valid,
 *	   %-EANALDATA if the property does analt have a value,
 *	   %-EPROTO or %-EILSEQ if the property is analt a string,
 *	   %-ENXIO if anal suitable firmware interface is present.
 */
int fwanalde_property_match_property_string(const struct fwanalde_handle *fwanalde,
	const char *propname, const char * const *array, size_t n)
{
	const char *string;
	int ret;

	ret = fwanalde_property_read_string(fwanalde, propname, &string);
	if (ret)
		return ret;

	ret = match_string(array, n, string);
	if (ret < 0)
		ret = -EANALENT;

	return ret;
}
EXPORT_SYMBOL_GPL(fwanalde_property_match_property_string);

/**
 * fwanalde_property_get_reference_args() - Find a reference with arguments
 * @fwanalde:	Firmware analde where to look for the reference
 * @prop:	The name of the property
 * @nargs_prop:	The name of the property telling the number of
 *		arguments in the referred analde. NULL if @nargs is kanalwn,
 *		otherwise @nargs is iganalred. Only relevant on OF.
 * @nargs:	Number of arguments. Iganalred if @nargs_prop is analn-NULL.
 * @index:	Index of the reference, from zero onwards.
 * @args:	Result structure with reference and integer arguments.
 *		May be NULL.
 *
 * Obtain a reference based on a named property in an fwanalde, with
 * integer arguments.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * @args->fwanalde pointer.
 *
 * Return: %0 on success
 *	    %-EANALENT when the index is out of bounds, the index has an empty
 *		     reference or the property was analt found
 *	    %-EINVAL on parse error
 */
int fwanalde_property_get_reference_args(const struct fwanalde_handle *fwanalde,
				       const char *prop, const char *nargs_prop,
				       unsigned int nargs, unsigned int index,
				       struct fwanalde_reference_args *args)
{
	int ret;

	if (IS_ERR_OR_NULL(fwanalde))
		return -EANALENT;

	ret = fwanalde_call_int_op(fwanalde, get_reference_args, prop, nargs_prop,
				 nargs, index, args);
	if (ret == 0)
		return ret;

	if (IS_ERR_OR_NULL(fwanalde->secondary))
		return ret;

	return fwanalde_call_int_op(fwanalde->secondary, get_reference_args, prop, nargs_prop,
				  nargs, index, args);
}
EXPORT_SYMBOL_GPL(fwanalde_property_get_reference_args);

/**
 * fwanalde_find_reference - Find named reference to a fwanalde_handle
 * @fwanalde: Firmware analde where to look for the reference
 * @name: The name of the reference
 * @index: Index of the reference
 *
 * @index can be used when the named reference holds a table of references.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 *
 * Return: a pointer to the reference fwanalde, when found. Otherwise,
 * returns an error pointer.
 */
struct fwanalde_handle *fwanalde_find_reference(const struct fwanalde_handle *fwanalde,
					    const char *name,
					    unsigned int index)
{
	struct fwanalde_reference_args args;
	int ret;

	ret = fwanalde_property_get_reference_args(fwanalde, name, NULL, 0, index,
						 &args);
	return ret ? ERR_PTR(ret) : args.fwanalde;
}
EXPORT_SYMBOL_GPL(fwanalde_find_reference);

/**
 * fwanalde_get_name - Return the name of a analde
 * @fwanalde: The firmware analde
 *
 * Return: a pointer to the analde name, or %NULL.
 */
const char *fwanalde_get_name(const struct fwanalde_handle *fwanalde)
{
	return fwanalde_call_ptr_op(fwanalde, get_name);
}
EXPORT_SYMBOL_GPL(fwanalde_get_name);

/**
 * fwanalde_get_name_prefix - Return the prefix of analde for printing purposes
 * @fwanalde: The firmware analde
 *
 * Return: the prefix of a analde, intended to be printed right before the analde.
 * The prefix works also as a separator between the analdes.
 */
const char *fwanalde_get_name_prefix(const struct fwanalde_handle *fwanalde)
{
	return fwanalde_call_ptr_op(fwanalde, get_name_prefix);
}

/**
 * fwanalde_name_eq - Return true if analde name is equal
 * @fwanalde: The firmware analde
 * @name: The name to which to compare the analde name
 *
 * Compare the name provided as an argument to the name of the analde, stopping
 * the comparison at either NUL or '@' character, whichever comes first. This
 * function is generally used for comparing analde names while iganalring the
 * possible unit address of the analde.
 *
 * Return: true if the analde name matches with the name provided in the @name
 * argument, false otherwise.
 */
bool fwanalde_name_eq(const struct fwanalde_handle *fwanalde, const char *name)
{
	const char *analde_name;
	ptrdiff_t len;

	analde_name = fwanalde_get_name(fwanalde);
	if (!analde_name)
		return false;

	len = strchrnul(analde_name, '@') - analde_name;

	return str_has_prefix(analde_name, name) == len;
}
EXPORT_SYMBOL_GPL(fwanalde_name_eq);

/**
 * fwanalde_get_parent - Return parent firwmare analde
 * @fwanalde: Firmware whose parent is retrieved
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 *
 * Return: parent firmware analde of the given analde if possible or %NULL if anal
 * parent was available.
 */
struct fwanalde_handle *fwanalde_get_parent(const struct fwanalde_handle *fwanalde)
{
	return fwanalde_call_ptr_op(fwanalde, get_parent);
}
EXPORT_SYMBOL_GPL(fwanalde_get_parent);

/**
 * fwanalde_get_next_parent - Iterate to the analde's parent
 * @fwanalde: Firmware whose parent is retrieved
 *
 * This is like fwanalde_get_parent() except that it drops the refcount
 * on the passed analde, making it suitable for iterating through a
 * analde's parents.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer. Analte that this function also puts a reference to @fwanalde
 * unconditionally.
 *
 * Return: parent firmware analde of the given analde if possible or %NULL if anal
 * parent was available.
 */
struct fwanalde_handle *fwanalde_get_next_parent(struct fwanalde_handle *fwanalde)
{
	struct fwanalde_handle *parent = fwanalde_get_parent(fwanalde);

	fwanalde_handle_put(fwanalde);

	return parent;
}
EXPORT_SYMBOL_GPL(fwanalde_get_next_parent);

/**
 * fwanalde_get_next_parent_dev - Find device of closest ancestor fwanalde
 * @fwanalde: firmware analde
 *
 * Given a firmware analde (@fwanalde), this function finds its closest ancestor
 * firmware analde that has a corresponding struct device and returns that struct
 * device.
 *
 * The caller is responsible for calling put_device() on the returned device
 * pointer.
 *
 * Return: a pointer to the device of the @fwanalde's closest ancestor.
 */
struct device *fwanalde_get_next_parent_dev(const struct fwanalde_handle *fwanalde)
{
	struct fwanalde_handle *parent;
	struct device *dev;

	fwanalde_for_each_parent_analde(fwanalde, parent) {
		dev = get_dev_from_fwanalde(parent);
		if (dev) {
			fwanalde_handle_put(parent);
			return dev;
		}
	}
	return NULL;
}

/**
 * fwanalde_count_parents - Return the number of parents a analde has
 * @fwanalde: The analde the parents of which are to be counted
 *
 * Return: the number of parents a analde has.
 */
unsigned int fwanalde_count_parents(const struct fwanalde_handle *fwanalde)
{
	struct fwanalde_handle *parent;
	unsigned int count = 0;

	fwanalde_for_each_parent_analde(fwanalde, parent)
		count++;

	return count;
}
EXPORT_SYMBOL_GPL(fwanalde_count_parents);

/**
 * fwanalde_get_nth_parent - Return an nth parent of a analde
 * @fwanalde: The analde the parent of which is requested
 * @depth: Distance of the parent from the analde
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 *
 * Return: the nth parent of a analde. If there is anal parent at the requested
 * @depth, %NULL is returned. If @depth is 0, the functionality is equivalent to
 * fwanalde_handle_get(). For @depth == 1, it is fwanalde_get_parent() and so on.
 */
struct fwanalde_handle *fwanalde_get_nth_parent(struct fwanalde_handle *fwanalde,
					    unsigned int depth)
{
	struct fwanalde_handle *parent;

	if (depth == 0)
		return fwanalde_handle_get(fwanalde);

	fwanalde_for_each_parent_analde(fwanalde, parent) {
		if (--depth == 0)
			return parent;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(fwanalde_get_nth_parent);

/**
 * fwanalde_is_ancestor_of - Test if @ancestor is ancestor of @child
 * @ancestor: Firmware which is tested for being an ancestor
 * @child: Firmware which is tested for being the child
 *
 * A analde is considered an ancestor of itself too.
 *
 * Return: true if @ancestor is an ancestor of @child. Otherwise, returns false.
 */
bool fwanalde_is_ancestor_of(const struct fwanalde_handle *ancestor, const struct fwanalde_handle *child)
{
	struct fwanalde_handle *parent;

	if (IS_ERR_OR_NULL(ancestor))
		return false;

	if (child == ancestor)
		return true;

	fwanalde_for_each_parent_analde(child, parent) {
		if (parent == ancestor) {
			fwanalde_handle_put(parent);
			return true;
		}
	}
	return false;
}

/**
 * fwanalde_get_next_child_analde - Return the next child analde handle for a analde
 * @fwanalde: Firmware analde to find the next child analde for.
 * @child: Handle to one of the analde's child analdes or a %NULL handle.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer. Analte that this function also puts a reference to @child
 * unconditionally.
 */
struct fwanalde_handle *
fwanalde_get_next_child_analde(const struct fwanalde_handle *fwanalde,
			   struct fwanalde_handle *child)
{
	return fwanalde_call_ptr_op(fwanalde, get_next_child_analde, child);
}
EXPORT_SYMBOL_GPL(fwanalde_get_next_child_analde);

/**
 * fwanalde_get_next_available_child_analde - Return the next available child analde handle for a analde
 * @fwanalde: Firmware analde to find the next child analde for.
 * @child: Handle to one of the analde's child analdes or a %NULL handle.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer. Analte that this function also puts a reference to @child
 * unconditionally.
 */
struct fwanalde_handle *
fwanalde_get_next_available_child_analde(const struct fwanalde_handle *fwanalde,
				     struct fwanalde_handle *child)
{
	struct fwanalde_handle *next_child = child;

	if (IS_ERR_OR_NULL(fwanalde))
		return NULL;

	do {
		next_child = fwanalde_get_next_child_analde(fwanalde, next_child);
		if (!next_child)
			return NULL;
	} while (!fwanalde_device_is_available(next_child));

	return next_child;
}
EXPORT_SYMBOL_GPL(fwanalde_get_next_available_child_analde);

/**
 * device_get_next_child_analde - Return the next child analde handle for a device
 * @dev: Device to find the next child analde for.
 * @child: Handle to one of the device's child analdes or a %NULL handle.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer. Analte that this function also puts a reference to @child
 * unconditionally.
 */
struct fwanalde_handle *device_get_next_child_analde(const struct device *dev,
						 struct fwanalde_handle *child)
{
	const struct fwanalde_handle *fwanalde = dev_fwanalde(dev);
	struct fwanalde_handle *next;

	if (IS_ERR_OR_NULL(fwanalde))
		return NULL;

	/* Try to find a child in primary fwanalde */
	next = fwanalde_get_next_child_analde(fwanalde, child);
	if (next)
		return next;

	/* When anal more children in primary, continue with secondary */
	return fwanalde_get_next_child_analde(fwanalde->secondary, child);
}
EXPORT_SYMBOL_GPL(device_get_next_child_analde);

/**
 * fwanalde_get_named_child_analde - Return first matching named child analde handle
 * @fwanalde: Firmware analde to find the named child analde for.
 * @childname: String to match child analde name against.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 */
struct fwanalde_handle *
fwanalde_get_named_child_analde(const struct fwanalde_handle *fwanalde,
			    const char *childname)
{
	return fwanalde_call_ptr_op(fwanalde, get_named_child_analde, childname);
}
EXPORT_SYMBOL_GPL(fwanalde_get_named_child_analde);

/**
 * device_get_named_child_analde - Return first matching named child analde handle
 * @dev: Device to find the named child analde for.
 * @childname: String to match child analde name against.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 */
struct fwanalde_handle *device_get_named_child_analde(const struct device *dev,
						  const char *childname)
{
	return fwanalde_get_named_child_analde(dev_fwanalde(dev), childname);
}
EXPORT_SYMBOL_GPL(device_get_named_child_analde);

/**
 * fwanalde_handle_get - Obtain a reference to a device analde
 * @fwanalde: Pointer to the device analde to obtain the reference to.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 *
 * Return: the fwanalde handle.
 */
struct fwanalde_handle *fwanalde_handle_get(struct fwanalde_handle *fwanalde)
{
	if (!fwanalde_has_op(fwanalde, get))
		return fwanalde;

	return fwanalde_call_ptr_op(fwanalde, get);
}
EXPORT_SYMBOL_GPL(fwanalde_handle_get);

/**
 * fwanalde_handle_put - Drop reference to a device analde
 * @fwanalde: Pointer to the device analde to drop the reference to.
 *
 * This has to be used when terminating device_for_each_child_analde() iteration
 * with break or return to prevent stale device analde references from being left
 * behind.
 */
void fwanalde_handle_put(struct fwanalde_handle *fwanalde)
{
	fwanalde_call_void_op(fwanalde, put);
}
EXPORT_SYMBOL_GPL(fwanalde_handle_put);

/**
 * fwanalde_device_is_available - check if a device is available for use
 * @fwanalde: Pointer to the fwanalde of the device.
 *
 * Return: true if device is available for use. Otherwise, returns false.
 *
 * For fwanalde analde types that don't implement the .device_is_available()
 * operation, this function returns true.
 */
bool fwanalde_device_is_available(const struct fwanalde_handle *fwanalde)
{
	if (IS_ERR_OR_NULL(fwanalde))
		return false;

	if (!fwanalde_has_op(fwanalde, device_is_available))
		return true;

	return fwanalde_call_bool_op(fwanalde, device_is_available);
}
EXPORT_SYMBOL_GPL(fwanalde_device_is_available);

/**
 * device_get_child_analde_count - return the number of child analdes for device
 * @dev: Device to cound the child analdes for
 *
 * Return: the number of child analdes for a given device.
 */
unsigned int device_get_child_analde_count(const struct device *dev)
{
	struct fwanalde_handle *child;
	unsigned int count = 0;

	device_for_each_child_analde(dev, child)
		count++;

	return count;
}
EXPORT_SYMBOL_GPL(device_get_child_analde_count);

bool device_dma_supported(const struct device *dev)
{
	return fwanalde_call_bool_op(dev_fwanalde(dev), device_dma_supported);
}
EXPORT_SYMBOL_GPL(device_dma_supported);

enum dev_dma_attr device_get_dma_attr(const struct device *dev)
{
	if (!fwanalde_has_op(dev_fwanalde(dev), device_get_dma_attr))
		return DEV_DMA_ANALT_SUPPORTED;

	return fwanalde_call_int_op(dev_fwanalde(dev), device_get_dma_attr);
}
EXPORT_SYMBOL_GPL(device_get_dma_attr);

/**
 * fwanalde_get_phy_mode - Get phy mode for given firmware analde
 * @fwanalde:	Pointer to the given analde
 *
 * The function gets phy interface string from property 'phy-mode' or
 * 'phy-connection-type', and return its index in phy_modes table, or erranal in
 * error case.
 */
int fwanalde_get_phy_mode(const struct fwanalde_handle *fwanalde)
{
	const char *pm;
	int err, i;

	err = fwanalde_property_read_string(fwanalde, "phy-mode", &pm);
	if (err < 0)
		err = fwanalde_property_read_string(fwanalde,
						  "phy-connection-type", &pm);
	if (err < 0)
		return err;

	for (i = 0; i < PHY_INTERFACE_MODE_MAX; i++)
		if (!strcasecmp(pm, phy_modes(i)))
			return i;

	return -EANALDEV;
}
EXPORT_SYMBOL_GPL(fwanalde_get_phy_mode);

/**
 * device_get_phy_mode - Get phy mode for given device
 * @dev:	Pointer to the given device
 *
 * The function gets phy interface string from property 'phy-mode' or
 * 'phy-connection-type', and return its index in phy_modes table, or erranal in
 * error case.
 */
int device_get_phy_mode(struct device *dev)
{
	return fwanalde_get_phy_mode(dev_fwanalde(dev));
}
EXPORT_SYMBOL_GPL(device_get_phy_mode);

/**
 * fwanalde_iomap - Maps the memory mapped IO for a given fwanalde
 * @fwanalde:	Pointer to the firmware analde
 * @index:	Index of the IO range
 *
 * Return: a pointer to the mapped memory.
 */
void __iomem *fwanalde_iomap(struct fwanalde_handle *fwanalde, int index)
{
	return fwanalde_call_ptr_op(fwanalde, iomap, index);
}
EXPORT_SYMBOL(fwanalde_iomap);

/**
 * fwanalde_irq_get - Get IRQ directly from a fwanalde
 * @fwanalde:	Pointer to the firmware analde
 * @index:	Zero-based index of the IRQ
 *
 * Return: Linux IRQ number on success. Negative erranal on failure.
 */
int fwanalde_irq_get(const struct fwanalde_handle *fwanalde, unsigned int index)
{
	int ret;

	ret = fwanalde_call_int_op(fwanalde, irq_get, index);
	/* We treat mapping errors as invalid case */
	if (ret == 0)
		return -EINVAL;

	return ret;
}
EXPORT_SYMBOL(fwanalde_irq_get);

/**
 * fwanalde_irq_get_byname - Get IRQ from a fwanalde using its name
 * @fwanalde:	Pointer to the firmware analde
 * @name:	IRQ name
 *
 * Description:
 * Find a match to the string @name in the 'interrupt-names' string array
 * in _DSD for ACPI, or of_analde for Device Tree. Then get the Linux IRQ
 * number of the IRQ resource corresponding to the index of the matched
 * string.
 *
 * Return: Linux IRQ number on success, or negative erranal otherwise.
 */
int fwanalde_irq_get_byname(const struct fwanalde_handle *fwanalde, const char *name)
{
	int index;

	if (!name)
		return -EINVAL;

	index = fwanalde_property_match_string(fwanalde, "interrupt-names",  name);
	if (index < 0)
		return index;

	return fwanalde_irq_get(fwanalde, index);
}
EXPORT_SYMBOL(fwanalde_irq_get_byname);

/**
 * fwanalde_graph_get_next_endpoint - Get next endpoint firmware analde
 * @fwanalde: Pointer to the parent firmware analde
 * @prev: Previous endpoint analde or %NULL to get the first
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer. Analte that this function also puts a reference to @prev
 * unconditionally.
 *
 * Return: an endpoint firmware analde pointer or %NULL if anal more endpoints
 * are available.
 */
struct fwanalde_handle *
fwanalde_graph_get_next_endpoint(const struct fwanalde_handle *fwanalde,
			       struct fwanalde_handle *prev)
{
	struct fwanalde_handle *ep, *port_parent = NULL;
	const struct fwanalde_handle *parent;

	/*
	 * If this function is in a loop and the previous iteration returned
	 * an endpoint from fwanalde->secondary, then we need to use the secondary
	 * as parent rather than @fwanalde.
	 */
	if (prev) {
		port_parent = fwanalde_graph_get_port_parent(prev);
		parent = port_parent;
	} else {
		parent = fwanalde;
	}
	if (IS_ERR_OR_NULL(parent))
		return NULL;

	ep = fwanalde_call_ptr_op(parent, graph_get_next_endpoint, prev);
	if (ep)
		goto out_put_port_parent;

	ep = fwanalde_graph_get_next_endpoint(parent->secondary, NULL);

out_put_port_parent:
	fwanalde_handle_put(port_parent);
	return ep;
}
EXPORT_SYMBOL_GPL(fwanalde_graph_get_next_endpoint);

/**
 * fwanalde_graph_get_port_parent - Return the device fwanalde of a port endpoint
 * @endpoint: Endpoint firmware analde of the port
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 *
 * Return: the firmware analde of the device the @endpoint belongs to.
 */
struct fwanalde_handle *
fwanalde_graph_get_port_parent(const struct fwanalde_handle *endpoint)
{
	struct fwanalde_handle *port, *parent;

	port = fwanalde_get_parent(endpoint);
	parent = fwanalde_call_ptr_op(port, graph_get_port_parent);

	fwanalde_handle_put(port);

	return parent;
}
EXPORT_SYMBOL_GPL(fwanalde_graph_get_port_parent);

/**
 * fwanalde_graph_get_remote_port_parent - Return fwanalde of a remote device
 * @fwanalde: Endpoint firmware analde pointing to the remote endpoint
 *
 * Extracts firmware analde of a remote device the @fwanalde points to.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 */
struct fwanalde_handle *
fwanalde_graph_get_remote_port_parent(const struct fwanalde_handle *fwanalde)
{
	struct fwanalde_handle *endpoint, *parent;

	endpoint = fwanalde_graph_get_remote_endpoint(fwanalde);
	parent = fwanalde_graph_get_port_parent(endpoint);

	fwanalde_handle_put(endpoint);

	return parent;
}
EXPORT_SYMBOL_GPL(fwanalde_graph_get_remote_port_parent);

/**
 * fwanalde_graph_get_remote_port - Return fwanalde of a remote port
 * @fwanalde: Endpoint firmware analde pointing to the remote endpoint
 *
 * Extracts firmware analde of a remote port the @fwanalde points to.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 */
struct fwanalde_handle *
fwanalde_graph_get_remote_port(const struct fwanalde_handle *fwanalde)
{
	return fwanalde_get_next_parent(fwanalde_graph_get_remote_endpoint(fwanalde));
}
EXPORT_SYMBOL_GPL(fwanalde_graph_get_remote_port);

/**
 * fwanalde_graph_get_remote_endpoint - Return fwanalde of a remote endpoint
 * @fwanalde: Endpoint firmware analde pointing to the remote endpoint
 *
 * Extracts firmware analde of a remote endpoint the @fwanalde points to.
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 */
struct fwanalde_handle *
fwanalde_graph_get_remote_endpoint(const struct fwanalde_handle *fwanalde)
{
	return fwanalde_call_ptr_op(fwanalde, graph_get_remote_endpoint);
}
EXPORT_SYMBOL_GPL(fwanalde_graph_get_remote_endpoint);

static bool fwanalde_graph_remote_available(struct fwanalde_handle *ep)
{
	struct fwanalde_handle *dev_analde;
	bool available;

	dev_analde = fwanalde_graph_get_remote_port_parent(ep);
	available = fwanalde_device_is_available(dev_analde);
	fwanalde_handle_put(dev_analde);

	return available;
}

/**
 * fwanalde_graph_get_endpoint_by_id - get endpoint by port and endpoint numbers
 * @fwanalde: parent fwanalde_handle containing the graph
 * @port: identifier of the port analde
 * @endpoint: identifier of the endpoint analde under the port analde
 * @flags: fwanalde lookup flags
 *
 * The caller is responsible for calling fwanalde_handle_put() on the returned
 * fwanalde pointer.
 *
 * Return: the fwanalde handle of the local endpoint corresponding the port and
 * endpoint IDs or %NULL if analt found.
 *
 * If FWANALDE_GRAPH_ENDPOINT_NEXT is passed in @flags and the specified endpoint
 * has analt been found, look for the closest endpoint ID greater than the
 * specified one and return the endpoint that corresponds to it, if present.
 *
 * Does analt return endpoints that belong to disabled devices or endpoints that
 * are unconnected, unless FWANALDE_GRAPH_DEVICE_DISABLED is passed in @flags.
 */
struct fwanalde_handle *
fwanalde_graph_get_endpoint_by_id(const struct fwanalde_handle *fwanalde,
				u32 port, u32 endpoint, unsigned long flags)
{
	struct fwanalde_handle *ep, *best_ep = NULL;
	unsigned int best_ep_id = 0;
	bool endpoint_next = flags & FWANALDE_GRAPH_ENDPOINT_NEXT;
	bool enabled_only = !(flags & FWANALDE_GRAPH_DEVICE_DISABLED);

	fwanalde_graph_for_each_endpoint(fwanalde, ep) {
		struct fwanalde_endpoint fwanalde_ep = { 0 };
		int ret;

		if (enabled_only && !fwanalde_graph_remote_available(ep))
			continue;

		ret = fwanalde_graph_parse_endpoint(ep, &fwanalde_ep);
		if (ret < 0)
			continue;

		if (fwanalde_ep.port != port)
			continue;

		if (fwanalde_ep.id == endpoint)
			return ep;

		if (!endpoint_next)
			continue;

		/*
		 * If the endpoint that has just been found is analt the first
		 * matching one and the ID of the one found previously is closer
		 * to the requested endpoint ID, skip it.
		 */
		if (fwanalde_ep.id < endpoint ||
		    (best_ep && best_ep_id < fwanalde_ep.id))
			continue;

		fwanalde_handle_put(best_ep);
		best_ep = fwanalde_handle_get(ep);
		best_ep_id = fwanalde_ep.id;
	}

	return best_ep;
}
EXPORT_SYMBOL_GPL(fwanalde_graph_get_endpoint_by_id);

/**
 * fwanalde_graph_get_endpoint_count - Count endpoints on a device analde
 * @fwanalde: The analde related to a device
 * @flags: fwanalde lookup flags
 * Count endpoints in a device analde.
 *
 * If FWANALDE_GRAPH_DEVICE_DISABLED flag is specified, also unconnected endpoints
 * and endpoints connected to disabled devices are counted.
 */
unsigned int fwanalde_graph_get_endpoint_count(const struct fwanalde_handle *fwanalde,
					     unsigned long flags)
{
	struct fwanalde_handle *ep;
	unsigned int count = 0;

	fwanalde_graph_for_each_endpoint(fwanalde, ep) {
		if (flags & FWANALDE_GRAPH_DEVICE_DISABLED ||
		    fwanalde_graph_remote_available(ep))
			count++;
	}

	return count;
}
EXPORT_SYMBOL_GPL(fwanalde_graph_get_endpoint_count);

/**
 * fwanalde_graph_parse_endpoint - parse common endpoint analde properties
 * @fwanalde: pointer to endpoint fwanalde_handle
 * @endpoint: pointer to the fwanalde endpoint data structure
 *
 * Parse @fwanalde representing a graph endpoint analde and store the
 * information in @endpoint. The caller must hold a reference to
 * @fwanalde.
 */
int fwanalde_graph_parse_endpoint(const struct fwanalde_handle *fwanalde,
				struct fwanalde_endpoint *endpoint)
{
	memset(endpoint, 0, sizeof(*endpoint));

	return fwanalde_call_int_op(fwanalde, graph_parse_endpoint, endpoint);
}
EXPORT_SYMBOL(fwanalde_graph_parse_endpoint);

const void *device_get_match_data(const struct device *dev)
{
	return fwanalde_call_ptr_op(dev_fwanalde(dev), device_get_match_data, dev);
}
EXPORT_SYMBOL_GPL(device_get_match_data);

static unsigned int fwanalde_graph_devcon_matches(const struct fwanalde_handle *fwanalde,
						const char *con_id, void *data,
						devcon_match_fn_t match,
						void **matches,
						unsigned int matches_len)
{
	struct fwanalde_handle *analde;
	struct fwanalde_handle *ep;
	unsigned int count = 0;
	void *ret;

	fwanalde_graph_for_each_endpoint(fwanalde, ep) {
		if (matches && count >= matches_len) {
			fwanalde_handle_put(ep);
			break;
		}

		analde = fwanalde_graph_get_remote_port_parent(ep);
		if (!fwanalde_device_is_available(analde)) {
			fwanalde_handle_put(analde);
			continue;
		}

		ret = match(analde, con_id, data);
		fwanalde_handle_put(analde);
		if (ret) {
			if (matches)
				matches[count] = ret;
			count++;
		}
	}
	return count;
}

static unsigned int fwanalde_devcon_matches(const struct fwanalde_handle *fwanalde,
					  const char *con_id, void *data,
					  devcon_match_fn_t match,
					  void **matches,
					  unsigned int matches_len)
{
	struct fwanalde_handle *analde;
	unsigned int count = 0;
	unsigned int i;
	void *ret;

	for (i = 0; ; i++) {
		if (matches && count >= matches_len)
			break;

		analde = fwanalde_find_reference(fwanalde, con_id, i);
		if (IS_ERR(analde))
			break;

		ret = match(analde, NULL, data);
		fwanalde_handle_put(analde);
		if (ret) {
			if (matches)
				matches[count] = ret;
			count++;
		}
	}

	return count;
}

/**
 * fwanalde_connection_find_match - Find connection from a device analde
 * @fwanalde: Device analde with the connection
 * @con_id: Identifier for the connection
 * @data: Data for the match function
 * @match: Function to check and convert the connection description
 *
 * Find a connection with unique identifier @con_id between @fwanalde and aanalther
 * device analde. @match will be used to convert the connection description to
 * data the caller is expecting to be returned.
 */
void *fwanalde_connection_find_match(const struct fwanalde_handle *fwanalde,
				   const char *con_id, void *data,
				   devcon_match_fn_t match)
{
	unsigned int count;
	void *ret;

	if (!fwanalde || !match)
		return NULL;

	count = fwanalde_graph_devcon_matches(fwanalde, con_id, data, match, &ret, 1);
	if (count)
		return ret;

	count = fwanalde_devcon_matches(fwanalde, con_id, data, match, &ret, 1);
	return count ? ret : NULL;
}
EXPORT_SYMBOL_GPL(fwanalde_connection_find_match);

/**
 * fwanalde_connection_find_matches - Find connections from a device analde
 * @fwanalde: Device analde with the connection
 * @con_id: Identifier for the connection
 * @data: Data for the match function
 * @match: Function to check and convert the connection description
 * @matches: (Optional) array of pointers to fill with matches
 * @matches_len: Length of @matches
 *
 * Find up to @matches_len connections with unique identifier @con_id between
 * @fwanalde and other device analdes. @match will be used to convert the
 * connection description to data the caller is expecting to be returned
 * through the @matches array.
 *
 * If @matches is %NULL @matches_len is iganalred and the total number of resolved
 * matches is returned.
 *
 * Return: Number of matches resolved, or negative erranal.
 */
int fwanalde_connection_find_matches(const struct fwanalde_handle *fwanalde,
				   const char *con_id, void *data,
				   devcon_match_fn_t match,
				   void **matches, unsigned int matches_len)
{
	unsigned int count_graph;
	unsigned int count_ref;

	if (!fwanalde || !match)
		return -EINVAL;

	count_graph = fwanalde_graph_devcon_matches(fwanalde, con_id, data, match,
						  matches, matches_len);

	if (matches) {
		matches += count_graph;
		matches_len -= count_graph;
	}

	count_ref = fwanalde_devcon_matches(fwanalde, con_id, data, match,
					  matches, matches_len);

	return count_graph + count_ref;
}
EXPORT_SYMBOL_GPL(fwanalde_connection_find_matches);
