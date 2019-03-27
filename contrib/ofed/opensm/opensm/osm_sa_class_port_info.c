/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2013 Oracle and/or its affiliates. All rights reserved.
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
 *    Implementation of osm_cpi_rcv_t.
 * This object represents the ClassPortInfo Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SA_CLASS_PORT_INFO_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_sa.h>

#define MAX_MSECS_TO_RTV 24
/* Precalculated table in msec (index is related to encoded value) */
/* 4.096 usec * 2 ** n (where n = 8 - 31) */
const static uint32_t msecs_to_rtv_table[MAX_MSECS_TO_RTV] = {
	1, 2, 4, 8,
	16, 33, 67, 134,
	268, 536, 1073, 2147,
	4294, 8589, 17179, 34359,
	68719, 137438, 274877, 549755,
	1099511, 2199023, 4398046, 8796093
};

static void cpi_rcv_respond(IN osm_sa_t * sa, IN const osm_madw_t * p_madw)
{
	osm_madw_t *p_resp_madw;
	const ib_sa_mad_t *p_sa_mad;
	ib_sa_mad_t *p_resp_sa_mad;
	ib_class_port_info_t *p_resp_cpi;
	ib_gid_t zero_gid;
	uint32_t cap_mask2;
	uint8_t rtv;

	OSM_LOG_ENTER(sa->p_log);

	memset(&zero_gid, 0, sizeof(ib_gid_t));

	/*
	   Get a MAD to reply. Address of Mad is in the received mad_wrapper
	 */
	p_resp_madw = osm_mad_pool_get(sa->p_mad_pool, p_madw->h_bind,
				       MAD_BLOCK_SIZE, &p_madw->mad_addr);
	if (!p_resp_madw) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1408: "
			"Unable to allocate MAD\n");
		goto Exit;
	}

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_resp_sa_mad = osm_madw_get_sa_mad_ptr(p_resp_madw);

	memcpy(p_resp_sa_mad, p_sa_mad, IB_SA_MAD_HDR_SIZE);
	p_resp_sa_mad->method |= IB_MAD_METHOD_RESP_MASK;
	/* C15-0.1.5 - always return SM_Key = 0 (table 185 p 884) */
	p_resp_sa_mad->sm_key = 0;

	p_resp_cpi =
	    (ib_class_port_info_t *) ib_sa_mad_get_payload_ptr(p_resp_sa_mad);

	/* finally do it (the job) man ! */
	p_resp_cpi->base_ver = 1;
	p_resp_cpi->class_ver = 2;
	/* Calculate encoded response time value */
	/* transaction timeout is in msec */
	if (sa->p_subn->opt.transaction_timeout >
	    msecs_to_rtv_table[MAX_MSECS_TO_RTV - 1])
		rtv = MAX_MSECS_TO_RTV - 1;
	else {
		for (rtv = 0; rtv < MAX_MSECS_TO_RTV; rtv++) {
			if (sa->p_subn->opt.transaction_timeout <=
			    msecs_to_rtv_table[rtv])
				break;
		}
	}
	rtv += 8;
	ib_class_set_resp_time_val(p_resp_cpi, rtv);
	p_resp_cpi->redir_gid = zero_gid;
	p_resp_cpi->redir_tc_sl_fl = 0;
	p_resp_cpi->redir_lid = 0;
	p_resp_cpi->redir_pkey = 0;
	p_resp_cpi->redir_qp = CL_NTOH32(1);
	p_resp_cpi->redir_qkey = IB_QP1_WELL_KNOWN_Q_KEY;
	p_resp_cpi->trap_gid = zero_gid;
	p_resp_cpi->trap_tc_sl_fl = 0;
	p_resp_cpi->trap_lid = 0;
	p_resp_cpi->trap_pkey = 0;
	p_resp_cpi->trap_hop_qp = 0;
	p_resp_cpi->trap_qkey = IB_QP1_WELL_KNOWN_Q_KEY;

	/* set specific capability mask bits */
	/* we do not support the following options/optional records:
	   OSM_CAP_IS_SUBN_OPT_RECS_SUP :
	   RandomForwardingTableRecord,
	   ServiceAssociationRecord
	   other optional records supported "under the table"

	   OSM_CAP_IS_MULTIPATH_SUP:
	   TraceRecord

	   OSM_CAP_IS_REINIT_SUP:
	   For reinitialization functionality.

	   So not sending traps, but supporting Get(Notice) and Set(Notice).
	 */

	/* Note host notation replaced later */
#if defined (VENDOR_RMPP_SUPPORT) && defined (DUAL_SIDED_RMPP)
	p_resp_cpi->cap_mask = OSM_CAP_IS_SUBN_GET_SET_NOTICE_SUP |
	    OSM_CAP_IS_PORT_INFO_CAPMASK_MATCH_SUPPORTED |
	    OSM_CAP_IS_MULTIPATH_SUP;
#else
	p_resp_cpi->cap_mask = OSM_CAP_IS_SUBN_GET_SET_NOTICE_SUP |
	    OSM_CAP_IS_PORT_INFO_CAPMASK_MATCH_SUPPORTED;
#endif
	cap_mask2 = OSM_CAP2_IS_FULL_PORTINFO_REC_SUPPORTED |
		    OSM_CAP2_IS_EXTENDED_SPEEDS_SUPPORTED |
		    OSM_CAP2_IS_ALIAS_GUIDS_SUPPORTED |
		    OSM_CAP2_IS_MULTICAST_SERVICE_RECS_SUPPORTED |
		    OSM_CAP2_IS_PORT_INFO_CAPMASK2_MATCH_SUPPORTED |
		    OSM_CAP2_IS_LINK_WIDTH_2X_SUPPORTED;
	if (sa->p_subn->opt.use_mfttop)
		cap_mask2 |= OSM_CAP2_IS_MCAST_TOP_SUPPORTED;
	if (sa->p_subn->opt.qos)
		cap_mask2 |= OSM_CAP2_IS_QOS_SUPPORTED;
	ib_class_set_cap_mask2(p_resp_cpi, cap_mask2);

	if (!sa->p_subn->opt.disable_multicast)
		p_resp_cpi->cap_mask |= OSM_CAP_IS_UD_MCAST_SUP;
	p_resp_cpi->cap_mask = cl_hton16(p_resp_cpi->cap_mask);

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_FRAMES))
		osm_dump_sa_mad_v2(sa->p_log, p_resp_sa_mad, FILE_ID, OSM_LOG_FRAMES);

	osm_sa_send(sa, p_resp_madw, FALSE);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 * This code actually handles the call
 **********************************************************************/
void osm_cpi_rcv_process(IN void *context, IN void *data)
{
	osm_sa_t *sa = context;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *p_sa_mad;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);

	/* we only support GET */
	if (p_sa_mad->method != IB_MAD_METHOD_GET) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1403: "
			"Unsupported Method (%s) for ClassPortInfo request\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_CLASS_PORT_INFO);

	/* CLASS PORT INFO does not really look at the SMDB - no lock required. */

	cpi_rcv_respond(sa, p_madw);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}
