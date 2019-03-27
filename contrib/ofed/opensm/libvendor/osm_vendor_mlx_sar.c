/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
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
#include <stdio.h>
#include <vendor/osm_vendor_mlx_sar.h>

ib_api_status_t
osmv_rmpp_sar_init(osmv_rmpp_sar_t * p_sar, void *p_arbt_mad,
		   uint32_t mad_size, boolean_t is_sa_mad)
{
	CL_ASSERT(p_sar);
	p_sar->p_arbt_mad = p_arbt_mad;
	if (is_sa_mad) {
		p_sar->data_len = mad_size - IB_SA_MAD_HDR_SIZE;
		p_sar->hdr_sz = IB_SA_MAD_HDR_SIZE;
		p_sar->data_sz = IB_SA_DATA_SIZE;
	} else {
		p_sar->data_len = mad_size - MAD_RMPP_HDR_SIZE;
		p_sar->hdr_sz = MAD_RMPP_HDR_SIZE;
		p_sar->data_sz = MAD_RMPP_DATA_SIZE;
	}
	return IB_SUCCESS;
}

void osmv_rmpp_sar_done(osmv_rmpp_sar_t * p_sar)
{
	p_sar->p_arbt_mad = NULL;
}

/* the big mad should be with mad header, rmpp header ( &sa hdr) space */
ib_api_status_t
osmv_rmpp_sar_get_mad_seg(IN osmv_rmpp_sar_t * p_sar,
			  IN uint32_t seg_idx, OUT void *p_buf)
{
	void *p_seg;
	uint32_t sz_left;
	uint32_t num_segs;

	CL_ASSERT(p_sar);

	num_segs = p_sar->data_len / p_sar->data_sz;
	if ((p_sar->data_len % p_sar->data_sz) > 0) {
		num_segs++;
	}

	if ((seg_idx > num_segs) && (seg_idx != 1)) {
		return IB_NOT_FOUND;
	}

	/* cleanup */
	memset(p_buf, 0, MAD_BLOCK_SIZE);

	/* attach header */
	memcpy(p_buf, p_sar->p_arbt_mad, p_sar->hdr_sz);

	/* fill data */
	p_seg =
	    (char *)p_sar->p_arbt_mad + p_sar->hdr_sz +
	    ((seg_idx - 1) * p_sar->data_sz);
	sz_left = p_sar->data_len - ((seg_idx - 1) * p_sar->data_sz);
	if (sz_left > p_sar->data_sz)
		memcpy((char *)p_buf + p_sar->hdr_sz, (char *)p_seg,
		       p_sar->data_sz);
	else
		memcpy((char *)p_buf + p_sar->hdr_sz, (char *)p_seg, sz_left);

	return IB_SUCCESS;
}

/* turns a list of mads to one big mad - including header */
/* ALSO - deallocates the list                              */
ib_api_status_t
osmv_rmpp_sar_reassemble_arbt_mad(osmv_rmpp_sar_t * p_sar, cl_qlist_t * p_bufs)
{
	void *buf_tmp, *p_mad;
	cl_list_item_t *p_item;
	cl_list_obj_t *p_obj;
	uint32_t space_left = p_sar->data_len + p_sar->hdr_sz;

	CL_ASSERT(p_sar);
	CL_ASSERT(FALSE == cl_is_qlist_empty(p_bufs));

	/* attach header */
	p_mad = p_sar->p_arbt_mad;
	p_item = cl_qlist_head(p_bufs);
	p_obj = PARENT_STRUCT(p_item, cl_list_obj_t, list_item);
	buf_tmp = cl_qlist_obj(p_obj);
	memcpy(p_mad, buf_tmp, p_sar->hdr_sz);
	p_mad = (char *)p_mad + p_sar->hdr_sz;
	space_left -= p_sar->hdr_sz;

	/* reassemble data */
	while (FALSE == cl_is_qlist_empty(p_bufs)) {

		p_item = cl_qlist_remove_head(p_bufs);
		p_obj = PARENT_STRUCT(p_item, cl_list_obj_t, list_item);
		buf_tmp = cl_qlist_obj(p_obj);

		if (FALSE == cl_is_qlist_empty(p_bufs)) {
			memcpy((char *)p_mad, (char *)buf_tmp + p_sar->hdr_sz,
			       p_sar->data_sz);
			p_mad = (char *)p_mad + p_sar->data_sz;
			space_left -= p_sar->data_sz;
		} else {
			/* the last mad on the list */
			memcpy((char *)p_mad, (char *)buf_tmp + p_sar->hdr_sz,
			       space_left);
			p_mad = (char *)p_mad + space_left;
		}

		free(buf_tmp);
		free(p_obj);
	}

	return IB_SUCCESS;
}
