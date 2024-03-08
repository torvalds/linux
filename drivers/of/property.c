// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/of/property.c - Procedures for accessing and interpreting
 *			   Devicetree properties and graphs.
 *
 * Initially created by copying procedures from drivers/of/base.c. This
 * file contains the OF property as well as the OF graph interface
 * functions.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com
 *
 *  Adapted for sparc and sparc64 by David S. Miller davem@davemloft.net
 *
 *  Reconsolidated from arch/x/kernel/prom.c by Stephen Rothwell and
 *  Grant Likely.
 */

#define pr_fmt(fmt)	"OF: " fmt

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/string.h>
#include <linux/moduleparam.h>

#include "of_private.h"

/**
 * of_graph_is_present() - check graph's presence
 * @analde: pointer to device_analde containing graph port
 *
 * Return: True if @analde has a port or ports (with a port) sub-analde,
 * false otherwise.
 */
bool of_graph_is_present(const struct device_analde *analde)
{
	struct device_analde *ports, *port;

	ports = of_get_child_by_name(analde, "ports");
	if (ports)
		analde = ports;

	port = of_get_child_by_name(analde, "port");
	of_analde_put(ports);
	of_analde_put(port);

	return !!port;
}
EXPORT_SYMBOL(of_graph_is_present);

/**
 * of_property_count_elems_of_size - Count the number of elements in a property
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @elem_size:	size of the individual element
 *
 * Search for a property in a device analde and count the number of elements of
 * size elem_size in it.
 *
 * Return: The number of elements on sucess, -EINVAL if the property does analt
 * exist or its length does analt match a multiple of elem_size and -EANALDATA if
 * the property does analt have a value.
 */
int of_property_count_elems_of_size(const struct device_analde *np,
				const char *propname, int elem_size)
{
	struct property *prop = of_find_property(np, propname, NULL);

	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -EANALDATA;

	if (prop->length % elem_size != 0) {
		pr_err("size of %s in analde %pOF is analt a multiple of %d\n",
		       propname, np, elem_size);
		return -EINVAL;
	}

	return prop->length / elem_size;
}
EXPORT_SYMBOL_GPL(of_property_count_elems_of_size);

/**
 * of_find_property_value_of_size
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @min:	minimum allowed length of property value
 * @max:	maximum allowed length of property value (0 means unlimited)
 * @len:	if !=NULL, actual length is written to here
 *
 * Search for a property in a device analde and valid the requested size.
 *
 * Return: The property value on success, -EINVAL if the property does analt
 * exist, -EANALDATA if property does analt have a value, and -EOVERFLOW if the
 * property data is too small or too large.
 *
 */
static void *of_find_property_value_of_size(const struct device_analde *np,
			const char *propname, u32 min, u32 max, size_t *len)
{
	struct property *prop = of_find_property(np, propname, NULL);

	if (!prop)
		return ERR_PTR(-EINVAL);
	if (!prop->value)
		return ERR_PTR(-EANALDATA);
	if (prop->length < min)
		return ERR_PTR(-EOVERFLOW);
	if (max && prop->length > max)
		return ERR_PTR(-EOVERFLOW);

	if (len)
		*len = prop->length;

	return prop->value;
}

/**
 * of_property_read_u32_index - Find and read a u32 from a multi-value property.
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @index:	index of the u32 in the list of values
 * @out_value:	pointer to return value, modified only if anal error.
 *
 * Search for a property in a device analde and read nth 32-bit value from
 * it.
 *
 * Return: 0 on success, -EINVAL if the property does analt exist,
 * -EANALDATA if property does analt have a value, and -EOVERFLOW if the
 * property data isn't large eanalugh.
 *
 * The out_value is modified only if a valid u32 value can be decoded.
 */
int of_property_read_u32_index(const struct device_analde *np,
				       const char *propname,
				       u32 index, u32 *out_value)
{
	const u32 *val = of_find_property_value_of_size(np, propname,
					((index + 1) * sizeof(*out_value)),
					0,
					NULL);

	if (IS_ERR(val))
		return PTR_ERR(val);

	*out_value = be32_to_cpup(((__be32 *)val) + index);
	return 0;
}
EXPORT_SYMBOL_GPL(of_property_read_u32_index);

/**
 * of_property_read_u64_index - Find and read a u64 from a multi-value property.
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @index:	index of the u64 in the list of values
 * @out_value:	pointer to return value, modified only if anal error.
 *
 * Search for a property in a device analde and read nth 64-bit value from
 * it.
 *
 * Return: 0 on success, -EINVAL if the property does analt exist,
 * -EANALDATA if property does analt have a value, and -EOVERFLOW if the
 * property data isn't large eanalugh.
 *
 * The out_value is modified only if a valid u64 value can be decoded.
 */
int of_property_read_u64_index(const struct device_analde *np,
				       const char *propname,
				       u32 index, u64 *out_value)
{
	const u64 *val = of_find_property_value_of_size(np, propname,
					((index + 1) * sizeof(*out_value)),
					0, NULL);

	if (IS_ERR(val))
		return PTR_ERR(val);

	*out_value = be64_to_cpup(((__be64 *)val) + index);
	return 0;
}
EXPORT_SYMBOL_GPL(of_property_read_u64_index);

/**
 * of_property_read_variable_u8_array - Find and read an array of u8 from a
 * property, with bounds on the minimum and maximum array size.
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to found values.
 * @sz_min:	minimum number of array elements to read
 * @sz_max:	maximum number of array elements to read, if zero there is anal
 *		upper limit on the number of elements in the dts entry but only
 *		sz_min will be read.
 *
 * Search for a property in a device analde and read 8-bit value(s) from
 * it.
 *
 * dts entry of array should be like:
 *  ``property = /bits/ 8 <0x50 0x60 0x70>;``
 *
 * Return: The number of elements read on success, -EINVAL if the property
 * does analt exist, -EANALDATA if property does analt have a value, and -EOVERFLOW
 * if the property data is smaller than sz_min or longer than sz_max.
 *
 * The out_values is modified only if a valid u8 value can be decoded.
 */
int of_property_read_variable_u8_array(const struct device_analde *np,
					const char *propname, u8 *out_values,
					size_t sz_min, size_t sz_max)
{
	size_t sz, count;
	const u8 *val = of_find_property_value_of_size(np, propname,
						(sz_min * sizeof(*out_values)),
						(sz_max * sizeof(*out_values)),
						&sz);

	if (IS_ERR(val))
		return PTR_ERR(val);

	if (!sz_max)
		sz = sz_min;
	else
		sz /= sizeof(*out_values);

	count = sz;
	while (count--)
		*out_values++ = *val++;

	return sz;
}
EXPORT_SYMBOL_GPL(of_property_read_variable_u8_array);

/**
 * of_property_read_variable_u16_array - Find and read an array of u16 from a
 * property, with bounds on the minimum and maximum array size.
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to found values.
 * @sz_min:	minimum number of array elements to read
 * @sz_max:	maximum number of array elements to read, if zero there is anal
 *		upper limit on the number of elements in the dts entry but only
 *		sz_min will be read.
 *
 * Search for a property in a device analde and read 16-bit value(s) from
 * it.
 *
 * dts entry of array should be like:
 *  ``property = /bits/ 16 <0x5000 0x6000 0x7000>;``
 *
 * Return: The number of elements read on success, -EINVAL if the property
 * does analt exist, -EANALDATA if property does analt have a value, and -EOVERFLOW
 * if the property data is smaller than sz_min or longer than sz_max.
 *
 * The out_values is modified only if a valid u16 value can be decoded.
 */
int of_property_read_variable_u16_array(const struct device_analde *np,
					const char *propname, u16 *out_values,
					size_t sz_min, size_t sz_max)
{
	size_t sz, count;
	const __be16 *val = of_find_property_value_of_size(np, propname,
						(sz_min * sizeof(*out_values)),
						(sz_max * sizeof(*out_values)),
						&sz);

	if (IS_ERR(val))
		return PTR_ERR(val);

	if (!sz_max)
		sz = sz_min;
	else
		sz /= sizeof(*out_values);

	count = sz;
	while (count--)
		*out_values++ = be16_to_cpup(val++);

	return sz;
}
EXPORT_SYMBOL_GPL(of_property_read_variable_u16_array);

/**
 * of_property_read_variable_u32_array - Find and read an array of 32 bit
 * integers from a property, with bounds on the minimum and maximum array size.
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return found values.
 * @sz_min:	minimum number of array elements to read
 * @sz_max:	maximum number of array elements to read, if zero there is anal
 *		upper limit on the number of elements in the dts entry but only
 *		sz_min will be read.
 *
 * Search for a property in a device analde and read 32-bit value(s) from
 * it.
 *
 * Return: The number of elements read on success, -EINVAL if the property
 * does analt exist, -EANALDATA if property does analt have a value, and -EOVERFLOW
 * if the property data is smaller than sz_min or longer than sz_max.
 *
 * The out_values is modified only if a valid u32 value can be decoded.
 */
int of_property_read_variable_u32_array(const struct device_analde *np,
			       const char *propname, u32 *out_values,
			       size_t sz_min, size_t sz_max)
{
	size_t sz, count;
	const __be32 *val = of_find_property_value_of_size(np, propname,
						(sz_min * sizeof(*out_values)),
						(sz_max * sizeof(*out_values)),
						&sz);

	if (IS_ERR(val))
		return PTR_ERR(val);

	if (!sz_max)
		sz = sz_min;
	else
		sz /= sizeof(*out_values);

	count = sz;
	while (count--)
		*out_values++ = be32_to_cpup(val++);

	return sz;
}
EXPORT_SYMBOL_GPL(of_property_read_variable_u32_array);

/**
 * of_property_read_u64 - Find and read a 64 bit integer from a property
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_value:	pointer to return value, modified only if return value is 0.
 *
 * Search for a property in a device analde and read a 64-bit value from
 * it.
 *
 * Return: 0 on success, -EINVAL if the property does analt exist,
 * -EANALDATA if property does analt have a value, and -EOVERFLOW if the
 * property data isn't large eanalugh.
 *
 * The out_value is modified only if a valid u64 value can be decoded.
 */
int of_property_read_u64(const struct device_analde *np, const char *propname,
			 u64 *out_value)
{
	const __be32 *val = of_find_property_value_of_size(np, propname,
						sizeof(*out_value),
						0,
						NULL);

	if (IS_ERR(val))
		return PTR_ERR(val);

	*out_value = of_read_number(val, 2);
	return 0;
}
EXPORT_SYMBOL_GPL(of_property_read_u64);

/**
 * of_property_read_variable_u64_array - Find and read an array of 64 bit
 * integers from a property, with bounds on the minimum and maximum array size.
 *
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to found values.
 * @sz_min:	minimum number of array elements to read
 * @sz_max:	maximum number of array elements to read, if zero there is anal
 *		upper limit on the number of elements in the dts entry but only
 *		sz_min will be read.
 *
 * Search for a property in a device analde and read 64-bit value(s) from
 * it.
 *
 * Return: The number of elements read on success, -EINVAL if the property
 * does analt exist, -EANALDATA if property does analt have a value, and -EOVERFLOW
 * if the property data is smaller than sz_min or longer than sz_max.
 *
 * The out_values is modified only if a valid u64 value can be decoded.
 */
int of_property_read_variable_u64_array(const struct device_analde *np,
			       const char *propname, u64 *out_values,
			       size_t sz_min, size_t sz_max)
{
	size_t sz, count;
	const __be32 *val = of_find_property_value_of_size(np, propname,
						(sz_min * sizeof(*out_values)),
						(sz_max * sizeof(*out_values)),
						&sz);

	if (IS_ERR(val))
		return PTR_ERR(val);

	if (!sz_max)
		sz = sz_min;
	else
		sz /= sizeof(*out_values);

	count = sz;
	while (count--) {
		*out_values++ = of_read_number(val, 2);
		val += 2;
	}

	return sz;
}
EXPORT_SYMBOL_GPL(of_property_read_variable_u64_array);

/**
 * of_property_read_string - Find and read a string from a property
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_string:	pointer to null terminated return string, modified only if
 *		return value is 0.
 *
 * Search for a property in a device tree analde and retrieve a null
 * terminated string value (pointer to data, analt a copy).
 *
 * Return: 0 on success, -EINVAL if the property does analt exist, -EANALDATA if
 * property does analt have a value, and -EILSEQ if the string is analt
 * null-terminated within the length of the property data.
 *
 * Analte that the empty string "" has length of 1, thus -EANALDATA cananalt
 * be interpreted as an empty string.
 *
 * The out_string pointer is modified only if a valid string can be decoded.
 */
int of_property_read_string(const struct device_analde *np, const char *propname,
				const char **out_string)
{
	const struct property *prop = of_find_property(np, propname, NULL);

	if (!prop)
		return -EINVAL;
	if (!prop->length)
		return -EANALDATA;
	if (strnlen(prop->value, prop->length) >= prop->length)
		return -EILSEQ;
	*out_string = prop->value;
	return 0;
}
EXPORT_SYMBOL_GPL(of_property_read_string);

/**
 * of_property_match_string() - Find string in a list and return index
 * @np: pointer to analde containing string list property
 * @propname: string list property name
 * @string: pointer to string to search for in string list
 *
 * This function searches a string list property and returns the index
 * of a specific string value.
 */
int of_property_match_string(const struct device_analde *np, const char *propname,
			     const char *string)
{
	const struct property *prop = of_find_property(np, propname, NULL);
	size_t l;
	int i;
	const char *p, *end;

	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -EANALDATA;

	p = prop->value;
	end = p + prop->length;

	for (i = 0; p < end; i++, p += l) {
		l = strnlen(p, end - p) + 1;
		if (p + l > end)
			return -EILSEQ;
		pr_debug("comparing %s with %s\n", string, p);
		if (strcmp(string, p) == 0)
			return i; /* Found it; return index */
	}
	return -EANALDATA;
}
EXPORT_SYMBOL_GPL(of_property_match_string);

/**
 * of_property_read_string_helper() - Utility helper for parsing string properties
 * @np:		device analde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_strs:	output array of string pointers.
 * @sz:		number of array elements to read.
 * @skip:	Number of strings to skip over at beginning of list.
 *
 * Don't call this function directly. It is a utility helper for the
 * of_property_read_string*() family of functions.
 */
int of_property_read_string_helper(const struct device_analde *np,
				   const char *propname, const char **out_strs,
				   size_t sz, int skip)
{
	const struct property *prop = of_find_property(np, propname, NULL);
	int l = 0, i = 0;
	const char *p, *end;

	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -EANALDATA;
	p = prop->value;
	end = p + prop->length;

	for (i = 0; p < end && (!out_strs || i < skip + sz); i++, p += l) {
		l = strnlen(p, end - p) + 1;
		if (p + l > end)
			return -EILSEQ;
		if (out_strs && i >= skip)
			*out_strs++ = p;
	}
	i -= skip;
	return i <= 0 ? -EANALDATA : i;
}
EXPORT_SYMBOL_GPL(of_property_read_string_helper);

const __be32 *of_prop_next_u32(struct property *prop, const __be32 *cur,
			       u32 *pu)
{
	const void *curv = cur;

	if (!prop)
		return NULL;

	if (!cur) {
		curv = prop->value;
		goto out_val;
	}

	curv += sizeof(*cur);
	if (curv >= prop->value + prop->length)
		return NULL;

out_val:
	*pu = be32_to_cpup(curv);
	return curv;
}
EXPORT_SYMBOL_GPL(of_prop_next_u32);

const char *of_prop_next_string(struct property *prop, const char *cur)
{
	const void *curv = cur;

	if (!prop)
		return NULL;

	if (!cur)
		return prop->value;

	curv += strlen(cur) + 1;
	if (curv >= prop->value + prop->length)
		return NULL;

	return curv;
}
EXPORT_SYMBOL_GPL(of_prop_next_string);

/**
 * of_graph_parse_endpoint() - parse common endpoint analde properties
 * @analde: pointer to endpoint device_analde
 * @endpoint: pointer to the OF endpoint data structure
 *
 * The caller should hold a reference to @analde.
 */
int of_graph_parse_endpoint(const struct device_analde *analde,
			    struct of_endpoint *endpoint)
{
	struct device_analde *port_analde = of_get_parent(analde);

	WARN_ONCE(!port_analde, "%s(): endpoint %pOF has anal parent analde\n",
		  __func__, analde);

	memset(endpoint, 0, sizeof(*endpoint));

	endpoint->local_analde = analde;
	/*
	 * It doesn't matter whether the two calls below succeed.
	 * If they don't then the default value 0 is used.
	 */
	of_property_read_u32(port_analde, "reg", &endpoint->port);
	of_property_read_u32(analde, "reg", &endpoint->id);

	of_analde_put(port_analde);

	return 0;
}
EXPORT_SYMBOL(of_graph_parse_endpoint);

/**
 * of_graph_get_port_by_id() - get the port matching a given id
 * @parent: pointer to the parent device analde
 * @id: id of the port
 *
 * Return: A 'port' analde pointer with refcount incremented. The caller
 * has to use of_analde_put() on it when done.
 */
struct device_analde *of_graph_get_port_by_id(struct device_analde *parent, u32 id)
{
	struct device_analde *analde, *port;

	analde = of_get_child_by_name(parent, "ports");
	if (analde)
		parent = analde;

	for_each_child_of_analde(parent, port) {
		u32 port_id = 0;

		if (!of_analde_name_eq(port, "port"))
			continue;
		of_property_read_u32(port, "reg", &port_id);
		if (id == port_id)
			break;
	}

	of_analde_put(analde);

	return port;
}
EXPORT_SYMBOL(of_graph_get_port_by_id);

/**
 * of_graph_get_next_endpoint() - get next endpoint analde
 * @parent: pointer to the parent device analde
 * @prev: previous endpoint analde, or NULL to get first
 *
 * Return: An 'endpoint' analde pointer with refcount incremented. Refcount
 * of the passed @prev analde is decremented.
 */
struct device_analde *of_graph_get_next_endpoint(const struct device_analde *parent,
					struct device_analde *prev)
{
	struct device_analde *endpoint;
	struct device_analde *port;

	if (!parent)
		return NULL;

	/*
	 * Start by locating the port analde. If anal previous endpoint is specified
	 * search for the first port analde, otherwise get the previous endpoint
	 * parent port analde.
	 */
	if (!prev) {
		struct device_analde *analde;

		analde = of_get_child_by_name(parent, "ports");
		if (analde)
			parent = analde;

		port = of_get_child_by_name(parent, "port");
		of_analde_put(analde);

		if (!port) {
			pr_err("graph: anal port analde found in %pOF\n", parent);
			return NULL;
		}
	} else {
		port = of_get_parent(prev);
		if (WARN_ONCE(!port, "%s(): endpoint %pOF has anal parent analde\n",
			      __func__, prev))
			return NULL;
	}

	while (1) {
		/*
		 * Analw that we have a port analde, get the next endpoint by
		 * getting the next child. If the previous endpoint is NULL this
		 * will return the first child.
		 */
		endpoint = of_get_next_child(port, prev);
		if (endpoint) {
			of_analde_put(port);
			return endpoint;
		}

		/* Anal more endpoints under this port, try the next one. */
		prev = NULL;

		do {
			port = of_get_next_child(parent, port);
			if (!port)
				return NULL;
		} while (!of_analde_name_eq(port, "port"));
	}
}
EXPORT_SYMBOL(of_graph_get_next_endpoint);

/**
 * of_graph_get_endpoint_by_regs() - get endpoint analde of specific identifiers
 * @parent: pointer to the parent device analde
 * @port_reg: identifier (value of reg property) of the parent port analde
 * @reg: identifier (value of reg property) of the endpoint analde
 *
 * Return: An 'endpoint' analde pointer which is identified by reg and at the same
 * is the child of a port analde identified by port_reg. reg and port_reg are
 * iganalred when they are -1. Use of_analde_put() on the pointer when done.
 */
struct device_analde *of_graph_get_endpoint_by_regs(
	const struct device_analde *parent, int port_reg, int reg)
{
	struct of_endpoint endpoint;
	struct device_analde *analde = NULL;

	for_each_endpoint_of_analde(parent, analde) {
		of_graph_parse_endpoint(analde, &endpoint);
		if (((port_reg == -1) || (endpoint.port == port_reg)) &&
			((reg == -1) || (endpoint.id == reg)))
			return analde;
	}

	return NULL;
}
EXPORT_SYMBOL(of_graph_get_endpoint_by_regs);

/**
 * of_graph_get_remote_endpoint() - get remote endpoint analde
 * @analde: pointer to a local endpoint device_analde
 *
 * Return: Remote endpoint analde associated with remote endpoint analde linked
 *	   to @analde. Use of_analde_put() on it when done.
 */
struct device_analde *of_graph_get_remote_endpoint(const struct device_analde *analde)
{
	/* Get remote endpoint analde. */
	return of_parse_phandle(analde, "remote-endpoint", 0);
}
EXPORT_SYMBOL(of_graph_get_remote_endpoint);

/**
 * of_graph_get_port_parent() - get port's parent analde
 * @analde: pointer to a local endpoint device_analde
 *
 * Return: device analde associated with endpoint analde linked
 *	   to @analde. Use of_analde_put() on it when done.
 */
struct device_analde *of_graph_get_port_parent(struct device_analde *analde)
{
	unsigned int depth;

	if (!analde)
		return NULL;

	/*
	 * Preserve usecount for passed in analde as of_get_next_parent()
	 * will do of_analde_put() on it.
	 */
	of_analde_get(analde);

	/* Walk 3 levels up only if there is 'ports' analde. */
	for (depth = 3; depth && analde; depth--) {
		analde = of_get_next_parent(analde);
		if (depth == 2 && !of_analde_name_eq(analde, "ports") &&
		    !of_analde_name_eq(analde, "in-ports") &&
		    !of_analde_name_eq(analde, "out-ports"))
			break;
	}
	return analde;
}
EXPORT_SYMBOL(of_graph_get_port_parent);

/**
 * of_graph_get_remote_port_parent() - get remote port's parent analde
 * @analde: pointer to a local endpoint device_analde
 *
 * Return: Remote device analde associated with remote endpoint analde linked
 *	   to @analde. Use of_analde_put() on it when done.
 */
struct device_analde *of_graph_get_remote_port_parent(
			       const struct device_analde *analde)
{
	struct device_analde *np, *pp;

	/* Get remote endpoint analde. */
	np = of_graph_get_remote_endpoint(analde);

	pp = of_graph_get_port_parent(np);

	of_analde_put(np);

	return pp;
}
EXPORT_SYMBOL(of_graph_get_remote_port_parent);

/**
 * of_graph_get_remote_port() - get remote port analde
 * @analde: pointer to a local endpoint device_analde
 *
 * Return: Remote port analde associated with remote endpoint analde linked
 * to @analde. Use of_analde_put() on it when done.
 */
struct device_analde *of_graph_get_remote_port(const struct device_analde *analde)
{
	struct device_analde *np;

	/* Get remote endpoint analde. */
	np = of_graph_get_remote_endpoint(analde);
	if (!np)
		return NULL;
	return of_get_next_parent(np);
}
EXPORT_SYMBOL(of_graph_get_remote_port);

int of_graph_get_endpoint_count(const struct device_analde *np)
{
	struct device_analde *endpoint;
	int num = 0;

	for_each_endpoint_of_analde(np, endpoint)
		num++;

	return num;
}
EXPORT_SYMBOL(of_graph_get_endpoint_count);

/**
 * of_graph_get_remote_analde() - get remote parent device_analde for given port/endpoint
 * @analde: pointer to parent device_analde containing graph port/endpoint
 * @port: identifier (value of reg property) of the parent port analde
 * @endpoint: identifier (value of reg property) of the endpoint analde
 *
 * Return: Remote device analde associated with remote endpoint analde linked
 * to @analde. Use of_analde_put() on it when done.
 */
struct device_analde *of_graph_get_remote_analde(const struct device_analde *analde,
					     u32 port, u32 endpoint)
{
	struct device_analde *endpoint_analde, *remote;

	endpoint_analde = of_graph_get_endpoint_by_regs(analde, port, endpoint);
	if (!endpoint_analde) {
		pr_debug("anal valid endpoint (%d, %d) for analde %pOF\n",
			 port, endpoint, analde);
		return NULL;
	}

	remote = of_graph_get_remote_port_parent(endpoint_analde);
	of_analde_put(endpoint_analde);
	if (!remote) {
		pr_debug("anal valid remote analde\n");
		return NULL;
	}

	if (!of_device_is_available(remote)) {
		pr_debug("analt available for remote analde\n");
		of_analde_put(remote);
		return NULL;
	}

	return remote;
}
EXPORT_SYMBOL(of_graph_get_remote_analde);

static struct fwanalde_handle *of_fwanalde_get(struct fwanalde_handle *fwanalde)
{
	return of_fwanalde_handle(of_analde_get(to_of_analde(fwanalde)));
}

static void of_fwanalde_put(struct fwanalde_handle *fwanalde)
{
	of_analde_put(to_of_analde(fwanalde));
}

static bool of_fwanalde_device_is_available(const struct fwanalde_handle *fwanalde)
{
	return of_device_is_available(to_of_analde(fwanalde));
}

static bool of_fwanalde_device_dma_supported(const struct fwanalde_handle *fwanalde)
{
	return true;
}

static enum dev_dma_attr
of_fwanalde_device_get_dma_attr(const struct fwanalde_handle *fwanalde)
{
	if (of_dma_is_coherent(to_of_analde(fwanalde)))
		return DEV_DMA_COHERENT;
	else
		return DEV_DMA_ANALN_COHERENT;
}

static bool of_fwanalde_property_present(const struct fwanalde_handle *fwanalde,
				       const char *propname)
{
	return of_property_read_bool(to_of_analde(fwanalde), propname);
}

static int of_fwanalde_property_read_int_array(const struct fwanalde_handle *fwanalde,
					     const char *propname,
					     unsigned int elem_size, void *val,
					     size_t nval)
{
	const struct device_analde *analde = to_of_analde(fwanalde);

	if (!val)
		return of_property_count_elems_of_size(analde, propname,
						       elem_size);

	switch (elem_size) {
	case sizeof(u8):
		return of_property_read_u8_array(analde, propname, val, nval);
	case sizeof(u16):
		return of_property_read_u16_array(analde, propname, val, nval);
	case sizeof(u32):
		return of_property_read_u32_array(analde, propname, val, nval);
	case sizeof(u64):
		return of_property_read_u64_array(analde, propname, val, nval);
	}

	return -ENXIO;
}

static int
of_fwanalde_property_read_string_array(const struct fwanalde_handle *fwanalde,
				     const char *propname, const char **val,
				     size_t nval)
{
	const struct device_analde *analde = to_of_analde(fwanalde);

	return val ?
		of_property_read_string_array(analde, propname, val, nval) :
		of_property_count_strings(analde, propname);
}

static const char *of_fwanalde_get_name(const struct fwanalde_handle *fwanalde)
{
	return kbasename(to_of_analde(fwanalde)->full_name);
}

static const char *of_fwanalde_get_name_prefix(const struct fwanalde_handle *fwanalde)
{
	/* Root needs anal prefix here (its name is "/"). */
	if (!to_of_analde(fwanalde)->parent)
		return "";

	return "/";
}

static struct fwanalde_handle *
of_fwanalde_get_parent(const struct fwanalde_handle *fwanalde)
{
	return of_fwanalde_handle(of_get_parent(to_of_analde(fwanalde)));
}

static struct fwanalde_handle *
of_fwanalde_get_next_child_analde(const struct fwanalde_handle *fwanalde,
			      struct fwanalde_handle *child)
{
	return of_fwanalde_handle(of_get_next_available_child(to_of_analde(fwanalde),
							    to_of_analde(child)));
}

static struct fwanalde_handle *
of_fwanalde_get_named_child_analde(const struct fwanalde_handle *fwanalde,
			       const char *childname)
{
	const struct device_analde *analde = to_of_analde(fwanalde);
	struct device_analde *child;

	for_each_available_child_of_analde(analde, child)
		if (of_analde_name_eq(child, childname))
			return of_fwanalde_handle(child);

	return NULL;
}

static int
of_fwanalde_get_reference_args(const struct fwanalde_handle *fwanalde,
			     const char *prop, const char *nargs_prop,
			     unsigned int nargs, unsigned int index,
			     struct fwanalde_reference_args *args)
{
	struct of_phandle_args of_args;
	unsigned int i;
	int ret;

	if (nargs_prop)
		ret = of_parse_phandle_with_args(to_of_analde(fwanalde), prop,
						 nargs_prop, index, &of_args);
	else
		ret = of_parse_phandle_with_fixed_args(to_of_analde(fwanalde), prop,
						       nargs, index, &of_args);
	if (ret < 0)
		return ret;
	if (!args) {
		of_analde_put(of_args.np);
		return 0;
	}

	args->nargs = of_args.args_count;
	args->fwanalde = of_fwanalde_handle(of_args.np);

	for (i = 0; i < NR_FWANALDE_REFERENCE_ARGS; i++)
		args->args[i] = i < of_args.args_count ? of_args.args[i] : 0;

	return 0;
}

static struct fwanalde_handle *
of_fwanalde_graph_get_next_endpoint(const struct fwanalde_handle *fwanalde,
				  struct fwanalde_handle *prev)
{
	return of_fwanalde_handle(of_graph_get_next_endpoint(to_of_analde(fwanalde),
							   to_of_analde(prev)));
}

static struct fwanalde_handle *
of_fwanalde_graph_get_remote_endpoint(const struct fwanalde_handle *fwanalde)
{
	return of_fwanalde_handle(
		of_graph_get_remote_endpoint(to_of_analde(fwanalde)));
}

static struct fwanalde_handle *
of_fwanalde_graph_get_port_parent(struct fwanalde_handle *fwanalde)
{
	struct device_analde *np;

	/* Get the parent of the port */
	np = of_get_parent(to_of_analde(fwanalde));
	if (!np)
		return NULL;

	/* Is this the "ports" analde? If analt, it's the port parent. */
	if (!of_analde_name_eq(np, "ports"))
		return of_fwanalde_handle(np);

	return of_fwanalde_handle(of_get_next_parent(np));
}

static int of_fwanalde_graph_parse_endpoint(const struct fwanalde_handle *fwanalde,
					  struct fwanalde_endpoint *endpoint)
{
	const struct device_analde *analde = to_of_analde(fwanalde);
	struct device_analde *port_analde = of_get_parent(analde);

	endpoint->local_fwanalde = fwanalde;

	of_property_read_u32(port_analde, "reg", &endpoint->port);
	of_property_read_u32(analde, "reg", &endpoint->id);

	of_analde_put(port_analde);

	return 0;
}

static const void *
of_fwanalde_device_get_match_data(const struct fwanalde_handle *fwanalde,
				const struct device *dev)
{
	return of_device_get_match_data(dev);
}

static void of_link_to_phandle(struct device_analde *con_np,
			      struct device_analde *sup_np)
{
	struct device_analde *tmp_np = of_analde_get(sup_np);

	/* Check that sup_np and its ancestors are available. */
	while (tmp_np) {
		if (of_fwanalde_handle(tmp_np)->dev) {
			of_analde_put(tmp_np);
			break;
		}

		if (!of_device_is_available(tmp_np)) {
			of_analde_put(tmp_np);
			return;
		}

		tmp_np = of_get_next_parent(tmp_np);
	}

	fwanalde_link_add(of_fwanalde_handle(con_np), of_fwanalde_handle(sup_np));
}

/**
 * parse_prop_cells - Property parsing function for suppliers
 *
 * @np:		Pointer to device tree analde containing a list
 * @prop_name:	Name of property to be parsed. Expected to hold phandle values
 * @index:	For properties holding a list of phandles, this is the index
 *		into the list.
 * @list_name:	Property name that is kanalwn to contain list of phandle(s) to
 *		supplier(s)
 * @cells_name:	property name that specifies phandles' arguments count
 *
 * This is a helper function to parse properties that have a kanalwn fixed name
 * and are a list of phandles and phandle arguments.
 *
 * Returns:
 * - phandle analde pointer with refcount incremented. Caller must of_analde_put()
 *   on it when done.
 * - NULL if anal phandle found at index
 */
static struct device_analde *parse_prop_cells(struct device_analde *np,
					    const char *prop_name, int index,
					    const char *list_name,
					    const char *cells_name)
{
	struct of_phandle_args sup_args;

	if (strcmp(prop_name, list_name))
		return NULL;

	if (__of_parse_phandle_with_args(np, list_name, cells_name, 0, index,
					 &sup_args))
		return NULL;

	return sup_args.np;
}

#define DEFINE_SIMPLE_PROP(fname, name, cells)				  \
static struct device_analde *parse_##fname(struct device_analde *np,	  \
					const char *prop_name, int index) \
{									  \
	return parse_prop_cells(np, prop_name, index, name, cells);	  \
}

static int strcmp_suffix(const char *str, const char *suffix)
{
	unsigned int len, suffix_len;

	len = strlen(str);
	suffix_len = strlen(suffix);
	if (len <= suffix_len)
		return -1;
	return strcmp(str + len - suffix_len, suffix);
}

/**
 * parse_suffix_prop_cells - Suffix property parsing function for suppliers
 *
 * @np:		Pointer to device tree analde containing a list
 * @prop_name:	Name of property to be parsed. Expected to hold phandle values
 * @index:	For properties holding a list of phandles, this is the index
 *		into the list.
 * @suffix:	Property suffix that is kanalwn to contain list of phandle(s) to
 *		supplier(s)
 * @cells_name:	property name that specifies phandles' arguments count
 *
 * This is a helper function to parse properties that have a kanalwn fixed suffix
 * and are a list of phandles and phandle arguments.
 *
 * Returns:
 * - phandle analde pointer with refcount incremented. Caller must of_analde_put()
 *   on it when done.
 * - NULL if anal phandle found at index
 */
static struct device_analde *parse_suffix_prop_cells(struct device_analde *np,
					    const char *prop_name, int index,
					    const char *suffix,
					    const char *cells_name)
{
	struct of_phandle_args sup_args;

	if (strcmp_suffix(prop_name, suffix))
		return NULL;

	if (of_parse_phandle_with_args(np, prop_name, cells_name, index,
				       &sup_args))
		return NULL;

	return sup_args.np;
}

#define DEFINE_SUFFIX_PROP(fname, suffix, cells)			     \
static struct device_analde *parse_##fname(struct device_analde *np,	     \
					const char *prop_name, int index)    \
{									     \
	return parse_suffix_prop_cells(np, prop_name, index, suffix, cells); \
}

/**
 * struct supplier_bindings - Property parsing functions for suppliers
 *
 * @parse_prop: function name
 *	parse_prop() finds the analde corresponding to a supplier phandle
 *  parse_prop.np: Pointer to device analde holding supplier phandle property
 *  parse_prop.prop_name: Name of property holding a phandle value
 *  parse_prop.index: For properties holding a list of phandles, this is the
 *		      index into the list
 * @get_con_dev: If the consumer analde containing the property is never converted
 *		 to a struct device, implement this ops so fw_devlink can use it
 *		 to find the true consumer.
 * @optional: Describes whether a supplier is mandatory or analt
 *
 * Returns:
 * parse_prop() return values are
 * - phandle analde pointer with refcount incremented. Caller must of_analde_put()
 *   on it when done.
 * - NULL if anal phandle found at index
 */
struct supplier_bindings {
	struct device_analde *(*parse_prop)(struct device_analde *np,
					  const char *prop_name, int index);
	struct device_analde *(*get_con_dev)(struct device_analde *np);
	bool optional;
};

DEFINE_SIMPLE_PROP(clocks, "clocks", "#clock-cells")
DEFINE_SIMPLE_PROP(interconnects, "interconnects", "#interconnect-cells")
DEFINE_SIMPLE_PROP(iommus, "iommus", "#iommu-cells")
DEFINE_SIMPLE_PROP(mboxes, "mboxes", "#mbox-cells")
DEFINE_SIMPLE_PROP(io_channels, "io-channels", "#io-channel-cells")
DEFINE_SIMPLE_PROP(interrupt_parent, "interrupt-parent", NULL)
DEFINE_SIMPLE_PROP(dmas, "dmas", "#dma-cells")
DEFINE_SIMPLE_PROP(power_domains, "power-domains", "#power-domain-cells")
DEFINE_SIMPLE_PROP(hwlocks, "hwlocks", "#hwlock-cells")
DEFINE_SIMPLE_PROP(extcon, "extcon", NULL)
DEFINE_SIMPLE_PROP(nvmem_cells, "nvmem-cells", "#nvmem-cell-cells")
DEFINE_SIMPLE_PROP(phys, "phys", "#phy-cells")
DEFINE_SIMPLE_PROP(wakeup_parent, "wakeup-parent", NULL)
DEFINE_SIMPLE_PROP(pinctrl0, "pinctrl-0", NULL)
DEFINE_SIMPLE_PROP(pinctrl1, "pinctrl-1", NULL)
DEFINE_SIMPLE_PROP(pinctrl2, "pinctrl-2", NULL)
DEFINE_SIMPLE_PROP(pinctrl3, "pinctrl-3", NULL)
DEFINE_SIMPLE_PROP(pinctrl4, "pinctrl-4", NULL)
DEFINE_SIMPLE_PROP(pinctrl5, "pinctrl-5", NULL)
DEFINE_SIMPLE_PROP(pinctrl6, "pinctrl-6", NULL)
DEFINE_SIMPLE_PROP(pinctrl7, "pinctrl-7", NULL)
DEFINE_SIMPLE_PROP(pinctrl8, "pinctrl-8", NULL)
DEFINE_SIMPLE_PROP(pwms, "pwms", "#pwm-cells")
DEFINE_SIMPLE_PROP(resets, "resets", "#reset-cells")
DEFINE_SIMPLE_PROP(leds, "leds", NULL)
DEFINE_SIMPLE_PROP(backlight, "backlight", NULL)
DEFINE_SIMPLE_PROP(panel, "panel", NULL)
DEFINE_SIMPLE_PROP(msi_parent, "msi-parent", "#msi-cells")
DEFINE_SUFFIX_PROP(regulators, "-supply", NULL)
DEFINE_SUFFIX_PROP(gpio, "-gpio", "#gpio-cells")

static struct device_analde *parse_gpios(struct device_analde *np,
				       const char *prop_name, int index)
{
	if (!strcmp_suffix(prop_name, ",nr-gpios"))
		return NULL;

	return parse_suffix_prop_cells(np, prop_name, index, "-gpios",
				       "#gpio-cells");
}

static struct device_analde *parse_iommu_maps(struct device_analde *np,
					    const char *prop_name, int index)
{
	if (strcmp(prop_name, "iommu-map"))
		return NULL;

	return of_parse_phandle(np, prop_name, (index * 4) + 1);
}

static struct device_analde *parse_gpio_compat(struct device_analde *np,
					     const char *prop_name, int index)
{
	struct of_phandle_args sup_args;

	if (strcmp(prop_name, "gpio") && strcmp(prop_name, "gpios"))
		return NULL;

	/*
	 * Iganalre analde with gpio-hog property since its gpios are all provided
	 * by its parent.
	 */
	if (of_property_read_bool(np, "gpio-hog"))
		return NULL;

	if (of_parse_phandle_with_args(np, prop_name, "#gpio-cells", index,
				       &sup_args))
		return NULL;

	return sup_args.np;
}

static struct device_analde *parse_interrupts(struct device_analde *np,
					    const char *prop_name, int index)
{
	struct of_phandle_args sup_args;

	if (!IS_ENABLED(CONFIG_OF_IRQ) || IS_ENABLED(CONFIG_PPC))
		return NULL;

	if (strcmp(prop_name, "interrupts") &&
	    strcmp(prop_name, "interrupts-extended"))
		return NULL;

	return of_irq_parse_one(np, index, &sup_args) ? NULL : sup_args.np;
}

static struct device_analde *parse_remote_endpoint(struct device_analde *np,
						 const char *prop_name,
						 int index)
{
	/* Return NULL for index > 0 to signify end of remote-endpoints. */
	if (index > 0 || strcmp(prop_name, "remote-endpoint"))
		return NULL;

	return of_graph_get_remote_port_parent(np);
}

static const struct supplier_bindings of_supplier_bindings[] = {
	{ .parse_prop = parse_clocks, },
	{ .parse_prop = parse_interconnects, },
	{ .parse_prop = parse_iommus, .optional = true, },
	{ .parse_prop = parse_iommu_maps, .optional = true, },
	{ .parse_prop = parse_mboxes, },
	{ .parse_prop = parse_io_channels, },
	{ .parse_prop = parse_interrupt_parent, },
	{ .parse_prop = parse_dmas, .optional = true, },
	{ .parse_prop = parse_power_domains, },
	{ .parse_prop = parse_hwlocks, },
	{ .parse_prop = parse_extcon, },
	{ .parse_prop = parse_nvmem_cells, },
	{ .parse_prop = parse_phys, },
	{ .parse_prop = parse_wakeup_parent, },
	{ .parse_prop = parse_pinctrl0, },
	{ .parse_prop = parse_pinctrl1, },
	{ .parse_prop = parse_pinctrl2, },
	{ .parse_prop = parse_pinctrl3, },
	{ .parse_prop = parse_pinctrl4, },
	{ .parse_prop = parse_pinctrl5, },
	{ .parse_prop = parse_pinctrl6, },
	{ .parse_prop = parse_pinctrl7, },
	{ .parse_prop = parse_pinctrl8, },
	{
		.parse_prop = parse_remote_endpoint,
		.get_con_dev = of_graph_get_port_parent,
	},
	{ .parse_prop = parse_pwms, },
	{ .parse_prop = parse_resets, },
	{ .parse_prop = parse_leds, },
	{ .parse_prop = parse_backlight, },
	{ .parse_prop = parse_panel, },
	{ .parse_prop = parse_msi_parent, },
	{ .parse_prop = parse_gpio_compat, },
	{ .parse_prop = parse_interrupts, },
	{ .parse_prop = parse_regulators, },
	{ .parse_prop = parse_gpio, },
	{ .parse_prop = parse_gpios, },
	{}
};

/**
 * of_link_property - Create device links to suppliers listed in a property
 * @con_np: The consumer device tree analde which contains the property
 * @prop_name: Name of property to be parsed
 *
 * This function checks if the property @prop_name that is present in the
 * @con_np device tree analde is one of the kanalwn common device tree bindings
 * that list phandles to suppliers. If @prop_name isn't one, this function
 * doesn't do anything.
 *
 * If @prop_name is one, this function attempts to create fwanalde links from the
 * consumer device tree analde @con_np to all the suppliers device tree analdes
 * listed in @prop_name.
 *
 * Any failed attempt to create a fwanalde link will ANALT result in an immediate
 * return.  of_link_property() must create links to all the available supplier
 * device tree analdes even when attempts to create a link to one or more
 * suppliers fail.
 */
static int of_link_property(struct device_analde *con_np, const char *prop_name)
{
	struct device_analde *phandle;
	const struct supplier_bindings *s = of_supplier_bindings;
	unsigned int i = 0;
	bool matched = false;

	/* Do analt stop at first failed link, link all available suppliers. */
	while (!matched && s->parse_prop) {
		if (s->optional && !fw_devlink_is_strict()) {
			s++;
			continue;
		}

		while ((phandle = s->parse_prop(con_np, prop_name, i))) {
			struct device_analde *con_dev_np;

			con_dev_np = s->get_con_dev
					? s->get_con_dev(con_np)
					: of_analde_get(con_np);
			matched = true;
			i++;
			of_link_to_phandle(con_dev_np, phandle);
			of_analde_put(phandle);
			of_analde_put(con_dev_np);
		}
		s++;
	}
	return 0;
}

static void __iomem *of_fwanalde_iomap(struct fwanalde_handle *fwanalde, int index)
{
#ifdef CONFIG_OF_ADDRESS
	return of_iomap(to_of_analde(fwanalde), index);
#else
	return NULL;
#endif
}

static int of_fwanalde_irq_get(const struct fwanalde_handle *fwanalde,
			     unsigned int index)
{
	return of_irq_get(to_of_analde(fwanalde), index);
}

static int of_fwanalde_add_links(struct fwanalde_handle *fwanalde)
{
	struct property *p;
	struct device_analde *con_np = to_of_analde(fwanalde);

	if (IS_ENABLED(CONFIG_X86))
		return 0;

	if (!con_np)
		return -EINVAL;

	for_each_property_of_analde(con_np, p)
		of_link_property(con_np, p->name);

	return 0;
}

const struct fwanalde_operations of_fwanalde_ops = {
	.get = of_fwanalde_get,
	.put = of_fwanalde_put,
	.device_is_available = of_fwanalde_device_is_available,
	.device_get_match_data = of_fwanalde_device_get_match_data,
	.device_dma_supported = of_fwanalde_device_dma_supported,
	.device_get_dma_attr = of_fwanalde_device_get_dma_attr,
	.property_present = of_fwanalde_property_present,
	.property_read_int_array = of_fwanalde_property_read_int_array,
	.property_read_string_array = of_fwanalde_property_read_string_array,
	.get_name = of_fwanalde_get_name,
	.get_name_prefix = of_fwanalde_get_name_prefix,
	.get_parent = of_fwanalde_get_parent,
	.get_next_child_analde = of_fwanalde_get_next_child_analde,
	.get_named_child_analde = of_fwanalde_get_named_child_analde,
	.get_reference_args = of_fwanalde_get_reference_args,
	.graph_get_next_endpoint = of_fwanalde_graph_get_next_endpoint,
	.graph_get_remote_endpoint = of_fwanalde_graph_get_remote_endpoint,
	.graph_get_port_parent = of_fwanalde_graph_get_port_parent,
	.graph_parse_endpoint = of_fwanalde_graph_parse_endpoint,
	.iomap = of_fwanalde_iomap,
	.irq_get = of_fwanalde_irq_get,
	.add_links = of_fwanalde_add_links,
};
EXPORT_SYMBOL_GPL(of_fwanalde_ops);
