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
#include <scsi/sas_ata.h>
#include "host.h"
#include "isci.h"
#include "remote_device.h"
#include "remote_node_context.h"
#include "scu_event_codes.h"
#include "scu_task_context.h"

#undef C
#define C(a) (#a)
const char *rnc_state_name(enum scis_sds_remote_node_context_states state)
{
	static const char * const strings[] = RNC_STATES;

	return strings[state];
}
#undef C

/**
 *
 * @sci_rnc: The state of the remote node context object to check.
 *
 * This method will return true if the remote node context is in a READY state
 * otherwise it will return false bool true if the remote node context is in
 * the ready state. false if the remote node context is not in the ready state.
 */
bool sci_remote_node_context_is_ready(
	struct sci_remote_node_context *sci_rnc)
{
	u32 current_state = sci_rnc->sm.current_state_id;

	if (current_state == SCI_RNC_READY) {
		return true;
	}

	return false;
}

bool sci_remote_node_context_is_suspended(struct sci_remote_node_context *sci_rnc)
{
	u32 current_state = sci_rnc->sm.current_state_id;

	if (current_state == SCI_RNC_TX_RX_SUSPENDED)
		return true;
	return false;
}

static union scu_remote_node_context *sci_rnc_by_id(struct isci_host *ihost, u16 id)
{
	if (id < ihost->remote_node_entries &&
	    ihost->device_table[id])
		return &ihost->remote_node_context_table[id];

	return NULL;
}

static void sci_remote_node_context_construct_buffer(struct sci_remote_node_context *sci_rnc)
{
	struct isci_remote_device *idev = rnc_to_dev(sci_rnc);
	struct domain_device *dev = idev->domain_dev;
	int rni = sci_rnc->remote_node_index;
	union scu_remote_node_context *rnc;
	struct isci_host *ihost;
	__le64 sas_addr;

	ihost = idev->owning_port->owning_controller;
	rnc = sci_rnc_by_id(ihost, rni);

	memset(rnc, 0, sizeof(union scu_remote_node_context)
		* sci_remote_device_node_count(idev));

	rnc->ssp.remote_node_index = rni;
	rnc->ssp.remote_node_port_width = idev->device_port_width;
	rnc->ssp.logical_port_index = idev->owning_port->physical_port_index;

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

	if (dev_is_sata(dev)) {
		rnc->ssp.connection_occupancy_timeout =
			ihost->user_parameters.stp_max_occupancy_timeout;
		rnc->ssp.connection_inactivity_timeout =
			ihost->user_parameters.stp_inactivity_timeout;
	} else {
		rnc->ssp.connection_occupancy_timeout  =
			ihost->user_parameters.ssp_max_occupancy_timeout;
		rnc->ssp.connection_inactivity_timeout =
			ihost->user_parameters.ssp_inactivity_timeout;
	}

	rnc->ssp.initial_arbitration_wait_time = 0;

	/* Open Address Frame Parameters */
	rnc->ssp.oaf_connection_rate = idev->connection_rate;
	rnc->ssp.oaf_features = 0;
	rnc->ssp.oaf_source_zone_group = 0;
	rnc->ssp.oaf_more_compatibility_features = 0;
}

static void sci_remote_node_context_save_cbparams(
	struct sci_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	sci_rnc->user_callback = callback;
	sci_rnc->user_cookie   = callback_parameter;
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
static void sci_remote_node_context_setup_to_resume(
	struct sci_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter,
	enum sci_remote_node_context_destination_state dest_param)
{
	if (sci_rnc->destination_state != RNC_DEST_FINAL) {
		sci_rnc->destination_state = dest_param;
		if (callback != NULL)
			sci_remote_node_context_save_cbparams(
				sci_rnc, callback, callback_parameter);
	}
}

static void sci_remote_node_context_setup_to_destroy(
	struct sci_remote_node_context *sci_rnc,
	scics_sds_remote_node_context_callback callback,
	void *callback_parameter)
{
	sci_rnc->destination_state = RNC_DEST_FINAL;
	sci_rnc->user_callback     = callback;
	sci_rnc->user_cookie       = callback_parameter;
}

/**
 *
 *
 * This method just calls the user callback function and then resets the
 * callback.
 */
static void sci_remote_node_context_notify_user(
	struct sci_remote_node_context *rnc)
{
	if (rnc->user_callback != NULL) {
		(*rnc->user_callback)(rnc->user_cookie);

		rnc->user_callback = NULL;
		rnc->user_cookie = NULL;
	}
}

static void sci_remote_node_context_continue_state_transitions(struct sci_remote_node_context *rnc)
{
	if ((rnc->destination_state == RNC_DEST_READY) ||
	    (rnc->destination_state == RNC_DEST_SUSPENDED_RESUME)) {
		rnc->destination_state = RNC_DEST_READY;
		sci_remote_node_context_resume(rnc, rnc->user_callback,
						    rnc->user_cookie);
	} else
		rnc->destination_state = RNC_DEST_UNSPECIFIED;
}

static void sci_remote_node_context_validate_context_buffer(struct sci_remote_node_context *sci_rnc)
{
	union scu_remote_node_context *rnc_buffer;
	struct isci_remote_device *idev = rnc_to_dev(sci_rnc);
	struct domain_device *dev = idev->domain_dev;
	struct isci_host *ihost = idev->owning_port->owning_controller;

	rnc_buffer = sci_rnc_by_id(ihost, sci_rnc->remote_node_index);

	rnc_buffer->ssp.is_valid = true;

	if (dev_is_sata(dev) && dev->parent) {
		sci_remote_device_post_request(idev, SCU_CONTEXT_COMMAND_POST_RNC_96);
	} else {
		sci_remote_device_post_request(idev, SCU_CONTEXT_COMMAND_POST_RNC_32);

		if (!dev->parent)
			sci_port_setup_transports(idev->owning_port,
						  sci_rnc->remote_node_index);
	}
}

static void sci_remote_node_context_invalidate_context_buffer(struct sci_remote_node_context *sci_rnc)
{
	union scu_remote_node_context *rnc_buffer;
	struct isci_remote_device *idev = rnc_to_dev(sci_rnc);
	struct isci_host *ihost = idev->owning_port->owning_controller;

	rnc_buffer = sci_rnc_by_id(ihost, sci_rnc->remote_node_index);

	rnc_buffer->ssp.is_valid = false;

	sci_remote_device_post_request(rnc_to_dev(sci_rnc),
				       SCU_CONTEXT_COMMAND_POST_RNC_INVALIDATE);
}

static void sci_remote_node_context_initial_state_enter(struct sci_base_state_machine *sm)
{
	struct sci_remote_node_context *rnc = container_of(sm, typeof(*rnc), sm);

	/* Check to see if we have gotten back to the initial state because
	 * someone requested to destroy the remote node context object.
	 */
	if (sm->previous_state_id == SCI_RNC_INVALIDATING) {
		rnc->destination_state = RNC_DEST_UNSPECIFIED;
		sci_remote_node_context_notify_user(rnc);
	}
}

static void sci_remote_node_context_posting_state_enter(struct sci_base_state_machine *sm)
{
	struct sci_remote_node_context *sci_rnc = container_of(sm, typeof(*sci_rnc), sm);

	sci_remote_node_context_validate_context_buffer(sci_rnc);
}

static void sci_remote_node_context_invalidating_state_enter(struct sci_base_state_machine *sm)
{
	struct sci_remote_node_context *rnc = container_of(sm, typeof(*rnc), sm);

	/* Terminate all outstanding requests. */
	sci_remote_device_terminate_requests(rnc_to_dev(rnc));
	sci_remote_node_context_invalidate_context_buffer(rnc);
}

static void sci_remote_node_context_resuming_state_enter(struct sci_base_state_machine *sm)
{
	struct sci_remote_node_context *rnc = container_of(sm, typeof(*rnc), sm);
	struct isci_remote_device *idev;
	struct domain_device *dev;

	idev = rnc_to_dev(rnc);
	dev = idev->domain_dev;

	/*
	 * For direct attached SATA devices we need to clear the TLCR
	 * NCQ to TCi tag mapping on the phy and in cases where we
	 * resume because of a target reset we also need to update
	 * the STPTLDARNI register with the RNi of the device
	 */
	if (dev_is_sata(dev) && !dev->parent)
		sci_port_setup_transports(idev->owning_port, rnc->remote_node_index);

	sci_remote_device_post_request(idev, SCU_CONTEXT_COMMAND_POST_RNC_RESUME);
}

static void sci_remote_node_context_ready_state_enter(struct sci_base_state_machine *sm)
{
	struct sci_remote_node_context *rnc = container_of(sm, typeof(*rnc), sm);
	enum sci_remote_node_context_destination_state dest_select;
	scics_sds_remote_node_context_callback usr_cb = rnc->user_callback;
	void *usr_param = rnc->user_cookie;
	int tell_user = 1;

	dest_select = rnc->destination_state;
	rnc->destination_state = RNC_DEST_UNSPECIFIED;

	if ((dest_select == RNC_DEST_SUSPENDED) ||
	    (dest_select == RNC_DEST_SUSPENDED_RESUME)) {
		sci_remote_node_context_suspend(
			rnc, rnc->suspend_reason,
			SCI_SOFTWARE_SUSPEND_EXPECTED_EVENT);

		if (dest_select == RNC_DEST_SUSPENDED_RESUME) {
			sci_remote_node_context_resume(rnc, usr_cb, usr_param);
			tell_user = 0;  /* Wait until ready again. */
		}
	}
	if (tell_user && rnc->user_callback)
		sci_remote_node_context_notify_user(rnc);
}

static void sci_remote_node_context_tx_suspended_state_enter(struct sci_base_state_machine *sm)
{
	struct sci_remote_node_context *rnc = container_of(sm, typeof(*rnc), sm);

	sci_remote_node_context_continue_state_transitions(rnc);
}

static void sci_remote_node_context_tx_rx_suspended_state_enter(struct sci_base_state_machine *sm)
{
	struct sci_remote_node_context *rnc = container_of(sm, typeof(*rnc), sm);
	struct isci_remote_device *idev = rnc_to_dev(rnc);
	struct isci_host *ihost = idev->owning_port->owning_controller;
	u32 new_count = rnc->suspend_count + 1;

	if (new_count == 0)
		rnc->suspend_count = 1;
	else
		rnc->suspend_count = new_count;
	smp_wmb();

	/* Terminate outstanding requests pending abort. */
	sci_remote_device_abort_requests_pending_abort(idev);

	wake_up(&ihost->eventq);
	sci_remote_node_context_continue_state_transitions(rnc);
}

static void sci_remote_node_context_await_suspend_state_exit(
	struct sci_base_state_machine *sm)
{
	struct sci_remote_node_context *rnc
		= container_of(sm, typeof(*rnc), sm);
	struct isci_remote_device *idev = rnc_to_dev(rnc);

	if (dev_is_sata(idev->domain_dev))
		isci_dev_set_hang_detection_timeout(idev, 0);
}

static const struct sci_base_state sci_remote_node_context_state_table[] = {
	[SCI_RNC_INITIAL] = {
		.enter_state = sci_remote_node_context_initial_state_enter,
	},
	[SCI_RNC_POSTING] = {
		.enter_state = sci_remote_node_context_posting_state_enter,
	},
	[SCI_RNC_INVALIDATING] = {
		.enter_state = sci_remote_node_context_invalidating_state_enter,
	},
	[SCI_RNC_RESUMING] = {
		.enter_state = sci_remote_node_context_resuming_state_enter,
	},
	[SCI_RNC_READY] = {
		.enter_state = sci_remote_node_context_ready_state_enter,
	},
	[SCI_RNC_TX_SUSPENDED] = {
		.enter_state = sci_remote_node_context_tx_suspended_state_enter,
	},
	[SCI_RNC_TX_RX_SUSPENDED] = {
		.enter_state = sci_remote_node_context_tx_rx_suspended_state_enter,
	},
	[SCI_RNC_AWAIT_SUSPENSION] = {
		.exit_state = sci_remote_node_context_await_suspend_state_exit,
	},
};

void sci_remote_node_context_construct(struct sci_remote_node_context *rnc,
					    u16 remote_node_index)
{
	memset(rnc, 0, sizeof(struct sci_remote_node_context));

	rnc->remote_node_index = remote_node_index;
	rnc->destination_state = RNC_DEST_UNSPECIFIED;

	sci_init_sm(&rnc->sm, sci_remote_node_context_state_table, SCI_RNC_INITIAL);
}

enum sci_status sci_remote_node_context_event_handler(struct sci_remote_node_context *sci_rnc,
							   u32 event_code)
{
	enum scis_sds_remote_node_context_states state;
	u32 next_state;

	state = sci_rnc->sm.current_state_id;
	switch (state) {
	case SCI_RNC_POSTING:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_POST_RNC_COMPLETE:
			sci_change_state(&sci_rnc->sm, SCI_RNC_READY);
			break;
		default:
			goto out;
		}
		break;
	case SCI_RNC_INVALIDATING:
		if (scu_get_event_code(event_code) == SCU_EVENT_POST_RNC_INVALIDATE_COMPLETE) {
			if (sci_rnc->destination_state == RNC_DEST_FINAL)
				next_state = SCI_RNC_INITIAL;
			else
				next_state = SCI_RNC_POSTING;
			sci_change_state(&sci_rnc->sm, next_state);
		} else {
			switch (scu_get_event_type(event_code)) {
			case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
			case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
				/* We really dont care if the hardware is going to suspend
				 * the device since it's being invalidated anyway */
				dev_dbg(scirdev_to_dev(rnc_to_dev(sci_rnc)),
					"%s: SCIC Remote Node Context 0x%p was "
					"suspeneded by hardware while being "
					"invalidated.\n", __func__, sci_rnc);
				break;
			default:
				goto out;
			}
		}
		break;
	case SCI_RNC_RESUMING:
		if (scu_get_event_code(event_code) == SCU_EVENT_POST_RCN_RELEASE) {
			sci_change_state(&sci_rnc->sm, SCI_RNC_READY);
		} else {
			switch (scu_get_event_type(event_code)) {
			case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
			case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
				/* We really dont care if the hardware is going to suspend
				 * the device since it's being resumed anyway */
				dev_dbg(scirdev_to_dev(rnc_to_dev(sci_rnc)),
					"%s: SCIC Remote Node Context 0x%p was "
					"suspeneded by hardware while being resumed.\n",
					__func__, sci_rnc);
				break;
			default:
				goto out;
			}
		}
		break;
	case SCI_RNC_READY:
		switch (scu_get_event_type(event_code)) {
		case SCU_EVENT_TL_RNC_SUSPEND_TX:
			sci_change_state(&sci_rnc->sm, SCI_RNC_TX_SUSPENDED);
			sci_rnc->suspend_type = scu_get_event_type(event_code);
			break;
		case SCU_EVENT_TL_RNC_SUSPEND_TX_RX:
			sci_change_state(&sci_rnc->sm, SCI_RNC_TX_RX_SUSPENDED);
			sci_rnc->suspend_type = scu_get_event_type(event_code);
			break;
		default:
			goto out;
		}
		break;
	case SCI_RNC_AWAIT_SUSPENSION:
		switch (scu_get_event_type(event_code)) {
		case SCU_EVENT_TL_RNC_SUSPEND_TX:
			next_state = SCI_RNC_TX_SUSPENDED;
			break;
		case SCU_EVENT_TL_RNC_SUSPEND_TX_RX:
			next_state = SCI_RNC_TX_RX_SUSPENDED;
			break;
		default:
			goto out;
		}
		if (sci_rnc->suspend_type == scu_get_event_type(event_code))
			sci_change_state(&sci_rnc->sm, next_state);
		break;
	default:
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			 "%s: invalid state: %s\n", __func__,
			 rnc_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
	return SCI_SUCCESS;

 out:
	dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
		 "%s: code: %#x state: %s\n", __func__, event_code,
		 rnc_state_name(state));
	return SCI_FAILURE;

}

enum sci_status sci_remote_node_context_destruct(struct sci_remote_node_context *sci_rnc,
						      scics_sds_remote_node_context_callback cb_fn,
						      void *cb_p)
{
	enum scis_sds_remote_node_context_states state;

	state = sci_rnc->sm.current_state_id;
	switch (state) {
	case SCI_RNC_INVALIDATING:
		sci_remote_node_context_setup_to_destroy(sci_rnc, cb_fn, cb_p);
		return SCI_SUCCESS;
	case SCI_RNC_POSTING:
	case SCI_RNC_RESUMING:
	case SCI_RNC_READY:
	case SCI_RNC_TX_SUSPENDED:
	case SCI_RNC_TX_RX_SUSPENDED:
	case SCI_RNC_AWAIT_SUSPENSION:
		sci_remote_node_context_setup_to_destroy(sci_rnc, cb_fn, cb_p);
		sci_change_state(&sci_rnc->sm, SCI_RNC_INVALIDATING);
		return SCI_SUCCESS;
	case SCI_RNC_INITIAL:
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			 "%s: invalid state: %s\n", __func__,
			 rnc_state_name(state));
		/* We have decided that the destruct request on the remote node context
		 * can not fail since it is either in the initial/destroyed state or is
		 * can be destroyed.
		 */
		return SCI_SUCCESS;
	default:
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			 "%s: invalid state %s\n", __func__,
			 rnc_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

enum sci_status sci_remote_node_context_suspend(
			struct sci_remote_node_context *sci_rnc,
			enum sci_remote_node_suspension_reasons suspend_reason,
			u32 suspend_type)
{
	enum scis_sds_remote_node_context_states state
		= sci_rnc->sm.current_state_id;
	struct isci_remote_device *idev = rnc_to_dev(sci_rnc);
	enum sci_status status = SCI_FAILURE_INVALID_STATE;
	enum sci_remote_node_context_destination_state dest_param =
		RNC_DEST_UNSPECIFIED;

	dev_dbg(scirdev_to_dev(idev),
		"%s: current state %d, current suspend_type %x dest state %d,"
			" arg suspend_reason %d, arg suspend_type %x",
		__func__, state, sci_rnc->suspend_type,
		sci_rnc->destination_state, suspend_reason,
		suspend_type);

	/* Disable automatic state continuations if explicitly suspending. */
	if ((suspend_reason == SCI_HW_SUSPEND) ||
	    (sci_rnc->destination_state == RNC_DEST_FINAL))
		dest_param = sci_rnc->destination_state;

	switch (state) {
	case SCI_RNC_RESUMING:
		break;  /* The RNC has been posted, so start the suspend. */
	case SCI_RNC_READY:
		break;
	case SCI_RNC_INVALIDATING:
		if (sci_rnc->destination_state == RNC_DEST_FINAL) {
			dev_warn(scirdev_to_dev(idev),
				 "%s: already destroying %p\n",
				 __func__, sci_rnc);
			return SCI_FAILURE_INVALID_STATE;
		}
		/* Fall through and handle like SCI_RNC_POSTING */
	case SCI_RNC_POSTING:
		/* Set the destination state to AWAIT - this signals the
		 * entry into the SCI_RNC_READY state that a suspension
		 * needs to be done immediately.
		 */
		sci_rnc->destination_state = RNC_DEST_SUSPENDED;
		sci_rnc->suspend_type = suspend_type;
		sci_rnc->suspend_reason = suspend_reason;
		return SCI_SUCCESS;

	case SCI_RNC_TX_SUSPENDED:
		if (suspend_type == SCU_EVENT_TL_RNC_SUSPEND_TX)
			status = SCI_SUCCESS;
		break;
	case SCI_RNC_TX_RX_SUSPENDED:
		if (suspend_type == SCU_EVENT_TL_RNC_SUSPEND_TX_RX)
			status = SCI_SUCCESS;
		break;
	case SCI_RNC_AWAIT_SUSPENSION:
		if ((sci_rnc->suspend_type == SCU_EVENT_TL_RNC_SUSPEND_TX_RX)
		    || (suspend_type == sci_rnc->suspend_type))
			return SCI_SUCCESS;
		break;
	default:
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			 "%s: invalid state %s\n", __func__,
			 rnc_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
	sci_rnc->destination_state = dest_param;
	sci_rnc->suspend_type = suspend_type;
	sci_rnc->suspend_reason = suspend_reason;

	if (status == SCI_SUCCESS) { /* Already in the destination state? */
		struct isci_host *ihost = idev->owning_port->owning_controller;

		wake_up_all(&ihost->eventq); /* Let observers look. */
		return SCI_SUCCESS;
	}
	if ((suspend_reason == SCI_SW_SUSPEND_NORMAL) ||
	    (suspend_reason == SCI_SW_SUSPEND_LINKHANG_DETECT)) {

		if (suspend_reason == SCI_SW_SUSPEND_LINKHANG_DETECT)
			isci_dev_set_hang_detection_timeout(idev, 0x00000001);

		sci_remote_device_post_request(
			idev, SCI_SOFTWARE_SUSPEND_CMD);
	}
	if (state != SCI_RNC_AWAIT_SUSPENSION)
		sci_change_state(&sci_rnc->sm, SCI_RNC_AWAIT_SUSPENSION);

	return SCI_SUCCESS;
}

enum sci_status sci_remote_node_context_resume(struct sci_remote_node_context *sci_rnc,
						    scics_sds_remote_node_context_callback cb_fn,
						    void *cb_p)
{
	enum scis_sds_remote_node_context_states state;
	struct isci_remote_device *idev = rnc_to_dev(sci_rnc);

	state = sci_rnc->sm.current_state_id;
	dev_dbg(scirdev_to_dev(idev),
		"%s: state %s, cb_fn = %p, cb_p = %p; dest_state = %d; "
			"dev resume path %s\n",
		__func__, rnc_state_name(state), cb_fn, cb_p,
		sci_rnc->destination_state,
		test_bit(IDEV_ABORT_PATH_ACTIVE, &idev->flags)
			? "<abort active>" : "<normal>");

	switch (state) {
	case SCI_RNC_INITIAL:
		if (sci_rnc->remote_node_index == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX)
			return SCI_FAILURE_INVALID_STATE;

		if (test_bit(IDEV_ABORT_PATH_ACTIVE, &idev->flags))
			sci_remote_node_context_save_cbparams(sci_rnc, cb_fn,
							      cb_p);
		else {
			sci_remote_node_context_setup_to_resume(sci_rnc, cb_fn,
							cb_p, RNC_DEST_READY);
			sci_remote_node_context_construct_buffer(sci_rnc);
			sci_change_state(&sci_rnc->sm, SCI_RNC_POSTING);
		}
		return SCI_SUCCESS;

	case SCI_RNC_POSTING:
	case SCI_RNC_INVALIDATING:
	case SCI_RNC_RESUMING:
		if (test_bit(IDEV_ABORT_PATH_ACTIVE, &idev->flags))
			sci_remote_node_context_save_cbparams(sci_rnc, cb_fn,
							      cb_p);
		else {
			/* We are still waiting to post when a resume was
			 * requested.
			 */
			switch (sci_rnc->destination_state) {
			case RNC_DEST_SUSPENDED:
			case RNC_DEST_SUSPENDED_RESUME:
				/* Previously waiting to suspend after posting.
				 * Now continue onto resumption.
				 */
				sci_remote_node_context_setup_to_resume(
					sci_rnc, cb_fn, cb_p,
					RNC_DEST_SUSPENDED_RESUME);
				break;
			default:
				sci_remote_node_context_setup_to_resume(
					sci_rnc, cb_fn, cb_p,
					RNC_DEST_READY);
				break;
			}
		}
		return SCI_SUCCESS;

	case SCI_RNC_TX_SUSPENDED:
	case SCI_RNC_TX_RX_SUSPENDED:
		if (test_bit(IDEV_ABORT_PATH_ACTIVE, &idev->flags))
			sci_remote_node_context_save_cbparams(sci_rnc, cb_fn,
							      cb_p);
		else {
			struct domain_device *dev = idev->domain_dev;
			/* If this is an expander attached SATA device we must
			 * invalidate and repost the RNC since this is the only
			 * way to clear the TCi to NCQ tag mapping table for
			 * the RNi. All other device types we can just resume.
			 */
			sci_remote_node_context_setup_to_resume(
				sci_rnc, cb_fn, cb_p, RNC_DEST_READY);

			if (dev_is_sata(dev) && dev->parent)
				sci_change_state(&sci_rnc->sm,
						 SCI_RNC_INVALIDATING);
			else
				sci_change_state(&sci_rnc->sm,
						 SCI_RNC_RESUMING);
		}
		return SCI_SUCCESS;

	case SCI_RNC_AWAIT_SUSPENSION:
		if (test_bit(IDEV_ABORT_PATH_ACTIVE, &idev->flags))
			sci_remote_node_context_save_cbparams(sci_rnc, cb_fn,
							      cb_p);
		else
			sci_remote_node_context_setup_to_resume(
				sci_rnc, cb_fn,	cb_p,
				RNC_DEST_SUSPENDED_RESUME);
		return SCI_SUCCESS;
	default:
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			 "%s: invalid state %s\n", __func__,
			 rnc_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

enum sci_status sci_remote_node_context_start_io(struct sci_remote_node_context *sci_rnc,
							     struct isci_request *ireq)
{
	enum scis_sds_remote_node_context_states state;

	state = sci_rnc->sm.current_state_id;

	switch (state) {
	case SCI_RNC_READY:
		return SCI_SUCCESS;
	case SCI_RNC_TX_SUSPENDED:
	case SCI_RNC_TX_RX_SUSPENDED:
	case SCI_RNC_AWAIT_SUSPENSION:
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			 "%s: invalid state %s\n", __func__,
			 rnc_state_name(state));
		return SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED;
	default:
		dev_dbg(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			"%s: invalid state %s\n", __func__,
			rnc_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

enum sci_status sci_remote_node_context_start_task(
	struct sci_remote_node_context *sci_rnc,
	struct isci_request *ireq,
	scics_sds_remote_node_context_callback cb_fn,
	void *cb_p)
{
	enum sci_status status = sci_remote_node_context_resume(sci_rnc,
								cb_fn, cb_p);
	if (status != SCI_SUCCESS)
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			"%s: resume failed: %d\n", __func__, status);
	return status;
}

int sci_remote_node_context_is_safe_to_abort(
	struct sci_remote_node_context *sci_rnc)
{
	enum scis_sds_remote_node_context_states state;

	state = sci_rnc->sm.current_state_id;
	switch (state) {
	case SCI_RNC_INVALIDATING:
	case SCI_RNC_TX_RX_SUSPENDED:
		return 1;
	case SCI_RNC_POSTING:
	case SCI_RNC_RESUMING:
	case SCI_RNC_READY:
	case SCI_RNC_TX_SUSPENDED:
	case SCI_RNC_AWAIT_SUSPENSION:
	case SCI_RNC_INITIAL:
		return 0;
	default:
		dev_warn(scirdev_to_dev(rnc_to_dev(sci_rnc)),
			 "%s: invalid state %d\n", __func__, state);
		return 0;
	}
}
