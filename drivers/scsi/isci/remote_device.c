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
#include "intel_ata.h"
#include "isci.h"
#include "port.h"
#include "remote_device.h"
#include "request.h"
#include "scic_controller.h"
#include "scic_io_request.h"
#include "scic_phy.h"
#include "scic_port.h"
#include "scic_sds_controller.h"
#include "scic_sds_phy.h"
#include "scic_sds_port.h"
#include "remote_node_context.h"
#include "scic_sds_request.h"
#include "sci_environment.h"
#include "sci_util.h"
#include "scu_event_codes.h"
#include "task.h"

/**
 * isci_remote_device_change_state() - This function gets the status of the
 *    remote_device object.
 * @isci_device: This parameter points to the isci_remote_device object
 *
 * status of the object as a isci_status enum.
 */
void isci_remote_device_change_state(
	struct isci_remote_device *isci_device,
	enum isci_status status)
{
	unsigned long flags;

	spin_lock_irqsave(&isci_device->state_lock, flags);
	isci_device->status = status;
	spin_unlock_irqrestore(&isci_device->state_lock, flags);
}

/**
 * isci_remote_device_not_ready() - This function is called by the scic when
 *    the remote device is not ready. We mark the isci device as ready (not
 *    "ready_for_io") and signal the waiting proccess.
 * @isci_host: This parameter specifies the isci host object.
 * @isci_device: This parameter specifies the remote device
 *
 */
static void isci_remote_device_not_ready(struct isci_host *ihost,
				  struct isci_remote_device *idev, u32 reason)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	if (reason == SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED)
		isci_remote_device_change_state(idev, isci_stopping);
	else
		/* device ready is actually a "not ready for io" state. */
		isci_remote_device_change_state(idev, isci_ready);
}

/**
 * isci_remote_device_ready() - This function is called by the scic when the
 *    remote device is ready. We mark the isci device as ready and signal the
 *    waiting proccess.
 * @ihost: our valid isci_host
 * @idev: remote device
 *
 */
static void isci_remote_device_ready(struct isci_host *ihost, struct isci_remote_device *idev)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p\n", __func__, idev);

	isci_remote_device_change_state(idev, isci_ready_for_io);
	if (test_and_clear_bit(IDEV_START_PENDING, &idev->flags))
		wake_up(&ihost->eventq);
}

/* called once the remote node context is ready to be freed.
 * The remote device can now report that its stop operation is complete. none
 */
static void rnc_destruct_done(void *_dev)
{
	struct scic_sds_remote_device *sci_dev = _dev;

	BUG_ON(sci_dev->started_request_count != 0);
	sci_base_state_machine_change_state(&sci_dev->state_machine,
					    SCI_BASE_REMOTE_DEVICE_STATE_STOPPED);
}

static enum sci_status scic_sds_remote_device_terminate_requests(struct scic_sds_remote_device *sci_dev)
{
	struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;
	u32 i, request_count = sci_dev->started_request_count;
	enum sci_status status  = SCI_SUCCESS;

	for (i = 0; i < SCI_MAX_IO_REQUESTS && i < request_count; i++) {
		struct scic_sds_request *sci_req;
		enum sci_status s;

		sci_req = scic->io_request_table[i];
		if (!sci_req || sci_req->target_device != sci_dev)
			continue;
		s = scic_controller_terminate_request(scic, sci_dev, sci_req);
		if (s != SCI_SUCCESS)
			status = s;
	}

	return status;
}

enum sci_status scic_remote_device_stop(struct scic_sds_remote_device *sci_dev,
					u32 timeout)
{
	struct sci_base_state_machine *sm = &sci_dev->state_machine;
	enum scic_sds_remote_device_states state = sm->current_state_id;

	switch (state) {
	case SCI_BASE_REMOTE_DEVICE_STATE_INITIAL:
	case SCI_BASE_REMOTE_DEVICE_STATE_FAILED:
	case SCI_BASE_REMOTE_DEVICE_STATE_FINAL:
	default:
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	case SCI_BASE_REMOTE_DEVICE_STATE_STOPPED:
		return SCI_SUCCESS;
	case SCI_BASE_REMOTE_DEVICE_STATE_STARTING:
		/* device not started so there had better be no requests */
		BUG_ON(sci_dev->started_request_count != 0);
		scic_sds_remote_node_context_destruct(&sci_dev->rnc,
						      rnc_destruct_done, sci_dev);
		/* Transition to the stopping state and wait for the
		 * remote node to complete being posted and invalidated.
		 */
		sci_base_state_machine_change_state(sm, SCI_BASE_REMOTE_DEVICE_STATE_STOPPING);
		return SCI_SUCCESS;
	case SCI_BASE_REMOTE_DEVICE_STATE_READY:
	case SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE:
	case SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD:
	case SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ:
	case SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR:
	case SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET:
	case SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE:
	case SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD:
		sci_base_state_machine_change_state(sm, SCI_BASE_REMOTE_DEVICE_STATE_STOPPING);
		if (sci_dev->started_request_count == 0) {
			scic_sds_remote_node_context_destruct(&sci_dev->rnc,
							      rnc_destruct_done, sci_dev);
			return SCI_SUCCESS;
		} else
			return scic_sds_remote_device_terminate_requests(sci_dev);
		break;
	case SCI_BASE_REMOTE_DEVICE_STATE_STOPPING:
		/* All requests should have been terminated, but if there is an
		 * attempt to stop a device already in the stopping state, then
		 * try again to terminate.
		 */
		return scic_sds_remote_device_terminate_requests(sci_dev);
	case SCI_BASE_REMOTE_DEVICE_STATE_RESETTING:
		sci_base_state_machine_change_state(sm, SCI_BASE_REMOTE_DEVICE_STATE_STOPPING);
		return SCI_SUCCESS;
	}
}


enum sci_status scic_remote_device_reset(
	struct scic_sds_remote_device *sci_dev)
{
	return sci_dev->state_handlers->reset_handler(sci_dev);
}


enum sci_status scic_remote_device_reset_complete(
	struct scic_sds_remote_device *sci_dev)
{
	return sci_dev->state_handlers->reset_complete_handler(sci_dev);
}

#define SCIC_SDS_REMOTE_DEVICE_MINIMUM_TIMER_COUNT (0)
#define SCIC_SDS_REMOTE_DEVICE_MAXIMUM_TIMER_COUNT (SCI_MAX_REMOTE_DEVICES)

/**
 *
 * @sci_dev: The remote device for which the suspend is being requested.
 *
 * This method invokes the remote device suspend state handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_suspend(
	struct scic_sds_remote_device *sci_dev,
	u32 suspend_type)
{
	return sci_dev->state_handlers->suspend_handler(sci_dev, suspend_type);
}

/**
 *
 * @sci_dev: The remote device for which the event handling is being
 *    requested.
 * @frame_index: This is the frame index that is being processed.
 *
 * This method invokes the frame handler for the remote device state machine
 * enum sci_status
 */
enum sci_status scic_sds_remote_device_frame_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 frame_index)
{
	return sci_dev->state_handlers->frame_handler(sci_dev, frame_index);
}

/**
 *
 * @sci_dev: The remote device for which the event handling is being
 *    requested.
 * @event_code: This is the event code that is to be processed.
 *
 * This method invokes the remote device event handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_event_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 event_code)
{
	return sci_dev->state_handlers->event_handler(sci_dev, event_code);
}

/**
 *
 * @controller: The controller that is starting the io request.
 * @sci_dev: The remote device for which the start io handling is being
 *    requested.
 * @io_request: The io request that is being started.
 *
 * This method invokes the remote device start io handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_start_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *io_request)
{
	return sci_dev->state_handlers->start_io_handler(
		       sci_dev, io_request);
}

/**
 *
 * @controller: The controller that is completing the io request.
 * @sci_dev: The remote device for which the complete io handling is being
 *    requested.
 * @io_request: The io request that is being completed.
 *
 * This method invokes the remote device complete io handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_complete_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *io_request)
{
	return sci_dev->state_handlers->complete_io_handler(
		       sci_dev, io_request);
}

/**
 *
 * @controller: The controller that is starting the task request.
 * @sci_dev: The remote device for which the start task handling is being
 *    requested.
 * @io_request: The task request that is being started.
 *
 * This method invokes the remote device start task handler. enum sci_status
 */
enum sci_status scic_sds_remote_device_start_task(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *io_request)
{
	return sci_dev->state_handlers->start_task_handler(
		       sci_dev, io_request);
}

/**
 *
 * @sci_dev:
 * @request:
 *
 * This method takes the request and bulids an appropriate SCU context for the
 * request and then requests the controller to post the request. none
 */
void scic_sds_remote_device_post_request(
	struct scic_sds_remote_device *sci_dev,
	u32 request)
{
	u32 context;

	context = scic_sds_remote_device_build_command_context(sci_dev, request);

	scic_sds_controller_post_request(
		scic_sds_remote_device_get_controller(sci_dev),
		context
		);
}

/* called once the remote node context has transisitioned to a
 * ready state.  This is the indication that the remote device object can also
 * transition to ready.
 */
static void remote_device_resume_done(void *_dev)
{
	struct scic_sds_remote_device *sci_dev = _dev;
	enum scic_sds_remote_device_states state;

	state = sci_dev->state_machine.current_state_id;
	switch (state) {
	case SCI_BASE_REMOTE_DEVICE_STATE_READY:
	case SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE:
	case SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD:
	case SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ:
	case SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR:
	case SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET:
	case SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE:
	case SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD:
		break;
	default:
		/* go 'ready' if we are not already in a ready state */
		sci_base_state_machine_change_state(&sci_dev->state_machine,
						    SCI_BASE_REMOTE_DEVICE_STATE_READY);
		break;
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
static void scic_sds_remote_device_start_request(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *sci_req,
	enum sci_status status)
{
	/* We still have a fault in starting the io complete it on the port */
	if (status == SCI_SUCCESS)
		scic_sds_remote_device_increment_request_count(sci_dev);
	else{
		sci_dev->owning_port->state_handlers->complete_io_handler(
			sci_dev->owning_port, sci_dev, sci_req
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
static void scic_sds_remote_device_continue_request(void *dev)
{
	struct scic_sds_remote_device *sci_dev = dev;

	/* we need to check if this request is still valid to continue. */
	if (sci_dev->working_request)
		scic_controller_continue_io(sci_dev->working_request);
}

static enum sci_status
default_device_handler(struct scic_sds_remote_device *sci_dev,
		       const char *func)
{
	dev_warn(scirdev_to_dev(sci_dev),
		 "%s: in wrong state: %d\n", func,
		 sci_base_state_machine_get_state(&sci_dev->state_machine));
	return SCI_FAILURE_INVALID_STATE;
}

static enum sci_status scic_sds_remote_device_default_reset_handler(
	struct scic_sds_remote_device *sci_dev)
{
	return default_device_handler(sci_dev, __func__);
}

static enum sci_status scic_sds_remote_device_default_reset_complete_handler(
	struct scic_sds_remote_device *sci_dev)
{
	return default_device_handler(sci_dev, __func__);
}

static enum sci_status scic_sds_remote_device_default_suspend_handler(
	struct scic_sds_remote_device *sci_dev, u32 suspend_type)
{
	return default_device_handler(sci_dev, __func__);
}

static enum sci_status scic_sds_remote_device_default_resume_handler(
	struct scic_sds_remote_device *sci_dev)
{
	return default_device_handler(sci_dev, __func__);
}

/**
 *
 * @device: The struct scic_sds_remote_device which is then cast into a
 *    struct scic_sds_remote_device.
 * @event_code: The event code that the struct scic_sds_controller wants the device
 *    object to process.
 *
 * This method is the default event handler.  It will call the RNC state
 * machine handler for any RNC events otherwise it will log a warning and
 * returns a failure. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status  scic_sds_remote_device_core_event_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 event_code,
	bool is_ready_state)
{
	enum sci_status status;

	switch (scu_get_event_type(event_code)) {
	case SCU_EVENT_TYPE_RNC_OPS_MISC:
	case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
	case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
		status = scic_sds_remote_node_context_event_handler(&sci_dev->rnc, event_code);
		break;
	case SCU_EVENT_TYPE_PTX_SCHEDULE_EVENT:

		if (scu_get_event_code(event_code) == SCU_EVENT_IT_NEXUS_TIMEOUT) {
			status = SCI_SUCCESS;

			/* Suspend the associated RNC */
			scic_sds_remote_node_context_suspend(&sci_dev->rnc,
							      SCI_SOFTWARE_SUSPENSION,
							      NULL, NULL);

			dev_dbg(scirdev_to_dev(sci_dev),
				"%s: device: %p event code: %x: %s\n",
				__func__, sci_dev, event_code,
				(is_ready_state)
				? "I_T_Nexus_Timeout event"
				: "I_T_Nexus_Timeout event in wrong state");

			break;
		}
	/* Else, fall through and treat as unhandled... */

	default:
		dev_dbg(scirdev_to_dev(sci_dev),
			"%s: device: %p event code: %x: %s\n",
			__func__, sci_dev, event_code,
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
 * @device: The struct scic_sds_remote_device which is then cast into a
 *    struct scic_sds_remote_device.
 * @event_code: The event code that the struct scic_sds_controller wants the device
 *    object to process.
 *
 * This method is the default event handler.  It will call the RNC state
 * machine handler for any RNC events otherwise it will log a warning and
 * returns a failure. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status  scic_sds_remote_device_default_event_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 event_code)
{
	return scic_sds_remote_device_core_event_handler(sci_dev,
							  event_code,
							  false);
}

/**
 *
 * @device: The struct scic_sds_remote_device which is then cast into a
 *    struct scic_sds_remote_device.
 * @frame_index: The frame index for which the struct scic_sds_controller wants this
 *    device object to process.
 *
 * This method is the default unsolicited frame handler.  It logs a warning,
 * releases the frame and returns a failure. enum sci_status
 * SCI_FAILURE_INVALID_STATE
 */
static enum sci_status scic_sds_remote_device_default_frame_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 frame_index)
{
	dev_warn(scirdev_to_dev(sci_dev),
		 "%s: SCIC Remote Device requested to handle frame %x "
		 "while in wrong state %d\n",
		 __func__,
		 frame_index,
		 sci_base_state_machine_get_state(
			 &sci_dev->state_machine));

	/* Return the frame back to the controller */
	scic_sds_controller_release_frame(
		scic_sds_remote_device_get_controller(sci_dev), frame_index
		);

	return SCI_FAILURE_INVALID_STATE;
}

static enum sci_status scic_sds_remote_device_default_start_request_handler(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *request)
{
	return default_device_handler(sci_dev, __func__);
}

static enum sci_status scic_sds_remote_device_default_complete_request_handler(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *request)
{
	return default_device_handler(sci_dev, __func__);
}

static enum sci_status scic_sds_remote_device_default_continue_request_handler(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *request)
{
	return default_device_handler(sci_dev, __func__);
}

/**
 *
 * @device: The struct scic_sds_remote_device which is then cast into a
 *    struct scic_sds_remote_device.
 * @frame_index: The frame index for which the struct scic_sds_controller wants this
 *    device object to process.
 *
 * This method is a general ssp frame handler.  In most cases the device object
 * needs to route the unsolicited frame processing to the io request object.
 * This method decodes the tag for the io request object and routes the
 * unsolicited frame to that object. enum sci_status SCI_FAILURE_INVALID_STATE
 */
static enum sci_status scic_sds_remote_device_general_frame_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 frame_index)
{
	enum sci_status result;
	struct sci_ssp_frame_header *frame_header;
	struct scic_sds_request *io_request;

	result = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_remote_device_get_controller(sci_dev)->uf_control),
		frame_index,
		(void **)&frame_header
		);

	if (SCI_SUCCESS == result) {
		io_request = scic_sds_controller_get_io_request_from_tag(
			scic_sds_remote_device_get_controller(sci_dev), frame_header->tag);

		if ((io_request == NULL)
		    || (io_request->target_device != sci_dev)) {
			/*
			 * We could not map this tag to a valid IO request
			 * Just toss the frame and continue */
			scic_sds_controller_release_frame(
				scic_sds_remote_device_get_controller(sci_dev), frame_index
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
 * @[in]: sci_dev This is the device object that is receiving the event.
 * @[in]: event_code The event code to process.
 *
 * This is a common method for handling events reported to the remote device
 * from the controller object. enum sci_status
 */
static enum sci_status scic_sds_remote_device_general_event_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 event_code)
{
	return scic_sds_remote_device_core_event_handler(sci_dev,
							  event_code,
							  true);
}

/**
 *
 * @device: The struct scic_sds_remote_device object which is cast to a
 *    struct scic_sds_remote_device object.
 *
 * This is the ready state device reset handler enum sci_status
 */
static enum sci_status scic_sds_remote_device_ready_state_reset_handler(
	struct scic_sds_remote_device *sci_dev)
{
	/* Request the parent state machine to transition to the stopping state */
	sci_base_state_machine_change_state(&sci_dev->state_machine,
					    SCI_BASE_REMOTE_DEVICE_STATE_RESETTING);

	return SCI_SUCCESS;
}

/*
 * This method will attempt to start a task request for this device object. The
 * remote device object will issue the start request for the task and if
 * successful it will start the request for the port object then increment its
 * own requet count. enum sci_status SCI_SUCCESS if the task request is started for
 * this device object. SCI_FAILURE_INSUFFICIENT_RESOURCES if the io request
 * object could not get the resources to start.
 */
static enum sci_status scic_sds_remote_device_ready_state_start_task_handler(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *request)
{
	enum sci_status result;

	/* See if the port is in a state where we can start the IO request */
	result = scic_sds_port_start_io(
		scic_sds_remote_device_get_port(sci_dev), sci_dev, request);

	if (result == SCI_SUCCESS) {
		result = scic_sds_remote_node_context_start_task(&sci_dev->rnc,
								 request);
		if (result == SCI_SUCCESS)
			result = scic_sds_request_start(request);

		scic_sds_remote_device_start_request(sci_dev, request, result);
	}

	return result;
}

/*
 * This method will attempt to start an io request for this device object. The
 * remote device object will issue the start request for the io and if
 * successful it will start the request for the port object then increment its
 * own requet count. enum sci_status SCI_SUCCESS if the io request is started for
 * this device object. SCI_FAILURE_INSUFFICIENT_RESOURCES if the io request
 * object could not get the resources to start.
 */
static enum sci_status scic_sds_remote_device_ready_state_start_io_handler(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *request)
{
	enum sci_status result;

	/* See if the port is in a state where we can start the IO request */
	result = scic_sds_port_start_io(
		scic_sds_remote_device_get_port(sci_dev), sci_dev, request);

	if (result == SCI_SUCCESS) {
		result = scic_sds_remote_node_context_start_io(&sci_dev->rnc, request);
		if (result == SCI_SUCCESS)
			result = scic_sds_request_start(request);

		scic_sds_remote_device_start_request(sci_dev, request, result);
	}

	return result;
}

/*
 * This method will complete the request for the remote device object.  The
 * method will call the completion handler for the request object and if
 * successful it will complete the request on the port object then decrement
 * its own started_request_count. enum sci_status
 */
static enum sci_status scic_sds_remote_device_ready_state_complete_request_handler(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *request)
{
	enum sci_status result;

	result = scic_sds_request_complete(request);

	if (result != SCI_SUCCESS)
		return result;

	/* See if the port is in a state
	 * where we can start the IO request */
	result = scic_sds_port_complete_io(
			scic_sds_remote_device_get_port(sci_dev),
			sci_dev, request);

	if (result == SCI_SUCCESS)
		scic_sds_remote_device_decrement_request_count(sci_dev);

	return result;
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
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *request)
{
	enum sci_status status = SCI_SUCCESS;

	status = scic_sds_request_complete(request);

	if (status != SCI_SUCCESS)
		return status;

	status = scic_sds_port_complete_io(scic_sds_remote_device_get_port(sci_dev),
					   sci_dev, request);
	if (status != SCI_SUCCESS)
		return status;

	scic_sds_remote_device_decrement_request_count(sci_dev);

	if (scic_sds_remote_device_get_request_count(sci_dev) == 0)
		scic_sds_remote_node_context_destruct(&sci_dev->rnc,
						      rnc_destruct_done, sci_dev);
	return SCI_SUCCESS;
}

static enum sci_status scic_sds_remote_device_resetting_state_reset_complete_handler(
	struct scic_sds_remote_device *sci_dev)
{
	sci_base_state_machine_change_state(&sci_dev->state_machine,
					    SCI_BASE_REMOTE_DEVICE_STATE_READY);

	return SCI_SUCCESS;
}

/* complete requests for this device while it is in the
 * SCI_BASE_REMOTE_DEVICE_STATE_RESETTING state. This method calls the complete
 * method for the request object and if that is successful the port object is
 * called to complete the task request. Then the device object itself completes
 * the task request. enum sci_status
 */
static enum sci_status scic_sds_remote_device_resetting_state_complete_request_handler(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *request)
{
	enum sci_status status = SCI_SUCCESS;

	status = scic_sds_request_complete(request);

	if (status == SCI_SUCCESS) {
		status = scic_sds_port_complete_io(
				scic_sds_remote_device_get_port(sci_dev),
				sci_dev, request);

		if (status == SCI_SUCCESS) {
			scic_sds_remote_device_decrement_request_count(sci_dev);
		}
	}

	return status;
}

static enum sci_status scic_sds_stp_remote_device_complete_request(struct scic_sds_remote_device *sci_dev,
								   struct scic_sds_request *sci_req)
{
	enum sci_status status;

	status = scic_sds_io_request_complete(sci_req);
	if (status != SCI_SUCCESS)
		goto out;

	status = scic_sds_port_complete_io(sci_dev->owning_port, sci_dev, sci_req);
	if (status != SCI_SUCCESS)
		goto out;

	scic_sds_remote_device_decrement_request_count(sci_dev);
	if (sci_req->sci_status == SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED) {
		/* This request causes hardware error, device needs to be Lun Reset.
		 * So here we force the state machine to IDLE state so the rest IOs
		 * can reach RNC state handler, these IOs will be completed by RNC with
		 * status of "DEVICE_RESET_REQUIRED", instead of "INVALID STATE".
		 */
		sci_base_state_machine_change_state(&sci_dev->state_machine,
						    SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET);
	} else if (scic_sds_remote_device_get_request_count(sci_dev) == 0)
		sci_base_state_machine_change_state(&sci_dev->state_machine,
						    SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);


 out:
	if (status != SCI_SUCCESS)
		dev_err(scirdev_to_dev(sci_dev),
			"%s: Port:0x%p Device:0x%p Request:0x%p Status:0x%x "
			"could not complete\n", __func__, sci_dev->owning_port,
			sci_dev, sci_req, status);

	return status;
}

/* scic_sds_stp_remote_device_ready_substate_start_request_handler - start stp
 * @device: The target device a task management request towards to.
 * @request: The task request.
 *
 * This is the READY NCQ substate handler to start task management request. In
 * this routine, we suspend and resume the RNC.  enum sci_status Always return
 * SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS status to let
 * controller_start_task_handler know that the controller can't post TC for
 * task request yet, instead, when RNC gets resumed, a controller_continue_task
 * callback will be called.
 */
static enum sci_status scic_sds_stp_remote_device_ready_substate_start_request_handler(
	struct scic_sds_remote_device *device,
	struct scic_sds_request *request)
{
	enum sci_status status;

	/* Will the port allow the io request to start? */
	status = device->owning_port->state_handlers->start_io_handler(
		device->owning_port, device, request);
	if (status != SCI_SUCCESS)
		return status;

	status = scic_sds_remote_node_context_start_task(&device->rnc, request);
	if (status != SCI_SUCCESS)
		goto out;

	status = request->state_handlers->start_handler(request);
	if (status != SCI_SUCCESS)
		goto out;

	/*
	 * Note: If the remote device state is not IDLE this will replace
	 * the request that probably resulted in the task management request.
	 */
	device->working_request = request;
	sci_base_state_machine_change_state(&device->state_machine,
			SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD);

	/*
	 * The remote node context must cleanup the TCi to NCQ mapping table.
	 * The only way to do this correctly is to either write to the TLCR
	 * register or to invalidate and repost the RNC. In either case the
	 * remote node context state machine will take the correct action when
	 * the remote node context is suspended and later resumed.
	 */
	scic_sds_remote_node_context_suspend(&device->rnc,
			SCI_SOFTWARE_SUSPENSION, NULL, NULL);
	scic_sds_remote_node_context_resume(&device->rnc,
			scic_sds_remote_device_continue_request,
			device);

out:
	scic_sds_remote_device_start_request(device, request, status);
	/*
	 * We need to let the controller start request handler know that it can't
	 * post TC yet. We will provide a callback function to post TC when RNC gets
	 * resumed.
	 */
	return SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS;
}

/* handle the start io operation for a sata device that is in the command idle
 * state. - Evalute the type of IO request to be started - If its an NCQ
 * request change to NCQ substate - If its any other command change to the CMD
 * substate
 *
 * If this is a softreset we may want to have a different substate.
 */
static enum sci_status scic_sds_stp_remote_device_ready_idle_substate_start_io_handler(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *request)
{
	enum sci_status status;
	struct isci_request *isci_request = request->ireq;
	enum scic_sds_remote_device_states new_state;

	/* Will the port allow the io request to start? */
	status = sci_dev->owning_port->state_handlers->start_io_handler(
			sci_dev->owning_port, sci_dev, request);
	if (status != SCI_SUCCESS)
		return status;

	status = scic_sds_remote_node_context_start_io(&sci_dev->rnc, request);
	if (status != SCI_SUCCESS)
		goto out;

	status = request->state_handlers->start_handler(request);
	if (status != SCI_SUCCESS)
		goto out;

	if (isci_sata_get_sat_protocol(isci_request) == SAT_PROTOCOL_FPDMA)
		new_state = SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ;
	else {
		sci_dev->working_request = request;
		new_state = SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD;
	}
	sci_base_state_machine_change_state(&sci_dev->state_machine, new_state);
out:
	scic_sds_remote_device_start_request(sci_dev, request, status);
	return status;
}


static enum sci_status scic_sds_stp_remote_device_ready_idle_substate_event_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 event_code)
{
	enum sci_status status;

	status = scic_sds_remote_device_general_event_handler(sci_dev, event_code);
	if (status != SCI_SUCCESS)
		return status;

	/* We pick up suspension events to handle specifically to this state. We
	 * resume the RNC right away. enum sci_status
	 */
	if (scu_get_event_type(event_code) == SCU_EVENT_TYPE_RNC_SUSPEND_TX ||
	    scu_get_event_type(event_code) == SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX)
		status = scic_sds_remote_node_context_resume(&sci_dev->rnc, NULL, NULL);

	return status;
}

static enum sci_status scic_sds_stp_remote_device_ready_ncq_substate_start_io_handler(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *request)
{
	enum sci_status status;
	struct isci_request *isci_request = request->ireq;
	scic_sds_port_io_request_handler_t start_io;

	if (isci_sata_get_sat_protocol(isci_request) == SAT_PROTOCOL_FPDMA) {
		start_io = sci_dev->owning_port->state_handlers->start_io_handler;
		status = start_io(sci_dev->owning_port, sci_dev, request);
		if (status != SCI_SUCCESS)
			return status;

		status = scic_sds_remote_node_context_start_io(&sci_dev->rnc, request);
		if (status == SCI_SUCCESS)
			status = request->state_handlers->start_handler(request);

		scic_sds_remote_device_start_request(sci_dev, request, status);
	} else
		status = SCI_FAILURE_INVALID_STATE;

	return status;
}

static enum sci_status scic_sds_stp_remote_device_ready_ncq_substate_frame_handler(struct scic_sds_remote_device *sci_dev,
										   u32 frame_index)
{
	enum sci_status status;
	struct sata_fis_header *frame_header;
	struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;

	status = scic_sds_unsolicited_frame_control_get_header(&scic->uf_control,
							       frame_index,
							       (void **)&frame_header);
	if (status != SCI_SUCCESS)
		return status;

	if (frame_header->fis_type == SATA_FIS_TYPE_SETDEVBITS &&
	    (frame_header->status & ATA_STATUS_REG_ERROR_BIT)) {
		sci_dev->not_ready_reason = SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED;

		/* TODO Check sactive and complete associated IO if any. */

		sci_base_state_machine_change_state(&sci_dev->state_machine,
						    SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR);
	} else if (frame_header->fis_type == SATA_FIS_TYPE_REGD2H &&
		   (frame_header->status & ATA_STATUS_REG_ERROR_BIT)) {
		/*
		 * Some devices return D2H FIS when an NCQ error is detected.
		 * Treat this like an SDB error FIS ready reason.
		 */
		sci_dev->not_ready_reason = SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED;

		sci_base_state_machine_change_state(&sci_dev->state_machine,
						    SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR);
	} else
		status = SCI_FAILURE;

	scic_sds_controller_release_frame(scic, frame_index);

	return status;
}

static enum sci_status scic_sds_stp_remote_device_ready_cmd_substate_start_io_handler(
	struct scic_sds_remote_device *device,
	struct scic_sds_request *request)
{
	/* device is already handling a command it can not accept new commands
	 * until this one is complete.
	 */
	return SCI_FAILURE_INVALID_STATE;
}

static enum sci_status scic_sds_stp_remote_device_ready_cmd_substate_suspend_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 suspend_type)
{
	enum sci_status status;

	status = scic_sds_remote_node_context_suspend(&sci_dev->rnc,
						      suspend_type, NULL, NULL);

	return status;
}

static enum sci_status scic_sds_stp_remote_device_ready_cmd_substate_frame_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 frame_index)
{
	/* The device doe not process any UF received from the hardware while
	 * in this state.  All unsolicited frames are forwarded to the io
	 * request object.
	 */
	return scic_sds_io_request_frame_handler(sci_dev->working_request,
						 frame_index);
}

static enum sci_status scic_sds_stp_remote_device_ready_await_reset_substate_start_io_handler(
	struct scic_sds_remote_device *device,
	struct scic_sds_request *request)
{
	return SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED;
}

static enum sci_status scic_sds_stp_remote_device_ready_await_reset_substate_complete_request_handler(
	struct scic_sds_remote_device *device,
	struct scic_sds_request *request)
{
	struct scic_sds_request *sci_req = request;
	enum sci_status status;

	status = scic_sds_io_request_complete(sci_req);
	if (status != SCI_SUCCESS)
		goto out;

	status = scic_sds_port_complete_io(device->owning_port, device, sci_req);
	if (status != SCI_SUCCESS)
		goto out;

	scic_sds_remote_device_decrement_request_count(device);
 out:
	if (status != SCI_SUCCESS)
		dev_err(scirdev_to_dev(device),
			"%s: Port:0x%p Device:0x%p Request:0x%p Status:0x%x "
			"could not complete\n",
			__func__, device->owning_port, device, sci_req, status);

	return status;
}

static void scic_sds_stp_remote_device_ready_idle_substate_resume_complete_handler(void *_dev)
{
	struct scic_sds_remote_device *sci_dev = _dev;
	struct isci_remote_device *idev = sci_dev_to_idev(sci_dev);
	struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;

	/* For NCQ operation we do not issue a isci_remote_device_not_ready().
	 * As a result, avoid sending the ready notification.
	 */
	if (sci_dev->state_machine.previous_state_id != SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ)
		isci_remote_device_ready(scic->ihost, idev);
}

static enum sci_status scic_sds_smp_remote_device_ready_idle_substate_start_io_handler(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *sci_req)
{
	enum sci_status status;

	/* Will the port allow the io request to start? */
	status = sci_dev->owning_port->state_handlers->start_io_handler(
			sci_dev->owning_port, sci_dev, sci_req);
	if (status != SCI_SUCCESS)
		return status;

	status = scic_sds_remote_node_context_start_io(&sci_dev->rnc, sci_req);
	if (status != SCI_SUCCESS)
		goto out;

	status = scic_sds_request_start(sci_req);
	if (status != SCI_SUCCESS)
		goto out;

	sci_dev->working_request = sci_req;
	sci_base_state_machine_change_state(&sci_dev->state_machine,
					    SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD);

 out:
	scic_sds_remote_device_start_request(sci_dev, sci_req, status);

	return status;
}

static enum sci_status scic_sds_smp_remote_device_ready_cmd_substate_start_io_handler(
	struct scic_sds_remote_device *device,
	struct scic_sds_request *request)
{
	/* device is already handling a command it can not accept new commands
	 * until this one is complete.
	 */
	return SCI_FAILURE_INVALID_STATE;
}

static enum sci_status
scic_sds_smp_remote_device_ready_cmd_substate_complete_io_handler(struct scic_sds_remote_device *sci_dev,
								  struct scic_sds_request *sci_req)
{
	enum sci_status status;

	status = scic_sds_io_request_complete(sci_req);
	if (status != SCI_SUCCESS)
		return status;

	status = scic_sds_port_complete_io(sci_dev->owning_port, sci_dev, sci_req);
	if (status != SCI_SUCCESS) {
		dev_err(scirdev_to_dev(sci_dev),
			"%s: SCIC SDS Remote Device 0x%p io request "
			"0x%p could not be completd on the port 0x%p "
			"failed with status %d.\n", __func__, sci_dev, sci_req,
			sci_dev->owning_port, status);
		return status;
	}

	scic_sds_remote_device_decrement_request_count(sci_dev);
	sci_base_state_machine_change_state(&sci_dev->state_machine,
					    SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);

	return status;
}

static enum sci_status scic_sds_smp_remote_device_ready_cmd_substate_frame_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 frame_index)
{
	enum sci_status status;

	/* The device does not process any UF received from the hardware while
	 * in this state.  All unsolicited frames are forwarded to the io request
	 * object.
	 */
	status = scic_sds_io_request_frame_handler(
		sci_dev->working_request,
		frame_index
		);

	return status;
}

static const struct scic_sds_remote_device_state_handler scic_sds_remote_device_state_handler_table[] = {
	[SCI_BASE_REMOTE_DEVICE_STATE_INITIAL] = {
		.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler	= scic_sds_remote_device_default_suspend_handler,
		.resume_handler		= scic_sds_remote_device_default_resume_handler,
		.event_handler		= scic_sds_remote_device_default_event_handler,
		.frame_handler		= scic_sds_remote_device_default_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STOPPED] = {
		.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler	= scic_sds_remote_device_default_suspend_handler,
		.resume_handler		= scic_sds_remote_device_default_resume_handler,
		.event_handler		= scic_sds_remote_device_default_event_handler,
		.frame_handler		= scic_sds_remote_device_default_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STARTING] = {
		.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler	= scic_sds_remote_device_default_suspend_handler,
		.resume_handler		= scic_sds_remote_device_default_resume_handler,
		.event_handler		= scic_sds_remote_device_general_event_handler,
		.frame_handler		= scic_sds_remote_device_default_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_READY] = {
		.reset_handler		= scic_sds_remote_device_ready_state_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_remote_device_ready_state_start_io_handler,
		.complete_io_handler	= scic_sds_remote_device_ready_state_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_remote_device_ready_state_start_task_handler,
		.complete_task_handler	= scic_sds_remote_device_ready_state_complete_request_handler,
		.suspend_handler	= scic_sds_remote_device_default_suspend_handler,
		.resume_handler		= scic_sds_remote_device_default_resume_handler,
		.event_handler		= scic_sds_remote_device_general_event_handler,
		.frame_handler		= scic_sds_remote_device_general_frame_handler,
	},
	[SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE] = {
		.reset_handler		= scic_sds_remote_device_ready_state_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_stp_remote_device_ready_idle_substate_start_io_handler,
		.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_stp_remote_device_ready_substate_start_request_handler,
		.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_stp_remote_device_ready_idle_substate_event_handler,
		.frame_handler			= scic_sds_remote_device_default_frame_handler
	},
	[SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD] = {
		.reset_handler		= scic_sds_remote_device_ready_state_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_stp_remote_device_ready_cmd_substate_start_io_handler,
		.complete_io_handler	= scic_sds_stp_remote_device_complete_request,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_stp_remote_device_ready_substate_start_request_handler,
		.complete_task_handler	= scic_sds_stp_remote_device_complete_request,
		.suspend_handler		= scic_sds_stp_remote_device_ready_cmd_substate_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_general_event_handler,
		.frame_handler			= scic_sds_stp_remote_device_ready_cmd_substate_frame_handler
	},
	[SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ] = {
		.reset_handler		= scic_sds_remote_device_ready_state_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_stp_remote_device_ready_ncq_substate_start_io_handler,
		.complete_io_handler	= scic_sds_stp_remote_device_complete_request,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_stp_remote_device_ready_substate_start_request_handler,
		.complete_task_handler	= scic_sds_stp_remote_device_complete_request,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_general_event_handler,
		.frame_handler			= scic_sds_stp_remote_device_ready_ncq_substate_frame_handler
	},
	[SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR] = {
		.reset_handler		= scic_sds_remote_device_ready_state_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_io_handler	= scic_sds_stp_remote_device_complete_request,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_stp_remote_device_ready_substate_start_request_handler,
		.complete_task_handler	= scic_sds_stp_remote_device_complete_request,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_general_event_handler,
		.frame_handler			= scic_sds_remote_device_general_frame_handler
	},
	[SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET] = {
		.reset_handler		= scic_sds_remote_device_ready_state_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_stp_remote_device_ready_await_reset_substate_start_io_handler,
		.complete_io_handler	= scic_sds_stp_remote_device_ready_await_reset_substate_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_stp_remote_device_ready_substate_start_request_handler,
		.complete_task_handler	= scic_sds_stp_remote_device_complete_request,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_general_event_handler,
		.frame_handler			= scic_sds_remote_device_general_frame_handler
	},
	[SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE] = {
		.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_smp_remote_device_ready_idle_substate_start_io_handler,
		.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler	= scic_sds_remote_device_default_suspend_handler,
		.resume_handler		= scic_sds_remote_device_default_resume_handler,
		.event_handler		= scic_sds_remote_device_general_event_handler,
		.frame_handler		= scic_sds_remote_device_default_frame_handler
	},
	[SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD] = {
		.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_smp_remote_device_ready_cmd_substate_start_io_handler,
		.complete_io_handler	= scic_sds_smp_remote_device_ready_cmd_substate_complete_io_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler	= scic_sds_remote_device_default_suspend_handler,
		.resume_handler		= scic_sds_remote_device_default_resume_handler,
		.event_handler		= scic_sds_remote_device_general_event_handler,
		.frame_handler		= scic_sds_smp_remote_device_ready_cmd_substate_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STOPPING] = {
		.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_io_handler	= scic_sds_remote_device_stopping_state_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_task_handler	= scic_sds_remote_device_stopping_state_complete_request_handler,
		.suspend_handler	= scic_sds_remote_device_default_suspend_handler,
		.resume_handler		= scic_sds_remote_device_default_resume_handler,
		.event_handler		= scic_sds_remote_device_general_event_handler,
		.frame_handler		= scic_sds_remote_device_general_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_FAILED] = {
		.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler	= scic_sds_remote_device_default_suspend_handler,
		.resume_handler		= scic_sds_remote_device_default_resume_handler,
		.event_handler		= scic_sds_remote_device_default_event_handler,
		.frame_handler		= scic_sds_remote_device_general_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_RESETTING] = {
		.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_resetting_state_reset_complete_handler,
		.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_io_handler	= scic_sds_remote_device_resetting_state_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_task_handler	= scic_sds_remote_device_resetting_state_complete_request_handler,
		.suspend_handler	= scic_sds_remote_device_default_suspend_handler,
		.resume_handler		= scic_sds_remote_device_default_resume_handler,
		.event_handler		= scic_sds_remote_device_default_event_handler,
		.frame_handler		= scic_sds_remote_device_general_frame_handler
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_FINAL] = {
		.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.start_io_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler	= scic_sds_remote_device_default_suspend_handler,
		.resume_handler		= scic_sds_remote_device_default_resume_handler,
		.event_handler		= scic_sds_remote_device_default_event_handler,
		.frame_handler		= scic_sds_remote_device_default_frame_handler
	}
};

static void scic_sds_remote_device_initial_state_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCI_BASE_REMOTE_DEVICE_STATE_INITIAL);

	/* Initial state is a transitional state to the stopped state */
	sci_base_state_machine_change_state(&sci_dev->state_machine,
					    SCI_BASE_REMOTE_DEVICE_STATE_STOPPED);
}

/**
 * scic_remote_device_destruct() - free remote node context and destruct
 * @remote_device: This parameter specifies the remote device to be destructed.
 *
 * Remote device objects are a limited resource.  As such, they must be
 * protected.  Thus calls to construct and destruct are mutually exclusive and
 * non-reentrant. The return value shall indicate if the device was
 * successfully destructed or if some failure occurred. enum sci_status This value
 * is returned if the device is successfully destructed.
 * SCI_FAILURE_INVALID_REMOTE_DEVICE This value is returned if the supplied
 * device isn't valid (e.g. it's already been destoryed, the handle isn't
 * valid, etc.).
 */
static enum sci_status scic_remote_device_destruct(struct scic_sds_remote_device *sci_dev)
{
	struct sci_base_state_machine *sm = &sci_dev->state_machine;
	enum scic_sds_remote_device_states state = sm->current_state_id;
	struct scic_sds_controller *scic;

	if (state != SCI_BASE_REMOTE_DEVICE_STATE_STOPPED) {
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	}

	scic = sci_dev->owning_port->owning_controller;
	scic_sds_controller_free_remote_node_context(scic, sci_dev,
						     sci_dev->rnc.remote_node_index);
	sci_dev->rnc.remote_node_index = SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX;
	sci_base_state_machine_change_state(sm, SCI_BASE_REMOTE_DEVICE_STATE_FINAL);

	return SCI_SUCCESS;
}

/**
 * isci_remote_device_deconstruct() - This function frees an isci_remote_device.
 * @ihost: This parameter specifies the isci host object.
 * @idev: This parameter specifies the remote device to be freed.
 *
 */
static void isci_remote_device_deconstruct(struct isci_host *ihost, struct isci_remote_device *idev)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	/* There should not be any outstanding io's. All paths to
	 * here should go through isci_remote_device_nuke_requests.
	 * If we hit this condition, we will need a way to complete
	 * io requests in process */
	while (!list_empty(&idev->reqs_in_process)) {

		dev_err(&ihost->pdev->dev,
			"%s: ** request list not empty! **\n", __func__);
		BUG();
	}

	scic_remote_device_destruct(&idev->sci);
	idev->domain_dev->lldd_dev = NULL;
	idev->domain_dev = NULL;
	idev->isci_port = NULL;
	list_del_init(&idev->node);

	clear_bit(IDEV_START_PENDING, &idev->flags);
	clear_bit(IDEV_STOP_PENDING, &idev->flags);
	wake_up(&ihost->eventq);
}

/**
 * isci_remote_device_stop_complete() - This function is called by the scic
 *    when the remote device stop has completed. We mark the isci device as not
 *    ready and remove the isci remote device.
 * @ihost: This parameter specifies the isci host object.
 * @idev: This parameter specifies the remote device.
 * @status: This parameter specifies status of the completion.
 *
 */
static void isci_remote_device_stop_complete(struct isci_host *ihost,
					     struct isci_remote_device *idev)
{
	dev_dbg(&ihost->pdev->dev, "%s: complete idev = %p\n", __func__, idev);

	isci_remote_device_change_state(idev, isci_stopped);

	/* after stop, we can tear down resources. */
	isci_remote_device_deconstruct(ihost, idev);
}

static void scic_sds_remote_device_stopped_state_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;
	struct scic_sds_controller *scic;
	struct isci_remote_device *idev;
	struct isci_host *ihost;
	u32 prev_state;

	scic = scic_sds_remote_device_get_controller(sci_dev);
	ihost = scic->ihost;
	idev = sci_dev_to_idev(sci_dev);

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCI_BASE_REMOTE_DEVICE_STATE_STOPPED);

	/* If we are entering from the stopping state let the SCI User know that
	 * the stop operation has completed.
	 */
	prev_state = sci_dev->state_machine.previous_state_id;
	if (prev_state == SCI_BASE_REMOTE_DEVICE_STATE_STOPPING)
		isci_remote_device_stop_complete(ihost, idev);

	scic_sds_controller_remote_device_stopped(scic, sci_dev);
}

static void scic_sds_remote_device_starting_state_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);
	struct isci_host *ihost = scic->ihost;
	struct isci_remote_device *idev = sci_dev_to_idev(sci_dev);

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCI_BASE_REMOTE_DEVICE_STATE_STARTING);

	isci_remote_device_not_ready(ihost, idev,
				     SCIC_REMOTE_DEVICE_NOT_READY_START_REQUESTED);
}

static void scic_sds_remote_device_ready_state_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;
	struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;
	struct domain_device *dev = sci_dev_to_domain(sci_dev);

	SET_STATE_HANDLER(sci_dev,
			  scic_sds_remote_device_state_handler_table,
			  SCI_BASE_REMOTE_DEVICE_STATE_READY);

	scic->remote_device_sequence[sci_dev->rnc.remote_node_index]++;

	if (dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_SATA)) {
		sci_base_state_machine_change_state(&sci_dev->state_machine,
						    SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);
	} else if (dev_is_expander(dev)) {
		sci_base_state_machine_change_state(&sci_dev->state_machine,
						    SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);
	} else
		isci_remote_device_ready(scic->ihost, sci_dev_to_idev(sci_dev));
}

static void scic_sds_remote_device_ready_state_exit(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;
	struct domain_device *dev = sci_dev_to_domain(sci_dev);

	if (dev->dev_type == SAS_END_DEV) {
		struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;
		struct isci_remote_device *idev = sci_dev_to_idev(sci_dev);

		isci_remote_device_not_ready(scic->ihost, idev,
					     SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED);
	}
}

static void scic_sds_remote_device_stopping_state_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;

	SET_STATE_HANDLER(
		sci_dev,
		scic_sds_remote_device_state_handler_table,
		SCI_BASE_REMOTE_DEVICE_STATE_STOPPING
		);
}

static void scic_sds_remote_device_failed_state_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;

	SET_STATE_HANDLER(
		sci_dev,
		scic_sds_remote_device_state_handler_table,
		SCI_BASE_REMOTE_DEVICE_STATE_FAILED
		);
}

static void scic_sds_remote_device_resetting_state_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;

	SET_STATE_HANDLER(
		sci_dev,
		scic_sds_remote_device_state_handler_table,
		SCI_BASE_REMOTE_DEVICE_STATE_RESETTING
		);

	scic_sds_remote_node_context_suspend(
		&sci_dev->rnc, SCI_SOFTWARE_SUSPENSION, NULL, NULL);
}

static void scic_sds_remote_device_resetting_state_exit(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;

	scic_sds_remote_node_context_resume(&sci_dev->rnc, NULL, NULL);
}

static void scic_sds_remote_device_final_state_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;

	SET_STATE_HANDLER(
		sci_dev,
		scic_sds_remote_device_state_handler_table,
		SCI_BASE_REMOTE_DEVICE_STATE_FINAL
		);
}

static void scic_sds_stp_remote_device_ready_idle_substate_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);

	sci_dev->working_request = NULL;
	if (scic_sds_remote_node_context_is_ready(&sci_dev->rnc)) {
		/*
		 * Since the RNC is ready, it's alright to finish completion
		 * processing (e.g. signal the remote device is ready). */
		scic_sds_stp_remote_device_ready_idle_substate_resume_complete_handler(sci_dev);
	} else {
		scic_sds_remote_node_context_resume(&sci_dev->rnc,
			scic_sds_stp_remote_device_ready_idle_substate_resume_complete_handler,
			sci_dev);
	}
}

static void scic_sds_stp_remote_device_ready_cmd_substate_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);

	BUG_ON(sci_dev->working_request == NULL);

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD);

	isci_remote_device_not_ready(scic->ihost, sci_dev_to_idev(sci_dev),
				     SCIC_REMOTE_DEVICE_NOT_READY_SATA_REQUEST_STARTED);
}

static void scic_sds_stp_remote_device_ready_ncq_substate_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ);
}

static void scic_sds_stp_remote_device_ready_ncq_error_substate_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);
	struct isci_remote_device *idev = sci_dev_to_idev(sci_dev);

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR);

	if (sci_dev->not_ready_reason == SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED)
		isci_remote_device_not_ready(scic->ihost, idev,
					     sci_dev->not_ready_reason);
}

static void scic_sds_stp_remote_device_ready_await_reset_substate_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
		SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET);
}

static void scic_sds_smp_remote_device_ready_idle_substate_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);

	isci_remote_device_ready(scic->ihost, sci_dev_to_idev(sci_dev));
}

static void scic_sds_smp_remote_device_ready_cmd_substate_enter(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);

	BUG_ON(sci_dev->working_request == NULL);

	SET_STATE_HANDLER(sci_dev, scic_sds_remote_device_state_handler_table,
			  SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD);

	isci_remote_device_not_ready(scic->ihost, sci_dev_to_idev(sci_dev),
				     SCIC_REMOTE_DEVICE_NOT_READY_SMP_REQUEST_STARTED);
}

static void scic_sds_smp_remote_device_ready_cmd_substate_exit(void *object)
{
	struct scic_sds_remote_device *sci_dev = object;

	sci_dev->working_request = NULL;
}

static const struct sci_base_state scic_sds_remote_device_state_table[] = {
	[SCI_BASE_REMOTE_DEVICE_STATE_INITIAL] = {
		.enter_state = scic_sds_remote_device_initial_state_enter,
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STOPPED] = {
		.enter_state = scic_sds_remote_device_stopped_state_enter,
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_STARTING] = {
		.enter_state = scic_sds_remote_device_starting_state_enter,
	},
	[SCI_BASE_REMOTE_DEVICE_STATE_READY] = {
		.enter_state = scic_sds_remote_device_ready_state_enter,
		.exit_state  = scic_sds_remote_device_ready_state_exit
	},
	[SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE] = {
		.enter_state = scic_sds_stp_remote_device_ready_idle_substate_enter,
	},
	[SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD] = {
		.enter_state = scic_sds_stp_remote_device_ready_cmd_substate_enter,
	},
	[SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ] = {
		.enter_state = scic_sds_stp_remote_device_ready_ncq_substate_enter,
	},
	[SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR] = {
		.enter_state = scic_sds_stp_remote_device_ready_ncq_error_substate_enter,
	},
	[SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET] = {
		.enter_state = scic_sds_stp_remote_device_ready_await_reset_substate_enter,
	},
	[SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE] = {
		.enter_state = scic_sds_smp_remote_device_ready_idle_substate_enter,
	},
	[SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD] = {
		.enter_state = scic_sds_smp_remote_device_ready_cmd_substate_enter,
		.exit_state  = scic_sds_smp_remote_device_ready_cmd_substate_exit,
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

/**
 * scic_remote_device_construct() - common construction
 * @sci_port: SAS/SATA port through which this device is accessed.
 * @sci_dev: remote device to construct
 *
 * This routine just performs benign initialization and does not
 * allocate the remote_node_context which is left to
 * scic_remote_device_[de]a_construct().  scic_remote_device_destruct()
 * frees the remote_node_context(s) for the device.
 */
static void scic_remote_device_construct(struct scic_sds_port *sci_port,
				  struct scic_sds_remote_device *sci_dev)
{
	sci_dev->owning_port = sci_port;
	sci_dev->started_request_count = 0;

	sci_base_state_machine_construct(
		&sci_dev->state_machine,
		sci_dev,
		scic_sds_remote_device_state_table,
		SCI_BASE_REMOTE_DEVICE_STATE_INITIAL
		);

	sci_base_state_machine_start(
		&sci_dev->state_machine
		);

	scic_sds_remote_node_context_construct(&sci_dev->rnc,
					       SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX);
}

/**
 * scic_remote_device_da_construct() - construct direct attached device.
 *
 * The information (e.g. IAF, Signature FIS, etc.) necessary to build
 * the device is known to the SCI Core since it is contained in the
 * scic_phy object.  Remote node context(s) is/are a global resource
 * allocated by this routine, freed by scic_remote_device_destruct().
 *
 * Returns:
 * SCI_FAILURE_DEVICE_EXISTS - device has already been constructed.
 * SCI_FAILURE_UNSUPPORTED_PROTOCOL - e.g. sas device attached to
 * sata-only controller instance.
 * SCI_FAILURE_INSUFFICIENT_RESOURCES - remote node contexts exhausted.
 */
static enum sci_status scic_remote_device_da_construct(struct scic_sds_port *sci_port,
						       struct scic_sds_remote_device *sci_dev)
{
	enum sci_status status;
	struct domain_device *dev = sci_dev_to_domain(sci_dev);

	scic_remote_device_construct(sci_port, sci_dev);

	/*
	 * This information is request to determine how many remote node context
	 * entries will be needed to store the remote node.
	 */
	sci_dev->is_direct_attached = true;
	status = scic_sds_controller_allocate_remote_node_context(sci_port->owning_controller,
								  sci_dev,
								  &sci_dev->rnc.remote_node_index);

	if (status != SCI_SUCCESS)
		return status;

	if (dev->dev_type == SAS_END_DEV || dev->dev_type == SATA_DEV ||
	    (dev->tproto & SAS_PROTOCOL_STP) || dev_is_expander(dev))
		/* pass */;
	else
		return SCI_FAILURE_UNSUPPORTED_PROTOCOL;

	sci_dev->connection_rate = scic_sds_port_get_max_allowed_speed(sci_port);

	/* / @todo Should I assign the port width by reading all of the phys on the port? */
	sci_dev->device_port_width = 1;

	return SCI_SUCCESS;
}

/**
 * scic_remote_device_ea_construct() - construct expander attached device
 *
 * Remote node context(s) is/are a global resource allocated by this
 * routine, freed by scic_remote_device_destruct().
 *
 * Returns:
 * SCI_FAILURE_DEVICE_EXISTS - device has already been constructed.
 * SCI_FAILURE_UNSUPPORTED_PROTOCOL - e.g. sas device attached to
 * sata-only controller instance.
 * SCI_FAILURE_INSUFFICIENT_RESOURCES - remote node contexts exhausted.
 */
static enum sci_status scic_remote_device_ea_construct(struct scic_sds_port *sci_port,
						       struct scic_sds_remote_device *sci_dev)
{
	struct domain_device *dev = sci_dev_to_domain(sci_dev);
	enum sci_status status;

	scic_remote_device_construct(sci_port, sci_dev);

	status = scic_sds_controller_allocate_remote_node_context(sci_port->owning_controller,
								  sci_dev,
								  &sci_dev->rnc.remote_node_index);
	if (status != SCI_SUCCESS)
		return status;

	if (dev->dev_type == SAS_END_DEV || dev->dev_type == SATA_DEV ||
	    (dev->tproto & SAS_PROTOCOL_STP) || dev_is_expander(dev))
		/* pass */;
	else
		return SCI_FAILURE_UNSUPPORTED_PROTOCOL;

	/*
	 * For SAS-2 the physical link rate is actually a logical link
	 * rate that incorporates multiplexing.  The SCU doesn't
	 * incorporate multiplexing and for the purposes of the
	 * connection the logical link rate is that same as the
	 * physical.  Furthermore, the SAS-2 and SAS-1.1 fields overlay
	 * one another, so this code works for both situations. */
	sci_dev->connection_rate = min_t(u16, scic_sds_port_get_max_allowed_speed(sci_port),
					 dev->linkrate);

	/* / @todo Should I assign the port width by reading all of the phys on the port? */
	sci_dev->device_port_width = 1;

	return SCI_SUCCESS;
}

/**
 * scic_remote_device_start() - This method will start the supplied remote
 *    device.  This method enables normal IO requests to flow through to the
 *    remote device.
 * @remote_device: This parameter specifies the device to be started.
 * @timeout: This parameter specifies the number of milliseconds in which the
 *    start operation should complete.
 *
 * An indication of whether the device was successfully started. SCI_SUCCESS
 * This value is returned if the device was successfully started.
 * SCI_FAILURE_INVALID_PHY This value is returned if the user attempts to start
 * the device when there have been no phys added to it.
 */
static enum sci_status scic_remote_device_start(struct scic_sds_remote_device *sci_dev,
						u32 timeout)
{
	struct sci_base_state_machine *sm = &sci_dev->state_machine;
	enum scic_sds_remote_device_states state = sm->current_state_id;
	enum sci_status status;

	if (state != SCI_BASE_REMOTE_DEVICE_STATE_STOPPED) {
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	}

	status = scic_sds_remote_node_context_resume(&sci_dev->rnc,
						     remote_device_resume_done,
						     sci_dev);
	if (status != SCI_SUCCESS)
		return status;

	sci_base_state_machine_change_state(sm, SCI_BASE_REMOTE_DEVICE_STATE_STARTING);

	return SCI_SUCCESS;
}

static enum sci_status isci_remote_device_construct(struct isci_port *iport,
						    struct isci_remote_device *idev)
{
	struct scic_sds_port *sci_port = iport->sci_port_handle;
	struct isci_host *ihost = iport->isci_host;
	struct domain_device *dev = idev->domain_dev;
	enum sci_status status;

	if (dev->parent && dev_is_expander(dev->parent))
		status = scic_remote_device_ea_construct(sci_port, &idev->sci);
	else
		status = scic_remote_device_da_construct(sci_port, &idev->sci);

	if (status != SCI_SUCCESS) {
		dev_dbg(&ihost->pdev->dev, "%s: construct failed: %d\n",
			__func__, status);

		return status;
	}

	/* start the device. */
	status = scic_remote_device_start(&idev->sci, ISCI_REMOTE_DEVICE_START_TIMEOUT);

	if (status != SCI_SUCCESS)
		dev_warn(&ihost->pdev->dev, "remote device start failed: %d\n",
			 status);

	return status;
}

void isci_remote_device_nuke_requests(struct isci_host *ihost, struct isci_remote_device *idev)
{
	DECLARE_COMPLETION_ONSTACK(aborted_task_completion);

	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p\n", __func__, idev);

	/* Cleanup all requests pending for this device. */
	isci_terminate_pending_requests(ihost, idev, terminating);

	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p, done\n", __func__, idev);
}

/**
 * This function builds the isci_remote_device when a libsas dev_found message
 *    is received.
 * @isci_host: This parameter specifies the isci host object.
 * @port: This parameter specifies the isci_port conected to this device.
 *
 * pointer to new isci_remote_device.
 */
static struct isci_remote_device *
isci_remote_device_alloc(struct isci_host *ihost, struct isci_port *iport)
{
	struct isci_remote_device *idev;
	int i;

	for (i = 0; i < SCI_MAX_REMOTE_DEVICES; i++) {
		idev = &ihost->devices[i];
		if (!test_and_set_bit(IDEV_ALLOCATED, &idev->flags))
			break;
	}

	if (i >= SCI_MAX_REMOTE_DEVICES) {
		dev_warn(&ihost->pdev->dev, "%s: failed\n", __func__);
		return NULL;
	}

	if (WARN_ONCE(!list_empty(&idev->reqs_in_process), "found requests in process\n"))
		return NULL;

	if (WARN_ONCE(!list_empty(&idev->node), "found non-idle remote device\n"))
		return NULL;

	isci_remote_device_change_state(idev, isci_freed);

	return idev;
}

/**
 * isci_remote_device_stop() - This function is called internally to stop the
 *    remote device.
 * @isci_host: This parameter specifies the isci host object.
 * @isci_device: This parameter specifies the remote device.
 *
 * The status of the scic request to stop.
 */
enum sci_status isci_remote_device_stop(struct isci_host *ihost, struct isci_remote_device *idev)
{
	enum sci_status status;
	unsigned long flags;

	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	isci_remote_device_change_state(idev, isci_stopping);

	/* Kill all outstanding requests. */
	isci_remote_device_nuke_requests(ihost, idev);

	set_bit(IDEV_STOP_PENDING, &idev->flags);

	spin_lock_irqsave(&ihost->scic_lock, flags);
	status = scic_remote_device_stop(&idev->sci, 50);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/* Wait for the stop complete callback. */
	if (status == SCI_SUCCESS) {
		wait_for_device_stop(ihost, idev);
		clear_bit(IDEV_ALLOCATED, &idev->flags);
	}

	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p - after completion wait\n",
		__func__, idev);

	return status;
}

/**
 * isci_remote_device_gone() - This function is called by libsas when a domain
 *    device is removed.
 * @domain_device: This parameter specifies the libsas domain device.
 *
 */
void isci_remote_device_gone(struct domain_device *dev)
{
	struct isci_host *ihost = dev_to_ihost(dev);
	struct isci_remote_device *idev = dev->lldd_dev;

	dev_dbg(&ihost->pdev->dev,
		"%s: domain_device = %p, isci_device = %p, isci_port = %p\n",
		__func__, dev, idev, idev->isci_port);

	isci_remote_device_stop(ihost, idev);
}


/**
 * isci_remote_device_found() - This function is called by libsas when a remote
 *    device is discovered. A remote device object is created and started. the
 *    function then sleeps until the sci core device started message is
 *    received.
 * @domain_device: This parameter specifies the libsas domain device.
 *
 * status, zero indicates success.
 */
int isci_remote_device_found(struct domain_device *domain_dev)
{
	struct isci_host *isci_host = dev_to_ihost(domain_dev);
	struct isci_port *isci_port;
	struct isci_phy *isci_phy;
	struct asd_sas_port *sas_port;
	struct asd_sas_phy *sas_phy;
	struct isci_remote_device *isci_device;
	enum sci_status status;

	dev_dbg(&isci_host->pdev->dev,
		"%s: domain_device = %p\n", __func__, domain_dev);

	wait_for_start(isci_host);

	sas_port = domain_dev->port;
	sas_phy = list_first_entry(&sas_port->phy_list, struct asd_sas_phy,
				   port_phy_el);
	isci_phy = to_isci_phy(sas_phy);
	isci_port = isci_phy->isci_port;

	/* we are being called for a device on this port,
	 * so it has to come up eventually
	 */
	wait_for_completion(&isci_port->start_complete);

	if ((isci_stopping == isci_port_get_state(isci_port)) ||
	    (isci_stopped == isci_port_get_state(isci_port)))
		return -ENODEV;

	isci_device = isci_remote_device_alloc(isci_host, isci_port);
	if (!isci_device)
		return -ENODEV;

	INIT_LIST_HEAD(&isci_device->node);
	domain_dev->lldd_dev = isci_device;
	isci_device->domain_dev = domain_dev;
	isci_device->isci_port = isci_port;
	isci_remote_device_change_state(isci_device, isci_starting);


	spin_lock_irq(&isci_host->scic_lock);
	list_add_tail(&isci_device->node, &isci_port->remote_dev_list);

	set_bit(IDEV_START_PENDING, &isci_device->flags);
	status = isci_remote_device_construct(isci_port, isci_device);
	spin_unlock_irq(&isci_host->scic_lock);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p\n",
		__func__, isci_device);

	if (status != SCI_SUCCESS) {

		spin_lock_irq(&isci_host->scic_lock);
		isci_remote_device_deconstruct(
			isci_host,
			isci_device
			);
		spin_unlock_irq(&isci_host->scic_lock);
		return -ENODEV;
	}

	/* wait for the device ready callback. */
	wait_for_device_start(isci_host, isci_device);

	return 0;
}
/**
 * isci_device_is_reset_pending() - This function will check if there is any
 *    pending reset condition on the device.
 * @request: This parameter is the isci_device object.
 *
 * true if there is a reset pending for the device.
 */
bool isci_device_is_reset_pending(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device)
{
	struct isci_request *isci_request;
	struct isci_request *tmp_req;
	bool reset_is_pending = false;
	unsigned long flags;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p\n", __func__, isci_device);

	spin_lock_irqsave(&isci_host->scic_lock, flags);

	/* Check for reset on all pending requests. */
	list_for_each_entry_safe(isci_request, tmp_req,
				 &isci_device->reqs_in_process, dev_node) {
		dev_dbg(&isci_host->pdev->dev,
			"%s: isci_device = %p request = %p\n",
			__func__, isci_device, isci_request);

		if (isci_request->ttype == io_task) {
			struct sas_task *task = isci_request_access_task(
				isci_request);

			spin_lock(&task->task_state_lock);
			if (task->task_state_flags & SAS_TASK_NEED_DEV_RESET)
				reset_is_pending = true;
			spin_unlock(&task->task_state_lock);
		}
	}

	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p reset_is_pending = %d\n",
		__func__, isci_device, reset_is_pending);

	return reset_is_pending;
}

/**
 * isci_device_clear_reset_pending() - This function will clear if any pending
 *    reset condition flags on the device.
 * @request: This parameter is the isci_device object.
 *
 * true if there is a reset pending for the device.
 */
void isci_device_clear_reset_pending(struct isci_host *ihost, struct isci_remote_device *idev)
{
	struct isci_request *isci_request;
	struct isci_request *tmp_req;
	unsigned long flags = 0;

	dev_dbg(&ihost->pdev->dev, "%s: idev=%p, ihost=%p\n",
		 __func__, idev, ihost);

	spin_lock_irqsave(&ihost->scic_lock, flags);

	/* Clear reset pending on all pending requests. */
	list_for_each_entry_safe(isci_request, tmp_req,
				 &idev->reqs_in_process, dev_node) {
		dev_dbg(&ihost->pdev->dev, "%s: idev = %p request = %p\n",
			 __func__, idev, isci_request);

		if (isci_request->ttype == io_task) {

			unsigned long flags2;
			struct sas_task *task = isci_request_access_task(
				isci_request);

			spin_lock_irqsave(&task->task_state_lock, flags2);
			task->task_state_flags &= ~SAS_TASK_NEED_DEV_RESET;
			spin_unlock_irqrestore(&task->task_state_lock, flags2);
		}
	}
	spin_unlock_irqrestore(&ihost->scic_lock, flags);
}
