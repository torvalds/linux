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

#include "intel_sas.h"
#include "scic_controller.h"
#include "scic_phy.h"
#include "scic_port.h"
#include "scic_remote_device.h"
#include "scic_sds_controller.h"
#include "scic_sds_phy.h"
#include "scic_sds_port.h"
#include "scic_sds_remote_device.h"
#include "scic_sds_remote_node_context.h"
#include "scic_sds_request.h"
#include "sci_environment.h"
#include "sci_util.h"
#include "scu_event_codes.h"


#define SCIC_SDS_REMOTE_DEVICE_RESET_TIMEOUT  (1000)

/*
 * *****************************************************************************
 * *  CORE REMOTE DEVICE PRIVATE METHODS
 * ***************************************************************************** */

/*
 * *****************************************************************************
 * *  CORE REMOTE DEVICE PUBLIC METHODS
 * ***************************************************************************** */

u32 scic_remote_device_get_object_size(void)
{
	return sizeof(struct scic_sds_remote_device)
	       + sizeof(struct scic_sds_remote_node_context);
}

enum sci_status scic_remote_device_da_construct(
	struct scic_sds_remote_device *sci_dev)
{
	enum sci_status status;
	u16 remote_node_index;
	struct sci_sas_identify_address_frame_protocols protocols;

	/*
	 * This information is request to determine how many remote node context
	 * entries will be needed to store the remote node.
	 */
	scic_sds_port_get_attached_protocols(sci_dev->owning_port, &protocols);
	sci_dev->target_protocols.u.all = protocols.u.all;
	sci_dev->is_direct_attached = true;
#if !defined(DISABLE_ATAPI)
	sci_dev->is_atapi = scic_sds_remote_device_is_atapi(sci_dev);
#endif

	status = scic_sds_controller_allocate_remote_node_context(
		sci_dev->owning_port->owning_controller,
		sci_dev,
		&remote_node_index);

	if (status == SCI_SUCCESS) {
		sci_dev->rnc->remote_node_index = remote_node_index;

		scic_sds_port_get_attached_sas_address(
			sci_dev->owning_port, &sci_dev->device_address);

		if (sci_dev->target_protocols.u.bits.attached_ssp_target) {
			sci_dev->has_ready_substate_machine = false;
		} else if (sci_dev->target_protocols.u.bits.attached_stp_target) {
			sci_dev->has_ready_substate_machine = true;

			sci_base_state_machine_construct(
				&sci_dev->ready_substate_machine,
				&sci_dev->parent.parent,
				scic_sds_stp_remote_device_ready_substate_table,
				SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);
		} else if (sci_dev->target_protocols.u.bits.attached_smp_target) {
			sci_dev->has_ready_substate_machine = true;

			/* add the SMP ready substate machine construction here */
			sci_base_state_machine_construct(
				&sci_dev->ready_substate_machine,
				&sci_dev->parent.parent,
				scic_sds_smp_remote_device_ready_substate_table,
				SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);
		}

		sci_dev->connection_rate = scic_sds_port_get_max_allowed_speed(
			sci_dev->owning_port);

		/* / @todo Should I assign the port width by reading all of the phys on the port? */
		sci_dev->device_port_width = 1;
	}

	return status;
}


static void scic_sds_remote_device_get_info_from_smp_discover_response(
	struct scic_sds_remote_device *this_device,
	struct smp_response_discover *discover_response)
{
	/* decode discover_response to set sas_address to this_device. */
	this_device->device_address.high =
		discover_response->attached_sas_address.high;

	this_device->device_address.low =
		discover_response->attached_sas_address.low;

	this_device->target_protocols.u.all = discover_response->protocols.u.all;
}


enum sci_status scic_remote_device_ea_construct(
	struct scic_sds_remote_device *sci_dev,
	struct smp_response_discover *discover_response)
{
	enum sci_status status;
	struct scic_sds_controller *the_controller;

	the_controller = scic_sds_port_get_controller(sci_dev->owning_port);

	scic_sds_remote_device_get_info_from_smp_discover_response(
		sci_dev, discover_response);

	status = scic_sds_controller_allocate_remote_node_context(
		the_controller, sci_dev, &sci_dev->rnc->remote_node_index);

	if (status == SCI_SUCCESS) {
		if (sci_dev->target_protocols.u.bits.attached_ssp_target) {
			sci_dev->has_ready_substate_machine = false;
		} else if (sci_dev->target_protocols.u.bits.attached_smp_target) {
			sci_dev->has_ready_substate_machine = true;

			/* add the SMP ready substate machine construction here */
			sci_base_state_machine_construct(
				&sci_dev->ready_substate_machine,
				&sci_dev->parent.parent,
				scic_sds_smp_remote_device_ready_substate_table,
				SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);
		} else if (sci_dev->target_protocols.u.bits.attached_stp_target) {
			sci_dev->has_ready_substate_machine = true;

			sci_base_state_machine_construct(
				&sci_dev->ready_substate_machine,
				&sci_dev->parent.parent,
				scic_sds_stp_remote_device_ready_substate_table,
				SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);
		}

		/*
		 * For SAS-2 the physical link rate is actually a logical link
		 * rate that incorporates multiplexing.  The SCU doesn't
		 * incorporate multiplexing and for the purposes of the
		 * connection the logical link rate is that same as the
		 * physical.  Furthermore, the SAS-2 and SAS-1.1 fields overlay
		 * one another, so this code works for both situations. */
		sci_dev->connection_rate = min_t(u16,
			scic_sds_port_get_max_allowed_speed(sci_dev->owning_port),
			discover_response->u2.sas1_1.negotiated_physical_link_rate
			);

		/* / @todo Should I assign the port width by reading all of the phys on the port? */
		sci_dev->device_port_width = 1;
	}

	return status;
}

enum sci_status scic_remote_device_destruct(
	struct scic_sds_remote_device *sci_dev)
{
	return sci_dev->state_handlers->parent.destruct_handler(&sci_dev->parent);
}


enum sci_status scic_remote_device_start(
	struct scic_sds_remote_device *sci_dev,
	u32 timeout)
{
	return sci_dev->state_handlers->parent.start_handler(&sci_dev->parent);
}


enum sci_status scic_remote_device_stop(
	struct scic_sds_remote_device *sci_dev,
	u32 timeout)
{
	return sci_dev->state_handlers->parent.stop_handler(&sci_dev->parent);
}


enum sci_status scic_remote_device_reset(
	struct scic_sds_remote_device *sci_dev)
{
	return sci_dev->state_handlers->parent.reset_handler(&sci_dev->parent);
}


enum sci_status scic_remote_device_reset_complete(
	struct scic_sds_remote_device *sci_dev)
{
	return sci_dev->state_handlers->parent.reset_complete_handler(&sci_dev->parent);
}


enum sci_sas_link_rate scic_remote_device_get_connection_rate(
	struct scic_sds_remote_device *sci_dev)
{
	return sci_dev->connection_rate;
}


void scic_remote_device_get_protocols(
	struct scic_sds_remote_device *sci_dev,
	struct smp_discover_response_protocols *pr)
{
	pr->u.all = sci_dev->target_protocols.u.all;
}

#if !defined(DISABLE_ATAPI)
bool scic_remote_device_is_atapi(struct scic_sds_remote_device *sci_dev)
{
	return sci_dev->is_atapi;
}
#endif


/*
 * *****************************************************************************
 * *  SCU DRIVER STANDARD (SDS) REMOTE DEVICE IMPLEMENTATIONS
 * ***************************************************************************** */

/**
 *
 *
 * Remote device timer requirements
 */
#define SCIC_SDS_REMOTE_DEVICE_MINIMUM_TIMER_COUNT (0)
#define SCIC_SDS_REMOTE_DEVICE_MAXIMUM_TIMER_COUNT (SCI_MAX_REMOTE_DEVICES)


/**
 *
 * @this_device: The remote device for which the suspend is being requested.
 *
 * This method invokes the remote device suspend state handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_suspend(
	struct scic_sds_remote_device *this_device,
	u32 suspend_type)
{
	return this_device->state_handlers->suspend_handler(this_device, suspend_type);
}

/**
 *
 * @this_device: The remote device for which the resume is being requested.
 *
 * This method invokes the remote device resume state handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_resume(
	struct scic_sds_remote_device *this_device)
{
	return this_device->state_handlers->resume_handler(this_device);
}

/**
 *
 * @this_device: The remote device for which the event handling is being
 *    requested.
 * @frame_index: This is the frame index that is being processed.
 *
 * This method invokes the frame handler for the remote device state machine
 * enum sci_status
 */
enum sci_status scic_sds_remote_device_frame_handler(
	struct scic_sds_remote_device *this_device,
	u32 frame_index)
{
	return this_device->state_handlers->frame_handler(this_device, frame_index);
}

/**
 *
 * @this_device: The remote device for which the event handling is being
 *    requested.
 * @event_code: This is the event code that is to be processed.
 *
 * This method invokes the remote device event handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_event_handler(
	struct scic_sds_remote_device *this_device,
	u32 event_code)
{
	return this_device->state_handlers->event_handler(this_device, event_code);
}

/**
 *
 * @controller: The controller that is starting the io request.
 * @this_device: The remote device for which the start io handling is being
 *    requested.
 * @io_request: The io request that is being started.
 *
 * This method invokes the remote device start io handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_start_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *this_device,
	struct scic_sds_request *io_request)
{
	return this_device->state_handlers->parent.start_io_handler(
		       &this_device->parent, &io_request->parent);
}

/**
 *
 * @controller: The controller that is completing the io request.
 * @this_device: The remote device for which the complete io handling is being
 *    requested.
 * @io_request: The io request that is being completed.
 *
 * This method invokes the remote device complete io handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_complete_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *this_device,
	struct scic_sds_request *io_request)
{
	return this_device->state_handlers->parent.complete_io_handler(
		       &this_device->parent, &io_request->parent);
}

/**
 *
 * @controller: The controller that is starting the task request.
 * @this_device: The remote device for which the start task handling is being
 *    requested.
 * @io_request: The task request that is being started.
 *
 * This method invokes the remote device start task handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_start_task(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *this_device,
	struct scic_sds_request *io_request)
{
	return this_device->state_handlers->parent.start_task_handler(
		       &this_device->parent, &io_request->parent);
}

/**
 *
 * @controller: The controller that is completing the task request.
 * @this_device: The remote device for which the complete task handling is
 *    being requested.
 * @io_request: The task request that is being completed.
 *
 * This method invokes the remote device complete task handler. enum sci_status
 */

/**
 *
 * @this_device:
 * @request:
 *
 * This method takes the request and bulids an appropriate SCU context for the
 * request and then requests the controller to post the request. none
 */
void scic_sds_remote_device_post_request(
	struct scic_sds_remote_device *this_device,
	u32 request)
{
	u32 context;

	context = scic_sds_remote_device_build_command_context(this_device, request);

	scic_sds_controller_post_request(
		scic_sds_remote_device_get_controller(this_device),
		context
		);
}

#if !defined(DISABLE_ATAPI)
/**
 *
 * @this_device: The device to be checked.
 *
 * This method check the signature fis of a stp device to decide whether a
 * device is atapi or not. true if a device is atapi device. False if a device
 * is not atapi.
 */
bool scic_sds_remote_device_is_atapi(
	struct scic_sds_remote_device *this_device)
{
	if (!this_device->target_protocols.u.bits.attached_stp_target)
		return false;
	else if (this_device->is_direct_attached) {
		struct scic_sds_phy *phy;
		struct scic_sata_phy_properties properties;
		struct sata_fis_reg_d2h *signature_fis;
		phy = scic_sds_port_get_a_connected_phy(this_device->owning_port);
		scic_sata_phy_get_properties(phy, &properties);

		/* decode the signature fis. */
		signature_fis = &(properties.signature_fis);

		if ((signature_fis->sector_count  == 0x01)
		    && (signature_fis->lba_low       == 0x01)
		    && (signature_fis->lba_mid       == 0x14)
		    && (signature_fis->lba_high      == 0xEB)
		    && ((signature_fis->device & 0x5F) == 0x00)
		    ) {
			/* An ATA device supporting the PACKET command set. */
			return true;
		} else
			return false;
	} else {
		/* Expander supported ATAPI device is not currently supported. */
		return false;
	}
}
#endif

/**
 *
 * @user_parameter: This is cast to a remote device object.
 *
 * This method is called once the remote node context is ready to be freed.
 * The remote device can now report that its stop operation is complete. none
 */
static void scic_sds_cb_remote_device_rnc_destruct_complete(
	void *user_parameter)
{
	struct scic_sds_remote_device *sci_dev;

	sci_dev = (struct scic_sds_remote_device *)user_parameter;

	BUG_ON(sci_dev->started_request_count != 0);

	sci_base_state_machine_change_state(&sci_dev->parent.state_machine,
					    SCI_BASE_REMOTE_DEVICE_STATE_STOPPED);
}

/**
 *
 * @user_parameter: This is cast to a remote device object.
 *
 * This method is called once the remote node context has transisitioned to a
 * ready state.  This is the indication that the remote device object can also
 * transition to ready. none
 */
static void scic_sds_remote_device_resume_complete_handler(
	void *user_parameter)
{
	struct scic_sds_remote_device *this_device;

	this_device = (struct scic_sds_remote_device *)user_parameter;

	if (
		sci_base_state_machine_get_state(&this_device->parent.state_machine)
		!= SCI_BASE_REMOTE_DEVICE_STATE_READY
		) {
		sci_base_state_machine_change_state(
			&this_device->parent.state_machine,
			SCI_BASE_REMOTE_DEVICE_STATE_READY
			);
	}
}

/**
 *
 * @device: This parameter specifies the device for which the request is being
 *    started.
 * @request: This parameter specifies the request being started.
 * @status: This parameter specifies the current start operation status.
 *
 * This method will perform the STP request start processing common to IO
 * requests and task requests of all types. none
 */
void scic_sds_remote_device_start_request(
	struct scic_sds_remote_device *this_device,
	struct scic_sds_request *the_request,
	enum sci_status status)
{
	/* We still have a fault in starting the io complete it on the port */
	if (status == SCI_SUCCESS)
		scic_sds_remote_device_increment_request_count(this_device);
	else{
		this_device->owning_port->state_handlers->complete_io_handler(
			this_device->owning_port, this_device, the_request
			);
	}
}


/**
 *
 * @request: This parameter specifies the request being continued.
 *
 * This method will continue to post tc for a STP request. This method usually
 * serves as a callback when RNC gets resumed during a task management
 * sequence. none
 */
void scic_sds_remote_device_continue_request(void *dev)
{
	struct scic_sds_remote_device *sci_dev = dev;
	struct scic_sds_request *sci_req = sci_dev->working_request;

	/* we need to check if this request is still valid to continue. */
	if (sci_req) {
		struct scic_sds_controller *scic = sci_req->owning_controller;
		u32 state = scic->parent.state_machine.current_state_id;
		sci_base_controller_request_handler_t continue_io;

		continue_io = scic_sds_controller_state_handler_table[state].base.continue_io;
		continue_io(&scic->parent, &sci_req->target_device->parent,
			    &sci_req->parent);
	}
}

/**
 *
 * @user_parameter: This is cast to a remote device object.
 *
 * This method is called once the remote node context has reached a suspended
 * state. The remote device can now report that its suspend operation is
 * complete. none
 */

/**
 * This method will terminate all of the IO requests in the controllers IO
 *    request table that were targeted for this device.
 * @this_device: This parameter specifies the remote device for which to
 *    attempt to terminate all requests.
 *
 * This method returns an indication as to whether all requests were
 * successfully terminated.  If a single request fails to be terminated, then
 * this method will return the failure.
 */
static enum sci_status scic_sds_remote_device_terminate_requests(
	struct scic_sds_remote_device *this_device)
{
	enum sci_status status           = SCI_SUCCESS;
	enum sci_status terminate_status = SCI_SUCCESS;
	struct scic_sds_request *the_request;
	u32 index;
	u32 request_count    = this_device->started_request_count;

	for (index = 0;
	     (index < SCI_MAX_IO_REQUESTS) && (request_count > 0);
	     index++) {
		the_request = this_device->owning_port->owning_controller->io_request_table[index];

		if ((the_request != NULL) && (the_request->target_device == this_device)) {
			terminate_status = scic_controller_terminate_request(
				this_device->owning_port->owning_controller,
				this_device,
				the_request
				);

			if (terminate_status != SCI_SUCCESS)
				status = terminate_status;

			request_count--;
		}
	}

	return status;
}

static enum sci_status default_device_handler(struct sci_base_remote_device *base_dev,
					      const char *func)
{
	struct scic_sds_remote_device *sci_dev;

	sci_dev = container_of(base_dev, typeof(*sci_dev), parent);
	dev_warn(scirdev_to_dev(sci_dev),
		 "%s: in wrong state: %d\n", func,
		 sci_base_state_machine_get_state(&base_dev->state_machine));
	return SCI_FAILURE_INVALID_STATE;
}

enum sci_status scic_sds_remote_device_default_start_handler(
	struct sci_base_remote_device *base_dev)
{
	return default_device_handler(base_dev, __func__);
}

static enum sci_status scic_sds_remote_device_default_stop_handler(
	struct sci_base_remote_device *base_dev)
{
	return default_device_handler(base_dev, __func__);
}

enum sci_status scic_sds_remote_device_default_fail_handler(
	struct sci_base_remote_device *base_dev)
{
	return default_device_handler(base_dev, __func__);
}

enum sci_status scic_sds_remote_device_default_destruct_handler(
	struct sci_base_remote_device *base_dev)
{
	return default_device_handler(base_dev, __func__);
}

enum sci_status scic_sds_remote_device_default_reset_handler(
	struct sci_base_remote_device *base_dev)
{
	return default_device_handler(base_dev, __func__);
}

enum sci_status scic_sds_remote_device_default_reset_complete_handler(
	struct sci_base_remote_device *base_dev)
{
	return default_device_handler(base_dev, __func__);
}

enum sci_status scic_sds_remote_device_default_suspend_handler(
	struct scic_sds_remote_device *sci_dev, u32 suspend_type)
{
	return default_device_handler(&sci_dev->parent, __func__);
}

enum sci_status scic_sds_remote_device_default_resume_handler(
	struct scic_sds_remote_device *sci_dev)
{
	return default_device_handler(&sci_dev->parent, __func__);
}

/**
 *
 * @device: The struct sci_base_remote_device which is then cast into a
 *    struct scic_sds_remote_device.
 * @event_code: The event code that the struct scic_sds_controller wants the device
 *    object to process.
 *
 * This method is the default event handler.  It will call the RNC state
 * machine handler for any RNC events otherwise it will log a warning and
 * returns a failure. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status  scic_sds_remote_device_core_event_handler(
	struct scic_sds_remote_device *this_device,
	u32 event_code,
	bool is_ready_state)
{
	enum sci_status status;

	switch (scu_get_event_type(event_code)) {
	case SCU_EVENT_TYPE_RNC_OPS_MISC:
	case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
	case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
		status = scic_sds_remote_node_context_event_handler(this_device->rnc, event_code);
		break;
	case SCU_EVENT_TYPE_PTX_SCHEDULE_EVENT:

		if (scu_get_event_code(event_code) == SCU_EVENT_IT_NEXUS_TIMEOUT) {
			status = SCI_SUCCESS;

			/* Suspend the associated RNC */
			scic_sds_remote_node_context_suspend(this_device->rnc,
							      SCI_SOFTWARE_SUSPENSION,
							      NULL, NULL);

			dev_dbg(scirdev_to_dev(this_device),
				"%s: device: %p event code: %x: %s\n",
				__func__, this_device, event_code,
				(is_ready_state)
				? "I_T_Nexus_Timeout event"
				: "I_T_Nexus_Timeout event in wrong state");

			break;
		}
	/* Else, fall through and treat as unhandled... */

	default:
		dev_dbg(scirdev_to_dev(this_device),
			"%s: device: %p event code: %x: %s\n",
			__func__, this_device, event_code,
			(is_ready_state)
			? "unexpected event"
			: "unexpected event in wrong state");
		status = SCI_FAILURE_INVALID_STATE;
		break;
	}

	return status;
}
/**
 *
 * @device: The struct sci_base_remote_device which is then cast into a
 *    struct scic_sds_remote_device.
 * @event_code: The event code that the struct scic_sds_controller wants the device
 *    object to process.
 *
 * This method is the default event handler.  It will call the RNC state
 * machine handler for any RNC events otherwise it will log a warning and
 * returns a failure. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status  scic_sds_remote_device_default_event_handler(
	struct scic_sds_remote_device *this_device,
	u32 event_code)
{
	return scic_sds_remote_device_core_event_handler(this_device,
							  event_code,
							  false);
}

/**
 *
 * @device: The struct sci_base_remote_device which is then cast into a
 *    struct scic_sds_remote_device.
 * @frame_index: The frame index for which the struct scic_sds_controller wants this
 *    device object to process.
 *
 * This method is the default unsolicited frame handler.  It logs a warning,
 * releases the frame and returns a failure. enum sci_status
 * SCI_FAILURE_INVALID_STATE
 */
enum sci_status scic_sds_remote_device_default_frame_handler(
	struct scic_sds_remote_device *this_device,
	u32 frame_index)
{
	dev_warn(scirdev_to_dev(this_device),
		 "%s: SCIC Remote Device requested to handle frame %x "
		 "while in wrong state %d\n",
		 __func__,
		 frame_index,
		 sci_base_state_machine_get_state(
			 &this_device->parent.state_machine));

	/* Return the frame back to the controller */
	scic_sds_controller_release_frame(
		scic_sds_remote_device_get_controller(this_device), frame_index
		);

	return SCI_FAILURE_INVALID_STATE;
}

enum sci_status scic_sds_remote_device_default_start_request_handler(
	struct sci_base_remote_device *base_dev,
	struct sci_base_request *request)
{
	return default_device_handler(base_dev, __func__);
}

enum sci_status scic_sds_remote_device_default_complete_request_handler(
	struct sci_base_remote_device *base_dev,
	struct sci_base_request *request)
{
	return default_device_handler(base_dev, __func__);
}

enum sci_status scic_sds_remote_device_default_continue_request_handler(
	struct sci_base_remote_device *base_dev,
	struct sci_base_request *request)
{
	return default_device_handler(base_dev, __func__);
}

/**
 *
 * @device: The struct sci_base_remote_device which is then cast into a
 *    struct scic_sds_remote_device.
 * @frame_index: The frame index for which the struct scic_sds_controller wants this
 *    device object to process.
 *
 * This method is a general ssp frame handler.  In most cases the device object
 * needs to route the unsolicited frame processing to the io request object.
 * This method decodes the tag for the io request object and routes the
 * unsolicited frame to that object. enum sci_status SCI_FAILURE_INVALID_STATE
 */
enum sci_status scic_sds_remote_device_general_frame_handler(
	struct scic_sds_remote_device *this_device,
	u32 frame_index)
{
	enum sci_status result;
	struct sci_ssp_frame_header *frame_header;
	struct scic_sds_request *io_request;

	result = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_remote_device_get_controller(this_device)->uf_control),
		frame_index,
		(void **)&frame_header
		);

	if (SCI_SUCCESS == result) {
		io_request = scic_sds_controller_get_io_request_from_tag(
			scic_sds_remote_device_get_controller(this_device), frame_header->tag);

		if ((io_request == NULL)
		    || (io_request->target_device != this_device)) {
			/*
			 * We could not map this tag to a valid IO request
			 * Just toss the frame and continue */
			scic_sds_controller_release_frame(
				scic_sds_remote_device_get_controller(this_device), frame_index
				);
		} else {
			/* The IO request is now in charge of releasing the frame */
			result = io_request->state_handlers->frame_handler(
				io_request, frame_index);
		}
	}

	return result;
}

/**
 *
 * @[in]: this_device This is the device object that is receiving the event.
 * @[in]: event_code The event code to process.
 *
 * This is a common method for handling events reported to the remote device
 * from the controller object. enum sci_status
 */
enum sci_status scic_sds_remote_device_general_event_handler(
	struct scic_sds_remote_device *this_device,
	u32 event_code)
{
	return scic_sds_remote_device_core_event_handler(this_device,
							  event_code,
							  true);
}

/*
 * *****************************************************************************
 * *  STOPPED STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @device:
 *
 * This method takes the struct scic_sds_remote_device from a stopped state and
 * attempts to start it.   The RNC buffer for the device is constructed and the
 * device state machine is transitioned to the
 * SCIC_BASE_REMOTE_DEVICE_STATE_STARTING. enum sci_status SCI_SUCCESS if there is
 * an RNC buffer available to construct the remote device.
 * SCI_FAILURE_INSUFFICIENT_RESOURCES if there is no RNC buffer available in
 * which to construct the remote device.
 */
static enum sci_status scic_sds_remote_device_stopped_state_start_handler(
	struct sci_base_remote_device *base_dev)
{
	enum sci_status status;
	struct scic_sds_remote_device *sci_dev;

	sci_dev = container_of(base_dev, typeof(*sci_dev), parent);

	status = scic_sds_remote_node_context_resume(sci_dev->rnc,
			scic_sds_remote_device_resume_complete_handler, sci_dev);

	if (status == SCI_SUCCESS)
		sci_base_state_machine_change_state(&base_dev->state_machine,
						    SCI_BASE_REMOTE_DEVICE_STATE_STARTING);

	return status;
}

static enum sci_status scic_sds_remote_device_stopped_state_stop_handler(
	struct sci_base_remote_device *base_dev)
{
	return SCI_SUCCESS;
}

/**
 *
 * @sci_dev: The struct sci_base_remote_device which is cast into a
 *    struct scic_sds_remote_device.
 *
 * This method will destruct a struct scic_sds_remote_device that is in a stopped
 * state.  This is the only state from which a destruct request will succeed.
 * The RNi for this struct scic_sds_remote_device is returned to the free pool and the
 * device object transitions to the SCI_BASE_REMOTE_DEVICE_STATE_FINAL.
 * enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_remote_device_stopped_state_destruct_handler(
	struct sci_base_remote_device *base_dev)
{
	struct scic_sds_remote_device *sci_dev;
	struct scic_sds_controller *scic;

	sci_dev = container_of(base_dev, typeof(*sci_dev), parent);
	scic = scic_sds_remote_device_get_controller(sci_dev);
	scic_sds_controller_free_remote_node_context(scic, sci_dev,
						     sci_dev->rnc->remote_node_index);
	sci_dev->rnc->remote_node_index = SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX;

	sci_base_state_machine_change_state(&base_dev->state_machine,
					    SCI_BASE_REMOTE_DEVICE_STATE_FINAL);

	return SCI_SUCCESS;
}

/*
 * *****************************************************************************
 * *  STARTING STATE HANDLERS
 * ***************************************************************************** */

static enum sci_status scic_sds_remote_device_starting_state_stop_handler(
	struct sci_base_remote_device *base_dev)
{
	struct scic_sds_remote_device *sci_dev;

	sci_dev = container_of(base_dev, typeof(*sci_dev), parent);
	/*
	 * This device has not yet started so there had better be no IO requests
	 */
	BUG_ON(sci_dev->started_request_count != 0);

	/*
	 * Destroy the remote node context
	 */
	scic_sds_remote_node_context_destruct(sci_dev->rnc,
		scic_sds_cb_remote_device_rnc_destruct_complete, sci_dev);

	/*
	 * Transition to the stopping state and wait for the remote node to
	 * complete being posted and invalidated.
	 */
	sci_base_state_machine_change_state(&base_dev->state_machine,
					    SCI_BASE_REMOTE_DEVICE_STATE_STOPPING);

	return SCI_SUCCESS;
}

enum sci_status scic_sds_remote_device_ready_state_stop_handler(
	struct sci_base_remote_device *base_dev)
{
	struct scic_sds_remote_device *sci_dev;
	enum sci_status status = SCI_SUCCESS;

	sci_dev = container_of(base_dev, typeof(*sci_dev), parent);
	/* Request the parent state machine to transition to the stopping state */
	sci_base_state_machine_change_state(&base_dev->state_machine,
					    SCI_BASE_REMOTE_DEVICE_STATE_STOPPING);

	if (sci_dev->started_request_count == 0) {
		scic_sds_remote_node_context_destruct(sci_dev->rnc,
			scic_sds_cb_remote_device_rnc_destruct_complete,
			sci_dev);
	} else
		status = scic_sds_remote_device_terminate_requests(sci_dev);

	return status;
}

/**
 *
 * @device: The struct sci_base_remote_device object which is cast to a
 *    struct scic_sds_remote_device object.
 *
 * This is the ready state device reset handler enum sci_status
 */
enum sci_status scic_sds_remote_device_ready_state_reset_handler(
	struct sci_base_remote_device *base_dev)
{
	struct scic_sds_remote_device *sci_dev;

	sci_dev = container_of(base_dev, typeof(*sci_dev), parent);
	/* Request the parent state machine to transition to the stopping state */
	sci_base_state_machine_change_state(&base_dev->state_machine,
					    SCI_BASE_REMOTE_DEVICE_STATE_RESETTING);

	return SCI_SUCCESS;
}

/**
 *
 * @device: The struct sci_base_remote_device which is cast to a
 *    struct scic_sds_remote_device for which the request is to be started.
 * @request: The struct sci_base_request which is cast to a SCIC_SDS_IO_REQUEST that
 *    is to be started.
 *
 * This method will attempt to start a task request for this device object. The
 * remote device object will issue the start request for the task and if
 * successful it will start the request for the port object then increment its
 * own requet count. enum sci_status SCI_SUCCESS if the task request is started for
 * this device object. SCI_FAILURE_INSUFFICIENT_RESOURCES if the io request
 * object could not get the resources to start.
 */
static enum sci_status scic_sds_remote_device_ready_state_start_task_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request)
{
	enum sci_status result;
	struct scic_sds_remote_device *this_device  = (struct scic_sds_remote_device *)device;
	struct scic_sds_request *task_request = (struct scic_sds_request *)request;

	/* See if the port is in a state where we can start the IO request */
	result = scic_sds_port_start_io(
		scic_sds_remote_device_get_port(this_device), this_device, task_request);

	if (result == SCI_SUCCESS) {
		result = scic_sds_remote_node_context_start_task(
			this_device->rnc, task_request
			);

		if (result == SCI_SUCCESS) {
			result = scic_sds_request_start(task_request);
		}

		scic_sds_remote_device_start_request(this_device, task_request, result);
	}

	return result;
}

/**
 *
 * @device: The struct sci_base_remote_device which is cast to a
 *    struct scic_sds_remote_device for which the request is to be started.
 * @request: The struct sci_base_request which is cast to a SCIC_SDS_IO_REQUEST that
 *    is to be started.
 *
 * This method will attempt to start an io request for this device object. The
 * remote device object will issue the start request for the io and if
 * successful it will start the request for the port object then increment its
 * own requet count. enum sci_status SCI_SUCCESS if the io request is started for
 * this device object. SCI_FAILURE_INSUFFICIENT_RESOURCES if the io request
 * object could not get the resources to start.
 */
static enum sci_status scic_sds_remote_device_ready_state_start_io_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request)
{
	enum sci_status result;
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)device;
	struct scic_sds_request *io_request  = (struct scic_sds_request *)request;

	/* See if the port is in a state where we can start the IO request */
	result = scic_sds_port_start_io(
		scic_sds_remote_device_get_port(this_device), this_device, io_request);

	if (result == SCI_SUCCESS) {
		result = scic_sds_remote_node_context_start_io(
			this_device->rnc, io_request
			);

		if (result == SCI_SUCCESS) {
			result = scic_sds_request_start(io_request);
		}

		scic_sds_remote_device_start_request(this_device, io_request, result);
	}

	return result;
}

/**
 *
 * @device: The struct sci_base_remote_device which is cast to a
 *    struct scic_sds_remote_device for which the request is to be completed.
 * @request: The struct sci_base_request which is cast to a SCIC_SDS_IO_REQUEST that
 *    is to be completed.
 *
 * This method will complete the request for the remote device object.  The
 * method will call the completion handler for the request object and if
 * successful it will complete the request on the port object then decrement
 * its own started_request_count. enum sci_status
 */
static enum sci_status scic_sds_remote_device_ready_state_complete_request_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request)
{
	enum sci_status result;
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)device;
	struct scic_sds_request *the_request = (struct scic_sds_request *)request;

	result = scic_sds_request_complete(the_request);

	if (result == SCI_SUCCESS) {
		/* See if the port is in a state where we can start the IO request */
		result = scic_sds_port_complete_io(
			scic_sds_remote_device_get_port(this_device), this_device, the_request);

		if (result == SCI_SUCCESS) {
			scic_sds_remote_device_decrement_request_count(this_device);
		}
	}

	return result;
}

/*
 * *****************************************************************************
 * *  STOPPING STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @this_device: The struct sci_base_remote_device which is cast into a
 *    struct scic_sds_remote_device.
 *
 * This method will stop a struct scic_sds_remote_device that is already in the
 * SCI_BASE_REMOTE_DEVICE_STATE_STOPPING state. This is not considered an error
 * since we allow a stop request on a device that is alreay stopping or
 * stopped. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_remote_device_stopping_state_stop_handler(
	struct sci_base_remote_device *device)
{
	/*
	 * All requests should have been terminated, but if there is an
	 * attempt to stop a device already in the stopping state, then
	 * try again to terminate. */
	return scic_sds_remote_device_terminate_requests(
		       (struct scic_sds_remote_device *)device);
}


/**
 *
 * @device: The device object for which the request is completing.
 * @request: The task request that is being completed.
 *
 * This method completes requests for this struct scic_sds_remote_device while it is
 * in the SCI_BASE_REMOTE_DEVICE_STATE_STOPPING state. This method calls the
 * complete method for the request object and if that is successful the port
 * object is called to complete the task request. Then the device object itself
 * completes the task request. If struct scic_sds_remote_device started_request_count
 * goes to 0 and the invalidate RNC request has completed the device object can
 * transition to the SCI_BASE_REMOTE_DEVICE_STATE_STOPPED. enum sci_status
 */
static enum sci_status scic_sds_remote_device_stopping_state_complete_request_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request)
{
	enum sci_status status = SCI_SUCCESS;
	struct scic_sds_request *this_request = (struct scic_sds_request *)request;
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)device;

	status = scic_sds_request_complete(this_request);
	if (status == SCI_SUCCESS) {
		status = scic_sds_port_complete_io(
			scic_sds_remote_device_get_port(this_device),
			this_device,
			this_request
			);

		if (status == SCI_SUCCESS) {
			scic_sds_remote_device_decrement_request_count(this_device);

			if (scic_sds_remote_device_get_request_count(this_device) == 0) {
				scic_sds_remote_node_context_destruct(
					this_device->rnc,
					scic_sds_cb_remote_device_rnc_destruct_complete,
					this_device
					);
			}
		}
	}

	return status;
}

/*
 * *****************************************************************************
 * *  RESETTING STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @device: The struct sci_base_remote_device which is to be cast into a
 *    struct scic_sds_remote_device object.
 *
 * This method will complete the reset operation when the device is in the
 * resetting state. enum sci_status
 */
static enum sci_status scic_sds_remote_device_resetting_state_reset_complete_handler(
	struct sci_base_remote_device *device)
{
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)device;

	sci_base_state_machine_change_state(
		&this_device->parent.state_machine,
		SCI_BASE_REMOTE_DEVICE_STATE_READY
		);

	return SCI_SUCCESS;
}

/**
 *
 * @device: The struct sci_base_remote_device which is to be cast into a
 *    struct scic_sds_remote_device object.
 *
 * This method will stop the remote device while in the resetting state.
 * enum sci_status
 */
static enum sci_status scic_sds_remote_device_resetting_state_stop_handler(
	struct sci_base_remote_device *device)
{
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)device;

	sci_base_state_machine_change_state(
		&this_device->parent.state_machine,
		SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
		);

	return SCI_SUCCESS;
}

/**
 *
 * @device: The device object for which the request is completing.
 * @request: The task request that is being completed.
 *
 * This method completes requests for this struct scic_sds_remote_device while it is
 * in the SCI_BASE_REMOTE_DEVICE_STATE_RESETTING state. This method calls the
 * complete method for the request object and if that is successful the port
 * object is called to complete the task request. Then the device object itself
 * completes the task request. enum sci_status
 */
static enum sci_status scic_sds_remote_device_resetting_state_complete_request_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request)
{
	enum sci_status status = SCI_SUCCESS;
	struct scic_sds_request *this_request = (struct scic_sds_request *)request;
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)device;

	status = scic_sds_request_complete(this_request);

	if (status == SCI_SUCCESS) {
		status = scic_sds_port_complete_io(
			scic_sds_remote_device_get_port(this_device), this_device, this_request);

		if (status == SCI_SUCCESS) {
			scic_sds_remote_device_decrement_request_count(this_device);
		}
	}

	return status;
}

/*
 * *****************************************************************************
 * *  FAILED STATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @device: The struct sci_base_remote_device which is to be cast into a
 *    struct scic_sds_remote_device object.
 *
 * This method handles the remove request for a failed struct scic_sds_remote_device
 * object. The method will transition the device object to the
 * SCIC_BASE_REMOTE_DEVICE_STATE_STOPPING. enum sci_status SCI_SUCCESS
 */

/* --------------------------------------------------------------------------- */

static const struct scic_sds_remote_device_state_handler scic_sds_remote_device_state_handler_table[] = {
	[SCI_BASE_REMOTE_DEVICE_STATE_INITIAL] = {
		.parent.start_handler		= scic_sds_remote_device_default_start_handler,
		.parent.stop_handler		= scic_sds_remote_device_default_stop_handler,
		.parent.fail_handler		= scic_sds_remote_device_default_fail_handler,
		.parent.destruct_handler	= scic_sds_remote_device_default_destruct_handler,
		.parent.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.parent.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.parent.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.parent.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.parent.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_default_event_handler,
		.frame_handler			= scic_sds_remote_device_default_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STOPPED] = {
		.parent.start_handler		= scic_sds_remote_device_stopped_state_start_handler,
		.parent.stop_handler		= scic_sds_remote_device_stopped_state_stop_handler,
		.parent.fail_handler		= scic_sds_remote_device_default_fail_handler,
		.parent.destruct_handler	= scic_sds_remote_device_stopped_state_destruct_handler,
		.parent.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.parent.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.parent.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.parent.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.parent.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_default_event_handler,
		.frame_handler			= scic_sds_remote_device_default_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STARTING] = {
		.parent.start_handler		= scic_sds_remote_device_default_start_handler,
		.parent.stop_handler		= scic_sds_remote_device_starting_state_stop_handler,
		.parent.fail_handler		= scic_sds_remote_device_default_fail_handler,
		.parent.destruct_handler	= scic_sds_remote_device_default_destruct_handler,
		.parent.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.parent.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.parent.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.parent.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.parent.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_general_event_handler,
		.frame_handler			= scic_sds_remote_device_default_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_READY] = {
		.parent.start_handler		= scic_sds_remote_device_default_start_handler,
		.parent.stop_handler		= scic_sds_remote_device_ready_state_stop_handler,
		.parent.fail_handler		= scic_sds_remote_device_default_fail_handler,
		.parent.destruct_handler	= scic_sds_remote_device_default_destruct_handler,
		.parent.reset_handler		= scic_sds_remote_device_ready_state_reset_handler,
		.parent.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.parent.start_io_handler	= scic_sds_remote_device_ready_state_start_io_handler,
		.parent.complete_io_handler	= scic_sds_remote_device_ready_state_complete_request_handler,
		.parent.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.parent.start_task_handler	= scic_sds_remote_device_ready_state_start_task_handler,
		.parent.complete_task_handler	= scic_sds_remote_device_ready_state_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_general_event_handler,
		.frame_handler			= scic_sds_remote_device_general_frame_handler,
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STOPPING] = {
		.parent.start_handler		= scic_sds_remote_device_default_start_handler,
		.parent.stop_handler		= scic_sds_remote_device_stopping_state_stop_handler,
		.parent.fail_handler		= scic_sds_remote_device_default_fail_handler,
		.parent.destruct_handler	= scic_sds_remote_device_default_destruct_handler,
		.parent.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.parent.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.parent.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_io_handler	= scic_sds_remote_device_stopping_state_complete_request_handler,
		.parent.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.parent.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_task_handler	= scic_sds_remote_device_stopping_state_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_general_event_handler,
		.frame_handler			= scic_sds_remote_device_general_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_FAILED] = {
		.parent.start_handler		= scic_sds_remote_device_default_start_handler,
		.parent.stop_handler		= scic_sds_remote_device_default_stop_handler,
		.parent.fail_handler		= scic_sds_remote_device_default_fail_handler,
		.parent.destruct_handler	= scic_sds_remote_device_default_destruct_handler,
		.parent.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.parent.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.parent.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.parent.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.parent.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_default_event_handler,
		.frame_handler			= scic_sds_remote_device_general_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_RESETTING] = {
		.parent.start_handler		= scic_sds_remote_device_default_start_handler,
		.parent.stop_handler		= scic_sds_remote_device_resetting_state_stop_handler,
		.parent.fail_handler		= scic_sds_remote_device_default_fail_handler,
		.parent.destruct_handler	= scic_sds_remote_device_default_destruct_handler,
		.parent.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.parent.reset_complete_handler	= scic_sds_remote_device_resetting_state_reset_complete_handler,
		.parent.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_io_handler	= scic_sds_remote_device_resetting_state_complete_request_handler,
		.parent.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.parent.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_task_handler	= scic_sds_remote_device_resetting_state_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_default_event_handler,
		.frame_handler			= scic_sds_remote_device_general_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_FINAL] = {
		.parent.start_handler		= scic_sds_remote_device_default_start_handler,
		.parent.stop_handler		= scic_sds_remote_device_default_stop_handler,
		.parent.fail_handler		= scic_sds_remote_device_default_fail_handler,
		.parent.destruct_handler	= scic_sds_remote_device_default_destruct_handler,
		.parent.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.parent.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.parent.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.parent.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.parent.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_default_event_handler,
		.frame_handler			= scic_sds_remote_device_default_frame_handler
	}
};

/**
 *
 * @object: This is the struct sci_base_object that is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_INITIAL it
 * immediatly transitions the remote device object to the stopped state. none
 */
static void scic_sds_remote_device_initial_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_remote_device *sci_dev = (struct scic_sds_remote_device *)object;

	sci_dev = container_of(object, typeof(*sci_dev), parent.parent);
	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCI_BASE_REMOTE_DEVICE_STATE_INITIAL);

	/* Initial state is a transitional state to the stopped state */
	sci_base_state_machine_change_state(&sci_dev->parent.state_machine,
					    SCI_BASE_REMOTE_DEVICE_STATE_STOPPED);
}

/**
 *
 * @object: This is the struct sci_base_object that is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the enter function for the SCI_BASE_REMOTE_DEVICE_STATE_INITIAL it
 * sets the stopped state handlers and if this state is entered from the
 * SCI_BASE_REMOTE_DEVICE_STATE_STOPPING then the SCI User is informed that the
 * device stop is complete. none
 */
static void scic_sds_remote_device_stopped_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_remote_device *sci_dev;
	struct scic_sds_controller *scic;
	struct isci_remote_device *idev;
	struct isci_host *ihost;
	u32 prev_state;

	sci_dev = container_of(object, typeof(*sci_dev), parent.parent);
	scic = scic_sds_remote_device_get_controller(sci_dev);
	ihost = sci_object_get_association(scic);
	idev = sci_object_get_association(sci_dev);

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCI_BASE_REMOTE_DEVICE_STATE_STOPPED);

	/* If we are entering from the stopping state let the SCI User know that
	 * the stop operation has completed.
	 */
	prev_state = sci_dev->parent.state_machine.previous_state_id;
	if (prev_state == SCI_BASE_REMOTE_DEVICE_STATE_STOPPING)
		isci_remote_device_stop_complete(ihost, idev, SCI_SUCCESS);

	scic_sds_controller_remote_device_stopped(scic, sci_dev);
}

/**
 *
 * @object: This is the struct sci_base_object that is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the enter function for the SCI_BASE_REMOTE_DEVICE_STATE_STARTING it
 * sets the starting state handlers, sets the device not ready, and posts the
 * remote node context to the hardware. none
 */
static void scic_sds_remote_device_starting_state_enter(struct sci_base_object *object)
{
	struct scic_sds_remote_device *sci_dev = container_of(object, typeof(*sci_dev),
							      parent.parent);
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);
	struct isci_host *ihost = sci_object_get_association(scic);
	struct isci_remote_device *idev = sci_object_get_association(sci_dev);

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCI_BASE_REMOTE_DEVICE_STATE_STARTING);

	isci_remote_device_not_ready(ihost, idev,
				     SCIC_REMOTE_DEVICE_NOT_READY_START_REQUESTED);
}

static void scic_sds_remote_device_starting_state_exit(struct sci_base_object *object)
{
	struct scic_sds_remote_device *sci_dev = container_of(object, typeof(*sci_dev),
							      parent.parent);
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);
	struct isci_host *ihost = sci_object_get_association(scic);
	struct isci_remote_device *idev = sci_object_get_association(sci_dev);

	/*
	 * @todo Check the device object for the proper return code for this
	 * callback
	 */
	isci_remote_device_start_complete(ihost, idev, SCI_SUCCESS);
}

/**
 *
 * @object: This is the struct sci_base_object that is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the enter function for the SCI_BASE_REMOTE_DEVICE_STATE_READY it sets
 * the ready state handlers, and starts the ready substate machine. none
 */
static void scic_sds_remote_device_ready_state_enter(struct sci_base_object *object)
{
	struct scic_sds_remote_device *sci_dev = container_of(object, typeof(*sci_dev),
							      parent.parent);
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);
	struct isci_host *ihost = sci_object_get_association(scic);
	struct isci_remote_device *idev = sci_object_get_association(sci_dev);

	SET_STATE_HANDLER(sci_dev,
			  scic_sds_remote_device_state_handler_table,
			  SCI_BASE_REMOTE_DEVICE_STATE_READY);

	scic->remote_device_sequence[sci_dev->rnc->remote_node_index]++;

	if (sci_dev->has_ready_substate_machine)
		sci_base_state_machine_start(&sci_dev->ready_substate_machine);
	else
		isci_remote_device_ready(ihost, idev);
}

/**
 *
 * @object: This is the struct sci_base_object that is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the exit function for the SCI_BASE_REMOTE_DEVICE_STATE_READY it does
 * nothing. none
 */
static void scic_sds_remote_device_ready_state_exit(
	struct sci_base_object *object)
{
	struct scic_sds_remote_device *sci_dev = container_of(object, typeof(*sci_dev),
							      parent.parent);
	if (sci_dev->has_ready_substate_machine)
		sci_base_state_machine_stop(&sci_dev->ready_substate_machine);
	else {
		struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);
		struct isci_host *ihost = sci_object_get_association(scic);
		struct isci_remote_device *idev = sci_object_get_association(sci_dev);

		isci_remote_device_not_ready(ihost, idev,
					     SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED);
	}
}

/**
 *
 * @object: This is the struct sci_base_object that is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_STOPPING it
 * sets the stopping state handlers and posts an RNC invalidate request to the
 * SCU hardware. none
 */
static void scic_sds_remote_device_stopping_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)object;

	SET_STATE_HANDLER(
		this_device,
		scic_sds_remote_device_state_handler_table,
		SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
		);
}

/**
 *
 * @object: This is the struct sci_base_object that is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_FAILED it sets
 * the stopping state handlers. none
 */
static void scic_sds_remote_device_failed_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)object;

	SET_STATE_HANDLER(
		this_device,
		scic_sds_remote_device_state_handler_table,
		SCI_BASE_REMOTE_DEVICE_STATE_FAILED
		);
}

/**
 *
 * @object: This is the struct sci_base_object that is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_RESETTING it
 * sets the resetting state handlers. none
 */
static void scic_sds_remote_device_resetting_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)object;

	SET_STATE_HANDLER(
		this_device,
		scic_sds_remote_device_state_handler_table,
		SCI_BASE_REMOTE_DEVICE_STATE_RESETTING
		);

	scic_sds_remote_node_context_suspend(
		this_device->rnc, SCI_SOFTWARE_SUSPENSION, NULL, NULL);
}

/**
 *
 * @object: This is the struct sci_base_object that is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the exit method for the SCI_BASE_REMOTE_DEVICE_STATE_RESETTING it
 * does nothing. none
 */
static void scic_sds_remote_device_resetting_state_exit(
	struct sci_base_object *object)
{
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)object;

	scic_sds_remote_node_context_resume(this_device->rnc, NULL, NULL);
}

/**
 *
 * @object: This is the struct sci_base_object that is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the enter method for the SCI_BASE_REMOTE_DEVICE_STATE_FINAL it sets
 * the final state handlers. none
 */
static void scic_sds_remote_device_final_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)object;

	SET_STATE_HANDLER(
		this_device,
		scic_sds_remote_device_state_handler_table,
		SCI_BASE_REMOTE_DEVICE_STATE_FINAL
		);
}

/* --------------------------------------------------------------------------- */

static const struct sci_base_state scic_sds_remote_device_state_table[] = {
	[SCI_BASE_REMOTE_DEVICE_STATE_INITIAL] = {
		.enter_state = scic_sds_remote_device_initial_state_enter,
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STOPPED] = {
		.enter_state = scic_sds_remote_device_stopped_state_enter,
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STARTING] = {
		.enter_state = scic_sds_remote_device_starting_state_enter,
		.exit_state  = scic_sds_remote_device_starting_state_exit
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_READY] = {
		.enter_state = scic_sds_remote_device_ready_state_enter,
		.exit_state  = scic_sds_remote_device_ready_state_exit
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STOPPING] = {
		.enter_state = scic_sds_remote_device_stopping_state_enter,
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_FAILED] = {
		.enter_state = scic_sds_remote_device_failed_state_enter,
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_RESETTING] = {
		.enter_state = scic_sds_remote_device_resetting_state_enter,
		.exit_state  = scic_sds_remote_device_resetting_state_exit
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_FINAL] = {
		.enter_state = scic_sds_remote_device_final_state_enter,
	},
};

void scic_remote_device_construct(struct scic_sds_port *sci_port,
				  struct scic_sds_remote_device *sci_dev)
{
	sci_dev->owning_port = sci_port;
	sci_dev->started_request_count = 0;
	sci_dev->rnc = (struct scic_sds_remote_node_context *) &sci_dev[1];

	sci_base_remote_device_construct(
		&sci_dev->parent,
		scic_sds_remote_device_state_table
		);

	scic_sds_remote_node_context_construct(
		sci_dev,
		sci_dev->rnc,
		SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX
		);

	sci_object_set_association(sci_dev->rnc, sci_dev);
}
