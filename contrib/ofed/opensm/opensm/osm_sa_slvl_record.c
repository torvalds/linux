/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of osm_slvl_rec_rcv_t.
 * This object represents the SLtoVL Mapping Query Receiver object.
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
#define FILE_ID OSM_FILE_SA_SLVL_RECORD_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_sa.h>

#define SA_SLVL_RESP_SIZE SA_ITEM_RESP_SIZE(slvl_rec)

typedef struct osm_slvl_search_ctxt {
	const ib_slvl_table_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	uint8_t in_port_num;
	cl_qlist_t *p_list;
	osm_sa_t *sa;
	const osm_physp_t *p_req_physp;
} osm_slvl_search_ctxt_t;

static void sa_slvl_create(IN osm_sa_t * sa, IN const osm_physp_t * p_physp,
			   IN osm_slvl_search_ctxt_t * p_ctxt,
			   IN uint8_t in_port_idx)
{
	osm_sa_item_t *p_rec_item;
	uint16_t lid;

	OSM_LOG_ENTER(sa->p_log);

	p_rec_item = malloc(SA_SLVL_RESP_SIZE);
	if (p_rec_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2602: "
			"rec_item alloc failed\n");
		goto Exit;
	}

	if (p_physp->p_node->node_info.node_type != IB_NODE_TYPE_SWITCH)
		lid = p_physp->port_info.base_lid;
	else
		lid = osm_node_get_base_lid(p_physp->p_node, 0);

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"New SLtoVL Map for: OUT port 0x%016" PRIx64
		", lid 0x%X, port %u to In Port:%u\n",
		cl_ntoh64(osm_physp_get_port_guid(p_physp)),
		cl_ntoh16(lid), osm_physp_get_port_num(p_physp), in_port_idx);

	memset(p_rec_item, 0, SA_SLVL_RESP_SIZE);

	p_rec_item->resp.slvl_rec.lid = lid;
	if (p_physp->p_node->node_info.node_type == IB_NODE_TYPE_SWITCH) {
		p_rec_item->resp.slvl_rec.out_port_num = osm_physp_get_port_num(p_physp);
		p_rec_item->resp.slvl_rec.in_port_num = in_port_idx;
	}
	p_rec_item->resp.slvl_rec.slvl_tbl =
	    *(osm_physp_get_slvl_tbl(p_physp, in_port_idx));

	cl_qlist_insert_tail(p_ctxt->p_list, &p_rec_item->list_item);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static void sa_slvl_by_comp_mask(IN osm_sa_t * sa, IN const osm_port_t * p_port,
				 osm_slvl_search_ctxt_t * p_ctxt)
{
	const ib_slvl_table_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	const osm_physp_t *p_out_physp, *p_in_physp;
	uint8_t in_port_num, out_port_num;
	uint8_t num_ports;
	uint8_t in_port_start, in_port_end;
	uint8_t out_port_start, out_port_end;
	const osm_physp_t *p_req_physp;

	OSM_LOG_ENTER(sa->p_log);

	p_rcvd_rec = p_ctxt->p_rcvd_rec;
	comp_mask = p_ctxt->comp_mask;
	num_ports = osm_node_get_num_physp(p_port->p_node);
	in_port_start = 0;
	in_port_end = num_ports - 1;
	out_port_start = 0;
	out_port_end = num_ports - 1;
	p_req_physp = p_ctxt->p_req_physp;

	if (p_port->p_node->node_info.node_type != IB_NODE_TYPE_SWITCH) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Using Physical Default Port Number: 0x%X (for End Node)\n",
			p_port->p_physp->port_num);
		p_out_physp = p_port->p_physp;
		/* check that the p_out_physp and the p_req_physp share a pkey */
		if (osm_physp_share_pkey(sa->p_log, p_req_physp, p_out_physp,
					 sa->p_subn->opt.allow_both_pkeys))
			sa_slvl_create(sa, p_out_physp, p_ctxt, 0);
	} else {
		if (comp_mask & IB_SLVL_COMPMASK_OUT_PORT)
			out_port_start = out_port_end =
			    p_rcvd_rec->out_port_num;
		if (comp_mask & IB_SLVL_COMPMASK_IN_PORT)
			in_port_start = in_port_end = p_rcvd_rec->in_port_num;

		for (out_port_num = out_port_start;
		     out_port_num <= out_port_end; out_port_num++) {
			p_out_physp =
			    osm_node_get_physp_ptr(p_port->p_node,
						   out_port_num);
			if (!p_out_physp)
				continue;

			for (in_port_num = in_port_start;
			     in_port_num <= in_port_end; in_port_num++) {
#if 0
				if (out_port_num && out_port_num == in_port_num)
					continue;
#endif

				p_in_physp =
				    osm_node_get_physp_ptr(p_port->p_node,
							   in_port_num);
				if (!p_in_physp)
					continue;

				/* if the requester and the p_out_physp don't share a pkey -
				   continue */
				if (!osm_physp_share_pkey(sa->p_log, p_req_physp, p_out_physp,
							  sa->p_subn->opt.allow_both_pkeys))
					continue;

				sa_slvl_create(sa, p_out_physp, p_ctxt,
					       in_port_num);
			}
		}
	}
	OSM_LOG_EXIT(sa->p_log);
}

static void sa_slvl_by_comp_mask_cb(IN cl_map_item_t * p_map_item, IN void *cxt)
{
	const osm_port_t *p_port = (osm_port_t *) p_map_item;
	osm_slvl_search_ctxt_t *p_ctxt = cxt;

	sa_slvl_by_comp_mask(p_ctxt->sa, p_port, p_ctxt);
}

void osm_slvl_rec_rcv_process(IN void *ctx, IN void *data)
{
	osm_sa_t *sa = ctx;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *p_rcvd_mad;
	const ib_slvl_table_record_t *p_rcvd_rec;
	const osm_port_t *p_port = NULL;
	cl_qlist_t rec_list;
	osm_slvl_search_ctxt_t context;
	ib_api_status_t status = IB_SUCCESS;
	ib_net64_t comp_mask;
	osm_physp_t *p_req_physp;

	CL_ASSERT(sa);

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec =
	    (ib_slvl_table_record_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);
	comp_mask = p_rcvd_mad->comp_mask;

	CL_ASSERT(p_rcvd_mad->attr_id == IB_MAD_ATTR_SLVL_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if (p_rcvd_mad->method != IB_MAD_METHOD_GET &&
	    p_rcvd_mad->method != IB_MAD_METHOD_GETTABLE) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2604: "
			"Unsupported Method (%s) for SL2VLRecord request\n",
			ib_get_sa_method_str(p_rcvd_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		goto Exit;
	}

	cl_plock_acquire(sa->p_lock);

	/* update the requester physical port */
	p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		cl_plock_release(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2603: "
			"Cannot find requester physical port\n");
		goto Exit;
	}
	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Requester port GUID 0x%" PRIx64 "\n",
		cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));

	cl_qlist_init(&rec_list);

	context.p_rcvd_rec = p_rcvd_rec;
	context.p_list = &rec_list;
	context.comp_mask = p_rcvd_mad->comp_mask;
	context.sa = sa;
	context.in_port_num = p_rcvd_rec->in_port_num;
	context.p_req_physp = p_req_physp;

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Got Query Lid:%u(%02X), In-Port:0x%02X(%02X), Out-Port:0x%02X(%02X)\n",
		cl_ntoh16(p_rcvd_rec->lid),
		(comp_mask & IB_SLVL_COMPMASK_LID) != 0,
		p_rcvd_rec->in_port_num,
		(comp_mask & IB_SLVL_COMPMASK_IN_PORT) != 0,
		p_rcvd_rec->out_port_num,
		(comp_mask & IB_SLVL_COMPMASK_OUT_PORT) != 0);

	/*
	   If the user specified a LID, it obviously narrows our
	   work load, since we don't have to search every port
	 */
	if (comp_mask & IB_SLVL_COMPMASK_LID) {
		p_port = osm_get_port_by_lid(sa->p_subn, p_rcvd_rec->lid);
		if (!p_port) {
			status = IB_NOT_FOUND;
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2608: "
				"No port found with LID %u\n",
				cl_ntoh16(p_rcvd_rec->lid));
		}
	}

	if (status == IB_SUCCESS) {
		/* if we have a unique port - no need for a port search */
		if (p_port)
			/*  this does the loop on all the port phys ports */
			sa_slvl_by_comp_mask(sa, p_port, &context);
		else
			cl_qmap_apply_func(&sa->p_subn->port_guid_tbl,
					   sa_slvl_by_comp_mask_cb, &context);
	}

	cl_plock_release(sa->p_lock);

	osm_sa_respond(sa, p_madw, sizeof(ib_slvl_table_record_t), &rec_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}
