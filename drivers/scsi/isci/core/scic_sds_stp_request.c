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
#include "sas.h"
#include "sci_base_state.h"
#include "sci_base_state_machine.h"
#include "scic_io_request.h"
#include "scic_sds_controller.h"
#include "remote_device.h"
#include "scic_sds_request.h"
#include "scic_sds_stp_pio_request.h"
#include "scic_sds_stp_request.h"
#include "scic_sds_unsolicited_frame_control.h"
#include "sci_environment.h"
#include "sci_util.h"
#include "scu_completion_codes.h"
#include "scu_event_codes.h"
#include "scu_task_context.h"

/**
 * scic_sds_stp_request_get_h2d_reg_buffer() -
 *
 * This macro returns the address of the stp h2d reg fis buffer in the io
 * request memory
 */
#define scic_sds_stp_request_get_h2d_reg_buffer(memory)	\
	((struct host_to_dev_fis *)(\
		 ((char *)(memory)) + sizeof(struct scic_sds_stp_request) \
		 ))

/**
 * scic_sds_stp_request_get_response_buffer() -
 *
 * This macro returns the address of the ssp response iu buffer in the io
 * request memory
 */
#define scic_sds_stp_request_get_response_buffer(memory) \
	((struct dev_to_host_fis *)(\
		 ((char *)(scic_sds_stp_request_get_h2d_reg_buffer(memory))) \
		 + sizeof(struct host_to_dev_fis) \
		 ))

/**
 * scic_sds_stp_request_get_task_context_buffer() -
 *
 * This macro returns the address of the task context buffer in the io request
 * memory
 */
#define scic_sds_stp_request_get_task_context_buffer(memory) \
	((struct scu_task_context *)(\
		 ((char *)(scic_sds_stp_request_get_response_buffer(memory))) \
		 + SSP_RESP_IU_MAX_SIZE \
		 ))

/**
 * scic_sds_stp_request_get_sgl_element_buffer() -
 *
 * This macro returns the address of the sgl elment pairs in the io request
 * memory buffer
 */
#define scic_sds_stp_request_get_sgl_element_buffer(memory) \
	((struct scu_sgl_element_pair *)(\
		 ((char *)(scic_sds_stp_request_get_task_context_buffer(memory))) \
		 + sizeof(struct scu_task_context) \
		 ))

/**
 *
 *
 * This method return the memory space required for STP PIO requests. u32
 */
u32 scic_sds_stp_request_get_object_size(void)
{
	return sizeof(struct scic_sds_stp_request)
	       + sizeof(struct host_to_dev_fis)
	       + sizeof(struct dev_to_host_fis)
	       + sizeof(struct scu_task_context)
	       + SMP_CACHE_BYTES
	       + sizeof(struct scu_sgl_element_pair) * SCU_MAX_SGL_ELEMENT_PAIRS;
}

void scic_sds_stp_request_assign_buffers(struct scic_sds_request *sci_req)
{
	struct scic_sds_stp_request *stp_req =
		container_of(sci_req, typeof(*stp_req), parent);

	sci_req->command_buffer = scic_sds_stp_request_get_h2d_reg_buffer(stp_req);
	sci_req->response_buffer = scic_sds_stp_request_get_response_buffer(stp_req);
	sci_req->sgl_element_pair_buffer = scic_sds_stp_request_get_sgl_element_buffer(stp_req);
	sci_req->sgl_element_pair_buffer = PTR_ALIGN(sci_req->sgl_element_pair_buffer,
						     sizeof(struct scu_sgl_element_pair));

	if (sci_req->was_tag_assigned_by_user == false) {
		sci_req->task_context_buffer =
			scic_sds_stp_request_get_task_context_buffer(stp_req);
		sci_req->task_context_buffer = PTR_ALIGN(sci_req->task_context_buffer,
							 SMP_CACHE_BYTES);
	}
}

/**
 * This method is will fill in the SCU Task Context for any type of SATA
 *    request.  This is called from the various SATA constructors.
 * @sci_req: The general IO request object which is to be used in
 *    constructing the SCU task context.
 * @task_context: The buffer pointer for the SCU task context which is being
 *    constructed.
 *
 * The general io request construction is complete. The buffer assignment for
 * the command buffer is complete. none Revisit task context construction to
 * determine what is common for SSP/SMP/STP task context structures.
 */
static void scu_sata_reqeust_construct_task_context(
	struct scic_sds_request *sds_request,
	struct scu_task_context *task_context)
{
	dma_addr_t dma_addr;
	struct scic_sds_controller *controller;
	struct scic_sds_remote_device *target_device;
	struct scic_sds_port *target_port;

	controller = scic_sds_request_get_controller(sds_request);
	target_device = scic_sds_request_get_device(sds_request);
	target_port = scic_sds_request_get_port(sds_request);

	/* Fill in the TC with the its required data */
	task_context->abort = 0;
	task_context->priority = SCU_TASK_PRIORITY_NORMAL;
	task_context->initiator_request = 1;
	task_context->connection_rate = target_device->connection_rate;
	task_context->protocol_engine_index =
		scic_sds_controller_get_protocol_engine_group(controller);
	task_context->logical_port_index =
		scic_sds_port_get_index(target_port);
	task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_STP;
	task_context->valid = SCU_TASK_CONTEXT_VALID;
	task_context->context_type = SCU_TASK_CONTEXT_TYPE;

	task_context->remote_node_index =
		scic_sds_remote_device_get_index(sds_request->target_device);
	task_context->command_code = 0;

	task_context->link_layer_control = 0;
	task_context->do_not_dma_ssp_good_response = 1;
	task_context->strict_ordering = 0;
	task_context->control_frame = 0;
	task_context->timeout_enable = 0;
	task_context->block_guard_enable = 0;

	task_context->address_modifier = 0;
	task_context->task_phase = 0x01;

	task_context->ssp_command_iu_length =
		(sizeof(struct host_to_dev_fis) - sizeof(u32)) / sizeof(u32);

	/* Set the first word of the H2D REG FIS */
	task_context->type.words[0] = *(u32 *)sds_request->command_buffer;

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
	 * Context. We must offset the command buffer by 4 bytes because the
	 * first 4 bytes are transfered in the body of the TC.
	 */
	dma_addr =
		scic_io_request_get_dma_addr(sds_request,
						(char *)sds_request->
							command_buffer +
							sizeof(u32));

	task_context->command_iu_upper = upper_32_bits(dma_addr);
	task_context->command_iu_lower = lower_32_bits(dma_addr);

	/* SATA Requests do not have a response buffer */
	task_context->response_iu_upper = 0;
	task_context->response_iu_lower = 0;
}

/**
 *
 * @sci_req:
 *
 * This method will perform any general sata request construction. What part of
 * SATA IO request construction is general? none
 */
static void scic_sds_stp_non_ncq_request_construct(
	struct scic_sds_request *sci_req)
{
	sci_req->has_started_substate_machine = true;
}

/**
 *
 * @sci_req: This parameter specifies the request to be constructed as an
 *    optimized request.
 * @optimized_task_type: This parameter specifies whether the request is to be
 *    an UDMA request or a NCQ request. - A value of 0 indicates UDMA. - A
 *    value of 1 indicates NCQ.
 *
 * This method will perform request construction common to all types of STP
 * requests that are optimized by the silicon (i.e. UDMA, NCQ). This method
 * returns an indication as to whether the construction was successful.
 */
static void scic_sds_stp_optimized_request_construct(struct scic_sds_request *sci_req,
						     u8 optimized_task_type,
						     u32 len,
						     enum dma_data_direction dir)
{
	struct scu_task_context *task_context = sci_req->task_context_buffer;

	/* Build the STP task context structure */
	scu_sata_reqeust_construct_task_context(sci_req, task_context);

	/* Copy over the SGL elements */
	scic_sds_request_build_sgl(sci_req);

	/* Copy over the number of bytes to be transfered */
	task_context->transfer_length_bytes = len;

	if (dir == DMA_TO_DEVICE) {
		/*
		 * The difference between the DMA IN and DMA OUT request task type
		 * values are consistent with the difference between FPDMA READ
		 * and FPDMA WRITE values.  Add the supplied task type parameter
		 * to this difference to set the task type properly for this
		 * DATA OUT (WRITE) case. */
		task_context->task_type = optimized_task_type + (SCU_TASK_TYPE_DMA_OUT
								 - SCU_TASK_TYPE_DMA_IN);
	} else {
		/*
		 * For the DATA IN (READ) case, simply save the supplied
		 * optimized task type. */
		task_context->task_type = optimized_task_type;
	}
}

/**
 *
 * @sci_req: This parameter specifies the request to be constructed.
 *
 * This method will construct the STP UDMA request and its associated TC data.
 * This method returns an indication as to whether the construction was
 * successful. SCI_SUCCESS Currently this method always returns this value.
 */
enum sci_status scic_sds_stp_ncq_request_construct(struct scic_sds_request *sci_req,
						   u32 len,
						   enum dma_data_direction dir)
{
	scic_sds_stp_optimized_request_construct(sci_req,
						 SCU_TASK_TYPE_FPDMAQ_READ,
						 len, dir);
	return SCI_SUCCESS;
}

/**
 * scu_stp_raw_request_construct_task_context -
 * @sci_req: This parameter specifies the STP request object for which to
 *    construct a RAW command frame task context.
 * @task_context: This parameter specifies the SCU specific task context buffer
 *    to construct.
 *
 * This method performs the operations common to all SATA/STP requests
 * utilizing the raw frame method. none
 */
static void scu_stp_raw_request_construct_task_context(
	struct scic_sds_stp_request *sci_req,
	struct scu_task_context *task_context)
{
	scu_sata_reqeust_construct_task_context(&sci_req->parent, task_context);

	task_context->control_frame         = 0;
	task_context->priority              = SCU_TASK_PRIORITY_NORMAL;
	task_context->task_type             = SCU_TASK_TYPE_SATA_RAW_FRAME;
	task_context->type.stp.fis_type     = FIS_REGH2D;
	task_context->transfer_length_bytes =
		sizeof(struct host_to_dev_fis) - sizeof(u32);
}

void scic_stp_io_request_set_ncq_tag(
	struct scic_sds_request *req,
	u16 ncq_tag)
{
	/**
	 * @note This could be made to return an error to the user if the user
	 *       attempts to set the NCQ tag in the wrong state.
	 */
	req->task_context_buffer->type.stp.ncq_tag = ncq_tag;
}


void *scic_stp_io_request_get_h2d_reg_address(
	struct scic_sds_request *req)
{
	return req->command_buffer;
}


void *scic_stp_io_request_get_d2h_reg_address(
	struct scic_sds_request *req)
{
	return &((struct scic_sds_stp_request *)req)->d2h_reg_fis;
}

/**
 *
 * @sci_req:
 *
 * Get the next SGL element from the request. - Check on which SGL element pair
 * we are working - if working on SLG pair element A - advance to element B -
 * else - check to see if there are more SGL element pairs for this IO request
 * - if there are more SGL element pairs - advance to the next pair and return
 * element A struct scu_sgl_element*
 */
static struct scu_sgl_element *scic_sds_stp_request_pio_get_next_sgl(struct scic_sds_stp_request *stp_req)
{
	struct scu_sgl_element *current_sgl;
	struct scic_sds_request *sci_req = &stp_req->parent;
	struct scic_sds_request_pio_sgl *pio_sgl = &stp_req->type.pio.request_current;

	if (pio_sgl->sgl_set == SCU_SGL_ELEMENT_PAIR_A) {
		if (pio_sgl->sgl_pair->B.address_lower == 0 &&
		    pio_sgl->sgl_pair->B.address_upper == 0) {
			current_sgl = NULL;
		} else {
			pio_sgl->sgl_set = SCU_SGL_ELEMENT_PAIR_B;
			current_sgl = &pio_sgl->sgl_pair->B;
		}
	} else {
		if (pio_sgl->sgl_pair->next_pair_lower == 0 &&
		    pio_sgl->sgl_pair->next_pair_upper == 0) {
			current_sgl = NULL;
		} else {
			u64 phys_addr;

			phys_addr = pio_sgl->sgl_pair->next_pair_upper;
			phys_addr <<= 32;
			phys_addr |= pio_sgl->sgl_pair->next_pair_lower;

			pio_sgl->sgl_pair = scic_request_get_virt_addr(sci_req, phys_addr);
			pio_sgl->sgl_set = SCU_SGL_ELEMENT_PAIR_A;
			current_sgl = &pio_sgl->sgl_pair->A;
		}
	}

	return current_sgl;
}

/**
 *
 * @sci_req:
 * @completion_code:
 *
 * This method processes a TC completion.  The expected TC completion is for
 * the transmission of the H2D register FIS containing the SATA/STP non-data
 * request. This method always successfully processes the TC completion.
 * SCI_SUCCESS This value is always returned.
 */
static enum sci_status scic_sds_stp_request_non_data_await_h2d_tc_completion_handler(
	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_request_set_status(
			sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);

		sci_base_state_machine_change_state(
			&sci_req->started_substate_machine,
			SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE
			);
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
			&sci_req->state_machine, SCI_BASE_REQUEST_STATE_COMPLETED);
		break;
	}

	return SCI_SUCCESS;
}

/**
 *
 * @request: This parameter specifies the request for which a frame has been
 *    received.
 * @frame_index: This parameter specifies the index of the frame that has been
 *    received.
 *
 * This method processes frames received from the target while waiting for a
 * device to host register FIS.  If a non-register FIS is received during this
 * time, it is treated as a protocol violation from an IO perspective. Indicate
 * if the received frame was processed successfully.
 */
static enum sci_status scic_sds_stp_request_non_data_await_d2h_frame_handler(
	struct scic_sds_request *sci_req,
	u32 frame_index)
{
	enum sci_status status;
	struct dev_to_host_fis *frame_header;
	u32 *frame_buffer;
	struct scic_sds_stp_request *stp_req =
		container_of(sci_req, typeof(*stp_req), parent);

	status = scic_sds_unsolicited_frame_control_get_header(
		&stp_req->parent.owning_controller->uf_control,
		frame_index,
		(void **)&frame_header);

	if (status == SCI_SUCCESS) {
		switch (frame_header->fis_type) {
		case FIS_REGD2H:
			scic_sds_unsolicited_frame_control_get_buffer(
				&stp_req->parent.owning_controller->uf_control,
				frame_index,
				(void **)&frame_buffer);

			scic_sds_controller_copy_sata_response(
				&stp_req->d2h_reg_fis,
				(u32 *)frame_header,
				frame_buffer);

			/* The command has completed with error */
			scic_sds_request_set_status(
				&stp_req->parent,
				SCU_TASK_DONE_CHECK_RESPONSE,
				SCI_FAILURE_IO_RESPONSE_VALID);
			break;

		default:
			dev_warn(scic_to_dev(sci_req->owning_controller),
				 "%s: IO Request:0x%p Frame Id:%d protocol "
				 "violation occurred\n",
				 __func__, stp_req, frame_index);

			scic_sds_request_set_status(
				&stp_req->parent,
				SCU_TASK_DONE_UNEXP_FIS,
				SCI_FAILURE_PROTOCOL_VIOLATION);
			break;
		}

		sci_base_state_machine_change_state(
			&stp_req->parent.state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED);

		/* Frame has been decoded return it to the controller */
		scic_sds_controller_release_frame(
			stp_req->parent.owning_controller, frame_index);
	} else
		dev_err(scic_to_dev(sci_req->owning_controller),
			"%s: SCIC IO Request 0x%p could not get frame header "
			"for frame index %d, status %x\n",
			__func__, stp_req, frame_index, status);

	return status;
}

/* --------------------------------------------------------------------------- */

static const struct scic_sds_io_request_state_handler scic_sds_stp_request_started_non_data_substate_handler_table[] = {
	[SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_stp_request_non_data_await_h2d_tc_completion_handler,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.frame_handler		= scic_sds_stp_request_non_data_await_d2h_frame_handler,
	}
};

static void scic_sds_stp_request_started_non_data_await_h2d_completion_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_stp_request_started_non_data_substate_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE
		);

	scic_sds_remote_device_set_working_request(
		sci_req->target_device, sci_req
		);
}

static void scic_sds_stp_request_started_non_data_await_d2h_enter(void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_stp_request_started_non_data_substate_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE
		);
}

/* --------------------------------------------------------------------------- */

static const struct sci_base_state scic_sds_stp_request_started_non_data_substate_table[] = {
	[SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_non_data_await_h2d_completion_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_non_data_await_d2h_enter,
	},
};

enum sci_status scic_sds_stp_non_data_request_construct(struct scic_sds_request *sci_req)
{
	struct scic_sds_stp_request *stp_req =
		container_of(sci_req, typeof(*stp_req), parent);

	scic_sds_stp_non_ncq_request_construct(sci_req);

	/* Build the STP task context structure */
	scu_stp_raw_request_construct_task_context(stp_req, sci_req->task_context_buffer);

	sci_base_state_machine_construct(&sci_req->started_substate_machine,
					 sci_req,
					 scic_sds_stp_request_started_non_data_substate_table,
					 SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE);

	return SCI_SUCCESS;
}

#define SCU_MAX_FRAME_BUFFER_SIZE  0x400  /* 1K is the maximum SCU frame data payload */

/**
 *
 * @sci_req:
 * @length:
 *
 * This function will transmit DATA_FIS from (current sgl + offset) for input
 * parameter length. current sgl and offset is alreay stored in the IO request
 * enum sci_status
 */

static enum sci_status scic_sds_stp_request_pio_data_out_trasmit_data_frame(
	struct scic_sds_request *sci_req,
	u32 length)
{
	struct scic_sds_stp_request *stp_req =
		container_of(sci_req, typeof(*stp_req), parent);
	struct scu_sgl_element *current_sgl;

	/*
	 * Recycle the TC and reconstruct it for sending out DATA FIS containing
	 * for the data from current_sgl+offset for the input length */
	struct scu_task_context *task_context = scic_sds_controller_get_task_context_buffer(
		sci_req->owning_controller,
		sci_req->io_tag
		);

	if (stp_req->type.pio.request_current.sgl_set == SCU_SGL_ELEMENT_PAIR_A)
		current_sgl = &(stp_req->type.pio.request_current.sgl_pair->A);
	else
		current_sgl = &(stp_req->type.pio.request_current.sgl_pair->B);

	/* update the TC */
	task_context->command_iu_upper = current_sgl->address_upper;
	task_context->command_iu_lower = current_sgl->address_lower;
	task_context->transfer_length_bytes = length;
	task_context->type.stp.fis_type = FIS_DATA;

	/* send the new TC out. */
	return scic_controller_continue_io(sci_req);
}

/**
 *
 * @sci_req:
 *
 * enum sci_status
 */
static enum sci_status scic_sds_stp_request_pio_data_out_transmit_data(
	struct scic_sds_request *sci_req)
{

	struct scu_sgl_element *current_sgl;
	u32 sgl_offset;
	u32 remaining_bytes_in_current_sgl = 0;
	enum sci_status status = SCI_SUCCESS;

	struct scic_sds_stp_request *stp_req =
		container_of(sci_req, typeof(*stp_req), parent);

	sgl_offset = stp_req->type.pio.request_current.sgl_offset;

	if (stp_req->type.pio.request_current.sgl_set == SCU_SGL_ELEMENT_PAIR_A) {
		current_sgl = &(stp_req->type.pio.request_current.sgl_pair->A);
		remaining_bytes_in_current_sgl = stp_req->type.pio.request_current.sgl_pair->A.length - sgl_offset;
	} else {
		current_sgl = &(stp_req->type.pio.request_current.sgl_pair->B);
		remaining_bytes_in_current_sgl = stp_req->type.pio.request_current.sgl_pair->B.length - sgl_offset;
	}


	if (stp_req->type.pio.pio_transfer_bytes > 0) {
		if (stp_req->type.pio.pio_transfer_bytes >= remaining_bytes_in_current_sgl) {
			/* recycle the TC and send the H2D Data FIS from (current sgl + sgl_offset) and length = remaining_bytes_in_current_sgl */
			status = scic_sds_stp_request_pio_data_out_trasmit_data_frame(sci_req, remaining_bytes_in_current_sgl);
			if (status == SCI_SUCCESS) {
				stp_req->type.pio.pio_transfer_bytes -= remaining_bytes_in_current_sgl;

				/* update the current sgl, sgl_offset and save for future */
				current_sgl = scic_sds_stp_request_pio_get_next_sgl(stp_req);
				sgl_offset = 0;
			}
		} else if (stp_req->type.pio.pio_transfer_bytes < remaining_bytes_in_current_sgl) {
			/* recycle the TC and send the H2D Data FIS from (current sgl + sgl_offset) and length = type.pio.pio_transfer_bytes */
			scic_sds_stp_request_pio_data_out_trasmit_data_frame(sci_req, stp_req->type.pio.pio_transfer_bytes);

			if (status == SCI_SUCCESS) {
				/* Sgl offset will be adjusted and saved for future */
				sgl_offset += stp_req->type.pio.pio_transfer_bytes;
				current_sgl->address_lower += stp_req->type.pio.pio_transfer_bytes;
				stp_req->type.pio.pio_transfer_bytes = 0;
			}
		}
	}

	if (status == SCI_SUCCESS) {
		stp_req->type.pio.request_current.sgl_offset = sgl_offset;
	}

	return status;
}

/**
 *
 * @stp_request: The request that is used for the SGL processing.
 * @data_buffer: The buffer of data to be copied.
 * @length: The length of the data transfer.
 *
 * Copy the data from the buffer for the length specified to the IO reqeust SGL
 * specified data region. enum sci_status
 */
static enum sci_status
scic_sds_stp_request_pio_data_in_copy_data_buffer(struct scic_sds_stp_request *stp_req,
						  u8 *data_buf, u32 len)
{
	struct scic_sds_request *sci_req;
	struct isci_request *ireq;
	u8 *src_addr;
	int copy_len;
	struct sas_task *task;
	struct scatterlist *sg;
	void *kaddr;
	int total_len = len;

	sci_req = &stp_req->parent;
	ireq = scic_sds_request_get_user_request(sci_req);
	task = isci_request_access_task(ireq);
	src_addr = data_buf;

	if (task->num_scatter > 0) {
		sg = task->scatter;

		while (total_len > 0) {
			struct page *page = sg_page(sg);

			copy_len = min_t(int, total_len, sg_dma_len(sg));
			kaddr = kmap_atomic(page, KM_IRQ0);
			memcpy(kaddr + sg->offset, src_addr, copy_len);
			kunmap_atomic(kaddr, KM_IRQ0);
			total_len -= copy_len;
			src_addr += copy_len;
			sg = sg_next(sg);
		}
	} else {
		BUG_ON(task->total_xfer_len < total_len);
		memcpy(task->scatter, src_addr, total_len);
	}

	return SCI_SUCCESS;
}

/**
 *
 * @sci_req: The PIO DATA IN request that is to receive the data.
 * @data_buffer: The buffer to copy from.
 *
 * Copy the data buffer to the io request data region. enum sci_status
 */
static enum sci_status scic_sds_stp_request_pio_data_in_copy_data(
	struct scic_sds_stp_request *sci_req,
	u8 *data_buffer)
{
	enum sci_status status;

	/*
	 * If there is less than 1K remaining in the transfer request
	 * copy just the data for the transfer */
	if (sci_req->type.pio.pio_transfer_bytes < SCU_MAX_FRAME_BUFFER_SIZE) {
		status = scic_sds_stp_request_pio_data_in_copy_data_buffer(
			sci_req, data_buffer, sci_req->type.pio.pio_transfer_bytes);

		if (status == SCI_SUCCESS)
			sci_req->type.pio.pio_transfer_bytes = 0;
	} else {
		/* We are transfering the whole frame so copy */
		status = scic_sds_stp_request_pio_data_in_copy_data_buffer(
			sci_req, data_buffer, SCU_MAX_FRAME_BUFFER_SIZE);

		if (status == SCI_SUCCESS)
			sci_req->type.pio.pio_transfer_bytes -= SCU_MAX_FRAME_BUFFER_SIZE;
	}

	return status;
}

/**
 *
 * @sci_req:
 * @completion_code:
 *
 * enum sci_status
 */
static enum sci_status scic_sds_stp_request_pio_await_h2d_completion_tc_completion_handler(
	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	enum sci_status status = SCI_SUCCESS;

	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_request_set_status(
			sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);

		sci_base_state_machine_change_state(
			&sci_req->started_substate_machine,
			SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
			);
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
			SCI_BASE_REQUEST_STATE_COMPLETED
			);
		break;
	}

	return status;
}

/**
 *
 * @sci_req:
 * @frame_index:
 *
 * enum sci_status
 */
static enum sci_status scic_sds_stp_request_pio_await_frame_frame_handler(
	struct scic_sds_request *sci_req,
	u32 frame_index)
{
	enum sci_status status;
	struct dev_to_host_fis *frame_header;
	u32 *frame_buffer;
	struct scic_sds_stp_request *stp_req = container_of(sci_req, typeof(*stp_req), parent);
	struct isci_request *ireq = sci_req->ireq;
	struct sas_task *task = isci_request_access_task(ireq);

	status = scic_sds_unsolicited_frame_control_get_header(
		&(stp_req->parent.owning_controller->uf_control),
		frame_index,
		(void **)&frame_header);

	if (status == SCI_SUCCESS) {
		switch (frame_header->fis_type) {
		case FIS_PIO_SETUP:
			/* Get from the frame buffer the PIO Setup Data */
			scic_sds_unsolicited_frame_control_get_buffer(
				&(stp_req->parent.owning_controller->uf_control),
				frame_index,
				(void **)&frame_buffer);

			/* Get the data from the PIO Setup The SCU Hardware
			 * returns first word in the frame_header and the rest
			 * of the data is in the frame buffer so we need to back
			 * up one dword
			 */

			/* transfer_count: first 16bits in the 4th dword */
			stp_req->type.pio.pio_transfer_bytes =
				frame_buffer[3] & 0xffff;

			/* ending_status: 4th byte in the 3rd dword */
			stp_req->type.pio.ending_status =
				(frame_buffer[2] >> 24) & 0xff;

			scic_sds_controller_copy_sata_response(
				&stp_req->d2h_reg_fis,
				(u32 *)frame_header,
				frame_buffer);

			stp_req->d2h_reg_fis.status =
				stp_req->type.pio.ending_status;

			/* The next state is dependent on whether the
			 * request was PIO Data-in or Data out
			 */
			if (task->data_dir == DMA_FROM_DEVICE) {
				sci_base_state_machine_change_state(
					&stp_req->parent.started_substate_machine,
					SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE);
			} else if (task->data_dir == DMA_TO_DEVICE) {
				/* Transmit data */
				status = scic_sds_stp_request_pio_data_out_transmit_data(sci_req);
				if (status == SCI_SUCCESS) {
					sci_base_state_machine_change_state(
						&stp_req->parent.started_substate_machine,
						SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE);
				}
			}
			break;

		case FIS_SETDEVBITS:
			sci_base_state_machine_change_state(
				&stp_req->parent.started_substate_machine,
				SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE);
			break;

		case FIS_REGD2H:
			if ((frame_header->status & ATA_BUSY) == 0) {
				scic_sds_unsolicited_frame_control_get_buffer(
					&(stp_req->parent.owning_controller->uf_control),
					frame_index,
					(void **)&frame_buffer);

				scic_sds_controller_copy_sata_response(
					&stp_req->d2h_reg_fis,
					(u32 *)frame_header,
					frame_buffer);

				scic_sds_request_set_status(
					&stp_req->parent,
					SCU_TASK_DONE_CHECK_RESPONSE,
					SCI_FAILURE_IO_RESPONSE_VALID);

				sci_base_state_machine_change_state(
					&stp_req->parent.state_machine,
					SCI_BASE_REQUEST_STATE_COMPLETED);
			} else {
				/* Now why is the drive sending a D2H Register
				 * FIS when it is still busy?
				 * Do nothing since we are still in the right
				 * state.
				 */
				dev_dbg(scic_to_dev(sci_req->owning_controller),
					"%s: SCIC PIO Request 0x%p received "
					"D2H Register FIS with BSY status "
					"0x%x\n",
					__func__,
					stp_req,
					frame_header->status);
			}
			break;

		default:
			/* FIXME: what do we do here? */
			break;
		}

		/* Frame is decoded return it to the controller */
		scic_sds_controller_release_frame(
			stp_req->parent.owning_controller,
			frame_index);
	} else
		dev_err(scic_to_dev(sci_req->owning_controller),
			"%s: SCIC IO Request 0x%p could not get frame header "
			"for frame index %d, status %x\n",
			__func__, stp_req, frame_index, status);

	return status;
}

/**
 *
 * @sci_req:
 * @frame_index:
 *
 * enum sci_status
 */
static enum sci_status scic_sds_stp_request_pio_data_in_await_data_frame_handler(
	struct scic_sds_request *sci_req,
	u32 frame_index)
{
	enum sci_status status;
	struct dev_to_host_fis *frame_header;
	struct sata_fis_data *frame_buffer;
	struct scic_sds_stp_request *stp_req =
		container_of(sci_req, typeof(*stp_req), parent);

	status = scic_sds_unsolicited_frame_control_get_header(
		&(stp_req->parent.owning_controller->uf_control),
		frame_index,
		(void **)&frame_header);

	if (status == SCI_SUCCESS) {
		if (frame_header->fis_type == FIS_DATA) {
			if (stp_req->type.pio.request_current.sgl_pair ==
					NULL) {
				stp_req->parent.saved_rx_frame_index =
					frame_index;
				stp_req->type.pio.pio_transfer_bytes = 0;
			} else {
				status = scic_sds_unsolicited_frame_control_get_buffer(
					&(stp_req->parent.owning_controller->uf_control),
					frame_index,
					(void **)&frame_buffer);

				status = scic_sds_stp_request_pio_data_in_copy_data(
						stp_req,
						(u8 *)frame_buffer);

				/* Frame is decoded return it to the controller */
				scic_sds_controller_release_frame(
					stp_req->parent.owning_controller,
					frame_index);
			}

			/*
			 * Check for the end of the transfer, are there more
			 * bytes remaining for this data transfer
			 */
			if ((status == SCI_SUCCESS) &&
			    (stp_req->type.pio.pio_transfer_bytes == 0)) {
				if ((stp_req->type.pio.ending_status &
							ATA_BUSY) == 0) {
					scic_sds_request_set_status(
						&stp_req->parent,
						SCU_TASK_DONE_CHECK_RESPONSE,
						SCI_FAILURE_IO_RESPONSE_VALID);

					sci_base_state_machine_change_state(
						&stp_req->parent.state_machine,
						SCI_BASE_REQUEST_STATE_COMPLETED);
				} else {
					sci_base_state_machine_change_state(
						&sci_req->started_substate_machine,
						SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE);
				}
			}
		} else {
			dev_err(scic_to_dev(sci_req->owning_controller),
				"%s: SCIC PIO Request 0x%p received frame %d "
				"with fis type 0x%02x when expecting a data "
				"fis.\n",
				__func__,
				stp_req,
				frame_index,
				frame_header->fis_type);

			scic_sds_request_set_status(
				&stp_req->parent,
				SCU_TASK_DONE_GOOD,
				SCI_FAILURE_IO_REQUIRES_SCSI_ABORT);

			sci_base_state_machine_change_state(
				&stp_req->parent.state_machine,
				SCI_BASE_REQUEST_STATE_COMPLETED);

			/* Frame is decoded return it to the controller */
			scic_sds_controller_release_frame(
				stp_req->parent.owning_controller,
				frame_index);
		}
	} else
		dev_err(scic_to_dev(sci_req->owning_controller),
			"%s: SCIC IO Request 0x%p could not get frame header "
			"for frame index %d, status %x\n",
			__func__, stp_req, frame_index, status);

	return status;
}


/**
 *
 * @sci_req:
 * @completion_code:
 *
 * enum sci_status
 */
static enum sci_status scic_sds_stp_request_pio_data_out_await_data_transmit_completion_tc_completion_handler(

	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	enum sci_status status = SCI_SUCCESS;
	bool all_frames_transferred = false;
	struct scic_sds_stp_request *stp_req =
		container_of(sci_req, typeof(*stp_req), parent);

	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		/* Transmit data */
		if (stp_req->type.pio.pio_transfer_bytes != 0) {
			status = scic_sds_stp_request_pio_data_out_transmit_data(sci_req);
			if (status == SCI_SUCCESS) {
				if (stp_req->type.pio.pio_transfer_bytes == 0)
					all_frames_transferred = true;
			}
		} else if (stp_req->type.pio.pio_transfer_bytes == 0) {
			/*
			 * this will happen if the all data is written at the
			 * first time after the pio setup fis is received
			 */
			all_frames_transferred  = true;
		}

		/* all data transferred. */
		if (all_frames_transferred) {
			/*
			 * Change the state to SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_FRAME_SUBSTATE
			 * and wait for PIO_SETUP fis / or D2H REg fis. */
			sci_base_state_machine_change_state(
				&sci_req->started_substate_machine,
				SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
				);
		}
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
			SCI_BASE_REQUEST_STATE_COMPLETED
			);
		break;
	}

	return status;
}

/**
 *
 * @request: This is the request which is receiving the event.
 * @event_code: This is the event code that the request on which the request is
 *    expected to take action.
 *
 * This method will handle any link layer events while waiting for the data
 * frame. enum sci_status SCI_SUCCESS SCI_FAILURE
 */
static enum sci_status scic_sds_stp_request_pio_data_in_await_data_event_handler(
	struct scic_sds_request *request,
	u32 event_code)
{
	enum sci_status status;

	switch (scu_get_event_specifier(event_code)) {
	case SCU_TASK_DONE_CRC_ERR << SCU_EVENT_SPECIFIC_CODE_SHIFT:
		/*
		 * We are waiting for data and the SCU has R_ERR the data frame.
		 * Go back to waiting for the D2H Register FIS */
		sci_base_state_machine_change_state(
			&request->started_substate_machine,
			SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
			);

		status = SCI_SUCCESS;
		break;

	default:
		dev_err(scic_to_dev(request->owning_controller),
			"%s: SCIC PIO Request 0x%p received unexpected "
			"event 0x%08x\n",
			__func__, request, event_code);

		/* / @todo Should we fail the PIO request when we get an unexpected event? */
		status = SCI_FAILURE;
		break;
	}

	return status;
}

/* --------------------------------------------------------------------------- */

static const struct scic_sds_io_request_state_handler scic_sds_stp_request_started_pio_substate_handler_table[] = {
	[SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_stp_request_pio_await_h2d_completion_tc_completion_handler,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.frame_handler		= scic_sds_stp_request_pio_await_frame_frame_handler
	},
	[SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.event_handler		= scic_sds_stp_request_pio_data_in_await_data_event_handler,
		.frame_handler		= scic_sds_stp_request_pio_data_in_await_data_frame_handler
	},
	[SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_stp_request_pio_data_out_await_data_transmit_completion_tc_completion_handler,
	}
};

static void scic_sds_stp_request_started_pio_await_h2d_completion_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_stp_request_started_pio_substate_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE
		);

	scic_sds_remote_device_set_working_request(
		sci_req->target_device, sci_req);
}

static void scic_sds_stp_request_started_pio_await_frame_enter(void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_stp_request_started_pio_substate_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
		);
}

static void scic_sds_stp_request_started_pio_data_in_await_data_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_stp_request_started_pio_substate_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE
		);
}

static void scic_sds_stp_request_started_pio_data_out_transmit_data_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_stp_request_started_pio_substate_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE
		);
}

/* --------------------------------------------------------------------------- */

static const struct sci_base_state scic_sds_stp_request_started_pio_substate_table[] = {
	[SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_pio_await_h2d_completion_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_pio_await_frame_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_pio_data_in_await_data_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_pio_data_out_transmit_data_enter,
	}
};

enum sci_status
scic_sds_stp_pio_request_construct(struct scic_sds_request *sci_req,
				   bool copy_rx_frame)
{
	struct scic_sds_stp_request *stp_req =
		container_of(sci_req, typeof(*stp_req), parent);
	struct scic_sds_stp_pio_request *pio = &stp_req->type.pio;

	scic_sds_stp_non_ncq_request_construct(sci_req);

	scu_stp_raw_request_construct_task_context(stp_req,
						   sci_req->task_context_buffer);

	pio->current_transfer_bytes = 0;
	pio->ending_error = 0;
	pio->ending_status = 0;

	pio->request_current.sgl_offset = 0;
	pio->request_current.sgl_set = SCU_SGL_ELEMENT_PAIR_A;

	if (copy_rx_frame) {
		scic_sds_request_build_sgl(sci_req);
		/* Since the IO request copy of the TC contains the same data as
		 * the actual TC this pointer is vaild for either.
		 */
		pio->request_current.sgl_pair = &sci_req->task_context_buffer->sgl_pair_ab;
	} else {
		/* The user does not want the data copied to the SGL buffer location */
		pio->request_current.sgl_pair = NULL;
	}

	sci_base_state_machine_construct(&sci_req->started_substate_machine,
					 sci_req,
					 scic_sds_stp_request_started_pio_substate_table,
					 SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE);

	return SCI_SUCCESS;
}

static void scic_sds_stp_request_udma_complete_request(
	struct scic_sds_request *request,
	u32 scu_status,
	enum sci_status sci_status)
{
	scic_sds_request_set_status(request, scu_status, sci_status);
	sci_base_state_machine_change_state(&request->state_machine,
		SCI_BASE_REQUEST_STATE_COMPLETED);
}

/**
 *
 * @sci_req:
 * @frame_index:
 *
 * enum sci_status
 */
static enum sci_status scic_sds_stp_request_udma_general_frame_handler(
	struct scic_sds_request *sci_req,
	u32 frame_index)
{
	enum sci_status status;
	struct dev_to_host_fis *frame_header;
	u32 *frame_buffer;

	status = scic_sds_unsolicited_frame_control_get_header(
		&sci_req->owning_controller->uf_control,
		frame_index,
		(void **)&frame_header);

	if ((status == SCI_SUCCESS) &&
	    (frame_header->fis_type == FIS_REGD2H)) {
		scic_sds_unsolicited_frame_control_get_buffer(
			&sci_req->owning_controller->uf_control,
			frame_index,
			(void **)&frame_buffer);

		scic_sds_controller_copy_sata_response(
			&((struct scic_sds_stp_request *)sci_req)->d2h_reg_fis,
			(u32 *)frame_header,
			frame_buffer);
	}

	scic_sds_controller_release_frame(
		sci_req->owning_controller, frame_index);

	return status;
}

/**
 * This method process TC completions while in the state where we are waiting
 *    for TC completions.
 * @sci_req:
 * @completion_code:
 *
 * enum sci_status
 */
static enum sci_status scic_sds_stp_request_udma_await_tc_completion_tc_completion_handler(
	struct scic_sds_request *request,
	u32 completion_code)
{
	enum sci_status status = SCI_SUCCESS;
	struct scic_sds_stp_request *sci_req = (struct scic_sds_stp_request *)request;

	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_stp_request_udma_complete_request(
			&sci_req->parent, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);
		break;

	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_FIS):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_REG_ERR):
		/*
		 * We must check ther response buffer to see if the D2H Register FIS was
		 * received before we got the TC completion. */
		if (sci_req->d2h_reg_fis.fis_type == FIS_REGD2H) {
			scic_sds_remote_device_suspend(
				sci_req->parent.target_device,
				SCU_EVENT_SPECIFIC(SCU_NORMALIZE_COMPLETION_STATUS(completion_code))
				);

			scic_sds_stp_request_udma_complete_request(
				&sci_req->parent,
				SCU_TASK_DONE_CHECK_RESPONSE,
				SCI_FAILURE_IO_RESPONSE_VALID
				);
		} else {
			/*
			 * If we have an error completion status for the TC then we can expect a
			 * D2H register FIS from the device so we must change state to wait for it */
			sci_base_state_machine_change_state(
				&sci_req->parent.started_substate_machine,
				SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE
				);
		}
		break;

	/*
	 * / @todo Check to see if any of these completion status need to wait for
	 * /       the device to host register fis. */
	/* / @todo We can retry the command for SCU_TASK_DONE_CMD_LL_R_ERR - this comes only for B0 */
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_INV_FIS_LEN):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_MAX_PLD_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_LL_R_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_CMD_LL_R_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_CRC_ERR):
		scic_sds_remote_device_suspend(
			sci_req->parent.target_device,
			SCU_EVENT_SPECIFIC(SCU_NORMALIZE_COMPLETION_STATUS(completion_code))
			);
	/* Fall through to the default case */
	default:
		/* All other completion status cause the IO to be complete. */
		scic_sds_stp_request_udma_complete_request(
			&sci_req->parent,
			SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
			SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
			);
		break;
	}

	return status;
}

static enum sci_status scic_sds_stp_request_udma_await_d2h_reg_fis_frame_handler(
	struct scic_sds_request *sci_req,
	u32 frame_index)
{
	enum sci_status status;

	/* Use the general frame handler to copy the resposne data */
	status = scic_sds_stp_request_udma_general_frame_handler(sci_req, frame_index);

	if (status == SCI_SUCCESS) {
		scic_sds_stp_request_udma_complete_request(
			sci_req,
			SCU_TASK_DONE_CHECK_RESPONSE,
			SCI_FAILURE_IO_RESPONSE_VALID
			);
	}

	return status;
}

/* --------------------------------------------------------------------------- */

static const struct scic_sds_io_request_state_handler scic_sds_stp_request_started_udma_substate_handler_table[] = {
	[SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_stp_request_udma_await_tc_completion_tc_completion_handler,
		.frame_handler		= scic_sds_stp_request_udma_general_frame_handler,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.frame_handler		= scic_sds_stp_request_udma_await_d2h_reg_fis_frame_handler,
	},
};

static void scic_sds_stp_request_started_udma_await_tc_completion_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_stp_request_started_udma_substate_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE
		);
}

/**
 *
 *
 * This state is entered when there is an TC completion failure.  The hardware
 * received an unexpected condition while processing the IO request and now
 * will UF the D2H register FIS to complete the IO.
 */
static void scic_sds_stp_request_started_udma_await_d2h_reg_fis_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_stp_request_started_udma_substate_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE
		);
}

/* --------------------------------------------------------------------------- */

static const struct sci_base_state scic_sds_stp_request_started_udma_substate_table[] = {
	[SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_udma_await_tc_completion_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_udma_await_d2h_reg_fis_enter,
	},
};

enum sci_status scic_sds_stp_udma_request_construct(struct scic_sds_request *sci_req,
						    u32 len,
						    enum dma_data_direction dir)
{
	scic_sds_stp_non_ncq_request_construct(sci_req);

	scic_sds_stp_optimized_request_construct(sci_req, SCU_TASK_TYPE_DMA_IN,
						 len, dir);

	sci_base_state_machine_construct(
		&sci_req->started_substate_machine,
		sci_req,
		scic_sds_stp_request_started_udma_substate_table,
		SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE
		);

	return SCI_SUCCESS;
}

/**
 *
 * @sci_req:
 * @completion_code:
 *
 * This method processes a TC completion.  The expected TC completion is for
 * the transmission of the H2D register FIS containing the SATA/STP non-data
 * request. This method always successfully processes the TC completion.
 * SCI_SUCCESS This value is always returned.
 */
static enum sci_status scic_sds_stp_request_soft_reset_await_h2d_asserted_tc_completion_handler(
	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_request_set_status(
			sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);

		sci_base_state_machine_change_state(
			&sci_req->started_substate_machine,
			SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE
			);
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
			&sci_req->state_machine, SCI_BASE_REQUEST_STATE_COMPLETED);
		break;
	}

	return SCI_SUCCESS;
}

/**
 *
 * @sci_req:
 * @completion_code:
 *
 * This method processes a TC completion.  The expected TC completion is for
 * the transmission of the H2D register FIS containing the SATA/STP non-data
 * request. This method always successfully processes the TC completion.
 * SCI_SUCCESS This value is always returned.
 */
static enum sci_status scic_sds_stp_request_soft_reset_await_h2d_diagnostic_tc_completion_handler(
	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_request_set_status(
			sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);

		sci_base_state_machine_change_state(
			&sci_req->started_substate_machine,
			SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE
			);
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

		sci_base_state_machine_change_state(&sci_req->state_machine,
				SCI_BASE_REQUEST_STATE_COMPLETED);
		break;
	}

	return SCI_SUCCESS;
}

/**
 *
 * @request: This parameter specifies the request for which a frame has been
 *    received.
 * @frame_index: This parameter specifies the index of the frame that has been
 *    received.
 *
 * This method processes frames received from the target while waiting for a
 * device to host register FIS.  If a non-register FIS is received during this
 * time, it is treated as a protocol violation from an IO perspective. Indicate
 * if the received frame was processed successfully.
 */
static enum sci_status scic_sds_stp_request_soft_reset_await_d2h_frame_handler(
	struct scic_sds_request *request,
	u32 frame_index)
{
	enum sci_status status;
	struct dev_to_host_fis *frame_header;
	u32 *frame_buffer;
	struct scic_sds_stp_request *stp_req =
		(struct scic_sds_stp_request *)request;

	status = scic_sds_unsolicited_frame_control_get_header(
		&(stp_req->parent.owning_controller->uf_control),
		frame_index,
		(void **)&frame_header);

	if (status == SCI_SUCCESS) {
		switch (frame_header->fis_type) {
		case FIS_REGD2H:
			scic_sds_unsolicited_frame_control_get_buffer(
				&(stp_req->parent.owning_controller->uf_control),
				frame_index,
				(void **)&frame_buffer);

			scic_sds_controller_copy_sata_response(
				&stp_req->d2h_reg_fis,
				(u32 *)frame_header,
				frame_buffer);

			/* The command has completed with error */
			scic_sds_request_set_status(
				&stp_req->parent,
				SCU_TASK_DONE_CHECK_RESPONSE,
				SCI_FAILURE_IO_RESPONSE_VALID);
			break;

		default:
			dev_warn(scic_to_dev(request->owning_controller),
				 "%s: IO Request:0x%p Frame Id:%d protocol "
				 "violation occurred\n",
				 __func__,
				 stp_req,
				 frame_index);

			scic_sds_request_set_status(
				&stp_req->parent,
				SCU_TASK_DONE_UNEXP_FIS,
				SCI_FAILURE_PROTOCOL_VIOLATION);
			break;
		}

		sci_base_state_machine_change_state(
			&stp_req->parent.state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED);

		/* Frame has been decoded return it to the controller */
		scic_sds_controller_release_frame(
			stp_req->parent.owning_controller, frame_index);
	} else
		dev_err(scic_to_dev(request->owning_controller),
			"%s: SCIC IO Request 0x%p could not get frame header "
			"for frame index %d, status %x\n",
			__func__, stp_req, frame_index, status);

	return status;
}

/* --------------------------------------------------------------------------- */

static const struct scic_sds_io_request_state_handler scic_sds_stp_request_started_soft_reset_substate_handler_table[] = {
	[SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_stp_request_soft_reset_await_h2d_asserted_tc_completion_handler,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_stp_request_soft_reset_await_h2d_diagnostic_tc_completion_handler,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.frame_handler		= scic_sds_stp_request_soft_reset_await_d2h_frame_handler,
	},
};

static void scic_sds_stp_request_started_soft_reset_await_h2d_asserted_completion_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_stp_request_started_soft_reset_substate_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE
		);

	scic_sds_remote_device_set_working_request(
		sci_req->target_device, sci_req
		);
}

static void scic_sds_stp_request_started_soft_reset_await_h2d_diagnostic_completion_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;
	struct scu_task_context *task_context;
	struct host_to_dev_fis *h2d_fis;
	enum sci_status status;

	/* Clear the SRST bit */
	h2d_fis = scic_stp_io_request_get_h2d_reg_address(sci_req);
	h2d_fis->control = 0;

	/* Clear the TC control bit */
	task_context = scic_sds_controller_get_task_context_buffer(
		sci_req->owning_controller, sci_req->io_tag);
	task_context->control_frame = 0;

	status = scic_controller_continue_io(sci_req);
	if (status == SCI_SUCCESS) {
		SET_STATE_HANDLER(
			sci_req,
			scic_sds_stp_request_started_soft_reset_substate_handler_table,
			SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE
			);
	}
}

static void scic_sds_stp_request_started_soft_reset_await_d2h_response_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_stp_request_started_soft_reset_substate_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE
		);
}

static const struct sci_base_state scic_sds_stp_request_started_soft_reset_substate_table[] = {
	[SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_soft_reset_await_h2d_asserted_completion_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_soft_reset_await_h2d_diagnostic_completion_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_soft_reset_await_d2h_response_enter,
	},
};

enum sci_status scic_sds_stp_soft_reset_request_construct(struct scic_sds_request *sci_req)
{
	struct scic_sds_stp_request *stp_req =
		container_of(sci_req, typeof(*stp_req), parent);

	scic_sds_stp_non_ncq_request_construct(sci_req);

	/* Build the STP task context structure */
	scu_stp_raw_request_construct_task_context(stp_req, sci_req->task_context_buffer);

	sci_base_state_machine_construct(&sci_req->started_substate_machine,
					 sci_req,
					 scic_sds_stp_request_started_soft_reset_substate_table,
					 SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE);

	return SCI_SUCCESS;
}
