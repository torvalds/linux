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
#include "sci_base_state_machine.h"
#include "scic_controller.h"
#include "scic_remote_device.h"
#include "scic_sds_controller.h"
#include "scic_sds_remote_device.h"
#include "scic_sds_request.h"
#include "scic_sds_smp_request.h"
#include "sci_environment.h"
#include "sci_util.h"
#include "scu_completion_codes.h"
#include "scu_task_context.h"

static void scu_smp_request_construct_task_context(
	struct scic_sds_request *this_request,
	struct smp_request *smp_request);

/**
 *
 *
 * This method return the memory space required for STP PIO requests. u32
 */
u32 scic_sds_smp_request_get_object_size(void)
{
	return sizeof(struct scic_sds_request)
	       + sizeof(struct smp_request)
	       + sizeof(struct smp_response)
	       + sizeof(struct scu_task_context)
	       + SMP_CACHE_BYTES;
}

/**
 * scic_sds_smp_request_get_command_buffer() -
 *
 * This macro returns the address of the smp command buffer in the smp request
 * memory. No need to cast to SMP request type.
 */
#define scic_sds_smp_request_get_command_buffer(memory)	\
	(((char *)(memory)) + sizeof(struct scic_sds_request))

/**
 * scic_sds_smp_request_get_response_buffer() -
 *
 * This macro returns the address of the smp response buffer in the smp request
 * memory.
 */
#define scic_sds_smp_request_get_response_buffer(memory) \
	(((char *)(scic_sds_smp_request_get_command_buffer(memory))) \
	 + sizeof(struct smp_request))

/**
 * scic_sds_smp_request_get_task_context_buffer() -
 *
 * This macro returs the task context buffer for the SMP request.
 */
#define scic_sds_smp_request_get_task_context_buffer(memory) \
	((struct scu_task_context *)(\
		 ((char *)(scic_sds_smp_request_get_response_buffer(memory))) \
		 + sizeof(struct smp_response) \
		 ))



/**
 * This method build the remainder of the IO request object.
 * @this_request: This parameter specifies the request object being constructed.
 *
 * The scic_sds_general_request_construct() must be called before this call is
 * valid. none
 */

void scic_sds_smp_request_assign_buffers(
	struct scic_sds_request *this_request)
{
	/* Assign all of the buffer pointers */
	this_request->command_buffer =
		scic_sds_smp_request_get_command_buffer(this_request);
	this_request->response_buffer =
		scic_sds_smp_request_get_response_buffer(this_request);
	this_request->sgl_element_pair_buffer = NULL;

	if (this_request->was_tag_assigned_by_user == false) {
		this_request->task_context_buffer =
			scic_sds_smp_request_get_task_context_buffer(this_request);
		this_request->task_context_buffer =
			PTR_ALIGN(this_request->task_context_buffer, SMP_CACHE_BYTES);
	}

}

/**
 * This method is called by the SCI user to build an SMP pass-through IO
 *    request.
 * @scic_smp_request: This parameter specifies the handle to the io request
 *    object to be built.
 * @passthru_cb: This parameter specifies the pointer to the callback structure
 *    that contains the function pointers
 *
 * - The user must have previously called scic_io_request_construct() on the
 * supplied IO request. Indicate if the controller successfully built the IO
 * request.
 */

/**
 * This method will fill in the SCU Task Context for a SMP request. The
 *    following important settings are utilized: -# task_type ==
 *    SCU_TASK_TYPE_SMP.  This simply indicates that a normal request type
 *    (i.e. non-raw frame) is being utilized to perform task management. -#
 *    control_frame == 1.  This ensures that the proper endianess is set so
 *    that the bytes are transmitted in the right order for a smp request frame.
 * @this_request: This parameter specifies the smp request object being
 *    constructed.
 *
 */
static void scu_smp_request_construct_task_context(
	struct scic_sds_request *sds_request,
	struct smp_request *smp_request)
{
	dma_addr_t dma_addr;
	struct scic_sds_controller *controller;
	struct scic_sds_remote_device *target_device;
	struct scic_sds_port *target_port;
	struct scu_task_context *task_context;

	/* byte swap the smp request. */
	scic_word_copy_with_swap(sds_request->command_buffer,
				 (u32 *)smp_request,
				 sizeof(struct smp_request) / sizeof(u32));

	task_context = scic_sds_request_get_task_context(sds_request);

	controller = scic_sds_request_get_controller(sds_request);
	target_device = scic_sds_request_get_device(sds_request);
	target_port = scic_sds_request_get_port(sds_request);

	/*
	 * Fill in the TC with the its required data
	 * 00h
	 */
	task_context->priority = 0;
	task_context->initiator_request = 1;
	task_context->connection_rate =
		scic_remote_device_get_connection_rate(target_device);
	task_context->protocol_engine_index =
		scic_sds_controller_get_protocol_engine_group(controller);
	task_context->logical_port_index =
		scic_sds_port_get_index(target_port);
	task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_SMP;
	task_context->abort = 0;
	task_context->valid = SCU_TASK_CONTEXT_VALID;
	task_context->context_type = SCU_TASK_CONTEXT_TYPE;

	/* 04h */
	task_context->remote_node_index =
		sds_request->target_device->rnc->remote_node_index;
	task_context->command_code = 0;
	task_context->task_type = SCU_TASK_TYPE_SMP_REQUEST;

	/* 08h */
	task_context->link_layer_control = 0;
	task_context->do_not_dma_ssp_good_response = 1;
	task_context->strict_ordering = 0;
	task_context->control_frame = 1;
	task_context->timeout_enable = 0;
	task_context->block_guard_enable = 0;

	/* 0ch */
	task_context->address_modifier = 0;

	/* 10h */
	task_context->ssp_command_iu_length =
		smp_request->header.request_length;

	/* 14h */
	task_context->transfer_length_bytes = 0;

	/*
	 * 18h ~ 30h, protocol specific
	 * since commandIU has been build by framework at this point, we just
	 * copy the frist DWord from command IU to this location. */
	memcpy((void *)(&task_context->type.smp),
	       sds_request->command_buffer,
	       sizeof(u32));

	/*
	 * 40h
	 * "For SMP you could program it to zero. We would prefer that way
	 * so that done code will be consistent." - Venki
	 */
	task_context->task_phase = 0;

	if (sds_request->was_tag_assigned_by_user) {
		/*
		 * Build the task context now since we have already read
		 * the data
		 */
		sds_request->post_context =
			(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC |
			 (scic_sds_controller_get_protocol_engine_group(
							controller) <<
			  SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) |
			 (scic_sds_port_get_index(target_port) <<
			  SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT) |
			 scic_sds_io_tag_get_index(sds_request->io_tag));
	} else {
		/*
		 * Build the task context now since we have already read
		 * the data.
		 * I/O tag index is not assigned because we have to wait
		 * until we get a TCi.
		 */
		sds_request->post_context =
			(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC |
			 (scic_sds_controller_get_protocol_engine_group(
							controller) <<
			  SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) |
			 (scic_sds_port_get_index(target_port) <<
			  SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT));
	}

	/*
	 * Copy the physical address for the command buffer to the SCU Task
	 * Context command buffer should not contain command header.
	 */
	dma_addr = scic_io_request_get_dma_addr(sds_request,
						(char *)
						(sds_request->command_buffer) +
						sizeof(u32));

	task_context->command_iu_upper = upper_32_bits(dma_addr);
	task_context->command_iu_lower = lower_32_bits(dma_addr);

	/* SMP response comes as UF, so no need to set response IU address. */
	task_context->response_iu_upper = 0;
	task_context->response_iu_lower = 0;
}

/**
 * This method processes an unsolicited frame while the SMP request is waiting
 *    for a response frame.  It will copy the response data, release the
 *    unsolicited frame, and transition the request to the
 *    SCI_BASE_REQUEST_STATE_COMPLETED state.
 * @this_request: This parameter specifies the request for which the
 *    unsolicited frame was received.
 * @frame_index: This parameter indicates the unsolicited frame index that
 *    should contain the response.
 *
 * This method returns an indication of whether the response frame was handled
 * successfully or not. SCI_SUCCESS Currently this value is always returned and
 * indicates successful processing of the TC response.
 */
static enum sci_status scic_sds_smp_request_await_response_frame_handler(
	struct scic_sds_request *this_request,
	u32 frame_index)
{
	enum sci_status status;
	void *frame_header;
	struct smp_response_header *this_frame_header;
	u8 *user_smp_buffer = this_request->response_buffer;

	status = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_request_get_controller(this_request)->uf_control),
		frame_index,
		&frame_header
		);

	/* byte swap the header. */
	scic_word_copy_with_swap(
		(u32 *)user_smp_buffer,
		frame_header,
		sizeof(struct smp_response_header) / sizeof(u32)
		);
	this_frame_header = (struct smp_response_header *)user_smp_buffer;

	if (this_frame_header->smp_frame_type == SMP_FRAME_TYPE_RESPONSE) {
		void *smp_response_buffer;

		status = scic_sds_unsolicited_frame_control_get_buffer(
			&(scic_sds_request_get_controller(this_request)->uf_control),
			frame_index,
			&smp_response_buffer
			);

		scic_word_copy_with_swap(
			(u32 *)(user_smp_buffer + sizeof(struct smp_response_header)),
			smp_response_buffer,
			sizeof(union smp_response_body) / sizeof(u32)
			);
		if (this_frame_header->function == SMP_FUNCTION_DISCOVER) {
			struct smp_response *this_smp_response;

			this_smp_response = (struct smp_response *)user_smp_buffer;

			/*
			 * Some expanders only report an attached SATA device, and
			 * not an STP target.  Since the core depends on the STP
			 * target attribute to correctly build I/O, set the bit now
			 * if necessary. */
			if (this_smp_response->response.discover.protocols.u.bits.attached_sata_device
			    && !this_smp_response->response.discover.protocols.u.bits.attached_stp_target) {
				this_smp_response->response.discover.protocols.u.bits.attached_stp_target = 1;

				dev_dbg(scic_to_dev(this_request->owning_controller),
					"%s: scic_sds_smp_request_await_response_frame_handler(0x%p) Found SATA dev, setting STP bit.\n",
					__func__, this_request);
			}
		}

		/*
		 * Don't need to copy to user space. User instead will refer to
		 * core request's response buffer. */

		/*
		 * copy the smp response to framework smp request's response buffer.
		 * scic_sds_smp_request_copy_response(this_request); */

		scic_sds_request_set_status(
			this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);

		sci_base_state_machine_change_state(
			&this_request->started_substate_machine,
			SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION
			);
	} else {
		/* This was not a response frame why did it get forwarded? */
		dev_err(scic_to_dev(this_request->owning_controller),
			"%s: SCIC SMP Request 0x%p received unexpected frame "
			"%d type 0x%02x\n",
			__func__,
			this_request,
			frame_index,
			this_frame_header->smp_frame_type);

		scic_sds_request_set_status(
			this_request,
			SCU_TASK_DONE_SMP_FRM_TYPE_ERR,
			SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
			);

		sci_base_state_machine_change_state(
			&this_request->parent.state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED
			);
	}

	scic_sds_controller_release_frame(
		this_request->owning_controller, frame_index
		);

	return SCI_SUCCESS;
}


/**
 * This method processes an abnormal TC completion while the SMP request is
 *    waiting for a response frame.  It decides what happened to the IO based
 *    on TC completion status.
 * @this_request: This parameter specifies the request for which the TC
 *    completion was received.
 * @completion_code: This parameter indicates the completion status information
 *    for the TC.
 *
 * Indicate if the tc completion handler was successful. SCI_SUCCESS currently
 * this method always returns success.
 */
static enum sci_status scic_sds_smp_request_await_response_tc_completion_handler(
	struct scic_sds_request *this_request,
	u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		/*
		 * In the AWAIT RESPONSE state, any TC completion is unexpected.
		 * but if the TC has success status, we complete the IO anyway. */
		scic_sds_request_set_status(
			this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);

		sci_base_state_machine_change_state(
			&this_request->parent.state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED
			);
		break;

	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_RESP_TO_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_UFI_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_FRM_TYPE_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_LL_RX_ERR):
		/*
		 * These status has been seen in a specific LSI expander, which sometimes
		 * is not able to send smp response within 2 ms. This causes our hardware
		 * break the connection and set TC completion with one of these SMP_XXX_XX_ERR
		 * status. For these type of error, we ask scic user to retry the request. */
		scic_sds_request_set_status(
			this_request, SCU_TASK_DONE_SMP_RESP_TO_ERR, SCI_FAILURE_RETRY_REQUIRED
			);

		sci_base_state_machine_change_state(
			&this_request->parent.state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED
			);
		break;

	default:
		/*
		 * All other completion status cause the IO to be complete.  If a NAK
		 * was received, then it is up to the user to retry the request. */
		scic_sds_request_set_status(
			this_request,
			SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
			SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
			);

		sci_base_state_machine_change_state(
			&this_request->parent.state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED
			);
		break;
	}

	return SCI_SUCCESS;
}


/**
 * This method processes the completions transport layer (TL) status to
 *    determine if the SMP request was sent successfully. If the SMP request
 *    was sent successfully, then the state for the SMP request transits to
 *    waiting for a response frame.
 * @this_request: This parameter specifies the request for which the TC
 *    completion was received.
 * @completion_code: This parameter indicates the completion status information
 *    for the TC.
 *
 * Indicate if the tc completion handler was successful. SCI_SUCCESS currently
 * this method always returns success.
 */
static enum sci_status scic_sds_smp_request_await_tc_completion_tc_completion_handler(
	struct scic_sds_request *this_request,
	u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_request_set_status(
			this_request, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);

		sci_base_state_machine_change_state(
			&this_request->parent.state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED
			);
		break;

	default:
		/*
		 * All other completion status cause the IO to be complete.  If a NAK
		 * was received, then it is up to the user to retry the request. */
		scic_sds_request_set_status(
			this_request,
			SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
			SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
			);

		sci_base_state_machine_change_state(
			&this_request->parent.state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED
			);
		break;
	}

	return SCI_SUCCESS;
}


static const struct scic_sds_io_request_state_handler scic_sds_smp_request_started_substate_handler_table[] = {
	[SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE] = {
		.parent.start_handler    = scic_sds_request_default_start_handler,
		.parent.abort_handler    = scic_sds_request_started_state_abort_handler,
		.parent.complete_handler = scic_sds_request_default_complete_handler,
		.parent.destruct_handler = scic_sds_request_default_destruct_handler,
		.tc_completion_handler   = scic_sds_smp_request_await_response_tc_completion_handler,
		.event_handler           = scic_sds_request_default_event_handler,
		.frame_handler           = scic_sds_smp_request_await_response_frame_handler,
	},
	[SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION] = {
		.parent.start_handler    = scic_sds_request_default_start_handler,
		.parent.abort_handler    = scic_sds_request_started_state_abort_handler,
		.parent.complete_handler = scic_sds_request_default_complete_handler,
		.parent.destruct_handler = scic_sds_request_default_destruct_handler,
		.tc_completion_handler   =  scic_sds_smp_request_await_tc_completion_tc_completion_handler,
		.event_handler           =  scic_sds_request_default_event_handler,
		.frame_handler           =  scic_sds_request_default_frame_handler,
	}
};

/**
 * This method performs the actions required when entering the
 *    SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_RESPONSE sub-state. This
 *    includes setting the IO request state handlers for this sub-state.
 * @object: This parameter specifies the request object for which the sub-state
 *    change is occuring.
 *
 * none.
 */
static void scic_sds_smp_request_started_await_response_substate_enter(
	struct sci_base_object *object)
{
	struct scic_sds_request *this_request = (struct scic_sds_request *)object;

	SET_STATE_HANDLER(
		this_request,
		scic_sds_smp_request_started_substate_handler_table,
		SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE
		);
}

/**
 * This method performs the actions required when entering the
 *    SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION sub-state.
 *    This includes setting the SMP request state handlers for this sub-state.
 * @object: This parameter specifies the request object for which the sub-state
 *    change is occuring.
 *
 * none.
 */
static void scic_sds_smp_request_started_await_tc_completion_substate_enter(
	struct sci_base_object *object)
{
	struct scic_sds_request *this_request = (struct scic_sds_request *)object;

	SET_STATE_HANDLER(
		this_request,
		scic_sds_smp_request_started_substate_handler_table,
		SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION
		);
}

static const struct sci_base_state scic_sds_smp_request_started_substate_table[] = {
	[SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE] = {
		.enter_state = scic_sds_smp_request_started_await_response_substate_enter,
	},
	[SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION] = {
		.enter_state = scic_sds_smp_request_started_await_tc_completion_substate_enter,
	},
};

/**
 * This method is called by the SCI user to build an SMP IO request.
 *
 * - The user must have previously called scic_io_request_construct() on the
 * supplied IO request. Indicate if the controller successfully built the IO
 * request. SCI_SUCCESS This value is returned if the IO request was
 * successfully built. SCI_FAILURE_UNSUPPORTED_PROTOCOL This value is returned
 * if the remote_device does not support the SMP protocol.
 * SCI_FAILURE_INVALID_ASSOCIATION This value is returned if the user did not
 * properly set the association between the SCIC IO request and the user's IO
 * request.  Please refer to the sci_object_set_association() routine for more
 * information.
 */
enum sci_status scic_io_request_construct_smp(struct scic_sds_request *sci_req)
{
	struct smp_request *smp_req = kmalloc(sizeof(*smp_req), GFP_KERNEL);

	if (!smp_req)
		return SCI_FAILURE_INSUFFICIENT_RESOURCES;

	sci_req->protocol                     = SCIC_SMP_PROTOCOL;
	sci_req->has_started_substate_machine = true;

	/* Construct the started sub-state machine. */
	sci_base_state_machine_construct(
		&sci_req->started_substate_machine,
		&sci_req->parent.parent,
		scic_sds_smp_request_started_substate_table,
		SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE
		);

	/* Construct the SMP SCU Task Context */
	memcpy(smp_req, sci_req->command_buffer, sizeof(*smp_req));

	/*
	 * Look at the SMP requests' header fields; for certain SAS 1.x SMP
	 * functions under SAS 2.0, a zero request length really indicates
	 * a non-zero default length. */
	if (smp_req->header.request_length == 0) {
		switch (smp_req->header.function) {
		case SMP_FUNCTION_DISCOVER:
		case SMP_FUNCTION_REPORT_PHY_ERROR_LOG:
		case SMP_FUNCTION_REPORT_PHY_SATA:
		case SMP_FUNCTION_REPORT_ROUTE_INFORMATION:
			smp_req->header.request_length = 2;
			break;
		case SMP_FUNCTION_CONFIGURE_ROUTE_INFORMATION:
		case SMP_FUNCTION_PHY_CONTROL:
		case SMP_FUNCTION_PHY_TEST:
			smp_req->header.request_length = 9;
			break;
			/* Default - zero is a valid default for 2.0. */
		}
	}

	scu_smp_request_construct_task_context(sci_req, smp_req);

	sci_base_state_machine_change_state(
		&sci_req->parent.state_machine,
		SCI_BASE_REQUEST_STATE_CONSTRUCTED
		);

	kfree(smp_req);

	return SCI_SUCCESS;
}
