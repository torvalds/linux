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

#ifndef _OSMV_SAR_H_
#define _OSMV_SAR_H_

#include <iba/ib_types.h>
#include <complib/cl_qlist.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

typedef struct _osmv_rmpp_sar {
	void *p_arbt_mad;
	uint32_t data_len;	/* total data len in all the mads */
	/* these data members contain only constants */
	uint32_t hdr_sz;
	uint32_t data_sz;	/*typical data sz for this kind of mad (sa or regular */

} osmv_rmpp_sar_t;

/*
 * NAME
 *   osmv_rmpp_sar_alloc
 *
 * DESCRIPTION
 *   c'tor for rmpp_sar object
 *
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_rmpp_sar_init(osmv_rmpp_sar_t * p_sar, void *p_arbt_mad,
		   uint32_t mad_size, boolean_t is_sa_mad);

/*
 * NAME
 *   osmv_rmpp_sar_dealloc
 *
 * DESCRIPTION
 *   d'tor for rmpp_sar object
 *
 * SEE ALSO
 *
 */
void osmv_rmpp_sar_done(osmv_rmpp_sar_t * p_sar);

/*
 * NAME
 *   osmv_rmpp_sar_get_mad_seg
 *
 * DESCRIPTION
 *  segments the original mad buffer . returnes a mad with the data of the i-th segment
 *
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_rmpp_sar_get_mad_seg(osmv_rmpp_sar_t * p_sar, uint32_t seg_idx,
			  void *p_buf);

/*
 * NAME
 *   osmv_rmpp_sar_reassemble_arbt_mad
 *
 * DESCRIPTION
 *  gets a qlist of mads and reassmbles to one big mad buffer
 *  ALSO - deallocates the mad list
 *
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_rmpp_sar_reassemble_arbt_mad(osmv_rmpp_sar_t * p_sar, cl_qlist_t * p_bufs);

END_C_DECLS
#endif				/* _OSMV_SAR_H_ */
