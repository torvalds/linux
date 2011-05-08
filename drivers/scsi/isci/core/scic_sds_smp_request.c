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

#include <scsi/sas.h>
#include "sci_base_state_machine.h"
#include "scic_controller.h"
#include "scic_sds_controller.h"
#include "remote_device.h"
#include "scic_sds_request.h"
#include "scic_sds_smp_request.h"
#include "sci_environment.h"
#include "sci_util.h"
#include "scu_completion_codes.h"
#include "scu_task_context.h"

static void scu_smp_request_construct_task_context(
	struct scic_sds_request *sci_req,
	struct smp_req *smp_req);

/**
 *
 *
 * This method return the memory space required for STP PIO requests. u32
 */
u32 scic_sds_smp_request_get_object_size(void)
{
	return sizeof(struct scic_sds_request)
	       + sizeof(struct smp_req)
	       + sizeof(struct smp_resp);
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
	 + sizeof(struct smp_req))

void scic_sds_smp_request_assign_buffers(struct scic_sds_request *sci_req)
{
	/* Assign all of the buffer pointers */
	sci_req->command_buffer =
		scic_sds_smp_request_get_command_buffer(sci_req);
	sci_req->response_buffer =
		scic_sds_smp_request_get_response_buffer(sci_req);

	if (sci_req->was_tag_assigned_by_user == false)
		sci_req->task_context_buffer = &sci_req->tc;
}

/*
 * This function will fill in the SCU Task Context for a SMP request. The
 *    following important settings are utilized: -# task_type ==
 *    SCU_TASK_TYPE_SMP.  This simply indicates that a normal request type
 *    (i.e. non-raw frame) is being utilized to perform task management. -#
 *    control_frame == 1.  This ensures that the proper endianess is set so
 *    that the bytes are transmitted in the right order for a smp request frame.
 * @sci_req: This parameter specifies the smp request object being
 *    constructed.
 *
 */
static void
scu_smp_request_construct_task_context(struct scic_sds_request *sci_req,
				       struct smp_req *smp_req)
{
	dma_addr_t dma_addr;
	struct scic_sds_controller *scic;
	struct scic_sds_remote_device *sci_dev;
	struct scic_sds_port *sci_port;
	struct scu_task_context *task_context;
	ssize_t word_cnt = sizeof(struct smp_req) / sizeof(u32);

	/* byte swap the smp request. */
	sci_swab32_cpy(sci_req->command_buffer, smp_req,
		       word_cnt);

	task_context = scic_sds_request_get_task_context(sci_req);

	scic = scic_sds_request_get_controller(sci_req);
	sci_dev = scic_sds_request_get_device(sci_req);
	sci_port = scic_sds_request_get_port(sci_req);

	/*
	 * Fill in the TC with the its required data
	 * 00h
	 */
	task_context->priority = 0;
	task_context->initiator_request = 1;
	task_context->connection_rate = sci_dev->connection_rate;
	task_context->protocol_engine_index =
		scic_sds_controller_get_protocol_engine_group(scic);
	task_context->logical_port_index = scic_sds_port_get_index(sci_port);
	task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_SMP;
	task_context->abort = 0;
	task_context->valid = SCU_TASK_CONTEXT_VALID;
	task_context->context_type = SCU_TASK_CONTEXT_TYPE;

	/* 04h */
	task_context->remote_node_index = sci_dev->rnc.remote_node_index;
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
	task_context->ssp_command_iu_length = smp_req->req_len;

	/* 14h */
	task_context->transfer_length_bytes = 0;

	/*
	 * 18h ~ 30h, protocol specific
	 * since commandIU has been build by framework at this point, we just
	 * copy the frist DWord from command IU to this location. */
	memcpy((void *)(&task_context->type.smp),
	       sci_req->command_buffer,
	       sizeof(u32));

	/*
	 * 40h
	 * "For SMP you could program it to zero. We would prefer that way
	 * so that done code will be consistent." - Venki
	 */
	task_context->task_phase = 0;

	if (sci_req->was_tag_assigned_by_user) {
		/*
		 * Build the task context now since we have already read
		 * the data
		 */
		sci_req->post_context =
			(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC |
			 (scic_sds_controller_get_protocol_engine_group(scic) <<
			  SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) |
			 (scic_sds_port_get_index(sci_port) <<
			  SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT) |
			 scic_sds_io_tag_get_index(sci_req->io_tag));
	} else {
		/*
		 * Build the task context now since we have already read
		 * the data.
		 * I/O tag index is not assigned because we have to wait
		 * until we get a TCi.
		 */
		sci_req->post_context =
			(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC |
			 (scic_sds_controller_get_protocol_engine_group(scic) <<
			  SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) |
			 (scic_sds_port_get_index(sci_port) <<
			  SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT));
	}

	/*
	 * Copy the physical address for the command buffer to the SCU Task
	 * Context command buffer should not contain command header.
	 */
	dma_addr = scic_io_request_get_dma_addr(sci_req,
						(char *)
						(sci_req->command_buffer) +
						sizeof(u32));

	task_context->command_iu_upper = upper_32_bits(dma_addr);
	task_context->command_iu_lower = lower_32_bits(dma_addr);

	/* SMP response comes as UF, so no need to set response IU address. */
	task_context->response_iu_upper = 0;
	task_context->response_iu_lower = 0;
}

/*
 * This function processes an unsolicited frame while the SMP request is waiting
 *    for a response frame.  It will copy the response data, release the
 *    unsolicited frame, and transition the request to the
 *    SCI_BASE_REQUEST_STATE_COMPLETED state.
 * @sci_req: This parameter specifies the request for which the
 *    unsolicited frame was received.
 * @frame_index: This parameter indicates the unsolicited frame index that
 *    should contain the response.
 *
 * This function returns an indication of whether the response frame was handled
 * successfully or not. SCI_SUCCESS Currently this value is always returned and
 * indicates successful processing of the TC response.
 */
static enum sci_status
scic_sds_smp_request_await_response_frame_handler(
		struct scic_sds_request *sci_req,
		u32 frame_index)
{
	enum sci_status status;
	void *frame_header;
	struct smp_resp *rsp_hdr;
	u8 *usr_smp_buf = sci_req->response_buffer;
	ssize_t word_cnt = SMP_RESP_HDR_SZ / sizeof(u32);

	status = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_request_get_controller(sci_req)->uf_control),
		frame_index,
		&frame_header);

	/* byte swap the header. */
	sci_swab32_cpy(usr_smp_buf, frame_header, word_cnt);

	rsp_hdr = (struct smp_resp *)usr_smp_buf;

	if (rsp_hdr->frame_type == SMP_RESPONSE) {
		void *smp_resp;

		status = scic_sds_unsolicited_frame_control_get_buffer(
			&(scic_sds_request_get_controller(sci_req)->uf_control),
			frame_index,
			&smp_resp);

		word_cnt = (sizeof(struct smp_req) - SMP_RESP_HDR_SZ) /
			sizeof(u32);

		sci_swab32_cpy(usr_smp_buf + SMP_RESP_HDR_SZ,
			       smp_resp, word_cnt);

		scic_sds_request_set_status(
			sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS);

		sci_base_state_machine_change_state(
			&sci_req->started_substate_machine,
			SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION);
	} else {
		/* This was not a response frame why did it get forwarded? */
		dev_err(scic_to_dev(sci_req->owning_controller),
			"%s: SCIC SMP Request 0x%p received unexpected frame "
			"%d type 0x%02x\n",
			__func__,
			sci_req,
			frame_index,
			rsp_hdr->frame_type);

		scic_sds_request_set_status(
			sci_req,
			SCU_TASK_DONE_SMP_FRM_TYPE_ERR,
			SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR);

		sci_base_state_machine_change_state(
			&sci_req->state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED);
	}

	scic_sds_controller_release_frame(sci_req->owning_controller,
					  frame_index);

	return SCI_SUCCESS;
}


/**
 * This method processes an abnormal TC completion while the SMP request is
 *    waiting for a response frame.  It decides what happened to the IO based
 *    on TC completion status.
 * @sci_req: This parameter specifies the request for which the TC
 *    completion was received.
 * @completion_code: This parameter indicates the completion status information
 *    for the TC.
 *
 * Indicate if the tc completion handler was successful. SCI_SUCCESS currently
 * this method always returns success.
 */
static enum sci_status scic_sds_smp_request_await_response_tc_completion_handler(
	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		/*
		 * In the AWAIT RESPONSE state, any TC completion is unexpected.
		 * but if the TC has success status, we complete the IO anyway. */
		scic_sds_request_set_status(
			sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);

		sci_base_state_machine_change_state(
			&sci_req->state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED);
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
			sci_req, SCU_TASK_DONE_SMP_RESP_TO_ERR, SCI_FAILURE_RETRY_REQUIRED
			);

		sci_base_state_machine_change_state(
			&sci_req->state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED);
		break;

	default:
		/*
		 * All other completion status cause the IO to be complete.  If a NAK
		 * was received, then it is up to the user to retry the request. */
		scic_sds_request_set_status(
			sci_req,
			SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
			SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
			);

		sci_base_state_machine_change_state(
			&sci_req->state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED);
		break;
	}

	return SCI_SUCCESS;
}


/**
 * This method processes the completions transport layer (TL) status to
 *    determine if the SMP request was sent successfully. If the SMP request
 *    was sent successfully, then the state for the SMP request transits to
 *    waiting for a response frame.
 * @sci_req: This parameter specifies the request for which the TC
 *    completion was received.
 * @completion_code: This parameter indicates the completion status information
 *    for the TC.
 *
 * Indicate if the tc completion handler was successful. SCI_SUCCESS currently
 * this method always returns success.
 */
static enum sci_status scic_sds_smp_request_await_tc_completion_tc_completion_handler(
	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_request_set_status(
			sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);

		sci_base_state_machine_change_state(
			&sci_req->state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED);
		break;

	default:
		/*
		 * All other completion status cause the IO to be complete.  If a NAK
		 * was received, then it is up to the user to retry the request. */
		scic_sds_request_set_status(
			sci_req,
			SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
			SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
			);

		sci_base_state_machine_change_state(
			&sci_req->state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED);
		break;
	}

	return SCI_SUCCESS;
}


static const struct scic_sds_io_request_state_handler scic_sds_smp_request_started_substate_handler_table[] = {
	[SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_smp_request_await_response_tc_completion_handler,
		.frame_handler		= scic_sds_smp_request_await_response_frame_handler,
	},
	[SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	=  scic_sds_smp_request_await_tc_completion_tc_completion_handler,
	}
};

/**
 * This method performs the actions required when entering the
 *    SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_RESPONSE sub-state. This
 *    includes setting the IO request state handlers for this sub-state.
 * @object: This parameter specifies the request object for which the sub-state
 *    change is occurring.
 *
 * none.
 */
static void scic_sds_smp_request_started_await_response_substate_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_smp_request_started_substate_handler_table,
		SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE
		);
}

/**
 * This method performs the actions required when entering the
 *    SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION sub-state.
 *    This includes setting the SMP request state handlers for this sub-state.
 * @object: This parameter specifies the request object for which the sub-state
 *    change is occurring.
 *
 * none.
 */
static void scic_sds_smp_request_started_await_tc_completion_substate_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
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
 * request.
 */
enum sci_status scic_io_request_construct_smp(struct scic_sds_request *sci_req)
{
	struct smp_req *smp_req = kmalloc(sizeof(*smp_req), GFP_KERNEL);

	if (!smp_req)
		return SCI_FAILURE_INSUFFICIENT_RESOURCES;

	sci_req->protocol                     = SCIC_SMP_PROTOCOL;
	sci_req->has_started_substate_machine = true;

	/* Construct the started sub-state machine. */
	sci_base_state_machine_construct(
		&sci_req->started_substate_machine,
		sci_req,
		scic_sds_smp_request_started_substate_table,
		SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE
		);

	/* Construct the SMP SCU Task Context */
	memcpy(smp_req, sci_req->command_buffer, sizeof(*smp_req));

	/*
	 * Look at the SMP requests' header fields; for certain SAS 1.x SMP
	 * functions under SAS 2.0, a zero request length really indicates
	 * a non-zero default length. */
	if (smp_req->req_len == 0) {
		switch (smp_req->func) {
		case SMP_DISCOVER:
		case SMP_REPORT_PHY_ERR_LOG:
		case SMP_REPORT_PHY_SATA:
		case SMP_REPORT_ROUTE_INFO:
			smp_req->req_len = 2;
			break;
		case SMP_CONF_ROUTE_INFO:
		case SMP_PHY_CONTROL:
		case SMP_PHY_TEST_FUNCTION:
			smp_req->req_len = 9;
			break;
			/* Default - zero is a valid default for 2.0. */
		}
	}

	scu_smp_request_construct_task_context(sci_req, smp_req);

	sci_base_state_machine_change_state(&sci_req->state_machine,
		SCI_BASE_REQUEST_STATE_CONSTRUCTED);

	kfree(smp_req);

	return SCI_SUCCESS;
}
