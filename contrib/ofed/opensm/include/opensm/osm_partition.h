/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2008 Mellanox Technologies LTD. All rights reserved.
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
 * 	Declaration of osm_prtn_t.
 *	This object represents an IBA Partition.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_PARTITION_H_
#define _OSM_PARTITION_H_

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_map.h>
#include <opensm/osm_log.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_multicast.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Partition
* NAME
*	Partition
*
* DESCRIPTION
*	The Partition object encapsulates the information needed by the
*	OpenSM to manage Partitions.  The OpenSM allocates one Partition
*	object per Partition in the IBA subnet.
*
*	The Partition is not thread safe, thus callers must provide
*	serialization.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Partition/osm_prtn_t
* NAME
*	osm_prtn_t
*
* DESCRIPTION
*	Partition structure.
*
*	The osm_prtn_t object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_prtn {
	cl_map_item_t map_item;
	ib_net16_t pkey;
	uint8_t sl;
	cl_map_t full_guid_tbl;
	cl_map_t part_guid_tbl;
	char name[32];
	osm_mgrp_t **mgrps;
	int nmgrps;
} osm_prtn_t;
/*
* FIELDS
*	map_item
*		Linkage structure for cl_qmap.  MUST BE FIRST MEMBER!
*
*	pkey
*		The IBA defined P_KEY of this Partition.
*
*	sl
*		The Service Level (SL) associated with this Partiton.
*
*	full_guid_tbl
*		Container of pointers to all Port objects in the Partition
*		with full membership, indexed by port GUID.
*
*	part_guid_tbl
*		Container of pointers to all Port objects in the Partition
*		with limited membership, indexed by port GUID.
*
*	name
*		Name of the Partition as specified in partition
*		configuration.
*
*	mgrps
*		List of well known Multicast Groups
*		that were created for this partition (when configured).
*		This includes the IPoIB broadcast group.
*
*	nmgrps
*		Number of known Multicast Groups.
*
* SEE ALSO
*	Partition
*********/

/****f* OpenSM: Partition/osm_prtn_delete
* NAME
*	osm_prtn_delete
*
* DESCRIPTION
*	This function destroys and deallocates a Partition object.
*
* SYNOPSIS
*/
void osm_prtn_delete(IN osm_subn_t * p_subn, IN OUT osm_prtn_t ** pp_prtn);
/*
* PARAMETERS
*	pp_prtn
*		[in][out] Pointer to a pointer to a Partition object to
*		delete. On return, this pointer is NULL.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified Partition object.
*
* SEE ALSO
*	Partition, osm_prtn_new
*********/

/****f* OpenSM: Partition/osm_prtn_new
* NAME
*	osm_prtn_new
*
* DESCRIPTION
*	This function allocates and initializes a Partition object.
*
* SYNOPSIS
*/
osm_prtn_t *osm_prtn_new(IN const char *name, IN uint16_t pkey);
/*
* PARAMETERS
*	name
*		[in] Partition name string
*
*	pkey
*		[in] Partition P_Key value
*
* RETURN VALUE
*	Pointer to the initialize Partition object.
*
* NOTES
*	Allows calling other partition methods.
*
* SEE ALSO
*	Partition
*********/

/****f* OpenSM: Partition/osm_prtn_is_guid
* NAME
*	osm_prtn_is_guid
*
* DESCRIPTION
*	Indicates if a port is a member of the partition.
*
* SYNOPSIS
*/
static inline boolean_t osm_prtn_is_guid(IN const osm_prtn_t * p_prtn,
					 IN ib_net64_t guid)
{
	return (cl_map_get(&p_prtn->full_guid_tbl, guid) != NULL) ||
	    (cl_map_get(&p_prtn->part_guid_tbl, guid) != NULL);
}

/*
* PARAMETERS
*	p_prtn
*		[in] Pointer to an osm_prtn_t object.
*
*	guid
*		[in] Port GUID.
*
* RETURN VALUES
*	TRUE if the specified port GUID is a member of the partition,
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Partition/osm_prtn_make_partitions
* NAME
*	osm_prtn_make_partitions
*
* DESCRIPTION
* 	Makes all partitions in subnet.
*
* SYNOPSIS
*/
ib_api_status_t osm_prtn_make_partitions(IN osm_log_t * p_log,
					 IN osm_subn_t * p_subn);
/*
* PARAMETERS
*	p_log
*		[in] Pointer to a log object.
*
*	p_subn
*		[in] Pointer to subnet object.
*
* RETURN VALUES
*	IB_SUCCESS value on success.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Partition/osm_prtn_find_by_name
* NAME
*	osm_prtn_find_by_name
*
* DESCRIPTION
* 	Fides partition by name.
*
* SYNOPSIS
*/
osm_prtn_t *osm_prtn_find_by_name(IN osm_subn_t * p_subn, IN const char *name);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to a subnet object.
*
*	name
*		[in] Required partition name.
*
* RETURN VALUES
*	Pointer to the partition object on success.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_PARTITION_H_ */
