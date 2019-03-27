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

#ifndef _OSMV_RMPP_CTX_H
#define _OSMV_RMPP_CTX_H

#include <complib/cl_event.h>
#include <opensm/osm_log.h>
#include <opensm/osm_madw.h>
#include <vendor/osm_vendor_mlx_sar.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

typedef struct _osmv_rmpp_send_ctx {

	uint8_t status;

	uint32_t window_first;
	uint32_t window_last;

	uint32_t mad_sz;
	boolean_t is_sa_mad;

	cl_event_t event;

	/* Segmentation engine */
	osmv_rmpp_sar_t sar;
	osm_log_t *p_log;

} osmv_rmpp_send_ctx_t;

typedef struct _osmv_rmpp_recv_ctx {

	boolean_t is_sa_mad;

	uint32_t expected_seg;

	/* Reassembly buffer */
	cl_qlist_t *p_rbuf;

	/* Reassembly engine */
	osmv_rmpp_sar_t sar;
	osm_log_t *p_log;

} osmv_rmpp_recv_ctx_t;

/*
 * NAME
 *   osmv_rmpp_send_ctx_init
 *
 * DESCRIPTION
 *  c'tor for rmpp_send_ctx obj
 *
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_rmpp_send_ctx_init(osmv_rmpp_send_ctx_t * p_ctx, void *arbt_mad,
			uint32_t mad_sz, osm_log_t * p_log);

/*
 * NAME
 *   osmv_rmpp_send_ctx_done
 *
 * DESCRIPTION
 *  d'tor for rmpp_send_ctx obj
 *
 * SEE ALSO
 *
 */
void osmv_rmpp_send_ctx_done(IN osmv_rmpp_send_ctx_t * ctx);

/*
 * NAME
 *   osmv_rmpp_send_ctx_get_wf
 *
 * DESCRIPTION
 *  returns number of first segment in current window
 * SEE ALSO
 *
 */
static inline uint32_t
osmv_rmpp_send_ctx_get_wf(IN const osmv_rmpp_send_ctx_t * p_ctx)
{
	CL_ASSERT(p_ctx);
	return p_ctx->window_first;
}

/*
 * NAME
 *   osmv_rmpp_send_ctx_set_wf
 *
 * DESCRIPTION
 *  sets number of first segment in current window
 * SEE ALSO
 *
 */
static inline void
osmv_rmpp_send_ctx_set_wf(IN osmv_rmpp_send_ctx_t * p_ctx, IN uint32_t val)
{
	CL_ASSERT(p_ctx);
	p_ctx->window_first = val;
}

/*
 * NAME
 *   osmv_rmpp_send_ctx_get_wl
 *
 * DESCRIPTION
 *  returns number of last segment in current window
 * SEE ALSO
 *
 */
static inline uint32_t
osmv_rmpp_send_ctx_get_wl(IN const osmv_rmpp_send_ctx_t * p_send_ctx)
{
	CL_ASSERT(p_send_ctx);
	return p_send_ctx->window_last;
}

/*
 * NAME
 *   osmv_rmpp_send_ctx_set_wl
 *
 * DESCRIPTION
 *  sets number of last segment in current window
 * SEE ALSO
 *
 */
static inline void
osmv_rmpp_send_ctx_set_wl(IN osmv_rmpp_send_ctx_t * p_ctx, IN uint32_t val)
{
	CL_ASSERT(p_ctx);
	p_ctx->window_last = val;
}

/*
 * NAME
 *   osmv_rmpp_send_ctx_get_num_segs
 *
 * DESCRIPTION
 *   returns the total number of mad segments to send
 * SEE ALSO
 *
 */
uint32_t osmv_rmpp_send_ctx_get_num_segs(IN osmv_rmpp_send_ctx_t * p_send_ctx);

/*
 * NAME
 *   osmv_rmpp_send_ctx_get_seg
 *
 * DESCRIPTION
 *   Retrieves the mad segment by seg number (including setting the mad relevant bits & hdrs)
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_rmpp_send_ctx_get_seg(IN osmv_rmpp_send_ctx_t * p_send_ctx,
			   IN uint32_t seg_idx, IN uint32_t resp_timeout,
			   OUT void *p_mad);

/*
 * NAME
 *   osmv_rmpp_recv_ctx_init
 *
 * DESCRIPTION
 *   c'tor for rmpp_recv_ctx obj
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_rmpp_recv_ctx_init(osmv_rmpp_recv_ctx_t * p_ctx, osm_log_t * p_log);

/*
 * NAME
 *   osmv_rmpp_recv_ctx_done
 *
 * DESCRIPTION
 *   d'tor for rmpp_recv_ctx obj
 * SEE ALSO
 *
 */
void osmv_rmpp_recv_ctx_done(IN osmv_rmpp_recv_ctx_t * p_ctx);

/*
 * NAME
 *   osmv_rmpp_recv_ctx_get_es
 *
 * DESCRIPTION
 *   retrunes index of expected segement in the curr window
 *
 */
static inline uint32_t
osmv_rmpp_recv_ctx_get_es(IN const osmv_rmpp_recv_ctx_t * p_recv_ctx)
{
	CL_ASSERT(p_recv_ctx);
	return p_recv_ctx->expected_seg;
}

/*
 * NAME
 *   osmv_rmpp_recv_ctx_set_es
 *
 * DESCRIPTION
 *   sets index of expected segement in the curr window
 *
 */
static inline void
osmv_rmpp_recv_ctx_set_es(IN osmv_rmpp_recv_ctx_t * p_recv_ctx, IN uint32_t val)
{
	CL_ASSERT(p_recv_ctx);
	p_recv_ctx->expected_seg = val;
}

/*
 * NAME
 *   osmv_rmpp_recv_ctx_store_madw_seg
 *
 * DESCRIPTION
 *  stores rmpp mad in the list
 *
 */
ib_api_status_t
osmv_rmpp_recv_ctx_store_mad_seg(IN osmv_rmpp_recv_ctx_t * p_recv_ctx,
				 IN void *p_mad);

uint32_t
osmv_rmpp_recv_ctx_get_cur_byte_num(IN osmv_rmpp_recv_ctx_t * p_recv_ctx);

uint32_t
osmv_rmpp_recv_ctx_get_byte_num_from_first(IN osmv_rmpp_recv_ctx_t *
					   p_recv_ctx);

uint32_t
osmv_rmpp_recv_ctx_get_byte_num_from_last(IN osmv_rmpp_recv_ctx_t * p_recv_ctx);

/*
 * NAME
 *   osmv_rmpp_recv_ctx_reassemble_arbt_mad
 *
 * DESCRIPTION
 *  reassembles all rmpp buffs to one big arbitrary mad
 */
ib_api_status_t
osmv_rmpp_recv_ctx_reassemble_arbt_mad(IN osmv_rmpp_recv_ctx_t * p_recv_ctx,
				       IN uint32_t size, IN void *p_arbt_mad);

END_C_DECLS
#endif
