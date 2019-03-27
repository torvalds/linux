/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
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
 *    Implementation of osm_sr_rcv_t.
 * This object represents the ServiceRecord Receiver object.
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
#define FILE_ID OSM_FILE_SA_SERVICE_RECORD_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_sa.h>
#include <opensm/osm_service.h>
#include <opensm/osm_pkey.h>

#define SA_SR_RESP_SIZE SA_ITEM_RESP_SIZE(service_rec)

typedef struct osm_sr_match_item {
	cl_qlist_t sr_list;
	ib_service_record_t *p_service_rec;
	ib_net64_t comp_mask;
	osm_sa_t *sa;
} osm_sr_match_item_t;

typedef struct osm_sr_search_ctxt {
	osm_sr_match_item_t *p_sr_item;
	const osm_physp_t *p_req_physp;
} osm_sr_search_ctxt_t;

static boolean_t
match_service_pkey_with_ports_pkey(IN osm_sa_t * sa,
				   IN const osm_madw_t * p_madw,
				   ib_service_record_t * p_service_rec,
				   ib_net64_t const comp_mask)
{
	boolean_t valid = TRUE;
	osm_physp_t *p_req_physp;
	ib_net64_t service_guid;
	osm_port_t *service_port;

	/* update the requester physical port */
	p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2404: "
			"Cannot find requester physical port\n");
		valid = FALSE;
		goto Exit;
	}
	OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
		"Requester port GUID 0x%" PRIx64 "\n",
		cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));

	if ((comp_mask & IB_SR_COMPMASK_SPKEY) == IB_SR_COMPMASK_SPKEY) {
		/* We have a ServiceP_Key - check matching on requester port,
		   and ServiceGid port (if such exists) */
		/* Make sure it matches the p_req_physp */
		if (!osm_physp_has_pkey
		    (sa->p_log, p_service_rec->service_pkey, p_req_physp)) {
			valid = FALSE;
			goto Exit;
		}

		/* If unicast, make sure it matches the port of the ServiceGid */
		if (comp_mask & IB_SR_COMPMASK_SGID &&
		    !ib_gid_is_multicast(&p_service_rec->service_gid)) {
			service_guid =
			    p_service_rec->service_gid.unicast.interface_id;
			service_port =
			    osm_get_port_by_alias_guid(sa->p_subn, service_guid);
			if (!service_port) {
				OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2405: "
					"No port object for port 0x%016" PRIx64
					"\n", cl_ntoh64(service_guid));
				valid = FALSE;
				goto Exit;
			}
			/* check on the table of the default physical port of the service port */
			if (!osm_physp_has_pkey(sa->p_log,
						p_service_rec->service_pkey,
						service_port->p_physp)) {
				valid = FALSE;
				goto Exit;
			}
		}
	}

Exit:
	return valid;
}

static boolean_t
match_name_to_key_association(IN osm_sa_t * sa,
			      ib_service_record_t * p_service_rec,
			      ib_net64_t comp_mask)
{
	UNUSED_PARAM(p_service_rec);
	UNUSED_PARAM(sa);

	if ((comp_mask & (IB_SR_COMPMASK_SKEY | IB_SR_COMPMASK_SNAME)) ==
	    (IB_SR_COMPMASK_SKEY | IB_SR_COMPMASK_SNAME)) {
		/* For now, we are not maintaining the ServiceAssociation record
		 * so just return TRUE
		 */
		return TRUE;
	}

	return TRUE;
}

static boolean_t validate_sr(IN osm_sa_t * sa, IN const osm_madw_t * p_madw)
{
	boolean_t valid = TRUE;
	ib_sa_mad_t *p_sa_mad;
	ib_service_record_t *p_recvd_service_rec;

	OSM_LOG_ENTER(sa->p_log);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_service_rec =
	    (ib_service_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	valid = match_service_pkey_with_ports_pkey(sa, p_madw,
						   p_recvd_service_rec,
						   p_sa_mad->comp_mask);
	if (!valid) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"No Match for Service Pkey\n");
		valid = FALSE;
		goto Exit;
	}

	valid = match_name_to_key_association(sa, p_recvd_service_rec,
					      p_sa_mad->comp_mask);
	if (!valid) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Service Record Name to key matching failed\n");
		valid = FALSE;
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return valid;
}

static void sr_rcv_respond(IN osm_sa_t * sa, IN osm_madw_t * p_madw,
			   IN cl_qlist_t * p_list)
{
	/* p923 - The ServiceKey shall be set to 0, except in the case of
	   a trusted request.
	   Note: In the mad controller we check that the SM_Key received on
	   the mad is valid. Meaning - is either zero or equal to the local
	   sm_key.
	 */
	if (!osm_madw_get_sa_mad_ptr(p_madw)->sm_key) {
		osm_sa_item_t *item;
		for (item = (osm_sa_item_t *) cl_qlist_head(p_list);
		     item != (osm_sa_item_t *) cl_qlist_end(p_list);
		     item = (osm_sa_item_t *) cl_qlist_next(&item->list_item))
			memset(item->resp.service_rec.service_key, 0,
			       sizeof(item->resp.service_rec.service_key));
	}

	osm_sa_respond(sa, p_madw, sizeof(ib_service_record_t), p_list);
}

static void get_matching_sr(IN cl_list_item_t * p_list_item, IN void *context)
{
	osm_sr_search_ctxt_t *p_ctxt = context;
	osm_svcr_t *p_svcr = (osm_svcr_t *) p_list_item;
	osm_sa_item_t *p_sr_pool_item;
	osm_sr_match_item_t *p_sr_item = p_ctxt->p_sr_item;
	ib_net64_t comp_mask = p_sr_item->comp_mask;
	const osm_physp_t *p_req_physp = p_ctxt->p_req_physp;

	if (comp_mask & IB_SR_COMPMASK_SID) {
		if (p_sr_item->p_service_rec->service_id !=
		    p_svcr->service_record.service_id)
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SGID) {
		if (memcmp(&p_sr_item->p_service_rec->service_gid,
			   &p_svcr->service_record.service_gid,
			   sizeof(p_svcr->service_record.service_gid)) != 0)
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SPKEY) {
		if (p_sr_item->p_service_rec->service_pkey !=
		    p_svcr->service_record.service_pkey)
			return;
	}

	if (comp_mask & IB_SR_COMPMASK_SKEY) {
		if (memcmp(p_sr_item->p_service_rec->service_key,
			   p_svcr->service_record.service_key,
			   16 * sizeof(uint8_t)))
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SNAME) {
		if (memcmp(p_sr_item->p_service_rec->service_name,
			   p_svcr->service_record.service_name,
			   sizeof(p_svcr->service_record.service_name)) != 0)
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA8_0) {
		if (p_sr_item->p_service_rec->service_data8[0] !=
		    p_svcr->service_record.service_data8[0])
			return;
	}

	if (comp_mask & IB_SR_COMPMASK_SDATA8_1) {
		if (p_sr_item->p_service_rec->service_data8[1] !=
		    p_svcr->service_record.service_data8[1])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA8_2) {
		if (p_sr_item->p_service_rec->service_data8[2] !=
		    p_svcr->service_record.service_data8[2])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA8_3) {
		if (p_sr_item->p_service_rec->service_data8[3] !=
		    p_svcr->service_record.service_data8[3])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA8_4) {
		if (p_sr_item->p_service_rec->service_data8[4] !=
		    p_svcr->service_record.service_data8[4])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA8_5) {
		if (p_sr_item->p_service_rec->service_data8[5] !=
		    p_svcr->service_record.service_data8[5])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA8_6) {
		if (p_sr_item->p_service_rec->service_data8[6] !=
		    p_svcr->service_record.service_data8[6])
			return;
	}

	if (comp_mask & IB_SR_COMPMASK_SDATA8_7) {
		if (p_sr_item->p_service_rec->service_data8[7] !=
		    p_svcr->service_record.service_data8[7])
			return;
	}

	if (comp_mask & IB_SR_COMPMASK_SDATA8_8) {
		if (p_sr_item->p_service_rec->service_data8[8] !=
		    p_svcr->service_record.service_data8[8])
			return;
	}

	if (comp_mask & IB_SR_COMPMASK_SDATA8_9) {
		if (p_sr_item->p_service_rec->service_data8[9] !=
		    p_svcr->service_record.service_data8[9])
			return;
	}

	if (comp_mask & IB_SR_COMPMASK_SDATA8_10) {
		if (p_sr_item->p_service_rec->service_data8[10] !=
		    p_svcr->service_record.service_data8[10])
			return;
	}

	if (comp_mask & IB_SR_COMPMASK_SDATA8_11) {
		if (p_sr_item->p_service_rec->service_data8[11] !=
		    p_svcr->service_record.service_data8[11])
			return;
	}

	if (comp_mask & IB_SR_COMPMASK_SDATA8_12) {
		if (p_sr_item->p_service_rec->service_data8[12] !=
		    p_svcr->service_record.service_data8[12])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA8_13) {
		if (p_sr_item->p_service_rec->service_data8[13] !=
		    p_svcr->service_record.service_data8[13])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA8_14) {
		if (p_sr_item->p_service_rec->service_data8[14] !=
		    p_svcr->service_record.service_data8[14])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA8_15) {
		if (p_sr_item->p_service_rec->service_data8[15] !=
		    p_svcr->service_record.service_data8[15])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA16_0) {
		if (p_sr_item->p_service_rec->service_data16[0] !=
		    p_svcr->service_record.service_data16[0])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA16_1) {
		if (p_sr_item->p_service_rec->service_data16[1] !=
		    p_svcr->service_record.service_data16[1])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA16_2) {
		if (p_sr_item->p_service_rec->service_data16[2] !=
		    p_svcr->service_record.service_data16[2])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA16_3) {
		if (p_sr_item->p_service_rec->service_data16[3] !=
		    p_svcr->service_record.service_data16[3])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA16_4) {
		if (p_sr_item->p_service_rec->service_data16[4] !=
		    p_svcr->service_record.service_data16[4])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA16_5) {
		if (p_sr_item->p_service_rec->service_data16[5] !=
		    p_svcr->service_record.service_data16[5])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA16_6) {
		if (p_sr_item->p_service_rec->service_data16[6] !=
		    p_svcr->service_record.service_data16[6])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA16_7) {
		if (p_sr_item->p_service_rec->service_data16[7] !=
		    p_svcr->service_record.service_data16[7])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA32_0) {
		if (p_sr_item->p_service_rec->service_data32[0] !=
		    p_svcr->service_record.service_data32[0])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA32_1) {
		if (p_sr_item->p_service_rec->service_data32[1] !=
		    p_svcr->service_record.service_data32[1])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA32_2) {
		if (p_sr_item->p_service_rec->service_data32[2] !=
		    p_svcr->service_record.service_data32[2])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA32_3) {
		if (p_sr_item->p_service_rec->service_data32[3] !=
		    p_svcr->service_record.service_data32[3])
			return;
	}

	if (comp_mask & IB_SR_COMPMASK_SDATA64_0) {
		if (p_sr_item->p_service_rec->service_data64[0] !=
		    p_svcr->service_record.service_data64[0])
			return;
	}
	if (comp_mask & IB_SR_COMPMASK_SDATA64_1) {
		if (p_sr_item->p_service_rec->service_data64[1] !=
		    p_svcr->service_record.service_data64[1])
			return;
	}

	/* Check that the requester port has the pkey which is the service_pkey.
	   If not - then it cannot receive this ServiceRecord. */
	/* The check is relevant only if the service_pkey is valid */
	if (!ib_pkey_is_invalid(p_svcr->service_record.service_pkey)) {
		if (!osm_physp_has_pkey(p_sr_item->sa->p_log,
					p_svcr->service_record.service_pkey,
					p_req_physp)) {
			OSM_LOG(p_sr_item->sa->p_log, OSM_LOG_VERBOSE,
				"requester port doesn't have the service_pkey: 0x%X\n",
				cl_ntoh16(p_svcr->service_record.service_pkey));
			return;
		}
	}

	p_sr_pool_item = malloc(SA_SR_RESP_SIZE);
	if (p_sr_pool_item == NULL) {
		OSM_LOG(p_sr_item->sa->p_log, OSM_LOG_ERROR, "ERR 2408: "
			"Unable to acquire Service Record from pool\n");
		goto Exit;
	}

	p_sr_pool_item->resp.service_rec = p_svcr->service_record;

	cl_qlist_insert_tail(&p_sr_item->sr_list, &p_sr_pool_item->list_item);

Exit:
	return;
}

static void sr_rcv_process_get_method(osm_sa_t * sa, IN osm_madw_t * p_madw)
{
	ib_sa_mad_t *p_sa_mad;
	ib_service_record_t *p_recvd_service_rec;
	osm_sr_match_item_t sr_match_item;
	osm_sr_search_ctxt_t context;
	osm_physp_t *p_req_physp;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	/* Grab the lock */
	cl_plock_acquire(sa->p_lock);

	/* update the requester physical port */
	p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		cl_plock_release(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2409: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_service_rec =
	    (ib_service_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requester port GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_physp_get_port_guid(p_req_physp)));
		osm_dump_service_record_v2(sa->p_log, p_recvd_service_rec,
					   FILE_ID, OSM_LOG_DEBUG);
	}

	cl_qlist_init(&sr_match_item.sr_list);
	sr_match_item.p_service_rec = p_recvd_service_rec;
	sr_match_item.comp_mask = p_sa_mad->comp_mask;
	sr_match_item.sa = sa;

	context.p_sr_item = &sr_match_item;
	context.p_req_physp = p_req_physp;

	cl_qlist_apply_func(&sa->p_subn->sa_sr_list, get_matching_sr, &context);

	cl_plock_release(sa->p_lock);

	if (p_sa_mad->method == IB_MAD_METHOD_GET &&
	    cl_qlist_count(&sr_match_item.sr_list) == 0) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"No records matched the Service Record query\n");
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_NO_RECORDS);
		goto Exit;
	}

	sr_rcv_respond(sa, p_madw, &sr_match_item.sr_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return;
}

static void sr_rcv_process_set_method(osm_sa_t * sa, IN osm_madw_t * p_madw)
{
	ib_sa_mad_t *p_sa_mad;
	ib_service_record_t *p_recvd_service_rec;
	ib_net64_t comp_mask;
	osm_svcr_t *p_svcr;
	osm_sa_item_t *p_sr_item;
	cl_qlist_t sr_list;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_service_rec =
	    (ib_service_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	comp_mask = p_sa_mad->comp_mask;

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_service_record_v2(sa->p_log, p_recvd_service_rec,
					   FILE_ID, OSM_LOG_DEBUG);

	if ((comp_mask & (IB_SR_COMPMASK_SID | IB_SR_COMPMASK_SGID)) !=
	    (IB_SR_COMPMASK_SID | IB_SR_COMPMASK_SGID)) {
		cl_plock_release(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
			"Component Mask RID check failed for METHOD_SET\n");
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_REQ_INVALID);
		goto Exit;
	}

	/* if we were not provided with a service lease make it infinite */
	if (!(comp_mask & IB_SR_COMPMASK_SLEASE)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"ServiceLease Component Mask not set - using infinite lease\n");
		p_recvd_service_rec->service_lease = 0xFFFFFFFF;
	}

	/* If Record exists with matching RID */
	p_svcr = osm_svcr_get_by_rid(sa->p_subn, sa->p_log,
				     p_recvd_service_rec);

	if (p_svcr == NULL) {
		/* Create the instance of the osm_svcr_t object */
		p_svcr = osm_svcr_new(p_recvd_service_rec);
		if (p_svcr == NULL) {
			cl_plock_release(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2411: "
				"Failed to create new service record\n");

			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_NO_RESOURCES);
			goto Exit;
		}

		/* Add this new osm_svcr_t object to subnet object */
		osm_svcr_insert_to_db(sa->p_subn, sa->p_log, p_svcr);

	} else			/* Update the old instance of the osm_svcr_t object */
		osm_svcr_init(p_svcr, p_recvd_service_rec);

	cl_plock_release(sa->p_lock);

	if (p_recvd_service_rec->service_lease != 0xFFFFFFFF) {
#if 0
		cl_timer_trim(&sa->sr_timer,
			      p_recvd_service_rec->service_lease * 1000);
#endif
		/*  This was a bug since no check was made to see if too long */
		/*  just make sure the timer works - get a call back within a second */
		cl_timer_trim(&sa->sr_timer, 1000);
		p_svcr->modified_time = cl_get_time_stamp_sec();
	}

	p_sr_item = malloc(SA_SR_RESP_SIZE);
	if (p_sr_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2412: "
			"Unable to acquire Service record\n");
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_NO_RESOURCES);
		goto Exit;
	}

	if ((comp_mask & IB_SR_COMPMASK_SPKEY) != IB_SR_COMPMASK_SPKEY)
		/* Set the Default Service P_Key in the response */
		p_recvd_service_rec->service_pkey = IB_DEFAULT_PKEY;

	p_sr_item->resp.service_rec = *p_recvd_service_rec;
	cl_qlist_init(&sr_list);

	cl_qlist_insert_tail(&sr_list, &p_sr_item->list_item);

	sr_rcv_respond(sa, p_madw, &sr_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

static void sr_rcv_process_delete_method(osm_sa_t * sa, IN osm_madw_t * p_madw)
{
	ib_sa_mad_t *p_sa_mad;
	ib_service_record_t *p_recvd_service_rec;
	osm_svcr_t *p_svcr;
	osm_sa_item_t *p_sr_item;
	cl_qlist_t sr_list;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_service_rec =
	    (ib_service_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	if (OSM_LOG_IS_ACTIVE_V2(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_service_record_v2(sa->p_log, p_recvd_service_rec,
					   FILE_ID, OSM_LOG_DEBUG);

	/* If Record exists with matching RID */
	p_svcr = osm_svcr_get_by_rid(sa->p_subn, sa->p_log,
				     p_recvd_service_rec);

	if (p_svcr == NULL) {
		cl_plock_release(sa->p_lock);
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"No records matched the RID\n");
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_NO_RECORDS);
		goto Exit;
	}

	osm_svcr_remove_from_db(sa->p_subn, sa->p_log, p_svcr);
	cl_plock_release(sa->p_lock);

	p_sr_item = malloc(SA_SR_RESP_SIZE);
	if (p_sr_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 2413: "
			"Unable to acquire Service record\n");
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_NO_RESOURCES);
		osm_svcr_delete(p_svcr);
		goto Exit;
	}

	/* provide back the copy of the record */
	p_sr_item->resp.service_rec = p_svcr->service_record;
	cl_qlist_init(&sr_list);

	cl_qlist_insert_tail(&sr_list, &p_sr_item->list_item);

	osm_svcr_delete(p_svcr);

	sr_rcv_respond(sa, p_madw, &sr_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return;
}

void osm_sr_rcv_process(IN void *context, IN void *data)
{
	osm_sa_t *sa = context;
	osm_madw_t *p_madw = data;
	ib_sa_mad_t *p_sa_mad;
	boolean_t valid;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_SERVICE_RECORD);

	switch (p_sa_mad->method) {
	case IB_MAD_METHOD_SET:
		cl_plock_excl_acquire(sa->p_lock);
		valid = validate_sr(sa, p_madw);
		if (!valid) {
			cl_plock_release(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
				"Component Mask check failed for set request\n");
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}
		sr_rcv_process_set_method(sa, p_madw);
		break;
	case IB_MAD_METHOD_DELETE:
		cl_plock_excl_acquire(sa->p_lock);
		valid = validate_sr(sa, p_madw);
		if (!valid) {
			cl_plock_release(sa->p_lock);
			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Component Mask check failed for delete request\n");
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_REQ_INVALID);
			goto Exit;
		}
		sr_rcv_process_delete_method(sa, p_madw);
		break;
	case IB_MAD_METHOD_GET:
	case IB_MAD_METHOD_GETTABLE:
		sr_rcv_process_get_method(sa, p_madw);
		break;
	default:
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Unsupported Method (%s) for ServiceRecord request\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		break;
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

void osm_sr_rcv_lease_cb(IN void *context)
{
	osm_sa_t *sa = context;
	cl_list_item_t *p_list_item;
	cl_list_item_t *p_next_list_item;
	osm_svcr_t *p_svcr;
	uint32_t curr_time;
	uint32_t elapsed_time;
	uint32_t trim_time = 20;	/*  maxiaml timer refresh is 20 seconds */

	OSM_LOG_ENTER(sa->p_log);

	cl_plock_excl_acquire(sa->p_lock);

	p_list_item = cl_qlist_head(&sa->p_subn->sa_sr_list);

	while (p_list_item != cl_qlist_end(&sa->p_subn->sa_sr_list)) {
		p_svcr = (osm_svcr_t *) p_list_item;

		if (p_svcr->service_record.service_lease == 0xFFFFFFFF) {
			p_list_item = cl_qlist_next(p_list_item);
			continue;
		}

		/* current time in seconds */
		curr_time = cl_get_time_stamp_sec();
		/* elapsed time from last modify */
		elapsed_time = curr_time - p_svcr->modified_time;
		/* but it can not be less then 1 */
		if (elapsed_time < 1)
			elapsed_time = 1;

		if (elapsed_time < p_svcr->lease_period) {
			/*
			   Just update the service lease period
			   note: for simplicity we work with a uint32_t field
			   external to the network order lease_period of the MAD
			 */
			p_svcr->lease_period -= elapsed_time;

			OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
				"Remaining time for Service Name:%s is:0x%X\n",
				p_svcr->service_record.service_name,
				p_svcr->lease_period);

			p_svcr->modified_time = curr_time;

			/* Update the trim timer */
			if (trim_time > p_svcr->lease_period) {
				trim_time = p_svcr->lease_period;
				if (trim_time < 1)
					trim_time = 1;
			}

			p_list_item = cl_qlist_next(p_list_item);
			continue;

		} else {
			p_next_list_item = cl_qlist_next(p_list_item);

			/* Remove the service Record */
			osm_svcr_remove_from_db(sa->p_subn, sa->p_log, p_svcr);

			osm_svcr_delete(p_svcr);

			p_list_item = p_next_list_item;
			continue;
		}
	}

	/* Release the Lock */
	cl_plock_release(sa->p_lock);

	if (trim_time != 0xFFFFFFFF) {
		cl_timer_trim(&sa->sr_timer, trim_time * 1000);	/* Convert to milli seconds */
	}

	OSM_LOG_EXIT(sa->p_log);
}
