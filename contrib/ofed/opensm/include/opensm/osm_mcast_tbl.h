/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2009 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 * 	Declaration of osm_mcast_tbl_t.
 *	This object represents a multicast forwarding table.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_MCAST_TBL_H_
#define _OSM_MCAST_TBL_H_

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <opensm/osm_base.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****s* OpenSM: Forwarding Table/osm_mcast_tbl_t
* NAME
*	osm_mcast_tbl_t
*
* DESCRIPTION
*	Multicast Forwarding Table structure.
*
*	Callers may directly access this object.
*
* SYNOPSIS
*/
typedef struct osm_mcast_fwdbl {
	uint8_t num_ports;
	uint8_t max_position;
	uint16_t max_block;
	int16_t max_block_in_use;
	uint16_t num_entries;
	uint16_t max_mlid_ho;
	uint16_t mft_depth;
	uint16_t(*p_mask_tbl)[][IB_MCAST_POSITION_MAX + 1];
} osm_mcast_tbl_t;
/*
* FIELDS
*	num_ports
*		The number of ports in the port mask.  This value
*		is the same as the number of ports on the switch
*
*	max_position
*		Maximum bit mask position for this table.  This value
*		is computed from the number of ports on the switch.
*
*	max_block
*		Maximum block number supported in the table.  This value
*		is approximately the number of MLID entries divided by the
*		number of MLIDs per block
*
*	num_entries
*		Number of entries in the table (aka number of MLIDs supported).
*
*	max_mlid_ho
*		Maximum MLID (host order) for the currently allocated multicast
*		port mask table.
*
*	mft_depth
*		Number of MLIDs in the currently allocated multicast port mask
*		table.
*
*	p_mask_tbl
*		Pointer to a two dimensional array of port_masks for this switch.
*		The first dimension is MLID offset, second dimension is mask position.
*		This pointer is null for switches that do not support multicast.
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_init
* NAME
*	osm_mcast_tbl_init
*
* DESCRIPTION
*	This function initializes a Multicast Forwarding Table object.
*
* SYNOPSIS
*/
void osm_mcast_tbl_init(IN osm_mcast_tbl_t * p_tbl, IN uint8_t num_ports,
			IN uint16_t capacity);
/*
* PARAMETERS
*	num_ports
*		[in] Number of ports in the switch owning this table.
*
*	capacity
*		[in] The number of MLID entries (starting at 0xC000) supported
*		by this switch.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_delete
* NAME
*	osm_mcast_tbl_delete
*
* DESCRIPTION
*	This destroys and deallocates a Multicast Forwarding Table object.
*
* SYNOPSIS
*/
void osm_mcast_tbl_delete(IN osm_mcast_tbl_t ** pp_tbl);
/*
* PARAMETERS
*	pp_tbl
*		[in] Pointer a Pointer to the Multicast Forwarding Table object.
*
* RETURN VALUE
*	On success, returns a pointer to a new Multicast Forwarding Table object
*	of the specified size.
*	NULL otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_realloc
* NAME
*	osm_mcast_tbl_realloc
*
* DESCRIPTION
*	This function reallocates the multicast port mask table if necessary.
*
* SYNOPSIS
*/
int osm_mcast_tbl_realloc(IN osm_mcast_tbl_t * p_tbl, IN unsigned mlid_offset);
/*
* PARAMETERS
*
*	p_tbl
*		[in] Pointer to the Multicast Forwarding Table object.
*
*	mlid_offset
*		[in] Offset of MLID being accessed.
*
* RETURN VALUE
*	Returns 0 on success and non-zero value otherwise.
*
* NOTES
*
* SEE ALSO
*/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_destroy
* NAME
*	osm_mcast_tbl_destroy
*
* DESCRIPTION
*	This destroys and deallocates a Multicast Forwarding Table object.
*
* SYNOPSIS
*/
void osm_mcast_tbl_destroy(IN osm_mcast_tbl_t * p_tbl);
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Multicast Forwarding Table object.
*
* RETURN VALUE
*    None
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_set
* NAME
*	osm_mcast_tbl_set
*
* DESCRIPTION
*	Adds the port to the multicast group.
*
* SYNOPSIS
*/
void osm_mcast_tbl_set(IN osm_mcast_tbl_t * p_tbl, IN uint16_t mlid_ho,
		       IN uint8_t port_num);
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Multicast Forwarding Table object.
*
*	mlid_ho
*		[in] MLID value (host order) for which to set the route.
*
*	port_num
*		[in] Port to add to the multicast group.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_clear_mlid
* NAME
*	osm_mcast_tbl_clear_mlid
*
* DESCRIPTION
*	Removes all multicast paths for the specified MLID.
*
* SYNOPSIS
*/
void osm_mcast_tbl_clear_mlid(IN osm_mcast_tbl_t * p_tbl, IN uint16_t mlid_ho);
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Multicast Forwarding Table object.
*
*	mlid_ho
*		[in] MLID value (host order) for which to clear.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_is_port
* NAME
*	osm_mcast_tbl_is_port
*
* DESCRIPTION
*	Returns TRUE if the port is in the multicast group.
*
* SYNOPSIS
*/
boolean_t osm_mcast_tbl_is_port(IN const osm_mcast_tbl_t * p_tbl,
				IN uint16_t mlid_ho, IN uint8_t port_num);
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Multicast Forwarding Table object.
*
*	mlid_ho
*		[in] MLID value (host order).
*
*	port_num
*		[in] Port number on the switch
*
* RETURN VALUE
*	Returns the port that routes the specified LID.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_is_any_port
* NAME
*	osm_mcast_tbl_is_any_port
*
* DESCRIPTION
*	Returns TRUE if any port is in the multicast group.
*
* SYNOPSIS
*/
boolean_t osm_mcast_tbl_is_any_port(IN const osm_mcast_tbl_t * p_tbl,
				    IN uint16_t mlid_ho);
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Multicast Forwarding Table object.
*
*	mlid_ho
*		[in] MLID value (host order).
*
* RETURN VALUE
*	Returns TRUE if any port is in the multicast group.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_set_block
* NAME
*	osm_mcast_tbl_set_block
*
* DESCRIPTION
*	Copies the specified block into the Multicast Forwarding Table.
*
* SYNOPSIS
*/
ib_api_status_t osm_mcast_tbl_set_block(IN osm_mcast_tbl_t * p_tbl,
					IN const ib_net16_t * p_block,
					IN int16_t block_num,
					IN uint8_t position);
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Multicast Forwarding Table object.
*
*	p_block
*		[in] Pointer to the Forwarding Table block.
*
*	block_num
*		[in] Block number of this block.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_get_tbl_block
* NAME
*	osm_mcast_get_tbl_block
*
* DESCRIPTION
*	Retrieve a multicast forwarding table block.
*
* SYNOPSIS
*/
boolean_t osm_mcast_tbl_get_block(IN osm_mcast_tbl_t * p_tbl,
				  IN int16_t block_num, IN uint8_t position,
				  OUT ib_net16_t * p_block);
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to an osm_mcast_tbl_t object.
*
*	p_block
*		[in] Pointer to the Forwarding Table block.
*
*	block_num
*		[in] Block number of this block.
*
*	p_block
*		[out] Pointer to the 32 entry array to store the
*		forwarding table clock specified by block_id.
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

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_get_max_block
* NAME
*	osm_mcast_tbl_get_max_block
*
* DESCRIPTION
*	Returns the maximum block ID in this table.
*
* SYNOPSIS
*/
static inline uint16_t osm_mcast_tbl_get_max_block(IN osm_mcast_tbl_t * p_tbl)
{
	return p_tbl->max_block;
}

/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to an osm_mcast_tbl_t object.
*
* RETURN VALUES
*	Returns the maximum block ID in this table.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_get_max_block_in_use
* NAME
*	osm_mcast_tbl_get_max_block_in_use
*
* DESCRIPTION
*	Returns the maximum block ID in use in this table.
*	A value of -1 indicates no blocks are in use.
*
* SYNOPSIS
*/
static inline int16_t
osm_mcast_tbl_get_max_block_in_use(IN osm_mcast_tbl_t * p_tbl)
{
	return (p_tbl->max_block_in_use);
}

/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to an osm_mcast_tbl_t object.
*
* RETURN VALUES
*	Returns the maximum block ID in use in this table.
*	A value of -1 indicates no blocks are in use.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_mcast_tbl_get_max_position
* NAME
*	osm_mcast_tbl_get_max_position
*
* DESCRIPTION
*	Returns the maximum position in this table.
*
* SYNOPSIS
*/
static inline uint8_t
osm_mcast_tbl_get_max_position(IN osm_mcast_tbl_t * p_tbl)
{
	return (p_tbl->max_position);
}

/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to an osm_mcast_tbl_t object.
*
* RETURN VALUES
*	Returns the maximum position in this table.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_MCAST_TBL_H_ */
