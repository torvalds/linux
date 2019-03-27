/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
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
 * 	Declaration of osm_sm_t.
 *	This object represents an IBA subnet.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_SM_H_
#define _OSM_SM_H_

#include <iba/ib_types.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_event.h>
#include <complib/cl_thread.h>
#include <complib/cl_dispatcher.h>
#include <complib/cl_event_wheel.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_stats.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_vl15intf.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_log.h>
#include <opensm/osm_sm_mad_ctrl.h>
#include <opensm/osm_lid_mgr.h>
#include <opensm/osm_ucast_mgr.h>
#include <opensm/osm_port.h>
#include <opensm/osm_db.h>
#include <opensm/osm_remote_sm.h>
#include <opensm/osm_multicast.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/SM
* NAME
*	SM
*
* DESCRIPTION
*	The SM object encapsulates the information needed by the
*	OpenSM to instantiate a subnet manager.  The OpenSM allocates
*	one SM object per subnet manager.
*
*	The SM object is thread safe.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: SM/osm_sm_t
* NAME
*  osm_sm_t
*
* DESCRIPTION
*  Subnet Manager structure.
*
*  This object should be treated as opaque and should
*  be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_sm {
	osm_thread_state_t thread_state;
	unsigned signal_mask;
	cl_spinlock_t signal_lock;
	cl_spinlock_t state_lock;
	cl_event_t signal_event;
	cl_event_t subnet_up_event;
	cl_timer_t sweep_timer;
	cl_timer_t polling_timer;
	cl_event_wheel_t trap_aging_tracker;
	cl_thread_t sweeper;
	unsigned master_sm_found;
	uint32_t retry_number;
	ib_net64_t master_sm_guid;
	ib_net64_t polling_sm_guid;
	osm_subn_t *p_subn;
	osm_db_t *p_db;
	osm_vendor_t *p_vendor;
	osm_log_t *p_log;
	osm_mad_pool_t *p_mad_pool;
	osm_vl15_t *p_vl15;
	cl_dispatcher_t *p_disp;
	cl_plock_t *p_lock;
	atomic32_t sm_trans_id;
	uint16_t mlids_init_max;
	unsigned mlids_req_max;
	uint8_t *mlids_req;
	osm_sm_mad_ctrl_t mad_ctrl;
	osm_lid_mgr_t lid_mgr;
	osm_ucast_mgr_t ucast_mgr;
	cl_disp_reg_handle_t sweep_fail_disp_h;
	cl_disp_reg_handle_t ni_disp_h;
	cl_disp_reg_handle_t pi_disp_h;
	cl_disp_reg_handle_t gi_disp_h;
	cl_disp_reg_handle_t nd_disp_h;
	cl_disp_reg_handle_t si_disp_h;
	cl_disp_reg_handle_t lft_disp_h;
	cl_disp_reg_handle_t mft_disp_h;
	cl_disp_reg_handle_t sm_info_disp_h;
	cl_disp_reg_handle_t trap_disp_h;
	cl_disp_reg_handle_t slvl_disp_h;
	cl_disp_reg_handle_t vla_disp_h;
	cl_disp_reg_handle_t pkey_disp_h;
	cl_disp_reg_handle_t mlnx_epi_disp_h;
} osm_sm_t;
/*
* FIELDS
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_db
*		Pointer to the database (persistency) object
*
*	p_vendor
*		Pointer to the vendor specific interfaces object.
*
*	p_log
*		Pointer to the log object.
*
*	p_mad_pool
*		Pointer to the MAD pool.
*
*	p_vl15
*		Pointer to the VL15 interface.
*
*	mad_ctrl
*		MAD Controller.
*
*	p_disp
*		Pointer to the Dispatcher.
*
*	p_lock
*		Pointer to the serializing lock.
*
* SEE ALSO
*	SM object
*********/

/****f* OpenSM: SM/osm_sm_construct
* NAME
*	osm_sm_construct
*
* DESCRIPTION
*	This function constructs an SM object.
*
* SYNOPSIS
*/
void osm_sm_construct(IN osm_sm_t * p_sm);
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to a SM object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_sm_init, osm_sm_destroy
*
*	Calling osm_sm_construct is a prerequisite to calling any other
*	method except osm_sm_init.
*
* SEE ALSO
*	SM object, osm_sm_init, osm_sm_destroy
*********/

/****f* OpenSM: SM/osm_sm_shutdown
* NAME
*	osm_sm_shutdown
*
* DESCRIPTION
*	The osm_sm_shutdown function shutdowns an SM, stopping the sweeper
*	and unregistering all messages from the dispatcher
*
* SYNOPSIS
*/
void osm_sm_shutdown(IN osm_sm_t * p_sm);
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to a SM object to shutdown.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	SM object, osm_sm_construct, osm_sm_init
*********/

/****f* OpenSM: SM/osm_sm_destroy
* NAME
*	osm_sm_destroy
*
* DESCRIPTION
*	The osm_sm_destroy function destroys an SM, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_sm_destroy(IN osm_sm_t * p_sm);
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to a SM object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified SM object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to osm_sm_construct or
*	osm_sm_init.
*
* SEE ALSO
*	SM object, osm_sm_construct, osm_sm_init
*********/

/****f* OpenSM: SM/osm_sm_init
* NAME
*	osm_sm_init
*
* DESCRIPTION
*	The osm_sm_init function initializes a SM object for use.
*
* SYNOPSIS
*/
ib_api_status_t osm_sm_init(IN osm_sm_t * p_sm, IN osm_subn_t * p_subn,
			    IN osm_db_t * p_db, IN osm_vendor_t * p_vendor,
			    IN osm_mad_pool_t * p_mad_pool,
			    IN osm_vl15_t * p_vl15, IN osm_log_t * p_log,
			    IN osm_stats_t * p_stats,
			    IN cl_dispatcher_t * p_disp, IN cl_plock_t * p_lock);
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object to initialize.
*
*	p_subn
*		[in] Pointer to the Subnet object for this subnet.
*
*	p_vendor
*		[in] Pointer to the vendor specific interfaces object.
*
*	p_mad_pool
*		[in] Pointer to the MAD pool.
*
*	p_vl15
*		[in] Pointer to the VL15 interface.
*
*	p_log
*		[in] Pointer to the log object.
*
*	p_stats
*		[in] Pointer to the statistics object.
*
*	p_disp
*		[in] Pointer to the OpenSM central Dispatcher.
*
*	p_lock
*		[in] Pointer to the OpenSM serializing lock.
*
* RETURN VALUES
*	IB_SUCCESS if the SM object was initialized successfully.
*
* NOTES
*	Allows calling other SM methods.
*
* SEE ALSO
*	SM object, osm_sm_construct, osm_sm_destroy
*********/

/****f* OpenSM: SM/osm_sm_signal
* NAME
*	osm_sm_signal
*
* DESCRIPTION
*	Signal event to SM
*
* SYNOPSIS
*/
void osm_sm_signal(IN osm_sm_t * p_sm, osm_signal_t signal);
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object.
*
*	signal
*		[in] sm signal number.
*
* NOTES
*
* SEE ALSO
*	SM object
*********/

/****f* OpenSM: SM/osm_sm_sweep
* NAME
*	osm_sm_sweep
*
* DESCRIPTION
*	Initiates a subnet sweep.
*
* SYNOPSIS
*/
void osm_sm_sweep(IN osm_sm_t * p_sm);
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object.
*
* RETURN VALUES
*	IB_SUCCESS if the sweep completed successfully.
*
* NOTES
*
* SEE ALSO
*	SM object
*********/

/****f* OpenSM: SM/osm_sm_bind
* NAME
*	osm_sm_bind
*
* DESCRIPTION
*	Binds the sm object to a port guid.
*
* SYNOPSIS
*/
ib_api_status_t osm_sm_bind(IN osm_sm_t * p_sm, IN ib_net64_t port_guid);
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object to bind.
*
*	port_guid
*		[in] Local port GUID with which to bind.
*
*
* RETURN VALUES
*	None
*
* NOTES
*	A given SM object can only be bound to one port at a time.
*
* SEE ALSO
*********/

/****f* OpenSM: SM/osm_req_get
* NAME
*	osm_req_get
*
* DESCRIPTION
*	Starts the process to transmit a directed route request for
*	the attribute.
*
* SYNOPSIS
*/
ib_api_status_t osm_req_get(IN osm_sm_t * sm, IN const osm_dr_path_t * p_path,
			    IN ib_net16_t attr_id, IN ib_net32_t attr_mod,
			    IN boolean_t find_mkey, ib_net64_t m_key,
			    IN cl_disp_msgid_t err_msg,
			    IN const osm_madw_context_t * p_context);
/*
* PARAMETERS
*	sm
*		[in] Pointer to an osm_sm_t object.
*
*	p_path
*		[in] Pointer to the directed route path to the node
*		from which to retrieve the attribute.
*
*	attr_id
*		[in] Attribute ID to request.
*
*	attr_mod
*		[in] Attribute modifier for this request.
*
*	find_mkey
*		[in] Flag to indicate whether the M_Key should be looked up for
*		     this MAD.
* 	m_key
* 		[in] M_Key value to be send with this MAD. Applied, only when
* 		     find_mkey is FALSE.
*
*	err_msg
*		[in] Message id with which to post this MAD if an error occurs.
*
*	p_context
*		[in] Mad wrapper context structure to be copied into the wrapper
*		context, and thus visible to the recipient of the response.
*
* RETURN VALUES
*	IB_SUCCESS if the request was successful.
*
* NOTES
*	This function asynchronously requests the specified attribute.
*	The response from the node will be routed through the Dispatcher
*	to the appropriate receive controller object.
*********/

/****f* OpenSM: SM/osm_send_req_mad
* NAME
*       osm_send_req_mad
*
* DESCRIPTION
*	Starts the process to transmit a preallocated/predefined directed route
*	Set() request.
*
* SYNOPSIS
*/
void osm_send_req_mad(IN osm_sm_t * sm, IN osm_madw_t *p_madw);
/*
* PARAMETERS
*	sm
*		[in] Pointer to an osm_sm_t object.
*	p_madw
*		[in] Pointer to a preallocated MAD buffer
*
*********/

/***f* OpenSM: SM/osm_prepare_req_set
* NAME
*	osm_prepare_req_set
*
* DESCRIPTION
*	Preallocate and fill a directed route Set() MAD w/o sending it.
*
* SYNOPSIS
*/
osm_madw_t *osm_prepare_req_set(IN osm_sm_t * sm, IN const osm_dr_path_t * p_path,
				IN const uint8_t * p_payload,
				IN size_t payload_size, IN ib_net16_t attr_id,
				IN ib_net32_t attr_mod, IN boolean_t find_mkey,
				IN ib_net64_t m_key, IN cl_disp_msgid_t err_msg,
				IN const osm_madw_context_t * p_context);
/*
* PARAMETERS
*	sm
*		[in] Pointer to an osm_sm_t object.
*
*	p_path
*		[in] Pointer to the directed route path of the recipient.
*
*	p_payload
*		[in] Pointer to the SMP payload to send.
*
*	payload_size
*		[in] The size of the payload to be copied to the SMP data field.
*
*	attr_id
*		[in] Attribute ID to request.
*
*	attr_mod
*		[in] Attribute modifier for this request.
*
*	find_mkey
*		[in] Flag to indicate whether the M_Key should be looked up for
*		     this MAD.
* 	m_key
* 		[in] M_Key value to be send with this MAD. Applied, only when
* 		     find_mkey is FALSE.
*
*	err_msg
*		[in] Message id with which to post this MAD if an error occurs.
*
*	p_context
*		[in] Mad wrapper context structure to be copied into the wrapper
*		     context, and thus visible to the recipient of the response.
*
* RETURN VALUES
*	Pointer the MAD buffer in case of success and NULL in case of failure.
*
*********/

/****f* OpenSM: SM/osm_req_set
* NAME
*	osm_req_set
*
* DESCRIPTION
*	Starts the process to transmit a directed route Set() request.
*
* SYNOPSIS
*/
ib_api_status_t osm_req_set(IN osm_sm_t * sm, IN const osm_dr_path_t * p_path,
			    IN const uint8_t * p_payload,
			    IN size_t payload_size, IN ib_net16_t attr_id,
			    IN ib_net32_t attr_mod, IN boolean_t find_mkey,
			    IN ib_net64_t m_key, IN cl_disp_msgid_t err_msg,
			    IN const osm_madw_context_t * p_context);
/*
* PARAMETERS
*	sm
*		[in] Pointer to an osm_sm_t object.
*
*	p_path
*		[in] Pointer to the directed route path of the recipient.
*
*	p_payload
*		[in] Pointer to the SMP payload to send.
*
*	payload_size
*		[in] The size of the payload to be copied to the SMP data field.
*
*	attr_id
*		[in] Attribute ID to request.
*
*	attr_mod
*		[in] Attribute modifier for this request.
*
*	find_mkey
*		[in] Flag to indicate whether the M_Key should be looked up for
*		     this MAD.
*
* 	m_key
* 		[in] M_Key value to be send with this MAD. Applied, only when
* 		     find_mkey is FALSE.
*
*	err_msg
*		[in] Message id with which to post this MAD if an error occurs.
*
*	p_context
*		[in] Mad wrapper context structure to be copied into the wrapper
*		context, and thus visible to the recipient of the response.
*
* RETURN VALUES
*	IB_SUCCESS if the request was successful.
*
* NOTES
*	This function asynchronously requests the specified attribute.
*	The response from the node will be routed through the Dispatcher
*	to the appropriate receive controller object.
*********/
/****f* OpenSM: SM/osm_resp_send
* NAME
*	osm_resp_send
*
* DESCRIPTION
*	Starts the process to transmit a directed route response.
*
* SYNOPSIS
*/
ib_api_status_t osm_resp_send(IN osm_sm_t * sm,
			      IN const osm_madw_t * p_req_madw,
			      IN ib_net16_t status,
			      IN const uint8_t * p_payload);
/*
* PARAMETERS
*	p_resp
*		[in] Pointer to an osm_resp_t object.
*
*	p_madw
*		[in] Pointer to the MAD Wrapper object for the requesting MAD
*		to which this response is generated.
*
*	status
*		[in] Status for this response.
*
*	p_payload
*		[in] Pointer to the payload of the response MAD.
*
* RETURN VALUES
*	IB_SUCCESS if the response was successful.
*
*********/

/****f* OpenSM: SM/osm_sm_reroute_mlid
* NAME
*	osm_sm_reroute_mlid
*
* DESCRIPTION
*	Requests (schedules) MLID rerouting
*
* SYNOPSIS
*/
void osm_sm_reroute_mlid(osm_sm_t * sm, ib_net16_t mlid);

/*
* PARAMETERS
*	sm
*		[in] Pointer to an osm_sm_t object.
*
*	mlid
*		[in] MLID value
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: OpenSM/osm_sm_wait_for_subnet_up
* NAME
*	osm_sm_wait_for_subnet_up
*
* DESCRIPTION
*	Blocks the calling thread until the subnet is up.
*
* SYNOPSIS
*/
static inline cl_status_t osm_sm_wait_for_subnet_up(IN osm_sm_t * p_sm,
						    IN uint32_t wait_us,
						    IN boolean_t interruptible)
{
	return cl_event_wait_on(&p_sm->subnet_up_event, wait_us, interruptible);
}

/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object.
*
*	wait_us
*		[in] Number of microseconds to wait.
*
*	interruptible
*		[in] Indicates whether the wait operation can be interrupted
*		by external signals.
*
* RETURN VALUES
*	CL_SUCCESS if the wait operation succeeded in response to the event
*	being set.
*
*	CL_TIMEOUT if the specified time period elapses.
*
*	CL_NOT_DONE if the wait was interrupted by an external signal.
*
*	CL_ERROR if the wait operation failed.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: State Manager/osm_sm_is_greater_than
* NAME
*	osm_sm_is_greater_than
*
* DESCRIPTION
*	Compares two SM's (14.4.1.2)
*
* SYNOPSIS
*/
static inline boolean_t osm_sm_is_greater_than(IN uint8_t l_priority,
					       IN ib_net64_t l_guid,
					       IN uint8_t r_priority,
					       IN ib_net64_t r_guid)
{
	return (l_priority > r_priority
		|| (l_priority == r_priority
		    && cl_ntoh64(l_guid) < cl_ntoh64(r_guid)));
}

/*
* PARAMETERS
*	l_priority
*		[in] Priority of the SM on the "left"
*
*	l_guid
*		[in] GUID of the SM on the "left"
*
*	r_priority
*		[in] Priority of the SM on the "right"
*
*	r_guid
*		[in] GUID of the SM on the "right"
*
* RETURN VALUES
*	Return TRUE if an sm with l_priority and l_guid is higher than an sm
*	with r_priority and r_guid, return FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*	State Manager
*********/

/****f* OpenSM: SM State Manager/osm_sm_state_mgr_process
* NAME
*	osm_sm_state_mgr_process
*
* DESCRIPTION
*	Processes and maintains the states of the SM.
*
* SYNOPSIS
*/
ib_api_status_t osm_sm_state_mgr_process(IN osm_sm_t *sm,
					 IN osm_sm_signal_t signal);
/*
* PARAMETERS
*	sm
*		[in] Pointer to an osm_sm_t object.
*
*	signal
*		[in] Signal to the state SM engine.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*	State Manager
*********/

/****f* OpenSM: SM State Manager/osm_sm_state_mgr_signal_master_is_alive
* NAME
*	osm_sm_state_mgr_signal_master_is_alive
*
* DESCRIPTION
*	Signals that the remote Master SM is alive.
*	Need to clear the retry_number variable.
*
* SYNOPSIS
*/
void osm_sm_state_mgr_signal_master_is_alive(IN osm_sm_t *sm);
/*
* PARAMETERS
*	sm
*		[in] Pointer to an osm_sm_t object.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*	State Manager
*********/

/****f* OpenSM: SM State Manager/osm_sm_state_mgr_check_legality
* NAME
*	osm_sm_state_mgr_check_legality
*
* DESCRIPTION
*	Checks the legality of the signal received, according to the
*  current state of the SM state machine.
*
* SYNOPSIS
*/
ib_api_status_t osm_sm_state_mgr_check_legality(IN osm_sm_t *sm,
						IN osm_sm_signal_t signal);
/*
* PARAMETERS
*	sm
*		[in] Pointer to an osm_sm_t object.
*
*	signal
*		[in] Signal to the state SM engine.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*	State Manager
*********/

void osm_report_sm_state(osm_sm_t *sm);

/****f* OpenSM: SM State Manager/osm_send_trap144
* NAME
*	osm_send_trap144
*
* DESCRIPTION
*	Send trap 144 to the master SM.
*
* SYNOPSIS
*/
int osm_send_trap144(osm_sm_t *sm, ib_net16_t local);
/*
* PARAMETERS
*	sm
*		[in] Pointer to an osm_sm_t object.
*
*	local
*		[in] OtherLocalChanges mask in network byte order.
*
* RETURN VALUES
*	0 on success, non-zero value otherwise.
*
*********/

void osm_set_sm_priority(osm_sm_t *sm, uint8_t priority);

END_C_DECLS
#endif				/* _OSM_SM_H_ */
