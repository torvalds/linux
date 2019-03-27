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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qlist.h>

#include <vendor/osm_vendor_mlx_rmpp_ctx.h>
#include <vendor/osm_vendor_mlx_svc.h>

ib_api_status_t
osmv_rmpp_send_ctx_init(osmv_rmpp_send_ctx_t * p_ctx, void *p_arbt_mad,
			uint32_t mad_sz, osm_log_t * p_log)
{
	ib_api_status_t st = IB_SUCCESS;
	cl_status_t cl_st;

	CL_ASSERT(p_ctx);
	if (NULL == p_arbt_mad) {
		return IB_INVALID_PARAMETER;
	}

	if (osmv_mad_is_sa((ib_mad_t *) p_arbt_mad)) {
		p_ctx->is_sa_mad = TRUE;
	} else
		p_ctx->is_sa_mad = FALSE;

	p_ctx->mad_sz = mad_sz;

	cl_event_construct(&p_ctx->event);
	cl_st = cl_event_init(&p_ctx->event, FALSE);
	if (cl_st != CL_SUCCESS) {
		return IB_ERROR;
	}

	st = osmv_rmpp_sar_init(&p_ctx->sar, p_arbt_mad, p_ctx->mad_sz,
				p_ctx->is_sa_mad);
	if (st == IB_SUCCESS) {
		p_ctx->window_first = 1;
		p_ctx->window_last = 1;
	}

	p_ctx->p_log = p_log;
	return st;
}

void osmv_rmpp_send_ctx_done(IN osmv_rmpp_send_ctx_t * p_ctx)
{
	CL_ASSERT(p_ctx);
	cl_event_destroy(&p_ctx->event);
	osmv_rmpp_sar_done(&p_ctx->sar);
	free(p_ctx);
}

uint32_t osmv_rmpp_send_ctx_get_num_segs(IN osmv_rmpp_send_ctx_t * p_send_ctx)
{
	uint32_t data_len, data_sz, num;

	CL_ASSERT(p_send_ctx);

	if (p_send_ctx->is_sa_mad) {
		data_len = p_send_ctx->mad_sz - IB_SA_MAD_HDR_SIZE;
		data_sz = IB_SA_DATA_SIZE;
	} else {
		data_len = p_send_ctx->mad_sz - MAD_RMPP_HDR_SIZE;
		data_sz = MAD_RMPP_DATA_SIZE;
	}

	num = data_len / data_sz;
	if (0 == data_len || (data_len % data_sz) > 0) {
		num++;
	}

	return num;
}

ib_api_status_t
osmv_rmpp_send_ctx_get_seg(IN osmv_rmpp_send_ctx_t * p_send_ctx,
			   IN uint32_t seg_idx,
			   IN uint32_t resp_timeout, OUT void *p_buf)
{
	ib_api_status_t st = IB_SUCCESS;
	uint32_t num_segs, paylen = 0;
	ib_rmpp_mad_t *p_rmpp_mad;

	OSM_LOG_ENTER(p_send_ctx->p_log);
	CL_ASSERT(p_send_ctx);

	st = osmv_rmpp_sar_get_mad_seg(&p_send_ctx->sar, seg_idx, p_buf);
	if (st != IB_SUCCESS) {
		goto Exit;
	}

	p_rmpp_mad = (ib_rmpp_mad_t *) p_buf;
	/* Set the relevant bits in the RMPP hdr */
	p_rmpp_mad->rmpp_status = IB_RMPP_STATUS_SUCCESS;
	p_rmpp_mad->rmpp_flags |= IB_RMPP_FLAG_ACTIVE;
	p_rmpp_mad->rmpp_flags |= resp_timeout << 3;

	num_segs = osmv_rmpp_send_ctx_get_num_segs(p_send_ctx);

	if (1 == seg_idx) {
		p_rmpp_mad->rmpp_flags |= IB_RMPP_FLAG_FIRST;

		/* This is the first segment -
		   the reported paylen is the total amount of data.
		 */
		if (p_send_ctx->is_sa_mad) {
			/* sa mad hdr sz */
			paylen = p_send_ctx->mad_sz - IB_SA_MAD_HDR_SIZE;
			paylen +=
			    num_segs * (IB_SA_MAD_HDR_SIZE - MAD_RMPP_HDR_SIZE);
		} else {
			/* mad hdr sz */
			paylen = p_send_ctx->mad_sz - MAD_RMPP_HDR_SIZE;
		}
	}

	if (seg_idx == num_segs) {
		p_rmpp_mad->rmpp_flags |= IB_RMPP_FLAG_LAST;

		/*
		   This is the last segment -
		   the reported paylen is only the amount of data left on this segment.
		 */
		if (p_send_ctx->is_sa_mad) {
			paylen = p_send_ctx->mad_sz - IB_SA_MAD_HDR_SIZE;
			paylen -= (num_segs - 1) * IB_SA_DATA_SIZE;
			paylen += (IB_SA_MAD_HDR_SIZE - MAD_RMPP_HDR_SIZE);
		} else {
			paylen = p_send_ctx->mad_sz - MAD_RMPP_HDR_SIZE;
			paylen -=
			    (num_segs - 1) * (MAD_BLOCK_SIZE -
					      MAD_RMPP_HDR_SIZE);
		}
	}

	p_rmpp_mad->rmpp_type = IB_RMPP_TYPE_DATA;
	p_rmpp_mad->rmpp_version = 1;
	p_rmpp_mad->paylen_newwin = cl_ntoh32(paylen);
	p_rmpp_mad->seg_num = cl_ntoh32(seg_idx);

Exit:
	OSM_LOG_EXIT(p_send_ctx->p_log);
	return st;
}

ib_api_status_t
osmv_rmpp_recv_ctx_init(osmv_rmpp_recv_ctx_t * p_ctx, osm_log_t * p_log)
{
	ib_api_status_t st = IB_SUCCESS;

	CL_ASSERT(p_ctx);

	p_ctx->is_sa_mad = FALSE;

	p_ctx->p_rbuf = malloc(sizeof(cl_qlist_t));
	if (p_ctx->p_rbuf) {
		memset(p_ctx->p_rbuf, 0, sizeof(cl_qlist_t));
		cl_qlist_init(p_ctx->p_rbuf);
		p_ctx->expected_seg = 1;
	} else
		st = IB_INSUFFICIENT_MEMORY;

	p_ctx->p_log = p_log;

	return st;
}

void osmv_rmpp_recv_ctx_done(IN osmv_rmpp_recv_ctx_t * p_ctx)
{
	cl_list_item_t *p_list_item;
	cl_list_obj_t *p_obj;

	CL_ASSERT(p_ctx);

	/* go over all the items in the list and remove them */
	p_list_item = cl_qlist_remove_head(p_ctx->p_rbuf);
	while (p_list_item != cl_qlist_end(p_ctx->p_rbuf)) {

		p_obj = PARENT_STRUCT(p_list_item, cl_list_obj_t, list_item);

		free(cl_qlist_obj(p_obj));
		free(p_obj);

		p_list_item = cl_qlist_remove_head(p_ctx->p_rbuf);
	}

	osmv_rmpp_sar_done(&p_ctx->sar);

	free(p_ctx->p_rbuf);
	free(p_ctx);
}

ib_api_status_t
osmv_rmpp_recv_ctx_store_mad_seg(IN osmv_rmpp_recv_ctx_t * p_recv_ctx,
				 IN void *p_mad)
{
	cl_list_obj_t *p_obj = NULL;
	void *p_list_mad;

	OSM_LOG_ENTER(p_recv_ctx->p_log);

	CL_ASSERT(p_recv_ctx);
	p_list_mad = malloc(MAD_BLOCK_SIZE);
	if (NULL == p_list_mad) {
		return IB_INSUFFICIENT_MEMORY;
	}
	memset(p_list_mad, 0, MAD_BLOCK_SIZE);
	memcpy(p_list_mad, p_mad, MAD_BLOCK_SIZE);

	p_obj = malloc(sizeof(cl_list_obj_t));
	if (NULL == p_obj) {
		free(p_list_mad);
		return IB_INSUFFICIENT_MEMORY;
	}
	memset(p_obj, 0, sizeof(cl_list_obj_t));
	cl_qlist_set_obj(p_obj, p_list_mad);

	cl_qlist_insert_tail(p_recv_ctx->p_rbuf, &p_obj->list_item);

	if (osmv_mad_is_sa((ib_mad_t *) p_mad)) {
		p_recv_ctx->is_sa_mad = TRUE;
	}

	return IB_SUCCESS;

}

uint32_t
osmv_rmpp_recv_ctx_get_cur_byte_num(IN osmv_rmpp_recv_ctx_t * p_recv_ctx)
{
	uint32_t num_segs;

	num_segs = cl_qlist_count(p_recv_ctx->p_rbuf);
	if (p_recv_ctx->is_sa_mad)
		return ((num_segs * IB_SA_DATA_SIZE) + IB_SA_MAD_HDR_SIZE);
	else
		return ((num_segs * MAD_RMPP_DATA_SIZE) + MAD_RMPP_HDR_SIZE);
}

uint32_t
osmv_rmpp_recv_ctx_get_byte_num_from_first(IN osmv_rmpp_recv_ctx_t * p_recv_ctx)
{
	cl_list_item_t *p_item;
	cl_list_obj_t *p_obj;
	void *p_list_mad;
	uint32_t num_bytes, num_segs;

	p_item = cl_qlist_head(p_recv_ctx->p_rbuf);
	p_obj = PARENT_STRUCT(p_item, cl_list_obj_t, list_item);
	p_list_mad = cl_qlist_obj(p_obj);

	/* mad data sz */
	num_bytes = cl_ntoh32(((ib_rmpp_mad_t *) p_list_mad)->paylen_newwin);
	if (0 != num_bytes) {
		if (p_recv_ctx->is_sa_mad) {
			/* sa mad hdr sz */
			num_segs = cl_qlist_count(p_recv_ctx->p_rbuf);
			num_bytes -=
			    num_segs * (IB_SA_MAD_HDR_SIZE - MAD_RMPP_HDR_SIZE);
			num_bytes += IB_SA_MAD_HDR_SIZE;
		} else {
			/* mad hdr sz */
			num_bytes += MAD_RMPP_HDR_SIZE;
		}
	}

	return num_bytes;
}

uint32_t
osmv_rmpp_recv_ctx_get_byte_num_from_last(IN osmv_rmpp_recv_ctx_t * p_recv_ctx)
{
	cl_list_item_t *p_item;
	cl_list_obj_t *p_obj;
	void *p_list_mad;
	uint32_t num_bytes, num_segs;

	p_item = cl_qlist_tail(p_recv_ctx->p_rbuf);
	p_obj = PARENT_STRUCT(p_item, cl_list_obj_t, list_item);
	p_list_mad = cl_qlist_obj(p_obj);

	/* mad data sz */
	num_segs = cl_qlist_count(p_recv_ctx->p_rbuf);
	num_bytes = cl_ntoh32(((ib_rmpp_mad_t *) p_list_mad)->paylen_newwin);

	if (0 != num_bytes) {
		if (p_recv_ctx->is_sa_mad) {
			/* sa mad hdr sz */
			num_bytes += MAD_RMPP_HDR_SIZE;
			num_bytes += (num_segs - 1) * IB_SA_DATA_SIZE;
		} else {
			/* mad hdr sz */
			num_bytes += MAD_RMPP_HDR_SIZE;
			num_bytes += (num_segs - 1) * MAD_RMPP_DATA_SIZE;
		}
	}

	return num_bytes;
}

/* assuming that the last rmpp pkt arrived so that data member: total_bytes has the right value */
ib_api_status_t
osmv_rmpp_recv_ctx_reassemble_arbt_mad(IN osmv_rmpp_recv_ctx_t * p_recv_ctx,
				       IN uint32_t size, IN void *p_arbt_mad)
{
	ib_api_status_t st = IB_SUCCESS;

	CL_ASSERT(p_recv_ctx);

	st = osmv_rmpp_sar_init(&p_recv_ctx->sar, p_arbt_mad, size,
				p_recv_ctx->is_sa_mad);
	if (st != IB_SUCCESS) {
		return st;
	}

	st = osmv_rmpp_sar_reassemble_arbt_mad(&p_recv_ctx->sar,
					       p_recv_ctx->p_rbuf);

	osmv_rmpp_sar_done(&p_recv_ctx->sar);

	return st;
}
