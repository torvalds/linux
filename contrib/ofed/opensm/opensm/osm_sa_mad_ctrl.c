/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 *    Implementation of osm_sa_mad_ctrl_t.
 * This object is part of the SA object.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <complib/cl_debug.h>
#include <iba/ib_types.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SA_MAD_CTRL_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_sa_mad_ctrl.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_sa.h>
#include <opensm/osm_opensm.h>

/****f* opensm: SA/sa_mad_ctrl_disp_done_callback
 * NAME
 * sa_mad_ctrl_disp_done_callback
 *
 * DESCRIPTION
 * This function is the Dispatcher callback that indicates
 * a received MAD has been processed by the recipient.
 *
 * SYNOPSIS
 */
static void sa_mad_ctrl_disp_done_callback(IN void *context, IN void *p_data)
{
	osm_sa_mad_ctrl_t *p_ctrl = context;
	osm_madw_t *p_madw = p_data;

	OSM_LOG_ENTER(p_ctrl->p_log);

	CL_ASSERT(p_madw);
	/*
	   Return the MAD & wrapper to the pool.
	 */
	osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
	OSM_LOG_EXIT(p_ctrl->p_log);
}

/************/

/****f* opensm: SA/sa_mad_ctrl_process
 * NAME
 * sa_mad_ctrl_process
 *
 * DESCRIPTION
 * This function handles known methods for received MADs.
 *
 * SYNOPSIS
 */
static void sa_mad_ctrl_process(IN osm_sa_mad_ctrl_t * p_ctrl,
				IN osm_madw_t * p_madw,
				IN boolean_t is_get_request)
{
	ib_sa_mad_t *p_sa_mad;
	cl_disp_reg_handle_t h_disp;
	cl_status_t status;
	cl_disp_msgid_t msg_id = CL_DISP_MSGID_NONE;
	uint64_t last_dispatched_msg_queue_time_msec;
	uint32_t num_messages;

	OSM_LOG_ENTER(p_ctrl->p_log);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);

	/*
	   If the dispatcher is showing us that it is overloaded
	   there is no point in placing the request in. We should instead
	   provide immediate response - IB_RESOURCE_BUSY
	   But how do we know?
	   The dispatcher reports back the number of outstanding messages and
	   the time the last message stayed in the queue.
	   HACK: Actually, we cannot send a mad from within the receive callback;
	   thus - we will just drop it.
	 */

	if (!is_get_request && p_ctrl->p_set_disp) {
		h_disp = p_ctrl->h_set_disp;
		goto SKIP_QUEUE_CHECK;
	}

	h_disp = p_ctrl->h_disp;
	cl_disp_get_queue_status(h_disp, &num_messages,
				 &last_dispatched_msg_queue_time_msec);

	if (num_messages > 1 && p_ctrl->p_subn->opt.max_msg_fifo_timeout &&
	    last_dispatched_msg_queue_time_msec >
	    p_ctrl->p_subn->opt.max_msg_fifo_timeout) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_INFO,
			/*             "Responding BUSY status since the dispatcher is already" */
			"Dropping MAD since the dispatcher is already"
			" overloaded with %u messages and queue time of:"
			"%" PRIu64 "[msec]\n",
			num_messages, last_dispatched_msg_queue_time_msec);

		/* send a busy response */
		/* osm_sa_send_error(p_ctrl->p_resp, p_madw, IB_RESOURCE_BUSY); */

		/* return the request to the pool */
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);

		goto Exit;
	}

SKIP_QUEUE_CHECK:
	/*
	   Note that attr_id (like the rest of the MAD) is in
	   network byte order.
	 */
	switch (p_sa_mad->attr_id) {
	case IB_MAD_ATTR_CLASS_PORT_INFO:
		msg_id = OSM_MSG_MAD_CLASS_PORT_INFO;
		break;

	case IB_MAD_ATTR_NODE_RECORD:
		msg_id = OSM_MSG_MAD_NODE_RECORD;
		break;

	case IB_MAD_ATTR_PORTINFO_RECORD:
		msg_id = OSM_MSG_MAD_PORTINFO_RECORD;
		break;

	case IB_MAD_ATTR_LINK_RECORD:
		msg_id = OSM_MSG_MAD_LINK_RECORD;
		break;

	case IB_MAD_ATTR_SMINFO_RECORD:
		msg_id = OSM_MSG_MAD_SMINFO_RECORD;
		break;

	case IB_MAD_ATTR_SERVICE_RECORD:
		msg_id = OSM_MSG_MAD_SERVICE_RECORD;
		break;

	case IB_MAD_ATTR_PATH_RECORD:
		msg_id = OSM_MSG_MAD_PATH_RECORD;
		break;

	case IB_MAD_ATTR_MCMEMBER_RECORD:
		msg_id = OSM_MSG_MAD_MCMEMBER_RECORD;
		break;

	case IB_MAD_ATTR_INFORM_INFO:
		msg_id = OSM_MSG_MAD_INFORM_INFO;
		break;

	case IB_MAD_ATTR_VLARB_RECORD:
		msg_id = OSM_MSG_MAD_VL_ARB_RECORD;
		break;

	case IB_MAD_ATTR_SLVL_RECORD:
		msg_id = OSM_MSG_MAD_SLVL_TBL_RECORD;
		break;

	case IB_MAD_ATTR_PKEY_TBL_RECORD:
		msg_id = OSM_MSG_MAD_PKEY_TBL_RECORD;
		break;

	case IB_MAD_ATTR_LFT_RECORD:
		msg_id = OSM_MSG_MAD_LFT_RECORD;
		break;

	case IB_MAD_ATTR_GUIDINFO_RECORD:
		msg_id = OSM_MSG_MAD_GUIDINFO_RECORD;
		break;

	case IB_MAD_ATTR_INFORM_INFO_RECORD:
		msg_id = OSM_MSG_MAD_INFORM_INFO_RECORD;
		break;

	case IB_MAD_ATTR_SWITCH_INFO_RECORD:
		msg_id = OSM_MSG_MAD_SWITCH_INFO_RECORD;
		break;

	case IB_MAD_ATTR_MFT_RECORD:
		msg_id = OSM_MSG_MAD_MFT_RECORD;
		break;

#if defined (VENDOR_RMPP_SUPPORT) && defined (DUAL_SIDED_RMPP)
	case IB_MAD_ATTR_MULTIPATH_RECORD:
		msg_id = OSM_MSG_MAD_MULTIPATH_RECORD;
		break;
#endif

	default:
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 1A01: "
			"Unsupported attribute 0x%X (%s)\n",
			cl_ntoh16(p_sa_mad->attr_id),
			ib_get_sa_attr_str(p_sa_mad->attr_id));
		osm_dump_sa_mad_v2(p_ctrl->p_log, p_sa_mad, FILE_ID, OSM_LOG_ERROR);
	}

	if (msg_id != CL_DISP_MSGID_NONE) {
		/*
		   Post this MAD to the dispatcher for asynchronous
		   processing by the appropriate controller.
		 */

		OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG,
			"Posting Dispatcher message %s\n",
			osm_get_disp_msg_str(msg_id));

		status = cl_disp_post(h_disp, msg_id, p_madw,
				      sa_mad_ctrl_disp_done_callback, p_ctrl);

		if (status != CL_SUCCESS) {
			OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 1A02: "
				"Dispatcher post message failed (%s) for attribute 0x%X (%s)\n",
				CL_STATUS_MSG(status),
				cl_ntoh16(p_sa_mad->attr_id),
				ib_get_sa_attr_str(p_sa_mad->attr_id));

			osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
			goto Exit;
		}
	} else {
		/*
		   There is an unknown MAD attribute type for which there is
		   no recipient.  Simply retire the MAD here.
		 */
		cl_atomic_inc(&p_ctrl->p_stats->sa_mads_rcvd_unknown);
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
	}

Exit:
	OSM_LOG_EXIT(p_ctrl->p_log);
}

/*
 * PARAMETERS
 *
 * RETURN VALUES
 *
 * NOTES
 *
 * SEE ALSO
 *********/

/****f* opensm: SA/sa_mad_ctrl_rcv_callback
 * NAME
 * sa_mad_ctrl_rcv_callback
 *
 * DESCRIPTION
 * This is the callback from the transport layer for received MADs.
 *
 * SYNOPSIS
 */
static void sa_mad_ctrl_rcv_callback(IN osm_madw_t * p_madw, IN void *context,
				     IN osm_madw_t * p_req_madw)
{
	osm_sa_mad_ctrl_t *p_ctrl = context;
	ib_sa_mad_t *p_sa_mad;
	boolean_t is_get_request = FALSE;

	OSM_LOG_ENTER(p_ctrl->p_log);

	CL_ASSERT(p_madw);

	/*
	   A MAD was received from the wire, possibly in response to a request.
	 */
	cl_atomic_inc(&p_ctrl->p_stats->sa_mads_rcvd);

	OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG,
		"%u SA MADs received\n", p_ctrl->p_stats->sa_mads_rcvd);

	/*
	 * C15-0.1.3 requires not responding to any MAD if the SM is
	 * not in active state!
	 * We will not respond if the sm_state is not MASTER, or if the
	 * first_time_master_sweep flag (of the subnet) is TRUE - this
	 * flag indicates that the master still didn't finish its first
	 * sweep, so the subnet is not up and stable yet.
	 */
	if (p_ctrl->p_subn->sm_state != IB_SMINFO_STATE_MASTER) {
		cl_atomic_inc(&p_ctrl->p_stats->sa_mads_ignored);
		OSM_LOG(p_ctrl->p_log, OSM_LOG_VERBOSE,
			"Received SA MAD while SM not MASTER. MAD ignored\n");
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		goto Exit;
	}
	if (p_ctrl->p_subn->first_time_master_sweep == TRUE) {
		cl_atomic_inc(&p_ctrl->p_stats->sa_mads_ignored);
		OSM_LOG(p_ctrl->p_log, OSM_LOG_VERBOSE,
			"Received SA MAD while SM in first sweep. MAD ignored\n");
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		goto Exit;
	}

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);

	if (OSM_LOG_IS_ACTIVE_V2(p_ctrl->p_log, OSM_LOG_FRAMES))
		osm_dump_sa_mad_v2(p_ctrl->p_log, p_sa_mad, FILE_ID, OSM_LOG_FRAMES);

	/*
	 * C15-0.1.5 - Table 185: SA Header - p884
	 * SM_key should be either 0 or match the current SM_Key
	 * otherwise discard the MAD.
	 */
	if (p_sa_mad->sm_key != 0 &&
	    p_sa_mad->sm_key != p_ctrl->p_subn->opt.sa_key) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 1A04: "
			"Non-Zero MAD SM_Key: 0x%" PRIx64 " != SM_Key: 0x%"
			PRIx64 "; SA MAD ignored for method 0x%X attribute 0x%X (%s)\n",
			cl_ntoh64(p_sa_mad->sm_key),
			cl_ntoh64(p_ctrl->p_subn->opt.sa_key),
			p_sa_mad->method, cl_ntoh16(p_sa_mad->attr_id),
			ib_get_sa_attr_str(p_sa_mad->attr_id));
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		goto Exit;
	}

	switch (p_sa_mad->method) {
	case IB_MAD_METHOD_REPORT_RESP:
		/* we do not really do anything with report responses -
		   just retire the transaction */
		OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG,
			"Received Report Response. Retiring the transaction\n");

		if (p_req_madw)
			osm_mad_pool_put(p_ctrl->p_mad_pool, p_req_madw);
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);

		break;

	case IB_MAD_METHOD_GET:
	case IB_MAD_METHOD_GETTABLE:
#if defined (VENDOR_RMPP_SUPPORT) && defined (DUAL_SIDED_RMPP)
	case IB_MAD_METHOD_GETMULTI:
#endif
		is_get_request = TRUE;
	case IB_MAD_METHOD_SET:
	case IB_MAD_METHOD_DELETE:
		/* if we are closing down simply do nothing */
		if (osm_exit_flag)
			osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		else
			sa_mad_ctrl_process(p_ctrl, p_madw, is_get_request);
		break;

	default:
		cl_atomic_inc(&p_ctrl->p_stats->sa_mads_rcvd_unknown);
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 1A05: "
			"Unsupported method = 0x%X\n", p_sa_mad->method);
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_ctrl->p_log);
}

/*
 * PARAMETERS
 *
 * RETURN VALUES
 *
 * NOTES
 *
 * SEE ALSO
 *********/

/****f* opensm: SA/sa_mad_ctrl_send_err_callback
 * NAME
 * sa_mad_ctrl_send_err_callback
 *
 * DESCRIPTION
 * This is the callback from the transport layer for send errors
 * on MADs that were expecting a response.
 *
 * SYNOPSIS
 */
static void sa_mad_ctrl_send_err_callback(IN void *context,
					  IN osm_madw_t * p_madw)
{
	osm_sa_mad_ctrl_t *p_ctrl = context;
	cl_status_t status;

	OSM_LOG_ENTER(p_ctrl->p_log);

	/*
	   We should never be here since the SA never originates a request.
	   Unless we generated a Report(Notice)
	 */

	CL_ASSERT(p_madw);

	OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 1A06: "
		"MAD completed in error (%s): "
		"%s(%s), attr_mod 0x%x, LID %u, TID 0x%" PRIx64 "\n",
		ib_get_err_str(p_madw->status),
		ib_get_sa_method_str(p_madw->p_mad->method),
		ib_get_sa_attr_str(p_madw->p_mad->attr_id),
		cl_ntoh32(p_madw->p_mad->attr_mod),
		cl_ntoh16(p_madw->mad_addr.dest_lid),
		cl_ntoh64(p_madw->p_mad->trans_id));

	osm_dump_sa_mad_v2(p_ctrl->p_log, osm_madw_get_sa_mad_ptr(p_madw),
			   FILE_ID, OSM_LOG_ERROR);

	/*
	   An error occurred.  No response was received to a request MAD.
	   Retire the original request MAD.
	 */

	if (osm_madw_get_err_msg(p_madw) != CL_DISP_MSGID_NONE) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG,
			"Posting Dispatcher message %s\n",
			osm_get_disp_msg_str(osm_madw_get_err_msg(p_madw)));

		if (p_ctrl->p_set_disp &&
		    (p_madw->p_mad->method == IB_MAD_METHOD_SET ||
		     p_madw->p_mad->method == IB_MAD_METHOD_DELETE))
			status = cl_disp_post(p_ctrl->h_set_disp,
					      osm_madw_get_err_msg(p_madw),
					      p_madw,
					      sa_mad_ctrl_disp_done_callback,
					      p_ctrl);
		else
			status = cl_disp_post(p_ctrl->h_disp,
					      osm_madw_get_err_msg(p_madw),
					      p_madw,
					      sa_mad_ctrl_disp_done_callback,
					      p_ctrl);
		if (status != CL_SUCCESS) {
			OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 1A07: "
				"Dispatcher post message failed (%s)\n",
				CL_STATUS_MSG(status));
		}
	} else			/* No error message was provided, just retire the MAD. */
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);

	OSM_LOG_EXIT(p_ctrl->p_log);
}

/*
 * PARAMETERS
 *
 * RETURN VALUES
 *
 * NOTES
 *
 * SEE ALSO
 *********/

void osm_sa_mad_ctrl_construct(IN osm_sa_mad_ctrl_t * p_ctrl)
{
	CL_ASSERT(p_ctrl);
	memset(p_ctrl, 0, sizeof(*p_ctrl));
	p_ctrl->h_disp = CL_DISP_INVALID_HANDLE;
	p_ctrl->h_set_disp = CL_DISP_INVALID_HANDLE;
}

void osm_sa_mad_ctrl_destroy(IN osm_sa_mad_ctrl_t * p_ctrl)
{
	CL_ASSERT(p_ctrl);
	cl_disp_unregister(p_ctrl->h_disp);
	cl_disp_unregister(p_ctrl->h_set_disp);
}

ib_api_status_t osm_sa_mad_ctrl_init(IN osm_sa_mad_ctrl_t * p_ctrl,
				     IN osm_sa_t * sa,
				     IN osm_mad_pool_t * p_mad_pool,
				     IN osm_vendor_t * p_vendor,
				     IN osm_subn_t * p_subn,
				     IN osm_log_t * p_log,
				     IN osm_stats_t * p_stats,
				     IN cl_dispatcher_t * p_disp,
				     IN cl_dispatcher_t * p_set_disp)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_log);

	osm_sa_mad_ctrl_construct(p_ctrl);

	p_ctrl->sa = sa;
	p_ctrl->p_log = p_log;
	p_ctrl->p_disp = p_disp;
	p_ctrl->p_set_disp = p_set_disp;
	p_ctrl->p_mad_pool = p_mad_pool;
	p_ctrl->p_vendor = p_vendor;
	p_ctrl->p_stats = p_stats;
	p_ctrl->p_subn = p_subn;

	p_ctrl->h_disp = cl_disp_register(p_disp, CL_DISP_MSGID_NONE, NULL,
					  p_ctrl);

	if (p_ctrl->h_disp == CL_DISP_INVALID_HANDLE) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 1A08: "
			"Dispatcher registration failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	if (p_set_disp) {
		p_ctrl->h_set_disp =
		    cl_disp_register(p_set_disp, CL_DISP_MSGID_NONE, NULL,
				     p_ctrl);

		if (p_ctrl->h_set_disp == CL_DISP_INVALID_HANDLE) {
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 1A0A: "
				"SA set dispatcher registration failed\n");
			status = IB_INSUFFICIENT_RESOURCES;
			goto Exit;
		}
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return status;
}

ib_api_status_t osm_sa_mad_ctrl_bind(IN osm_sa_mad_ctrl_t * p_ctrl,
				     IN ib_net64_t port_guid)
{
	osm_bind_info_t bind_info;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_ctrl->p_log);

	if (p_ctrl->h_bind != OSM_BIND_INVALID_HANDLE) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 1A09: "
			"Multiple binds not allowed\n");
		status = IB_ERROR;
		goto Exit;
	}

	bind_info.class_version = 2;
	bind_info.is_responder = TRUE;
	bind_info.is_report_processor = FALSE;
	bind_info.is_trap_processor = FALSE;
	bind_info.mad_class = IB_MCLASS_SUBN_ADM;
	bind_info.port_guid = port_guid;
	bind_info.recv_q_size = OSM_SM_DEFAULT_QP1_RCV_SIZE;
	bind_info.send_q_size = OSM_SM_DEFAULT_QP1_SEND_SIZE;
	bind_info.timeout = p_ctrl->sa->p_subn->opt.transaction_timeout;
	bind_info.retries = p_ctrl->sa->p_subn->opt.transaction_retries;

	OSM_LOG(p_ctrl->p_log, OSM_LOG_VERBOSE,
		"Binding to port GUID 0x%" PRIx64 "\n", cl_ntoh64(port_guid));

	p_ctrl->h_bind = osm_vendor_bind(p_ctrl->p_vendor, &bind_info,
					 p_ctrl->p_mad_pool,
					 sa_mad_ctrl_rcv_callback,
					 sa_mad_ctrl_send_err_callback, p_ctrl);

	if (p_ctrl->h_bind == OSM_BIND_INVALID_HANDLE) {
		status = IB_ERROR;
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 1A10: "
			"Vendor specific bind failed (%s)\n",
			ib_get_err_str(status));
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_ctrl->p_log);
	return status;
}

ib_api_status_t osm_sa_mad_ctrl_unbind(IN osm_sa_mad_ctrl_t * p_ctrl)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_ctrl->p_log);

	if (p_ctrl->h_bind == OSM_BIND_INVALID_HANDLE) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 1A11: "
			"No previous bind\n");
		status = IB_ERROR;
		goto Exit;
	}

	osm_vendor_unbind(p_ctrl->h_bind);
Exit:
	OSM_LOG_EXIT(p_ctrl->p_log);
	return status;
}
