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
 * 	Declaration of osm_ucast_mgr_t.
 *	This object represents the Unicast Manager object.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_UCAST_MGR_H_
#define _OSM_UCAST_MGR_H_

#include <complib/cl_passivelock.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_log.h>
#include <opensm/osm_ucast_cache.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Unicast Manager
* NAME
*	Unicast Manager
*
* DESCRIPTION
*	The Unicast Manager object encapsulates the information
*	needed to control unicast LID forwarding on the subnet.
*
*	The Unicast Manager object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
struct osm_sm;
/****s* OpenSM: Unicast Manager/osm_ucast_mgr_t
* NAME
*	osm_ucast_mgr_t
*
* DESCRIPTION
*	Unicast Manager structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_ucast_mgr {
	struct osm_sm *sm;
	osm_subn_t *p_subn;
	osm_log_t *p_log;
	cl_plock_t *p_lock;
	uint16_t max_lid;
	cl_qlist_t port_order_list;
	boolean_t is_dor;
	boolean_t some_hop_count_set;
	cl_qmap_t cache_sw_tbl;
	boolean_t cache_valid;
} osm_ucast_mgr_t;
/*
* FIELDS
*	sm
*		Pointer to the SM object.
*
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_log
*		Pointer to the log object.
*
*	p_lock
*		Pointer to the serializing lock.
*
*	is_dor
*		Dimension Order Routing (DOR) will be done
*
*	port_order_list
*		List of ports ordered for routing.
*
*	some_hop_count_set
*		Initialized to FALSE at the beginning of each the min hop
*		tables calculation iteration cycle, set to TRUE to indicate
*		that some hop count changes were done.
*
*	cache_sw_tbl
*		Cached switches table.
*
*	cache_valid
*		TRUE if the unicast cache is valid.
*
* SEE ALSO
*	Unicast Manager object
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_construct
* NAME
*	osm_ucast_mgr_construct
*
* DESCRIPTION
*	This function constructs a Unicast Manager object.
*
* SYNOPSIS
*/
void osm_ucast_mgr_construct(IN osm_ucast_mgr_t * p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to a Unicast Manager object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows osm_ucast_mgr_destroy
*
*	Calling osm_ucast_mgr_construct is a prerequisite to calling any other
*	method except osm_ucast_mgr_init.
*
* SEE ALSO
*	Unicast Manager object, osm_ucast_mgr_init,
*	osm_ucast_mgr_destroy
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_destroy
* NAME
*	osm_ucast_mgr_destroy
*
* DESCRIPTION
*	The osm_ucast_mgr_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_ucast_mgr_destroy(IN osm_ucast_mgr_t * p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified
*	Unicast Manager object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_ucast_mgr_construct or osm_ucast_mgr_init.
*
* SEE ALSO
*	Unicast Manager object, osm_ucast_mgr_construct,
*	osm_ucast_mgr_init
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_init
* NAME
*	osm_ucast_mgr_init
*
* DESCRIPTION
*	The osm_ucast_mgr_init function initializes a
*	Unicast Manager object for use.
*
* SYNOPSIS
*/
ib_api_status_t osm_ucast_mgr_init(IN osm_ucast_mgr_t * p_mgr,
				   IN struct osm_sm * sm);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_ucast_mgr_t object to initialize.
*
*	sm
*		[in] Pointer to the SM object.
*
* RETURN VALUES
*	IB_SUCCESS if the Unicast Manager object was initialized
*	successfully.
*
* NOTES
*	Allows calling other Unicast Manager methods.
*
* SEE ALSO
*	Unicast Manager object, osm_ucast_mgr_construct,
*	osm_ucast_mgr_destroy
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_set_fwd_tables
* NAME
*	osm_ucast_mgr_set_fwd_tables
*
* DESCRIPTION
*	Setup forwarding table for the switch (from prepared new_lft).
*
* SYNOPSIS
*/
void osm_ucast_mgr_set_fwd_tables(IN osm_ucast_mgr_t * p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_ucast_mgr_t object.
*
* SEE ALSO
*	Unicast Manager
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_build_lid_matrices
* NAME
*	osm_ucast_mgr_build_lid_matrices
*
* DESCRIPTION
*	Build switches's lid matrices.
*
* SYNOPSIS
*/
int osm_ucast_mgr_build_lid_matrices(IN osm_ucast_mgr_t * p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_ucast_mgr_t object.
*
* NOTES
*	This function processes the subnet, configuring switches'
*	min hops tables (aka lid matrices).
*
* SEE ALSO
*	Unicast Manager
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_process
* NAME
*	osm_ucast_mgr_process
*
* DESCRIPTION
*	Process and configure the subnet's unicast forwarding tables.
*
* SYNOPSIS
*/
int osm_ucast_mgr_process(IN osm_ucast_mgr_t * p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_ucast_mgr_t object.
*
* RETURN VALUES
*	Returns zero on success and negative value on failure.
*
* NOTES
*	This function processes the subnet, configuring switch
*	unicast forwarding tables.
*
* SEE ALSO
*	Unicast Manager, Node Info Response Controller
*********/

int ucast_dummy_build_lid_matrices(void *context);
END_C_DECLS
#endif				/* _OSM_UCAST_MGR_H_ */
