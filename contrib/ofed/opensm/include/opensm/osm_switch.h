/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2009 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 * 	Declaration of osm_switch_t.
 *	This object represents an IBA switch.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_SWITCH_H_
#define _OSM_SWITCH_H_

#include <iba/ib_types.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_node.h>
#include <opensm/osm_port.h>
#include <opensm/osm_mcast_tbl.h>
#include <opensm/osm_port_profile.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Switch
* NAME
*	Switch
*
* DESCRIPTION
*	The Switch object encapsulates the information needed by the
*	OpenSM to manage switches.  The OpenSM allocates one switch object
*	per switch in the IBA subnet.
*
*	The Switch object is not thread safe, thus callers must provide
*	serialization.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Switch/osm_switch_t
* NAME
*	osm_switch_t
*
* DESCRIPTION
*	Switch structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_switch {
	cl_map_item_t map_item;
	osm_node_t *p_node;
	ib_switch_info_t switch_info;
	uint16_t max_lid_ho;
	uint8_t num_ports;
	uint16_t num_hops;
	uint8_t **hops;
	osm_port_profile_t *p_prof;
	uint8_t *search_ordering_ports;
	uint8_t *lft;
	uint8_t *new_lft;
	uint16_t lft_size;
	osm_mcast_tbl_t mcast_tbl;
	int32_t mft_block_num;
	uint32_t mft_position;
	unsigned endport_links;
	unsigned need_update;
	void *priv;
	cl_map_item_t mgrp_item;
	uint32_t num_of_mcm;
	uint8_t is_mc_member;
} osm_switch_t;
/*
* FIELDS
*	map_item
*		Linkage structure for cl_qmap.  MUST BE FIRST MEMBER!
*
*	p_node
*		Pointer to the Node object for this switch.
*
*	switch_info
*		IBA defined SwitchInfo structure for this switch.
*
*	max_lid_ho
*		Max LID that is accessible from this switch.
*
*	num_ports
*		Number of ports for this switch.
*
*	num_hops
*		Size of hops table for this switch.
*
*	hops
*		LID Matrix for this switch containing the hop count
*		to every LID from every port.
*
*	p_prof
*		Pointer to array of Port Profile objects for this switch.
*
*	lft
*		This switch's linear forwarding table.
*
*	new_lft
*		This switch's linear forwarding table, as was
*		calculated by the last routing engine execution.
*
*	mcast_tbl
*		Multicast forwarding table for this switch.
*
*	need_update
*		When set indicates that switch was probably reset, so
*		fwd tables and rest cached data should be flushed
*
*	mgrp_item
*		map item for switch in building mcast tree
*
*	num_of_mcm
*		number of mcast members(ports) connected to switch
*
*	is_mc_member
*		whether switch is a mcast member itself
*
* SEE ALSO
*	Switch object
*********/

/****s* OpenSM: Switch/struct osm_remote_guids_count
* NAME
*	struct osm_remote_guids_count
*
* DESCRIPTION
*	Stores array of pointers to remote node and the numbers of
*	times a switch has forwarded to it.
*
* SYNOPSIS
*/
struct osm_remote_guids_count {
	unsigned count;
	struct osm_remote_node {
		osm_node_t *node;
		unsigned forwarded_to;
		uint8_t port;
	} guids[0];
};
/*
* FIELDS
*	count
*		A number of used entries in array.
*
*	node
*		A pointer to node.
*
*	forwarded_to
*		A count of lids forwarded to this node.
*
*	port
*		Port number on the node.
*********/

/****f* OpenSM: Switch/osm_switch_delete
* NAME
*	osm_switch_delete
*
* DESCRIPTION
*	Destroys and deallocates the object.
*
* SYNOPSIS
*/
void osm_switch_delete(IN OUT osm_switch_t ** pp_sw);
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*	Switch object, osm_switch_new
*********/

/****f* OpenSM: Switch/osm_switch_new
* NAME
*	osm_switch_new
*
* DESCRIPTION
*	The osm_switch_new function initializes a Switch object for use.
*
* SYNOPSIS
*/
osm_switch_t *osm_switch_new(IN osm_node_t * p_node,
			     IN const osm_madw_t * p_madw);
/*
* PARAMETERS
*	p_node
*		[in] Pointer to the node object of this switch
*
*	p_madw
*		[in] Pointer to the MAD Wrapper containing the switch's
*		SwitchInfo attribute.
*
* RETURN VALUES
*	Pointer to the new initialized switch object.
*
* NOTES
*
* SEE ALSO
*	Switch object, osm_switch_delete
*********/

/****f* OpenSM: Switch/osm_switch_get_hop_count
* NAME
*	osm_switch_get_hop_count
*
* DESCRIPTION
*	Returns the hop count at the specified LID/Port intersection.
*
* SYNOPSIS
*/
static inline uint8_t osm_switch_get_hop_count(IN const osm_switch_t * p_sw,
					       IN uint16_t lid_ho,
					       IN uint8_t port_num)
{
	return (lid_ho > p_sw->max_lid_ho || !p_sw->hops[lid_ho]) ?
	    OSM_NO_PATH : p_sw->hops[lid_ho][port_num];
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to a Switch object.
*
*	lid_ho
*		[in] LID value (host order) for which to return the hop count
*
*	port_num
*		[in] Port number in the switch
*
* RETURN VALUES
*	Returns the hop count at the specified LID/Port intersection.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_set_hops
* NAME
*	osm_switch_set_hops
*
* DESCRIPTION
*	Sets the hop count at the specified LID/Port intersection.
*
* SYNOPSIS
*/
cl_status_t osm_switch_set_hops(IN osm_switch_t * p_sw, IN uint16_t lid_ho,
				IN uint8_t port_num, IN uint8_t num_hops);
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to a Switch object.
*
*	lid_ho
*		[in] LID value (host order) for which to set the count.
*
*	port_num
*		[in] port number for which to set the count.
*
*	num_hops
*		[in] value to assign to this entry.
*
* RETURN VALUES
*	Returns 0 if successful. -1 if it failed
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_clear_hops
* NAME
*	osm_switch_clear_hops
*
* DESCRIPTION
*	Cleanup existing hops tables (lid matrix)
*
* SYNOPSIS
*/
void osm_switch_clear_hops(IN osm_switch_t * p_sw);
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to a Switch object.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_get_least_hops
* NAME
*	osm_switch_get_least_hops
*
* DESCRIPTION
*	Returns the number of hops in the short path to this lid from
*	any port on the switch.
*
* SYNOPSIS
*/
static inline uint8_t osm_switch_get_least_hops(IN const osm_switch_t * p_sw,
						IN uint16_t lid_ho)
{
	return (lid_ho > p_sw->max_lid_ho || !p_sw->hops[lid_ho]) ?
	    OSM_NO_PATH : p_sw->hops[lid_ho][0];
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to an osm_switch_t object.
*
*	lid_ho
*		[in] LID (host order) for which to retrieve the shortest hop count.
*
* RETURN VALUES
*	Returns the number of hops in the short path to this lid from
*	any port on the switch.
*
* NOTES
*
* SEE ALSO
*	Switch object
*********/

/****f* OpenSM: Switch/osm_switch_get_port_least_hops
* NAME
*	osm_switch_get_port_least_hops
*
* DESCRIPTION
*	Returns the number of hops in the short path to this port from
*	any port on the switch.
*
* SYNOPSIS
*/
uint8_t osm_switch_get_port_least_hops(IN const osm_switch_t * p_sw,
				       IN const osm_port_t * p_port);
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to an osm_switch_t object.
*
*	p_port
*		[in] Pointer to an osm_port_t object for which to
*		retrieve the shortest hop count.
*
* RETURN VALUES
*	Returns the number of hops in the short path to this lid from
*	any port on the switch.
*
* NOTES
*
* SEE ALSO
*	Switch object
*********/

/****d* OpenSM: osm_lft_type_enum
* NAME
*	osm_lft_type_enum
*
* DESCRIPTION
*	Enumerates LFT sets types of a switch.
*
* SYNOPSIS
*/
typedef enum osm_lft_type_enum {
	OSM_LFT = 0,
	OSM_NEW_LFT
} osm_lft_type_enum;
/***********/

/****f* OpenSM: Switch/osm_switch_get_port_by_lid
* NAME
*	osm_switch_get_port_by_lid
*
* DESCRIPTION
*	Returns the switch port number on which the specified LID is routed.
*
* SYNOPSIS
*/
static inline uint8_t osm_switch_get_port_by_lid(IN const osm_switch_t * p_sw,
						 IN uint16_t lid_ho,
						 IN osm_lft_type_enum lft_enum)
{
	if (lid_ho == 0 || lid_ho > p_sw->max_lid_ho)
		return OSM_NO_PATH;
	return lft_enum == OSM_LFT ? p_sw->lft[lid_ho] : p_sw->new_lft[lid_ho];
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to an osm_switch_t object.
*
*	lid_ho
*		[in] LID (host order) for which to retrieve the shortest hop count.
*
*	lft_enum
*		[in] Use LFT that was calculated by routing engine, or
*		current LFT on the switch.
*
* RETURN VALUES
*	Returns the switch port on which the specified LID is routed.
*
* NOTES
*
* SEE ALSO
*	Switch object
*********/

/****f* OpenSM: Switch/osm_switch_get_route_by_lid
* NAME
*	osm_switch_get_route_by_lid
*
* DESCRIPTION
*	Gets the physical port object that routes the specified LID.
*
* SYNOPSIS
*/
static inline osm_physp_t *osm_switch_get_route_by_lid(IN const osm_switch_t *
						       p_sw, IN ib_net16_t lid)
{
	uint8_t port_num;

	CL_ASSERT(p_sw);
	CL_ASSERT(lid);

	port_num = osm_switch_get_port_by_lid(p_sw, cl_ntoh16(lid),
					      OSM_NEW_LFT);

	/*
	   In order to avoid holes in the subnet (usually happens when
	   running UPDN algorithm), i.e. cases where port is
	   unreachable through a switch (we put an OSM_NO_PATH value at
	   the port entry, we do not assert on unreachable lid entries
	   at the fwd table but return NULL
	 */
	if (port_num != OSM_NO_PATH)
		return (osm_node_get_physp_ptr(p_sw->p_node, port_num));
	else
		return NULL;
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to an osm_switch_t object.
*
*	lid
*		[in] LID for which to find a route.  This must be a unicast
*		LID value < 0xC000.
*
* RETURN VALUES
*	Returns a pointer to the Physical Port Object object that
*	routes the specified LID.  A return value of zero means
*	there is no route for the lid through this switch.
*	The lid value must be a unicast LID.
*
* NOTES
*
* SEE ALSO
*	Switch object
*********/

/****f* OpenSM: Switch/osm_switch_sp0_is_lmc_capable
* NAME
*	osm_switch_sp0_is_lmc_capable
*
* DESCRIPTION
*	Returns whether switch port 0 (SP0) can support LMC
*
*/
static inline unsigned
osm_switch_sp0_is_lmc_capable(IN const osm_switch_t * p_sw,
			      IN osm_subn_t * p_subn)
{
	return (p_subn->opt.lmc_esp0 &&
		ib_switch_info_is_enhanced_port0(&p_sw->switch_info)) ? 1 : 0;
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to an osm_switch_t object.
*
*	p_subn
*		[in] Pointer to an osm_subn_t object.
*
* RETURN VALUES
*	TRUE if SP0 is enhanced and globally enabled. FALSE otherwise.
*
* NOTES
*	This is workaround function, it takes into account user defined
*	p_subn->opt.lmc_esp0 parameter.
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_get_max_block_id_in_use
* NAME
*	osm_switch_get_max_block_id_in_use
*
* DESCRIPTION
*	Returns the maximum block ID (host order) of this switch that
*	is used for unicast routing.
*
* SYNOPSIS
*/
static inline uint16_t
osm_switch_get_max_block_id_in_use(IN const osm_switch_t * p_sw)
{
	return cl_ntoh16(p_sw->switch_info.lin_top) / IB_SMP_DATA_SIZE;
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to an osm_switch_t object.
*
* RETURN VALUES
*	Returns the maximum block ID (host order) of this switch.
*
* NOTES
*
* SEE ALSO
*	Switch object
*********/

/****f* OpenSM: Switch/osm_switch_get_lft_block
* NAME
*	osm_switch_get_lft_block
*
* DESCRIPTION
*	Retrieve a linear forwarding table block.
*
* SYNOPSIS
*/
boolean_t osm_switch_get_lft_block(IN const osm_switch_t * p_sw,
				   IN uint16_t block_id, OUT uint8_t * p_block);
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to an osm_switch_t object.
*
*	block_id
*		[in] The block_id to retrieve.
*
*	p_block
*		[out] Pointer to the 64 byte array to store the
*		forwarding table block specified by block_id.
*
* RETURN VALUES
*	Returns true if there are more blocks necessary to
*	configure all the LIDs reachable from this switch.
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_supports_mcast
* NAME
*	osm_switch_supports_mcast
*
* DESCRIPTION
*	Indicates if a switch supports multicast.
*
* SYNOPSIS
*/
static inline boolean_t osm_switch_supports_mcast(IN const osm_switch_t * p_sw)
{
	return (p_sw->switch_info.mcast_cap != 0);
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to an osm_switch_t object.
*
* RETURN VALUES
*	Returns TRUE if the switch supports multicast.
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_set_switch_info
* NAME
*	osm_switch_set_switch_info
*
* DESCRIPTION
*	Updates the switch info attribute of this switch.
*
* SYNOPSIS
*/
static inline void osm_switch_set_switch_info(IN osm_switch_t * p_sw,
					      IN const ib_switch_info_t * p_si)
{
	CL_ASSERT(p_sw);
	CL_ASSERT(p_si);
	p_sw->switch_info = *p_si;
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to a Switch object.
*
*	p_si
*		[in] Pointer to the SwitchInfo attribute for this switch.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_count_path
* NAME
*	osm_switch_count_path
*
* DESCRIPTION
*	Counts this path in port profile.
*
* SYNOPSIS
*/
static inline void osm_switch_count_path(IN osm_switch_t * p_sw,
					 IN uint8_t port)
{
	osm_port_prof_path_count_inc(&p_sw->p_prof[port]);
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch object.
*
*	port
*		[in] Port to count path.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_set_lft_block
* NAME
*	osm_switch_set_lft_block
*
* DESCRIPTION
*	Copies in the specified block into
*	the switch's Linear Forwarding Table.
*
* SYNOPSIS
*/
static inline ib_api_status_t
osm_switch_set_lft_block(IN osm_switch_t * p_sw, IN const uint8_t * p_block,
			 IN uint32_t block_num)
{
	uint16_t lid_start =
		(uint16_t) (block_num * IB_SMP_DATA_SIZE);
	CL_ASSERT(p_sw);

	if (lid_start + IB_SMP_DATA_SIZE > p_sw->lft_size)
		return IB_INVALID_PARAMETER;

	memcpy(&p_sw->lft[lid_start], p_block, IB_SMP_DATA_SIZE);
	return IB_SUCCESS;
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch object.
*
*	p_block
*		[in] Pointer to the forwarding table block.
*
*	block_num
*		[in] Block number for this block
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_set_mft_block
* NAME
*	osm_switch_set_mft_block
*
* DESCRIPTION
*	Sets a block of multicast port masks into the multicast table.
*
* SYNOPSIS
*/
static inline ib_api_status_t
osm_switch_set_mft_block(IN osm_switch_t * p_sw, IN const ib_net16_t * p_block,
			 IN uint16_t block_num, IN uint8_t position)
{
	CL_ASSERT(p_sw);
	return osm_mcast_tbl_set_block(&p_sw->mcast_tbl, p_block, block_num,
				       position);
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch object.
*
*	p_block
*		[in] Pointer to the block of port masks to set.
*
*	block_num
*		[in] Block number (0-511) to set.
*
*	position
*		[in] Port mask position (0-15) to set.
*
* RETURN VALUE
*	IB_SUCCESS on success.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_get_mft_block
* NAME
*	osm_switch_get_mft_block
*
* DESCRIPTION
*	Retrieve a block of multicast port masks from the multicast table.
*
* SYNOPSIS
*/
static inline boolean_t osm_switch_get_mft_block(IN osm_switch_t * p_sw,
						 IN uint16_t block_num,
						 IN uint8_t position,
						 OUT ib_net16_t * p_block)
{
	CL_ASSERT(p_sw);
	return osm_mcast_tbl_get_block(&p_sw->mcast_tbl, block_num, position,
				       p_block);
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch object.
*
*	block_num
*		[in] Block number (0-511) to set.
*
*	position
*		[in] Port mask position (0-15) to set.
*
*	p_block
*		[out] Pointer to the block of port masks stored.
*
* RETURN VALUES
*	Returns true if there are more blocks necessary to
*	configure all the MLIDs reachable from this switch.
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_get_mft_max_block
* NAME
*	osm_switch_get_mft_max_block
*
* DESCRIPTION
*       Get the max_block from the associated multicast table.
*
* SYNOPSIS
*/
static inline uint16_t osm_switch_get_mft_max_block(IN osm_switch_t * p_sw)
{
	CL_ASSERT(p_sw);
	return osm_mcast_tbl_get_max_block(&p_sw->mcast_tbl);
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch object.
*
* RETURN VALUE
*/

/****f* OpenSM: Switch/osm_switch_get_mft_max_block_in_use
* NAME
*	osm_switch_get_mft_max_block_in_use
*
* DESCRIPTION
*	Get the max_block_in_use from the associated multicast table.
*
* SYNOPSIS
*/
static inline int16_t osm_switch_get_mft_max_block_in_use(IN osm_switch_t * p_sw)
{
	CL_ASSERT(p_sw);
	return osm_mcast_tbl_get_max_block_in_use(&p_sw->mcast_tbl);
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch object.
*
* RETURN VALUES
*	Returns the maximum block ID in use in this switch's mcast table.
*	A value of -1 indicates no blocks are in use.
*
* NOTES
*
* SEE ALSO
*/

/****f* OpenSM: Switch/osm_switch_get_mft_max_position
* NAME
*	osm_switch_get_mft_max_position
*
* DESCRIPTION
*       Get the max_position from the associated multicast table.
*
* SYNOPSIS
*/
static inline uint8_t osm_switch_get_mft_max_position(IN osm_switch_t * p_sw)
{
	CL_ASSERT(p_sw);
	return osm_mcast_tbl_get_max_position(&p_sw->mcast_tbl);
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch object.
*
* RETURN VALUE
*/

/****f* OpenSM: Switch/osm_switch_get_dimn_port
* NAME
*	osm_switch_get_dimn_port
*
* DESCRIPTION
*	Get the routing ordered port
*
* SYNOPSIS
*/
static inline uint8_t osm_switch_get_dimn_port(IN const osm_switch_t * p_sw,
					       IN uint8_t port_num)
{
	CL_ASSERT(p_sw);
	if (p_sw->search_ordering_ports == NULL)
		return port_num;
	return p_sw->search_ordering_ports[port_num];
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch object.
*
*	port_num
*		[in] Port number in the switch
*
* RETURN VALUES
*	Returns the port number ordered for routing purposes.
*/

/****f* OpenSM: Switch/osm_switch_recommend_path
* NAME
*	osm_switch_recommend_path
*
* DESCRIPTION
*	Returns the recommended port on which to route this LID.
*	In cases where LMC > 0, the remote side system and node
*	used for the routing are tracked in the provided arrays
*	(and counts) such that other lid for the same port will
*	try and avoid going through the same remote system/node.
*
* SYNOPSIS
*/
uint8_t osm_switch_recommend_path(IN const osm_switch_t * p_sw,
				  IN osm_port_t * p_port, IN uint16_t lid_ho,
				  IN unsigned start_from,
				  IN boolean_t ignore_existing,
				  IN boolean_t routing_for_lmc,
				  IN boolean_t dor,
				  IN boolean_t port_shifting,
				  IN uint32_t scatter_ports,
				  IN osm_lft_type_enum lft_enum);
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch object.
*
*	p_port
*		[in] Pointer to the port object for which to get a path
*		advisory.
*
*	lid_ho
*		[in] LID value (host order) for which to get a path advisory.
*
*	start_from
*		[in] Port number from where to start balance counting.
*
*	ignore_existing
*		[in] Set to cause the switch to choose the optimal route
*		regardless of existing paths.
*		If false, the switch will choose an existing route if one
*		exists, otherwise will choose the optimal route.
*
*	routing_for_lmc
*		[in] We support an enhanced LMC aware routing mode:
*		In the case of LMC > 0, we can track the remote side
*		system and node for all of the lids of the target
*		and try and avoid routing again through the same
*		system / node.
*
*		Assume if routing_for_lmc is TRUE that this procedure
*		was provided with the tracking array and counter via
*		p_port->priv, and we can conduct this algorithm.
*
*	dor
*		[in] If TRUE, Dimension Order Routing will be done.
*
*	port_shifting
*		[in] If TRUE, port_shifting will be done.
*
* 	scatter_ports
* 		[in] If not zero, randomize the selection of the best ports.
*
* 	lft_enum
*		[in] Use LFT that was calculated by routing engine, or
*		current LFT on the switch.
*
* RETURN VALUE
*	Returns the recommended port on which to route this LID.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_recommend_mcast_path
* NAME
*	osm_switch_recommend_mcast_path
*
* DESCRIPTION
*	Returns the recommended port on which to route this LID.
*
* SYNOPSIS
*/
uint8_t osm_switch_recommend_mcast_path(IN osm_switch_t * p_sw,
					IN osm_port_t * p_port,
					IN uint16_t mlid_ho,
					IN boolean_t ignore_existing);
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch object.
*
*	p_port
*		[in] Pointer to the port object for which to get
*		the multicast path.
*
*	mlid_ho
*		[in] MLID for the multicast group in question.
*
*	ignore_existing
*		[in] Set to cause the switch to choose the optimal route
*		regardless of existing paths.
*		If false, the switch will choose an existing route if one exists,
*		otherwise will choose the optimal route.
*
* RETURN VALUE
*	Returns the recommended port on which to route this LID.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_get_mcast_fwd_tbl_size
* NAME
*	osm_switch_get_mcast_fwd_tbl_size
*
* DESCRIPTION
*	Returns the number of entries available in the multicast forwarding table.
*
* SYNOPSIS
*/
static inline uint16_t
osm_switch_get_mcast_fwd_tbl_size(IN const osm_switch_t * p_sw)
{
	return cl_ntoh16(p_sw->switch_info.mcast_cap);
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch.
*
* RETURN VALUE
*	Returns the number of entries available in the multicast forwarding table.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_path_count_get
* NAME
*	osm_switch_path_count_get
*
* DESCRIPTION
*	Returns the count of the number of paths going through this port.
*
* SYNOPSIS
*/
static inline uint32_t osm_switch_path_count_get(IN const osm_switch_t * p_sw,
						 IN uint8_t port_num)
{
	return osm_port_prof_path_count_get(&p_sw->p_prof[port_num]);
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the Switch object.
*
*	port_num
*		[in] Port number for which to get path count.
*
* RETURN VALUE
*	Returns the count of the number of paths going through this port.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_prepare_path_rebuild
* NAME
*	osm_switch_prepare_path_rebuild
*
* DESCRIPTION
*	Prepares a switch to rebuild pathing information.
*
* SYNOPSIS
*/
int osm_switch_prepare_path_rebuild(IN osm_switch_t * p_sw,
				    IN uint16_t max_lids);
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the Switch object.
*
*	max_lids
*		[in] Max number of lids in the subnet.
*
* RETURN VALUE
*	Returns zero on success, or negative value if an error occurred.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_get_mcast_tbl_ptr
* NAME
*	osm_switch_get_mcast_tbl_ptr
*
* DESCRIPTION
*	Returns a pointer to the switch's multicast table.
*
* SYNOPSIS
*/
static inline osm_mcast_tbl_t *osm_switch_get_mcast_tbl_ptr(IN const
							    osm_switch_t * p_sw)
{
	return (osm_mcast_tbl_t *) & p_sw->mcast_tbl;
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch.
*
* RETURN VALUE
*	Returns a pointer to the switch's multicast table.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Switch/osm_switch_is_in_mcast_tree
* NAME
*	osm_switch_is_in_mcast_tree
*
* DESCRIPTION
*	Returns true if this switch already belongs in the tree for the specified
*	multicast group.
*
* SYNOPSIS
*/
static inline boolean_t
osm_switch_is_in_mcast_tree(IN const osm_switch_t * p_sw, IN uint16_t mlid_ho)
{
	const osm_mcast_tbl_t *p_tbl;

	p_tbl = &p_sw->mcast_tbl;
	if (p_tbl)
		return osm_mcast_tbl_is_any_port(&p_sw->mcast_tbl, mlid_ho);
	else
		return FALSE;
}
/*
* PARAMETERS
*	p_sw
*		[in] Pointer to the switch.
*
*	mlid_ho
*		[in] MLID (host order) of the multicast tree to check.
*
* RETURN VALUE
*	Returns true if this switch already belongs in the tree for the specified
*	multicast group.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_SWITCH_H_ */
