/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
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
 *    Implementation of osm_resp_t.
 * This object represents the generic attribute responder.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_RESP_C
#include <opensm/osm_madw.h>
#include <opensm/osm_attrib_req.h>
#include <opensm/osm_log.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_vl15intf.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_opensm.h>

static void resp_make_resp_smp(IN osm_sm_t * sm, IN const ib_smp_t * p_src_smp,
			       IN ib_net16_t status,
			       IN const uint8_t * p_payload,
			       OUT ib_smp_t * p_dest_smp)
{
	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_dest_smp);
	CL_ASSERT(p_src_smp);
	CL_ASSERT(!ib_smp_is_response(p_src_smp));

	*p_dest_smp = *p_src_smp;
	if (p_src_smp->method == IB_MAD_METHOD_GET ||
	    p_src_smp->method == IB_MAD_METHOD_SET) {
		p_dest_smp->method = IB_MAD_METHOD_GET_RESP;
		p_dest_smp->status = status;
	} else if (p_src_smp->method == IB_MAD_METHOD_TRAP) {
		p_dest_smp->method = IB_MAD_METHOD_TRAP_REPRESS;
		p_dest_smp->status = 0;
	} else {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 1302: "
			"src smp method unsupported 0x%X\n", p_src_smp->method);
		goto Exit;
	}

	if (p_src_smp->mgmt_class == IB_MCLASS_SUBN_DIR)
		p_dest_smp->status |= IB_SMP_DIRECTION;

	p_dest_smp->dr_dlid = p_dest_smp->dr_slid;
	p_dest_smp->dr_slid = p_dest_smp->dr_dlid;
	memcpy(&p_dest_smp->data, p_payload, IB_SMP_DATA_SIZE);

Exit:
	OSM_LOG_EXIT(sm->p_log);
}

ib_api_status_t osm_resp_send(IN osm_sm_t * sm,
			      IN const osm_madw_t * p_req_madw,
			      IN ib_net16_t mad_status,
			      IN const uint8_t * p_payload)
{
	const ib_smp_t *p_req_smp;
	ib_smp_t *p_smp;
	osm_madw_t *p_madw;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_req_madw);
	CL_ASSERT(p_payload);

	/* do nothing if we are exiting ... */
	if (osm_exit_flag)
		goto Exit;

	p_madw = osm_mad_pool_get(sm->p_mad_pool,
				  osm_madw_get_bind_handle(p_req_madw),
				  MAD_BLOCK_SIZE, NULL);

	if (p_madw == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 1301: Unable to acquire MAD\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	/*
	   Copy the request smp to the response smp, then just
	   update the necessary fields.
	 */
	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_req_smp = osm_madw_get_smp_ptr(p_req_madw);
	resp_make_resp_smp(sm, p_req_smp, mad_status, p_payload, p_smp);
	p_madw->mad_addr.dest_lid =
	    p_req_madw->mad_addr.addr_type.smi.source_lid;
	p_madw->mad_addr.addr_type.smi.source_lid =
	    p_req_madw->mad_addr.dest_lid;

	p_madw->resp_expected = FALSE;
	p_madw->fail_msg = CL_DISP_MSGID_NONE;

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "Responding to %s (0x%X)"
		"\n\t\t\t\tattribute modifier 0x%X, TID 0x%" PRIx64 "\n",
		ib_get_sm_attr_str(p_smp->attr_id), cl_ntoh16(p_smp->attr_id),
		cl_ntoh32(p_smp->attr_mod), cl_ntoh64(p_smp->trans_id));

	osm_vl15_post(sm->p_vl15, p_madw);

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return status;
}
