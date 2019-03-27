/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2007,2009 Mellanox Technologies LTD. All rights reserved.
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

#ifndef _OSMV_H_
#define _OSMV_H_

#include <sys/types.h>
#include <opensm/osm_log.h>
#include <complib/cl_qlist.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/*
  Forward reference
*/
struct _osm_pkt_randomizer;

/* The structure behind the OSM Vendor handle */

typedef struct _osm_vendor {

	/* for holding common transport info - useful at ibmgt transport */
	void *p_transport_info;

	osm_log_t *p_log;

	/* Uniform timeout for every ACK/single MAD */
	uint32_t resp_timeout;

	/* Uniform timeout for every rmpp transaction */
	uint32_t ttime_timeout;

	/* All the bind handles associated with the vendor */
	cl_qlist_t bind_handles;

	/* run randomizer flag */
	boolean_t run_randomizer;

	/* Packet Randomizer object */
	struct _osm_pkt_randomizer *p_pkt_randomizer;

} osm_vendor_t;

/* Repeating the definitions in osm_vendor_api.h */

typedef void *osm_bind_handle_t;

typedef struct _osm_vend_wrap {
	ib_mad_t *p_mad;
} osm_vend_wrap_t;

#ifndef OSM_BIND_INVALID_HANDLE
#define OSM_BIND_INVALID_HANDLE NULL
#endif

/* The maximum number of retransmissions of the same MAD */
#define OSM_DEFAULT_RETRY_COUNT  3

END_C_DECLS
#endif
