/*
 * Copyright (c) 2002-2013 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2013, Oracle and/or its affiliates. All rights reserved.
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
 *    Implementation of osm_sm_state_mgr_t.
 * This file implements the SM State Manager object.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <time.h>
#include <iba/ib_types.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SM_STATE_MGR_C
#include <opensm/osm_sm.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_log.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_node.h>
#include <opensm/osm_port.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_opensm.h>

void osm_report_sm_state(osm_sm_t * sm)
{
	char buf[64];
	const char *state_str = osm_get_sm_mgr_state_str(sm->p_subn->sm_state);

	osm_log_v2(sm->p_log, OSM_LOG_SYS, FILE_ID, "Entering %s state\n", state_str);
	snprintf(buf, sizeof(buf), "ENTERING SM %s STATE", state_str);
	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE, buf);
}

static boolean_t sm_state_mgr_send_master_sm_info_req(osm_sm_t * sm, uint8_t sm_state)
{
	osm_madw_context_t context;
	const osm_port_t *p_port;
	ib_api_status_t status;
	osm_dr_path_t dr_path;
	ib_net64_t guid;
	boolean_t sent_req = FALSE;

	OSM_LOG_ENTER(sm->p_log);

	memset(&context, 0, sizeof(context));
	if (sm_state == IB_SMINFO_STATE_STANDBY) {
		/*
		 * We are in STANDBY state - this means we need to poll the
		 * master SM (according to master_guid).
		 * Send a query of SubnGet(SMInfo) to the subn
		 * master_sm_base_lid object.
		 */
		guid = sm->master_sm_guid;
	} else {
		/*
		 * We are not in STANDBY - this means we are in MASTER state -
		 * so we need to poll the SM that is saved in polling_sm_guid
		 * under sm.
		 * Send a query of SubnGet(SMInfo) to that SM.
		 */
		guid = sm->polling_sm_guid;
	}

	/* Verify that SM is not polling itself */
	if (guid == sm->p_subn->sm_port_guid) {
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"OpenSM doesn't poll itself\n");
		goto Exit;
	}

	p_port = osm_get_port_by_guid(sm->p_subn, guid);

	if (p_port == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3203: "
			"No port object for GUID 0x%016" PRIx64 "\n",
			cl_ntoh64(guid));
		goto Exit;
	}

	context.smi_context.port_guid = guid;
	context.smi_context.set_method = FALSE;
	memcpy(&dr_path, osm_physp_get_dr_path_ptr(p_port->p_physp), sizeof(osm_dr_path_t));

	status = osm_req_get(sm, &dr_path,
			     IB_MAD_ATTR_SM_INFO, 0, FALSE,
			     ib_port_info_get_m_key(&p_port->p_physp->port_info),
			     CL_DISP_MSGID_NONE, &context);

	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3204: "
			"Failure requesting SMInfo (%s)\n",
			ib_get_err_str(status));
	else
		sent_req = TRUE;

Exit:
	OSM_LOG_EXIT(sm->p_log);

	return (sent_req);
}

static void sm_state_mgr_start_polling(osm_sm_t * sm)
{
	uint32_t timeout;
	cl_status_t cl_status;

	OSM_LOG_ENTER(sm->p_log);

	/*
	 * Init the retry_number back to zero - need to restart counting
	 */
	sm->retry_number = 0;

	/*
	 * Send a SubnGet(SMInfo) query to the current (or new) master found.
	 */
	CL_PLOCK_ACQUIRE(sm->p_lock);
	timeout = sm->p_subn->opt.sminfo_polling_timeout;
	sm_state_mgr_send_master_sm_info_req(sm, sm->p_subn->sm_state);
	CL_PLOCK_RELEASE(sm->p_lock);

	/*
	 * Start a timer that will wake up every sminfo_polling_timeout milliseconds.
	 * The callback of the timer will send a SubnGet(SMInfo) to the Master SM
	 * and restart the timer
	 */
	cl_status = cl_timer_start(&sm->polling_timer, timeout);
	if (cl_status != CL_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3210: "
			"Failed to start polling timer\n");

	OSM_LOG_EXIT(sm->p_log);
}

void osm_sm_state_mgr_polling_callback(IN void *context)
{
	osm_sm_t *sm = context;
	uint32_t timeout;
	cl_status_t cl_status;
	uint8_t sm_state;

	OSM_LOG_ENTER(sm->p_log);

	cl_spinlock_acquire(&sm->state_lock);
	sm_state = sm->p_subn->sm_state;
	cl_spinlock_release(&sm->state_lock);

	CL_PLOCK_ACQUIRE(sm->p_lock);
	timeout = sm->p_subn->opt.sminfo_polling_timeout;

	/*
	 * We can be here in one of two cases:
	 * 1. We are a STANDBY sm polling on the master SM.
	 * 2. We are a MASTER sm, waiting for a handover from a remote master sm.
	 * If we are not in one of these cases - don't need to restart the poller.
	 */
	if (!((sm_state == IB_SMINFO_STATE_MASTER &&
	       sm->polling_sm_guid != 0) ||
	      sm_state == IB_SMINFO_STATE_STANDBY)) {
		CL_PLOCK_RELEASE(sm->p_lock);
		goto Exit;
	}

	/*
	 * If we are a STANDBY sm and the osm_exit_flag is set, then let's
	 * signal the subnet_up. This is relevant for the case of running only
	 * once. In that case - the program is stuck until this signal is
	 * received. In other cases - it is not relevant whether or not the
	 * signal is on - since we are currently in exit flow
	 */
	if (sm_state == IB_SMINFO_STATE_STANDBY && osm_exit_flag) {
		CL_PLOCK_RELEASE(sm->p_lock);
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Signalling subnet_up_event\n");
		cl_event_signal(&sm->subnet_up_event);
		goto Exit;
	}

	/*
	 * If retry number reached the max_retry_number in the subnet opt - call
	 * osm_sm_state_mgr_process with signal OSM_SM_SIGNAL_POLLING_TIMEOUT
	 */
	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE, "SM State %d (%s), Retry number:%d\n",
		sm->p_subn->sm_state,  osm_get_sm_mgr_state_str(sm->p_subn->sm_state),
		sm->retry_number);

	if (sm->retry_number > sm->p_subn->opt.polling_retry_number) {
		CL_PLOCK_RELEASE(sm->p_lock);
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Reached polling_retry_number value in retry_number. "
			"Go to DISCOVERY state\n");
		osm_sm_state_mgr_process(sm, OSM_SM_SIGNAL_POLLING_TIMEOUT);
		goto Exit;
	}

	/* Send a SubnGet(SMInfo) request to the remote sm (depends on our state) */
	if (sm_state_mgr_send_master_sm_info_req(sm, sm_state)) {
		/* Request sent, increment the retry number */
		sm->retry_number++;
	}

	CL_PLOCK_RELEASE(sm->p_lock);

	/* restart the timer */
	cl_status = cl_timer_start(&sm->polling_timer, timeout);
	if (cl_status != CL_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3211: "
			"Failed to restart polling timer\n");

Exit:
	OSM_LOG_EXIT(sm->p_log);
}

static void sm_state_mgr_signal_error(osm_sm_t * sm, IN osm_sm_signal_t signal)
{
	OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3207: "
		"Invalid signal %s in state %s\n",
		osm_get_sm_mgr_signal_str(signal),
		osm_get_sm_mgr_state_str(sm->p_subn->sm_state));
}

void osm_sm_state_mgr_signal_master_is_alive(osm_sm_t * sm)
{
	OSM_LOG_ENTER(sm->p_log);
	sm->retry_number = 0;
	OSM_LOG_EXIT(sm->p_log);
}

ib_api_status_t osm_sm_state_mgr_process(osm_sm_t * sm,
					 IN osm_sm_signal_t signal)
{
	ib_api_status_t status = IB_SUCCESS;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	/*
	 * The state lock prevents many race conditions from screwing
	 * up the state transition process.
	 */
	cl_spinlock_acquire(&sm->state_lock);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Received signal %s in state %s\n",
		osm_get_sm_mgr_signal_str(signal),
		osm_get_sm_mgr_state_str(sm->p_subn->sm_state));

	switch (sm->p_subn->sm_state) {
	case IB_SMINFO_STATE_DISCOVERING:
		switch (signal) {
		case OSM_SM_SIGNAL_DISCOVERY_COMPLETED:
			/*
			 * Update the state of the SM to MASTER
			 */
			/* Turn on the first_time_master_sweep flag */
			sm->p_subn->sm_state = IB_SMINFO_STATE_MASTER;
			osm_report_sm_state(sm);
			/*
			 * Make sure to set the subnet master_sm_base_lid
			 * to the sm_base_lid value
			 */
			CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
			sm->p_subn->first_time_master_sweep = TRUE;
			sm->p_subn->master_sm_base_lid =
			    sm->p_subn->sm_base_lid;
			CL_PLOCK_RELEASE(sm->p_lock);
			break;
		case OSM_SM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED:
			/*
			 * Finished all discovery actions - move to STANDBY
			 * start the polling
			 */
			sm->p_subn->sm_state = IB_SMINFO_STATE_STANDBY;
			osm_report_sm_state(sm);
			/*
			 * Since another SM is doing the LFT config - we should not
			 * ignore the results of it
			 */
			CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
			sm->p_subn->ignore_existing_lfts = FALSE;
			CL_PLOCK_RELEASE(sm->p_lock);
			sm_state_mgr_start_polling(sm);
			break;
		case OSM_SM_SIGNAL_HANDOVER:
			/*
			 * Signal for a new sweep. We need to discover the other SM.
			 * If we already discovered this SM, and got the
			 * HANDOVER - this means the remote SM is of lower priority.
			 * In this case we will stop polling it (since it is a lower
			 * priority SM in STANDBY state).
			 */
			osm_sm_signal(sm, OSM_SIGNAL_SWEEP);
			break;
		default:
			sm_state_mgr_signal_error(sm, signal);
			status = IB_INVALID_PARAMETER;
			break;
		}
		break;

	case IB_SMINFO_STATE_STANDBY:
		switch (signal) {
		case OSM_SM_SIGNAL_POLLING_TIMEOUT:
		case OSM_SM_SIGNAL_DISCOVER:
			/*
			 * case 1: Polling timeout occured - this means that the Master SM
			 * is no longer alive.
			 * case 2: Got a signal to move to DISCOVERING
			 * Move to DISCOVERING state and start sweeping
			 */
			sm->p_subn->sm_state = IB_SMINFO_STATE_DISCOVERING;
			osm_report_sm_state(sm);
			CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
			sm->p_subn->coming_out_of_standby = TRUE;
			CL_PLOCK_RELEASE(sm->p_lock);
			osm_sm_signal(sm, OSM_SIGNAL_SWEEP);
			break;
		case OSM_SM_SIGNAL_DISABLE:
			/*
			 * Update the state to NOT_ACTIVE
			 */
			sm->p_subn->sm_state = IB_SMINFO_STATE_NOTACTIVE;
			osm_report_sm_state(sm);
			break;
		case OSM_SM_SIGNAL_HANDOVER:
			/*
			 * Update the state to MASTER, and start sweeping
			 * OPTIONAL: send ACKNOWLEDGE
			 */
			/* Turn on the force_first_time_master_sweep flag */
			/* We want full reconfiguration to occur on the first */
			/* master sweep of this SM */
			CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
			/*
			 * Make sure to set the subnet master_sm_base_lid
			 * to the sm_base_lid value
			 */
			sm->p_subn->master_sm_base_lid =
			    sm->p_subn->sm_base_lid;

			sm->p_subn->force_first_time_master_sweep = TRUE;
			CL_PLOCK_RELEASE(sm->p_lock);

			sm->p_subn->sm_state = IB_SMINFO_STATE_MASTER;
			osm_report_sm_state(sm);
			osm_sm_signal(sm, OSM_SIGNAL_SWEEP);
			break;
		case OSM_SM_SIGNAL_ACKNOWLEDGE:
			/*
			 * Do nothing - already moved to STANDBY
			 */
			break;
		default:
			sm_state_mgr_signal_error(sm, signal);
			status = IB_INVALID_PARAMETER;
			break;
		}
		break;

	case IB_SMINFO_STATE_NOTACTIVE:
		switch (signal) {
		case OSM_SM_SIGNAL_STANDBY:
			/*
			 * Update the state to STANDBY
			 * start the polling
			 */
			sm->p_subn->sm_state = IB_SMINFO_STATE_STANDBY;
			osm_report_sm_state(sm);
			sm_state_mgr_start_polling(sm);
			break;
		default:
			sm_state_mgr_signal_error(sm, signal);
			status = IB_INVALID_PARAMETER;
			break;
		}
		break;

	case IB_SMINFO_STATE_MASTER:
		switch (signal) {
		case OSM_SM_SIGNAL_POLLING_TIMEOUT:
			/*
			 * We received a polling timeout - this means that we
			 * waited for a remote master sm to send us a handover,
			 * but didn't get it, and didn't get a response from
			 * that remote sm.
			 * We want to force a heavy sweep - hopefully this
			 * occurred because the remote sm died, and we'll find
			 * this out and configure the subnet after a heavy sweep.
			 * We also want to clear the polling_sm_guid - since
			 * we are done polling on that remote sm - we are
			 * sweeping again.
			 */
		case OSM_SM_SIGNAL_HANDOVER:
			/*
			 * If we received a handover in a master state - then we
			 * want to force a heavy sweep. This means that either
			 * we are in a sweep currently - in this case - no
			 * change, or we are in idle state - since we
			 * recognized a master SM before - so we want to make a
			 * heavy sweep and reconfigure the new subnet.
			 * We also want to clear the polling_sm_guid - since
			 * we are done polling on that remote sm - we got a
			 * handover from it.
			 */
			OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
				"Forcing heavy sweep. Received signal %s\n",
				osm_get_sm_mgr_signal_str(signal));
			CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
			sm->polling_sm_guid = 0;
			sm->p_subn->force_first_time_master_sweep = TRUE;
			CL_PLOCK_RELEASE(sm->p_lock);
			osm_sm_signal(sm, OSM_SIGNAL_SWEEP);
			break;
		case OSM_SM_SIGNAL_HANDOVER_SENT:
			/*
			 * Just sent a HANDOVER signal - move to STANDBY
			 * start the polling
			 */
			sm->p_subn->sm_state = IB_SMINFO_STATE_STANDBY;
			osm_report_sm_state(sm);
			sm_state_mgr_start_polling(sm);
			break;
		case OSM_SM_SIGNAL_WAIT_FOR_HANDOVER:
			/*
			 * We found a remote master SM, and we are waiting for
			 * it to handover the mastership to us. Need to start
			 * polling that SM, to make sure it is alive, if it
			 * isn't - then we should move back to discovering,
			 * since something must have happened to it.
			 */
			sm_state_mgr_start_polling(sm);
			break;
		case OSM_SM_SIGNAL_DISCOVER:
			sm->p_subn->sm_state = IB_SMINFO_STATE_DISCOVERING;
			osm_report_sm_state(sm);
			break;
		default:
			sm_state_mgr_signal_error(sm, signal);
			status = IB_INVALID_PARAMETER;
			break;
		}
		break;

	default:
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3208: "
			"Invalid state %s\n",
			osm_get_sm_mgr_state_str(sm->p_subn->sm_state));

	}

	cl_spinlock_release(&sm->state_lock);

	OSM_LOG_EXIT(sm->p_log);
	return status;
}

ib_api_status_t osm_sm_state_mgr_check_legality(osm_sm_t * sm,
						IN osm_sm_signal_t signal)
{
	ib_api_status_t status = IB_SUCCESS;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	/*
	 * The state lock prevents many race conditions from screwing
	 * up the state transition process.
	 */
	cl_spinlock_acquire(&sm->state_lock);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "Received signal %s in state %s\n",
		osm_get_sm_mgr_signal_str(signal),
		osm_get_sm_mgr_state_str(sm->p_subn->sm_state));

	switch (sm->p_subn->sm_state) {
	case IB_SMINFO_STATE_DISCOVERING:
		switch (signal) {
		case OSM_SM_SIGNAL_DISCOVERY_COMPLETED:
		case OSM_SM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED:
		case OSM_SM_SIGNAL_HANDOVER:
			status = IB_SUCCESS;
			break;
		default:
			sm_state_mgr_signal_error(sm, signal);
			status = IB_INVALID_PARAMETER;
			break;
		}
		break;

	case IB_SMINFO_STATE_STANDBY:
		switch (signal) {
		case OSM_SM_SIGNAL_POLLING_TIMEOUT:
		case OSM_SM_SIGNAL_DISCOVER:
		case OSM_SM_SIGNAL_DISABLE:
		case OSM_SM_SIGNAL_HANDOVER:
		case OSM_SM_SIGNAL_ACKNOWLEDGE:
			status = IB_SUCCESS;
			break;
		default:
			sm_state_mgr_signal_error(sm, signal);
			status = IB_INVALID_PARAMETER;
			break;
		}
		break;

	case IB_SMINFO_STATE_NOTACTIVE:
		switch (signal) {
		case OSM_SM_SIGNAL_STANDBY:
			status = IB_SUCCESS;
			break;
		default:
			sm_state_mgr_signal_error(sm, signal);
			status = IB_INVALID_PARAMETER;
			break;
		}
		break;

	case IB_SMINFO_STATE_MASTER:
		switch (signal) {
		case OSM_SM_SIGNAL_HANDOVER:
		case OSM_SM_SIGNAL_HANDOVER_SENT:
			status = IB_SUCCESS;
			break;
		default:
			sm_state_mgr_signal_error(sm, signal);
			status = IB_INVALID_PARAMETER;
			break;
		}
		break;

	default:
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3209: "
			"Invalid state %s\n",
			osm_get_sm_mgr_state_str(sm->p_subn->sm_state));
		status = IB_INVALID_PARAMETER;

	}

	cl_spinlock_release(&sm->state_lock);

	OSM_LOG_EXIT(sm->p_log);
	return status;
}
