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
 *    Implementation of osm_vlarb_rec_rcv_t.
 * This object represents the VLArbitrationRecord Receiver object.
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
#define FILE_ID OSM_FILE_SA_VLARB_RECORD_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_sa.h>

#define SA_VLA_RESP_SIZE SA_ITEM_RESP_SIZE(vlarb_rec)

typedef struct osm_vl_arb_search_ctxt {
	const ib_vl_arb_table_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	uint8_t block_num;
	cl_qlist_t *p_list;
	osm_sa_t *sa;
	const osm_physp_t *p_req_physp;
} osm_vl_arb_search_ctxt_t;

static void sa_vl_arb_create(IN osm_sa_t * sa, IN osm_physp_t * p_physp,
			     IN osm_vl_arb_search_ctxt_t * p_ctxt,
			     IN uint8_t block)
{
	osm_sa_item_t *p_rec_item;
	uint16_t lid;

	OSM_LOG_ENTER(sa->p_log);

	p_rec_item = malloc(SA_VLA_RESP_SIZE);
	if (p_rec_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2A02: "
			"rec_item alloc failed\n");
		goto Exit;
	}

	if (p_physp->p_node->node_info.node_type != IB_NODE_TYPE_SWITCH)
		lid = p_physp->port_info.base_lid;
	else
		lid = osm_node_get_base_lid(p_physp->p_node, 0);

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"New VLArbitration for: port 0x%016" PRIx64
		", lid %u, port %u Block:%u\n",
		cl_ntoh64(osm_physp_get_port_guid(p_physp)),
		cl_ntoh16(lid), osm_physp_get_port_num(p_physp), block);

	memset(p_rec_item, 0, SA_VLA_RESP_SIZE);

	p_rec_item->resp.vlarb_rec.lid = lid;
	p_rec_item->resp.vlarb_rec.port_num = osm_physp_get_port_num(p_physp);
	p_rec_item->resp.vlarb_rec.block_num = block;
	p_rec_item->resp.vlarb_rec.vl_arb_tbl = *(osm_physp_get_vla_tbl(p_physp, block));

	cl_qlist_insert_tail(p_ctxt->p_list, &p_rec_item->list_item);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static void sa_vl_arb_check_physp(IN osm_sa_t * sa, IN osm_physp_t * p_physp,
				  osm_vl_arb_search_ctxt_t * p_ctxt)
{
	ib_net64_t comp_mask = p_ctxt->comp_mask;
	uint8_t block;

	OSM_LOG_ENTER(sa->p_log);

	/* we got here with the phys port - all that's left is to get the right block */
	for (block = 1; block <= 4; block++) {
		if (!(comp_mask & IB_VLA_COMPMASK_BLOCK)
		    || block == p_ctxt->block_num)
			sa_vl_arb_create(sa, p_physp, p_ctxt, block);
	}

	OSM_LOG_EXIT(sa->p_log);
}

static void sa_vl_arb_by_comp_mask(osm_sa_t * sa, IN const osm_port_t * p_port,
				   osm_vl_arb_search_ctxt_t * p_ctxt)
{
	const ib_vl_arb_table_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	osm_physp_t *p_physp;
	uint8_t port_num;
	uint8_t num_ports;
	const osm_physp_t *p_req_physp;

	OSM_LOG_ENTER(sa->p_log);

	p_rcvd_rec = p_ctxt->p_rcvd_rec;
	comp_mask = p_ctxt->comp_mask;
	port_num = p_rcvd_rec->port_num;
	p_req_physp = p_ctxt->p_req_physp;

	/* if this is a switch port we can search all ports
	   otherwise we must be looking on port 0 */
	if (p_port->p_node->node_info.node_type != IB_NODE_TYPE_SWITCH) {
		/* we put it in the comp mask and port num */
		port_num = p_port->p_physp->port_num;
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Using Physical Default Port Number: 0x%X (for End Node)\n",
			port_num);
		comp_mask |= IB_VLA_COMPMASK_OUT_PORT;
	}

	if (comp_mask & IB_VLA_COMPMASK_OUT_PORT) {
		if (port_num < osm_node_get_num_physp(p_port->p_node)) {
			p_physp =
			    osm_node_get_physp_ptr(p_port->p_node, port_num);
			/* check that the p_physp is valid, and that the requester
			   and the p_physp share a pkey. */
			if (p_physp &&
			    osm_physp_share_pkey(sa->p_log, p_req_physp, p_physp,
						 sa->p_subn->opt.allow_both_pkeys))
				sa_vl_arb_check_physp(sa, p_physp, p_ctxt);
		} else {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2A03: "
				"Given Physical Port Number: 0x%X is out of range should be < 0x%X\n",
				port_num,
				osm_node_get_num_physp(p_port->p_node));
			goto Exit;
		}
	} else {
		num_ports = osm_node_get_num_physp(p_port->p_node);
		for (port_num = 0; port_num < num_ports; port_num++) {
			p_physp =
			    osm_node_get_physp_ptr(p_port->p_node, port_num);
			if (!p_physp)
				continue;

			/* if the requester and the p_physp don't share a pkey -
			   continue */
			if (!osm_physp_share_pkey(sa->p_log, p_req_physp, p_physp,
						  sa->p_subn->opt.allow_both_pkeys))
				continue;

			sa_vl_arb_check_physp(sa, p_physp, p_ctxt);
		}
	}
Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static void sa_vl_arb_by_comp_mask_cb(IN cl_map_item_t * p_map_item, void *cxt)
{
	const osm_port_t *p_port = (osm_port_t *) p_map_item;
	osm_vl_arb_search_ctxt_t *p_ctxt = cxt;

	sa_vl_arb_by_comp_mask(p_ctxt->sa, p_port, p_ctxt);
}

void osm_vlarb_rec_rcv_process(IN void *ctx, IN void *data)
{
	osm_sa_t *sa = ctx;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *sad_mad;
	const ib_vl_arb_table_record_t *p_rcvd_rec;
	const osm_port_t *p_port = NULL;
	cl_qlist_t rec_list;
	osm_vl_arb_search_ctxt_t context;
	ib_api_status_t status = IB_SUCCESS;
	ib_net64_t comp_mask;
	osm_physp_t *p_req_physp;

	CL_ASSERT(sa);

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	sad_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec =
	    (ib_vl_arb_table_record_t *) ib_sa_mad_get_payload_ptr(sad_mad);
	comp_mask = sad_mad->comp_mask;

	CL_ASSERT(sad_mad->attr_id == IB_MAD_ATTR_VLARB_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if (sad_mad->method != IB_MAD_METHOD_GET &&
	    sad_mad->method != IB_MAD_METHOD_GETTABLE) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2A05: "
			"Unsupported Method (%s) for a VLArbRecord request\n",
			ib_get_sa_method_str(sad_mad->method));
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
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2A04: "
			"Cannot find requester physical port\n");
		goto Exit;
	}
	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Requester port GUID 0x%" PRIx64 "\n",
		cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));

	cl_qlist_init(&rec_list);

	context.p_rcvd_rec = p_rcvd_rec;
	context.p_list = &rec_list;
	context.comp_mask = sad_mad->comp_mask;
	context.sa = sa;
	context.block_num = p_rcvd_rec->block_num;
	context.p_req_physp = p_req_physp;

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Got Query Lid:%u(%02X), Port:0x%02X(%02X), Block:0x%02X(%02X)\n",
		cl_ntoh16(p_rcvd_rec->lid),
		(comp_mask & IB_VLA_COMPMASK_LID) != 0, p_rcvd_rec->port_num,
		(comp_mask & IB_VLA_COMPMASK_OUT_PORT) != 0,
		p_rcvd_rec->block_num,
		(comp_mask & IB_VLA_COMPMASK_BLOCK) != 0);

	/*
	   If the user specified a LID, it obviously narrows our
	   work load, since we don't have to search every port
	 */
	if (comp_mask & IB_VLA_COMPMASK_LID) {
		p_port = osm_get_port_by_lid(sa->p_subn, p_rcvd_rec->lid);
		if (!p_port) {
			status = IB_NOT_FOUND;
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2A09: "
				"No port found with LID %u\n",
				cl_ntoh16(p_rcvd_rec->lid));
		}
	}

	if (status == IB_SUCCESS) {
		/* if we got a unique port - no need for a port search */
		if (p_port)
			/*  this does the loop on all the port phys ports */
			sa_vl_arb_by_comp_mask(sa, p_port, &context);
		else
			cl_qmap_apply_func(&sa->p_subn->port_guid_tbl,
					   sa_vl_arb_by_comp_mask_cb, &context);
	}

	cl_plock_release(sa->p_lock);

	osm_sa_respond(sa, p_madw, sizeof(ib_vl_arb_table_record_t), &rec_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}
