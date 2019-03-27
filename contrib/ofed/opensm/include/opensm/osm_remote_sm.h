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
 * 	Declaration of osm_sm_t, osm_remote_sm_t.
 *	This object represents an IBA subnet.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_REMOTE_SM_H_
#define _OSM_REMOTE_SM_H_

#include <iba/ib_types.h>
#include <opensm/osm_base.h>
#include <opensm/osm_port.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Remote SM
* NAME
*	Remote SM
*
* DESCRIPTION
*	The Remote SM object encapsulates the information tracked for
*	other SM ports on the subnet.
*
*	The Remote SM object is thread safe.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Remote SM/osm_remote_sm_t
* NAME
*	osm_remote_sm_t
*
* DESCRIPTION
*	Remote Subnet Manager structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_remote_sm {
	cl_map_item_t map_item;
	ib_sm_info_t smi;
} osm_remote_sm_t;
/*
* FIELDS
*	map_item
*		Linkage for the cl_qmap container.  MUST BE FIRST ELEMENT!!
*	p_port
*		Pointer to the port object for this SM.
*
*	smi
*		The SMInfo attribute for this SM.
*
* SEE ALSO
*********/

/****f* OpenSM: SM/osm_remote_sm_construct
* NAME
*	osm_remote_sm_construct
*
* DESCRIPTION
*	This function constructs an Remote SM object.
*
* SYNOPSIS
*/
void osm_remote_sm_construct(IN osm_remote_sm_t * p_sm);
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an Remote SM object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_remote_sm_init, osm_remote_sm_destroy
*
*	Calling osm_remote_sm_construct is a prerequisite to calling any other
*	method except osm_remote_sm_init.
*
* SEE ALSO
*	SM object, osm_remote_sm_init, osm_remote_sm_destroy
*********/

/****f* OpenSM: SM/osm_remote_sm_destroy
* NAME
*	osm_remote_sm_destroy
*
* DESCRIPTION
*	The osm_remote_sm_destroy function destroys an SM, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_remote_sm_destroy(IN osm_remote_sm_t * p_sm);
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an Remote SM object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified Remote SM object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_remote_sm_construct or osm_remote_sm_init.
*
* SEE ALSO
*	Remote SM object, osm_remote_sm_construct, osm_remote_sm_init
*********/

/****f* OpenSM: SM/osm_remote_sm_init
* NAME
*	osm_remote_sm_init
*
* DESCRIPTION
*	The osm_remote_sm_init function initializes an Remote SM object for use.
*
* SYNOPSIS
*/
void osm_remote_sm_init(IN osm_remote_sm_t * p_sm, IN const ib_sm_info_t * p_smi);
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_remote_sm_t object to initialize.
*
*	p_port
*		[in] Pointer to the Remote SM's port object.
*
*	p_smi
*		[in] Pointer to the SMInfo attribute for this SM.
*
* RETURN VALUES
*	This function does not return a value.
*
* NOTES
*	Allows calling other Remote SM methods.
*
* SEE ALSO
*	Remote SM object, osm_remote_sm_construct, osm_remote_sm_destroy
*********/

END_C_DECLS
#endif				/* _OSM_REMOTE_SM_H_ */
