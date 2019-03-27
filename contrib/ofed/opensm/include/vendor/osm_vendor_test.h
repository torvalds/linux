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

#ifndef _OSM_VENDOR_TEST_H_
#define _OSM_VENDOR_TEST_H_

#include <iba/ib_types.h>
#include <opensm/osm_base.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/* This value must be zero for the TEST transport. */
#define OSM_BIND_INVALID_HANDLE 0
/*
 * Abstract:
 * 	Declaration of vendor specific transport interface.
 *  This is the "Test" vendor which allows compilation and some
 *  testing without a real vendor interface.
 *	These objects are part of the OpenSM family of objects.
 */
/****h* OpenSM/Vendor Test
* NAME
*	Vendor Test
*
* DESCRIPTION
*	The Vendor Test structure encapsulates an artificial transport layer
*	interface for testing.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Vendor Test/osm_vend_wrap_t
* NAME
*	osm_vend_wrap_t
*
* DESCRIPTION
*	Vendor specific MAD wrapper context.
*
*	This structure allows direct access to member variables.
*
* SYNOPSIS
*/
typedef struct _osm_vend_wrap {
	uint32_t dummy;

} osm_vend_wrap_t;
/*********/

/****s* OpenSM: Vendor Test/osm_vendor_t
* NAME
*	osm_vendor_t
*
* DESCRIPTION
*	Vendor specific MAD interface.
*
*	This interface defines access to the vendor specific MAD
*	transport layer.
*
* SYNOPSIS
*/
typedef struct _osm_vendor {
	osm_log_t *p_log;
	uint32_t timeout;

} osm_vendor_t;
/*********/

typedef struct _osm_bind_handle {
	osm_vendor_t *p_vend;
	ib_net64_t port_guid;
	uint8_t mad_class;
	uint8_t class_version;
	boolean_t is_responder;
	boolean_t is_trap_processor;
	boolean_t is_report_processor;
	uint32_t send_q_size;
	uint32_t recv_q_size;

} *osm_bind_handle_t;

END_C_DECLS
#endif				/* _OSM_VENDOR_TEST_H_ */
