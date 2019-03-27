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
 *    Implementation of osm_mftr_rcv_t.
 *   This object represents the MulticastForwardingTable Receiver object.
 *   This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SA_MFT_RECORD_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_sa.h>

#define SA_MFTR_RESP_SIZE SA_ITEM_RESP_SIZE(mft_rec)

typedef struct osm_mftr_search_ctxt {
	const ib_mft_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	cl_qlist_t *p_list;
	osm_sa_t *sa;
	const osm_physp_t *p_req_physp;
} osm_mftr_search_ctxt_t;

static ib_api_status_t mftr_rcv_new_mftr(IN osm_sa_t * sa,
					 IN osm_switch_t * p_sw,
					 IN cl_qlist_t * p_list,
					 IN ib_net16_t lid, IN uint16_t block,
					 IN uint8_t position)
{
	osm_sa_item_t *p_rec_item;
	ib_api_status_t status = IB_SUCCESS;
	uint16_t position_block_num;

	OSM_LOG_ENTER(sa->p_log);

	p_rec_item = malloc(SA_MFTR_RESP_SIZE);
	if (p_rec_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4A02: "
			"rec_item alloc failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"New MulticastForwardingTable: sw 0x%016" PRIx64
		"\n\t\t\t\tblock %u position %u lid %u\n",
		cl_ntoh64(osm_node_get_node_guid(p_sw->p_node)),
		block, position, cl_ntoh16(lid));

	position_block_num = ((uint16_t) position << 12) |
	    (block & IB_MCAST_BLOCK_ID_MASK_HO);

	memset(p_rec_item, 0, SA_MFTR_RESP_SIZE);

	p_rec_item->resp.mft_rec.lid = lid;
	p_rec_item->resp.mft_rec.position_block_num = cl_hton16(position_block_num);

	/* copy the mft block */
	osm_switch_get_mft_block(p_sw, block, position, p_rec_item->resp.mft_rec.mft);

	cl_qlist_insert_tail(p_list, &p_rec_item->list_item);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return status;
}

static void mftr_rcv_by_comp_mask(IN cl_map_item_t * p_map_item, IN void *cxt)
{
	const osm_mftr_search_ctxt_t *p_ctxt = cxt;
	osm_switch_t *p_sw = (osm_switch_t *) p_map_item;
	const ib_mft_record_t *const p_rcvd_rec = p_ctxt->p_rcvd_rec;
	osm_sa_t *sa = p_ctxt->sa;
	ib_net64_t const comp_mask = p_ctxt->comp_mask;
	const osm_physp_t *const p_req_physp = p_ctxt->p_req_physp;
	osm_port_t *p_port;
	uint16_t min_lid_ho, max_lid_ho;
	uint16_t position_block_num_ho;
	uint16_t min_block, max_block, block;
	const osm_physp_t *p_physp;
	uint8_t min_position, max_position, position;

	/* In switches, the port guid is the node guid. */
	p_port =
	    osm_get_port_by_guid(sa->p_subn, p_sw->p_node->node_info.port_guid);
	if (!p_port) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4A05: "
			"Failed to find Port by Node Guid:0x%016" PRIx64
			"\n", cl_ntoh64(p_sw->p_node->node_info.node_guid));
		return;
	}

	/* check that the requester physp and the current physp are under
	   the same partition. */
	p_physp = p_port->p_physp;
	if (!p_physp) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4A06: "
			"Failed to find default physical Port by Node Guid:0x%016"
			PRIx64 "\n",
			cl_ntoh64(p_sw->p_node->node_info.node_guid));
		return;
	}
	if (!osm_physp_share_pkey(sa->p_log, p_req_physp, p_physp,
				  sa->p_subn->opt.allow_both_pkeys))
		return;

	/* get the port 0 of the switch */
	osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);

	/* compare the lids - if required */
	if (comp_mask & IB_MFTR_COMPMASK_LID) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Comparing lid:%u to port lid range: %u .. %u\n",
			cl_ntoh16(p_rcvd_rec->lid), min_lid_ho, max_lid_ho);
		/* ok we are ready for range check */
		if (min_lid_ho > cl_ntoh16(p_rcvd_rec->lid) ||
		    max_lid_ho < cl_ntoh16(p_rcvd_rec->lid))
			return;
	}

	if (!osm_switch_supports_mcast(p_sw))
		return;

	/* Are there any blocks in use ? */
	if (osm_switch_get_mft_max_block_in_use(p_sw) == -1)
		return;

	position_block_num_ho = cl_ntoh16(p_rcvd_rec->position_block_num);

	/* now we need to decide which blocks to output */
	if (comp_mask & IB_MFTR_COMPMASK_BLOCK) {
		max_block = min_block =
		    position_block_num_ho & IB_MCAST_BLOCK_ID_MASK_HO;
		if (max_block > osm_switch_get_mft_max_block_in_use(p_sw))
			return;
	} else {
		/* use as many blocks as needed */
		min_block = 0;
		max_block = osm_switch_get_mft_max_block_in_use(p_sw);
	}

	/* need to decide which positions to output */
	if (comp_mask & IB_MFTR_COMPMASK_POSITION) {
		min_position = max_position =
		    (position_block_num_ho & 0xF000) >> 12;
		if (max_position > osm_switch_get_mft_max_position(p_sw))
			return;
	} else {
		/* use as many positions as needed */
		min_position = 0;
		max_position = osm_switch_get_mft_max_position(p_sw);
	}

	/* so we can add these one by one ... */
	for (block = min_block; block <= max_block; block++)
		for (position = min_position; position <= max_position;
		     position++)
			mftr_rcv_new_mftr(sa, p_sw, p_ctxt->p_list,
					  osm_port_get_base_lid(p_port), block,
					  position);
}

void osm_mftr_rcv_process(IN void *ctx, IN void *data)
{
	osm_sa_t *sa = ctx;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *p_rcvd_mad;
	const ib_mft_record_t *p_rcvd_rec;
	cl_qlist_t rec_list;
	osm_mftr_search_ctxt_t context;
	osm_physp_t *p_req_physp;

	CL_ASSERT(sa);

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec = (ib_mft_record_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);

	CL_ASSERT(p_rcvd_mad->attr_id == IB_MAD_ATTR_MFT_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if (p_rcvd_mad->method != IB_MAD_METHOD_GET &&
	    p_rcvd_mad->method != IB_MAD_METHOD_GETTABLE) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4A08: "
			"Unsupported Method (%s) for MFTRecord request\n",
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
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4A07: "
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
	context.p_req_physp = p_req_physp;

	/* Go over all switches */
	cl_qmap_apply_func(&sa->p_subn->sw_guid_tbl, mftr_rcv_by_comp_mask,
			   &context);

	cl_plock_release(sa->p_lock);

	osm_sa_respond(sa, p_madw, sizeof(ib_mft_record_t), &rec_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}
