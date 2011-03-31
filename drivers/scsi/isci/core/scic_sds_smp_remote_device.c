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

#include "scic_remote_device.h"
#include "scic_sds_controller.h"
#include "scic_sds_port.h"
#include "scic_sds_remote_device.h"
#include "scic_sds_request.h"
#include "sci_environment.h"
#include "sci_util.h"
#include "scu_event_codes.h"
#include "scu_task_context.h"

/*
 * *****************************************************************************
 * *  SMP REMOTE DEVICE READY IDLE SUBSTATE HANDLERS
 * ***************************************************************************** */

/**
 *
 * @[in]: device The device the io is sent to.
 * @[in]: request The io to start.
 *
 * This method will handle the start io operation for a SMP device that is in
 * the idle state. enum sci_status
 */
static enum sci_status scic_sds_smp_remote_device_ready_idle_substate_start_io_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request)
{
	enum sci_status status;
	struct scic_sds_remote_device *this_device = (struct scic_sds_remote_device *)device;
	struct scic_sds_request *io_request  = (struct scic_sds_request *)request;

	/* Will the port allow the io request to start? */
	status = this_device->owning_port->state_handlers->start_io_handler(
		this_device->owning_port,
		this_device,
		io_request
		);

	if (status == SCI_SUCCESS) {
		status =
			scic_sds_remote_node_context_start_io(this_device->rnc, io_request);

		if (status == SCI_SUCCESS) {
			status = scic_sds_request_start(io_request);
		}

		if (status == SCI_SUCCESS) {
			this_device->working_request = io_request;

			sci_base_state_machine_change_state(
				&this_device->ready_substate_machine,
				SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD
				);
		}

		scic_sds_remote_device_start_request(this_device, io_request, status);
	}

	return status;
}


/*
 * ******************************************************************************
 * * SMP REMOTE DEVICE READY SUBSTATE CMD HANDLERS
 * ****************************************************************************** */
/**
 *
 * @device: This is the device object that is receiving the IO.
 * @request: The io to start.
 *
 * This device is already handling a command it can not accept new commands
 * until this one is complete. enum sci_status
 */
static enum sci_status scic_sds_smp_remote_device_ready_cmd_substate_start_io_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request)
{
	return SCI_FAILURE_INVALID_STATE;
}


/**
 * this is the complete_io_handler for smp device at ready cmd substate.
 * @device: This is the device object that is receiving the IO.
 * @request: The io to start.
 *
 * enum sci_status
 */
static enum sci_status scic_sds_smp_remote_device_ready_cmd_substate_complete_io_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request)
{
	enum sci_status status;
	struct scic_sds_remote_device *this_device;
	struct scic_sds_request *the_request;

	this_device = (struct scic_sds_remote_device *)device;
	the_request = (struct scic_sds_request *)request;

	status = scic_sds_io_request_complete(the_request);

	if (status == SCI_SUCCESS) {
		status = scic_sds_port_complete_io(
			this_device->owning_port, this_device, the_request);

		if (status == SCI_SUCCESS) {
			scic_sds_remote_device_decrement_request_count(this_device);
			sci_base_state_machine_change_state(
				&this_device->ready_substate_machine,
				SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE
				);
		} else
			dev_err(scirdev_to_dev(this_device),
				"%s: SCIC SDS Remote Device 0x%p io request "
				"0x%p could not be completd on the port 0x%p "
				"failed with status %d.\n",
				__func__,
				this_device,
				the_request,
				this_device->owning_port,
				status);
	}

	return status;
}

/**
 * This is frame handler for smp device ready cmd substate.
 * @this_device: This is the device object that is receiving the frame.
 * @frame_index: The index for the frame received.
 *
 * enum sci_status
 */
static enum sci_status scic_sds_smp_remote_device_ready_cmd_substate_frame_handler(
	struct scic_sds_remote_device *this_device,
	u32 frame_index)
{
	enum sci_status status;

	/*
	 * / The device does not process any UF received from the hardware while
	 * / in this state.  All unsolicited frames are forwarded to the io request
	 * / object. */
	status = scic_sds_io_request_frame_handler(
		this_device->working_request,
		frame_index
		);

	return status;
}

/* --------------------------------------------------------------------------- */

static const struct scic_sds_remote_device_state_handler scic_sds_smp_remote_device_ready_substate_handler_table[] = {
	[SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE] = {
		.parent.start_handler		= scic_sds_remote_device_default_start_handler,
		.parent.stop_handler		= scic_sds_remote_device_ready_state_stop_handler,
		.parent.fail_handler		= scic_sds_remote_device_default_fail_handler,
		.parent.destruct_handler	= scic_sds_remote_device_default_destruct_handler,
		.parent.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.parent.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.parent.start_io_handler	= scic_sds_smp_remote_device_ready_idle_substate_start_io_handler,
		.parent.complete_io_handler	= scic_sds_remote_device_default_complete_request_handler,
		.parent.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.parent.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_general_event_handler,
		.frame_handler			= scic_sds_remote_device_default_frame_handler
	},
	[SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD] = {
		.parent.start_handler		= scic_sds_remote_device_default_start_handler,
		.parent.stop_handler		= scic_sds_remote_device_ready_state_stop_handler,
		.parent.fail_handler		= scic_sds_remote_device_default_fail_handler,
		.parent.destruct_handler	= scic_sds_remote_device_default_destruct_handler,
		.parent.reset_handler		= scic_sds_remote_device_default_reset_handler,
		.parent.reset_complete_handler	= scic_sds_remote_device_default_reset_complete_handler,
		.parent.start_io_handler	= scic_sds_smp_remote_device_ready_cmd_substate_start_io_handler,
		.parent.complete_io_handler	= scic_sds_smp_remote_device_ready_cmd_substate_complete_io_handler,
		.parent.continue_io_handler	= scic_sds_remote_device_default_continue_request_handler,
		.parent.start_task_handler	= scic_sds_remote_device_default_start_request_handler,
		.parent.complete_task_handler	= scic_sds_remote_device_default_complete_request_handler,
		.suspend_handler		= scic_sds_remote_device_default_suspend_handler,
		.resume_handler			= scic_sds_remote_device_default_resume_handler,
		.event_handler			= scic_sds_remote_device_general_event_handler,
		.frame_handler			= scic_sds_smp_remote_device_ready_cmd_substate_frame_handler
	}
};

/**
 *
 * @object: This is the struct sci_base_object which is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE enter method.
 * This function sets the ready cmd substate handlers and reports the device as
 * ready. none
 */
static void scic_sds_smp_remote_device_ready_idle_substate_enter(struct sci_base_object *object)
{
	struct scic_sds_remote_device *sci_dev = container_of(object, typeof(*sci_dev),
							      parent.parent);
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);
	struct isci_host *ihost = sci_object_get_association(scic);
	struct isci_remote_device *idev = sci_object_get_association(sci_dev);

	SET_STATE_HANDLER(sci_dev,
			  scic_sds_smp_remote_device_ready_substate_handler_table,
			  SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE);

	isci_remote_device_ready(ihost, idev);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD enter method. This
 * function sets the remote device objects ready cmd substate handlers, and
 * notify core user that the device is not ready. none
 */
static void scic_sds_smp_remote_device_ready_cmd_substate_enter(
	struct sci_base_object *object)
{
	struct scic_sds_remote_device *sci_dev = container_of(object, typeof(*sci_dev),
							      parent.parent);
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);
	struct isci_host *ihost = sci_object_get_association(scic);
	struct isci_remote_device *idev = sci_object_get_association(sci_dev);

	BUG_ON(sci_dev->working_request == NULL);

	SET_STATE_HANDLER(sci_dev,
			  scic_sds_smp_remote_device_ready_substate_handler_table,
			  SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD);

	isci_remote_device_not_ready(ihost, idev,
				     SCIC_REMOTE_DEVICE_NOT_READY_SMP_REQUEST_STARTED);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast into a
 *    struct scic_sds_remote_device.
 *
 * This is the SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATE_CMD exit method. none
 */
static void scic_sds_smp_remote_device_ready_cmd_substate_exit(struct sci_base_object *object)
{
	struct scic_sds_remote_device *sci_dev = container_of(object, typeof(*sci_dev),
							      parent.parent);
	sci_dev->working_request = NULL;
}

/* --------------------------------------------------------------------------- */

const struct sci_base_state scic_sds_smp_remote_device_ready_substate_table[] = {
	[SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE] = {
		.enter_state = scic_sds_smp_remote_device_ready_idle_substate_enter,
	},
	[SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD] = {
		.enter_state = scic_sds_smp_remote_device_ready_cmd_substate_enter,
		.exit_state  = scic_sds_smp_remote_device_ready_cmd_substate_exit,
	},
};
