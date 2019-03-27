/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005,2008 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009,2010 HNR Consulting. All rights reserved.
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
#include <complib/cl_debug.h>
#include <complib/cl_timer.h>
#include <complib/cl_event.h>
#include <vendor/osm_vendor_api.h>
#include <vendor/osm_vendor_sa_api.h>

/* this struct is the internal rep of the bind handle */
typedef struct _osmv_sa_bind_info {
	osm_bind_handle_t h_bind;
	osm_log_t *p_log;
	osm_vendor_t *p_vendor;
	osm_mad_pool_t *p_mad_pool;
	uint64_t port_guid;
	cl_event_t sync_event;
	uint64_t last_lids_update_sec;
	uint16_t lid;
	uint16_t sm_lid;
} osmv_sa_bind_info_t;

/*
  Call back on new mad received:

  We basically only need to set the context of the query.
  Or report an error.

  A pointer to the actual context of the request (a copy of the oriignal
  request structure) is attached as the p_madw->context.ni_context.node_guid
*/
static void
__osmv_sa_mad_rcv_cb(IN osm_madw_t * p_madw,
		     IN void *bind_context, IN osm_madw_t * p_req_madw)
{
	osmv_sa_bind_info_t *p_bind = (osmv_sa_bind_info_t *) bind_context;
	osmv_query_req_t *p_query_req_copy = NULL;
	osmv_query_res_t query_res;
	ib_sa_mad_t *p_sa_mad;
	ib_net16_t mad_status;

	OSM_LOG_ENTER(p_bind->p_log);

	if (!p_req_madw) {
		OSM_LOG(p_bind->p_log, OSM_LOG_DEBUG,
			"Ignoring a non-response mad\n");
		osm_mad_pool_put(p_bind->p_mad_pool, p_madw);
		goto Exit;
	}

	/* obtain the sent context */
	p_query_req_copy =
	    (osmv_query_req_t *) (p_req_madw->context.arb_context.context1);

	/* provide the context of the original request in the result */
	query_res.query_context = p_query_req_copy->query_context;

	/* provide the resulting madw */
	query_res.p_result_madw = p_madw;

	/* update the req fields */
	p_sa_mad = (ib_sa_mad_t *) p_madw->p_mad;

	/* if we got a remote error track it in the status */
	mad_status = (ib_net16_t) (p_sa_mad->status & IB_SMP_STATUS_MASK);
	if (mad_status != IB_SUCCESS) {
		OSM_LOG(p_bind->p_log, OSM_LOG_ERROR, "ERR 0501: "
			"Remote error:0x%04X\n", cl_ntoh16(mad_status));
		query_res.status = IB_REMOTE_ERROR;
	} else
		query_res.status = IB_SUCCESS;

	/* what if we have got back an empty mad ? */
	if (!p_madw->mad_size) {
		OSM_LOG(p_bind->p_log, OSM_LOG_ERROR, "ERR 0502: "
			"Got an empty mad\n");
		query_res.status = IB_ERROR;
	}

	if (IB_SUCCESS == mad_status) {

		/* if we are in not in a method response of an rmpp nature we must get only 1 */
		/* HACK: in the future we might need to be smarter for other methods... */
		if (p_sa_mad->method != IB_MAD_METHOD_GETTABLE_RESP) {
			query_res.result_cnt = 1;
		} else {
#ifndef VENDOR_RMPP_SUPPORT
			if (mad_status != IB_SUCCESS)
				query_res.result_cnt = 0;
			else
				query_res.result_cnt = 1;
#else
			/* we used the offset value to calculate the number of
			   records in here */
			if (ib_get_attr_size(p_sa_mad->attr_offset) == 0) {
				query_res.result_cnt = 0;
				OSM_LOG(p_bind->p_log, OSM_LOG_DEBUG,
					"Count = 0\n");
			}
			else {
				query_res.result_cnt =
					(p_madw->mad_size - IB_SA_MAD_HDR_SIZE) /
					ib_get_attr_size(p_sa_mad->attr_offset);
				OSM_LOG(p_bind->p_log, OSM_LOG_DEBUG,
					"Count = %u = %zu / %u (%zu)\n",
					query_res.result_cnt,
					p_madw->mad_size - IB_SA_MAD_HDR_SIZE,
					ib_get_attr_size(p_sa_mad->attr_offset),
					(p_madw->mad_size - IB_SA_MAD_HDR_SIZE) %
					ib_get_attr_size(p_sa_mad->attr_offset));
			}
#endif
		}
	}

	query_res.query_type = p_query_req_copy->query_type;

	p_query_req_copy->pfn_query_cb(&query_res);

	if ((p_query_req_copy->flags & OSM_SA_FLAGS_SYNC) == OSM_SA_FLAGS_SYNC)
		cl_event_signal(&p_bind->sync_event);

Exit:

	/* free the copied query request if found */
	if (p_query_req_copy)
		free(p_query_req_copy);

	/* put back the request madw */
	if (p_req_madw)
		osm_mad_pool_put(p_bind->p_mad_pool, p_req_madw);

	OSM_LOG_EXIT(p_bind->p_log);
}

/*
  Send Error Callback:

  Only report the error and get rid of the mad wrapper
*/
static void __osmv_sa_mad_err_cb(IN void *bind_context, IN osm_madw_t * p_madw)
{
	osmv_sa_bind_info_t *p_bind = (osmv_sa_bind_info_t *) bind_context;
	osmv_query_req_t *p_query_req_copy = NULL;
	osmv_query_res_t query_res;

	OSM_LOG_ENTER(p_bind->p_log);

	/* Obtain the sent context etc */
	p_query_req_copy =
	    (osmv_query_req_t *) (p_madw->context.arb_context.context1);

	/* provide the context of the original request in the result */
	query_res.query_context = p_query_req_copy->query_context;

	query_res.p_result_madw = p_madw;

	query_res.status = IB_TIMEOUT;
	query_res.result_cnt = 0;
	query_res.p_result_madw->status = IB_TIMEOUT;
	p_madw->status = IB_TIMEOUT;
	query_res.query_type = p_query_req_copy->query_type;

	p_query_req_copy->pfn_query_cb(&query_res);

	if ((p_query_req_copy->flags & OSM_SA_FLAGS_SYNC) == OSM_SA_FLAGS_SYNC)
		cl_event_signal(&p_bind->sync_event);

	free(p_query_req_copy);
	OSM_LOG_EXIT(p_bind->p_log);
}

/*****************************************************************************
 This routine needs to be invoked on every send - since the SM LID and Local
 lid might change. To do that without any major perfoermance impact we cache
 the results and time they were obtained. Refresh only twice a minute.
 To avoid the need to use statics and risk a race - we require the refresh time
 to be stored in the context of the results. Also this coveres cases were
 we query for multiple guids.
 *****************************************************************************/
static ib_api_status_t
__osmv_get_lid_and_sm_lid_by_port_guid(IN osm_vendor_t * const p_vend,
				       IN ib_net64_t port_guid,
				       IN OUT uint64_t * p_lids_update_time_sec,
				       OUT uint16_t * lid,
				       OUT uint16_t * sm_lid)
{

	ib_api_status_t status;
	ib_port_attr_t *p_attr_array;
	uint32_t num_ports;
	uint32_t port_num;

	OSM_LOG_ENTER(p_vend->p_log);

	/* use prevous values if current time is close enough to previous query */
	if (cl_get_time_stamp_sec() <= *p_lids_update_time_sec + 30) {
		OSM_LOG(p_vend->p_log, OSM_LOG_DEBUG,
			"Using previously stored lid:0x%04x sm_lid:0x%04x\n",
			*lid, *sm_lid);
		status = IB_SUCCESS;
		goto Exit;
	}

	/* obtain the number of available ports */
	num_ports = 0;
	status = osm_vendor_get_all_port_attr(p_vend, NULL, &num_ports);
	if (status != IB_INSUFFICIENT_MEMORY) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 0503: "
			"Expected to get the IB_INSUFFICIENT_MEMORY but got: %s\n",
			ib_get_err_str(status));
		status = IB_ERROR;
		goto Exit;
	}

	OSM_LOG(p_vend->p_log, OSM_LOG_DEBUG,
		"Found total of %u ports. Looking for guid:0x%016" PRIx64 "\n",
		num_ports, cl_ntoh64(port_guid));

	/* allocate the attributes */
	p_attr_array =
	    (ib_port_attr_t *) malloc(sizeof(ib_port_attr_t) * num_ports);

	/* obtain the attributes */
	status = osm_vendor_get_all_port_attr(p_vend, p_attr_array, &num_ports);
	if (status != IB_SUCCESS) {
		OSM_LOG(p_vend->p_log, OSM_LOG_ERROR, "ERR 0504: "
			"Failed to get port attributes (error: %s)\n",
			ib_get_err_str(status));
		free(p_attr_array);
		goto Exit;
	}

	status = IB_ERROR;
	/* find the port requested in the list */
	for (port_num = 0; (port_num < num_ports) && (status == IB_ERROR);
	     port_num++) {
		if (p_attr_array[port_num].port_guid == port_guid) {
			*lid = p_attr_array[port_num].lid;
			*sm_lid = p_attr_array[port_num].sm_lid;
			*p_lids_update_time_sec = cl_get_time_stamp_sec();
			status = IB_SUCCESS;
			OSM_LOG(p_vend->p_log, OSM_LOG_DEBUG,
				"Found guid:0x%016" PRIx64 " with idx:%d\n",
				cl_ntoh64(port_guid), port_num);
		}
	}

	free(p_attr_array);

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

osm_bind_handle_t
osmv_bind_sa(IN osm_vendor_t * const p_vend,
	     IN osm_mad_pool_t * const p_mad_pool, IN ib_net64_t port_guid)
{
	osm_bind_info_t bind_info;
	osm_log_t *p_log = p_vend->p_log;
	ib_api_status_t status = IB_SUCCESS;
	osmv_sa_bind_info_t *p_sa_bind_info;
	cl_status_t cl_status;

	OSM_LOG_ENTER(p_log);

	OSM_LOG(p_log, OSM_LOG_DEBUG,
		"Binding to port 0x%" PRIx64 "\n", cl_ntoh64(port_guid));

	bind_info.port_guid = port_guid;
	bind_info.mad_class = IB_MCLASS_SUBN_ADM;
	bind_info.class_version = 2;
	bind_info.is_responder = FALSE;
	bind_info.is_trap_processor = FALSE;
	bind_info.is_report_processor = FALSE;
	bind_info.send_q_size = 256;
	bind_info.recv_q_size = 256;

	/* allocate the new sa bind info */
	p_sa_bind_info =
	    (osmv_sa_bind_info_t *) malloc(sizeof(osmv_sa_bind_info_t));
	if (!p_sa_bind_info) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0505: "
			"Failed to allocate new bind structure\n");
		p_sa_bind_info = OSM_BIND_INVALID_HANDLE;
		goto Exit;
	}

	/* store some important context */
	p_sa_bind_info->p_log = p_log;
	p_sa_bind_info->port_guid = port_guid;
	p_sa_bind_info->p_mad_pool = p_mad_pool;
	p_sa_bind_info->p_vendor = p_vend;
	p_sa_bind_info->last_lids_update_sec = 0;

	/* Bind to the lower level */
	p_sa_bind_info->h_bind = osm_vendor_bind(p_vend, &bind_info, p_mad_pool, __osmv_sa_mad_rcv_cb, __osmv_sa_mad_err_cb, p_sa_bind_info);	/* context provided to CBs */

	if (p_sa_bind_info->h_bind == OSM_BIND_INVALID_HANDLE) {
		free(p_sa_bind_info);
		p_sa_bind_info = OSM_BIND_INVALID_HANDLE;
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0506: "
			"Failed to bind to vendor GSI\n");
		goto Exit;
	}

	/* obtain the sm_lid from the vendor */
	status =
	    __osmv_get_lid_and_sm_lid_by_port_guid(p_vend, port_guid,
						   &p_sa_bind_info->
						   last_lids_update_sec,
						   &p_sa_bind_info->lid,
						   &p_sa_bind_info->sm_lid);
	if (status != IB_SUCCESS) {
		free(p_sa_bind_info);
		p_sa_bind_info = OSM_BIND_INVALID_HANDLE;
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0507: "
			"Failed to obtain the SM lid\n");
		goto Exit;
	}

	/* initialize the sync_event */
	cl_event_construct(&p_sa_bind_info->sync_event);
	cl_status = cl_event_init(&p_sa_bind_info->sync_event, TRUE);
	if (cl_status != CL_SUCCESS) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0508: "
			"cl_init_event failed: %s\n", ib_get_err_str(cl_status));
		free(p_sa_bind_info);
		p_sa_bind_info = OSM_BIND_INVALID_HANDLE;
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return (p_sa_bind_info);
}

/****t* OSM Vendor SA Client/osmv_sa_mad_data
 * NAME
 *    osmv_sa_mad_data
 *
 * DESCRIPTION
 * Extra fields required to perform a mad query
 *  This struct is passed to the actual send method
 *
 * SYNOPSIS
 */
typedef struct _osmv_sa_mad_data {
	/* MAD data. */
	uint8_t method;
	ib_net16_t attr_id;
	ib_net16_t attr_offset;
	ib_net32_t attr_mod;
	ib_net64_t comp_mask;
	void *p_attr;
} osmv_sa_mad_data_t;
/*
 * method
 *    The method of the mad to be sent
 *
 *  attr_id
 *     Attribute ID
 *
 *  attr_offset
 *     Offset as defined by RMPP
 *
 *  attr_mod
 *     Attribute modifier
 *
 *  comp_mask
 *     The component mask of the query
 *
 *  p_attr
 *     A pointer to the record of the attribute to be sent.
 *
 *****/

/* Send a MAD out on the GSI interface */
static ib_api_status_t
__osmv_send_sa_req(IN osmv_sa_bind_info_t * p_bind,
		   IN const osmv_sa_mad_data_t * const p_sa_mad_data,
		   IN const osmv_query_req_t * const p_query_req)
{
	ib_api_status_t status;
	ib_mad_t *p_mad_hdr;
	ib_sa_mad_t *p_sa_mad;
	osm_madw_t *p_madw;
	osm_log_t *p_log = p_bind->p_log;
	static atomic32_t trans_id;
	boolean_t sync;
	osmv_query_req_t *p_query_req_copy;
	uint32_t sa_size;

	OSM_LOG_ENTER(p_log);

	/*
	   since the sm_lid might change we obtain it every send
	   (actually it is cached in the bind object and refreshed
	   every 30sec by this proc)
	 */
	status =
	    __osmv_get_lid_and_sm_lid_by_port_guid(p_bind->p_vendor,
						   p_bind->port_guid,
						   &p_bind->
						   last_lids_update_sec,
						   &p_bind->lid,
						   &p_bind->sm_lid);
	if (status != IB_SUCCESS) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0509: "
			"Failed to obtain the SM lid\n");
		goto Exit;
	}

	/* Get a MAD wrapper for the send */
	p_madw = osm_mad_pool_get(p_bind->p_mad_pool,
				  p_bind->h_bind, MAD_BLOCK_SIZE, NULL);

	if (p_madw == NULL) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0510: "
			"Unable to acquire MAD\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	/* Initialize the Sent MAD: */

	/* Initialize the MAD buffer for the send operation. */
	p_mad_hdr = osm_madw_get_mad_ptr(p_madw);
	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);

	/* Get a new transaction Id */
	cl_atomic_inc(&trans_id);

	/* Cleanup the MAD from any residue */
	memset(p_sa_mad, 0, MAD_BLOCK_SIZE);

	/* Initialize the standard MAD header. */
	ib_mad_init_new(p_mad_hdr,	/* mad pointer */
			IB_MCLASS_SUBN_ADM,	/* class */
			(uint8_t) 2,	/* version */
			p_sa_mad_data->method,	/* method */
			cl_hton64((uint64_t) trans_id),	/* tid */
			p_sa_mad_data->attr_id,	/* attr id */
			p_sa_mad_data->attr_mod	/* attr mod */);

	/* Set the query information. */
	p_sa_mad->sm_key = p_query_req->sm_key;
	p_sa_mad->attr_offset = 0;
	p_sa_mad->comp_mask = p_sa_mad_data->comp_mask;
	if (p_sa_mad->comp_mask) {
		p_sa_mad_data->attr_offset ? (sa_size = ib_get_attr_size(p_sa_mad_data->attr_offset)) : (sa_size = IB_SA_DATA_SIZE);
		memcpy(p_sa_mad->data, p_sa_mad_data->p_attr, sa_size);
	}

	/*
	   Provide the address to send to
	 */
	/* Patch to handle IBAL - host order , where it should take destination lid in network order */
#ifdef OSM_VENDOR_INTF_AL
	p_madw->mad_addr.dest_lid = p_bind->sm_lid;
#else
	p_madw->mad_addr.dest_lid = cl_hton16(p_bind->sm_lid);
#endif
	p_madw->mad_addr.addr_type.smi.source_lid = cl_hton16(p_bind->lid);
	p_madw->mad_addr.addr_type.gsi.remote_qp = CL_HTON32(1);
	p_madw->resp_expected = TRUE;
	p_madw->fail_msg = CL_DISP_MSGID_NONE;

	/*
	   Provide MAD context such that the call back will know what to do.
	   We have to keep the entire request structure so we know the CB.
	   Since we can not rely on the client to keep it around until
	   the response - we duplicate it and will later dispose it (in CB).
	   To store on the MADW we cast it into what opensm has:
	   p_madw->context.arb_context.context1
	 */
	p_query_req_copy = malloc(sizeof(*p_query_req_copy));
	if (!p_query_req_copy) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 0511: "
			"Unable to acquire memory for query copy\n");
		osm_mad_pool_put(p_bind->p_mad_pool, p_madw);
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}
	*p_query_req_copy = *p_query_req;
	p_madw->context.arb_context.context1 = p_query_req_copy;

	/* we can support async as well as sync calls */
	sync = ((p_query_req->flags & OSM_SA_FLAGS_SYNC) == OSM_SA_FLAGS_SYNC);

	/* send the mad asynchronously */
	status = osm_vendor_send(osm_madw_get_bind_handle(p_madw),
				 p_madw, p_madw->resp_expected);

	/* if synchronous - wait on the event */
	if (sync) {
		OSM_LOG(p_log, OSM_LOG_DEBUG, "Waiting for async event\n");
		cl_event_wait_on(&p_bind->sync_event, EVENT_NO_TIMEOUT, FALSE);
		cl_event_reset(&p_bind->sync_event);
		status = p_madw->status;
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return status;
}

/*
 * Query the SA based on the user's request.
 */
ib_api_status_t
osmv_query_sa(IN osm_bind_handle_t h_bind,
	      IN const osmv_query_req_t * const p_query_req)
{
	union {
		ib_service_record_t svc_rec;
		ib_node_record_t node_rec;
		ib_portinfo_record_t port_info;
		ib_path_rec_t path_rec;
#ifdef DUAL_SIDED_RMPP
		ib_multipath_rec_t multipath_rec;
#endif
		ib_class_port_info_t class_port_info;
	} u;
	osmv_sa_mad_data_t sa_mad_data;
	osmv_sa_bind_info_t *p_bind = (osmv_sa_bind_info_t *) h_bind;
	osmv_user_query_t *p_user_query;
#ifdef DUAL_SIDED_RMPP
	osmv_multipath_req_t *p_mpr_req;
	int i, j;
#endif
	osm_log_t *p_log = p_bind->p_log;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_log);

	/* Set the request information. */
	sa_mad_data.method = IB_MAD_METHOD_GETTABLE;
	sa_mad_data.attr_mod = 0;
	sa_mad_data.attr_offset = 0;

	/* Set the MAD attributes and component mask correctly. */
	switch (p_query_req->query_type) {

	case OSMV_QUERY_USER_DEFINED:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 USER_DEFINED\n");
		p_user_query = (osmv_user_query_t *) p_query_req->p_query_input;
		if (p_user_query->method)
			sa_mad_data.method = p_user_query->method;
#ifdef DUAL_SIDED_RMPP
		if (sa_mad_data.method == IB_MAD_METHOD_GETMULTI ||
		    sa_mad_data.method == IB_MAD_METHOD_GETTRACETABLE)
			sa_mad_data.attr_offset = p_user_query->attr_offset;
#endif
		sa_mad_data.attr_id = p_user_query->attr_id;
		sa_mad_data.attr_mod = p_user_query->attr_mod;
		sa_mad_data.comp_mask = p_user_query->comp_mask;
		sa_mad_data.p_attr = p_user_query->p_attr;
		break;

	case OSMV_QUERY_ALL_SVC_RECS:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 SVC_REC_BY_NAME\n");
		sa_mad_data.attr_id = IB_MAD_ATTR_SERVICE_RECORD;
		sa_mad_data.comp_mask = 0;
		sa_mad_data.p_attr = &u.svc_rec;
		break;

	case OSMV_QUERY_SVC_REC_BY_NAME:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 SVC_REC_BY_NAME\n");
		sa_mad_data.method = IB_MAD_METHOD_GET;
		sa_mad_data.attr_id = IB_MAD_ATTR_SERVICE_RECORD;
		sa_mad_data.comp_mask = IB_SR_COMPMASK_SNAME;
		sa_mad_data.p_attr = &u.svc_rec;
		memcpy(u.svc_rec.service_name, p_query_req->p_query_input,
		       sizeof(ib_svc_name_t));
		break;

	case OSMV_QUERY_SVC_REC_BY_ID:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 SVC_REC_BY_ID\n");
		sa_mad_data.attr_id = IB_MAD_ATTR_SERVICE_RECORD;
		sa_mad_data.comp_mask = IB_SR_COMPMASK_SID;
		sa_mad_data.p_attr = &u.svc_rec;
		u.svc_rec.service_id =
		    *(ib_net64_t *) (p_query_req->p_query_input);
		break;

	case OSMV_QUERY_CLASS_PORT_INFO:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 CLASS_PORT_INFO\n");
		sa_mad_data.method = IB_MAD_METHOD_GET;
		sa_mad_data.attr_id = IB_MAD_ATTR_CLASS_PORT_INFO;
		sa_mad_data.comp_mask = 0;
		sa_mad_data.p_attr = &u.class_port_info;
		break;

	case OSMV_QUERY_NODE_REC_BY_NODE_GUID:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 NODE_REC_BY_NODE_GUID\n");
		sa_mad_data.attr_id = IB_MAD_ATTR_NODE_RECORD;
		sa_mad_data.comp_mask = IB_NR_COMPMASK_NODEGUID;
		sa_mad_data.p_attr = &u.node_rec;
		u.node_rec.node_info.node_guid =
		    *(ib_net64_t *) (p_query_req->p_query_input);
		break;

	case OSMV_QUERY_PORT_REC_BY_LID:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 PORT_REC_BY_LID\n");
		sa_mad_data.attr_id = IB_MAD_ATTR_PORTINFO_RECORD;
		sa_mad_data.comp_mask = IB_PIR_COMPMASK_LID;
		sa_mad_data.p_attr = &u.port_info;
		u.port_info.lid = *(ib_net16_t *) (p_query_req->p_query_input);
		break;

	case OSMV_QUERY_PORT_REC_BY_LID_AND_NUM:
		sa_mad_data.method = IB_MAD_METHOD_GET;
		p_user_query = (osmv_user_query_t *) p_query_req->p_query_input;
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 PORT_REC_BY_LID_AND_NUM\n");
		sa_mad_data.attr_id = IB_MAD_ATTR_PORTINFO_RECORD;
		sa_mad_data.comp_mask =
		    IB_PIR_COMPMASK_LID | IB_PIR_COMPMASK_PORTNUM;
		sa_mad_data.p_attr = p_user_query->p_attr;
		break;

	case OSMV_QUERY_VLARB_BY_LID_PORT_BLOCK:
		sa_mad_data.method = IB_MAD_METHOD_GET;
		p_user_query = (osmv_user_query_t *) p_query_req->p_query_input;
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 OSMV_QUERY_VLARB_BY_LID_PORT_BLOCK\n");
		sa_mad_data.attr_id = IB_MAD_ATTR_VLARB_RECORD;
		sa_mad_data.comp_mask =
		    IB_VLA_COMPMASK_LID | IB_VLA_COMPMASK_OUT_PORT |
		    IB_VLA_COMPMASK_BLOCK;
		sa_mad_data.p_attr = p_user_query->p_attr;
		break;

	case OSMV_QUERY_SLVL_BY_LID_AND_PORTS:
		sa_mad_data.method = IB_MAD_METHOD_GET;
		p_user_query = (osmv_user_query_t *) p_query_req->p_query_input;
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 OSMV_QUERY_VLARB_BY_LID_PORT_BLOCK\n");
		sa_mad_data.attr_id = IB_MAD_ATTR_SLVL_RECORD;
		sa_mad_data.comp_mask =
		    IB_SLVL_COMPMASK_LID | IB_SLVL_COMPMASK_OUT_PORT |
		    IB_SLVL_COMPMASK_IN_PORT;
		sa_mad_data.p_attr = p_user_query->p_attr;
		break;

	case OSMV_QUERY_PATH_REC_BY_PORT_GUIDS:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 PATH_REC_BY_PORT_GUIDS\n");
		memset(&u.path_rec, 0, sizeof(ib_path_rec_t));
		sa_mad_data.attr_id = IB_MAD_ATTR_PATH_RECORD;
		sa_mad_data.comp_mask =
		    (IB_PR_COMPMASK_DGID | IB_PR_COMPMASK_SGID | IB_PR_COMPMASK_NUMBPATH);
		u.path_rec.num_path = 0x7f;
		sa_mad_data.p_attr = &u.path_rec;
		ib_gid_set_default(&u.path_rec.dgid,
				   ((osmv_guid_pair_t *) (p_query_req->
							  p_query_input))->
				   dest_guid);
		ib_gid_set_default(&u.path_rec.sgid,
				   ((osmv_guid_pair_t *) (p_query_req->
							  p_query_input))->
				   src_guid);
		break;

	case OSMV_QUERY_PATH_REC_BY_GIDS:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 PATH_REC_BY_GIDS\n");
		memset(&u.path_rec, 0, sizeof(ib_path_rec_t));
		sa_mad_data.attr_id = IB_MAD_ATTR_PATH_RECORD;
		sa_mad_data.comp_mask =
		    (IB_PR_COMPMASK_DGID | IB_PR_COMPMASK_SGID | IB_PR_COMPMASK_NUMBPATH);
		u.path_rec.num_path = 0x7f;
		sa_mad_data.p_attr = &u.path_rec;
		memcpy(&u.path_rec.dgid,
		       &((osmv_gid_pair_t *) (p_query_req->p_query_input))->
		       dest_gid, sizeof(ib_gid_t));
		memcpy(&u.path_rec.sgid,
		       &((osmv_gid_pair_t *) (p_query_req->p_query_input))->
		       src_gid, sizeof(ib_gid_t));
		break;

	case OSMV_QUERY_PATH_REC_BY_LIDS:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 PATH_REC_BY_LIDS\n");
		memset(&u.path_rec, 0, sizeof(ib_path_rec_t));
		sa_mad_data.method = IB_MAD_METHOD_GET;
		sa_mad_data.attr_id = IB_MAD_ATTR_PATH_RECORD;
		sa_mad_data.comp_mask =
		    (IB_PR_COMPMASK_DLID | IB_PR_COMPMASK_SLID);
		sa_mad_data.p_attr = &u.path_rec;
		u.path_rec.dlid =
		    ((osmv_lid_pair_t *) (p_query_req->p_query_input))->
		    dest_lid;
		u.path_rec.slid =
		    ((osmv_lid_pair_t *) (p_query_req->p_query_input))->src_lid;
		break;

	case OSMV_QUERY_UD_MULTICAST_SET:
		sa_mad_data.method = IB_MAD_METHOD_SET;
		p_user_query = (osmv_user_query_t *) p_query_req->p_query_input;
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 OSMV_QUERY_UD_MULTICAST_SET\n");
		sa_mad_data.attr_id = IB_MAD_ATTR_MCMEMBER_RECORD;
		sa_mad_data.comp_mask = p_user_query->comp_mask;
		sa_mad_data.p_attr = p_user_query->p_attr;
		break;

	case OSMV_QUERY_UD_MULTICAST_DELETE:
		sa_mad_data.method = IB_MAD_METHOD_DELETE;
		p_user_query = (osmv_user_query_t *) p_query_req->p_query_input;
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 OSMV_QUERY_UD_MULTICAST_DELETE\n");
		sa_mad_data.attr_id = IB_MAD_ATTR_MCMEMBER_RECORD;
		sa_mad_data.comp_mask = p_user_query->comp_mask;
		sa_mad_data.p_attr = p_user_query->p_attr;
		break;

#ifdef DUAL_SIDED_RMPP
	case OSMV_QUERY_MULTIPATH_REC:
		OSM_LOG(p_log, OSM_LOG_DEBUG, "DBG:001 MULTIPATH_REC\n");
		/* Validate sgid/dgid counts against SA client limit */
		p_mpr_req = (osmv_multipath_req_t *) p_query_req->p_query_input;
		if (p_mpr_req->sgid_count + p_mpr_req->dgid_count >
		    IB_MULTIPATH_MAX_GIDS) {
			OSM_LOG(p_log, OSM_LOG_ERROR, "DBG:001 MULTIPATH_REC "
				"SGID count %d DGID count %d max count %d\n",
				p_mpr_req->sgid_count, p_mpr_req->dgid_count,
				IB_MULTIPATH_MAX_GIDS);
			CL_ASSERT(0);
			return IB_ERROR;
		}
		memset(&u.multipath_rec, 0, sizeof(ib_multipath_rec_t));
		sa_mad_data.method = IB_MAD_METHOD_GETMULTI;
		sa_mad_data.attr_id = IB_MAD_ATTR_MULTIPATH_RECORD;
		sa_mad_data.attr_offset =
		    ib_get_attr_offset(sizeof(ib_multipath_rec_t));
		sa_mad_data.p_attr = &u.multipath_rec;
		sa_mad_data.comp_mask = p_mpr_req->comp_mask;
		u.multipath_rec.num_path = p_mpr_req->num_path;
		if (p_mpr_req->reversible)
			u.multipath_rec.num_path |= 0x80;
		else
			u.multipath_rec.num_path &= ~0x80;
		u.multipath_rec.pkey = p_mpr_req->pkey;
		ib_multipath_rec_set_sl(&u.multipath_rec, p_mpr_req->sl);
		ib_multipath_rec_set_qos_class(&u.multipath_rec, 0);
		u.multipath_rec.independence = p_mpr_req->independence;
		u.multipath_rec.sgid_count = p_mpr_req->sgid_count;
		u.multipath_rec.dgid_count = p_mpr_req->dgid_count;
		j = 0;
		for (i = 0; i < p_mpr_req->sgid_count; i++, j++)
			u.multipath_rec.gids[j] = p_mpr_req->gids[j];
		for (i = 0; i < p_mpr_req->dgid_count; i++, j++)
			u.multipath_rec.gids[j] = p_mpr_req->gids[j];
		break;
#endif

	default:
		OSM_LOG(p_log, OSM_LOG_ERROR, "DBG:001 UNKNOWN\n");
		CL_ASSERT(0);
		return IB_ERROR;
	}

	status = __osmv_send_sa_req(h_bind, &sa_mad_data, p_query_req);

	OSM_LOG_EXIT(p_log);
	return status;
}
