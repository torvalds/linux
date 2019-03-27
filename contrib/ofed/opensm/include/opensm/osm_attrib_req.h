/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
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

#ifndef _OSM_ATTRIB_REQ_H_
#define _OSM_ATTRIB_REQ_H_

#include <opensm/osm_path.h>

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
 * 	Declaration of the attribute request object.  This object
 *  encapsulates information needed by the generic request controller
 *  to request an attribute from a node.
 *	These objects are part of the OpenSM family of objects.
 */
/****h* OpenSM/Attribute Request
* NAME
*	Attribute Request
*
* DESCRIPTION
*	The Attribute Request structure encapsulates
*   encapsulates information needed by the generic request controller
*   to request an attribute from a node.
*
*	This structure allows direct access to member variables.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Attribute Request/osm_attrib_req_t
* NAME
*	osm_attrib_req_t
*
* DESCRIPTION
*	Attribute request structure.
*
*	This structure allows direct access to member variables.
*
* SYNOPSIS
*/
typedef struct osm_attrib_req {
	uint16_t attrib_id;
	uint32_t attrib_mod;
	osm_madw_context_t context;
	osm_dr_path_t path;
	cl_disp_msgid_t err_msg;
} osm_attrib_req_t;
/*
* FIELDS
*	attrib_id
*		Attribute ID for this request.
*
*	attrib_mod
*		Attribute modifier for this request.
*
*	context
*		Context to insert in outbound mad wrapper context.
*
*	path
*		The directed route path to the node.
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_ATTRIB_REQ_H_ */
