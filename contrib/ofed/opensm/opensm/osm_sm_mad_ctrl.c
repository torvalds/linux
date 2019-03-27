/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
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
 *    Implementation of osm_sm_mad_ctrl_t.
 * This object represents the SM MAD request controller object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <complib/cl_debug.h>
#include <iba/ib_types.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SM_MAD_CTRL_C
#include <opensm/osm_sm_mad_ctrl.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_opensm.h>

/****f* opensm: SM/sm_mad_ctrl_retire_trans_mad
 * NAME
 * sm_mad_ctrl_retire_trans_mad
 *
 * DESCRIPTION
 * This function handles clean-up of MADs associated with the SM's
 * outstanding transactions on the wire.
 *
 * SYNOPSIS
 */

static void sm_mad_ctrl_retire_trans_mad(IN osm_sm_mad_ctrl_t * p_ctrl,
					 IN osm_madw_t * p_madw)
{
	uint32_t outstanding;

	OSM_LOG_ENTER(p_ctrl->p_log);

	CL_ASSERT(p_madw);
	/*
	   Return the MAD & wrapper to the pool.
	 */
	OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG,
		"Retiring MAD with TID 0x%" PRIx64 "\n",
		cl_ntoh64(osm_madw_get_smp_ptr(p_madw)->trans_id));

	osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);

	outstanding = osm_stats_dec_qp0_outstanding(p_ctrl->p_stats);

	OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG, "%u QP0 MADs outstanding%s\n",
		p_ctrl->p_stats->qp0_mads_outstanding,
		outstanding ? "" : ": wire is clean.");

	OSM_LOG_EXIT(p_ctrl->p_log);
}

/************/

/****f* opensm: SM/sm_mad_ctrl_disp_done_callback
 * NAME
 * sm_mad_ctrl_disp_done_callback
 *
 * DESCRIPTION
 * This function is the Dispatcher callback that indicates
 * a received MAD has been processed by the recipient.
 *
 * SYNOPSIS
 */
static void sm_mad_ctrl_disp_done_callback(IN void *context, IN void *p_data)
{
	osm_sm_mad_ctrl_t *p_ctrl = context;
	osm_madw_t *p_madw = p_data;
	ib_smp_t *p_smp;

	OSM_LOG_ENTER(p_ctrl->p_log);

	/*
	   If the MAD that just finished processing was a response,
	   then retire the transaction, since we must have generated
	   the request.

	   Otherwise, retire the transaction if a response was expected,
	   as in the case of a send failure. If a response was not expected,
	   just put the MAD back in the pool, because the MAD was a query
	   from some outside agent, e.g. Get(SMInfo) from another SM.
	 */
	p_smp = osm_madw_get_smp_ptr(p_madw);
	if (ib_smp_is_response(p_smp)) {
		CL_ASSERT(p_madw->resp_expected == FALSE);
		sm_mad_ctrl_retire_trans_mad(p_ctrl, p_madw);
	} else if (p_madw->resp_expected == TRUE)
		sm_mad_ctrl_retire_trans_mad(p_ctrl, p_madw);
	else
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);

	OSM_LOG_EXIT(p_ctrl->p_log);
}

/************/

/****f* opensm: SM/sm_mad_ctrl_update_wire_stats
 * NAME
 * sm_mad_ctrl_update_wire_stats
 *
 * DESCRIPTION
 * Updates wire stats for outstanding MADs and calls the VL15 poller.
 *
 * SYNOPSIS
 */
static void sm_mad_ctrl_update_wire_stats(IN osm_sm_mad_ctrl_t * p_ctrl)
{
	uint32_t mads_on_wire;

	OSM_LOG_ENTER(p_ctrl->p_log);

	mads_on_wire =
	    cl_atomic_dec(&p_ctrl->p_stats->qp0_mads_outstanding_on_wire);

	OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG,
		"%u SMPs on the wire, %u outstanding\n", mads_on_wire,
		p_ctrl->p_stats->qp0_mads_outstanding);

	/*
	   We can signal the VL15 controller to send another MAD
	   if any are waiting for transmission.
	 */
	osm_vl15_poll(p_ctrl->p_vl15);
	OSM_LOG_EXIT(p_ctrl->p_log);
}

/****f* opensm: SM/sm_mad_ctrl_process_get_resp
 * NAME
 * sm_mad_ctrl_process_get_resp
 *
 * DESCRIPTION
 * This function handles method GetResp() for received MADs.
 * This is the most common path for QP0 MADs.
 *
 * SYNOPSIS
 */
static void sm_mad_ctrl_process_get_resp(IN osm_sm_mad_ctrl_t * p_ctrl,
					 IN osm_madw_t * p_madw,
					 IN void *transaction_context)
{
	ib_smp_t *p_smp;
	cl_status_t status;
	osm_madw_t *p_old_madw;
	cl_disp_msgid_t msg_id = CL_DISP_MSGID_NONE;

	OSM_LOG_ENTER(p_ctrl->p_log);

	CL_ASSERT(p_madw);
	CL_ASSERT(transaction_context);

	p_smp = osm_madw_get_smp_ptr(p_madw);

	if (p_smp->mgmt_class == IB_MCLASS_SUBN_DIR && !ib_smp_is_d(p_smp)) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3102: "
			"'D' bit not set in returned SMP\n");
		osm_dump_dr_smp_v2(p_ctrl->p_log, p_smp, FILE_ID, OSM_LOG_ERROR);
	}

	p_old_madw = transaction_context;

	sm_mad_ctrl_update_wire_stats(p_ctrl);

	/*
	   Copy the MAD Wrapper context from the requesting MAD
	   to the new MAD.  This mechanism allows the recipient
	   controller to recover its own context regarding this
	   MAD transaction.  Once we've copied the context, we
	   can return the original MAD to the pool.
	 */
	osm_madw_copy_context(p_madw, p_old_madw);
	osm_mad_pool_put(p_ctrl->p_mad_pool, p_old_madw);

	/*
	   Note that attr_id (like the rest of the MAD) is in
	   network byte order.
	 */
	switch (p_smp->attr_id) {
	case IB_MAD_ATTR_NODE_DESC:
		msg_id = OSM_MSG_MAD_NODE_DESC;
		break;
	case IB_MAD_ATTR_NODE_INFO:
		msg_id = OSM_MSG_MAD_NODE_INFO;
		break;
	case IB_MAD_ATTR_GUID_INFO:
		msg_id = OSM_MSG_MAD_GUID_INFO;
		break;
	case IB_MAD_ATTR_SWITCH_INFO:
		msg_id = OSM_MSG_MAD_SWITCH_INFO;
		break;
	case IB_MAD_ATTR_PORT_INFO:
		msg_id = OSM_MSG_MAD_PORT_INFO;
		break;
	case IB_MAD_ATTR_LIN_FWD_TBL:
		msg_id = OSM_MSG_MAD_LFT;
		break;
	case IB_MAD_ATTR_MCAST_FWD_TBL:
		msg_id = OSM_MSG_MAD_MFT;
		break;
	case IB_MAD_ATTR_SM_INFO:
		msg_id = OSM_MSG_MAD_SM_INFO;
		break;
	case IB_MAD_ATTR_SLVL_TABLE:
		msg_id = OSM_MSG_MAD_SLVL;
		break;
	case IB_MAD_ATTR_VL_ARBITRATION:
		msg_id = OSM_MSG_MAD_VL_ARB;
		break;
	case IB_MAD_ATTR_P_KEY_TABLE:
		msg_id = OSM_MSG_MAD_PKEY;
		break;
	case IB_MAD_ATTR_MLNX_EXTENDED_PORT_INFO:
		msg_id = OSM_MSG_MAD_MLNX_EXT_PORT_INFO;
		break;
	case IB_MAD_ATTR_CLASS_PORT_INFO:
	case IB_MAD_ATTR_NOTICE:
	case IB_MAD_ATTR_INFORM_INFO:
	default:
		cl_atomic_inc(&p_ctrl->p_stats->qp0_mads_rcvd_unknown);
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3103: "
			"Unsupported attribute 0x%X (%s)\n",
			cl_ntoh16(p_smp->attr_id),
			ib_get_sm_attr_str(p_smp->attr_id));
		osm_dump_dr_smp_v2(p_ctrl->p_log, p_smp, FILE_ID, OSM_LOG_ERROR);
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		goto Exit;
	}

	/*
	   Post this MAD to the dispatcher for asynchronous
	   processing by the appropriate controller.
	 */

	OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG, "Posting Dispatcher message %s\n",
		osm_get_disp_msg_str(msg_id));

	status = cl_disp_post(p_ctrl->h_disp, msg_id, p_madw,
			      sm_mad_ctrl_disp_done_callback, p_ctrl);

	if (status != CL_SUCCESS) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3104: "
			"Dispatcher post message failed (%s) for attribute 0x%X (%s)\n",
			CL_STATUS_MSG(status), cl_ntoh16(p_smp->attr_id),
			ib_get_sm_attr_str(p_smp->attr_id));
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_ctrl->p_log);
}

/****f* opensm: SM/sm_mad_ctrl_process_get
 * NAME
 * sm_mad_ctrl_process_get
 *
 * DESCRIPTION
 * This function handles method Get() for received MADs.
 *
 * SYNOPSIS
 */
static void sm_mad_ctrl_process_get(IN osm_sm_mad_ctrl_t * p_ctrl,
				    IN osm_madw_t * p_madw)
{
	ib_smp_t *p_smp;
	cl_status_t status;
	cl_disp_msgid_t msg_id = CL_DISP_MSGID_NONE;

	OSM_LOG_ENTER(p_ctrl->p_log);

	p_smp = osm_madw_get_smp_ptr(p_madw);

	/*
	   Note that attr_id (like the rest of the MAD) is in
	   network byte order.
	 */
	switch (p_smp->attr_id) {
	case IB_MAD_ATTR_SM_INFO:
		msg_id = OSM_MSG_MAD_SM_INFO;
		break;
	default:
		cl_atomic_inc(&p_ctrl->p_stats->qp0_mads_rcvd_unknown);
		OSM_LOG(p_ctrl->p_log, OSM_LOG_VERBOSE,
			"Ignoring SubnGet MAD - unsupported attribute 0x%X\n",
			cl_ntoh16(p_smp->attr_id));
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		goto Exit;
	}

	/*
	   Post this MAD to the dispatcher for asynchronous
	   processing by the appropriate controller.
	 */

	OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG, "Posting Dispatcher message %s\n",
		osm_get_disp_msg_str(msg_id));

	status = cl_disp_post(p_ctrl->h_disp, msg_id, p_madw,
			      sm_mad_ctrl_disp_done_callback, p_ctrl);

	if (status != CL_SUCCESS) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3106: "
			"Dispatcher post message failed (%s)\n",
			CL_STATUS_MSG(status));
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

/****f* opensm: SM/sm_mad_ctrl_process_set
 * NAME
 * sm_mad_ctrl_process_set
 *
 * DESCRIPTION
 * This function handles method Set() for received MADs.
 *
 * SYNOPSIS
 */
static void sm_mad_ctrl_process_set(IN osm_sm_mad_ctrl_t * p_ctrl,
				    IN osm_madw_t * p_madw)
{
	ib_smp_t *p_smp;
	cl_status_t status;
	cl_disp_msgid_t msg_id = CL_DISP_MSGID_NONE;

	OSM_LOG_ENTER(p_ctrl->p_log);

	p_smp = osm_madw_get_smp_ptr(p_madw);

	/*
	   Note that attr_id (like the rest of the MAD) is in
	   network byte order.
	 */
	switch (p_smp->attr_id) {
	case IB_MAD_ATTR_SM_INFO:
		msg_id = OSM_MSG_MAD_SM_INFO;
		break;
	default:
		cl_atomic_inc(&p_ctrl->p_stats->qp0_mads_rcvd_unknown);
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3107: "
			"Unsupported attribute 0x%X (%s)\n",
			cl_ntoh16(p_smp->attr_id),
			ib_get_sm_attr_str(p_smp->attr_id));
		osm_dump_dr_smp_v2(p_ctrl->p_log, p_smp, FILE_ID, OSM_LOG_ERROR);
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		goto Exit;
	}

	/*
	   Post this MAD to the dispatcher for asynchronous
	   processing by the appropriate controller.
	 */

	OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG, "Posting Dispatcher message %s\n",
		osm_get_disp_msg_str(msg_id));

	status = cl_disp_post(p_ctrl->h_disp, msg_id, p_madw,
			      sm_mad_ctrl_disp_done_callback, p_ctrl);

	if (status != CL_SUCCESS) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3108: "
			"Dispatcher post message failed (%s)\n",
			CL_STATUS_MSG(status));
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

/****f* opensm: SM/sm_mad_ctrl_process_trap
 * NAME
 * sm_mad_ctrl_process_trap
 *
 * DESCRIPTION
 * This function handles method Trap() for received MADs.
 *
 * SYNOPSIS
 */
static void sm_mad_ctrl_process_trap(IN osm_sm_mad_ctrl_t * p_ctrl,
				     IN osm_madw_t * p_madw)
{
	ib_smp_t *p_smp;
	cl_status_t status;
	cl_disp_msgid_t msg_id = CL_DISP_MSGID_NONE;

	OSM_LOG_ENTER(p_ctrl->p_log);

	p_smp = osm_madw_get_smp_ptr(p_madw);

	/* Make sure OpenSM is master. If not - then we should not process the trap */
	if (p_ctrl->p_subn->sm_state != IB_SMINFO_STATE_MASTER) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG,
			"Received trap but OpenSM is not in MASTER state. "
			"Dropping mad\n");
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		goto Exit;
	}

	/*
	   Note that attr_id (like the rest of the MAD) is in
	   network byte order.
	 */
	switch (p_smp->attr_id) {
	case IB_MAD_ATTR_NOTICE:
		msg_id = OSM_MSG_MAD_NOTICE;
		break;
	default:
		cl_atomic_inc(&p_ctrl->p_stats->qp0_mads_rcvd_unknown);
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3109: "
			"Unsupported attribute 0x%X (%s)\n",
			cl_ntoh16(p_smp->attr_id),
			ib_get_sm_attr_str(p_smp->attr_id));
		osm_dump_dr_smp_v2(p_ctrl->p_log, p_smp, FILE_ID, OSM_LOG_ERROR);
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		goto Exit;
	}

	/*
	   Post this MAD to the dispatcher for asynchronous
	   processing by the appropriate controller.
	 */

	OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG, "Posting Dispatcher message %s\n",
		osm_get_disp_msg_str(msg_id));

	status = cl_disp_post(p_ctrl->h_disp, msg_id, p_madw,
			      sm_mad_ctrl_disp_done_callback, p_ctrl);

	if (status != CL_SUCCESS) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3110: "
			"Dispatcher post message failed (%s)\n",
			CL_STATUS_MSG(status));
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

/****f* opensm: SM/sm_mad_ctrl_process_trap_repress
 * NAME
 * sm_mad_ctrl_process_trap_repress
 *
 * DESCRIPTION
 * This function handles method TrapRepress() for received MADs.
 *
 * SYNOPSIS
 */
static void sm_mad_ctrl_process_trap_repress(IN osm_sm_mad_ctrl_t * p_ctrl,
					     IN osm_madw_t * p_madw)
{
	ib_smp_t *p_smp;

	OSM_LOG_ENTER(p_ctrl->p_log);

	p_smp = osm_madw_get_smp_ptr(p_madw);

	/*
	   Note that attr_id (like the rest of the MAD) is in
	   network byte order.
	 */
	switch (p_smp->attr_id) {
	case IB_MAD_ATTR_NOTICE:
		sm_mad_ctrl_update_wire_stats(p_ctrl);
		sm_mad_ctrl_retire_trans_mad(p_ctrl, p_madw);
		break;
	default:
		cl_atomic_inc(&p_ctrl->p_stats->qp0_mads_rcvd_unknown);
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3105: "
			"Unsupported attribute 0x%X (%s)\n",
			cl_ntoh16(p_smp->attr_id),
			ib_get_sm_attr_str(p_smp->attr_id));
		osm_dump_dr_smp_v2(p_ctrl->p_log, p_smp, FILE_ID, OSM_LOG_ERROR);
		osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);
		break;
	}

	OSM_LOG_EXIT(p_ctrl->p_log);
}

static void log_rcv_cb_error(osm_log_t *p_log, ib_smp_t *p_smp, ib_net16_t status)
{
	char buf[BUF_SIZE];
	uint32_t i;

	if (p_smp->mgmt_class == IB_MCLASS_SUBN_DIR) {
		char ipath[BUF_SIZE], rpath[BUF_SIZE];
		int ni = sprintf(ipath, "%d", p_smp->initial_path[0]);
		int nr = sprintf(rpath, "%d", p_smp->return_path[0]);
		for (i = 1; i <= p_smp->hop_count; i++) {
			ni += sprintf(ipath + ni, ",%d", p_smp->initial_path[i]);
			nr += sprintf(rpath + nr, ",%d", p_smp->return_path[i]);
		}
		snprintf(buf, sizeof(buf),
			 "\n\t\t\tInitial path: %s Return path: %s",
			 ipath, rpath);
	}

	OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 3111: "
		"Received MAD with error status = 0x%X\n"
		"\t\t\t%s(%s), attr_mod 0x%x, TID 0x%" PRIx64 "%s\n",
		cl_ntoh16(status), ib_get_sm_method_str(p_smp->method),
		ib_get_sm_attr_str(p_smp->attr_id), cl_ntoh32(p_smp->attr_mod),
		cl_ntoh64(p_smp->trans_id),
		p_smp->mgmt_class == IB_MCLASS_SUBN_DIR ? buf : "");

	osm_dump_dr_smp_v2(p_log, p_smp, FILE_ID, OSM_LOG_VERBOSE);
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

/****f* opensm: SM/sm_mad_ctrl_rcv_callback
 * NAME
 * sm_mad_ctrl_rcv_callback
 *
 * DESCRIPTION
 * This is the callback from the transport layer for received MADs.
 *
 * SYNOPSIS
 */
static void sm_mad_ctrl_rcv_callback(IN osm_madw_t * p_madw,
				     IN void *bind_context,
				     IN osm_madw_t * p_req_madw)
{
	osm_sm_mad_ctrl_t *p_ctrl = bind_context;
	ib_smp_t *p_smp;
	ib_net16_t status;

	OSM_LOG_ENTER(p_ctrl->p_log);

	CL_ASSERT(p_madw);

	/*
	   A MAD was received from the wire, possibly in response to a request.
	 */
	cl_atomic_inc(&p_ctrl->p_stats->qp0_mads_rcvd);

	OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG, "%u QP0 MADs received\n",
		p_ctrl->p_stats->qp0_mads_rcvd);

	p_smp = osm_madw_get_smp_ptr(p_madw);

	/* if we are closing down simply do nothing */
	if (osm_exit_flag) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR,
			"Ignoring received mad - since we are exiting\n");

		osm_dump_dr_smp_v2(p_ctrl->p_log, p_smp, FILE_ID, OSM_LOG_DEBUG);

		/* retire the mad or put it back */
		if (ib_smp_is_response(p_smp)) {
			CL_ASSERT(p_madw->resp_expected == FALSE);
			sm_mad_ctrl_retire_trans_mad(p_ctrl, p_madw);
		} else if (p_madw->resp_expected == TRUE)
			sm_mad_ctrl_retire_trans_mad(p_ctrl, p_madw);
		else
			osm_mad_pool_put(p_ctrl->p_mad_pool, p_madw);

		goto Exit;
	}

	if (OSM_LOG_IS_ACTIVE_V2(p_ctrl->p_log, OSM_LOG_FRAMES))
		osm_dump_dr_smp_v2(p_ctrl->p_log, p_smp, FILE_ID, OSM_LOG_FRAMES);

	if (p_smp->mgmt_class == IB_MCLASS_SUBN_DIR)
		status = ib_smp_get_status(p_smp);
	else
		status = p_smp->status;

	if (status != 0)
		log_rcv_cb_error(p_ctrl->p_log, p_smp, status);

	switch (p_smp->method) {
	case IB_MAD_METHOD_GET_RESP:
		CL_ASSERT(p_req_madw != NULL);
		sm_mad_ctrl_process_get_resp(p_ctrl, p_madw, p_req_madw);
		break;
	case IB_MAD_METHOD_GET:
		CL_ASSERT(p_req_madw == NULL);
		sm_mad_ctrl_process_get(p_ctrl, p_madw);
		break;
	case IB_MAD_METHOD_TRAP:
		CL_ASSERT(p_req_madw == NULL);
		sm_mad_ctrl_process_trap(p_ctrl, p_madw);
		break;
	case IB_MAD_METHOD_SET:
		CL_ASSERT(p_req_madw == NULL);
		sm_mad_ctrl_process_set(p_ctrl, p_madw);
		break;
	case IB_MAD_METHOD_TRAP_REPRESS:
		CL_ASSERT(p_req_madw != NULL);
		sm_mad_ctrl_process_trap_repress(p_ctrl, p_madw);
		break;
	case IB_MAD_METHOD_SEND:
	case IB_MAD_METHOD_REPORT:
	case IB_MAD_METHOD_REPORT_RESP:
	default:
		cl_atomic_inc(&p_ctrl->p_stats->qp0_mads_rcvd_unknown);
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3112: "
			"Unsupported method = 0x%X\n", p_smp->method);
		osm_dump_dr_smp_v2(p_ctrl->p_log, p_smp, FILE_ID, OSM_LOG_ERROR);
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

/****f* opensm: SM/sm_mad_ctrl_send_err_cb
 * NAME
 * sm_mad_ctrl_send_err_cb
 *
 * DESCRIPTION
 * This is the callback from the transport layer for send errors
 * on MADs that were expecting a response.
 *
 * SYNOPSIS
 */
static void sm_mad_ctrl_send_err_cb(IN void *context, IN osm_madw_t * p_madw)
{
	osm_sm_mad_ctrl_t *p_ctrl = context;
	ib_api_status_t status;
	ib_smp_t *p_smp;

	OSM_LOG_ENTER(p_ctrl->p_log);

	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3113: "
		"MAD completed in error (%s): "
		"%s(%s), attr_mod 0x%x, TID 0x%" PRIx64 "\n",
		ib_get_err_str(p_madw->status),
		ib_get_sm_method_str(p_smp->method),
		ib_get_sm_attr_str(p_smp->attr_id), cl_ntoh32(p_smp->attr_mod),
		cl_ntoh64(p_smp->trans_id));

	/*
	   If this was a SubnSet MAD, then this error might indicate a problem
	   in configuring the subnet. In this case - need to mark that there was
	   such a problem. The subnet will not be up, and the next sweep should
	   be a heavy sweep as well.
	 */
	if (p_smp->method == IB_MAD_METHOD_SET &&
	    (p_smp->attr_id == IB_MAD_ATTR_PORT_INFO ||
	     p_smp->attr_id == IB_MAD_ATTR_MLNX_EXTENDED_PORT_INFO ||
	     p_smp->attr_id == IB_MAD_ATTR_MCAST_FWD_TBL ||
	     p_smp->attr_id == IB_MAD_ATTR_SWITCH_INFO ||
	     p_smp->attr_id == IB_MAD_ATTR_LIN_FWD_TBL ||
	     p_smp->attr_id == IB_MAD_ATTR_P_KEY_TABLE)) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3119: "
			"Set method failed for attribute 0x%X (%s)\n",
			cl_ntoh16(p_smp->attr_id),
			ib_get_sm_attr_str(p_smp->attr_id));
		p_ctrl->p_subn->subnet_initialization_error = TRUE;
	} else if (p_madw->status == IB_TIMEOUT &&
		   p_smp->method == IB_MAD_METHOD_GET) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3120: "
			"Timeout while getting attribute 0x%X (%s); "
			"Possible mis-set mkey?\n",
			cl_ntoh16(p_smp->attr_id),
			ib_get_sm_attr_str(p_smp->attr_id));
	}

	osm_dump_dr_smp_v2(p_ctrl->p_log, p_smp, FILE_ID, OSM_LOG_VERBOSE);

	/*
	   Since we did not get any response we suspect the DR path
	   used for the target port.
	   Find it and replace it with an alternate path.
	   This is true only if the destination lid is not 0xFFFF, since
	   then we are aiming for a specific path and not specific destination
	   lid.
	 */
	/* For now - do not add the alternate dr path to the release */
#if 0
	if (p_madw->mad_addr.dest_lid != 0xFFFF) {
		osm_physp_t *p_physp = osm_get_physp_by_mad_addr(p_ctrl->p_log,
								 p_ctrl->p_subn,
								 &(p_madw->
								   mad_addr));
		if (!p_physp) {
			OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3114: "
				"Failed to find the corresponding phys port\n");
		} else {
			osm_physp_replace_dr_path_with_alternate_dr_path
			    (p_ctrl->p_log, p_ctrl->p_subn, p_physp,
			     p_madw->h_bind);
		}
	}
#endif

	/*
	   An error occurred.  No response was received to a request MAD.
	   Retire the original request MAD.
	 */
	sm_mad_ctrl_update_wire_stats(p_ctrl);

	if (osm_madw_get_err_msg(p_madw) != CL_DISP_MSGID_NONE) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_DEBUG,
			"Posting Dispatcher message %s\n",
			osm_get_disp_msg_str(osm_madw_get_err_msg(p_madw)));

		status = cl_disp_post(p_ctrl->h_disp,
				      osm_madw_get_err_msg(p_madw), p_madw,
				      sm_mad_ctrl_disp_done_callback, p_ctrl);
		if (status != CL_SUCCESS)
			OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3115: "
				"Dispatcher post message failed (%s)\n",
				CL_STATUS_MSG(status));
	} else
		/*
		   No error message was provided, just retire the MAD.
		 */
		sm_mad_ctrl_retire_trans_mad(p_ctrl, p_madw);

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

void osm_sm_mad_ctrl_construct(IN osm_sm_mad_ctrl_t * p_ctrl)
{
	CL_ASSERT(p_ctrl);
	memset(p_ctrl, 0, sizeof(*p_ctrl));
	p_ctrl->h_disp = CL_DISP_INVALID_HANDLE;
}

void osm_sm_mad_ctrl_destroy(IN osm_sm_mad_ctrl_t * p_ctrl)
{
	CL_ASSERT(p_ctrl);

	if (p_ctrl->h_bind != CL_DISP_INVALID_HANDLE)
		osm_vendor_unbind(p_ctrl->h_bind);
	cl_disp_unregister(p_ctrl->h_disp);
}

ib_api_status_t osm_sm_mad_ctrl_init(IN osm_sm_mad_ctrl_t * p_ctrl,
				     IN osm_subn_t * p_subn,
				     IN osm_mad_pool_t * p_mad_pool,
				     IN osm_vl15_t * p_vl15,
				     IN osm_vendor_t * p_vendor,
				     IN osm_log_t * p_log,
				     IN osm_stats_t * p_stats,
				     IN cl_plock_t * p_lock,
				     IN cl_dispatcher_t * p_disp)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_log);

	osm_sm_mad_ctrl_construct(p_ctrl);

	p_ctrl->p_subn = p_subn;
	p_ctrl->p_log = p_log;
	p_ctrl->p_disp = p_disp;
	p_ctrl->p_mad_pool = p_mad_pool;
	p_ctrl->p_vendor = p_vendor;
	p_ctrl->p_stats = p_stats;
	p_ctrl->p_lock = p_lock;
	p_ctrl->p_vl15 = p_vl15;

	p_ctrl->h_disp = cl_disp_register(p_disp, CL_DISP_MSGID_NONE, NULL,
					  NULL);

	if (p_ctrl->h_disp == CL_DISP_INVALID_HANDLE) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 3116: "
			"Dispatcher registration failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return status;
}

ib_api_status_t osm_sm_mad_ctrl_bind(IN osm_sm_mad_ctrl_t * p_ctrl,
				     IN ib_net64_t port_guid)
{
	osm_bind_info_t bind_info;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_ctrl->p_log);

	if (p_ctrl->h_bind != OSM_BIND_INVALID_HANDLE) {
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3117: "
			"Multiple binds not allowed\n");
		status = IB_ERROR;
		goto Exit;
	}

	bind_info.class_version = 1;
	bind_info.is_report_processor = FALSE;
	bind_info.is_responder = TRUE;
	bind_info.is_trap_processor = TRUE;
	bind_info.mad_class = IB_MCLASS_SUBN_DIR;
	bind_info.port_guid = port_guid;
	bind_info.recv_q_size = OSM_SM_DEFAULT_QP0_RCV_SIZE;
	bind_info.send_q_size = OSM_SM_DEFAULT_QP0_SEND_SIZE;
	bind_info.timeout = p_ctrl->p_subn->opt.transaction_timeout;
	bind_info.retries = p_ctrl->p_subn->opt.transaction_retries;

	OSM_LOG(p_ctrl->p_log, OSM_LOG_VERBOSE,
		"Binding to port 0x%" PRIx64 "\n", cl_ntoh64(port_guid));

	p_ctrl->h_bind = osm_vendor_bind(p_ctrl->p_vendor, &bind_info,
					 p_ctrl->p_mad_pool,
					 sm_mad_ctrl_rcv_callback,
					 sm_mad_ctrl_send_err_cb, p_ctrl);

	if (p_ctrl->h_bind == OSM_BIND_INVALID_HANDLE) {
		status = IB_ERROR;
		OSM_LOG(p_ctrl->p_log, OSM_LOG_ERROR, "ERR 3118: "
			"Vendor specific bind failed\n");
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_ctrl->p_log);
	return status;
}
