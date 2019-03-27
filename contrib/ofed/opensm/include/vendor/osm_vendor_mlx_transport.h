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

/**
 *  FILE                   osmv_transport.h
 *  AUTHOR                 Edward Bortnikov
 *
 *  DESCRIPTION
 *     The lower-level MAD transport interface implementation
 *     that allows sending a single MAD/receiving a callback
 *     when a single MAD is received.
*/

#ifndef _OSMV_TRANSPORT_H_
#define _OSMV_TRANSPORT_H_

#include <vendor/osm_vendor_mlx.h>
#include <vendor/osm_vendor_mlx_defs.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/*
 * NAME
 *   osmv_transport_init
 *
 * DESCRIPTION
 *   Setup the MAD transport infrastructure (filters, callbacks etc).
 */
#define VENDOR_HCA_MAXNAMES 32
ib_api_status_t
osmv_transport_init(IN osm_bind_info_t * p_info,
		    IN char hca_id[VENDOR_HCA_MAXNAMES],
		    IN uint8_t hca_idx, IN osmv_bind_obj_t * p_bo);

/*
 * NAME
 *   osmv_transport_send_mad
 *
 * DESCRIPTION
 *   Send a single MAD (256 byte)
 */
ib_api_status_t
osmv_transport_mad_send(IN const osm_bind_handle_t h_bind,
			IN void *p_mad, IN const osm_mad_addr_t * p_mad_addr);

/*
 * NAME
 *   osmv_transport_done
 *
 * DESCRIPTION
 *   deallocator of transportation infrastructure
 */
void osmv_transport_done(IN const osm_bind_handle_t h_bind);

END_C_DECLS
#endif				/* _OSMV_TRANSPORT_H_ */
