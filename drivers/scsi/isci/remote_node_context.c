/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "host.h"
#include "state_machine.h"
#include "scic_sds_port.h"
#include "remote_device.h"
#include "remote_node_context.h"
#include "sci_util.h"
#include "scu_event_codes.h"
#include "scu_task_context.h"


/**
 *
 * @sci_rnc: The RNC for which the is posted request is being made.
 *
 * This method will return true if the RNC is not in the initial state.  In all
 * other states the RNC is considered active and this will return true. The
 * destroy request of the state machine drives the RNC back to the initial
 * state.  If the state machine changes then this routine will also have to be
 * changed. bool true if the state machine is not in the initial state false if
 * the state machine is in the initial state
 */

/**
 *
 * @sci_rnc: The state of the remote node context object to check.
 *
 * This method will return true if the remote node context is in a READY state
 * otherwise it will return false bool true if the remote node context is in
 * the ready state. false if the remote node context is not in the ready state.
 */
bool scic_sds_remote_node_context_is_ready(
	struct scic_sds_remote_node_context *sci_rnc)
{
	u32 current_state = sci_base_state_machine_get_state(&sci_rnc->state_machine);

	if (current_state == SCIC_SDS_REMOTE_NODE_CONTEXT_READY_STATE) {
		return true;
	}

	return false;
}

/**
 *
 * @sci_dev: The remote device to use to construct the RNC buffer.
 * @rnc: The buffer into which the remote device data will be copied.
 *
 * This method will construct the RNC buffer for this remote device object. none
 */
static void scic_sds_remote_node_context_construct_buffer(
	struct scic_sds_remote_node_context *sci_rnc)
{
	struct scic_sds_remote_device *sci_dev = rnc_to_dev(sci_rnc);
	struct domain_device *dev = sci_dev_to_domain(sci_dev);
	int rni = sci_rnc->remote_node_index;
	union scu_remote_node_context *rnc;
	struct scic_sds_controller *scic;
	__le64 sas_addr;

	scic = scic_sds_remote_device_get_controller(sci_dev);
	rnc = scic_sds_controller_get_remote_node_context_buffer(scic, rni);

	memset(rnc, 0, sizeof(union scu_remote_node_context)
		* scic_sds_remote_device_node_count(sci_dev));

	rnc->ssp.remote_node_index = rni;
	rnc->ssp.remote_node_port_width = sci_dev->device_port_width;
	rnc->ssp.logical_port_index = sci_dev->owning_port->physical_port_index;

	/* sas address is __be64, context ram format is __le64 */
	sas_addr = cpu_to_le64(SAS_ADDR(dev->sas_addr));
	rnc->ssp.remote_sas_address_hi = upper_32_bits(sas_addr);
	rnc->ssp.remote_sas_address_lo = lower_32_bits(sas_addr);

	rnc->ssp.nexus_loss_timer_enable = true;
	rnc->ssp.check_bit               = false;
	rnc->ssp.is_valid                = false;
	rnc->ssp.is_remote_node_context  = true;
	rnc->ssp.function_number         = 0;

	rnc->ssp.arbitration_wait_time = 0;

	if (dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP)) {
		rnc->ssp.connection_occupancy_timeout =
			scic->user_parameters.sds1.stp_max_occupancy_timeout;
		rnc->ssp.connection_inactivity_timeout =
			scic->user_parameters.sds1.stp_inactivity_timeout;
	} else {
		rnc->ssp.connection_occupancy_timeout  =
			scic->user_parameters.sds1.ssp_max_occupancy_timeout;
		rnc->ssp.connection_inactivity_timeout =
			scic->user_parameters.sds1.ssp_inactivity_timeout;
	}

	rnc->ssp.initial_arbitration_wait_time = 0;

	/* Open Address Frame Parameters */
	rnc->ssp.oaf_connection_rate = sci_dev->connection_rate;
	rnc->ssp.oaf_features = 0;
	rnc->ssp.oaf_source_zone_group = 0;
	rnc->ssp.oaf_more_compatibility_features = 0;
}

/**
 *
 * @sci_rnc:
 * @callback:
 * @callback_parameter:
 *
 * This method will setup the remote node context object so it will transition
 * to its ready state.  If the remote node context is already setup to
 * transition to its final state then this function does nothing. none
 */
static void scic_sds_remote_node_context_setup_to_resume(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	if (sci_rnc->destination_state != SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_FINAL) {
		sci_rnc->destination_state = SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_READY;
		sci_rnc->user_callback     = callback;
		sci_rnc->user_cookie       = callback_parameter;
	}
}

/**
 *
 * @sci_rnc:
 * @callback:
 * @callback_parameter:
 *
 * This method will setup the remote node context object so it will transistion
 * to its final state. none
 */
static void scic_sds_remote_node_context_setup_to_destory(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	sci_rnc->destination_state = SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_FINAL;
	sci_rnc->user_callback     = callback;
	sci_rnc->user_cookie       = callback_parameter;
}

/**
 *
 * @sci_rnc:
 * @callback:
 *
 * This method will continue to resume a remote node context.  This is used in
 * the states where a resume is requested while a resume is in progress.
 */
static enum sci_status scic_sds_remote_node_context_continue_to_resume_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	if (sci_rnc->destination_state == SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_READY) {
		sci_rnc->user_callback = callback;
		sci_rnc->user_cookie   = callback_parameter;

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INVALID_STATE;
}

/* --------------------------------------------------------------------------- */

static enum sci_status scic_sds_remote_node_context_default_destruct_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
		 "%s: SCIC Remote Node Context 0x%p requested to stop while "
		 "in unexpected state %d\n",
		 __func__,
		 sci_rnc,
		 sci_base_state_machine_get_state(&sci_rnc->state_machine));

	/*
	 * We have decided that the destruct request on the remote node context can not fail
	 * since it is either in the initial/destroyed state or is can be destroyed. */
	return SCI_SUCCESS;
}

static enum sci_status scic_sds_remote_node_context_default_suspend_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	u32 suspend_type,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
		 "%s: SCIC Remote Node Context 0x%p requested to suspend "
		 "while in wrong state %d\n",
		 __func__,
		 sci_rnc,
		 sci_base_state_machine_get_state(&sci_rnc->state_machine));

	return SCI_FAILURE_INVALID_STATE;
}

static enum sci_status scic_sds_remote_node_context_default_resume_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
		 "%s: SCIC Remote Node Context 0x%p requested to resume "
		 "while in wrong state %d\n",
		 __func__,
		 sci_rnc,
		 sci_base_state_machine_get_state(&sci_rnc->state_machine));

	return SCI_FAILURE_INVALID_STATE;
}

static enum sci_status scic_sds_remote_node_context_default_start_io_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	struct scic_sds_request *sci_req)
{
	dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
		 "%s: SCIC Remote Node Context 0x%p requested to start io "
		 "0x%p while in wrong state %d\n",
		 __func__,
		 sci_rnc,
		 sci_req,
		 sci_base_state_machine_get_state(&sci_rnc->state_machine));

	return SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED;
}

static enum sci_status scic_sds_remote_node_context_default_start_task_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	struct scic_sds_request *sci_req)
{
	dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
		 "%s: SCIC Remote Node Context 0x%p requested to start "
		 "task 0x%p while in wrong state %d\n",
		 __func__,
		 sci_rnc,
		 sci_req,
		 sci_base_state_machine_get_state(&sci_rnc->state_machine));

	return SCI_FAILURE;
}

static enum sci_status scic_sds_remote_node_context_default_event_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	u32 event_code)
{
	dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
		 "%s: SCIC Remote Node Context 0x%p requested to process "
		 "event 0x%x while in wrong state %d\n",
		 __func__,
		 sci_rnc,
		 event_code,
		 sci_base_state_machine_get_state(&sci_rnc->state_machine));

	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @sci_rnc: The rnc for which the task request is targeted.
 * @sci_req: The request which is going to be started.
 *
 * This method determines if the task request can be started by the SCU
 * hardware. When the RNC is in the ready state any task can be started.
 * enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_remote_node_context_success_start_task_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	struct scic_sds_request *sci_req)
{
	return SCI_SUCCESS;
}

/**
 *
 * @sci_rnc:
 * @callback:
 * @callback_parameter:
 *
 * This method handles destruct calls from the various state handlers.  The
 * remote node context can be requested to destroy from any state. If there was
 * a user callback it is always replaced with the request to destroy user
 * callback. enum sci_status
 */
static enum sci_status scic_sds_remote_node_context_general_destruct_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	scic_sds_remote_node_context_setup_to_destory(
		sci_rnc, callback, callback_parameter
		);

	sci_base_state_machine_change_state(
		&sci_rnc->state_machine,
		SCIC_SDS_REMOTE_NODE_CONTEXT_INVALIDATING_STATE
		);

	return SCI_SUCCESS;
}

/* --------------------------------------------------------------------------- */

static enum sci_status scic_sds_remote_node_context_initial_state_resume_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	if (sci_rnc->remote_node_index != SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX) {
		scic_sds_remote_node_context_setup_to_resume(
			sci_rnc, callback, callback_parameter
			);

		scic_sds_remote_node_context_construct_buffer(sci_rnc);

		sci_base_state_machine_change_state(
			&sci_rnc->state_machine,
			SCIC_SDS_REMOTE_NODE_CONTEXT_POSTING_STATE
			);

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INVALID_STATE;
}

/* --------------------------------------------------------------------------- */

static enum sci_status scic_sds_remote_node_context_posting_state_event_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	u32 event_code)
{
	enum sci_status status;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_POST_RNC_COMPLETE:
		status = SCI_SUCCESS;

		sci_base_state_machine_change_state(
			&sci_rnc->state_machine,
			SCIC_SDS_REMOTE_NODE_CONTEXT_READY_STATE
			);
		break;

	default:
		status = SCI_FAILURE;
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			 "%s: SCIC Remote Node Context 0x%p requested to "
			 "process unexpected event 0x%x while in posting "
			 "state\n",
			 __func__,
			 sci_rnc,
			 event_code);
		break;
	}

	return status;
}

/* --------------------------------------------------------------------------- */

static enum sci_status scic_sds_remote_node_context_invalidating_state_destruct_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	scic_sds_remote_node_context_setup_to_destory(
		sci_rnc, callback, callback_parameter
		);

	return SCI_SUCCESS;
}

static enum sci_status scic_sds_remote_node_context_invalidating_state_event_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	u32 event_code)
{
	enum sci_status status;

	if (scu_get_event_code(event_code) == SCU_EVENT_POST_RNC_INVALIDATE_COMPLETE) {
		status = SCI_SUCCESS;

		if (sci_rnc->destination_state == SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_FINAL) {
			sci_base_state_machine_change_state(
				&sci_rnc->state_machine,
				SCIC_SDS_REMOTE_NODE_CONTEXT_INITIAL_STATE
				);
		} else {
			sci_base_state_machine_change_state(
				&sci_rnc->state_machine,
				SCIC_SDS_REMOTE_NODE_CONTEXT_POSTING_STATE
				);
		}
	} else {
		switch (scu_get_event_type(event_code)) {
		case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
		case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
			/*
			 * We really dont care if the hardware is going to suspend
			 * the device since it's being invalidated anyway */
			dev_dbg(scirdev_to_dev(rnc_to_dev(sci_rnc)),
				"%s: SCIC Remote Node Context 0x%p was "
				"suspeneded by hardware while being "
				"invalidated.\n",
				__func__,
				sci_rnc);
			status = SCI_SUCCESS;
			break;

		default:
			dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
				 "%s: SCIC Remote Node Context 0x%p "
				 "requested to process event 0x%x while "
				 "in state %d.\n",
				 __func__,
				 sci_rnc,
				 event_code,
				 sci_base_state_machine_get_state(
					 &sci_rnc->state_machine));
			status = SCI_FAILURE;
			break;
		}
	}

	return status;
}

/* --------------------------------------------------------------------------- */


static enum sci_status scic_sds_remote_node_context_resuming_state_event_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	u32 event_code)
{
	enum sci_status status;

	if (scu_get_event_code(event_code) == SCU_EVENT_POST_RCN_RELEASE) {
		status = SCI_SUCCESS;

		sci_base_state_machine_change_state(
			&sci_rnc->state_machine,
			SCIC_SDS_REMOTE_NODE_CONTEXT_READY_STATE
			);
	} else {
		switch (scu_get_event_type(event_code)) {
		case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
		case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
			/*
			 * We really dont care if the hardware is going to suspend
			 * the device since it's being resumed anyway */
			dev_dbg(scirdev_to_dev(rnc_to_dev(sci_rnc)),
				"%s: SCIC Remote Node Context 0x%p was "
				"suspeneded by hardware while being resumed.\n",
				__func__,
				sci_rnc);
			status = SCI_SUCCESS;
			break;

		default:
			dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
				 "%s: SCIC Remote Node Context 0x%p requested "
				 "to process event 0x%x while in state %d.\n",
				 __func__,
				 sci_rnc,
				 event_code,
				 sci_base_state_machine_get_state(
					 &sci_rnc->state_machine));
			status = SCI_FAILURE;
			break;
		}
	}

	return status;
}

/* --------------------------------------------------------------------------- */

/**
 *
 * @sci_rnc: The remote node context object being suspended.
 * @callback: The callback when the suspension is complete.
 * @callback_parameter: The parameter that is to be passed into the callback.
 *
 * This method will handle the suspend requests from the ready state.
 * SCI_SUCCESS
 */
static enum sci_status scic_sds_remote_node_context_ready_state_suspend_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	u32 suspend_type,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	sci_rnc->user_callback   = callback;
	sci_rnc->user_cookie     = callback_parameter;
	sci_rnc->suspension_code = suspend_type;

	if (suspend_type == SCI_SOFTWARE_SUSPENSION) {
		scic_sds_remote_device_post_request(rnc_to_dev(sci_rnc),
						    SCU_CONTEXT_COMMAND_POST_RNC_SUSPEND_TX);
	}

	sci_base_state_machine_change_state(
		&sci_rnc->state_machine,
		SCIC_SDS_REMOTE_NODE_CONTEXT_AWAIT_SUSPENSION_STATE
		);

	return SCI_SUCCESS;
}

/**
 *
 * @sci_rnc: The rnc for which the io request is targeted.
 * @sci_req: The request which is going to be started.
 *
 * This method determines if the io request can be started by the SCU hardware.
 * When the RNC is in the ready state any io request can be started. enum sci_status
 * SCI_SUCCESS
 */
static enum sci_status scic_sds_remote_node_context_ready_state_start_io_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	struct scic_sds_request *sci_req)
{
	return SCI_SUCCESS;
}


static enum sci_status scic_sds_remote_node_context_ready_state_event_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	u32 event_code)
{
	enum sci_status status;

	switch (scu_get_event_type(event_code)) {
	case SCU_EVENT_TL_RNC_SUSPEND_TX:
		sci_base_state_machine_change_state(
			&sci_rnc->state_machine,
			SCIC_SDS_REMOTE_NODE_CONTEXT_TX_SUSPENDED_STATE
			);

		sci_rnc->suspension_code = scu_get_event_specifier(event_code);
		status = SCI_SUCCESS;
		break;

	case SCU_EVENT_TL_RNC_SUSPEND_TX_RX:
		sci_base_state_machine_change_state(
			&sci_rnc->state_machine,
			SCIC_SDS_REMOTE_NODE_CONTEXT_TX_RX_SUSPENDED_STATE
			);

		sci_rnc->suspension_code = scu_get_event_specifier(event_code);
		status = SCI_SUCCESS;
		break;

	default:
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			"%s: SCIC Remote Node Context 0x%p requested to "
			"process event 0x%x while in state %d.\n",
			__func__,
			sci_rnc,
			event_code,
			sci_base_state_machine_get_state(
				&sci_rnc->state_machine));

		status = SCI_FAILURE;
		break;
	}

	return status;
}

/* --------------------------------------------------------------------------- */

static enum sci_status scic_sds_remote_node_context_tx_suspended_state_resume_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	struct scic_sds_remote_device *sci_dev = rnc_to_dev(sci_rnc);
	struct domain_device *dev = sci_dev_to_domain(sci_dev);
	enum sci_status status = SCI_SUCCESS;

	scic_sds_remote_node_context_setup_to_resume(sci_rnc, callback,
						     callback_parameter);

	/* TODO: consider adding a resume action of NONE, INVALIDATE, WRITE_TLCR */
	if (dev->dev_type == SAS_END_DEV || dev_is_expander(dev))
		sci_base_state_machine_change_state(&sci_rnc->state_machine,
						    SCIC_SDS_REMOTE_NODE_CONTEXT_RESUMING_STATE);
	else if (dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP)) {
		if (sci_dev->is_direct_attached) {
			/* @todo Fix this since I am being silly in writing to the STPTLDARNI register. */
			sci_base_state_machine_change_state(
				&sci_rnc->state_machine,
				SCIC_SDS_REMOTE_NODE_CONTEXT_RESUMING_STATE);
		} else {
			sci_base_state_machine_change_state(
				&sci_rnc->state_machine,
				SCIC_SDS_REMOTE_NODE_CONTEXT_INVALIDATING_STATE);
		}
	} else
		status = SCI_FAILURE;

	return status;
}

/**
 *
 * @sci_rnc: The remote node context which is to receive the task request.
 * @sci_req: The task request to be transmitted to to the remote target
 *    device.
 *
 * This method will report a success or failure attempt to start a new task
 * request to the hardware.  Since all task requests are sent on the high
 * priority queue they can be sent when the RCN is in a TX suspend state.
 * enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_remote_node_context_suspended_start_task_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	struct scic_sds_request *sci_req)
{
	scic_sds_remote_node_context_resume(sci_rnc, NULL, NULL);

	return SCI_SUCCESS;
}

/* --------------------------------------------------------------------------- */

static enum sci_status scic_sds_remote_node_context_tx_rx_suspended_state_resume_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	scic_sds_remote_node_context_setup_to_resume(
		sci_rnc, callback, callback_parameter
		);

	sci_base_state_machine_change_state(
		&sci_rnc->state_machine,
		SCIC_SDS_REMOTE_NODE_CONTEXT_RESUMING_STATE
		);

	return SCI_FAILURE_INVALID_STATE;
}

/* --------------------------------------------------------------------------- */

/**
 *
 *
 *
 */
static enum sci_status scic_sds_remote_node_context_await_suspension_state_resume_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	scic_sds_remote_node_context_setup_to_resume(
		sci_rnc, callback, callback_parameter
		);

	return SCI_SUCCESS;
}

/**
 *
 * @sci_rnc: The remote node context which is to receive the task request.
 * @sci_req: The task request to be transmitted to to the remote target
 *    device.
 *
 * This method will report a success or failure attempt to start a new task
 * request to the hardware.  Since all task requests are sent on the high
 * priority queue they can be sent when the RCN is in a TX suspend state.
 * enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_remote_node_context_await_suspension_state_start_task_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	struct scic_sds_request *sci_req)
{
	return SCI_SUCCESS;
}

static enum sci_status scic_sds_remote_node_context_await_suspension_state_event_handler(
	struct scic_sds_remote_node_context *sci_rnc,
	u32 event_code)
{
	enum sci_status status;

	switch (scu_get_event_type(event_code)) {
	case SCU_EVENT_TL_RNC_SUSPEND_TX:
		sci_base_state_machine_change_state(
			&sci_rnc->state_machine,
			SCIC_SDS_REMOTE_NODE_CONTEXT_TX_SUSPENDED_STATE
			);

		sci_rnc->suspension_code = scu_get_event_specifier(event_code);
		status = SCI_SUCCESS;
		break;

	case SCU_EVENT_TL_RNC_SUSPEND_TX_RX:
		sci_base_state_machine_change_state(
			&sci_rnc->state_machine,
			SCIC_SDS_REMOTE_NODE_CONTEXT_TX_RX_SUSPENDED_STATE
			);

		sci_rnc->suspension_code = scu_get_event_specifier(event_code);
		status = SCI_SUCCESS;
		break;

	default:
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			 "%s: SCIC Remote Node Context 0x%p requested to "
			 "process event 0x%x while in state %d.\n",
			 __func__,
			 sci_rnc,
			 event_code,
			 sci_base_state_machine_get_state(
				 &sci_rnc->state_machine));

		status = SCI_FAILURE;
		break;
	}

	return status;
}

/* --------------------------------------------------------------------------- */

static struct scic_sds_remote_node_context_handlers
scic_sds_remote_node_context_state_handler_table[] = {
	[SCIC_SDS_REMOTE_NODE_CONTEXT_INITIAL_STATE] = {
		.destruct_handler	= scic_sds_remote_node_context_default_destruct_handler,
		.suspend_handler	= scic_sds_remote_node_context_default_suspend_handler,
		.resume_handler		= scic_sds_remote_node_context_initial_state_resume_handler,
		.start_io_handler	= scic_sds_remote_node_context_default_start_io_handler,
		.start_task_handler	= scic_sds_remote_node_context_default_start_task_handler,
		.event_handler		= scic_sds_remote_node_context_default_event_handler
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_POSTING_STATE] = {
		.destruct_handler	= scic_sds_remote_node_context_general_destruct_handler,
		.suspend_handler	= scic_sds_remote_node_context_default_suspend_handler,
		.resume_handler		= scic_sds_remote_node_context_continue_to_resume_handler,
		.start_io_handler	= scic_sds_remote_node_context_default_start_io_handler,
		.start_task_handler	= scic_sds_remote_node_context_default_start_task_handler,
		.event_handler		= scic_sds_remote_node_context_posting_state_event_handler
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_INVALIDATING_STATE] = {
		.destruct_handler	= scic_sds_remote_node_context_invalidating_state_destruct_handler,
		.suspend_handler	= scic_sds_remote_node_context_default_suspend_handler,
		.resume_handler		= scic_sds_remote_node_context_continue_to_resume_handler,
		.start_io_handler	= scic_sds_remote_node_context_default_start_io_handler,
		.start_task_handler	= scic_sds_remote_node_context_default_start_task_handler,
		.event_handler		= scic_sds_remote_node_context_invalidating_state_event_handler
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_RESUMING_STATE] = {
		.destruct_handler	= scic_sds_remote_node_context_general_destruct_handler,
		.suspend_handler	= scic_sds_remote_node_context_default_suspend_handler,
		.resume_handler		= scic_sds_remote_node_context_continue_to_resume_handler,
		.start_io_handler	= scic_sds_remote_node_context_default_start_io_handler,
		.start_task_handler	= scic_sds_remote_node_context_success_start_task_handler,
		.event_handler		= scic_sds_remote_node_context_resuming_state_event_handler
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_READY_STATE] = {
		.destruct_handler	= scic_sds_remote_node_context_general_destruct_handler,
		.suspend_handler	= scic_sds_remote_node_context_ready_state_suspend_handler,
		.resume_handler		= scic_sds_remote_node_context_default_resume_handler,
		.start_io_handler	= scic_sds_remote_node_context_ready_state_start_io_handler,
		.start_task_handler	= scic_sds_remote_node_context_success_start_task_handler,
		.event_handler		= scic_sds_remote_node_context_ready_state_event_handler
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_TX_SUSPENDED_STATE] = {
		.destruct_handler	= scic_sds_remote_node_context_general_destruct_handler,
		.suspend_handler	= scic_sds_remote_node_context_default_suspend_handler,
		.resume_handler		= scic_sds_remote_node_context_tx_suspended_state_resume_handler,
		.start_io_handler	= scic_sds_remote_node_context_default_start_io_handler,
		.start_task_handler	= scic_sds_remote_node_context_suspended_start_task_handler,
		.event_handler		= scic_sds_remote_node_context_default_event_handler
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_TX_RX_SUSPENDED_STATE] = {
		.destruct_handler	= scic_sds_remote_node_context_general_destruct_handler,
		.suspend_handler	= scic_sds_remote_node_context_default_suspend_handler,
		.resume_handler		= scic_sds_remote_node_context_tx_rx_suspended_state_resume_handler,
		.start_io_handler	= scic_sds_remote_node_context_default_start_io_handler,
		.start_task_handler	= scic_sds_remote_node_context_suspended_start_task_handler,
		.event_handler		= scic_sds_remote_node_context_default_event_handler
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_AWAIT_SUSPENSION_STATE] = {
		.destruct_handler	= scic_sds_remote_node_context_general_destruct_handler,
		.suspend_handler	= scic_sds_remote_node_context_default_suspend_handler,
		.resume_handler		= scic_sds_remote_node_context_await_suspension_state_resume_handler,
		.start_io_handler	= scic_sds_remote_node_context_default_start_io_handler,
		.start_task_handler	= scic_sds_remote_node_context_await_suspension_state_start_task_handler,
		.event_handler		= scic_sds_remote_node_context_await_suspension_state_event_handler
	}
};

/*
 * *****************************************************************************
 * * REMOTE NODE CONTEXT PRIVATE METHODS
 * ***************************************************************************** */

/**
 *
 *
 * This method just calls the user callback function and then resets the
 * callback.
 */
static void scic_sds_remote_node_context_notify_user(
	struct scic_sds_remote_node_context *rnc)
{
	if (rnc->user_callback != NULL) {
		(*rnc->user_callback)(rnc->user_cookie);

		rnc->user_callback = NULL;
		rnc->user_cookie = NULL;
	}
}

/**
 *
 *
 * This method will continue the remote node context state machine by
 * requesting to resume the remote node context state machine from its current
 * state.
 */
static void scic_sds_remote_node_context_continue_state_transitions(
	struct scic_sds_remote_node_context *rnc)
{
	if (rnc->destination_state == SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_READY) {
		rnc->state_handlers->resume_handler(
			rnc, rnc->user_callback, rnc->user_cookie
			);
	}
}

/**
 *
 * @sci_rnc: The remote node context object that is to be validated.
 *
 * This method will mark the rnc buffer as being valid and post the request to
 * the hardware. none
 */
static void scic_sds_remote_node_context_validate_context_buffer(
	struct scic_sds_remote_node_context *sci_rnc)
{
	struct scic_sds_remote_device *sci_dev = rnc_to_dev(sci_rnc);
	struct domain_device *dev = sci_dev_to_domain(sci_dev);
	union scu_remote_node_context *rnc_buffer;

	rnc_buffer = scic_sds_controller_get_remote_node_context_buffer(
		scic_sds_remote_device_get_controller(sci_dev),
		sci_rnc->remote_node_index
		);

	rnc_buffer->ssp.is_valid = true;

	if (!sci_dev->is_direct_attached &&
	    (dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP))) {
		scic_sds_remote_device_post_request(sci_dev,
						    SCU_CONTEXT_COMMAND_POST_RNC_96);
	} else {
		scic_sds_remote_device_post_request(sci_dev, SCU_CONTEXT_COMMAND_POST_RNC_32);

		if (sci_dev->is_direct_attached) {
			scic_sds_port_setup_transports(sci_dev->owning_port,
						       sci_rnc->remote_node_index);
		}
	}
}

/**
 *
 * @sci_rnc: The remote node context object that is to be invalidated.
 *
 * This method will update the RNC buffer and post the invalidate request. none
 */
static void scic_sds_remote_node_context_invalidate_context_buffer(
	struct scic_sds_remote_node_context *sci_rnc)
{
	union scu_remote_node_context *rnc_buffer;

	rnc_buffer = scic_sds_controller_get_remote_node_context_buffer(
		scic_sds_remote_device_get_controller(rnc_to_dev(sci_rnc)),
		sci_rnc->remote_node_index);

	rnc_buffer->ssp.is_valid = false;

	scic_sds_remote_device_post_request(rnc_to_dev(sci_rnc),
					    SCU_CONTEXT_COMMAND_POST_RNC_INVALIDATE);
}

/*
 * *****************************************************************************
 * * REMOTE NODE CONTEXT STATE ENTER AND EXIT METHODS
 * ***************************************************************************** */

/**
 *
 *
 *
 */
static void scic_sds_remote_node_context_initial_state_enter(void *object)
{
	struct scic_sds_remote_node_context *rnc = object;

	SET_STATE_HANDLER(
		rnc,
		scic_sds_remote_node_context_state_handler_table,
		SCIC_SDS_REMOTE_NODE_CONTEXT_INITIAL_STATE
		);

	/*
	 * Check to see if we have gotten back to the initial state because someone
	 * requested to destroy the remote node context object. */
	if (
		rnc->state_machine.previous_state_id
		== SCIC_SDS_REMOTE_NODE_CONTEXT_INVALIDATING_STATE
		) {
		rnc->destination_state = SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_UNSPECIFIED;

		scic_sds_remote_node_context_notify_user(rnc);
	}
}

/**
 *
 *
 *
 */
static void scic_sds_remote_node_context_posting_state_enter(void *object)
{
	struct scic_sds_remote_node_context *sci_rnc = object;

	SET_STATE_HANDLER(
		sci_rnc,
		scic_sds_remote_node_context_state_handler_table,
		SCIC_SDS_REMOTE_NODE_CONTEXT_POSTING_STATE
		);

	scic_sds_remote_node_context_validate_context_buffer(sci_rnc);
}

/**
 *
 *
 *
 */
static void scic_sds_remote_node_context_invalidating_state_enter(void *object)
{
	struct scic_sds_remote_node_context *rnc = object;

	SET_STATE_HANDLER(
		rnc,
		scic_sds_remote_node_context_state_handler_table,
		SCIC_SDS_REMOTE_NODE_CONTEXT_INVALIDATING_STATE
		);

	scic_sds_remote_node_context_invalidate_context_buffer(rnc);
}

/**
 *
 *
 *
 */
static void scic_sds_remote_node_context_resuming_state_enter(void *object)
{
	struct scic_sds_remote_node_context *rnc = object;
	struct scic_sds_remote_device *sci_dev;
	struct domain_device *dev;

	sci_dev = rnc_to_dev(rnc);
	dev = sci_dev_to_domain(sci_dev);

	SET_STATE_HANDLER(
		rnc,
		scic_sds_remote_node_context_state_handler_table,
		SCIC_SDS_REMOTE_NODE_CONTEXT_RESUMING_STATE
		);

	/*
	 * For direct attached SATA devices we need to clear the TLCR
	 * NCQ to TCi tag mapping on the phy and in cases where we
	 * resume because of a target reset we also need to update
	 * the STPTLDARNI register with the RNi of the device
	 */
	if ((dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP)) &&
	    sci_dev->is_direct_attached)
		scic_sds_port_setup_transports(sci_dev->owning_port,
					       rnc->remote_node_index);

	scic_sds_remote_device_post_request(sci_dev, SCU_CONTEXT_COMMAND_POST_RNC_RESUME);
}

/**
 *
 *
 *
 */
static void scic_sds_remote_node_context_ready_state_enter(void *object)
{
	struct scic_sds_remote_node_context *rnc = object;

	SET_STATE_HANDLER(
		rnc,
		scic_sds_remote_node_context_state_handler_table,
		SCIC_SDS_REMOTE_NODE_CONTEXT_READY_STATE
		);

	rnc->destination_state = SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_UNSPECIFIED;

	if (rnc->user_callback != NULL) {
		scic_sds_remote_node_context_notify_user(rnc);
	}
}

/**
 *
 *
 *
 */
static void scic_sds_remote_node_context_tx_suspended_state_enter(void *object)
{
	struct scic_sds_remote_node_context *rnc = object;

	SET_STATE_HANDLER(
		rnc,
		scic_sds_remote_node_context_state_handler_table,
		SCIC_SDS_REMOTE_NODE_CONTEXT_TX_SUSPENDED_STATE
		);

	scic_sds_remote_node_context_continue_state_transitions(rnc);
}

/**
 *
 *
 *
 */
static void scic_sds_remote_node_context_tx_rx_suspended_state_enter(
		void *object)
{
	struct scic_sds_remote_node_context *rnc = object;

	SET_STATE_HANDLER(
		rnc,
		scic_sds_remote_node_context_state_handler_table,
		SCIC_SDS_REMOTE_NODE_CONTEXT_TX_RX_SUSPENDED_STATE
		);

	scic_sds_remote_node_context_continue_state_transitions(rnc);
}

/**
 *
 *
 *
 */
static void scic_sds_remote_node_context_await_suspension_state_enter(
	void *object)
{
	struct scic_sds_remote_node_context *rnc = object;

	SET_STATE_HANDLER(
		rnc,
		scic_sds_remote_node_context_state_handler_table,
		SCIC_SDS_REMOTE_NODE_CONTEXT_AWAIT_SUSPENSION_STATE
		);
}

/* --------------------------------------------------------------------------- */

static const struct sci_base_state scic_sds_remote_node_context_state_table[] = {
	[SCIC_SDS_REMOTE_NODE_CONTEXT_INITIAL_STATE] = {
		.enter_state = scic_sds_remote_node_context_initial_state_enter,
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_POSTING_STATE] = {
		.enter_state = scic_sds_remote_node_context_posting_state_enter,
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_INVALIDATING_STATE] = {
		.enter_state = scic_sds_remote_node_context_invalidating_state_enter,
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_RESUMING_STATE] = {
		.enter_state = scic_sds_remote_node_context_resuming_state_enter,
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_READY_STATE] = {
		.enter_state = scic_sds_remote_node_context_ready_state_enter,
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_TX_SUSPENDED_STATE] = {
		.enter_state = scic_sds_remote_node_context_tx_suspended_state_enter,
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_TX_RX_SUSPENDED_STATE] = {
		.enter_state = scic_sds_remote_node_context_tx_rx_suspended_state_enter,
	},
	[SCIC_SDS_REMOTE_NODE_CONTEXT_AWAIT_SUSPENSION_STATE] = {
		.enter_state = scic_sds_remote_node_context_await_suspension_state_enter,
	},
};

void scic_sds_remote_node_context_construct(struct scic_sds_remote_node_context *rnc,
					    u16 remote_node_index)
{
	memset(rnc, 0, sizeof(struct scic_sds_remote_node_context));

	rnc->remote_node_index = remote_node_index;
	rnc->destination_state = SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_UNSPECIFIED;

	sci_base_state_machine_construct(
		&rnc->state_machine,
		rnc,
		scic_sds_remote_node_context_state_table,
		SCIC_SDS_REMOTE_NODE_CONTEXT_INITIAL_STATE
		);

	sci_base_state_machine_start(&rnc->state_machine);
}
