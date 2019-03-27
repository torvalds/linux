/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
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
 * 	Declaration of osm_mad_wrapper_t.
 *	This object represents the context wrapper for OpenSM MAD processing.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_VENDOR_UMADT_h_
#define _OSM_VENDOR_UMADT_h_

#include "iba/ib_types.h"
#include "complib/cl_qlist.h"
#include "complib/cl_thread.h"
#include <opensm/osm_base.h>
#include <vendor/umadt.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/ Vendor Umadt
* NAME
*	MAD Wrapper
*
* DESCRIPTION
*
*
* AUTHOR
*	Ranjit Pandit, Intel
*
*********/
typedef void *osm_vendor_t;
#define OSM_BIND_INVALID_HANDLE 0

/****s* OpenSM: Vendor Umadt /osm_bind_handle_t
* NAME
*   osm_bind_handle_t
*
* DESCRIPTION
* 	handle returned by the vendor transport bind call.
*
* SYNOPSIS
*/

typedef void *osm_bind_handle_t;

/****s* OpenSM: Vendor Umadt /mad_direction_t
* NAME
*	mad_direction_t
*
* DESCRIPTION
*	Tags for mad wrapper to indicate the direction of mads.
*	Umadt vendor transport layer uses this tag to call the appropriate
* 	Umadt APIs.
*
* SYNOPSIS
*/
typedef enum _mad_direction_t {
	SEND = 0,
	RECEIVE,
} mad_direction_t;

/****s* OpenSM/ osm_vend_wrap_t
* NAME
*   Umadt Vendor MAD Wrapper
*
* DESCRIPTION
*	Umadt specific MAD wrapper. Umadt transport layer sets this for
*	housekeeping.
*
* SYNOPSIS
*********/
typedef struct _osm_vend_wrap_t {
	MadtStruct *p_madt_struct;
	mad_direction_t direction;	// send or receive
	uint32_t size;
} osm_vend_wrap_t;
/*
* FIELDS
*	p_madt_struct
*		Umadt mad structure to identify a mad.
*
*	direction
*		Used to identify a mad with it's direction.
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_VENDOR_UMADT_h_ */
