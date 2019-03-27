/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
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
 * 	Declaration of osm_lid_mgr_t.
 *	This object represents the LID Manager object.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_LID_MGR_H_
#define _OSM_LID_MGR_H_

#include <complib/cl_passivelock.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_db.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
#define OSM_LID_MGR_LIST_SIZE_MIN 256
/****h* OpenSM/LID Manager
* NAME
*	LID Manager
*
* DESCRIPTION
*	The LID Manager object encapsulates the information
*	needed to control LID assignments on the subnet.
*
*	The LID Manager object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
struct osm_sm;
/****s* OpenSM: LID Manager/osm_lid_mgr_t
* NAME
*	osm_lid_mgr_t
*
* DESCRIPTION
*	LID Manager structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_lid_mgr {
	struct osm_sm *sm;
	osm_subn_t *p_subn;
	osm_db_t *p_db;
	osm_log_t *p_log;
	cl_plock_t *p_lock;
	osm_db_domain_t *p_g2l;
	cl_qlist_t free_ranges;
	boolean_t dirty;
	uint8_t used_lids[IB_LID_UCAST_END_HO + 1];
} osm_lid_mgr_t;
/*
* FIELDS
*	sm
*		Pointer to the SM object.
*
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_db
*		Pointer to the database (persistency) object
*
*	p_log
*		Pointer to the log object.
*
*	p_lock
*		Pointer to the serializing lock.
*
*	p_g2l
*		Pointer to the database domain storing guid to lid mapping.
*
*	free_ranges
*		A list of available free lid ranges. The list is initialized
*		by the code that initializes the lid assignment and is consumed
*		by the procedure that finds a free range. It holds elements of
*		type osm_lid_mgr_range_t
*
*	dirty
*		 Indicates that lid table was updated
*
*	used_lids
*		 An array of used lids. keeps track of
*		 existing and non existing mapping of guid->lid
*
* SEE ALSO
*	LID Manager object
*********/

/****f* OpenSM: LID Manager/osm_lid_mgr_construct
* NAME
*	osm_lid_mgr_construct
*
* DESCRIPTION
*	This function constructs a LID Manager object.
*
* SYNOPSIS
*/
void osm_lid_mgr_construct(IN osm_lid_mgr_t * p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to a LID Manager object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows osm_lid_mgr_destroy
*
*	Calling osm_lid_mgr_construct is a prerequisite to calling any other
*	method except osm_lid_mgr_init.
*
* SEE ALSO
*	LID Manager object, osm_lid_mgr_init,
*	osm_lid_mgr_destroy
*********/

/****f* OpenSM: LID Manager/osm_lid_mgr_destroy
* NAME
*	osm_lid_mgr_destroy
*
* DESCRIPTION
*	The osm_lid_mgr_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_lid_mgr_destroy(IN osm_lid_mgr_t * p_mgr);
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
*	LID Manager object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_lid_mgr_construct or osm_lid_mgr_init.
*
* SEE ALSO
*	LID Manager object, osm_lid_mgr_construct,
*	osm_lid_mgr_init
*********/

/****f* OpenSM: LID Manager/osm_lid_mgr_init
* NAME
*	osm_lid_mgr_init
*
* DESCRIPTION
*	The osm_lid_mgr_init function initializes a
*	LID Manager object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_lid_mgr_init(IN osm_lid_mgr_t * p_mgr, IN struct osm_sm * sm);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_lid_mgr_t object to initialize.
*
*	sm
*		[in] Pointer to the SM object for this subnet.
*
* RETURN VALUES
*	CL_SUCCESS if the LID Manager object was initialized
*	successfully.
*
* NOTES
*	Allows calling other LID Manager methods.
*
* SEE ALSO
*	LID Manager object, osm_lid_mgr_construct,
*	osm_lid_mgr_destroy
*********/

/****f* OpenSM: LID Manager/osm_lid_mgr_process_sm
* NAME
*	osm_lid_mgr_process_sm
*
* DESCRIPTION
*	Configures the SM's port with its designated LID values.
*
* SYNOPSIS
*/
int osm_lid_mgr_process_sm(IN osm_lid_mgr_t * p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_lid_mgr_t object.
*
* RETURN VALUES
*	Returns 0 on success and non-zero value otherwise.
*
* NOTES
*
* SEE ALSO
*	LID Manager
*********/

/****f* OpenSM: LID Manager/osm_lid_mgr_process_subnet
* NAME
*	osm_lid_mgr_process_subnet
*
* DESCRIPTION
*	Configures subnet ports (except the SM port itself) with their
*	designated LID values.
*
* SYNOPSIS
*/
int osm_lid_mgr_process_subnet(IN osm_lid_mgr_t * p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_lid_mgr_t object.
*
* RETURN VALUES
*	Returns 0 on success and non-zero value otherwise.
*
* NOTES
*
* SEE ALSO
*	LID Manager
*********/

END_C_DECLS
#endif				/* _OSM_LID_MGR_H_ */
