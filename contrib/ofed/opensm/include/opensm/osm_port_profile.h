/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005,2008 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 Xsigo Systems Inc. All rights reserved.
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
 * 	Declaration of Switch/osm_port_profile_t.
 *	This object represents a port profile for an IBA switch.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_PORT_PROFILE_H_
#define _OSM_PORT_PROFILE_H_

#include <string.h>
#include <iba/ib_types.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_node.h>
#include <opensm/osm_port.h>
#include <opensm/osm_mcast_tbl.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Port Profile
* NAME
*	Port Profile
*
* DESCRIPTION
*	The Port Profile object contains profiling information for
*	each Physical Port on a switch.  The profile information
*	may be used to optimize path selection.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Switch/osm_port_profile_t
* NAME
*	osm_port_profile_t
*
* DESCRIPTION
*	The Port Profile object contains profiling information for
*	each Physical Port on the switch.  The profile information
*	may be used to optimize path selection.
*
*	This object should be treated as opaque and should be
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_port_profile {
	uint32_t num_paths;
} osm_port_profile_t;
/*
* FIELDS
*	num_paths
*		The number of paths using this port.
*
* SEE ALSO
*********/

/****s* OpenSM: Switch/osm_port_mask_t
* NAME
*	osm_port_mask_t
*
* DESCRIPTION
*       The Port Mask object contains a port numbered bit mask
*	for whether the port should be ignored by the link load
*	equalization algorithm.
*
* SYNOPSIS
*/
typedef long osm_port_mask_t[32 / sizeof(long)];
/*
* FIELDS
*	osm_port_mask_t
*		Bit mask by port number
*
* SEE ALSO
*********/

/****f* OpenSM: Port Profile/osm_port_prof_construct
* NAME
*	osm_port_prof_construct
*
* DESCRIPTION
*
*
* SYNOPSIS
*/
static inline void osm_port_prof_construct(IN osm_port_profile_t * p_prof)
{
	CL_ASSERT(p_prof);
	memset(p_prof, 0, sizeof(*p_prof));
}
/*
* PARAMETERS
*	p_prof
*		[in] Pointer to the Port Profile object to construct.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Port Profile/osm_port_prof_path_count_inc
* NAME
*	osm_port_prof_path_count_inc
*
* DESCRIPTION
*	Increments the count of the number of paths going through this port.
*
*
* SYNOPSIS
*/
static inline void osm_port_prof_path_count_inc(IN osm_port_profile_t * p_prof)
{
	CL_ASSERT(p_prof);
	p_prof->num_paths++;
}
/*
* PARAMETERS
*	p_prof
*		[in] Pointer to the Port Profile object.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Port Profile/osm_port_prof_path_count_get
* NAME
*	osm_port_prof_path_count_get
*
* DESCRIPTION
*	Returns the count of the number of paths going through this port.
*
* SYNOPSIS
*/
static inline uint32_t
osm_port_prof_path_count_get(IN const osm_port_profile_t * p_prof)
{
	return p_prof->num_paths;
}
/*
* PARAMETERS
*	p_prof
*		[in] Pointer to the Port Profile object.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_PORT_PROFILE_H_ */
