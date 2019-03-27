/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2006 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 *    Implementation of osm_infr_rcv_t.
 * This object represents the InformInfo Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SA_INFORMINFO_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_sa.h>
#include <opensm/osm_inform.h>
#include <opensm/osm_pkey.h>

#define SA_IIR_RESP_SIZE SA_ITEM_RESP_SIZE(inform_rec)
#define SA_II_RESP_SIZE SA_ITEM_RESP_SIZE(inform)

typedef struct osm_iir_search_ctxt {
	const ib_inform_info_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	cl_qlist_t *p_list;
	ib_gid_t subscriber_gid;
	ib_net16_t subscriber_enum;
	osm_sa_t *sa;
	osm_physp_t *p_req_physp;
	ib_net64_t sm_key;
} osm_iir_search_ctxt_t;

/**********************************************************************
o13-14.1.1: Except for Set(InformInfo) requests with Inform-
Info:LIDRangeBegin=0xFFFF, managers that support event forwarding
shall, upon receiving a Set(InformInfo), verify that the requester
originating the Set(InformInfo) and a Trap() source identified by Inform-
can access each other - can use path record to verify that.
**********************************************************************/
static boolean_t validate_ports_access_rights(IN osm_sa_t * sa,
					      IN osm_infr_t * p_infr_rec)
{
	boolean_t valid = TRUE;
	osm_physp_t *p_requester_physp;
	osm_port_t *p_port;
	ib_net64_t portguid;
	uint16_t lid_range_begin, lid_range_end, lid;

	OSM_LOG_ENTER(sa->p_log);

	/* get the requester physp from the request address */
	p_requester_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
						      &p_infr_rec->report_addr);

	if (ib_gid_is_notzero(&p_infr_rec->inform_record.inform_info.gid)) {
		/* a gid is defined */
		portguid =
		    p_infr_rec->inform_record.inform_info.gid.unicast.
		    interface_id;

		p_port = osm_get_port_by_guid(sa->p_subn, portguid);
		if (p_port == NULL) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4301: "
				"Invalid port guid: 0x%016" PRIx64 "\n",
				cl_ntoh64(portguid));
			valid = FALSE;
			goto Exit;
		}

		/* make sure that the requester and destination port can access
		   each other according to the current partitioning. */
		if (!osm_physp_share_pkey
		    (sa->p_log, p_port->p_physp, p_requester_physp,
		     sa->p_subn->opt.allow_both_pkeys)) {
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"port and requester don't share pkey\n");
			valid = FALSE;
			goto Exit;
		}
	} else {
		size_t lids_size;

		/* gid is zero - check if LID range is defined */
		lid_range_begin =
		    cl_ntoh16(p_infr_rec->inform_record.inform_info.
			      lid_range_begin);
		/* if lid is 0xFFFF - meaning all endports managed by the manager */
		if (lid_range_begin == 0xFFFF)
			goto Exit;

		lid_range_end =
		    cl_ntoh16(p_infr_rec->inform_record.inform_info.
			      lid_range_end);

		lids_size = cl_ptr_vector_get_size(&sa->p_subn->port_lid_tbl);

		/* lid_range_end is set to zero if no range desired. In this
		   case - just make it equal to the lid_range_begin. */
		if (lid_range_end == 0)
			lid_range_end = lid_range_begin;
		else if (lid_range_end >= lids_size)
			lid_range_end = lids_size - 1;

		if (lid_range_begin >= lids_size) {
			/* requested lids are out of range */
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4302: "
				"Given LIDs (%u-%u) are out of range (%zu)\n",
				lid_range_begin, lid_range_end, lids_size);
			valid = FALSE;
			goto Exit;
		}

		/* go over all defined lids within the range and make sure that the
		   requester port can access them according to current partitioning. */
		for (lid = lid_range_begin; lid <= lid_range_end; lid++) {
			p_port = osm_get_port_by_lid_ho(sa->p_subn, lid);
			if (p_port == NULL)
				continue;

			/* make sure that the requester and destination port can access
			   each other according to the current partitioning. */
			if (!osm_physp_share_pkey
			    (sa->p_log, p_port->p_physp, p_requester_physp,
			     sa->p_subn->opt.allow_both_pkeys)) {
				OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
					"port and requester don't share pkey\n");
				valid = FALSE;
				goto Exit;
			}
		}
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return valid;
}

static boolean_t validate_infr(IN osm_sa_t * sa, IN osm_infr_t * p_infr_rec)
{
	boolean_t valid = TRUE;

	OSM_LOG_ENTER(sa->p_log);

	valid = validate_ports_access_rights(sa, p_infr_rec);
	if (!valid) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Invalid Access for InformInfo\n");
		valid = FALSE;
	}

	OSM_LOG_EXIT(sa->p_log);
	return valid;
}

/**********************************************************************
o13-12.1.1: Confirm a valid request for event subscription by responding
with an InformInfo attribute that is a copy of the data in the
Set(InformInfo) request.
**********************************************************************/
static void infr_rcv_respond(IN osm_sa_t * sa, IN osm_madw_t * p_madw)
{
	cl_qlist_t rec_list;
	osm_sa_item_t *item;

	OSM_LOG_ENTER(sa->p_log);

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Generating successful InformInfo response\n");

	item = malloc(SA_II_RESP_SIZE);
	if (!item) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4303: "
			"rec_item alloc failed\n");
		goto Exit;
	}

	memcpy(&item->resp.inform,
	       ib_sa_mad_get_payload_ptr(osm_madw_get_sa_mad_ptr(p_madw)),
	       sizeof(ib_inform_info_t));

	cl_qlist_init(&rec_list);
	cl_qlist_insert_tail(&rec_list, &item->list_item);

	osm_sa_respond(sa, p_madw, sizeof(ib_inform_info_t), &rec_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static void sa_inform_info_rec_by_comp_mask(IN osm_sa_t * sa,
					    IN const osm_infr_t * p_infr,
					    osm_iir_search_ctxt_t * p_ctxt)
{
	ib_net64_t comp_mask;
	ib_net64_t portguid;
	osm_port_t *p_subscriber_port;
	osm_physp_t *p_subscriber_physp;
	const osm_physp_t *p_req_physp;
	osm_sa_item_t *p_rec_item;

	OSM_LOG_ENTER(sa->p_log);

	comp_mask = p_ctxt->comp_mask;
	p_req_physp = p_ctxt->p_req_physp;

	if (comp_mask & IB_IIR_COMPMASK_SUBSCRIBERGID &&
	    memcmp(&p_infr->inform_record.subscriber_gid,
		   &p_ctxt->subscriber_gid,
		   sizeof(p_infr->inform_record.subscriber_gid)))
		goto Exit;

	if (comp_mask & IB_IIR_COMPMASK_ENUM &&
	    p_infr->inform_record.subscriber_enum != p_ctxt->subscriber_enum)
		goto Exit;

	/* Implement any other needed search cases */

	/* Ensure pkey is shared before returning any records */
	portguid = p_infr->inform_record.subscriber_gid.unicast.interface_id;
	p_subscriber_port = osm_get_port_by_guid(sa->p_subn, portguid);
	if (p_subscriber_port == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 430D: "
			"Invalid subscriber port guid: 0x%016" PRIx64 "\n",
			cl_ntoh64(portguid));
		goto Exit;
	}

	/* get the subscriber InformInfo physical port */
	p_subscriber_physp = p_subscriber_port->p_physp;
	/* make sure that the requester and subscriber port can access each
	   other according to the current partitioning. */
	if (!osm_physp_share_pkey(sa->p_log, p_req_physp, p_subscriber_physp,
				  sa->p_subn->opt.allow_both_pkeys)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"requester and subscriber ports don't share pkey\n");
		goto Exit;
	}

	p_rec_item = malloc(SA_IIR_RESP_SIZE);
	if (p_rec_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 430E: "
			"rec_item alloc failed\n");
		goto Exit;
	}

	memcpy(&p_rec_item->resp.inform_rec, &p_infr->inform_record,
	       sizeof(ib_inform_info_record_t));

	/*
	 * Per C15-0.2-1.16, InformInfoRecords shall always be
	 * provided with the QPN set to 0, except for the case
	 * of a trusted request, in which case the actual
	 * subscriber QPN shall be returned.
	 */
	if (p_ctxt->sm_key == 0)
		ib_inform_info_set_qpn(&p_rec_item->resp.inform_rec.inform_info, 0);

	cl_qlist_insert_tail(p_ctxt->p_list, &p_rec_item->list_item);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static void sa_inform_info_rec_by_comp_mask_cb(IN cl_list_item_t * p_list_item,
					       IN void *context)
{
	const osm_infr_t *p_infr = (osm_infr_t *) p_list_item;
	osm_iir_search_ctxt_t *p_ctxt = context;

	sa_inform_info_rec_by_comp_mask(p_ctxt->sa, p_infr, p_ctxt);
}

/**********************************************************************
Received a Get(InformInfoRecord) or GetTable(InformInfoRecord) MAD
**********************************************************************/
static void infr_rcv_process_get_method(osm_sa_t * sa, IN osm_madw_t * p_madw)
{
	char gid_str[INET6_ADDRSTRLEN];
	ib_sa_mad_t *p_rcvd_mad;
	const ib_inform_info_record_t *p_rcvd_rec;
	cl_qlist_t rec_list;
	osm_iir_search_ctxt_t context;
	osm_physp_t *p_req_physp;
	osm_sa_item_t *item;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);
	p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec =
	    (ib_inform_info_record_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);

	cl_plock_acquire(sa->p_lock);

	/* update the requester physical port */
	p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		cl_plock_release(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4309: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requester port GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));
		osm_dump_inform_info_record_v2(sa->p_log, p_rcvd_rec,
					       FILE_ID, OSM_LOG_DEBUG);
	}

	cl_qlist_init(&rec_list);

	context.p_rcvd_rec = p_rcvd_rec;
	context.p_list = &rec_list;
	context.comp_mask = p_rcvd_mad->comp_mask;
	context.subscriber_gid = p_rcvd_rec->subscriber_gid;
	context.subscriber_enum = p_rcvd_rec->subscriber_enum;
	context.sa = sa;
	context.p_req_physp = p_req_physp;
	context.sm_key = p_rcvd_mad->sm_key;

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Query Subscriber GID:%s(%02X) Enum:0x%X(%02X)\n",
		inet_ntop(AF_INET6, p_rcvd_rec->subscriber_gid.raw,
			  gid_str, sizeof gid_str),
		(p_rcvd_mad->comp_mask & IB_IIR_COMPMASK_SUBSCRIBERGID) != 0,
		cl_ntoh16(p_rcvd_rec->subscriber_enum),
		(p_rcvd_mad->comp_mask & IB_IIR_COMPMASK_ENUM) != 0);

	cl_qlist_apply_func(&sa->p_subn->sa_infr_list,
			    sa_inform_info_rec_by_comp_mask_cb, &context);

	/* clear reserved and pad fields in InformInfoRecord */
	for (item = (osm_sa_item_t *) cl_qlist_head(&rec_list);
	     item != (osm_sa_item_t *) cl_qlist_end(&rec_list);
	     item = (osm_sa_item_t *) cl_qlist_next(&item->list_item)) {
		memset(item->resp.inform_rec.reserved, 0, sizeof(item->resp.inform_rec.reserved));
		memset(item->resp.inform_rec.pad, 0, sizeof(item->resp.inform_rec.pad));
	}

	cl_plock_release(sa->p_lock);

	osm_sa_respond(sa, p_madw, sizeof(ib_inform_info_record_t), &rec_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/*********************************************************************
Received a Set(InformInfo) MAD
**********************************************************************/
static void infr_rcv_process_set_method(osm_sa_t * sa, IN osm_madw_t * p_madw)
{
	ib_sa_mad_t *p_sa_mad;
	ib_inform_info_t *p_recvd_inform_info;
	osm_infr_t inform_info_rec;	/* actual inform record to be stored for reports */
	osm_infr_t *p_infr;
	ib_net32_t qpn;
	uint8_t resp_time_val;
	ib_api_status_t res;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_inform_info =
	    (ib_inform_info_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

#if 0
	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_inform_info_v2(sa->p_log, p_recvd_inform_info,
				        FILE_ID, OSM_LOG_DEBUG);
#endif

	/* Grab the lock */
	cl_plock_excl_acquire(sa->p_lock);

	/* define the inform record */
	inform_info_rec.inform_record.inform_info = *p_recvd_inform_info;

	/* following C13-32.1.2 Tbl 120: we only copy the source address vector */
	inform_info_rec.report_addr = p_madw->mad_addr;

	/* we will need to know the mad srvc to send back through */
	inform_info_rec.h_bind = p_madw->h_bind;
	inform_info_rec.sa = sa;

	/* update the subscriber GID according to mad address */
	res = osm_get_gid_by_mad_addr(sa->p_log, sa->p_subn, &p_madw->mad_addr,
				      &inform_info_rec.inform_record.
				      subscriber_gid);
	if (res != IB_SUCCESS) {
		cl_plock_release(sa->p_lock);

		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4308: "
			"Subscribe Request from unknown LID: %u\n",
			cl_ntoh16(p_madw->mad_addr.dest_lid));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/* HACK: enum is always 0 (currently) */
	inform_info_rec.inform_record.subscriber_enum = 0;

	/* Subscribe values above 1 are undefined */
	if (p_recvd_inform_info->subscribe > 1) {
		cl_plock_release(sa->p_lock);

		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 430A: "
			"Invalid subscribe: %d\n",
			p_recvd_inform_info->subscribe);
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/*
	 * Per C15-0.2-1.16, SubnAdmSet(InformInfo) subscriptions for
	 * SM security traps shall be provided only if they come from a
	 * trusted source.
	 */
	if ((p_sa_mad->sm_key == 0) && p_recvd_inform_info->is_generic &&
	    ((cl_ntoh16(p_recvd_inform_info->g_or_v.generic.trap_num) >= SM_BAD_MKEY_TRAP) &&
	     (cl_ntoh16(p_recvd_inform_info->g_or_v.generic.trap_num) <= SM_BAD_SWITCH_PKEY_TRAP))) {
		cl_plock_release(sa->p_lock);

		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 430B "
			"Request for security trap from non-trusted requester: "
			"Given SM_Key:0x%016" PRIx64 "\n",
			cl_ntoh64(p_sa_mad->sm_key));
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/*
	 * MODIFICATIONS DONE ON INCOMING REQUEST:
	 *
	 * QPN:
	 * Internally we keep the QPN field of the InformInfo updated
	 * so we can simply compare it in the record - when finding such.
	 */
	if (p_recvd_inform_info->subscribe) {
		ib_inform_info_set_qpn(&inform_info_rec.inform_record.
				       inform_info,
				       inform_info_rec.report_addr.addr_type.
				       gsi.remote_qp);

		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Subscribe Request with QPN: 0x%06X\n",
			cl_ntoh32(inform_info_rec.report_addr.addr_type.gsi.
				  remote_qp));
	} else {
		ib_inform_info_get_qpn_resp_time(p_recvd_inform_info->g_or_v.
						 generic.qpn_resp_time_val,
						 &qpn, &resp_time_val);

		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"UnSubscribe Request with QPN: 0x%06X\n",
			cl_ntoh32(qpn));
	}

	/* If record exists with matching InformInfo */
	p_infr = osm_infr_get_by_rec(sa->p_subn, sa->p_log, &inform_info_rec);

	/* check to see if the request was for subscribe */
	if (p_recvd_inform_info->subscribe) {
		/* validate the request for a new or update InformInfo */
		if (validate_infr(sa, &inform_info_rec) != TRUE) {
			cl_plock_release(sa->p_lock);

			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4305: "
				"Failed to validate a new inform object\n");

			/* o13-13.1.1: we need to set the subscribe bit to 0 */
			p_recvd_inform_info->subscribe = 0;
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}

		/* ok - we can try and create a new entry */
		if (p_infr == NULL) {
			/* Create the instance of the osm_infr_t object */
			p_infr = osm_infr_new(&inform_info_rec);
			if (p_infr == NULL) {
				cl_plock_release(sa->p_lock);

				OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4306: "
					"Failed to create a new inform object\n");

				/* o13-13.1.1: we need to set the subscribe bit to 0 */
				p_recvd_inform_info->subscribe = 0;
				osm_sa_send_error(sa, p_madw,
						  IB_SA_MAD_STATUS_NO_RESOURCES);
				goto Exit;
			}

			OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
				"Adding event subscription for port 0x%" PRIx64 "\n",
				cl_ntoh64(inform_info_rec.inform_record.subscriber_gid.unicast.interface_id));

			/* Add this new osm_infr_t object to subnet object */
			osm_infr_insert_to_db(sa->p_subn, sa->p_log, p_infr);
		} else
			/* Update the old instance of the osm_infr_t object */
			p_infr->inform_record = inform_info_rec.inform_record;
		/* We got an UnSubscribe request */
	} else if (p_infr == NULL) {
		cl_plock_release(sa->p_lock);

		/* No Such Item - So Error */
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 4307: "
			"Failed to UnSubscribe to non existing inform object\n");

		/* o13-13.1.1: we need to set the subscribe bit to 0 */
		p_recvd_inform_info->subscribe = 0;
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	} else {
		/* Delete this object from the subnet list of informs */
		OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
			"Removing event subscription for port 0x%" PRIx64 "\n",
			cl_ntoh64(inform_info_rec.inform_record.subscriber_gid.unicast.interface_id));
		osm_infr_remove_from_db(sa->p_subn, sa->p_log, p_infr);
	}

	cl_plock_release(sa->p_lock);

	/* send the success response */
	infr_rcv_respond(sa, p_madw);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

void osm_infr_rcv_process(IN void *context, IN void *data)
{
	osm_sa_t *sa = context;
	osm_madw_t *p_madw = data;
	ib_sa_mad_t *p_sa_mad;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_INFORM_INFO);

	if (p_sa_mad->method != IB_MAD_METHOD_SET) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Unsupported Method (%s) for InformInfo\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		goto Exit;
	}

	infr_rcv_process_set_method(sa, p_madw);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

void osm_infir_rcv_process(IN void *context, IN void *data)
{
	osm_sa_t *sa = context;
	osm_madw_t *p_madw = data;
	ib_sa_mad_t *p_sa_mad;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_INFORM_INFO_RECORD);

	if (p_sa_mad->method != IB_MAD_METHOD_GET &&
	    p_sa_mad->method != IB_MAD_METHOD_GETTABLE) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Unsupported Method (%s) for InformInfoRecord\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		goto Exit;
	}

	infr_rcv_process_get_method(sa, p_madw);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}
