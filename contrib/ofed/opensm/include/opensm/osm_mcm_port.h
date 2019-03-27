/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2012 Mellanox Technologies LTD. All rights reserved.
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
 * 	Declaration of osm_mcm_port_t.
 *	This object represents the membership of a port in a multicast group.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_MCM_PORT_H_
#define _OSM_MCM_PORT_H_

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
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

struct osm_mgrp;

/****s* OpenSM: MCM Port Object/osm_mcm_port_t
* NAME
* 	osm_mcm_port_t
*
* DESCRIPTION
* 	This object represents a particular port as a member of a
*	multicast group.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_mcm_port {
	cl_map_item_t map_item;
	cl_list_item_t list_item;
	osm_port_t *port;
	struct osm_mgrp *mgrp;
} osm_mcm_port_t;
/*
* FIELDS
*	map_item
*		Map Item for qmap linkage.  Must be first element!!
*
*	list_item
*		Linkage structure for cl_qlist.
*
*	port
*		Reference to the parent port
*
*	mgrp
*		The pointer to multicast group where this port is member of
*
* SEE ALSO
*	MCM Port Object
*********/

/****f* OpenSM: MCM Port Object/osm_mcm_port_new
* NAME
*	osm_mcm_port_new
*
* DESCRIPTION
*	The osm_mcm_port_new function allocates and initializes a
*	MCM Port Object for use.
*
* SYNOPSIS
*/
osm_mcm_port_t *osm_mcm_port_new(IN osm_port_t * port, IN struct osm_mgrp *mgrp);
/*
* PARAMETERS
*	port
*		[in] Pointer to the port object
*
*	mgrp
*		[in] Pointer to multicast group where this port is joined
*
* RETURN VALUES
*	Pointer to the allocated and initialized MCM Port object.
*
* NOTES
*
* SEE ALSO
*	MCM Port Object, osm_mcm_port_delete,
*********/

/****f* OpenSM: MCM Port Object/osm_mcm_port_delete
* NAME
*	osm_mcm_port_delete
*
* DESCRIPTION
*	The osm_mcm_port_delete function destroys and dellallocates an
*	MCM Port Object, releasing all resources.
*
* SYNOPSIS
*/
void osm_mcm_port_delete(IN osm_mcm_port_t * p_mcm);
/*
* PARAMETERS
*	p_mcm
*		[in] Pointer to a MCM Port Object to delete.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*
* SEE ALSO
*	MCM Port Object, osm_mcm_port_new
*********/

/****s* OpenSM: MCM Port Object/osm_mcm_alias_guid_t
* NAME
*	osm_mcm_alias_guid_t
*
* DESCRIPTION
*	This object represents an alias guid for a mcm port.
*
*	The osm_mcm_alias_guid_t object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_mcm_alias_guid {
	cl_map_item_t map_item;
	ib_net64_t alias_guid;
	osm_mcm_port_t *p_base_mcm_port;
	ib_gid_t port_gid;
	uint8_t scope_state;
	boolean_t proxy_join;
} osm_mcm_alias_guid_t;
/*
* FIELDS
*	map_item
*		Linkage structure for cl_qmap.  MUST BE FIRST MEMBER!
*
*	alias_guid
*		Alias GUID for port obtained from SM GUIDInfo attribute
*
*	p_base_mcm_port
*		Pointer to osm_mcm_port_t for base port GUID
*
*	port_gid
*		GID of the member port
*
*	scope_state
*
*	proxy_join
*		If FALSE - Join was performed by the endport identified
*		by PortGID. If TRUE - Join was performed on behalf of
*		the endport identified by PortGID by another port within
*		the same partition.
*
* SEE ALSO
*	MCM Port, Physical Port, Physical Port Table
*/

/****f* OpenSM: MCM Port Object/osm_mcm_alias_guid_new
* NAME
*	osm_mcm_alias_guid_new
*
* DESCRIPTION
*	This function allocates and initializes an mcm alias guid object.
*
* SYNOPSIS
*/
osm_mcm_alias_guid_t *osm_mcm_alias_guid_new(IN osm_mcm_port_t *p_base_mcm_port,
					     IN ib_member_rec_t *mcmr,
					     IN boolean_t proxy);
/*
* PARAMETERS
*	p_base_mcm_port
*		[in] Pointer to the mcm port for this base GUID
*
*	mcmr
*		[in] Pointer to MCMember record of the join request
*
*	proxy
*		[in] proxy_join state analyzed from the request
*
* RETURN VALUE
*	Pointer to the initialized mcm alias guid object.
*
* NOTES
*	Allows calling other mcm alias guid methods.
*
* SEE ALSO
*       MCM Port Object
*********/

/****f* OpenSM: MCM Port Object/osm_mcm_alias_guid_delete
* NAME
*	osm_mcm_alias_guid_delete
*
* DESCRIPTION
*	This function destroys and deallocates an mcm alias guid object.
*
* SYNOPSIS
*/
void osm_mcm_alias_guid_delete(IN OUT osm_mcm_alias_guid_t ** pp_mcm_alias_guid);
/*
* PARAMETERS
*	pp_mcm_alias_guid
*		[in][out] Pointer to a pointer to an mcm alias guid object to
*		delete. On return, this pointer is NULL.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified mcm alias guid object.
*
* SEE ALSO
*	MCM Port Object
*********/

END_C_DECLS
#endif				/* _OSM_MCM_PORT_H_ */
