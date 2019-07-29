// SPDX-License-Identifier: GPL-2.0+
/* vim: set ts=8 sw=8 noet tw=80 nowrap: */
/*
 *  comedi/drivers/ni_routes.c
 *  Route information for NI boards.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bsearch.h>
#include <linux/sort.h>

#include "../comedi.h"

#include "ni_routes.h"
#include "ni_routing/ni_route_values.h"
#include "ni_routing/ni_device_routes.h"

/*
 * This is defined in ni_routing/ni_route_values.h:
 * #define B(x)	((x) - NI_NAMES_BASE)
 */

/*
 * These are defined in ni_routing/ni_route_values.h to identify clearly
 * elements of the table that were set.  In other words, entries that are zero
 * are invalid.  To get the value to use for the register, one must mask out the
 * high bit.
 *
 * #define V(x)	((x) | 0x80)
 *
 * #define UNMARK(x)	((x) & (~(0x80)))
 *
 */

/* Helper for accessing data. */
#define RVi(table, src, dest)	((table)[(dest) * NI_NUM_NAMES + (src)])

static const size_t route_table_size = NI_NUM_NAMES * NI_NUM_NAMES;

/*
 * Find the proper route_values and ni_device_routes tables for this particular
 * device.
 *
 * Return: -ENODATA if either was not found; 0 if both were found.
 */
static int ni_find_device_routes(const char *device_family,
				 const char *board_name,
				 struct ni_route_tables *tables)
{
	const struct ni_device_routes *dr = NULL;
	const u8 *rv = NULL;
	int i;

	/* First, find the register_values table for this device family */
	for (i = 0; ni_all_route_values[i]; ++i) {
		if (memcmp(ni_all_route_values[i]->family, device_family,
			   strnlen(device_family, 30)) == 0) {
			rv = &ni_all_route_values[i]->register_values[0][0];
			break;
		}
	}

	if (!rv)
		return -ENODATA;

	/* Second, find the set of routes valid for this device. */
	for (i = 0; ni_device_routes_list[i]; ++i) {
		if (memcmp(ni_device_routes_list[i]->device, board_name,
			   strnlen(board_name, 30)) == 0) {
			dr = ni_device_routes_list[i];
			break;
		}
	}

	if (!dr)
		return -ENODATA;

	tables->route_values = rv;
	tables->valid_routes = dr;

	return 0;
}

/**
 * ni_assign_device_routes() - Assign the proper lookup table for NI signal
 *			       routing to the specified NI device.
 *
 * Return: -ENODATA if assignment was not successful; 0 if successful.
 */
int ni_assign_device_routes(const char *device_family,
			    const char *board_name,
			    struct ni_route_tables *tables)
{
	memset(tables, 0, sizeof(struct ni_route_tables));
	return ni_find_device_routes(device_family, board_name, tables);
}
EXPORT_SYMBOL_GPL(ni_assign_device_routes);

/**
 * ni_count_valid_routes() - Count the number of valid routes.
 * @tables: Routing tables for which to count all valid routes.
 */
unsigned int ni_count_valid_routes(const struct ni_route_tables *tables)
{
	int total = 0;
	int i;

	for (i = 0; i < tables->valid_routes->n_route_sets; ++i) {
		const struct ni_route_set *R = &tables->valid_routes->routes[i];
		int j;

		for (j = 0; j < R->n_src; ++j) {
			const int src  = R->src[j];
			const int dest = R->dest;
			const u8 *rv = tables->route_values;

			if (RVi(rv, B(src), B(dest)))
				/* direct routing is valid */
				++total;
			else if (channel_is_rtsi(dest) &&
				 (RVi(rv, B(src), B(NI_RGOUT0)) ||
				  RVi(rv, B(src), B(NI_RTSI_BRD(0))) ||
				  RVi(rv, B(src), B(NI_RTSI_BRD(1))) ||
				  RVi(rv, B(src), B(NI_RTSI_BRD(2))) ||
				  RVi(rv, B(src), B(NI_RTSI_BRD(3))))) {
				++total;
			}
		}
	}
	return total;
}
EXPORT_SYMBOL_GPL(ni_count_valid_routes);

/**
 * ni_get_valid_routes() - Implements INSN_DEVICE_CONFIG_GET_ROUTES.
 * @tables:	pointer to relevant set of routing tables.
 * @n_pairs:	Number of pairs for which memory is allocated by the user.  If
 *		the user specifies '0', only the number of available pairs is
 *		returned.
 * @pair_data:	Pointer to memory allocated to return pairs back to user.  Each
 *		even, odd indexed member of this array will hold source,
 *		destination of a route pair respectively.
 *
 * Return: the number of valid routes if n_pairs == 0; otherwise, the number of
 *	valid routes copied.
 */
unsigned int ni_get_valid_routes(const struct ni_route_tables *tables,
				 unsigned int n_pairs,
				 unsigned int *pair_data)
{
	unsigned int n_valid = ni_count_valid_routes(tables);
	int i;

	if (n_pairs == 0 || n_valid == 0)
		return n_valid;

	if (!pair_data)
		return 0;

	n_valid = 0;

	for (i = 0; i < tables->valid_routes->n_route_sets; ++i) {
		const struct ni_route_set *R = &tables->valid_routes->routes[i];
		int j;

		for (j = 0; j < R->n_src; ++j) {
			const int src  = R->src[j];
			const int dest = R->dest;
			bool valid = false;
			const u8 *rv = tables->route_values;

			if (RVi(rv, B(src), B(dest)))
				/* direct routing is valid */
				valid = true;
			else if (channel_is_rtsi(dest) &&
				 (RVi(rv, B(src), B(NI_RGOUT0)) ||
				  RVi(rv, B(src), B(NI_RTSI_BRD(0))) ||
				  RVi(rv, B(src), B(NI_RTSI_BRD(1))) ||
				  RVi(rv, B(src), B(NI_RTSI_BRD(2))) ||
				  RVi(rv, B(src), B(NI_RTSI_BRD(3))))) {
				/* indirect routing also valid */
				valid = true;
			}

			if (valid) {
				pair_data[2 * n_valid] = src;
				pair_data[2 * n_valid + 1] = dest;
				++n_valid;
			}

			if (n_valid >= n_pairs)
				return n_valid;
		}
	}
	return n_valid;
}
EXPORT_SYMBOL_GPL(ni_get_valid_routes);

/**
 * List of NI global signal names that, as destinations, are only routeable
 * indirectly through the *_arg elements of the comedi_cmd structure.
 */
static const int NI_CMD_DESTS[] = {
	NI_AI_SampleClock,
	NI_AI_StartTrigger,
	NI_AI_ConvertClock,
	NI_AO_SampleClock,
	NI_AO_StartTrigger,
	NI_DI_SampleClock,
	NI_DO_SampleClock,
};

/**
 * ni_is_cmd_dest() - Determine whether the given destination is only
 *		      configurable via a comedi_cmd struct.
 * @dest: Destination to test.
 */
bool ni_is_cmd_dest(int dest)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(NI_CMD_DESTS); ++i)
		if (NI_CMD_DESTS[i] == dest)
			return true;
	return false;
}
EXPORT_SYMBOL_GPL(ni_is_cmd_dest);

/* **** BEGIN Routes sort routines **** */
static int _ni_sort_destcmp(const void *va, const void *vb)
{
	const struct ni_route_set *a = va;
	const struct ni_route_set *b = vb;

	if (a->dest < b->dest)
		return -1;
	else if (a->dest > b->dest)
		return 1;
	return 0;
}

static int _ni_sort_srccmp(const void *vsrc0, const void *vsrc1)
{
	const int *src0 = vsrc0;
	const int *src1 = vsrc1;

	if (*src0 < *src1)
		return -1;
	else if (*src0 > *src1)
		return 1;
	return 0;
}

/**
 * ni_sort_device_routes() - Sort the list of valid device signal routes in
 *			     preparation for use.
 * @valid_routes:	pointer to ni_device_routes struct to sort.
 */
void ni_sort_device_routes(struct ni_device_routes *valid_routes)
{
	unsigned int n;

	/* 1. Count and set the number of ni_route_set objects. */
	valid_routes->n_route_sets = 0;
	while (valid_routes->routes[valid_routes->n_route_sets].dest != 0)
		++valid_routes->n_route_sets;

	/* 2. sort all ni_route_set objects by destination. */
	sort(valid_routes->routes, valid_routes->n_route_sets,
	     sizeof(struct ni_route_set), _ni_sort_destcmp, NULL);

	/* 3. Loop through each route_set for sorting. */
	for (n = 0; n < valid_routes->n_route_sets; ++n) {
		struct ni_route_set *rs = &valid_routes->routes[n];

		/* 3a. Count and set the number of sources. */
		rs->n_src = 0;
		while (rs->src[rs->n_src])
			++rs->n_src;

		/* 3a. Sort sources. */
		sort(valid_routes->routes[n].src, valid_routes->routes[n].n_src,
		     sizeof(int), _ni_sort_srccmp, NULL);
	}
}
EXPORT_SYMBOL_GPL(ni_sort_device_routes);

/* sort all valid device signal routes in prep for use */
static void ni_sort_all_device_routes(void)
{
	unsigned int i;

	for (i = 0; ni_device_routes_list[i]; ++i)
		ni_sort_device_routes(ni_device_routes_list[i]);
}

/* **** BEGIN Routes search routines **** */
static int _ni_bsearch_destcmp(const void *vkey, const void *velt)
{
	const int *key = vkey;
	const struct ni_route_set *elt = velt;

	if (*key < elt->dest)
		return -1;
	else if (*key > elt->dest)
		return 1;
	return 0;
}

static int _ni_bsearch_srccmp(const void *vkey, const void *velt)
{
	const int *key = vkey;
	const int *elt = velt;

	if (*key < *elt)
		return -1;
	else if (*key > *elt)
		return 1;
	return 0;
}

/**
 * ni_find_route_set() - Finds the proper route set with the specified
 *			 destination.
 * @destination: Destination of which to search for the route set.
 * @valid_routes: Pointer to device routes within which to search.
 *
 * Return: NULL if no route_set is found with the specified @destination;
 *	otherwise, a pointer to the route_set if found.
 */
const struct ni_route_set *
ni_find_route_set(const int destination,
		  const struct ni_device_routes *valid_routes)
{
	return bsearch(&destination, valid_routes->routes,
		       valid_routes->n_route_sets, sizeof(struct ni_route_set),
		       _ni_bsearch_destcmp);
}
EXPORT_SYMBOL_GPL(ni_find_route_set);

/**
 * ni_route_set_has_source() - Determines whether the given source is in
 *			       included given route_set.
 *
 * Return: true if found; false otherwise.
 */
bool ni_route_set_has_source(const struct ni_route_set *routes,
			     const int source)
{
	if (!bsearch(&source, routes->src, routes->n_src, sizeof(int),
		     _ni_bsearch_srccmp))
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(ni_route_set_has_source);

/**
 * ni_lookup_route_register() - Look up a register value for a particular route
 *				without checking whether the route is valid for
 *				the particular device.
 * @src:	global-identifier for route source
 * @dest:	global-identifier for route destination
 * @tables:	pointer to relevant set of routing tables.
 *
 * Return: -EINVAL if the specified route is not valid for this device family.
 */
s8 ni_lookup_route_register(int src, int dest,
			    const struct ni_route_tables *tables)
{
	s8 regval;

	/*
	 * Be sure to use the B() macro to subtract off the NI_NAMES_BASE before
	 * indexing into the route_values array.
	 */
	src = B(src);
	dest = B(dest);
	if (src < 0 || src >= NI_NUM_NAMES || dest < 0 || dest >= NI_NUM_NAMES)
		return -EINVAL;
	regval = RVi(tables->route_values, src, dest);
	if (!regval)
		return -EINVAL;
	/* mask out the valid-value marking bit */
	return UNMARK(regval);
}
EXPORT_SYMBOL_GPL(ni_lookup_route_register);

/**
 * ni_route_to_register() - Validates and converts the specified signal route
 *			    (src-->dest) to the value used at the appropriate
 *			    register.
 * @src:	global-identifier for route source
 * @dest:	global-identifier for route destination
 * @tables:	pointer to relevant set of routing tables.
 *
 * Generally speaking, most routes require the first six bits and a few require
 * 7 bits.  Special handling is given for the return value when the route is to
 * be handled by the RTSI sub-device.  In this case, the returned register may
 * not be sufficient to define the entire route path, but rather may only
 * indicate the intermediate route.  For example, if the route must go through
 * the RGOUT0 pin, the (src->RGOUT0) register value will be returned.
 * Similarly, if the route must go through the NI_RTSI_BRD lines, the BIT(6)
 * will be set:
 *
 * if route does not need RTSI_BRD lines:
 *   bits 0:7 : register value
 *              for a route that must go through RGOUT0 pin, this will be equal
 *              to the (src->RGOUT0) register value.
 * else: * route is (src->RTSI_BRD(x), RTSI_BRD(x)->TRIGGER_LINE(i)) *
 *   bits 0:5 : zero
 *   bits 6   : set to 1
 *   bits 7:7 : zero
 *
 * Return: register value to be used for source at destination with special
 *	cases given above; Otherwise, -1 if the specified route is not valid for
 *	this particular device.
 */
s8 ni_route_to_register(const int src, const int dest,
			const struct ni_route_tables *tables)
{
	const struct ni_route_set *routes =
		ni_find_route_set(dest, tables->valid_routes);
	const u8 *rv;
	s8 regval;

	/* first check to see if source is listed with bunch of destinations. */
	if (!routes)
		return -1;
	/* 2nd, check to see if destination is in list of source's targets. */
	if (!ni_route_set_has_source(routes, src))
		return -1;
	/*
	 * finally, check to see if we know how to route...
	 * Be sure to use the B() macro to subtract off the NI_NAMES_BASE before
	 * indexing into the route_values array.
	 */
	rv = tables->route_values;
	regval = RVi(rv, B(src), B(dest));

	/*
	 * if we did not validate the route, we'll see if we can route through
	 * one of the muxes
	 */
	if (!regval && channel_is_rtsi(dest)) {
		regval = RVi(rv, B(src), B(NI_RGOUT0));
		if (!regval && (RVi(rv, B(src), B(NI_RTSI_BRD(0))) ||
				RVi(rv, B(src), B(NI_RTSI_BRD(1))) ||
				RVi(rv, B(src), B(NI_RTSI_BRD(2))) ||
				RVi(rv, B(src), B(NI_RTSI_BRD(3)))))
			regval = BIT(6);
	}

	if (!regval)
		return -1;
	/* mask out the valid-value marking bit */
	return UNMARK(regval);
}
EXPORT_SYMBOL_GPL(ni_route_to_register);

/**
 * ni_find_route_source() - Finds the signal source corresponding to a signal
 *			    route (src-->dest) of the specified routing register
 *			    value and the specified route destination on the
 *			    specified device.
 *
 * Note that this function does _not_ validate the source based on device
 * routes.
 *
 * Return: The NI signal value (e.g. NI_PFI(0) or PXI_Clk10) if found.
 *	If the source was not found (i.e. the register value is not
 *	valid for any routes to the destination), -EINVAL is returned.
 */
int ni_find_route_source(const u8 src_sel_reg_value, int dest,
			 const struct ni_route_tables *tables)
{
	int src;

	dest = B(dest); /* subtract NI names offset */
	/* ensure we are not going to under/over run the route value table */
	if (dest < 0 || dest >= NI_NUM_NAMES)
		return -EINVAL;
	for (src = 0; src < NI_NUM_NAMES; ++src)
		if (RVi(tables->route_values, src, dest) ==
		    V(src_sel_reg_value))
			return src + NI_NAMES_BASE;
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(ni_find_route_source);

/* **** END Routes search routines **** */

/* **** BEGIN simple module entry/exit functions **** */
static int __init ni_routes_module_init(void)
{
	ni_sort_all_device_routes();
	return 0;
}

static void __exit ni_routes_module_exit(void)
{
}

module_init(ni_routes_module_init);
module_exit(ni_routes_module_exit);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi helper for routing signals-->terminals for NI");
MODULE_LICENSE("GPL");
/* **** END simple module entry/exit functions **** */
