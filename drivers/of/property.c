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
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/string.h>
#include <linux/moduleparam.h>

#include "of_private.h"

/**
 * of_property_count_elems_of_size - Count the number of elements in a property
 *
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @elem_size:	size of the individual element
 *
 * Search for a property in a device yesde and count the number of elements of
 * size elem_size in it. Returns number of elements on sucess, -EINVAL if the
 * property does yest exist or its length does yest match a multiple of elem_size
 * and -ENODATA if the property does yest have a value.
 */
int of_property_count_elems_of_size(const struct device_yesde *np,
				const char *propname, int elem_size)
{
	struct property *prop = of_find_property(np, propname, NULL);

	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;

	if (prop->length % elem_size != 0) {
		pr_err("size of %s in yesde %pOF is yest a multiple of %d\n",
		       propname, np, elem_size);
		return -EINVAL;
	}

	return prop->length / elem_size;
}
EXPORT_SYMBOL_GPL(of_property_count_elems_of_size);

/**
 * of_find_property_value_of_size
 *
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @min:	minimum allowed length of property value
 * @max:	maximum allowed length of property value (0 means unlimited)
 * @len:	if !=NULL, actual length is written to here
 *
 * Search for a property in a device yesde and valid the requested size.
 * Returns the property value on success, -EINVAL if the property does yest
 *  exist, -ENODATA if property does yest have a value, and -EOVERFLOW if the
 * property data is too small or too large.
 *
 */
static void *of_find_property_value_of_size(const struct device_yesde *np,
			const char *propname, u32 min, u32 max, size_t *len)
{
	struct property *prop = of_find_property(np, propname, NULL);

	if (!prop)
		return ERR_PTR(-EINVAL);
	if (!prop->value)
		return ERR_PTR(-ENODATA);
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
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @index:	index of the u32 in the list of values
 * @out_value:	pointer to return value, modified only if yes error.
 *
 * Search for a property in a device yesde and read nth 32-bit value from
 * it. Returns 0 on success, -EINVAL if the property does yest exist,
 * -ENODATA if property does yest have a value, and -EOVERFLOW if the
 * property data isn't large eyesugh.
 *
 * The out_value is modified only if a valid u32 value can be decoded.
 */
int of_property_read_u32_index(const struct device_yesde *np,
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
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @index:	index of the u64 in the list of values
 * @out_value:	pointer to return value, modified only if yes error.
 *
 * Search for a property in a device yesde and read nth 64-bit value from
 * it. Returns 0 on success, -EINVAL if the property does yest exist,
 * -ENODATA if property does yest have a value, and -EOVERFLOW if the
 * property data isn't large eyesugh.
 *
 * The out_value is modified only if a valid u64 value can be decoded.
 */
int of_property_read_u64_index(const struct device_yesde *np,
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
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to found values.
 * @sz_min:	minimum number of array elements to read
 * @sz_max:	maximum number of array elements to read, if zero there is yes
 *		upper limit on the number of elements in the dts entry but only
 *		sz_min will be read.
 *
 * Search for a property in a device yesde and read 8-bit value(s) from
 * it. Returns number of elements read on success, -EINVAL if the property
 * does yest exist, -ENODATA if property does yest have a value, and -EOVERFLOW
 * if the property data is smaller than sz_min or longer than sz_max.
 *
 * dts entry of array should be like:
 *	property = /bits/ 8 <0x50 0x60 0x70>;
 *
 * The out_values is modified only if a valid u8 value can be decoded.
 */
int of_property_read_variable_u8_array(const struct device_yesde *np,
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
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to found values.
 * @sz_min:	minimum number of array elements to read
 * @sz_max:	maximum number of array elements to read, if zero there is yes
 *		upper limit on the number of elements in the dts entry but only
 *		sz_min will be read.
 *
 * Search for a property in a device yesde and read 16-bit value(s) from
 * it. Returns number of elements read on success, -EINVAL if the property
 * does yest exist, -ENODATA if property does yest have a value, and -EOVERFLOW
 * if the property data is smaller than sz_min or longer than sz_max.
 *
 * dts entry of array should be like:
 *	property = /bits/ 16 <0x5000 0x6000 0x7000>;
 *
 * The out_values is modified only if a valid u16 value can be decoded.
 */
int of_property_read_variable_u16_array(const struct device_yesde *np,
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
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to return found values.
 * @sz_min:	minimum number of array elements to read
 * @sz_max:	maximum number of array elements to read, if zero there is yes
 *		upper limit on the number of elements in the dts entry but only
 *		sz_min will be read.
 *
 * Search for a property in a device yesde and read 32-bit value(s) from
 * it. Returns number of elements read on success, -EINVAL if the property
 * does yest exist, -ENODATA if property does yest have a value, and -EOVERFLOW
 * if the property data is smaller than sz_min or longer than sz_max.
 *
 * The out_values is modified only if a valid u32 value can be decoded.
 */
int of_property_read_variable_u32_array(const struct device_yesde *np,
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
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_value:	pointer to return value, modified only if return value is 0.
 *
 * Search for a property in a device yesde and read a 64-bit value from
 * it. Returns 0 on success, -EINVAL if the property does yest exist,
 * -ENODATA if property does yest have a value, and -EOVERFLOW if the
 * property data isn't large eyesugh.
 *
 * The out_value is modified only if a valid u64 value can be decoded.
 */
int of_property_read_u64(const struct device_yesde *np, const char *propname,
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
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_values:	pointer to found values.
 * @sz_min:	minimum number of array elements to read
 * @sz_max:	maximum number of array elements to read, if zero there is yes
 *		upper limit on the number of elements in the dts entry but only
 *		sz_min will be read.
 *
 * Search for a property in a device yesde and read 64-bit value(s) from
 * it. Returns number of elements read on success, -EINVAL if the property
 * does yest exist, -ENODATA if property does yest have a value, and -EOVERFLOW
 * if the property data is smaller than sz_min or longer than sz_max.
 *
 * The out_values is modified only if a valid u64 value can be decoded.
 */
int of_property_read_variable_u64_array(const struct device_yesde *np,
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
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_string:	pointer to null terminated return string, modified only if
 *		return value is 0.
 *
 * Search for a property in a device tree yesde and retrieve a null
 * terminated string value (pointer to data, yest a copy). Returns 0 on
 * success, -EINVAL if the property does yest exist, -ENODATA if property
 * does yest have a value, and -EILSEQ if the string is yest null-terminated
 * within the length of the property data.
 *
 * The out_string pointer is modified only if a valid string can be decoded.
 */
int of_property_read_string(const struct device_yesde *np, const char *propname,
				const char **out_string)
{
	const struct property *prop = of_find_property(np, propname, NULL);
	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;
	if (strnlen(prop->value, prop->length) >= prop->length)
		return -EILSEQ;
	*out_string = prop->value;
	return 0;
}
EXPORT_SYMBOL_GPL(of_property_read_string);

/**
 * of_property_match_string() - Find string in a list and return index
 * @np: pointer to yesde containing string list property
 * @propname: string list property name
 * @string: pointer to string to search for in string list
 *
 * This function searches a string list property and returns the index
 * of a specific string value.
 */
int of_property_match_string(const struct device_yesde *np, const char *propname,
			     const char *string)
{
	const struct property *prop = of_find_property(np, propname, NULL);
	size_t l;
	int i;
	const char *p, *end;

	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;

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
	return -ENODATA;
}
EXPORT_SYMBOL_GPL(of_property_match_string);

/**
 * of_property_read_string_helper() - Utility helper for parsing string properties
 * @np:		device yesde from which the property value is to be read.
 * @propname:	name of the property to be searched.
 * @out_strs:	output array of string pointers.
 * @sz:		number of array elements to read.
 * @skip:	Number of strings to skip over at beginning of list.
 *
 * Don't call this function directly. It is a utility helper for the
 * of_property_read_string*() family of functions.
 */
int of_property_read_string_helper(const struct device_yesde *np,
				   const char *propname, const char **out_strs,
				   size_t sz, int skip)
{
	const struct property *prop = of_find_property(np, propname, NULL);
	int l = 0, i = 0;
	const char *p, *end;

	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;
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
	return i <= 0 ? -ENODATA : i;
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
 * of_graph_parse_endpoint() - parse common endpoint yesde properties
 * @yesde: pointer to endpoint device_yesde
 * @endpoint: pointer to the OF endpoint data structure
 *
 * The caller should hold a reference to @yesde.
 */
int of_graph_parse_endpoint(const struct device_yesde *yesde,
			    struct of_endpoint *endpoint)
{
	struct device_yesde *port_yesde = of_get_parent(yesde);

	WARN_ONCE(!port_yesde, "%s(): endpoint %pOF has yes parent yesde\n",
		  __func__, yesde);

	memset(endpoint, 0, sizeof(*endpoint));

	endpoint->local_yesde = yesde;
	/*
	 * It doesn't matter whether the two calls below succeed.
	 * If they don't then the default value 0 is used.
	 */
	of_property_read_u32(port_yesde, "reg", &endpoint->port);
	of_property_read_u32(yesde, "reg", &endpoint->id);

	of_yesde_put(port_yesde);

	return 0;
}
EXPORT_SYMBOL(of_graph_parse_endpoint);

/**
 * of_graph_get_port_by_id() - get the port matching a given id
 * @parent: pointer to the parent device yesde
 * @id: id of the port
 *
 * Return: A 'port' yesde pointer with refcount incremented. The caller
 * has to use of_yesde_put() on it when done.
 */
struct device_yesde *of_graph_get_port_by_id(struct device_yesde *parent, u32 id)
{
	struct device_yesde *yesde, *port;

	yesde = of_get_child_by_name(parent, "ports");
	if (yesde)
		parent = yesde;

	for_each_child_of_yesde(parent, port) {
		u32 port_id = 0;

		if (!of_yesde_name_eq(port, "port"))
			continue;
		of_property_read_u32(port, "reg", &port_id);
		if (id == port_id)
			break;
	}

	of_yesde_put(yesde);

	return port;
}
EXPORT_SYMBOL(of_graph_get_port_by_id);

/**
 * of_graph_get_next_endpoint() - get next endpoint yesde
 * @parent: pointer to the parent device yesde
 * @prev: previous endpoint yesde, or NULL to get first
 *
 * Return: An 'endpoint' yesde pointer with refcount incremented. Refcount
 * of the passed @prev yesde is decremented.
 */
struct device_yesde *of_graph_get_next_endpoint(const struct device_yesde *parent,
					struct device_yesde *prev)
{
	struct device_yesde *endpoint;
	struct device_yesde *port;

	if (!parent)
		return NULL;

	/*
	 * Start by locating the port yesde. If yes previous endpoint is specified
	 * search for the first port yesde, otherwise get the previous endpoint
	 * parent port yesde.
	 */
	if (!prev) {
		struct device_yesde *yesde;

		yesde = of_get_child_by_name(parent, "ports");
		if (yesde)
			parent = yesde;

		port = of_get_child_by_name(parent, "port");
		of_yesde_put(yesde);

		if (!port) {
			pr_err("graph: yes port yesde found in %pOF\n", parent);
			return NULL;
		}
	} else {
		port = of_get_parent(prev);
		if (WARN_ONCE(!port, "%s(): endpoint %pOF has yes parent yesde\n",
			      __func__, prev))
			return NULL;
	}

	while (1) {
		/*
		 * Now that we have a port yesde, get the next endpoint by
		 * getting the next child. If the previous endpoint is NULL this
		 * will return the first child.
		 */
		endpoint = of_get_next_child(port, prev);
		if (endpoint) {
			of_yesde_put(port);
			return endpoint;
		}

		/* No more endpoints under this port, try the next one. */
		prev = NULL;

		do {
			port = of_get_next_child(parent, port);
			if (!port)
				return NULL;
		} while (!of_yesde_name_eq(port, "port"));
	}
}
EXPORT_SYMBOL(of_graph_get_next_endpoint);

/**
 * of_graph_get_endpoint_by_regs() - get endpoint yesde of specific identifiers
 * @parent: pointer to the parent device yesde
 * @port_reg: identifier (value of reg property) of the parent port yesde
 * @reg: identifier (value of reg property) of the endpoint yesde
 *
 * Return: An 'endpoint' yesde pointer which is identified by reg and at the same
 * is the child of a port yesde identified by port_reg. reg and port_reg are
 * igyesred when they are -1. Use of_yesde_put() on the pointer when done.
 */
struct device_yesde *of_graph_get_endpoint_by_regs(
	const struct device_yesde *parent, int port_reg, int reg)
{
	struct of_endpoint endpoint;
	struct device_yesde *yesde = NULL;

	for_each_endpoint_of_yesde(parent, yesde) {
		of_graph_parse_endpoint(yesde, &endpoint);
		if (((port_reg == -1) || (endpoint.port == port_reg)) &&
			((reg == -1) || (endpoint.id == reg)))
			return yesde;
	}

	return NULL;
}
EXPORT_SYMBOL(of_graph_get_endpoint_by_regs);

/**
 * of_graph_get_remote_endpoint() - get remote endpoint yesde
 * @yesde: pointer to a local endpoint device_yesde
 *
 * Return: Remote endpoint yesde associated with remote endpoint yesde linked
 *	   to @yesde. Use of_yesde_put() on it when done.
 */
struct device_yesde *of_graph_get_remote_endpoint(const struct device_yesde *yesde)
{
	/* Get remote endpoint yesde. */
	return of_parse_phandle(yesde, "remote-endpoint", 0);
}
EXPORT_SYMBOL(of_graph_get_remote_endpoint);

/**
 * of_graph_get_port_parent() - get port's parent yesde
 * @yesde: pointer to a local endpoint device_yesde
 *
 * Return: device yesde associated with endpoint yesde linked
 *	   to @yesde. Use of_yesde_put() on it when done.
 */
struct device_yesde *of_graph_get_port_parent(struct device_yesde *yesde)
{
	unsigned int depth;

	if (!yesde)
		return NULL;

	/*
	 * Preserve usecount for passed in yesde as of_get_next_parent()
	 * will do of_yesde_put() on it.
	 */
	of_yesde_get(yesde);

	/* Walk 3 levels up only if there is 'ports' yesde. */
	for (depth = 3; depth && yesde; depth--) {
		yesde = of_get_next_parent(yesde);
		if (depth == 2 && !of_yesde_name_eq(yesde, "ports"))
			break;
	}
	return yesde;
}
EXPORT_SYMBOL(of_graph_get_port_parent);

/**
 * of_graph_get_remote_port_parent() - get remote port's parent yesde
 * @yesde: pointer to a local endpoint device_yesde
 *
 * Return: Remote device yesde associated with remote endpoint yesde linked
 *	   to @yesde. Use of_yesde_put() on it when done.
 */
struct device_yesde *of_graph_get_remote_port_parent(
			       const struct device_yesde *yesde)
{
	struct device_yesde *np, *pp;

	/* Get remote endpoint yesde. */
	np = of_graph_get_remote_endpoint(yesde);

	pp = of_graph_get_port_parent(np);

	of_yesde_put(np);

	return pp;
}
EXPORT_SYMBOL(of_graph_get_remote_port_parent);

/**
 * of_graph_get_remote_port() - get remote port yesde
 * @yesde: pointer to a local endpoint device_yesde
 *
 * Return: Remote port yesde associated with remote endpoint yesde linked
 *	   to @yesde. Use of_yesde_put() on it when done.
 */
struct device_yesde *of_graph_get_remote_port(const struct device_yesde *yesde)
{
	struct device_yesde *np;

	/* Get remote endpoint yesde. */
	np = of_graph_get_remote_endpoint(yesde);
	if (!np)
		return NULL;
	return of_get_next_parent(np);
}
EXPORT_SYMBOL(of_graph_get_remote_port);

int of_graph_get_endpoint_count(const struct device_yesde *np)
{
	struct device_yesde *endpoint;
	int num = 0;

	for_each_endpoint_of_yesde(np, endpoint)
		num++;

	return num;
}
EXPORT_SYMBOL(of_graph_get_endpoint_count);

/**
 * of_graph_get_remote_yesde() - get remote parent device_yesde for given port/endpoint
 * @yesde: pointer to parent device_yesde containing graph port/endpoint
 * @port: identifier (value of reg property) of the parent port yesde
 * @endpoint: identifier (value of reg property) of the endpoint yesde
 *
 * Return: Remote device yesde associated with remote endpoint yesde linked
 *	   to @yesde. Use of_yesde_put() on it when done.
 */
struct device_yesde *of_graph_get_remote_yesde(const struct device_yesde *yesde,
					     u32 port, u32 endpoint)
{
	struct device_yesde *endpoint_yesde, *remote;

	endpoint_yesde = of_graph_get_endpoint_by_regs(yesde, port, endpoint);
	if (!endpoint_yesde) {
		pr_debug("yes valid endpoint (%d, %d) for yesde %pOF\n",
			 port, endpoint, yesde);
		return NULL;
	}

	remote = of_graph_get_remote_port_parent(endpoint_yesde);
	of_yesde_put(endpoint_yesde);
	if (!remote) {
		pr_debug("yes valid remote yesde\n");
		return NULL;
	}

	if (!of_device_is_available(remote)) {
		pr_debug("yest available for remote yesde\n");
		of_yesde_put(remote);
		return NULL;
	}

	return remote;
}
EXPORT_SYMBOL(of_graph_get_remote_yesde);

static struct fwyesde_handle *of_fwyesde_get(struct fwyesde_handle *fwyesde)
{
	return of_fwyesde_handle(of_yesde_get(to_of_yesde(fwyesde)));
}

static void of_fwyesde_put(struct fwyesde_handle *fwyesde)
{
	of_yesde_put(to_of_yesde(fwyesde));
}

static bool of_fwyesde_device_is_available(const struct fwyesde_handle *fwyesde)
{
	return of_device_is_available(to_of_yesde(fwyesde));
}

static bool of_fwyesde_property_present(const struct fwyesde_handle *fwyesde,
				       const char *propname)
{
	return of_property_read_bool(to_of_yesde(fwyesde), propname);
}

static int of_fwyesde_property_read_int_array(const struct fwyesde_handle *fwyesde,
					     const char *propname,
					     unsigned int elem_size, void *val,
					     size_t nval)
{
	const struct device_yesde *yesde = to_of_yesde(fwyesde);

	if (!val)
		return of_property_count_elems_of_size(yesde, propname,
						       elem_size);

	switch (elem_size) {
	case sizeof(u8):
		return of_property_read_u8_array(yesde, propname, val, nval);
	case sizeof(u16):
		return of_property_read_u16_array(yesde, propname, val, nval);
	case sizeof(u32):
		return of_property_read_u32_array(yesde, propname, val, nval);
	case sizeof(u64):
		return of_property_read_u64_array(yesde, propname, val, nval);
	}

	return -ENXIO;
}

static int
of_fwyesde_property_read_string_array(const struct fwyesde_handle *fwyesde,
				     const char *propname, const char **val,
				     size_t nval)
{
	const struct device_yesde *yesde = to_of_yesde(fwyesde);

	return val ?
		of_property_read_string_array(yesde, propname, val, nval) :
		of_property_count_strings(yesde, propname);
}

static const char *of_fwyesde_get_name(const struct fwyesde_handle *fwyesde)
{
	return kbasename(to_of_yesde(fwyesde)->full_name);
}

static const char *of_fwyesde_get_name_prefix(const struct fwyesde_handle *fwyesde)
{
	/* Root needs yes prefix here (its name is "/"). */
	if (!to_of_yesde(fwyesde)->parent)
		return "";

	return "/";
}

static struct fwyesde_handle *
of_fwyesde_get_parent(const struct fwyesde_handle *fwyesde)
{
	return of_fwyesde_handle(of_get_parent(to_of_yesde(fwyesde)));
}

static struct fwyesde_handle *
of_fwyesde_get_next_child_yesde(const struct fwyesde_handle *fwyesde,
			      struct fwyesde_handle *child)
{
	return of_fwyesde_handle(of_get_next_available_child(to_of_yesde(fwyesde),
							    to_of_yesde(child)));
}

static struct fwyesde_handle *
of_fwyesde_get_named_child_yesde(const struct fwyesde_handle *fwyesde,
			       const char *childname)
{
	const struct device_yesde *yesde = to_of_yesde(fwyesde);
	struct device_yesde *child;

	for_each_available_child_of_yesde(yesde, child)
		if (of_yesde_name_eq(child, childname))
			return of_fwyesde_handle(child);

	return NULL;
}

static int
of_fwyesde_get_reference_args(const struct fwyesde_handle *fwyesde,
			     const char *prop, const char *nargs_prop,
			     unsigned int nargs, unsigned int index,
			     struct fwyesde_reference_args *args)
{
	struct of_phandle_args of_args;
	unsigned int i;
	int ret;

	if (nargs_prop)
		ret = of_parse_phandle_with_args(to_of_yesde(fwyesde), prop,
						 nargs_prop, index, &of_args);
	else
		ret = of_parse_phandle_with_fixed_args(to_of_yesde(fwyesde), prop,
						       nargs, index, &of_args);
	if (ret < 0)
		return ret;
	if (!args)
		return 0;

	args->nargs = of_args.args_count;
	args->fwyesde = of_fwyesde_handle(of_args.np);

	for (i = 0; i < NR_FWNODE_REFERENCE_ARGS; i++)
		args->args[i] = i < of_args.args_count ? of_args.args[i] : 0;

	return 0;
}

static struct fwyesde_handle *
of_fwyesde_graph_get_next_endpoint(const struct fwyesde_handle *fwyesde,
				  struct fwyesde_handle *prev)
{
	return of_fwyesde_handle(of_graph_get_next_endpoint(to_of_yesde(fwyesde),
							   to_of_yesde(prev)));
}

static struct fwyesde_handle *
of_fwyesde_graph_get_remote_endpoint(const struct fwyesde_handle *fwyesde)
{
	return of_fwyesde_handle(
		of_graph_get_remote_endpoint(to_of_yesde(fwyesde)));
}

static struct fwyesde_handle *
of_fwyesde_graph_get_port_parent(struct fwyesde_handle *fwyesde)
{
	struct device_yesde *np;

	/* Get the parent of the port */
	np = of_get_parent(to_of_yesde(fwyesde));
	if (!np)
		return NULL;

	/* Is this the "ports" yesde? If yest, it's the port parent. */
	if (!of_yesde_name_eq(np, "ports"))
		return of_fwyesde_handle(np);

	return of_fwyesde_handle(of_get_next_parent(np));
}

static int of_fwyesde_graph_parse_endpoint(const struct fwyesde_handle *fwyesde,
					  struct fwyesde_endpoint *endpoint)
{
	const struct device_yesde *yesde = to_of_yesde(fwyesde);
	struct device_yesde *port_yesde = of_get_parent(yesde);

	endpoint->local_fwyesde = fwyesde;

	of_property_read_u32(port_yesde, "reg", &endpoint->port);
	of_property_read_u32(yesde, "reg", &endpoint->id);

	of_yesde_put(port_yesde);

	return 0;
}

static const void *
of_fwyesde_device_get_match_data(const struct fwyesde_handle *fwyesde,
				const struct device *dev)
{
	return of_device_get_match_data(dev);
}

static bool of_is_ancestor_of(struct device_yesde *test_ancestor,
			      struct device_yesde *child)
{
	of_yesde_get(child);
	while (child) {
		if (child == test_ancestor) {
			of_yesde_put(child);
			return true;
		}
		child = of_get_next_parent(child);
	}
	return false;
}

/**
 * of_link_to_phandle - Add device link to supplier from supplier phandle
 * @dev: consumer device
 * @sup_np: phandle to supplier device tree yesde
 *
 * Given a phandle to a supplier device tree yesde (@sup_np), this function
 * finds the device that owns the supplier device tree yesde and creates a
 * device link from @dev consumer device to the supplier device. This function
 * doesn't create device links for invalid scenarios such as trying to create a
 * link with a parent device as the consumer of its child device. In such
 * cases, it returns an error.
 *
 * Returns:
 * - 0 if link successfully created to supplier
 * - -EAGAIN if linking to the supplier should be reattempted
 * - -EINVAL if the supplier link is invalid and should yest be created
 * - -ENODEV if there is yes device that corresponds to the supplier phandle
 */
static int of_link_to_phandle(struct device *dev, struct device_yesde *sup_np,
			      u32 dl_flags)
{
	struct device *sup_dev;
	int ret = 0;
	struct device_yesde *tmp_np = sup_np;
	int is_populated;

	of_yesde_get(sup_np);
	/*
	 * Find the device yesde that contains the supplier phandle.  It may be
	 * @sup_np or it may be an ancestor of @sup_np.
	 */
	while (sup_np && !of_find_property(sup_np, "compatible", NULL))
		sup_np = of_get_next_parent(sup_np);
	if (!sup_np) {
		dev_dbg(dev, "Not linking to %pOFP - No device\n", tmp_np);
		return -ENODEV;
	}

	/*
	 * Don't allow linking a device yesde as a consumer of one of its
	 * descendant yesdes. By definition, a child yesde can't be a functional
	 * dependency for the parent yesde.
	 */
	if (of_is_ancestor_of(dev->of_yesde, sup_np)) {
		dev_dbg(dev, "Not linking to %pOFP - is descendant\n", sup_np);
		of_yesde_put(sup_np);
		return -EINVAL;
	}
	sup_dev = get_dev_from_fwyesde(&sup_np->fwyesde);
	is_populated = of_yesde_check_flag(sup_np, OF_POPULATED);
	of_yesde_put(sup_np);
	if (!sup_dev && is_populated) {
		/* Early device without struct device. */
		dev_dbg(dev, "Not linking to %pOFP - No struct device\n",
			sup_np);
		return -ENODEV;
	} else if (!sup_dev) {
		return -EAGAIN;
	}
	if (!device_link_add(dev, sup_dev, dl_flags))
		ret = -EAGAIN;
	put_device(sup_dev);
	return ret;
}

/**
 * parse_prop_cells - Property parsing function for suppliers
 *
 * @np:		Pointer to device tree yesde containing a list
 * @prop_name:	Name of property to be parsed. Expected to hold phandle values
 * @index:	For properties holding a list of phandles, this is the index
 *		into the list.
 * @list_name:	Property name that is kyeswn to contain list of phandle(s) to
 *		supplier(s)
 * @cells_name:	property name that specifies phandles' arguments count
 *
 * This is a helper function to parse properties that have a kyeswn fixed name
 * and are a list of phandles and phandle arguments.
 *
 * Returns:
 * - phandle yesde pointer with refcount incremented. Caller must of_yesde_put()
 *   on it when done.
 * - NULL if yes phandle found at index
 */
static struct device_yesde *parse_prop_cells(struct device_yesde *np,
					    const char *prop_name, int index,
					    const char *list_name,
					    const char *cells_name)
{
	struct of_phandle_args sup_args;

	if (strcmp(prop_name, list_name))
		return NULL;

	if (of_parse_phandle_with_args(np, list_name, cells_name, index,
				       &sup_args))
		return NULL;

	return sup_args.np;
}

#define DEFINE_SIMPLE_PROP(fname, name, cells)				  \
static struct device_yesde *parse_##fname(struct device_yesde *np,	  \
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
 * @np:		Pointer to device tree yesde containing a list
 * @prop_name:	Name of property to be parsed. Expected to hold phandle values
 * @index:	For properties holding a list of phandles, this is the index
 *		into the list.
 * @suffix:	Property suffix that is kyeswn to contain list of phandle(s) to
 *		supplier(s)
 * @cells_name:	property name that specifies phandles' arguments count
 *
 * This is a helper function to parse properties that have a kyeswn fixed suffix
 * and are a list of phandles and phandle arguments.
 *
 * Returns:
 * - phandle yesde pointer with refcount incremented. Caller must of_yesde_put()
 *   on it when done.
 * - NULL if yes phandle found at index
 */
static struct device_yesde *parse_suffix_prop_cells(struct device_yesde *np,
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
static struct device_yesde *parse_##fname(struct device_yesde *np,	     \
					const char *prop_name, int index)    \
{									     \
	return parse_suffix_prop_cells(np, prop_name, index, suffix, cells); \
}

/**
 * struct supplier_bindings - Property parsing functions for suppliers
 *
 * @parse_prop: function name
 *	parse_prop() finds the yesde corresponding to a supplier phandle
 * @parse_prop.np: Pointer to device yesde holding supplier phandle property
 * @parse_prop.prop_name: Name of property holding a phandle value
 * @parse_prop.index: For properties holding a list of phandles, this is the
 *		      index into the list
 *
 * Returns:
 * parse_prop() return values are
 * - phandle yesde pointer with refcount incremented. Caller must of_yesde_put()
 *   on it when done.
 * - NULL if yes phandle found at index
 */
struct supplier_bindings {
	struct device_yesde *(*parse_prop)(struct device_yesde *np,
					  const char *prop_name, int index);
};

DEFINE_SIMPLE_PROP(clocks, "clocks", "#clock-cells")
DEFINE_SIMPLE_PROP(interconnects, "interconnects", "#interconnect-cells")
DEFINE_SIMPLE_PROP(iommus, "iommus", "#iommu-cells")
DEFINE_SIMPLE_PROP(mboxes, "mboxes", "#mbox-cells")
DEFINE_SIMPLE_PROP(io_channels, "io-channel", "#io-channel-cells")
DEFINE_SIMPLE_PROP(interrupt_parent, "interrupt-parent", NULL)
DEFINE_SIMPLE_PROP(dmas, "dmas", "#dma-cells")
DEFINE_SUFFIX_PROP(regulators, "-supply", NULL)
DEFINE_SUFFIX_PROP(gpio, "-gpio", "#gpio-cells")
DEFINE_SUFFIX_PROP(gpios, "-gpios", "#gpio-cells")

static struct device_yesde *parse_iommu_maps(struct device_yesde *np,
					    const char *prop_name, int index)
{
	if (strcmp(prop_name, "iommu-map"))
		return NULL;

	return of_parse_phandle(np, prop_name, (index * 4) + 1);
}

static const struct supplier_bindings of_supplier_bindings[] = {
	{ .parse_prop = parse_clocks, },
	{ .parse_prop = parse_interconnects, },
	{ .parse_prop = parse_iommus, },
	{ .parse_prop = parse_iommu_maps, },
	{ .parse_prop = parse_mboxes, },
	{ .parse_prop = parse_io_channels, },
	{ .parse_prop = parse_interrupt_parent, },
	{ .parse_prop = parse_dmas, },
	{ .parse_prop = parse_regulators, },
	{ .parse_prop = parse_gpio, },
	{ .parse_prop = parse_gpios, },
	{}
};

/**
 * of_link_property - Create device links to suppliers listed in a property
 * @dev: Consumer device
 * @con_np: The consumer device tree yesde which contains the property
 * @prop_name: Name of property to be parsed
 *
 * This function checks if the property @prop_name that is present in the
 * @con_np device tree yesde is one of the kyeswn common device tree bindings
 * that list phandles to suppliers. If @prop_name isn't one, this function
 * doesn't do anything.
 *
 * If @prop_name is one, this function attempts to create device links from the
 * consumer device @dev to all the devices of the suppliers listed in
 * @prop_name.
 *
 * Any failed attempt to create a device link will NOT result in an immediate
 * return.  of_link_property() must create links to all the available supplier
 * devices even when attempts to create a link to one or more suppliers fail.
 */
static int of_link_property(struct device *dev, struct device_yesde *con_np,
			     const char *prop_name)
{
	struct device_yesde *phandle;
	const struct supplier_bindings *s = of_supplier_bindings;
	unsigned int i = 0;
	bool matched = false;
	int ret = 0;
	u32 dl_flags;

	if (dev->of_yesde == con_np)
		dl_flags = DL_FLAG_AUTOPROBE_CONSUMER;
	else
		dl_flags = DL_FLAG_SYNC_STATE_ONLY;

	/* Do yest stop at first failed link, link all available suppliers. */
	while (!matched && s->parse_prop) {
		while ((phandle = s->parse_prop(con_np, prop_name, i))) {
			matched = true;
			i++;
			if (of_link_to_phandle(dev, phandle, dl_flags)
								== -EAGAIN)
				ret = -EAGAIN;
			of_yesde_put(phandle);
		}
		s++;
	}
	return ret;
}

static int of_link_to_suppliers(struct device *dev,
				  struct device_yesde *con_np)
{
	struct device_yesde *child;
	struct property *p;
	int ret = 0;

	for_each_property_of_yesde(con_np, p)
		if (of_link_property(dev, con_np, p->name))
			ret = -ENODEV;

	for_each_child_of_yesde(con_np, child)
		if (of_link_to_suppliers(dev, child) && !ret)
			ret = -EAGAIN;

	return ret;
}

static bool of_devlink;
core_param(of_devlink, of_devlink, bool, 0);

static int of_fwyesde_add_links(const struct fwyesde_handle *fwyesde,
			       struct device *dev)
{
	if (!of_devlink)
		return 0;

	if (unlikely(!is_of_yesde(fwyesde)))
		return 0;

	return of_link_to_suppliers(dev, to_of_yesde(fwyesde));
}

const struct fwyesde_operations of_fwyesde_ops = {
	.get = of_fwyesde_get,
	.put = of_fwyesde_put,
	.device_is_available = of_fwyesde_device_is_available,
	.device_get_match_data = of_fwyesde_device_get_match_data,
	.property_present = of_fwyesde_property_present,
	.property_read_int_array = of_fwyesde_property_read_int_array,
	.property_read_string_array = of_fwyesde_property_read_string_array,
	.get_name = of_fwyesde_get_name,
	.get_name_prefix = of_fwyesde_get_name_prefix,
	.get_parent = of_fwyesde_get_parent,
	.get_next_child_yesde = of_fwyesde_get_next_child_yesde,
	.get_named_child_yesde = of_fwyesde_get_named_child_yesde,
	.get_reference_args = of_fwyesde_get_reference_args,
	.graph_get_next_endpoint = of_fwyesde_graph_get_next_endpoint,
	.graph_get_remote_endpoint = of_fwyesde_graph_get_remote_endpoint,
	.graph_get_port_parent = of_fwyesde_graph_get_port_parent,
	.graph_parse_endpoint = of_fwyesde_graph_parse_endpoint,
	.add_links = of_fwyesde_add_links,
};
EXPORT_SYMBOL_GPL(of_fwyesde_ops);
