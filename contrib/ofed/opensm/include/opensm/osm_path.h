/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
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

#ifndef _OSM_PATH_H_
#define _OSM_PATH_H_

#include <string.h>
#include <opensm/osm_base.h>
#include <vendor/osm_vendor_api.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/*
 * Abstract:
 * 	Declaration of path related objects.
 *	These objects are part of the OpenSM family of objects.
 */
/****h* OpenSM/DR Path
* NAME
*	DR Path
*
* DESCRIPTION
*	The DR Path structure encapsulates a directed route through the subnet.
*
*	This structure allows direct access to member variables.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: DR Path/osm_dr_path_t
* NAME
*	osm_dr_path_t
*
* DESCRIPTION
*	Directed Route structure.
*
*	This structure allows direct access to member variables.
*
* SYNOPSIS
*/
typedef struct osm_dr_path {
	uint8_t hop_count;
	uint8_t path[IB_SUBNET_PATH_HOPS_MAX];
} osm_dr_path_t;
/*
* FIELDS
*	h_bind
*		Bind handle for port to which this path applies.
*
*	hop_count
*		The number of hops in this path.
*
*	path
*		The array of port numbers that comprise this path.
*
* SEE ALSO
*	DR Path structure
*********/
/****f* OpenSM: DR Path/osm_dr_path_construct
* NAME
*	osm_dr_path_construct
*
* DESCRIPTION
*	This function constructs a directed route path object.
*
* SYNOPSIS
*/
static inline void osm_dr_path_construct(IN osm_dr_path_t * p_path)
{
	/* The first location in the path array is reserved. */
	memset(p_path, 0, sizeof(*p_path));
}

/*
* PARAMETERS
*	p_path
*		[in] Pointer to a directed route path object to initialize.
*
*	h_bind
*		[in] Bind handle for the port on which this path applies.
*
*	hop_count
*		[in] Hop count needed to reach this node.
*
*	path
*		[in] Directed route path to reach this node.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: DR Path/osm_dr_path_init
* NAME
*	osm_dr_path_init
*
* DESCRIPTION
*	This function initializes a directed route path object.
*
* SYNOPSIS
*/
static inline void
osm_dr_path_init(IN osm_dr_path_t * p_path, IN uint8_t hop_count,
		 IN const uint8_t path[IB_SUBNET_PATH_HOPS_MAX])
{
	/* The first location in the path array is reserved. */
	CL_ASSERT(path[0] == 0);
	CL_ASSERT(hop_count < IB_SUBNET_PATH_HOPS_MAX);
	p_path->hop_count = hop_count;
	memcpy(p_path->path, path, hop_count + 1);
}

/*
* PARAMETERS
*	p_path
*		[in] Pointer to a directed route path object to initialize.
*
*	h_bind
*		[in] Bind handle for the port on which this path applies.
*
*	hop_count
*		[in] Hop count needed to reach this node.
*
*	path
*		[in] Directed route path to reach this node.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/
/****f* OpenSM: DR Path/osm_dr_path_extend
* NAME
*	osm_dr_path_extend
*
* DESCRIPTION
*	Adds a new hop to a path.
*
* SYNOPSIS
*/
static inline int osm_dr_path_extend(IN osm_dr_path_t * p_path,
				     IN uint8_t port_num)
{
	p_path->hop_count++;

	if (p_path->hop_count >= IB_SUBNET_PATH_HOPS_MAX)
		return -1;
	/*
	   Location 0 in the path array is reserved per IB spec.
	 */
	p_path->path[p_path->hop_count] = port_num;
	return 0;
}

/*
* PARAMETERS
*	p_path
*		[in] Pointer to a directed route path object to initialize.
*
*	port_num
*		[in] Additional port to add to the DR path.
*
* RETURN VALUES
*	0 indicates path was extended.
*	Other than 0 indicates path was not extended.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_PATH_H_ */
