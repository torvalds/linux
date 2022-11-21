/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  comedi/drivers/ni_routes.h
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

#ifndef _COMEDI_DRIVERS_NI_ROUTES_H
#define _COMEDI_DRIVERS_NI_ROUTES_H

#include <linux/types.h>
#include <linux/errno.h>

#ifndef NI_ROUTE_VALUE_EXTERNAL_CONVERSION
#include <linux/bitops.h>
#endif

#include <linux/comedi.h>

/**
 * struct ni_route_set - Set of destinations with a common source.
 * @dest: Destination of all sources in this route set.
 * @n_src: Number of sources for this route set.
 * @src: List of sources that all map to the same destination.
 */
struct ni_route_set {
	int dest;
	int n_src;
	int *src;
};

/**
 * struct ni_device_routes - List of all src->dest sets for a particular device.
 * @device: Name of board/device (e.g. pxi-6733).
 * @n_route_sets: Number of route sets that are valid for this device.
 * @routes: List of route sets that are valid for this device.
 */
struct ni_device_routes {
	const char *device;
	int n_route_sets;
	struct ni_route_set *routes;
};

/**
 * struct ni_route_tables - Register values and valid routes for a device.
 * @valid_routes: Pointer to a all valid route sets for a single device.
 * @route_values: Pointer to register values for all routes for the family to
 *		  which the device belongs.
 *
 * Link to the valid src->dest routes and the register values used to assign
 * such routes for that particular device.
 */
struct ni_route_tables {
	const struct ni_device_routes *valid_routes;
	const u8 *route_values;
};

/*
 * ni_assign_device_routes() - Assign the proper lookup table for NI signal
 *			       routing to the specified NI device.
 *
 * Return: -ENODATA if assignment was not successful; 0 if successful.
 */
int ni_assign_device_routes(const char *device_family,
			    const char *board_name,
			    const char *alt_board_name,
			    struct ni_route_tables *tables);

/*
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
		  const struct ni_device_routes *valid_routes);

/*
 * ni_route_set_has_source() - Determines whether the given source is in
 *			       included given route_set.
 *
 * Return: true if found; false otherwise.
 */
bool ni_route_set_has_source(const struct ni_route_set *routes, const int src);

/*
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
			const struct ni_route_tables *tables);

static inline bool ni_rtsi_route_requires_mux(s8 value)
{
	return value & BIT(6);
}

/*
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
			    const struct ni_route_tables *tables);

/**
 * route_is_valid() - Determines whether the specified signal route (src-->dest)
 *		      is valid for the given NI comedi_device.
 * @src:	global-identifier for route source
 * @dest:	global-identifier for route destination
 * @tables:	pointer to relevant set of routing tables.
 *
 * Return: True if the route is valid, otherwise false.
 */
static inline bool route_is_valid(const int src, const int dest,
				  const struct ni_route_tables *tables)
{
	return ni_route_to_register(src, dest, tables) >= 0;
}

/*
 * ni_is_cmd_dest() - Determine whether the given destination is only
 *		      configurable via a comedi_cmd struct.
 * @dest: Destination to test.
 */
bool ni_is_cmd_dest(int dest);

static inline bool channel_is_pfi(int channel)
{
	return NI_PFI(0) <= channel && channel <= NI_PFI(-1);
}

static inline bool channel_is_rtsi(int channel)
{
	return TRIGGER_LINE(0) <= channel && channel <= TRIGGER_LINE(-1);
}

static inline bool channel_is_ctr(int channel)
{
	return channel >= NI_COUNTER_NAMES_BASE &&
	       channel <= NI_COUNTER_NAMES_MAX;
}

/*
 * ni_count_valid_routes() - Count the number of valid routes.
 * @tables: Routing tables for which to count all valid routes.
 */
unsigned int ni_count_valid_routes(const struct ni_route_tables *tables);

/*
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
				 unsigned int *pair_data);

/*
 * ni_sort_device_routes() - Sort the list of valid device signal routes in
 *			     preparation for use.
 * @valid_routes:	pointer to ni_device_routes struct to sort.
 */
void ni_sort_device_routes(struct ni_device_routes *valid_routes);

/*
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
int ni_find_route_source(const u8 src_sel_reg_value, const int dest,
			 const struct ni_route_tables *tables);

/**
 * route_register_is_valid() - Determines whether the register value for the
 *			       specified route destination on the specified
 *			       device is valid.
 */
static inline bool route_register_is_valid(const u8 src_sel_reg_value,
					   const int dest,
					   const struct ni_route_tables *tables)
{
	return ni_find_route_source(src_sel_reg_value, dest, tables) >= 0;
}

/**
 * ni_get_reg_value_roffs() - Determines the proper register value for a
 *			      particular valid NI signal/terminal route.
 * @src:	Either a direct register value or one of NI_* signal names.
 * @dest:	global-identifier for route destination
 * @tables:	pointer to relevant set of routing tables.
 * @direct_reg_offset:
 *		Compatibility compensation argument.  This argument allows us to
 *		arbitrarily apply an offset to src if src is a direct register
 *		value reference.  This is necessary to be compatible with
 *		definitions of register values as previously exported directly
 *		to user space.
 *
 * Return: the register value (>0) to be used at the destination if the src is
 *	valid for the given destination; -1 otherwise.
 */
static inline s8 ni_get_reg_value_roffs(int src, const int dest,
					const struct ni_route_tables *tables,
					const int direct_reg_offset)
{
	if (src < NI_NAMES_BASE) {
		src += direct_reg_offset;
		/*
		 * In this case, the src is expected to actually be a register
		 * value.
		 */
		if (route_register_is_valid(src, dest, tables))
			return src;
		return -1;
	}

	/*
	 * Otherwise, the src is expected to be one of the abstracted NI
	 * signal/terminal names.
	 */
	return ni_route_to_register(src, dest, tables);
}

static inline int ni_get_reg_value(const int src, const int dest,
				   const struct ni_route_tables *tables)
{
	return ni_get_reg_value_roffs(src, dest, tables, 0);
}

/**
 * ni_check_trigger_arg_roffs() - Checks the trigger argument (*_arg) of an NI
 *				  device to ensure that the *_arg value
 *				  corresponds to _either_ a valid register value
 *				  to define a trigger source, _or_ a valid NI
 *				  signal/terminal name that has a valid route to
 *				  the destination on the particular device.
 * @src:	Either a direct register value or one of NI_* signal names.
 * @dest:	global-identifier for route destination
 * @tables:	pointer to relevant set of routing tables.
 * @direct_reg_offset:
 *		Compatibility compensation argument.  This argument allows us to
 *		arbitrarily apply an offset to src if src is a direct register
 *		value reference.  This is necessary to be compatible with
 *		definitions of register values as previously exported directly
 *		to user space.
 *
 * Return: 0 if the src (either register value or NI signal/terminal name) is
 *	valid for the destination; -EINVAL otherwise.
 */
static inline
int ni_check_trigger_arg_roffs(int src, const int dest,
			       const struct ni_route_tables *tables,
			       const int direct_reg_offset)
{
	if (ni_get_reg_value_roffs(src, dest, tables, direct_reg_offset) < 0)
		return -EINVAL;
	return 0;
}

static inline int ni_check_trigger_arg(const int src, const int dest,
				       const struct ni_route_tables *tables)
{
	return ni_check_trigger_arg_roffs(src, dest, tables, 0);
}

#endif /* _COMEDI_DRIVERS_NI_ROUTES_H */
