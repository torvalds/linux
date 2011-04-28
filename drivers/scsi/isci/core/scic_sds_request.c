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
#include "intel_sata.h"
#include "intel_sat.h"
#include "scic_controller.h"
#include "scic_io_request.h"
#include "scic_sds_controller.h"
#include "scu_registers.h"
#include "scic_sds_port.h"
#include "remote_device.h"
#include "scic_sds_request.h"
#include "scic_sds_smp_request.h"
#include "scic_sds_stp_request.h"
#include "scic_sds_unsolicited_frame_control.h"
#include "sci_environment.h"
#include "sci_util.h"
#include "scu_completion_codes.h"
#include "scu_constants.h"
#include "scu_task_context.h"

/*
 * ****************************************************************************
 * * SCIC SDS IO REQUEST CONSTANTS
 * **************************************************************************** */

/**
 *
 *
 * We have no timer requirements for IO requests right now
 */
#define SCIC_SDS_IO_REQUEST_MINIMUM_TIMER_COUNT (0)
#define SCIC_SDS_IO_REQUEST_MAXIMUM_TIMER_COUNT (0)

/*
 * ****************************************************************************
 * * SCIC SDS IO REQUEST MACROS
 * **************************************************************************** */

/**
 * scic_ssp_io_request_get_object_size() -
 *
 * This macro returns the sizeof memory required to store the an SSP IO
 * request.  This does not include the size of the SGL or SCU Task Context
 * memory.
 */
#define scic_ssp_io_request_get_object_size() \
	(\
		sizeof(struct sci_ssp_command_iu) \
		+ sizeof(struct sci_ssp_response_iu)	\
	)

/**
 * scic_sds_ssp_request_get_command_buffer() -
 *
 * This macro returns the address of the ssp command buffer in the io request
 * memory
 */
#define scic_sds_ssp_request_get_command_buffer(memory)	\
	((struct sci_ssp_command_iu *)(\
		 ((char *)(memory)) + sizeof(struct scic_sds_request) \
		 ))

/**
 * scic_sds_ssp_request_get_response_buffer() -
 *
 * This macro returns the address of the ssp response buffer in the io request
 * memory
 */
#define scic_sds_ssp_request_get_response_buffer(memory) \
	((struct sci_ssp_response_iu *)(\
		 ((char *)(scic_sds_ssp_request_get_command_buffer(memory))) \
		 + sizeof(struct sci_ssp_command_iu)	\
		 ))

/**
 * scic_sds_ssp_request_get_task_context_buffer() -
 *
 * This macro returns the address of the task context buffer in the io request
 * memory
 */
#define scic_sds_ssp_request_get_task_context_buffer(memory) \
	((struct scu_task_context *)(\
		 ((char *)(scic_sds_ssp_request_get_response_buffer(memory))) \
		 + sizeof(struct sci_ssp_response_iu) \
		 ))

/**
 * scic_sds_ssp_request_get_sgl_element_buffer() -
 *
 * This macro returns the address of the sgl elment pairs in the io request
 * memory buffer
 */
#define scic_sds_ssp_request_get_sgl_element_buffer(memory) \
	((struct scu_sgl_element_pair *)(\
		 ((char *)(scic_sds_ssp_request_get_task_context_buffer(memory))) \
		 + sizeof(struct scu_task_context) \
		 ))


/**
 * scic_ssp_task_request_get_object_size() -
 *
 * This macro returns the sizeof of memory required to store an SSP Task
 * request.  This does not include the size of the SCU Task Context memory.
 */
#define scic_ssp_task_request_get_object_size()	\
	(\
		sizeof(struct sci_ssp_task_iu) \
		+ sizeof(struct sci_ssp_response_iu)	\
	)

/**
 * scic_sds_ssp_task_request_get_command_buffer() -
 *
 * This macro returns the address of the ssp command buffer in the task request
 * memory.  Yes its the same as the above macro except for the name.
 */
#define scic_sds_ssp_task_request_get_command_buffer(memory) \
	((struct sci_ssp_task_iu *)(\
		 ((char *)(memory)) + sizeof(struct scic_sds_request) \
		 ))

/**
 * scic_sds_ssp_task_request_get_response_buffer() -
 *
 * This macro returns the address of the ssp response buffer in the task
 * request memory.
 */
#define scic_sds_ssp_task_request_get_response_buffer(memory) \
	((struct sci_ssp_response_iu *)(\
		 ((char *)(scic_sds_ssp_task_request_get_command_buffer(memory))) \
		 + sizeof(struct sci_ssp_task_iu) \
		 ))

/**
 * scic_sds_ssp_task_request_get_task_context_buffer() -
 *
 * This macro returs the task context buffer for the SSP task request.
 */
#define scic_sds_ssp_task_request_get_task_context_buffer(memory) \
	((struct scu_task_context *)(\
		 ((char *)(scic_sds_ssp_task_request_get_response_buffer(memory))) \
		 + sizeof(struct sci_ssp_response_iu) \
		 ))



/*
 * ****************************************************************************
 * * SCIC SDS IO REQUEST PRIVATE METHODS
 * **************************************************************************** */

/**
 *
 *
 * This method returns the size required to store an SSP IO request object. u32
 */
static u32 scic_sds_ssp_request_get_object_size(void)
{
	return sizeof(struct scic_sds_request)
	       + scic_ssp_io_request_get_object_size()
	       + sizeof(struct scu_task_context)
	       + SMP_CACHE_BYTES
	       + sizeof(struct scu_sgl_element_pair) * SCU_MAX_SGL_ELEMENT_PAIRS;
}

/**
 * This method returns the sgl element pair for the specificed sgl_pair index.
 * @sci_req: This parameter specifies the IO request for which to retrieve
 *    the Scatter-Gather List element pair.
 * @sgl_pair_index: This parameter specifies the index into the SGL element
 *    pair to be retrieved.
 *
 * This method returns a pointer to an struct scu_sgl_element_pair.
 */
static struct scu_sgl_element_pair *scic_sds_request_get_sgl_element_pair(
	struct scic_sds_request *sci_req,
	u32 sgl_pair_index
	) {
	struct scu_task_context *task_context;

	task_context = (struct scu_task_context *)sci_req->task_context_buffer;

	if (sgl_pair_index == 0) {
		return &task_context->sgl_pair_ab;
	} else if (sgl_pair_index == 1) {
		return &task_context->sgl_pair_cd;
	}

	return &sci_req->sgl_element_pair_buffer[sgl_pair_index - 2];
}

/**
 * This function will build the SGL list for an IO request.
 * @sci_req: This parameter specifies the IO request for which to build
 *    the Scatter-Gather List.
 *
 */
void scic_sds_request_build_sgl(struct scic_sds_request *sds_request)
{
	struct isci_request *isci_request =
		(struct isci_request *)sci_object_get_association(sds_request);
	struct isci_host *isci_host = isci_request->isci_host;
	struct sas_task *task = isci_request_access_task(isci_request);
	struct scatterlist *sg = NULL;
	dma_addr_t dma_addr;
	u32 sg_idx = 0;
	struct scu_sgl_element_pair *scu_sg   = NULL;
	struct scu_sgl_element_pair *prev_sg  = NULL;

	if (task->num_scatter > 0) {
		sg = task->scatter;

		while (sg) {
			scu_sg = scic_sds_request_get_sgl_element_pair(
					sds_request,
					sg_idx);

			SCU_SGL_COPY(scu_sg->A, sg);

			sg = sg_next(sg);

			if (sg) {
				SCU_SGL_COPY(scu_sg->B, sg);
				sg = sg_next(sg);
			} else
				SCU_SGL_ZERO(scu_sg->B);

			if (prev_sg) {
				dma_addr =
					scic_io_request_get_dma_addr(
							sds_request,
							scu_sg);

				prev_sg->next_pair_upper =
					upper_32_bits(dma_addr);
				prev_sg->next_pair_lower =
					lower_32_bits(dma_addr);
			}

			prev_sg = scu_sg;
			sg_idx++;
		}
	} else {	/* handle when no sg */
		scu_sg = scic_sds_request_get_sgl_element_pair(sds_request,
							       sg_idx);

		dma_addr = dma_map_single(&isci_host->pdev->dev,
					  task->scatter,
					  task->total_xfer_len,
					  task->data_dir);

		isci_request->zero_scatter_daddr = dma_addr;

		scu_sg->A.length = task->total_xfer_len;
		scu_sg->A.address_upper = upper_32_bits(dma_addr);
		scu_sg->A.address_lower = lower_32_bits(dma_addr);
	}

	if (scu_sg) {
		scu_sg->next_pair_upper = 0;
		scu_sg->next_pair_lower = 0;
	}
}

/**
 * This method build the remainder of the IO request object.
 * @sci_req: This parameter specifies the request object being constructed.
 *
 * The scic_sds_general_request_construct() must be called before this call is
 * valid. none
 */
static void scic_sds_ssp_io_request_assign_buffers(
	struct scic_sds_request *sci_req)
{
	sci_req->command_buffer =
		scic_sds_ssp_request_get_command_buffer(sci_req);
	sci_req->response_buffer =
		scic_sds_ssp_request_get_response_buffer(sci_req);
	sci_req->sgl_element_pair_buffer =
		scic_sds_ssp_request_get_sgl_element_buffer(sci_req);
	sci_req->sgl_element_pair_buffer =
		PTR_ALIGN(sci_req->sgl_element_pair_buffer,
			  sizeof(struct scu_sgl_element_pair));

	if (sci_req->was_tag_assigned_by_user == false) {
		sci_req->task_context_buffer =
			scic_sds_ssp_request_get_task_context_buffer(sci_req);
		sci_req->task_context_buffer =
			PTR_ALIGN(sci_req->task_context_buffer,
				  SMP_CACHE_BYTES);
	}
}

/**
 * This method constructs the SSP Command IU data for this io request object.
 * @sci_req: This parameter specifies the request object for which the SSP
 *    command information unit is being built.
 *
 */
static void scic_sds_io_request_build_ssp_command_iu(
	struct scic_sds_request *sds_request)
{
	struct sci_ssp_command_iu *command_frame;
	u32 cdb_length;
	u32 *cdb_buffer;
	struct isci_request *isci_request =
		(struct isci_request *)sci_object_get_association(sds_request);

	command_frame =
		(struct sci_ssp_command_iu *)sds_request->command_buffer;

	command_frame->lun_upper = 0;
	command_frame->lun_lower =
		isci_request_ssp_io_request_get_lun(isci_request);

	((u32 *)command_frame)[2] = 0;

	cdb_length = isci_request_ssp_io_request_get_cdb_length(isci_request);
	cdb_buffer = (u32 *)isci_request_ssp_io_request_get_cdb_address(
					isci_request);

	if (cdb_length > 16) {
		command_frame->additional_cdb_length = cdb_length - 16;
	}

	/* / @todo Is it ok to leave junk at the end of the cdb buffer? */
	scic_word_copy_with_swap(
		(u32 *)(&command_frame->cdb),
		(u32 *)(cdb_buffer),
		(cdb_length + 3) / sizeof(u32)
		);

	command_frame->enable_first_burst = 0;
	command_frame->task_priority =
		isci_request_ssp_io_request_get_command_priority(isci_request);
	command_frame->task_attribute =
		isci_request_ssp_io_request_get_task_attribute(isci_request);
}


/**
 * This method constructs the SSP Task IU data for this io request object.
 * @sci_req:
 *
 */
static void scic_sds_task_request_build_ssp_task_iu(
	struct scic_sds_request *sds_request)
{
	struct sci_ssp_task_iu *command_frame;
	struct isci_request *isci_request =
		(struct isci_request *)sci_object_get_association(sds_request);

	command_frame =
		(struct sci_ssp_task_iu *)sds_request->command_buffer;

	command_frame->lun_upper = 0;
	command_frame->lun_lower = isci_request_ssp_io_request_get_lun(
					isci_request);

	((u32 *)command_frame)[2] = 0;

	command_frame->task_function =
		isci_task_ssp_request_get_function(isci_request);
	command_frame->task_tag =
		isci_task_ssp_request_get_io_tag_to_manage(
				isci_request);
}


/**
 * This method is will fill in the SCU Task Context for any type of SSP request.
 * @sci_req:
 * @task_context:
 *
 */
static void scu_ssp_reqeust_construct_task_context(
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
	task_context->priority = 0;
	task_context->initiator_request = 1;
	task_context->connection_rate =
		scic_remote_device_get_connection_rate(target_device);
	task_context->protocol_engine_index =
		scic_sds_controller_get_protocol_engine_group(controller);
	task_context->logical_port_index =
		scic_sds_port_get_index(target_port);
	task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_SSP;
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

	/* task_context->type.ssp.tag = sci_req->io_tag; */
	task_context->task_phase = 0x01;

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
		 * the data
		 *
		 * I/O tag index is not assigned because we have to wait
		 * until we get a TCi
		 */
		sds_request->post_context =
			(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC |
			 (scic_sds_controller_get_protocol_engine_group(
							owning_controller) <<
			  SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) |
			 (scic_sds_port_get_index(target_port) <<
			  SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT));
	}

	/*
	 * Copy the physical address for the command buffer to the
	 * SCU Task Context
	 */
	dma_addr = scic_io_request_get_dma_addr(sds_request,
						sds_request->command_buffer);

	task_context->command_iu_upper = upper_32_bits(dma_addr);
	task_context->command_iu_lower = lower_32_bits(dma_addr);

	/*
	 * Copy the physical address for the response buffer to the
	 * SCU Task Context
	 */
	dma_addr = scic_io_request_get_dma_addr(sds_request,
						sds_request->response_buffer);

	task_context->response_iu_upper = upper_32_bits(dma_addr);
	task_context->response_iu_lower = lower_32_bits(dma_addr);
}

/**
 * This method is will fill in the SCU Task Context for a SSP IO request.
 * @sci_req:
 *
 */
static void scu_ssp_io_request_construct_task_context(
	struct scic_sds_request *sci_req,
	enum dma_data_direction dir,
	u32 len)
{
	struct scu_task_context *task_context;

	task_context = scic_sds_request_get_task_context(sci_req);

	scu_ssp_reqeust_construct_task_context(sci_req, task_context);

	task_context->ssp_command_iu_length = sizeof(struct sci_ssp_command_iu) / sizeof(u32);
	task_context->type.ssp.frame_type = SCI_SAS_COMMAND_FRAME;

	switch (dir) {
	case DMA_FROM_DEVICE:
	case DMA_NONE:
	default:
		task_context->task_type = SCU_TASK_TYPE_IOREAD;
		break;
	case DMA_TO_DEVICE:
		task_context->task_type = SCU_TASK_TYPE_IOWRITE;
		break;
	}

	task_context->transfer_length_bytes = len;

	if (task_context->transfer_length_bytes > 0)
		scic_sds_request_build_sgl(sci_req);
}


/**
 * This method will fill in the remainder of the io request object for SSP Task
 *    requests.
 * @sci_req:
 *
 */
static void scic_sds_ssp_task_request_assign_buffers(
	struct scic_sds_request *sci_req)
{
	/* Assign all of the buffer pointers */
	sci_req->command_buffer =
		scic_sds_ssp_task_request_get_command_buffer(sci_req);
	sci_req->response_buffer =
		scic_sds_ssp_task_request_get_response_buffer(sci_req);
	sci_req->sgl_element_pair_buffer = NULL;

	if (sci_req->was_tag_assigned_by_user == false) {
		sci_req->task_context_buffer =
			scic_sds_ssp_task_request_get_task_context_buffer(sci_req);
		sci_req->task_context_buffer =
			PTR_ALIGN(sci_req->task_context_buffer, SMP_CACHE_BYTES);
	}
}

/**
 * This method will fill in the SCU Task Context for a SSP Task request.  The
 *    following important settings are utilized: -# priority ==
 *    SCU_TASK_PRIORITY_HIGH.  This ensures that the task request is issued
 *    ahead of other task destined for the same Remote Node. -# task_type ==
 *    SCU_TASK_TYPE_IOREAD.  This simply indicates that a normal request type
 *    (i.e. non-raw frame) is being utilized to perform task management. -#
 *    control_frame == 1.  This ensures that the proper endianess is set so
 *    that the bytes are transmitted in the right order for a task frame.
 * @sci_req: This parameter specifies the task request object being
 *    constructed.
 *
 */
static void scu_ssp_task_request_construct_task_context(
	struct scic_sds_request *sci_req)
{
	struct scu_task_context *task_context;

	task_context = scic_sds_request_get_task_context(sci_req);

	scu_ssp_reqeust_construct_task_context(sci_req, task_context);

	task_context->control_frame                = 1;
	task_context->priority                     = SCU_TASK_PRIORITY_HIGH;
	task_context->task_type                    = SCU_TASK_TYPE_RAW_FRAME;
	task_context->transfer_length_bytes        = 0;
	task_context->type.ssp.frame_type          = SCI_SAS_TASK_FRAME;
	task_context->ssp_command_iu_length = sizeof(struct sci_ssp_task_iu) / sizeof(u32);
}


/**
 * This method constructs the SSP Command IU data for this ssp passthrough
 *    comand request object.
 * @sci_req: This parameter specifies the request object for which the SSP
 *    command information unit is being built.
 *
 * enum sci_status, returns invalid parameter is cdb > 16
 */


/**
 * This method constructs the SATA request object.
 * @sci_req:
 * @sat_protocol:
 * @transfer_length:
 * @data_direction:
 * @copy_rx_frame:
 *
 * enum sci_status
 */
static enum sci_status scic_io_request_construct_sata(struct scic_sds_request *sci_req,
						      u8 proto, u32 len,
						      enum dma_data_direction dir,
						      bool copy)
{
	enum sci_status status = SCI_SUCCESS;

	switch (proto) {
	case SAT_PROTOCOL_PIO_DATA_IN:
	case SAT_PROTOCOL_PIO_DATA_OUT:
		status = scic_sds_stp_pio_request_construct(sci_req, proto, copy);
		break;

	case SAT_PROTOCOL_UDMA_DATA_IN:
	case SAT_PROTOCOL_UDMA_DATA_OUT:
		status = scic_sds_stp_udma_request_construct(sci_req, len, dir);
		break;

	case SAT_PROTOCOL_ATA_HARD_RESET:
	case SAT_PROTOCOL_SOFT_RESET:
		status = scic_sds_stp_soft_reset_request_construct(sci_req);
		break;

	case SAT_PROTOCOL_NON_DATA:
		status = scic_sds_stp_non_data_request_construct(sci_req);
		break;

	case SAT_PROTOCOL_FPDMA:
		status = scic_sds_stp_ncq_request_construct(sci_req, len, dir);
		break;

	case SAT_PROTOCOL_DMA_QUEUED:
	case SAT_PROTOCOL_DMA:
	case SAT_PROTOCOL_DEVICE_DIAGNOSTIC:
	case SAT_PROTOCOL_DEVICE_RESET:
	case SAT_PROTOCOL_RETURN_RESPONSE_INFO:
	default:
		dev_err(scic_to_dev(sci_req->owning_controller),
			"%s: SCIC IO Request 0x%p received un-handled "
			"SAT Protocl %d.\n",
			__func__, sci_req, proto);

		status = SCI_FAILURE;
		break;
	}

	return status;
}

u32 scic_io_request_get_object_size(void)
{
	u32 ssp_request_size;
	u32 stp_request_size;
	u32 smp_request_size;

	ssp_request_size = scic_sds_ssp_request_get_object_size();
	stp_request_size = scic_sds_stp_request_get_object_size();
	smp_request_size = scic_sds_smp_request_get_object_size();

	return max(ssp_request_size, max(stp_request_size, smp_request_size));
}

enum sci_status scic_io_request_construct_basic_ssp(
	struct scic_sds_request *sci_req)
{
	struct isci_request *isci_request =
		(struct isci_request *)sci_object_get_association(sci_req);

	sci_req->protocol = SCIC_SSP_PROTOCOL;

	scu_ssp_io_request_construct_task_context(
		sci_req,
		isci_request_io_request_get_data_direction(isci_request),
		isci_request_io_request_get_transfer_length(isci_request));

	scic_sds_io_request_build_ssp_command_iu(sci_req);

	sci_base_state_machine_change_state(&sci_req->state_machine,
		SCI_BASE_REQUEST_STATE_CONSTRUCTED);

	return SCI_SUCCESS;
}


enum sci_status scic_task_request_construct_ssp(
	struct scic_sds_request *sci_req)
{
	/* Construct the SSP Task SCU Task Context */
	scu_ssp_task_request_construct_task_context(sci_req);

	/* Fill in the SSP Task IU */
	scic_sds_task_request_build_ssp_task_iu(sci_req);

	sci_base_state_machine_change_state(&sci_req->state_machine,
		SCI_BASE_REQUEST_STATE_CONSTRUCTED);

	return SCI_SUCCESS;
}


enum sci_status scic_io_request_construct_basic_sata(
		struct scic_sds_request *sci_req)
{
	enum sci_status status;
	struct scic_sds_stp_request *stp_req;
	u8 proto;
	u32 len;
	enum dma_data_direction dir;
	bool copy = false;
	struct isci_request *isci_request =
		(struct isci_request *)sci_object_get_association(sci_req);
	struct sas_task *task = isci_request_access_task(isci_request);

	stp_req = container_of(sci_req, typeof(*stp_req), parent);

	sci_req->protocol = SCIC_STP_PROTOCOL;

	len = isci_request_io_request_get_transfer_length(isci_request);
	dir = isci_request_io_request_get_data_direction(isci_request);
	proto = isci_sata_get_sat_protocol(isci_request);
	copy = (task->data_dir == DMA_NONE) ? false : true;

	status = scic_io_request_construct_sata(sci_req, proto, len, dir, copy);

	if (status == SCI_SUCCESS)
		sci_base_state_machine_change_state(&sci_req->state_machine,
			SCI_BASE_REQUEST_STATE_CONSTRUCTED);

	return status;
}


enum sci_status scic_task_request_construct_sata(
	struct scic_sds_request *sci_req)
{
	enum sci_status status;
	u8 sat_protocol;
	struct isci_request *isci_request =
		(struct isci_request *)sci_object_get_association(sci_req);

	sat_protocol = isci_sata_get_sat_protocol(isci_request);

	switch (sat_protocol) {
	case SAT_PROTOCOL_ATA_HARD_RESET:
	case SAT_PROTOCOL_SOFT_RESET:
		status = scic_sds_stp_soft_reset_request_construct(sci_req);
		break;

	default:
		dev_err(scic_to_dev(sci_req->owning_controller),
			"%s: SCIC IO Request 0x%p received un-handled SAT "
			"Protocl %d.\n",
			__func__,
			sci_req,
			sat_protocol);

		status = SCI_FAILURE;
		break;
	}

	if (status == SCI_SUCCESS)
		sci_base_state_machine_change_state(&sci_req->state_machine,
			SCI_BASE_REQUEST_STATE_CONSTRUCTED);

	return status;
}


u16 scic_io_request_get_io_tag(
	struct scic_sds_request *sci_req)
{
	return sci_req->io_tag;
}


u32 scic_request_get_controller_status(
	struct scic_sds_request *sci_req)
{
	return sci_req->scu_status;
}


void *scic_io_request_get_command_iu_address(
	struct scic_sds_request *sci_req)
{
	return sci_req->command_buffer;
}


void *scic_io_request_get_response_iu_address(
	struct scic_sds_request *sci_req)
{
	return sci_req->response_buffer;
}


#define SCU_TASK_CONTEXT_SRAM 0x200000
u32 scic_io_request_get_number_of_bytes_transferred(
	struct scic_sds_request *scic_sds_request)
{
	struct scic_sds_controller *scic = scic_sds_request->owning_controller;
	u32 ret_val = 0;

	if (readl(&scic->smu_registers->address_modifier) == 0) {
		void __iomem *scu_reg_base = scic->scu_registers;
		/*
		 * get the bytes of data from the Address == BAR1 + 20002Ch + (256*TCi) where
		 *   BAR1 is the scu_registers
		 *   0x20002C = 0x200000 + 0x2c
		 *            = start of task context SRAM + offset of (type.ssp.data_offset)
		 *   TCi is the io_tag of struct scic_sds_request */
		ret_val = readl(scu_reg_base +
				(SCU_TASK_CONTEXT_SRAM + offsetof(struct scu_task_context, type.ssp.data_offset)) +
				((sizeof(struct scu_task_context)) * scic_sds_io_tag_get_index(scic_sds_request->io_tag)));
	}

	return ret_val;
}


/*
 * ****************************************************************************
 * * SCIC SDS Interface Implementation
 * **************************************************************************** */

enum sci_status
scic_sds_request_start(struct scic_sds_request *request)
{
	if (request->device_sequence !=
	    scic_sds_remote_device_get_sequence(request->target_device))
		return SCI_FAILURE;

	if (request->state_handlers->start_handler)
		return request->state_handlers->start_handler(request);

	dev_warn(scic_to_dev(request->owning_controller),
		 "%s: SCIC IO Request requested to start while in wrong "
		 "state %d\n",
		 __func__,
		 sci_base_state_machine_get_state(&request->state_machine));

	return SCI_FAILURE_INVALID_STATE;
}

enum sci_status
scic_sds_io_request_terminate(struct scic_sds_request *request)
{
	if (request->state_handlers->abort_handler)
		return request->state_handlers->abort_handler(request);

	dev_warn(scic_to_dev(request->owning_controller),
		"%s: SCIC IO Request requested to abort while in wrong "
		"state %d\n",
		__func__,
		sci_base_state_machine_get_state(&request->state_machine));

	return SCI_FAILURE_INVALID_STATE;
}

enum sci_status
scic_sds_io_request_complete(struct scic_sds_request *request)
{
	if (request->state_handlers->complete_handler)
		return request->state_handlers->complete_handler(request);

	dev_warn(scic_to_dev(request->owning_controller),
		"%s: SCIC IO Request requested to complete while in wrong "
		"state %d\n",
		__func__,
		sci_base_state_machine_get_state(&request->state_machine));

	return SCI_FAILURE_INVALID_STATE;
}

enum sci_status scic_sds_io_request_event_handler(
	struct scic_sds_request *request,
	u32 event_code)
{
	if (request->state_handlers->event_handler)
		return request->state_handlers->event_handler(request, event_code);

	dev_warn(scic_to_dev(request->owning_controller),
		 "%s: SCIC IO Request given event code notification %x while "
		 "in wrong state %d\n",
		 __func__,
		 event_code,
		 sci_base_state_machine_get_state(&request->state_machine));

	return SCI_FAILURE_INVALID_STATE;
}

enum sci_status
scic_sds_io_request_tc_completion(struct scic_sds_request *request, u32 completion_code)
{
	if (request->state_machine.current_state_id == SCI_BASE_REQUEST_STATE_STARTED &&
	    request->has_started_substate_machine == false)
		return scic_sds_request_started_state_tc_completion_handler(request, completion_code);
	else if (request->state_handlers->tc_completion_handler)
		return request->state_handlers->tc_completion_handler(request, completion_code);

	dev_warn(scic_to_dev(request->owning_controller),
		"%s: SCIC IO Request given task completion notification %x "
		"while in wrong state %d\n",
		__func__,
		completion_code,
		sci_base_state_machine_get_state(&request->state_machine));

	return SCI_FAILURE_INVALID_STATE;

}


/**
 *
 * @sci_req: The SCIC_SDS_IO_REQUEST_T object for which the start
 *    operation is to be executed.
 * @frame_index: The frame index returned by the hardware for the reqeust
 *    object.
 *
 * This method invokes the core state frame handler for the
 * SCIC_SDS_IO_REQUEST_T object. enum sci_status
 */
enum sci_status scic_sds_io_request_frame_handler(
	struct scic_sds_request *request,
	u32 frame_index)
{
	if (request->state_handlers->frame_handler)
		return request->state_handlers->frame_handler(request, frame_index);

	dev_warn(scic_to_dev(request->owning_controller),
		 "%s: SCIC IO Request given unexpected frame %x while in "
		 "state %d\n",
		 __func__,
		 frame_index,
		 sci_base_state_machine_get_state(&request->state_machine));

	scic_sds_controller_release_frame(request->owning_controller, frame_index);
	return SCI_FAILURE_INVALID_STATE;
}

/**
 *
 * @sci_req: The SCIC_SDS_IO_REQUEST_T object for which the task start
 *    operation is to be executed.
 *
 * This method invokes the core state task complete handler for the
 * SCIC_SDS_IO_REQUEST_T object. enum sci_status
 */

/*
 * ****************************************************************************
 * * SCIC SDS PROTECTED METHODS
 * **************************************************************************** */

/**
 * This method copies response data for requests returning response data
 *    instead of sense data.
 * @sci_req: This parameter specifies the request object for which to copy
 *    the response data.
 *
 */
void scic_sds_io_request_copy_response(struct scic_sds_request *sds_request)
{
	void *response_buffer;
	u32 user_response_length;
	u32 core_response_length;
	struct sci_ssp_response_iu *ssp_response;
	struct isci_request *isci_request =
		(struct isci_request *)sci_object_get_association(sds_request);

	ssp_response =
		(struct sci_ssp_response_iu *)sds_request->response_buffer;

	response_buffer =
		isci_task_ssp_request_get_response_data_address(
				isci_request);

	user_response_length =
		isci_task_ssp_request_get_response_data_length(
				isci_request);

	core_response_length = sci_ssp_get_response_data_length(
					ssp_response->response_data_length);

	user_response_length = min(user_response_length, core_response_length);

	memcpy(response_buffer, ssp_response->data, user_response_length);
}

/*
 * *****************************************************************************
 * *  CONSTRUCTED STATE HANDLERS
 * ***************************************************************************** */

/*
 * This method implements the action taken when a constructed
 * SCIC_SDS_IO_REQUEST_T object receives a scic_sds_request_start() request.
 * This method will, if necessary, allocate a TCi for the io request object and
 * then will, if necessary, copy the constructed TC data into the actual TC
 * buffer.  If everything is successful the post context field is updated with
 * the TCi so the controller can post the request to the hardware. enum sci_status
 * SCI_SUCCESS SCI_FAILURE_INSUFFICIENT_RESOURCES
 */
static enum sci_status scic_sds_request_constructed_state_start_handler(
	struct scic_sds_request *request)
{
	struct scu_task_context *task_context;

	if (request->io_tag == SCI_CONTROLLER_INVALID_IO_TAG) {
		request->io_tag =
			scic_controller_allocate_io_tag(request->owning_controller);
	}

	/* Record the IO Tag in the request */
	if (request->io_tag != SCI_CONTROLLER_INVALID_IO_TAG) {
		task_context = request->task_context_buffer;

		task_context->task_index = scic_sds_io_tag_get_index(request->io_tag);

		switch (task_context->protocol_type) {
		case SCU_TASK_CONTEXT_PROTOCOL_SMP:
		case SCU_TASK_CONTEXT_PROTOCOL_SSP:
			/* SSP/SMP Frame */
			task_context->type.ssp.tag = request->io_tag;
			task_context->type.ssp.target_port_transfer_tag = 0xFFFF;
			break;

		case SCU_TASK_CONTEXT_PROTOCOL_STP:
			/*
			 * STP/SATA Frame
			 * task_context->type.stp.ncq_tag = request->ncq_tag; */
			break;

		case SCU_TASK_CONTEXT_PROTOCOL_NONE:
			/* / @todo When do we set no protocol type? */
			break;

		default:
			/* This should never happen since we build the IO requests */
			break;
		}

		/*
		 * Check to see if we need to copy the task context buffer
		 * or have been building into the task context buffer */
		if (request->was_tag_assigned_by_user == false) {
			scic_sds_controller_copy_task_context(
				request->owning_controller, request);
		}

		/* Add to the post_context the io tag value */
		request->post_context |= scic_sds_io_tag_get_index(request->io_tag);

		/* Everything is good go ahead and change state */
		sci_base_state_machine_change_state(&request->state_machine,
			SCI_BASE_REQUEST_STATE_STARTED);

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INSUFFICIENT_RESOURCES;
}

/*
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_terminate() request. Since the request
 * has not yet been posted to the hardware the request transitions to the
 * completed state. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_request_constructed_state_abort_handler(
	struct scic_sds_request *request)
{
	/*
	 * This request has been terminated by the user make sure that the correct
	 * status code is returned */
	scic_sds_request_set_status(request,
		SCU_TASK_DONE_TASK_ABORT,
		SCI_FAILURE_IO_TERMINATED);

	sci_base_state_machine_change_state(&request->state_machine,
		SCI_BASE_REQUEST_STATE_COMPLETED);
	return SCI_SUCCESS;
}

/*
 * *****************************************************************************
 * *  STARTED STATE HANDLERS
 * ***************************************************************************** */

/*
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_terminate() request. Since the request
 * has been posted to the hardware the io request state is changed to the
 * aborting state. enum sci_status SCI_SUCCESS
 */
enum sci_status scic_sds_request_started_state_abort_handler(
	struct scic_sds_request *request)
{
	if (request->has_started_substate_machine)
		sci_base_state_machine_stop(&request->started_substate_machine);

	sci_base_state_machine_change_state(&request->state_machine,
		SCI_BASE_REQUEST_STATE_ABORTING);
	return SCI_SUCCESS;
}

/**
 * scic_sds_request_started_state_tc_completion_handler() - This method process
 *    TC (task context) completions for normal IO request (i.e. Task/Abort
 *    Completions of type 0).  This method will update the
 *    SCIC_SDS_IO_REQUEST_T::status field.
 * @sci_req: This parameter specifies the request for which a completion
 *    occurred.
 * @completion_code: This parameter specifies the completion code received from
 *    the SCU.
 *
 */
enum sci_status scic_sds_request_started_state_tc_completion_handler(
	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	u8 data_present;
	struct sci_ssp_response_iu *response_buffer;

	/**
	 * @todo Any SDMA return code of other than 0 is bad
	 *       decode 0x003C0000 to determine SDMA status
	 */
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_request_set_status(
			sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS
			);
		break;

	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_EARLY_RESP):
	{
		/*
		 * There are times when the SCU hardware will return an early response
		 * because the io request specified more data than is returned by the
		 * target device (mode pages, inquiry data, etc.).  We must check the
		 * response stats to see if this is truly a failed request or a good
		 * request that just got completed early. */
		struct sci_ssp_response_iu *response = (struct sci_ssp_response_iu *)
						  sci_req->response_buffer;
		scic_word_copy_with_swap(
			sci_req->response_buffer,
			sci_req->response_buffer,
			sizeof(struct sci_ssp_response_iu) / sizeof(u32)
			);

		if (response->status == 0) {
			scic_sds_request_set_status(
				sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS_IO_DONE_EARLY
				);
		} else {
			scic_sds_request_set_status(
				sci_req,
				SCU_TASK_DONE_CHECK_RESPONSE,
				SCI_FAILURE_IO_RESPONSE_VALID
				);
		}
	}
	break;

	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_CHECK_RESPONSE):
		scic_word_copy_with_swap(
			sci_req->response_buffer,
			sci_req->response_buffer,
			sizeof(struct sci_ssp_response_iu) / sizeof(u32)
			);

		scic_sds_request_set_status(
			sci_req,
			SCU_TASK_DONE_CHECK_RESPONSE,
			SCI_FAILURE_IO_RESPONSE_VALID
			);
		break;

	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_RESP_LEN_ERR):
		/*
		 * / @todo With TASK_DONE_RESP_LEN_ERR is the response frame guaranteed
		 * /       to be received before this completion status is posted? */
		response_buffer =
			(struct sci_ssp_response_iu *)sci_req->response_buffer;
		data_present =
			response_buffer->data_present & SCI_SSP_RESPONSE_IU_DATA_PRESENT_MASK;

		if ((data_present == 0x01) || (data_present == 0x02)) {
			scic_sds_request_set_status(
				sci_req,
				SCU_TASK_DONE_CHECK_RESPONSE,
				SCI_FAILURE_IO_RESPONSE_VALID
				);
		} else {
			scic_sds_request_set_status(
				sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS
				);
		}
		break;

	/* only stp device gets suspended. */
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_ACK_NAK_TO):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_LL_PERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_NAK_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_DATA_LEN_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_LL_ABORT_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_XR_WD_LEN):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_MAX_PLD_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_RESP):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_SDBFIS):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_REG_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SDB_ERR):
		if (sci_req->protocol == SCIC_STP_PROTOCOL) {
			scic_sds_request_set_status(
				sci_req,
				SCU_GET_COMPLETION_TL_STATUS(completion_code) >> SCU_COMPLETION_TL_STATUS_SHIFT,
				SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED
				);
		} else {
			scic_sds_request_set_status(
				sci_req,
				SCU_GET_COMPLETION_TL_STATUS(completion_code) >> SCU_COMPLETION_TL_STATUS_SHIFT,
				SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
				);
		}
		break;

	/* both stp/ssp device gets suspended */
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_LF_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_WRONG_DESTINATION):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_1):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_2):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_3):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_BAD_DESTINATION):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_ZONE_VIOLATION):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_STP_RESOURCES_BUSY):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_PROTOCOL_NOT_SUPPORTED):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_OPEN_REJECT_CONNECTION_RATE_NOT_SUPPORTED):
		scic_sds_request_set_status(
			sci_req,
			SCU_GET_COMPLETION_TL_STATUS(completion_code) >> SCU_COMPLETION_TL_STATUS_SHIFT,
			SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED
			);
		break;

	/* neither ssp nor stp gets suspended. */
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_NAK_CMD_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_XR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_XR_IU_LEN_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SDMA_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_OFFSET_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_EXCESS_DATA):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_RESP_TO_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_UFI_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_FRM_TYPE_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_LL_RX_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_DATA):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_OPEN_FAIL):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_VIIT_ENTRY_NV):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_IIT_ENTRY_NV):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_RNCNV_OUTBOUND):
	default:
		scic_sds_request_set_status(
			sci_req,
			SCU_GET_COMPLETION_TL_STATUS(completion_code) >> SCU_COMPLETION_TL_STATUS_SHIFT,
			SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
			);
		break;
	}

	/**
	 * @todo This is probably wrong for ACK/NAK timeout conditions
	 */

	/* In all cases we will treat this as the completion of the IO request. */
	sci_base_state_machine_change_state(&sci_req->state_machine,
		SCI_BASE_REQUEST_STATE_COMPLETED);
	return SCI_SUCCESS;
}

/*
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_frame_handler() request. This method
 * first determines the frame type received.  If this is a response frame then
 * the response data is copied to the io request response buffer for processing
 * at completion time. If the frame type is not a response buffer an error is
 * logged. enum sci_status SCI_SUCCESS SCI_FAILURE_INVALID_PARAMETER_VALUE
 */
static enum sci_status scic_sds_request_started_state_frame_handler(
	struct scic_sds_request *sci_req,
	u32 frame_index)
{
	enum sci_status status;
	struct sci_ssp_frame_header *frame_header;

	/* / @todo If this is a response frame we must record that we received it */
	status = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_request_get_controller(sci_req)->uf_control),
		frame_index,
		(void **)&frame_header
		);

	if (frame_header->frame_type == SCI_SAS_RESPONSE_FRAME) {
		struct sci_ssp_response_iu *response_buffer;

		status = scic_sds_unsolicited_frame_control_get_buffer(
			&(scic_sds_request_get_controller(sci_req)->uf_control),
			frame_index,
			(void **)&response_buffer
			);

		scic_word_copy_with_swap(
			sci_req->response_buffer,
			(u32 *)response_buffer,
			sizeof(struct sci_ssp_response_iu)
			);

		response_buffer = (struct sci_ssp_response_iu *)sci_req->response_buffer;

		if ((response_buffer->data_present == 0x01) ||
		    (response_buffer->data_present == 0x02)) {
			scic_sds_request_set_status(
				sci_req,
				SCU_TASK_DONE_CHECK_RESPONSE,
				SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR
				);
		} else
			scic_sds_request_set_status(
				sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS
				);
	} else
		/* This was not a response frame why did it get forwarded? */
		dev_err(scic_to_dev(sci_req->owning_controller),
			"%s: SCIC IO Request 0x%p received unexpected "
			"frame %d type 0x%02x\n",
			__func__,
			sci_req,
			frame_index,
			frame_header->frame_type);

	/*
	 * In any case we are done with this frame buffer return it to the
	 * controller */
	scic_sds_controller_release_frame(
		sci_req->owning_controller, frame_index
		);

	return SCI_SUCCESS;
}

/*
 * *****************************************************************************
 * *  COMPLETED STATE HANDLERS
 * ***************************************************************************** */


/*
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_complete() request. This method frees up
 * any io request resources that have been allocated and transitions the
 * request to its final state. Consider stopping the state machine instead of
 * transitioning to the final state? enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_request_completed_state_complete_handler(
	struct scic_sds_request *request)
{
	if (request->was_tag_assigned_by_user != true) {
		scic_controller_free_io_tag(
			request->owning_controller, request->io_tag);
	}

	if (request->saved_rx_frame_index != SCU_INVALID_FRAME_INDEX) {
		scic_sds_controller_release_frame(
			request->owning_controller, request->saved_rx_frame_index);
	}

	sci_base_state_machine_change_state(&request->state_machine,
		SCI_BASE_REQUEST_STATE_FINAL);
	return SCI_SUCCESS;
}

/*
 * *****************************************************************************
 * *  ABORTING STATE HANDLERS
 * ***************************************************************************** */

/*
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_terminate() request. This method is the
 * io request aborting state abort handlers.  On receipt of a multiple
 * terminate requests the io request will transition to the completed state.
 * This should not happen in normal operation. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_request_aborting_state_abort_handler(
	struct scic_sds_request *request)
{
	sci_base_state_machine_change_state(&request->state_machine,
		SCI_BASE_REQUEST_STATE_COMPLETED);
	return SCI_SUCCESS;
}

/*
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_task_completion() request. This method
 * decodes the completion type waiting for the abort task complete
 * notification. When the abort task complete is received the io request
 * transitions to the completed state. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_request_aborting_state_tc_completion_handler(
	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case (SCU_TASK_DONE_GOOD << SCU_COMPLETION_TL_STATUS_SHIFT):
	case (SCU_TASK_DONE_TASK_ABORT << SCU_COMPLETION_TL_STATUS_SHIFT):
		scic_sds_request_set_status(
			sci_req, SCU_TASK_DONE_TASK_ABORT, SCI_FAILURE_IO_TERMINATED
			);

		sci_base_state_machine_change_state(&sci_req->state_machine,
			SCI_BASE_REQUEST_STATE_COMPLETED);
		break;

	default:
		/*
		 * Unless we get some strange error wait for the task abort to complete
		 * TODO: Should there be a state change for this completion? */
		break;
	}

	return SCI_SUCCESS;
}

/*
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_frame_handler() request. This method
 * discards the unsolicited frame since we are waiting for the abort task
 * completion. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_request_aborting_state_frame_handler(
	struct scic_sds_request *sci_req,
	u32 frame_index)
{
	/* TODO: Is it even possible to get an unsolicited frame in the aborting state? */

	scic_sds_controller_release_frame(
		sci_req->owning_controller, frame_index);

	return SCI_SUCCESS;
}

static const struct scic_sds_io_request_state_handler scic_sds_request_state_handler_table[] = {
	[SCI_BASE_REQUEST_STATE_INITIAL] = {
	},
	[SCI_BASE_REQUEST_STATE_CONSTRUCTED] = {
		.start_handler		= scic_sds_request_constructed_state_start_handler,
		.abort_handler		= scic_sds_request_constructed_state_abort_handler,
	},
	[SCI_BASE_REQUEST_STATE_STARTED] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_request_started_state_tc_completion_handler,
		.frame_handler		= scic_sds_request_started_state_frame_handler,
	},
	[SCI_BASE_REQUEST_STATE_COMPLETED] = {
		.complete_handler	= scic_sds_request_completed_state_complete_handler,
	},
	[SCI_BASE_REQUEST_STATE_ABORTING] = {
		.abort_handler		= scic_sds_request_aborting_state_abort_handler,
		.tc_completion_handler	= scic_sds_request_aborting_state_tc_completion_handler,
		.frame_handler		= scic_sds_request_aborting_state_frame_handler,
	},
	[SCI_BASE_REQUEST_STATE_FINAL] = {
	},
};

/**
 * scic_sds_request_initial_state_enter() -
 * @object: This parameter specifies the base object for which the state
 *    transition is occurring.
 *
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_INITIAL state. This state is entered when the initial
 * base request is constructed. Entry into the initial state sets all handlers
 * for the io request object to their default handlers. none
 */
static void scic_sds_request_initial_state_enter(void *object)
{
	struct scic_sds_request *sci_req = (struct scic_sds_request *)object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCI_BASE_REQUEST_STATE_INITIAL
		);
}

/**
 * scic_sds_request_constructed_state_enter() -
 * @object: The io request object that is to enter the constructed state.
 *
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_CONSTRUCTED state. The method sets the state handlers
 * for the the constructed state. none
 */
static void scic_sds_request_constructed_state_enter(void *object)
{
	struct scic_sds_request *sci_req = (struct scic_sds_request *)object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCI_BASE_REQUEST_STATE_CONSTRUCTED
		);
}

/**
 * scic_sds_request_started_state_enter() -
 * @object: This parameter specifies the base object for which the state
 *    transition is occurring.  This is cast into a SCIC_SDS_IO_REQUEST object.
 *
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_STARTED state. If the io request object type is a
 * SCSI Task request we must enter the started substate machine. none
 */
static void scic_sds_request_started_state_enter(void *object)
{
	struct scic_sds_request *sci_req = (struct scic_sds_request *)object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCI_BASE_REQUEST_STATE_STARTED
		);

	/*
	 * Most of the request state machines have a started substate machine so
	 * start its execution on the entry to the started state. */
	if (sci_req->has_started_substate_machine == true)
		sci_base_state_machine_start(&sci_req->started_substate_machine);
}

/**
 * scic_sds_request_started_state_exit() -
 * @object: This parameter specifies the base object for which the state
 *    transition is occurring.  This object is cast into a SCIC_SDS_IO_REQUEST
 *    object.
 *
 * This method implements the actions taken when exiting the
 * SCI_BASE_REQUEST_STATE_STARTED state. For task requests the action will be
 * to stop the started substate machine. none
 */
static void scic_sds_request_started_state_exit(void *object)
{
	struct scic_sds_request *sci_req = (struct scic_sds_request *)object;

	if (sci_req->has_started_substate_machine == true)
		sci_base_state_machine_stop(&sci_req->started_substate_machine);
}

/**
 * scic_sds_request_completed_state_enter() -
 * @object: This parameter specifies the base object for which the state
 *    transition is occurring.  This object is cast into a SCIC_SDS_IO_REQUEST
 *    object.
 *
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_COMPLETED state.  This state is entered when the
 * SCIC_SDS_IO_REQUEST has completed.  The method will decode the request
 * completion status and convert it to an enum sci_status to return in the
 * completion callback function. none
 */
static void scic_sds_request_completed_state_enter(void *object)
{
	struct scic_sds_request *sci_req = (struct scic_sds_request *)object;
	struct scic_sds_controller *scic =
		scic_sds_request_get_controller(sci_req);
	struct isci_host *ihost = scic->ihost;
	struct isci_request *ireq = sci_object_get_association(sci_req);

	SET_STATE_HANDLER(sci_req,
			  scic_sds_request_state_handler_table,
			  SCI_BASE_REQUEST_STATE_COMPLETED);

	/* Tell the SCI_USER that the IO request is complete */
	if (sci_req->is_task_management_request == false)
		isci_request_io_request_complete(ihost,
						 ireq,
						 sci_req->sci_status);
	else
		isci_task_request_complete(ihost, ireq, sci_req->sci_status);
}

/**
 * scic_sds_request_aborting_state_enter() -
 * @object: This parameter specifies the base object for which the state
 *    transition is occurring.  This object is cast into a SCIC_SDS_IO_REQUEST
 *    object.
 *
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_ABORTING state. none
 */
static void scic_sds_request_aborting_state_enter(void *object)
{
	struct scic_sds_request *sci_req = (struct scic_sds_request *)object;

	/* Setting the abort bit in the Task Context is required by the silicon. */
	sci_req->task_context_buffer->abort = 1;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCI_BASE_REQUEST_STATE_ABORTING
		);
}

/**
 * scic_sds_request_final_state_enter() -
 * @object: This parameter specifies the base object for which the state
 *    transition is occurring.  This is cast into a SCIC_SDS_IO_REQUEST object.
 *
 * This method implements the actions taken when entering the
 * SCI_BASE_REQUEST_STATE_FINAL state. The only action required is to put the
 * state handlers in place. none
 */
static void scic_sds_request_final_state_enter(void *object)
{
	struct scic_sds_request *sci_req = (struct scic_sds_request *)object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCI_BASE_REQUEST_STATE_FINAL
		);
}

static const struct sci_base_state scic_sds_request_state_table[] = {
	[SCI_BASE_REQUEST_STATE_INITIAL] = {
		.enter_state = scic_sds_request_initial_state_enter,
	},
	[SCI_BASE_REQUEST_STATE_CONSTRUCTED] = {
		.enter_state = scic_sds_request_constructed_state_enter,
	},
	[SCI_BASE_REQUEST_STATE_STARTED] = {
		.enter_state = scic_sds_request_started_state_enter,
		.exit_state  = scic_sds_request_started_state_exit
	},
	[SCI_BASE_REQUEST_STATE_COMPLETED] = {
		.enter_state = scic_sds_request_completed_state_enter,
	},
	[SCI_BASE_REQUEST_STATE_ABORTING] = {
		.enter_state = scic_sds_request_aborting_state_enter,
	},
	[SCI_BASE_REQUEST_STATE_FINAL] = {
		.enter_state = scic_sds_request_final_state_enter,
	},
};

static void scic_sds_general_request_construct(struct scic_sds_controller *scic,
					       struct scic_sds_remote_device *sci_dev,
					       u16 io_tag,
					       void *user_io_request_object,
					       struct scic_sds_request *sci_req)
{
	sci_req->parent.private = NULL;
	sci_base_state_machine_construct(&sci_req->state_machine, &sci_req->parent,
			scic_sds_request_state_table, SCI_BASE_REQUEST_STATE_INITIAL);
	sci_base_state_machine_start(&sci_req->state_machine);

	sci_req->io_tag = io_tag;
	sci_req->user_request = user_io_request_object;
	sci_req->owning_controller = scic;
	sci_req->target_device = sci_dev;
	sci_req->has_started_substate_machine = false;
	sci_req->protocol = SCIC_NO_PROTOCOL;
	sci_req->saved_rx_frame_index = SCU_INVALID_FRAME_INDEX;
	sci_req->device_sequence = scic_sds_remote_device_get_sequence(sci_dev);

	sci_req->sci_status   = SCI_SUCCESS;
	sci_req->scu_status   = 0;
	sci_req->post_context = 0xFFFFFFFF;

	sci_req->is_task_management_request = false;

	if (io_tag == SCI_CONTROLLER_INVALID_IO_TAG) {
		sci_req->was_tag_assigned_by_user = false;
		sci_req->task_context_buffer = NULL;
	} else {
		sci_req->was_tag_assigned_by_user = true;

		sci_req->task_context_buffer =
			scic_sds_controller_get_task_context_buffer(scic, io_tag);
	}
}

enum sci_status scic_io_request_construct(struct scic_sds_controller *scic,
					  struct scic_sds_remote_device *sci_dev,
					  u16 io_tag,
					  void *user_io_request_object,
					  struct scic_sds_request *sci_req,
					  struct scic_sds_request **new_scic_io_request_handle)
{
	struct domain_device *dev = sci_dev_to_domain(sci_dev);
	enum sci_status status = SCI_SUCCESS;

	/* Build the common part of the request */
	scic_sds_general_request_construct(scic, sci_dev, io_tag,
					   user_io_request_object, sci_req);

	if (sci_dev->rnc.remote_node_index == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX)
		return SCI_FAILURE_INVALID_REMOTE_DEVICE;

	if (dev->dev_type == SAS_END_DEV) {
		scic_sds_ssp_io_request_assign_buffers(sci_req);
	} else if (dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP)) {
		scic_sds_stp_request_assign_buffers(sci_req);
		memset(sci_req->command_buffer, 0, sizeof(struct sata_fis_reg_h2d));
	} else if (dev_is_expander(dev)) {
		scic_sds_smp_request_assign_buffers(sci_req);
		memset(sci_req->command_buffer, 0, sizeof(struct smp_request));
	} else
		status = SCI_FAILURE_UNSUPPORTED_PROTOCOL;

	if (status == SCI_SUCCESS) {
		memset(sci_req->task_context_buffer, 0,
			SCI_FIELD_OFFSET(struct scu_task_context, sgl_pair_ab));
		*new_scic_io_request_handle = sci_req;
	}

	return status;
}

enum sci_status scic_task_request_construct(struct scic_sds_controller *scic,
					    struct scic_sds_remote_device *sci_dev,
					    u16 io_tag,
					    void *user_io_request_object,
					    struct scic_sds_request *sci_req,
					    struct scic_sds_request **new_sci_req)
{
	struct domain_device *dev = sci_dev_to_domain(sci_dev);
	enum sci_status status = SCI_SUCCESS;

	/* Build the common part of the request */
	scic_sds_general_request_construct(scic, sci_dev, io_tag,
					   user_io_request_object,
					   sci_req);

	if (dev->dev_type == SAS_END_DEV) {
		scic_sds_ssp_task_request_assign_buffers(sci_req);

		sci_req->has_started_substate_machine = true;

		/* Construct the started sub-state machine. */
		sci_base_state_machine_construct(
			&sci_req->started_substate_machine,
			&sci_req->parent,
			scic_sds_io_request_started_task_mgmt_substate_table,
			SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION
			);
	} else if (dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP))
		scic_sds_stp_request_assign_buffers(sci_req);
	else
		status = SCI_FAILURE_UNSUPPORTED_PROTOCOL;

	if (status == SCI_SUCCESS) {
		sci_req->is_task_management_request = true;
		memset(sci_req->task_context_buffer, 0, sizeof(struct scu_task_context));
		*new_sci_req = sci_req;
	}

	return status;
}
