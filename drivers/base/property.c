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

struct fwnode_handle *__dev_fwnode(struct device *dev)
{
	return IS_ENABLED(CONFIG_OF) && dev->of_node ?
		of_fwnode_handle(dev->of_node) : dev->fwnode;
}
EXPORT_SYMBOL_GPL(__dev_fwnode);

const struct fwnode_handle *__dev_fwnode_const(const struct device *dev)
{
	return IS_ENABLED(CONFIG_OF) && dev->of_node ?
		of_fwnode_handle(dev->of_node) : dev->fwnode;
}
EXPORT_SYMBOL_GPL(__dev_fwnode_const);

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
	return fwnode_property_present(dev_fwnode(dev), propname);
}
EXPORT_SYMBOL_GPL(device_property_present);

/**
 * fwnode_property_present - check if a property of a firmware node is present
 * @fwnode: Firmware node whose property to check
 * @propname: Name of the property
 *
 * Return: true if property @propname is present. Otherwise, returns false.
 */
bool fwnode_property_present(const struct fwnode_handle *fwnode,
			     const char *propname)
{
	bool ret;

	if (IS_ERR_OR_NULL(fwnode))
		return false;

	ret = fwnode_call_bool_op(fwnode, property_present, propname);
	if (ret)
		return ret;

	return fwnode_call_bool_op(fwnode->secondary, property_present, propname);
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
 * It's recommended to call device_property_count_u8() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_u8_array(const struct device *dev, const char *propname,
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
 * It's recommended to call device_property_count_u16() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_u16_array(const struct device *dev, const char *propname,
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
 * It's recommended to call device_property_count_u32() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_u32_array(const struct device *dev, const char *propname,
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
 * It's recommended to call device_property_count_u64() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_u64_array(const struct device *dev, const char *propname,
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
 * It's recommended to call device_property_string_array_count() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values read on success if @val is non-NULL,
 *	   number of values available on success if @val is NULL,
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO or %-EILSEQ if the property is not an array of strings,
 *	   %-EOVERFLOW if the size of the property is not as expected.
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_read_string_array(const struct device *dev, const char *propname,
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
int device_property_read_string(const struct device *dev, const char *propname,
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
 * Return: index, starting from %0, if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of strings,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int device_property_match_string(const struct device *dev, const char *propname,
				 const char *string)
{
	return fwnode_property_match_string(dev_fwnode(dev), propname, string);
}
EXPORT_SYMBOL_GPL(device_property_match_string);

static int fwnode_property_read_int_array(const struct fwnode_handle *fwnode,
					  const char *propname,
					  unsigned int elem_size, void *val,
					  size_t nval)
{
	int ret;

	if (IS_ERR_OR_NULL(fwnode))
		return -EINVAL;

	ret = fwnode_call_int_op(fwnode, property_read_int_array, propname,
				 elem_size, val, nval);
	if (ret != -EINVAL)
		return ret;

	return fwnode_call_int_op(fwnode->secondary, property_read_int_array, propname,
				  elem_size, val, nval);
}

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
 * It's recommended to call fwnode_property_count_u8() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_u8_array(const struct fwnode_handle *fwnode,
				  const char *propname, u8 *val, size_t nval)
{
	return fwnode_property_read_int_array(fwnode, propname, sizeof(u8),
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
 * It's recommended to call fwnode_property_count_u16() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_u16_array(const struct fwnode_handle *fwnode,
				   const char *propname, u16 *val, size_t nval)
{
	return fwnode_property_read_int_array(fwnode, propname, sizeof(u16),
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
 * It's recommended to call fwnode_property_count_u32() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_u32_array(const struct fwnode_handle *fwnode,
				   const char *propname, u32 *val, size_t nval)
{
	return fwnode_property_read_int_array(fwnode, propname, sizeof(u32),
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
 * It's recommended to call fwnode_property_count_u64() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values if @val was %NULL,
 *         %0 if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of numbers,
 *	   %-EOVERFLOW if the size of the property is not as expected,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_u64_array(const struct fwnode_handle *fwnode,
				   const char *propname, u64 *val, size_t nval)
{
	return fwnode_property_read_int_array(fwnode, propname, sizeof(u64),
					      val, nval);
}
EXPORT_SYMBOL_GPL(fwnode_property_read_u64_array);

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
 * It's recommended to call fwnode_property_string_array_count() instead of calling
 * this function with @val equals %NULL and @nval equals 0.
 *
 * Return: number of values read on success if @val is non-NULL,
 *	   number of values available on success if @val is NULL,
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO or %-EILSEQ if the property is not an array of strings,
 *	   %-EOVERFLOW if the size of the property is not as expected,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_read_string_array(const struct fwnode_handle *fwnode,
				      const char *propname, const char **val,
				      size_t nval)
{
	int ret;

	if (IS_ERR_OR_NULL(fwnode))
		return -EINVAL;

	ret = fwnode_call_int_op(fwnode, property_read_string_array, propname,
				 val, nval);
	if (ret != -EINVAL)
		return ret;

	return fwnode_call_int_op(fwnode->secondary, property_read_string_array, propname,
				  val, nval);
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
int fwnode_property_read_string(const struct fwnode_handle *fwnode,
				const char *propname, const char **val)
{
	int ret = fwnode_property_read_string_array(fwnode, propname, val, 1);

	return ret < 0 ? ret : 0;
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
 * Return: index, starting from %0, if the property was found (success),
 *	   %-EINVAL if given arguments are not valid,
 *	   %-ENODATA if the property does not have a value,
 *	   %-EPROTO if the property is not an array of strings,
 *	   %-ENXIO if no suitable firmware interface is present.
 */
int fwnode_property_match_string(const struct fwnode_handle *fwnode,
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
		goto out_free;

	ret = match_string(values, nval, string);
	if (ret < 0)
		ret = -ENODATA;

out_free:
	kfree(values);
	return ret;
}
EXPORT_SYMBOL_GPL(fwnode_property_match_string);

/**
 * fwnode_property_get_reference_args() - Find a reference with arguments
 * @fwnode:	Firmware node where to look for the reference
 * @prop:	The name of the property
 * @nargs_prop:	The name of the property telling the number of
 *		arguments in the referred node. NULL if @nargs is known,
 *		otherwise @nargs is ignored. Only relevant on OF.
 * @nargs:	Number of arguments. Ignored if @nargs_prop is non-NULL.
 * @index:	Index of the reference, from zero onwards.
 * @args:	Result structure with reference and integer arguments.
 *
 * Obtain a reference based on a named property in an fwnode, with
 * integer arguments.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * @args->fwnode pointer.
 *
 * Return: %0 on success
 *	    %-ENOENT when the index is out of bounds, the index has an empty
 *		     reference or the property was not found
 *	    %-EINVAL on parse error
 */
int fwnode_property_get_reference_args(const struct fwnode_handle *fwnode,
				       const char *prop, const char *nargs_prop,
				       unsigned int nargs, unsigned int index,
				       struct fwnode_reference_args *args)
{
	int ret;

	if (IS_ERR_OR_NULL(fwnode))
		return -ENOENT;

	ret = fwnode_call_int_op(fwnode, get_reference_args, prop, nargs_prop,
				 nargs, index, args);
	if (ret == 0)
		return ret;

	if (IS_ERR_OR_NULL(fwnode->secondary))
		return ret;

	return fwnode_call_int_op(fwnode->secondary, get_reference_args, prop, nargs_prop,
				  nargs, index, args);
}
EXPORT_SYMBOL_GPL(fwnode_property_get_reference_args);

/**
 * fwnode_find_reference - Find named reference to a fwnode_handle
 * @fwnode: Firmware node where to look for the reference
 * @name: The name of the reference
 * @index: Index of the reference
 *
 * @index can be used when the named reference holds a table of references.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 *
 * Return: a pointer to the reference fwnode, when found. Otherwise,
 * returns an error pointer.
 */
struct fwnode_handle *fwnode_find_reference(const struct fwnode_handle *fwnode,
					    const char *name,
					    unsigned int index)
{
	struct fwnode_reference_args args;
	int ret;

	ret = fwnode_property_get_reference_args(fwnode, name, NULL, 0, index,
						 &args);
	return ret ? ERR_PTR(ret) : args.fwnode;
}
EXPORT_SYMBOL_GPL(fwnode_find_reference);

/**
 * fwnode_get_name - Return the name of a node
 * @fwnode: The firmware node
 *
 * Return: a pointer to the node name, or %NULL.
 */
const char *fwnode_get_name(const struct fwnode_handle *fwnode)
{
	return fwnode_call_ptr_op(fwnode, get_name);
}
EXPORT_SYMBOL_GPL(fwnode_get_name);

/**
 * fwnode_get_name_prefix - Return the prefix of node for printing purposes
 * @fwnode: The firmware node
 *
 * Return: the prefix of a node, intended to be printed right before the node.
 * The prefix works also as a separator between the nodes.
 */
const char *fwnode_get_name_prefix(const struct fwnode_handle *fwnode)
{
	return fwnode_call_ptr_op(fwnode, get_name_prefix);
}

/**
 * fwnode_get_parent - Return parent firwmare node
 * @fwnode: Firmware whose parent is retrieved
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 *
 * Return: parent firmware node of the given node if possible or %NULL if no
 * parent was available.
 */
struct fwnode_handle *fwnode_get_parent(const struct fwnode_handle *fwnode)
{
	return fwnode_call_ptr_op(fwnode, get_parent);
}
EXPORT_SYMBOL_GPL(fwnode_get_parent);

/**
 * fwnode_get_next_parent - Iterate to the node's parent
 * @fwnode: Firmware whose parent is retrieved
 *
 * This is like fwnode_get_parent() except that it drops the refcount
 * on the passed node, making it suitable for iterating through a
 * node's parents.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer. Note that this function also puts a reference to @fwnode
 * unconditionally.
 *
 * Return: parent firmware node of the given node if possible or %NULL if no
 * parent was available.
 */
struct fwnode_handle *fwnode_get_next_parent(struct fwnode_handle *fwnode)
{
	struct fwnode_handle *parent = fwnode_get_parent(fwnode);

	fwnode_handle_put(fwnode);

	return parent;
}
EXPORT_SYMBOL_GPL(fwnode_get_next_parent);

/**
 * fwnode_get_next_parent_dev - Find device of closest ancestor fwnode
 * @fwnode: firmware node
 *
 * Given a firmware node (@fwnode), this function finds its closest ancestor
 * firmware node that has a corresponding struct device and returns that struct
 * device.
 *
 * The caller is responsible for calling put_device() on the returned device
 * pointer.
 *
 * Return: a pointer to the device of the @fwnode's closest ancestor.
 */
struct device *fwnode_get_next_parent_dev(const struct fwnode_handle *fwnode)
{
	struct fwnode_handle *parent;
	struct device *dev;

	fwnode_for_each_parent_node(fwnode, parent) {
		dev = get_dev_from_fwnode(parent);
		if (dev) {
			fwnode_handle_put(parent);
			return dev;
		}
	}
	return NULL;
}

/**
 * fwnode_count_parents - Return the number of parents a node has
 * @fwnode: The node the parents of which are to be counted
 *
 * Return: the number of parents a node has.
 */
unsigned int fwnode_count_parents(const struct fwnode_handle *fwnode)
{
	struct fwnode_handle *parent;
	unsigned int count = 0;

	fwnode_for_each_parent_node(fwnode, parent)
		count++;

	return count;
}
EXPORT_SYMBOL_GPL(fwnode_count_parents);

/**
 * fwnode_get_nth_parent - Return an nth parent of a node
 * @fwnode: The node the parent of which is requested
 * @depth: Distance of the parent from the node
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 *
 * Return: the nth parent of a node. If there is no parent at the requested
 * @depth, %NULL is returned. If @depth is 0, the functionality is equivalent to
 * fwnode_handle_get(). For @depth == 1, it is fwnode_get_parent() and so on.
 */
struct fwnode_handle *fwnode_get_nth_parent(struct fwnode_handle *fwnode,
					    unsigned int depth)
{
	struct fwnode_handle *parent;

	if (depth == 0)
		return fwnode_handle_get(fwnode);

	fwnode_for_each_parent_node(fwnode, parent) {
		if (--depth == 0)
			return parent;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(fwnode_get_nth_parent);

/**
 * fwnode_is_ancestor_of - Test if @ancestor is ancestor of @child
 * @ancestor: Firmware which is tested for being an ancestor
 * @child: Firmware which is tested for being the child
 *
 * A node is considered an ancestor of itself too.
 *
 * Return: true if @ancestor is an ancestor of @child. Otherwise, returns false.
 */
bool fwnode_is_ancestor_of(const struct fwnode_handle *ancestor, const struct fwnode_handle *child)
{
	struct fwnode_handle *parent;

	if (IS_ERR_OR_NULL(ancestor))
		return false;

	if (child == ancestor)
		return true;

	fwnode_for_each_parent_node(child, parent) {
		if (parent == ancestor) {
			fwnode_handle_put(parent);
			return true;
		}
	}
	return false;
}

/**
 * fwnode_get_next_child_node - Return the next child node handle for a node
 * @fwnode: Firmware node to find the next child node for.
 * @child: Handle to one of the node's child nodes or a %NULL handle.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer. Note that this function also puts a reference to @child
 * unconditionally.
 */
struct fwnode_handle *
fwnode_get_next_child_node(const struct fwnode_handle *fwnode,
			   struct fwnode_handle *child)
{
	return fwnode_call_ptr_op(fwnode, get_next_child_node, child);
}
EXPORT_SYMBOL_GPL(fwnode_get_next_child_node);

/**
 * fwnode_get_next_available_child_node - Return the next available child node handle for a node
 * @fwnode: Firmware node to find the next child node for.
 * @child: Handle to one of the node's child nodes or a %NULL handle.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer. Note that this function also puts a reference to @child
 * unconditionally.
 */
struct fwnode_handle *
fwnode_get_next_available_child_node(const struct fwnode_handle *fwnode,
				     struct fwnode_handle *child)
{
	struct fwnode_handle *next_child = child;

	if (IS_ERR_OR_NULL(fwnode))
		return NULL;

	do {
		next_child = fwnode_get_next_child_node(fwnode, next_child);
		if (!next_child)
			return NULL;
	} while (!fwnode_device_is_available(next_child));

	return next_child;
}
EXPORT_SYMBOL_GPL(fwnode_get_next_available_child_node);

/**
 * device_get_next_child_node - Return the next child node handle for a device
 * @dev: Device to find the next child node for.
 * @child: Handle to one of the device's child nodes or a %NULL handle.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer. Note that this function also puts a reference to @child
 * unconditionally.
 */
struct fwnode_handle *device_get_next_child_node(const struct device *dev,
						 struct fwnode_handle *child)
{
	const struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct fwnode_handle *next;

	if (IS_ERR_OR_NULL(fwnode))
		return NULL;

	/* Try to find a child in primary fwnode */
	next = fwnode_get_next_child_node(fwnode, child);
	if (next)
		return next;

	/* When no more children in primary, continue with secondary */
	return fwnode_get_next_child_node(fwnode->secondary, child);
}
EXPORT_SYMBOL_GPL(device_get_next_child_node);

/**
 * fwnode_get_named_child_node - Return first matching named child node handle
 * @fwnode: Firmware node to find the named child node for.
 * @childname: String to match child node name against.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 */
struct fwnode_handle *
fwnode_get_named_child_node(const struct fwnode_handle *fwnode,
			    const char *childname)
{
	return fwnode_call_ptr_op(fwnode, get_named_child_node, childname);
}
EXPORT_SYMBOL_GPL(fwnode_get_named_child_node);

/**
 * device_get_named_child_node - Return first matching named child node handle
 * @dev: Device to find the named child node for.
 * @childname: String to match child node name against.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 */
struct fwnode_handle *device_get_named_child_node(const struct device *dev,
						  const char *childname)
{
	return fwnode_get_named_child_node(dev_fwnode(dev), childname);
}
EXPORT_SYMBOL_GPL(device_get_named_child_node);

/**
 * fwnode_handle_get - Obtain a reference to a device node
 * @fwnode: Pointer to the device node to obtain the reference to.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 *
 * Return: the fwnode handle.
 */
struct fwnode_handle *fwnode_handle_get(struct fwnode_handle *fwnode)
{
	if (!fwnode_has_op(fwnode, get))
		return fwnode;

	return fwnode_call_ptr_op(fwnode, get);
}
EXPORT_SYMBOL_GPL(fwnode_handle_get);

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
	fwnode_call_void_op(fwnode, put);
}
EXPORT_SYMBOL_GPL(fwnode_handle_put);

/**
 * fwnode_device_is_available - check if a device is available for use
 * @fwnode: Pointer to the fwnode of the device.
 *
 * Return: true if device is available for use. Otherwise, returns false.
 *
 * For fwnode node types that don't implement the .device_is_available()
 * operation, this function returns true.
 */
bool fwnode_device_is_available(const struct fwnode_handle *fwnode)
{
	if (IS_ERR_OR_NULL(fwnode))
		return false;

	if (!fwnode_has_op(fwnode, device_is_available))
		return true;

	return fwnode_call_bool_op(fwnode, device_is_available);
}
EXPORT_SYMBOL_GPL(fwnode_device_is_available);

/**
 * device_get_child_node_count - return the number of child nodes for device
 * @dev: Device to cound the child nodes for
 *
 * Return: the number of child nodes for a given device.
 */
unsigned int device_get_child_node_count(const struct device *dev)
{
	struct fwnode_handle *child;
	unsigned int count = 0;

	device_for_each_child_node(dev, child)
		count++;

	return count;
}
EXPORT_SYMBOL_GPL(device_get_child_node_count);

bool device_dma_supported(const struct device *dev)
{
	return fwnode_call_bool_op(dev_fwnode(dev), device_dma_supported);
}
EXPORT_SYMBOL_GPL(device_dma_supported);

enum dev_dma_attr device_get_dma_attr(const struct device *dev)
{
	if (!fwnode_has_op(dev_fwnode(dev), device_get_dma_attr))
		return DEV_DMA_NOT_SUPPORTED;

	return fwnode_call_int_op(dev_fwnode(dev), device_get_dma_attr);
}
EXPORT_SYMBOL_GPL(device_get_dma_attr);

/**
 * fwnode_get_phy_mode - Get phy mode for given firmware node
 * @fwnode:	Pointer to the given node
 *
 * The function gets phy interface string from property 'phy-mode' or
 * 'phy-connection-type', and return its index in phy_modes table, or errno in
 * error case.
 */
int fwnode_get_phy_mode(const struct fwnode_handle *fwnode)
{
	const char *pm;
	int err, i;

	err = fwnode_property_read_string(fwnode, "phy-mode", &pm);
	if (err < 0)
		err = fwnode_property_read_string(fwnode,
						  "phy-connection-type", &pm);
	if (err < 0)
		return err;

	for (i = 0; i < PHY_INTERFACE_MODE_MAX; i++)
		if (!strcasecmp(pm, phy_modes(i)))
			return i;

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(fwnode_get_phy_mode);

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
	return fwnode_get_phy_mode(dev_fwnode(dev));
}
EXPORT_SYMBOL_GPL(device_get_phy_mode);

/**
 * fwnode_iomap - Maps the memory mapped IO for a given fwnode
 * @fwnode:	Pointer to the firmware node
 * @index:	Index of the IO range
 *
 * Return: a pointer to the mapped memory.
 */
void __iomem *fwnode_iomap(struct fwnode_handle *fwnode, int index)
{
	return fwnode_call_ptr_op(fwnode, iomap, index);
}
EXPORT_SYMBOL(fwnode_iomap);

/**
 * fwnode_irq_get - Get IRQ directly from a fwnode
 * @fwnode:	Pointer to the firmware node
 * @index:	Zero-based index of the IRQ
 *
 * Return: Linux IRQ number on success. Other values are determined
 * according to acpi_irq_get() or of_irq_get() operation.
 */
int fwnode_irq_get(const struct fwnode_handle *fwnode, unsigned int index)
{
	return fwnode_call_int_op(fwnode, irq_get, index);
}
EXPORT_SYMBOL(fwnode_irq_get);

/**
 * fwnode_irq_get_byname - Get IRQ from a fwnode using its name
 * @fwnode:	Pointer to the firmware node
 * @name:	IRQ name
 *
 * Description:
 * Find a match to the string @name in the 'interrupt-names' string array
 * in _DSD for ACPI, or of_node for Device Tree. Then get the Linux IRQ
 * number of the IRQ resource corresponding to the index of the matched
 * string.
 *
 * Return: Linux IRQ number on success, or negative errno otherwise.
 */
int fwnode_irq_get_byname(const struct fwnode_handle *fwnode, const char *name)
{
	int index;

	if (!name)
		return -EINVAL;

	index = fwnode_property_match_string(fwnode, "interrupt-names",  name);
	if (index < 0)
		return index;

	return fwnode_irq_get(fwnode, index);
}
EXPORT_SYMBOL(fwnode_irq_get_byname);

/**
 * fwnode_graph_get_next_endpoint - Get next endpoint firmware node
 * @fwnode: Pointer to the parent firmware node
 * @prev: Previous endpoint node or %NULL to get the first
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer. Note that this function also puts a reference to @prev
 * unconditionally.
 *
 * Return: an endpoint firmware node pointer or %NULL if no more endpoints
 * are available.
 */
struct fwnode_handle *
fwnode_graph_get_next_endpoint(const struct fwnode_handle *fwnode,
			       struct fwnode_handle *prev)
{
	struct fwnode_handle *ep, *port_parent = NULL;
	const struct fwnode_handle *parent;

	/*
	 * If this function is in a loop and the previous iteration returned
	 * an endpoint from fwnode->secondary, then we need to use the secondary
	 * as parent rather than @fwnode.
	 */
	if (prev) {
		port_parent = fwnode_graph_get_port_parent(prev);
		parent = port_parent;
	} else {
		parent = fwnode;
	}
	if (IS_ERR_OR_NULL(parent))
		return NULL;

	ep = fwnode_call_ptr_op(parent, graph_get_next_endpoint, prev);
	if (ep)
		goto out_put_port_parent;

	ep = fwnode_graph_get_next_endpoint(parent->secondary, NULL);

out_put_port_parent:
	fwnode_handle_put(port_parent);
	return ep;
}
EXPORT_SYMBOL_GPL(fwnode_graph_get_next_endpoint);

/**
 * fwnode_graph_get_port_parent - Return the device fwnode of a port endpoint
 * @endpoint: Endpoint firmware node of the port
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 *
 * Return: the firmware node of the device the @endpoint belongs to.
 */
struct fwnode_handle *
fwnode_graph_get_port_parent(const struct fwnode_handle *endpoint)
{
	struct fwnode_handle *port, *parent;

	port = fwnode_get_parent(endpoint);
	parent = fwnode_call_ptr_op(port, graph_get_port_parent);

	fwnode_handle_put(port);

	return parent;
}
EXPORT_SYMBOL_GPL(fwnode_graph_get_port_parent);

/**
 * fwnode_graph_get_remote_port_parent - Return fwnode of a remote device
 * @fwnode: Endpoint firmware node pointing to the remote endpoint
 *
 * Extracts firmware node of a remote device the @fwnode points to.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 */
struct fwnode_handle *
fwnode_graph_get_remote_port_parent(const struct fwnode_handle *fwnode)
{
	struct fwnode_handle *endpoint, *parent;

	endpoint = fwnode_graph_get_remote_endpoint(fwnode);
	parent = fwnode_graph_get_port_parent(endpoint);

	fwnode_handle_put(endpoint);

	return parent;
}
EXPORT_SYMBOL_GPL(fwnode_graph_get_remote_port_parent);

/**
 * fwnode_graph_get_remote_port - Return fwnode of a remote port
 * @fwnode: Endpoint firmware node pointing to the remote endpoint
 *
 * Extracts firmware node of a remote port the @fwnode points to.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 */
struct fwnode_handle *
fwnode_graph_get_remote_port(const struct fwnode_handle *fwnode)
{
	return fwnode_get_next_parent(fwnode_graph_get_remote_endpoint(fwnode));
}
EXPORT_SYMBOL_GPL(fwnode_graph_get_remote_port);

/**
 * fwnode_graph_get_remote_endpoint - Return fwnode of a remote endpoint
 * @fwnode: Endpoint firmware node pointing to the remote endpoint
 *
 * Extracts firmware node of a remote endpoint the @fwnode points to.
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 */
struct fwnode_handle *
fwnode_graph_get_remote_endpoint(const struct fwnode_handle *fwnode)
{
	return fwnode_call_ptr_op(fwnode, graph_get_remote_endpoint);
}
EXPORT_SYMBOL_GPL(fwnode_graph_get_remote_endpoint);

static bool fwnode_graph_remote_available(struct fwnode_handle *ep)
{
	struct fwnode_handle *dev_node;
	bool available;

	dev_node = fwnode_graph_get_remote_port_parent(ep);
	available = fwnode_device_is_available(dev_node);
	fwnode_handle_put(dev_node);

	return available;
}

/**
 * fwnode_graph_get_endpoint_by_id - get endpoint by port and endpoint numbers
 * @fwnode: parent fwnode_handle containing the graph
 * @port: identifier of the port node
 * @endpoint: identifier of the endpoint node under the port node
 * @flags: fwnode lookup flags
 *
 * The caller is responsible for calling fwnode_handle_put() on the returned
 * fwnode pointer.
 *
 * Return: the fwnode handle of the local endpoint corresponding the port and
 * endpoint IDs or %NULL if not found.
 *
 * If FWNODE_GRAPH_ENDPOINT_NEXT is passed in @flags and the specified endpoint
 * has not been found, look for the closest endpoint ID greater than the
 * specified one and return the endpoint that corresponds to it, if present.
 *
 * Does not return endpoints that belong to disabled devices or endpoints that
 * are unconnected, unless FWNODE_GRAPH_DEVICE_DISABLED is passed in @flags.
 */
struct fwnode_handle *
fwnode_graph_get_endpoint_by_id(const struct fwnode_handle *fwnode,
				u32 port, u32 endpoint, unsigned long flags)
{
	struct fwnode_handle *ep, *best_ep = NULL;
	unsigned int best_ep_id = 0;
	bool endpoint_next = flags & FWNODE_GRAPH_ENDPOINT_NEXT;
	bool enabled_only = !(flags & FWNODE_GRAPH_DEVICE_DISABLED);

	fwnode_graph_for_each_endpoint(fwnode, ep) {
		struct fwnode_endpoint fwnode_ep = { 0 };
		int ret;

		if (enabled_only && !fwnode_graph_remote_available(ep))
			continue;

		ret = fwnode_graph_parse_endpoint(ep, &fwnode_ep);
		if (ret < 0)
			continue;

		if (fwnode_ep.port != port)
			continue;

		if (fwnode_ep.id == endpoint)
			return ep;

		if (!endpoint_next)
			continue;

		/*
		 * If the endpoint that has just been found is not the first
		 * matching one and the ID of the one found previously is closer
		 * to the requested endpoint ID, skip it.
		 */
		if (fwnode_ep.id < endpoint ||
		    (best_ep && best_ep_id < fwnode_ep.id))
			continue;

		fwnode_handle_put(best_ep);
		best_ep = fwnode_handle_get(ep);
		best_ep_id = fwnode_ep.id;
	}

	return best_ep;
}
EXPORT_SYMBOL_GPL(fwnode_graph_get_endpoint_by_id);

/**
 * fwnode_graph_get_endpoint_count - Count endpoints on a device node
 * @fwnode: The node related to a device
 * @flags: fwnode lookup flags
 * Count endpoints in a device node.
 *
 * If FWNODE_GRAPH_DEVICE_DISABLED flag is specified, also unconnected endpoints
 * and endpoints connected to disabled devices are counted.
 */
unsigned int fwnode_graph_get_endpoint_count(const struct fwnode_handle *fwnode,
					     unsigned long flags)
{
	struct fwnode_handle *ep;
	unsigned int count = 0;

	fwnode_graph_for_each_endpoint(fwnode, ep) {
		if (flags & FWNODE_GRAPH_DEVICE_DISABLED ||
		    fwnode_graph_remote_available(ep))
			count++;
	}

	return count;
}
EXPORT_SYMBOL_GPL(fwnode_graph_get_endpoint_count);

/**
 * fwnode_graph_parse_endpoint - parse common endpoint node properties
 * @fwnode: pointer to endpoint fwnode_handle
 * @endpoint: pointer to the fwnode endpoint data structure
 *
 * Parse @fwnode representing a graph endpoint node and store the
 * information in @endpoint. The caller must hold a reference to
 * @fwnode.
 */
int fwnode_graph_parse_endpoint(const struct fwnode_handle *fwnode,
				struct fwnode_endpoint *endpoint)
{
	memset(endpoint, 0, sizeof(*endpoint));

	return fwnode_call_int_op(fwnode, graph_parse_endpoint, endpoint);
}
EXPORT_SYMBOL(fwnode_graph_parse_endpoint);

const void *device_get_match_data(const struct device *dev)
{
	return fwnode_call_ptr_op(dev_fwnode(dev), device_get_match_data, dev);
}
EXPORT_SYMBOL_GPL(device_get_match_data);

static unsigned int fwnode_graph_devcon_matches(const struct fwnode_handle *fwnode,
						const char *con_id, void *data,
						devcon_match_fn_t match,
						void **matches,
						unsigned int matches_len)
{
	struct fwnode_handle *node;
	struct fwnode_handle *ep;
	unsigned int count = 0;
	void *ret;

	fwnode_graph_for_each_endpoint(fwnode, ep) {
		if (matches && count >= matches_len) {
			fwnode_handle_put(ep);
			break;
		}

		node = fwnode_graph_get_remote_port_parent(ep);
		if (!fwnode_device_is_available(node)) {
			fwnode_handle_put(node);
			continue;
		}

		ret = match(node, con_id, data);
		fwnode_handle_put(node);
		if (ret) {
			if (matches)
				matches[count] = ret;
			count++;
		}
	}
	return count;
}

static unsigned int fwnode_devcon_matches(const struct fwnode_handle *fwnode,
					  const char *con_id, void *data,
					  devcon_match_fn_t match,
					  void **matches,
					  unsigned int matches_len)
{
	struct fwnode_handle *node;
	unsigned int count = 0;
	unsigned int i;
	void *ret;

	for (i = 0; ; i++) {
		if (matches && count >= matches_len)
			break;

		node = fwnode_find_reference(fwnode, con_id, i);
		if (IS_ERR(node))
			break;

		ret = match(node, NULL, data);
		fwnode_handle_put(node);
		if (ret) {
			if (matches)
				matches[count] = ret;
			count++;
		}
	}

	return count;
}

/**
 * fwnode_connection_find_match - Find connection from a device node
 * @fwnode: Device node with the connection
 * @con_id: Identifier for the connection
 * @data: Data for the match function
 * @match: Function to check and convert the connection description
 *
 * Find a connection with unique identifier @con_id between @fwnode and another
 * device node. @match will be used to convert the connection description to
 * data the caller is expecting to be returned.
 */
void *fwnode_connection_find_match(const struct fwnode_handle *fwnode,
				   const char *con_id, void *data,
				   devcon_match_fn_t match)
{
	unsigned int count;
	void *ret;

	if (!fwnode || !match)
		return NULL;

	count = fwnode_graph_devcon_matches(fwnode, con_id, data, match, &ret, 1);
	if (count)
		return ret;

	count = fwnode_devcon_matches(fwnode, con_id, data, match, &ret, 1);
	return count ? ret : NULL;
}
EXPORT_SYMBOL_GPL(fwnode_connection_find_match);

/**
 * fwnode_connection_find_matches - Find connections from a device node
 * @fwnode: Device node with the connection
 * @con_id: Identifier for the connection
 * @data: Data for the match function
 * @match: Function to check and convert the connection description
 * @matches: (Optional) array of pointers to fill with matches
 * @matches_len: Length of @matches
 *
 * Find up to @matches_len connections with unique identifier @con_id between
 * @fwnode and other device nodes. @match will be used to convert the
 * connection description to data the caller is expecting to be returned
 * through the @matches array.
 *
 * If @matches is %NULL @matches_len is ignored and the total number of resolved
 * matches is returned.
 *
 * Return: Number of matches resolved, or negative errno.
 */
int fwnode_connection_find_matches(const struct fwnode_handle *fwnode,
				   const char *con_id, void *data,
				   devcon_match_fn_t match,
				   void **matches, unsigned int matches_len)
{
	unsigned int count_graph;
	unsigned int count_ref;

	if (!fwnode || !match)
		return -EINVAL;

	count_graph = fwnode_graph_devcon_matches(fwnode, con_id, data, match,
						  matches, matches_len);

	if (matches) {
		matches += count_graph;
		matches_len -= count_graph;
	}

	count_ref = fwnode_devcon_matches(fwnode, con_id, data, match,
					  matches, matches_len);

	return count_graph + count_ref;
}
EXPORT_SYMBOL_GPL(fwnode_connection_find_matches);
