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

#include "isci.h"
#include "task.h"
#include "request.h"
#include "sata.h"
#include "scu_completion_codes.h"
#include "scu_event_codes.h"
#include "sas.h"

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

	return &sci_req->sg_table[sgl_pair_index - 2];
}

/**
 * This function will build the SGL list for an IO request.
 * @sci_req: This parameter specifies the IO request for which to build
 *    the Scatter-Gather List.
 *
 */
static void scic_sds_request_build_sgl(struct scic_sds_request *sds_request)
{
	struct isci_request *isci_request = sci_req_to_ireq(sds_request);
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

static void scic_sds_io_request_build_ssp_command_iu(struct scic_sds_request *sci_req)
{
	struct ssp_cmd_iu *cmd_iu;
	struct isci_request *ireq = sci_req_to_ireq(sci_req);
	struct sas_task *task = isci_request_access_task(ireq);

	cmd_iu = &sci_req->ssp.cmd;

	memcpy(cmd_iu->LUN, task->ssp_task.LUN, 8);
	cmd_iu->add_cdb_len = 0;
	cmd_iu->_r_a = 0;
	cmd_iu->_r_b = 0;
	cmd_iu->en_fburst = 0; /* unsupported */
	cmd_iu->task_prio = task->ssp_task.task_prio;
	cmd_iu->task_attr = task->ssp_task.task_attr;
	cmd_iu->_r_c = 0;

	sci_swab32_cpy(&cmd_iu->cdb, task->ssp_task.cdb,
		       sizeof(task->ssp_task.cdb) / sizeof(u32));
}

static void scic_sds_task_request_build_ssp_task_iu(struct scic_sds_request *sci_req)
{
	struct ssp_task_iu *task_iu;
	struct isci_request *ireq = sci_req_to_ireq(sci_req);
	struct sas_task *task = isci_request_access_task(ireq);
	struct isci_tmf *isci_tmf = isci_request_access_tmf(ireq);

	task_iu = &sci_req->ssp.tmf;

	memset(task_iu, 0, sizeof(struct ssp_task_iu));

	memcpy(task_iu->LUN, task->ssp_task.LUN, 8);

	task_iu->task_func = isci_tmf->tmf_code;
	task_iu->task_tag =
		(ireq->ttype == tmf_task) ?
		isci_tmf->io_tag :
		SCI_CONTROLLER_INVALID_IO_TAG;
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
	task_context->connection_rate = target_device->connection_rate;
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
						&sds_request->ssp.cmd);

	task_context->command_iu_upper = upper_32_bits(dma_addr);
	task_context->command_iu_lower = lower_32_bits(dma_addr);

	/*
	 * Copy the physical address for the response buffer to the
	 * SCU Task Context
	 */
	dma_addr = scic_io_request_get_dma_addr(sds_request,
						&sds_request->ssp.rsp);

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

	task_context->ssp_command_iu_length =
		sizeof(struct ssp_cmd_iu) / sizeof(u32);
	task_context->type.ssp.frame_type = SSP_COMMAND;

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
	task_context->type.ssp.frame_type          = SSP_TASK;
	task_context->ssp_command_iu_length =
		sizeof(struct ssp_task_iu) / sizeof(u32);
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
	struct scic_sds_request *sci_req,
	struct scu_task_context *task_context)
{
	dma_addr_t dma_addr;
	struct scic_sds_controller *controller;
	struct scic_sds_remote_device *target_device;
	struct scic_sds_port *target_port;

	controller = scic_sds_request_get_controller(sci_req);
	target_device = scic_sds_request_get_device(sci_req);
	target_port = scic_sds_request_get_port(sci_req);

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
		scic_sds_remote_device_get_index(sci_req->target_device);
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
	task_context->type.words[0] = *(u32 *)&sci_req->stp.cmd;

	if (sci_req->was_tag_assigned_by_user) {
		/*
		 * Build the task context now since we have already read
		 * the data
		 */
		sci_req->post_context =
			(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC |
			 (scic_sds_controller_get_protocol_engine_group(
							controller) <<
			  SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) |
			 (scic_sds_port_get_index(target_port) <<
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
	dma_addr = scic_io_request_get_dma_addr(sci_req,
						((char *) &sci_req->stp.cmd) +
						sizeof(u32));

	task_context->command_iu_upper = upper_32_bits(dma_addr);
	task_context->command_iu_lower = lower_32_bits(dma_addr);

	/* SATA Requests do not have a response buffer */
	task_context->response_iu_upper = 0;
	task_context->response_iu_lower = 0;
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
static void scu_stp_raw_request_construct_task_context(struct scic_sds_stp_request *stp_req,
						       struct scu_task_context *task_context)
{
	struct scic_sds_request *sci_req = to_sci_req(stp_req);

	scu_sata_reqeust_construct_task_context(sci_req, task_context);

	task_context->control_frame         = 0;
	task_context->priority              = SCU_TASK_PRIORITY_NORMAL;
	task_context->task_type             = SCU_TASK_TYPE_SATA_RAW_FRAME;
	task_context->type.stp.fis_type     = FIS_REGH2D;
	task_context->transfer_length_bytes = sizeof(struct host_to_dev_fis) - sizeof(u32);
}

static enum sci_status
scic_sds_stp_pio_request_construct(struct scic_sds_request *sci_req,
				   bool copy_rx_frame)
{
	struct scic_sds_stp_request *stp_req = &sci_req->stp.req;
	struct scic_sds_stp_pio_request *pio = &stp_req->type.pio;

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

	return SCI_SUCCESS;
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



static enum sci_status
scic_io_request_construct_sata(struct scic_sds_request *sci_req,
			       u32 len,
			       enum dma_data_direction dir,
			       bool copy)
{
	enum sci_status status = SCI_SUCCESS;
	struct isci_request *ireq = sci_req_to_ireq(sci_req);
	struct sas_task *task = isci_request_access_task(ireq);

	/* check for management protocols */
	if (ireq->ttype == tmf_task) {
		struct isci_tmf *tmf = isci_request_access_tmf(ireq);

		if (tmf->tmf_code == isci_tmf_sata_srst_high ||
		    tmf->tmf_code == isci_tmf_sata_srst_low) {
			scu_stp_raw_request_construct_task_context(&sci_req->stp.req,
								   sci_req->task_context_buffer);
			return SCI_SUCCESS;
		} else {
			dev_err(scic_to_dev(sci_req->owning_controller),
				"%s: Request 0x%p received un-handled SAT "
				"management protocol 0x%x.\n",
				__func__, sci_req, tmf->tmf_code);

			return SCI_FAILURE;
		}
	}

	if (!sas_protocol_ata(task->task_proto)) {
		dev_err(scic_to_dev(sci_req->owning_controller),
			"%s: Non-ATA protocol in SATA path: 0x%x\n",
			__func__,
			task->task_proto);
		return SCI_FAILURE;

	}

	/* non data */
	if (task->data_dir == DMA_NONE) {
		scu_stp_raw_request_construct_task_context(&sci_req->stp.req,
							   sci_req->task_context_buffer);
		return SCI_SUCCESS;
	}

	/* NCQ */
	if (task->ata_task.use_ncq) {
		scic_sds_stp_optimized_request_construct(sci_req,
							 SCU_TASK_TYPE_FPDMAQ_READ,
							 len, dir);
		return SCI_SUCCESS;
	}

	/* DMA */
	if (task->ata_task.dma_xfer) {
		scic_sds_stp_optimized_request_construct(sci_req,
							 SCU_TASK_TYPE_DMA_IN,
							 len, dir);
		return SCI_SUCCESS;
	} else /* PIO */
		return scic_sds_stp_pio_request_construct(sci_req, copy);

	return status;
}

static enum sci_status scic_io_request_construct_basic_ssp(struct scic_sds_request *sci_req)
{
	struct isci_request *ireq = sci_req_to_ireq(sci_req);
	struct sas_task *task = isci_request_access_task(ireq);

	sci_req->protocol = SCIC_SSP_PROTOCOL;

	scu_ssp_io_request_construct_task_context(sci_req,
						  task->data_dir,
						  task->total_xfer_len);

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

static enum sci_status scic_io_request_construct_basic_sata(struct scic_sds_request *sci_req)
{
	enum sci_status status;
	struct scic_sds_stp_request *stp_req;
	bool copy = false;
	struct isci_request *isci_request = sci_req_to_ireq(sci_req);
	struct sas_task *task = isci_request_access_task(isci_request);

	stp_req = &sci_req->stp.req;
	sci_req->protocol = SCIC_STP_PROTOCOL;

	copy = (task->data_dir == DMA_NONE) ? false : true;

	status = scic_io_request_construct_sata(sci_req,
						task->total_xfer_len,
						task->data_dir,
						copy);

	if (status == SCI_SUCCESS)
		sci_base_state_machine_change_state(&sci_req->state_machine,
						    SCI_BASE_REQUEST_STATE_CONSTRUCTED);

	return status;
}

enum sci_status scic_task_request_construct_sata(struct scic_sds_request *sci_req)
{
	enum sci_status status = SCI_SUCCESS;
	struct isci_request *ireq = sci_req_to_ireq(sci_req);

	/* check for management protocols */
	if (ireq->ttype == tmf_task) {
		struct isci_tmf *tmf = isci_request_access_tmf(ireq);

		if (tmf->tmf_code == isci_tmf_sata_srst_high ||
		    tmf->tmf_code == isci_tmf_sata_srst_low) {
			scu_stp_raw_request_construct_task_context(&sci_req->stp.req,
								   sci_req->task_context_buffer);
		} else {
			dev_err(scic_to_dev(sci_req->owning_controller),
				"%s: Request 0x%p received un-handled SAT "
				"Protocol 0x%x.\n",
				__func__, sci_req, tmf->tmf_code);

			return SCI_FAILURE;
		}
	}

	if (status != SCI_SUCCESS)
		return status;
	sci_base_state_machine_change_state(&sci_req->state_machine,
					    SCI_BASE_REQUEST_STATE_CONSTRUCTED);

	return status;
}

/**
 * sci_req_tx_bytes - bytes transferred when reply underruns request
 * @sci_req: request that was terminated early
 */
#define SCU_TASK_CONTEXT_SRAM 0x200000
static u32 sci_req_tx_bytes(struct scic_sds_request *sci_req)
{
	struct scic_sds_controller *scic = sci_req->owning_controller;
	u32 ret_val = 0;

	if (readl(&scic->smu_registers->address_modifier) == 0) {
		void __iomem *scu_reg_base = scic->scu_registers;

		/* get the bytes of data from the Address == BAR1 + 20002Ch + (256*TCi) where
		 *   BAR1 is the scu_registers
		 *   0x20002C = 0x200000 + 0x2c
		 *            = start of task context SRAM + offset of (type.ssp.data_offset)
		 *   TCi is the io_tag of struct scic_sds_request
		 */
		ret_val = readl(scu_reg_base +
				(SCU_TASK_CONTEXT_SRAM + offsetof(struct scu_task_context, type.ssp.data_offset)) +
				((sizeof(struct scu_task_context)) * scic_sds_io_tag_get_index(sci_req->io_tag)));
	}

	return ret_val;
}

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

/*
 * This function copies response data for requests returning response data
 *    instead of sense data.
 * @sci_req: This parameter specifies the request object for which to copy
 *    the response data.
 */
static void scic_sds_io_request_copy_response(struct scic_sds_request *sci_req)
{
	void *resp_buf;
	u32 len;
	struct ssp_response_iu *ssp_response;
	struct isci_request *ireq = sci_req_to_ireq(sci_req);
	struct isci_tmf *isci_tmf = isci_request_access_tmf(ireq);

	ssp_response = &sci_req->ssp.rsp;

	resp_buf = &isci_tmf->resp.resp_iu;

	len = min_t(u32,
		    SSP_RESP_IU_MAX_SIZE,
		    be32_to_cpu(ssp_response->response_data_len));

	memcpy(resp_buf, ssp_response->resp_data, len);
}

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

static enum sci_status scic_sds_request_started_state_abort_handler(struct scic_sds_request *sci_req)
{
	sci_base_state_machine_change_state(&sci_req->state_machine,
					    SCI_BASE_REQUEST_STATE_ABORTING);
	return SCI_SUCCESS;
}

/*
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
static enum sci_status
scic_sds_request_started_state_tc_completion_handler(struct scic_sds_request *sci_req,
						     u32 completion_code)
{
	u8 datapres;
	struct ssp_response_iu *resp_iu;

	/*
	 * TODO: Any SDMA return code of other than 0 is bad
	 *       decode 0x003C0000 to determine SDMA status
	 */
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_request_set_status(sci_req,
					    SCU_TASK_DONE_GOOD,
					    SCI_SUCCESS);
		break;

	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_EARLY_RESP):
	{
		/*
		 * There are times when the SCU hardware will return an early
		 * response because the io request specified more data than is
		 * returned by the target device (mode pages, inquiry data,
		 * etc.).  We must check the response stats to see if this is
		 * truly a failed request or a good request that just got
		 * completed early.
		 */
		struct ssp_response_iu *resp = &sci_req->ssp.rsp;
		ssize_t word_cnt = SSP_RESP_IU_MAX_SIZE / sizeof(u32);

		sci_swab32_cpy(&sci_req->ssp.rsp,
			       &sci_req->ssp.rsp,
			       word_cnt);

		if (resp->status == 0) {
			scic_sds_request_set_status(
				sci_req,
				SCU_TASK_DONE_GOOD,
				SCI_SUCCESS_IO_DONE_EARLY);
		} else {
			scic_sds_request_set_status(
				sci_req,
				SCU_TASK_DONE_CHECK_RESPONSE,
				SCI_FAILURE_IO_RESPONSE_VALID);
		}
	}
	break;

	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_CHECK_RESPONSE):
	{
		ssize_t word_cnt = SSP_RESP_IU_MAX_SIZE / sizeof(u32);

		sci_swab32_cpy(&sci_req->ssp.rsp,
			       &sci_req->ssp.rsp,
			       word_cnt);

		scic_sds_request_set_status(sci_req,
					    SCU_TASK_DONE_CHECK_RESPONSE,
					    SCI_FAILURE_IO_RESPONSE_VALID);
		break;
	}

	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_RESP_LEN_ERR):
		/*
		 * / @todo With TASK_DONE_RESP_LEN_ERR is the response frame
		 * guaranteed to be received before this completion status is
		 * posted?
		 */
		resp_iu = &sci_req->ssp.rsp;
		datapres = resp_iu->datapres;

		if ((datapres == 0x01) || (datapres == 0x02)) {
			scic_sds_request_set_status(
				sci_req,
				SCU_TASK_DONE_CHECK_RESPONSE,
				SCI_FAILURE_IO_RESPONSE_VALID);
		} else
			scic_sds_request_set_status(
				sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS);
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
				SCU_GET_COMPLETION_TL_STATUS(completion_code) >>
				SCU_COMPLETION_TL_STATUS_SHIFT,
				SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED);
		} else {
			scic_sds_request_set_status(
				sci_req,
				SCU_GET_COMPLETION_TL_STATUS(completion_code) >>
				SCU_COMPLETION_TL_STATUS_SHIFT,
				SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR);
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
			SCU_GET_COMPLETION_TL_STATUS(completion_code) >>
			SCU_COMPLETION_TL_STATUS_SHIFT,
			SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED);
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
			SCU_GET_COMPLETION_TL_STATUS(completion_code) >>
			SCU_COMPLETION_TL_STATUS_SHIFT,
			SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR);
		break;
	}

	/*
	 * TODO: This is probably wrong for ACK/NAK timeout conditions
	 */

	/* In all cases we will treat this as the completion of the IO req. */
	sci_base_state_machine_change_state(&sci_req->state_machine,
					    SCI_BASE_REQUEST_STATE_COMPLETED);
	return SCI_SUCCESS;
}

enum sci_status
scic_sds_io_request_tc_completion(struct scic_sds_request *request, u32 completion_code)
{
	if (request->state_handlers->tc_completion_handler)
		return request->state_handlers->tc_completion_handler(request, completion_code);

	dev_warn(scic_to_dev(request->owning_controller),
		"%s: SCIC IO Request given task completion notification %x "
		"while in wrong state %d\n",
		__func__,
		completion_code,
		sci_base_state_machine_get_state(&request->state_machine));

	return SCI_FAILURE_INVALID_STATE;
}

/*
 * This method implements the action to be taken when an SCIC_SDS_IO_REQUEST_T
 * object receives a scic_sds_request_frame_handler() request. This method
 * first determines the frame type received.  If this is a response frame then
 * the response data is copied to the io request response buffer for processing
 * at completion time. If the frame type is not a response buffer an error is
 * logged. enum sci_status SCI_SUCCESS SCI_FAILURE_INVALID_PARAMETER_VALUE
 */
static enum sci_status
scic_sds_request_started_state_frame_handler(struct scic_sds_request *sci_req,
					     u32 frame_index)
{
	enum sci_status status;
	u32 *frame_header;
	struct ssp_frame_hdr ssp_hdr;
	ssize_t word_cnt;

	status = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_request_get_controller(sci_req)->uf_control),
		frame_index,
		(void **)&frame_header);

	word_cnt = sizeof(struct ssp_frame_hdr) / sizeof(u32);
	sci_swab32_cpy(&ssp_hdr, frame_header, word_cnt);

	if (ssp_hdr.frame_type == SSP_RESPONSE) {
		struct ssp_response_iu *resp_iu;
		ssize_t word_cnt = SSP_RESP_IU_MAX_SIZE / sizeof(u32);

		status = scic_sds_unsolicited_frame_control_get_buffer(
			&(scic_sds_request_get_controller(sci_req)->uf_control),
			frame_index,
			(void **)&resp_iu);

		sci_swab32_cpy(&sci_req->ssp.rsp,
			       resp_iu, word_cnt);

		resp_iu = &sci_req->ssp.rsp;

		if ((resp_iu->datapres == 0x01) ||
		    (resp_iu->datapres == 0x02)) {
			scic_sds_request_set_status(
				sci_req,
				SCU_TASK_DONE_CHECK_RESPONSE,
				SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR);
		} else
			scic_sds_request_set_status(
				sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS);
	} else {
		/* This was not a response frame why did it get forwarded? */
		dev_err(scic_to_dev(sci_req->owning_controller),
			"%s: SCIC IO Request 0x%p received unexpected "
			"frame %d type 0x%02x\n",
			__func__,
			sci_req,
			frame_index,
			ssp_hdr.frame_type);
	}

	/*
	 * In any case we are done with this frame buffer return it to the
	 * controller
	 */
	scic_sds_controller_release_frame(
		sci_req->owning_controller, frame_index);

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

/**
 * This method processes the completions transport layer (TL) status to
 *    determine if the RAW task management frame was sent successfully. If the
 *    raw frame was sent successfully, then the state for the task request
 *    transitions to waiting for a response frame.
 * @sci_req: This parameter specifies the request for which the TC
 *    completion was received.
 * @completion_code: This parameter indicates the completion status information
 *    for the TC.
 *
 * Indicate if the tc completion handler was successful. SCI_SUCCESS currently
 * this method always returns success.
 */
static enum sci_status scic_sds_ssp_task_request_await_tc_completion_tc_completion_handler(
	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_request_set_status(sci_req, SCU_TASK_DONE_GOOD,
					    SCI_SUCCESS);

		sci_base_state_machine_change_state(&sci_req->state_machine,
						    SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE);
		break;

	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_ACK_NAK_TO):
		/*
		 * Currently, the decision is to simply allow the task request to
		 * timeout if the task IU wasn't received successfully.
		 * There is a potential for receiving multiple task responses if we
		 * decide to send the task IU again. */
		dev_warn(scic_to_dev(sci_req->owning_controller),
			 "%s: TaskRequest:0x%p CompletionCode:%x - "
			 "ACK/NAK timeout\n",
			 __func__,
			 sci_req,
			 completion_code);

		sci_base_state_machine_change_state(&sci_req->state_machine,
						    SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE);
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
 * This method is responsible for processing a terminate/abort request for this
 *    TC while the request is waiting for the task management response
 *    unsolicited frame.
 * @sci_req: This parameter specifies the request for which the
 *    termination was requested.
 *
 * This method returns an indication as to whether the abort request was
 * successfully handled. need to update to ensure the received UF doesn't cause
 * damage to subsequent requests (i.e. put the extended tag in a holding
 * pattern for this particular device).
 */
static enum sci_status scic_sds_ssp_task_request_await_tc_response_abort_handler(
	struct scic_sds_request *request)
{
	sci_base_state_machine_change_state(&request->state_machine,
					    SCI_BASE_REQUEST_STATE_ABORTING);
	sci_base_state_machine_change_state(&request->state_machine,
					    SCI_BASE_REQUEST_STATE_COMPLETED);
	return SCI_SUCCESS;
}

/**
 * This method processes an unsolicited frame while the task mgmt request is
 *    waiting for a response frame.  It will copy the response data, release
 *    the unsolicited frame, and transition the request to the
 *    SCI_BASE_REQUEST_STATE_COMPLETED state.
 * @sci_req: This parameter specifies the request for which the
 *    unsolicited frame was received.
 * @frame_index: This parameter indicates the unsolicited frame index that
 *    should contain the response.
 *
 * This method returns an indication of whether the TC response frame was
 * handled successfully or not. SCI_SUCCESS Currently this value is always
 * returned and indicates successful processing of the TC response. Should
 * probably update to check frame type and make sure it is a response frame.
 */
static enum sci_status scic_sds_ssp_task_request_await_tc_response_frame_handler(
	struct scic_sds_request *request,
	u32 frame_index)
{
	scic_sds_io_request_copy_response(request);

	sci_base_state_machine_change_state(&request->state_machine,
					    SCI_BASE_REQUEST_STATE_COMPLETED);
	scic_sds_controller_release_frame(request->owning_controller,
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
		scic_sds_request_set_status(sci_req, SCU_TASK_DONE_GOOD,
					    SCI_SUCCESS);

		sci_base_state_machine_change_state(&sci_req->state_machine,
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
		scic_sds_request_set_status(sci_req, SCU_TASK_DONE_SMP_RESP_TO_ERR,
					    SCI_FAILURE_RETRY_REQUIRED);

		sci_base_state_machine_change_state(&sci_req->state_machine,
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

		sci_base_state_machine_change_state(&sci_req->state_machine,
						    SCI_BASE_REQUEST_STATE_COMPLETED);
		break;
	}

	return SCI_SUCCESS;
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
scic_sds_smp_request_await_response_frame_handler(struct scic_sds_request *sci_req,
						  u32 frame_index)
{
	enum sci_status status;
	void *frame_header;
	struct smp_resp *rsp_hdr = &sci_req->smp.rsp;
	ssize_t word_cnt = SMP_RESP_HDR_SZ / sizeof(u32);

	status = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_request_get_controller(sci_req)->uf_control),
		frame_index,
		&frame_header);

	/* byte swap the header. */
	sci_swab32_cpy(rsp_hdr, frame_header, word_cnt);

	if (rsp_hdr->frame_type == SMP_RESPONSE) {
		void *smp_resp;

		status = scic_sds_unsolicited_frame_control_get_buffer(
			&(scic_sds_request_get_controller(sci_req)->uf_control),
			frame_index,
			&smp_resp);

		word_cnt = (sizeof(struct smp_req) - SMP_RESP_HDR_SZ) /
			sizeof(u32);

		sci_swab32_cpy(((u8 *) rsp_hdr) + SMP_RESP_HDR_SZ,
			       smp_resp, word_cnt);

		scic_sds_request_set_status(
			sci_req, SCU_TASK_DONE_GOOD, SCI_SUCCESS);

		sci_base_state_machine_change_state(&sci_req->state_machine,
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

		sci_base_state_machine_change_state(&sci_req->state_machine,
						    SCI_BASE_REQUEST_STATE_COMPLETED);
	}

	scic_sds_controller_release_frame(sci_req->owning_controller,
					  frame_index);

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
		scic_sds_request_set_status(sci_req, SCU_TASK_DONE_GOOD,
					    SCI_SUCCESS);

		sci_base_state_machine_change_state(&sci_req->state_machine,
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

void scic_stp_io_request_set_ncq_tag(struct scic_sds_request *req,
				     u16 ncq_tag)
{
	/**
	 * @note This could be made to return an error to the user if the user
	 *       attempts to set the NCQ tag in the wrong state.
	 */
	req->task_context_buffer->type.stp.ncq_tag = ncq_tag;
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
	struct scic_sds_request *sci_req = to_sci_req(stp_req);
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
			&sci_req->state_machine,
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
	struct scic_sds_stp_request *stp_req = &sci_req->stp.req;
	struct scic_sds_controller *scic = sci_req->owning_controller;

	status = scic_sds_unsolicited_frame_control_get_header(&scic->uf_control,
							       frame_index,
							       (void **)&frame_header);

	if (status != SCI_SUCCESS) {
		dev_err(scic_to_dev(sci_req->owning_controller),
			"%s: SCIC IO Request 0x%p could not get frame header "
			"for frame index %d, status %x\n",
			__func__, stp_req, frame_index, status);

		return status;
	}

	switch (frame_header->fis_type) {
	case FIS_REGD2H:
		scic_sds_unsolicited_frame_control_get_buffer(&scic->uf_control,
							      frame_index,
							      (void **)&frame_buffer);

		scic_sds_controller_copy_sata_response(&sci_req->stp.rsp,
						       frame_header,
						       frame_buffer);

		/* The command has completed with error */
		scic_sds_request_set_status(sci_req, SCU_TASK_DONE_CHECK_RESPONSE,
					    SCI_FAILURE_IO_RESPONSE_VALID);
		break;

	default:
		dev_warn(scic_to_dev(scic),
			 "%s: IO Request:0x%p Frame Id:%d protocol "
			  "violation occurred\n", __func__, stp_req,
			  frame_index);

		scic_sds_request_set_status(sci_req, SCU_TASK_DONE_UNEXP_FIS,
					    SCI_FAILURE_PROTOCOL_VIOLATION);
		break;
	}

	sci_base_state_machine_change_state(&sci_req->state_machine,
					    SCI_BASE_REQUEST_STATE_COMPLETED);

	/* Frame has been decoded return it to the controller */
	scic_sds_controller_release_frame(scic, frame_index);

	return status;
}

#define SCU_MAX_FRAME_BUFFER_SIZE  0x400  /* 1K is the maximum SCU frame data payload */

/* transmit DATA_FIS from (current sgl + offset) for input
 * parameter length. current sgl and offset is alreay stored in the IO request
 */
static enum sci_status scic_sds_stp_request_pio_data_out_trasmit_data_frame(
	struct scic_sds_request *sci_req,
	u32 length)
{
	struct scic_sds_controller *scic = sci_req->owning_controller;
	struct scic_sds_stp_request *stp_req = &sci_req->stp.req;
	struct scu_task_context *task_context;
	struct scu_sgl_element *current_sgl;

	/* Recycle the TC and reconstruct it for sending out DATA FIS containing
	 * for the data from current_sgl+offset for the input length
	 */
	task_context = scic_sds_controller_get_task_context_buffer(scic,
								   sci_req->io_tag);

	if (stp_req->type.pio.request_current.sgl_set == SCU_SGL_ELEMENT_PAIR_A)
		current_sgl = &stp_req->type.pio.request_current.sgl_pair->A;
	else
		current_sgl = &stp_req->type.pio.request_current.sgl_pair->B;

	/* update the TC */
	task_context->command_iu_upper = current_sgl->address_upper;
	task_context->command_iu_lower = current_sgl->address_lower;
	task_context->transfer_length_bytes = length;
	task_context->type.stp.fis_type = FIS_DATA;

	/* send the new TC out. */
	return scic_controller_continue_io(sci_req);
}

static enum sci_status scic_sds_stp_request_pio_data_out_transmit_data(struct scic_sds_request *sci_req)
{

	struct scu_sgl_element *current_sgl;
	u32 sgl_offset;
	u32 remaining_bytes_in_current_sgl = 0;
	enum sci_status status = SCI_SUCCESS;
	struct scic_sds_stp_request *stp_req = &sci_req->stp.req;

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

	sci_req = to_sci_req(stp_req);
	ireq = sci_req_to_ireq(sci_req);
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
			&sci_req->state_machine,
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

static enum sci_status scic_sds_stp_request_pio_await_frame_frame_handler(struct scic_sds_request *sci_req,
									  u32 frame_index)
{
	struct scic_sds_controller *scic = sci_req->owning_controller;
	struct scic_sds_stp_request *stp_req = &sci_req->stp.req;
	struct isci_request *ireq = sci_req_to_ireq(sci_req);
	struct sas_task *task = isci_request_access_task(ireq);
	struct dev_to_host_fis *frame_header;
	enum sci_status status;
	u32 *frame_buffer;

	status = scic_sds_unsolicited_frame_control_get_header(&scic->uf_control,
							       frame_index,
							       (void **)&frame_header);

	if (status != SCI_SUCCESS) {
		dev_err(scic_to_dev(scic),
			"%s: SCIC IO Request 0x%p could not get frame header "
			"for frame index %d, status %x\n",
			__func__, stp_req, frame_index, status);
		return status;
	}

	switch (frame_header->fis_type) {
	case FIS_PIO_SETUP:
		/* Get from the frame buffer the PIO Setup Data */
		scic_sds_unsolicited_frame_control_get_buffer(&scic->uf_control,
							      frame_index,
							      (void **)&frame_buffer);

		/* Get the data from the PIO Setup The SCU Hardware returns
		 * first word in the frame_header and the rest of the data is in
		 * the frame buffer so we need to back up one dword
		 */

		/* transfer_count: first 16bits in the 4th dword */
		stp_req->type.pio.pio_transfer_bytes = frame_buffer[3] & 0xffff;

		/* ending_status: 4th byte in the 3rd dword */
		stp_req->type.pio.ending_status = (frame_buffer[2] >> 24) & 0xff;

		scic_sds_controller_copy_sata_response(&sci_req->stp.rsp,
						       frame_header,
						       frame_buffer);

		sci_req->stp.rsp.status = stp_req->type.pio.ending_status;

		/* The next state is dependent on whether the
		 * request was PIO Data-in or Data out
		 */
		if (task->data_dir == DMA_FROM_DEVICE) {
			sci_base_state_machine_change_state(&sci_req->state_machine,
							    SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE);
		} else if (task->data_dir == DMA_TO_DEVICE) {
			/* Transmit data */
			status = scic_sds_stp_request_pio_data_out_transmit_data(sci_req);
			if (status != SCI_SUCCESS)
				break;
			sci_base_state_machine_change_state(&sci_req->state_machine,
							    SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE);
		}
		break;
	case FIS_SETDEVBITS:
		sci_base_state_machine_change_state(&sci_req->state_machine,
						    SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE);
		break;
	case FIS_REGD2H:
		if (frame_header->status & ATA_BUSY) {
			/* Now why is the drive sending a D2H Register FIS when
			 * it is still busy?  Do nothing since we are still in
			 * the right state.
			 */
			dev_dbg(scic_to_dev(scic),
				"%s: SCIC PIO Request 0x%p received "
				"D2H Register FIS with BSY status "
				"0x%x\n", __func__, stp_req,
				frame_header->status);
			break;
		}

		scic_sds_unsolicited_frame_control_get_buffer(&scic->uf_control,
							      frame_index,
							      (void **)&frame_buffer);

		scic_sds_controller_copy_sata_response(&sci_req->stp.req,
						       frame_header,
						       frame_buffer);

		scic_sds_request_set_status(sci_req,
					    SCU_TASK_DONE_CHECK_RESPONSE,
					    SCI_FAILURE_IO_RESPONSE_VALID);

		sci_base_state_machine_change_state(&sci_req->state_machine,
						    SCI_BASE_REQUEST_STATE_COMPLETED);
		break;
	default:
		/* FIXME: what do we do here? */
		break;
	}

	/* Frame is decoded return it to the controller */
	scic_sds_controller_release_frame(scic, frame_index);

	return status;
}

static enum sci_status scic_sds_stp_request_pio_data_in_await_data_frame_handler(struct scic_sds_request *sci_req,
										 u32 frame_index)
{
	enum sci_status status;
	struct dev_to_host_fis *frame_header;
	struct sata_fis_data *frame_buffer;
	struct scic_sds_stp_request *stp_req = &sci_req->stp.req;
	struct scic_sds_controller *scic = sci_req->owning_controller;

	status = scic_sds_unsolicited_frame_control_get_header(&scic->uf_control,
							       frame_index,
							       (void **)&frame_header);

	if (status != SCI_SUCCESS) {
		dev_err(scic_to_dev(scic),
			"%s: SCIC IO Request 0x%p could not get frame header "
			"for frame index %d, status %x\n",
			__func__, stp_req, frame_index, status);
		return status;
	}

	if (frame_header->fis_type == FIS_DATA) {
		if (stp_req->type.pio.request_current.sgl_pair == NULL) {
			sci_req->saved_rx_frame_index = frame_index;
			stp_req->type.pio.pio_transfer_bytes = 0;
		} else {
			scic_sds_unsolicited_frame_control_get_buffer(&scic->uf_control,
								      frame_index,
								      (void **)&frame_buffer);

			status = scic_sds_stp_request_pio_data_in_copy_data(stp_req,
									    (u8 *)frame_buffer);

			/* Frame is decoded return it to the controller */
			scic_sds_controller_release_frame(scic, frame_index);
		}

		/* Check for the end of the transfer, are there more
		 * bytes remaining for this data transfer
		 */
		if (status != SCI_SUCCESS ||
		    stp_req->type.pio.pio_transfer_bytes != 0)
			return status;

		if ((stp_req->type.pio.ending_status & ATA_BUSY) == 0) {
			scic_sds_request_set_status(sci_req,
						    SCU_TASK_DONE_CHECK_RESPONSE,
						    SCI_FAILURE_IO_RESPONSE_VALID);

			sci_base_state_machine_change_state(&sci_req->state_machine,
							    SCI_BASE_REQUEST_STATE_COMPLETED);
		} else {
			sci_base_state_machine_change_state(&sci_req->state_machine,
							    SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE);
		}
	} else {
		dev_err(scic_to_dev(scic),
			"%s: SCIC PIO Request 0x%p received frame %d "
			"with fis type 0x%02x when expecting a data "
			"fis.\n", __func__, stp_req, frame_index,
			frame_header->fis_type);

		scic_sds_request_set_status(sci_req,
					    SCU_TASK_DONE_GOOD,
					    SCI_FAILURE_IO_REQUIRES_SCSI_ABORT);

		sci_base_state_machine_change_state(&sci_req->state_machine,
						    SCI_BASE_REQUEST_STATE_COMPLETED);

		/* Frame is decoded return it to the controller */
		scic_sds_controller_release_frame(scic, frame_index);
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
static enum sci_status scic_sds_stp_request_pio_data_out_await_data_transmit_completion_tc_completion_handler(

	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	enum sci_status status = SCI_SUCCESS;
	bool all_frames_transferred = false;
	struct scic_sds_stp_request *stp_req = &sci_req->stp.req;

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
				&sci_req->state_machine,
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
			&request->state_machine,
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

static void scic_sds_stp_request_udma_complete_request(
	struct scic_sds_request *request,
	u32 scu_status,
	enum sci_status sci_status)
{
	scic_sds_request_set_status(request, scu_status, sci_status);
	sci_base_state_machine_change_state(&request->state_machine,
		SCI_BASE_REQUEST_STATE_COMPLETED);
}

static enum sci_status scic_sds_stp_request_udma_general_frame_handler(struct scic_sds_request *sci_req,
								       u32 frame_index)
{
	struct scic_sds_controller *scic = sci_req->owning_controller;
	struct dev_to_host_fis *frame_header;
	enum sci_status status;
	u32 *frame_buffer;

	status = scic_sds_unsolicited_frame_control_get_header(&scic->uf_control,
							       frame_index,
							       (void **)&frame_header);

	if ((status == SCI_SUCCESS) &&
	    (frame_header->fis_type == FIS_REGD2H)) {
		scic_sds_unsolicited_frame_control_get_buffer(&scic->uf_control,
							      frame_index,
							      (void **)&frame_buffer);

		scic_sds_controller_copy_sata_response(&sci_req->stp.rsp,
						       frame_header,
						       frame_buffer);
	}

	scic_sds_controller_release_frame(scic, frame_index);

	return status;
}

static enum sci_status scic_sds_stp_request_udma_await_tc_completion_tc_completion_handler(
	struct scic_sds_request *sci_req,
	u32 completion_code)
{
	enum sci_status status = SCI_SUCCESS;

	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		scic_sds_stp_request_udma_complete_request(sci_req,
							   SCU_TASK_DONE_GOOD,
							   SCI_SUCCESS);
		break;
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_FIS):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_REG_ERR):
		/*
		 * We must check ther response buffer to see if the D2H Register FIS was
		 * received before we got the TC completion. */
		if (sci_req->stp.rsp.fis_type == FIS_REGD2H) {
			scic_sds_remote_device_suspend(sci_req->target_device,
				SCU_EVENT_SPECIFIC(SCU_NORMALIZE_COMPLETION_STATUS(completion_code)));

			scic_sds_stp_request_udma_complete_request(sci_req,
								   SCU_TASK_DONE_CHECK_RESPONSE,
								   SCI_FAILURE_IO_RESPONSE_VALID);
		} else {
			/*
			 * If we have an error completion status for the TC then we can expect a
			 * D2H register FIS from the device so we must change state to wait for it */
			sci_base_state_machine_change_state(&sci_req->state_machine,
				SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE);
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
		scic_sds_remote_device_suspend(sci_req->target_device,
			SCU_EVENT_SPECIFIC(SCU_NORMALIZE_COMPLETION_STATUS(completion_code)));
	/* Fall through to the default case */
	default:
		/* All other completion status cause the IO to be complete. */
		scic_sds_stp_request_udma_complete_request(sci_req,
					SCU_NORMALIZE_COMPLETION_STATUS(completion_code),
					SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR);
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

	if (status != SCI_SUCCESS)
		return status;

	scic_sds_stp_request_udma_complete_request(sci_req,
						   SCU_TASK_DONE_CHECK_RESPONSE,
						   SCI_FAILURE_IO_RESPONSE_VALID);

	return status;
}

enum sci_status scic_sds_stp_udma_request_construct(struct scic_sds_request *sci_req,
						    u32 len,
						    enum dma_data_direction dir)
{
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
			&sci_req->state_machine,
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
		scic_sds_request_set_status(sci_req, SCU_TASK_DONE_GOOD,
					    SCI_SUCCESS);

		sci_base_state_machine_change_state(&sci_req->state_machine,
			SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE);
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
	struct scic_sds_request *sci_req,
	u32 frame_index)
{
	enum sci_status status;
	struct dev_to_host_fis *frame_header;
	u32 *frame_buffer;
	struct scic_sds_stp_request *stp_req = &sci_req->stp.req;
	struct scic_sds_controller *scic = sci_req->owning_controller;

	status = scic_sds_unsolicited_frame_control_get_header(&scic->uf_control,
							       frame_index,
							       (void **)&frame_header);
	if (status != SCI_SUCCESS) {
		dev_err(scic_to_dev(scic),
			"%s: SCIC IO Request 0x%p could not get frame header "
			"for frame index %d, status %x\n",
			__func__, stp_req, frame_index, status);
		return status;
	}

	switch (frame_header->fis_type) {
	case FIS_REGD2H:
		scic_sds_unsolicited_frame_control_get_buffer(&scic->uf_control,
							      frame_index,
							      (void **)&frame_buffer);

		scic_sds_controller_copy_sata_response(&sci_req->stp.rsp,
						       frame_header,
						       frame_buffer);

		/* The command has completed with error */
		scic_sds_request_set_status(sci_req,
					    SCU_TASK_DONE_CHECK_RESPONSE,
					    SCI_FAILURE_IO_RESPONSE_VALID);
		break;

	default:
		dev_warn(scic_to_dev(scic),
			 "%s: IO Request:0x%p Frame Id:%d protocol "
			 "violation occurred\n", __func__, stp_req,
			 frame_index);

		scic_sds_request_set_status(sci_req, SCU_TASK_DONE_UNEXP_FIS,
					    SCI_FAILURE_PROTOCOL_VIOLATION);
		break;
	}

	sci_base_state_machine_change_state(&sci_req->state_machine,
					    SCI_BASE_REQUEST_STATE_COMPLETED);

	/* Frame has been decoded return it to the controller */
	scic_sds_controller_release_frame(scic, frame_index);

	return status;
}

static const struct scic_sds_io_request_state_handler scic_sds_request_state_handler_table[] = {
	[SCI_BASE_REQUEST_STATE_INITIAL] = { },
	[SCI_BASE_REQUEST_STATE_CONSTRUCTED] = {
		.start_handler		= scic_sds_request_constructed_state_start_handler,
		.abort_handler		= scic_sds_request_constructed_state_abort_handler,
	},
	[SCI_BASE_REQUEST_STATE_STARTED] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_request_started_state_tc_completion_handler,
		.frame_handler		= scic_sds_request_started_state_frame_handler,
	},
	[SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_ssp_task_request_await_tc_completion_tc_completion_handler,
	},
	[SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE] = {
		.abort_handler		= scic_sds_ssp_task_request_await_tc_response_abort_handler,
		.frame_handler		= scic_sds_ssp_task_request_await_tc_response_frame_handler,
	},
	[SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_smp_request_await_response_tc_completion_handler,
		.frame_handler		= scic_sds_smp_request_await_response_frame_handler,
	},
	[SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	=  scic_sds_smp_request_await_tc_completion_tc_completion_handler,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_stp_request_udma_await_tc_completion_tc_completion_handler,
		.frame_handler		= scic_sds_stp_request_udma_general_frame_handler,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.frame_handler		= scic_sds_stp_request_udma_await_d2h_reg_fis_frame_handler,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.tc_completion_handler	= scic_sds_stp_request_non_data_await_h2d_tc_completion_handler,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE] = {
		.abort_handler		= scic_sds_request_started_state_abort_handler,
		.frame_handler		= scic_sds_stp_request_non_data_await_d2h_frame_handler,
	},
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
	},
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
	[SCI_BASE_REQUEST_STATE_COMPLETED] = {
		.complete_handler	= scic_sds_request_completed_state_complete_handler,
	},
	[SCI_BASE_REQUEST_STATE_ABORTING] = {
		.abort_handler		= scic_sds_request_aborting_state_abort_handler,
		.tc_completion_handler	= scic_sds_request_aborting_state_tc_completion_handler,
		.frame_handler		= scic_sds_request_aborting_state_frame_handler,
	},
	[SCI_BASE_REQUEST_STATE_FINAL] = { },
};


/**
 * isci_request_process_response_iu() - This function sets the status and
 *    response iu, in the task struct, from the request object for the upper
 *    layer driver.
 * @sas_task: This parameter is the task struct from the upper layer driver.
 * @resp_iu: This parameter points to the response iu of the completed request.
 * @dev: This parameter specifies the linux device struct.
 *
 * none.
 */
static void isci_request_process_response_iu(
	struct sas_task *task,
	struct ssp_response_iu *resp_iu,
	struct device *dev)
{
	dev_dbg(dev,
		"%s: resp_iu = %p "
		"resp_iu->status = 0x%x,\nresp_iu->datapres = %d "
		"resp_iu->response_data_len = %x, "
		"resp_iu->sense_data_len = %x\nrepsonse data: ",
		__func__,
		resp_iu,
		resp_iu->status,
		resp_iu->datapres,
		resp_iu->response_data_len,
		resp_iu->sense_data_len);

	task->task_status.stat = resp_iu->status;

	/* libsas updates the task status fields based on the response iu. */
	sas_ssp_task_response(dev, task, resp_iu);
}

/**
 * isci_request_set_open_reject_status() - This function prepares the I/O
 *    completion for OPEN_REJECT conditions.
 * @request: This parameter is the completed isci_request object.
 * @response_ptr: This parameter specifies the service response for the I/O.
 * @status_ptr: This parameter specifies the exec status for the I/O.
 * @complete_to_host_ptr: This parameter specifies the action to be taken by
 *    the LLDD with respect to completing this request or forcing an abort
 *    condition on the I/O.
 * @open_rej_reason: This parameter specifies the encoded reason for the
 *    abandon-class reject.
 *
 * none.
 */
static void isci_request_set_open_reject_status(
	struct isci_request *request,
	struct sas_task *task,
	enum service_response *response_ptr,
	enum exec_status *status_ptr,
	enum isci_completion_selection *complete_to_host_ptr,
	enum sas_open_rej_reason open_rej_reason)
{
	/* Task in the target is done. */
	request->complete_in_target       = true;
	*response_ptr                     = SAS_TASK_UNDELIVERED;
	*status_ptr                       = SAS_OPEN_REJECT;
	*complete_to_host_ptr             = isci_perform_normal_io_completion;
	task->task_status.open_rej_reason = open_rej_reason;
}

/**
 * isci_request_handle_controller_specific_errors() - This function decodes
 *    controller-specific I/O completion error conditions.
 * @request: This parameter is the completed isci_request object.
 * @response_ptr: This parameter specifies the service response for the I/O.
 * @status_ptr: This parameter specifies the exec status for the I/O.
 * @complete_to_host_ptr: This parameter specifies the action to be taken by
 *    the LLDD with respect to completing this request or forcing an abort
 *    condition on the I/O.
 *
 * none.
 */
static void isci_request_handle_controller_specific_errors(
	struct isci_remote_device *isci_device,
	struct isci_request *request,
	struct sas_task *task,
	enum service_response *response_ptr,
	enum exec_status *status_ptr,
	enum isci_completion_selection *complete_to_host_ptr)
{
	unsigned int cstatus;

	cstatus = request->sci.scu_status;

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: %p SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR "
		"- controller status = 0x%x\n",
		__func__, request, cstatus);

	/* Decode the controller-specific errors; most
	 * important is to recognize those conditions in which
	 * the target may still have a task outstanding that
	 * must be aborted.
	 *
	 * Note that there are SCU completion codes being
	 * named in the decode below for which SCIC has already
	 * done work to handle them in a way other than as
	 * a controller-specific completion code; these are left
	 * in the decode below for completeness sake.
	 */
	switch (cstatus) {
	case SCU_TASK_DONE_DMASETUP_DIRERR:
	/* Also SCU_TASK_DONE_SMP_FRM_TYPE_ERR: */
	case SCU_TASK_DONE_XFERCNT_ERR:
		/* Also SCU_TASK_DONE_SMP_UFI_ERR: */
		if (task->task_proto == SAS_PROTOCOL_SMP) {
			/* SCU_TASK_DONE_SMP_UFI_ERR == Task Done. */
			*response_ptr = SAS_TASK_COMPLETE;

			/* See if the device has been/is being stopped. Note
			 * that we ignore the quiesce state, since we are
			 * concerned about the actual device state.
			 */
			if ((isci_device->status == isci_stopping) ||
			    (isci_device->status == isci_stopped))
				*status_ptr = SAS_DEVICE_UNKNOWN;
			else
				*status_ptr = SAS_ABORTED_TASK;

			request->complete_in_target = true;

			*complete_to_host_ptr =
				isci_perform_normal_io_completion;
		} else {
			/* Task in the target is not done. */
			*response_ptr = SAS_TASK_UNDELIVERED;

			if ((isci_device->status == isci_stopping) ||
			    (isci_device->status == isci_stopped))
				*status_ptr = SAS_DEVICE_UNKNOWN;
			else
				*status_ptr = SAM_STAT_TASK_ABORTED;

			request->complete_in_target = false;

			*complete_to_host_ptr =
				isci_perform_error_io_completion;
		}

		break;

	case SCU_TASK_DONE_CRC_ERR:
	case SCU_TASK_DONE_NAK_CMD_ERR:
	case SCU_TASK_DONE_EXCESS_DATA:
	case SCU_TASK_DONE_UNEXP_FIS:
	/* Also SCU_TASK_DONE_UNEXP_RESP: */
	case SCU_TASK_DONE_VIIT_ENTRY_NV:       /* TODO - conditions? */
	case SCU_TASK_DONE_IIT_ENTRY_NV:        /* TODO - conditions? */
	case SCU_TASK_DONE_RNCNV_OUTBOUND:      /* TODO - conditions? */
		/* These are conditions in which the target
		 * has completed the task, so that no cleanup
		 * is necessary.
		 */
		*response_ptr = SAS_TASK_COMPLETE;

		/* See if the device has been/is being stopped. Note
		 * that we ignore the quiesce state, since we are
		 * concerned about the actual device state.
		 */
		if ((isci_device->status == isci_stopping) ||
		    (isci_device->status == isci_stopped))
			*status_ptr = SAS_DEVICE_UNKNOWN;
		else
			*status_ptr = SAS_ABORTED_TASK;

		request->complete_in_target = true;

		*complete_to_host_ptr = isci_perform_normal_io_completion;
		break;


	/* Note that the only open reject completion codes seen here will be
	 * abandon-class codes; all others are automatically retried in the SCU.
	 */
	case SCU_TASK_OPEN_REJECT_WRONG_DESTINATION:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			complete_to_host_ptr, SAS_OREJ_WRONG_DEST);
		break;

	case SCU_TASK_OPEN_REJECT_ZONE_VIOLATION:

		/* Note - the return of AB0 will change when
		 * libsas implements detection of zone violations.
		 */
		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			complete_to_host_ptr, SAS_OREJ_RESV_AB0);
		break;

	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_1:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			complete_to_host_ptr, SAS_OREJ_RESV_AB1);
		break;

	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_2:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			complete_to_host_ptr, SAS_OREJ_RESV_AB2);
		break;

	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_3:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			complete_to_host_ptr, SAS_OREJ_RESV_AB3);
		break;

	case SCU_TASK_OPEN_REJECT_BAD_DESTINATION:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			complete_to_host_ptr, SAS_OREJ_BAD_DEST);
		break;

	case SCU_TASK_OPEN_REJECT_STP_RESOURCES_BUSY:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			complete_to_host_ptr, SAS_OREJ_STP_NORES);
		break;

	case SCU_TASK_OPEN_REJECT_PROTOCOL_NOT_SUPPORTED:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			complete_to_host_ptr, SAS_OREJ_EPROTO);
		break;

	case SCU_TASK_OPEN_REJECT_CONNECTION_RATE_NOT_SUPPORTED:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			complete_to_host_ptr, SAS_OREJ_CONN_RATE);
		break;

	case SCU_TASK_DONE_LL_R_ERR:
	/* Also SCU_TASK_DONE_ACK_NAK_TO: */
	case SCU_TASK_DONE_LL_PERR:
	case SCU_TASK_DONE_LL_SY_TERM:
	/* Also SCU_TASK_DONE_NAK_ERR:*/
	case SCU_TASK_DONE_LL_LF_TERM:
	/* Also SCU_TASK_DONE_DATA_LEN_ERR: */
	case SCU_TASK_DONE_LL_ABORT_ERR:
	case SCU_TASK_DONE_SEQ_INV_TYPE:
	/* Also SCU_TASK_DONE_UNEXP_XR: */
	case SCU_TASK_DONE_XR_IU_LEN_ERR:
	case SCU_TASK_DONE_INV_FIS_LEN:
	/* Also SCU_TASK_DONE_XR_WD_LEN: */
	case SCU_TASK_DONE_SDMA_ERR:
	case SCU_TASK_DONE_OFFSET_ERR:
	case SCU_TASK_DONE_MAX_PLD_ERR:
	case SCU_TASK_DONE_LF_ERR:
	case SCU_TASK_DONE_SMP_RESP_TO_ERR:  /* Escalate to dev reset? */
	case SCU_TASK_DONE_SMP_LL_RX_ERR:
	case SCU_TASK_DONE_UNEXP_DATA:
	case SCU_TASK_DONE_UNEXP_SDBFIS:
	case SCU_TASK_DONE_REG_ERR:
	case SCU_TASK_DONE_SDB_ERR:
	case SCU_TASK_DONE_TASK_ABORT:
	default:
		/* Task in the target is not done. */
		*response_ptr = SAS_TASK_UNDELIVERED;
		*status_ptr = SAM_STAT_TASK_ABORTED;
		request->complete_in_target = false;

		*complete_to_host_ptr = isci_perform_error_io_completion;
		break;
	}
}

/**
 * isci_task_save_for_upper_layer_completion() - This function saves the
 *    request for later completion to the upper layer driver.
 * @host: This parameter is a pointer to the host on which the the request
 *    should be queued (either as an error or success).
 * @request: This parameter is the completed request.
 * @response: This parameter is the response code for the completed task.
 * @status: This parameter is the status code for the completed task.
 *
 * none.
 */
static void isci_task_save_for_upper_layer_completion(
	struct isci_host *host,
	struct isci_request *request,
	enum service_response response,
	enum exec_status status,
	enum isci_completion_selection task_notification_selection)
{
	struct sas_task *task = isci_request_access_task(request);

	task_notification_selection
		= isci_task_set_completion_status(task, response, status,
						  task_notification_selection);

	/* Tasks aborted specifically by a call to the lldd_abort_task
	 * function should not be completed to the host in the regular path.
	 */
	switch (task_notification_selection) {

	case isci_perform_normal_io_completion:

		/* Normal notification (task_done) */
		dev_dbg(&host->pdev->dev,
			"%s: Normal - task = %p, response=%d (%d), status=%d (%d)\n",
			__func__,
			task,
			task->task_status.resp, response,
			task->task_status.stat, status);
		/* Add to the completed list. */
		list_add(&request->completed_node,
			 &host->requests_to_complete);

		/* Take the request off the device's pending request list. */
		list_del_init(&request->dev_node);
		break;

	case isci_perform_aborted_io_completion:
		/* No notification to libsas because this request is
		 * already in the abort path.
		 */
		dev_warn(&host->pdev->dev,
			 "%s: Aborted - task = %p, response=%d (%d), status=%d (%d)\n",
			 __func__,
			 task,
			 task->task_status.resp, response,
			 task->task_status.stat, status);

		/* Wake up whatever process was waiting for this
		 * request to complete.
		 */
		WARN_ON(request->io_request_completion == NULL);

		if (request->io_request_completion != NULL) {

			/* Signal whoever is waiting that this
			* request is complete.
			*/
			complete(request->io_request_completion);
		}
		break;

	case isci_perform_error_io_completion:
		/* Use sas_task_abort */
		dev_warn(&host->pdev->dev,
			 "%s: Error - task = %p, response=%d (%d), status=%d (%d)\n",
			 __func__,
			 task,
			 task->task_status.resp, response,
			 task->task_status.stat, status);
		/* Add to the aborted list. */
		list_add(&request->completed_node,
			 &host->requests_to_errorback);
		break;

	default:
		dev_warn(&host->pdev->dev,
			 "%s: Unknown - task = %p, response=%d (%d), status=%d (%d)\n",
			 __func__,
			 task,
			 task->task_status.resp, response,
			 task->task_status.stat, status);

		/* Add to the error to libsas list. */
		list_add(&request->completed_node,
			 &host->requests_to_errorback);
		break;
	}
}

static void isci_request_io_request_complete(struct isci_host *isci_host,
					     struct isci_request *request,
					     enum sci_io_status completion_status)
{
	struct sas_task *task = isci_request_access_task(request);
	struct ssp_response_iu *resp_iu;
	void *resp_buf;
	unsigned long task_flags;
	struct isci_remote_device *isci_device   = request->isci_device;
	enum service_response response       = SAS_TASK_UNDELIVERED;
	enum exec_status status         = SAS_ABORTED_TASK;
	enum isci_request_status request_status;
	enum isci_completion_selection complete_to_host
		= isci_perform_normal_io_completion;

	dev_dbg(&isci_host->pdev->dev,
		"%s: request = %p, task = %p,\n"
		"task->data_dir = %d completion_status = 0x%x\n",
		__func__,
		request,
		task,
		task->data_dir,
		completion_status);

	spin_lock(&request->state_lock);
	request_status = isci_request_get_state(request);

	/* Decode the request status.  Note that if the request has been
	 * aborted by a task management function, we don't care
	 * what the status is.
	 */
	switch (request_status) {

	case aborted:
		/* "aborted" indicates that the request was aborted by a task
		 * management function, since once a task management request is
		 * perfomed by the device, the request only completes because
		 * of the subsequent driver terminate.
		 *
		 * Aborted also means an external thread is explicitly managing
		 * this request, so that we do not complete it up the stack.
		 *
		 * The target is still there (since the TMF was successful).
		 */
		request->complete_in_target = true;
		response = SAS_TASK_COMPLETE;

		/* See if the device has been/is being stopped. Note
		 * that we ignore the quiesce state, since we are
		 * concerned about the actual device state.
		 */
		if ((isci_device->status == isci_stopping)
		    || (isci_device->status == isci_stopped)
		    )
			status = SAS_DEVICE_UNKNOWN;
		else
			status = SAS_ABORTED_TASK;

		complete_to_host = isci_perform_aborted_io_completion;
		/* This was an aborted request. */

		spin_unlock(&request->state_lock);
		break;

	case aborting:
		/* aborting means that the task management function tried and
		 * failed to abort the request. We need to note the request
		 * as SAS_TASK_UNDELIVERED, so that the scsi mid layer marks the
		 * target as down.
		 *
		 * Aborting also means an external thread is explicitly managing
		 * this request, so that we do not complete it up the stack.
		 */
		request->complete_in_target = true;
		response = SAS_TASK_UNDELIVERED;

		if ((isci_device->status == isci_stopping) ||
		    (isci_device->status == isci_stopped))
			/* The device has been /is being stopped. Note that
			 * we ignore the quiesce state, since we are
			 * concerned about the actual device state.
			 */
			status = SAS_DEVICE_UNKNOWN;
		else
			status = SAS_PHY_DOWN;

		complete_to_host = isci_perform_aborted_io_completion;

		/* This was an aborted request. */

		spin_unlock(&request->state_lock);
		break;

	case terminating:

		/* This was an terminated request.  This happens when
		 * the I/O is being terminated because of an action on
		 * the device (reset, tear down, etc.), and the I/O needs
		 * to be completed up the stack.
		 */
		request->complete_in_target = true;
		response = SAS_TASK_UNDELIVERED;

		/* See if the device has been/is being stopped. Note
		 * that we ignore the quiesce state, since we are
		 * concerned about the actual device state.
		 */
		if ((isci_device->status == isci_stopping) ||
		    (isci_device->status == isci_stopped))
			status = SAS_DEVICE_UNKNOWN;
		else
			status = SAS_ABORTED_TASK;

		complete_to_host = isci_perform_aborted_io_completion;

		/* This was a terminated request. */

		spin_unlock(&request->state_lock);
		break;

	default:

		/* The request is done from an SCU HW perspective. */
		request->status = completed;

		spin_unlock(&request->state_lock);

		/* This is an active request being completed from the core. */
		switch (completion_status) {

		case SCI_IO_FAILURE_RESPONSE_VALID:
			dev_dbg(&isci_host->pdev->dev,
				"%s: SCI_IO_FAILURE_RESPONSE_VALID (%p/%p)\n",
				__func__,
				request,
				task);

			if (sas_protocol_ata(task->task_proto)) {
				resp_buf = &request->sci.stp.rsp;
				isci_request_process_stp_response(task,
								  resp_buf);
			} else if (SAS_PROTOCOL_SSP == task->task_proto) {

				/* crack the iu response buffer. */
				resp_iu = &request->sci.ssp.rsp;
				isci_request_process_response_iu(task, resp_iu,
								 &isci_host->pdev->dev);

			} else if (SAS_PROTOCOL_SMP == task->task_proto) {

				dev_err(&isci_host->pdev->dev,
					"%s: SCI_IO_FAILURE_RESPONSE_VALID: "
					"SAS_PROTOCOL_SMP protocol\n",
					__func__);

			} else
				dev_err(&isci_host->pdev->dev,
					"%s: unknown protocol\n", __func__);

			/* use the task status set in the task struct by the
			 * isci_request_process_response_iu call.
			 */
			request->complete_in_target = true;
			response = task->task_status.resp;
			status = task->task_status.stat;
			break;

		case SCI_IO_SUCCESS:
		case SCI_IO_SUCCESS_IO_DONE_EARLY:

			response = SAS_TASK_COMPLETE;
			status   = SAM_STAT_GOOD;
			request->complete_in_target = true;

			if (task->task_proto == SAS_PROTOCOL_SMP) {
				void *rsp = &request->sci.smp.rsp;

				dev_dbg(&isci_host->pdev->dev,
					"%s: SMP protocol completion\n",
					__func__);

				sg_copy_from_buffer(
					&task->smp_task.smp_resp, 1,
					rsp, sizeof(struct smp_resp));
			} else if (completion_status
				   == SCI_IO_SUCCESS_IO_DONE_EARLY) {

				/* This was an SSP / STP / SATA transfer.
				 * There is a possibility that less data than
				 * the maximum was transferred.
				 */
				u32 transferred_length = sci_req_tx_bytes(&request->sci);

				task->task_status.residual
					= task->total_xfer_len - transferred_length;

				/* If there were residual bytes, call this an
				 * underrun.
				 */
				if (task->task_status.residual != 0)
					status = SAS_DATA_UNDERRUN;

				dev_dbg(&isci_host->pdev->dev,
					"%s: SCI_IO_SUCCESS_IO_DONE_EARLY %d\n",
					__func__,
					status);

			} else
				dev_dbg(&isci_host->pdev->dev,
					"%s: SCI_IO_SUCCESS\n",
					__func__);

			break;

		case SCI_IO_FAILURE_TERMINATED:
			dev_dbg(&isci_host->pdev->dev,
				"%s: SCI_IO_FAILURE_TERMINATED (%p/%p)\n",
				__func__,
				request,
				task);

			/* The request was terminated explicitly.  No handling
			 * is needed in the SCSI error handler path.
			 */
			request->complete_in_target = true;
			response = SAS_TASK_UNDELIVERED;

			/* See if the device has been/is being stopped. Note
			 * that we ignore the quiesce state, since we are
			 * concerned about the actual device state.
			 */
			if ((isci_device->status == isci_stopping) ||
			    (isci_device->status == isci_stopped))
				status = SAS_DEVICE_UNKNOWN;
			else
				status = SAS_ABORTED_TASK;

			complete_to_host = isci_perform_normal_io_completion;
			break;

		case SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR:

			isci_request_handle_controller_specific_errors(
				isci_device, request, task, &response, &status,
				&complete_to_host);

			break;

		case SCI_IO_FAILURE_REMOTE_DEVICE_RESET_REQUIRED:
			/* This is a special case, in that the I/O completion
			 * is telling us that the device needs a reset.
			 * In order for the device reset condition to be
			 * noticed, the I/O has to be handled in the error
			 * handler.  Set the reset flag and cause the
			 * SCSI error thread to be scheduled.
			 */
			spin_lock_irqsave(&task->task_state_lock, task_flags);
			task->task_state_flags |= SAS_TASK_NEED_DEV_RESET;
			spin_unlock_irqrestore(&task->task_state_lock, task_flags);

			/* Fail the I/O. */
			response = SAS_TASK_UNDELIVERED;
			status = SAM_STAT_TASK_ABORTED;

			complete_to_host = isci_perform_error_io_completion;
			request->complete_in_target = false;
			break;

		default:
			/* Catch any otherwise unhandled error codes here. */
			dev_warn(&isci_host->pdev->dev,
				 "%s: invalid completion code: 0x%x - "
				 "isci_request = %p\n",
				 __func__, completion_status, request);

			response = SAS_TASK_UNDELIVERED;

			/* See if the device has been/is being stopped. Note
			 * that we ignore the quiesce state, since we are
			 * concerned about the actual device state.
			 */
			if ((isci_device->status == isci_stopping) ||
			    (isci_device->status == isci_stopped))
				status = SAS_DEVICE_UNKNOWN;
			else
				status = SAS_ABORTED_TASK;

			complete_to_host = isci_perform_error_io_completion;
			request->complete_in_target = false;
			break;
		}
		break;
	}

	isci_request_unmap_sgl(request, isci_host->pdev);

	/* Put the completed request on the correct list */
	isci_task_save_for_upper_layer_completion(isci_host, request, response,
						  status, complete_to_host
						  );

	/* complete the io request to the core. */
	scic_controller_complete_io(&isci_host->sci,
				    &isci_device->sci,
				    &request->sci);
	/* set terminated handle so it cannot be completed or
	 * terminated again, and to cause any calls into abort
	 * task to recognize the already completed case.
	 */
	request->terminated = true;

	isci_host_can_dequeue(isci_host, 1);
}

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
	struct scic_sds_request *sci_req = object;

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
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCI_BASE_REQUEST_STATE_CONSTRUCTED
		);
}

static void scic_sds_request_started_state_enter(void *object)
{
	struct scic_sds_request *sci_req = object;
	struct sci_base_state_machine *sm = &sci_req->state_machine;
	struct isci_request *ireq = sci_req_to_ireq(sci_req);
	struct domain_device *dev = sci_dev_to_domain(sci_req->target_device);
	struct sas_task *task;

	/* XXX as hch said always creating an internal sas_task for tmf
	 * requests would simplify the driver
	 */
	task = ireq->ttype == io_task ? isci_request_access_task(ireq) : NULL;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCI_BASE_REQUEST_STATE_STARTED
		);

	/* all unaccelerated request types (non ssp or ncq) handled with
	 * substates
	 */
	if (!task && dev->dev_type == SAS_END_DEV) {
		sci_base_state_machine_change_state(sm,
			SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION);
	} else if (!task &&
		   (isci_request_access_tmf(ireq)->tmf_code == isci_tmf_sata_srst_high ||
		    isci_request_access_tmf(ireq)->tmf_code == isci_tmf_sata_srst_low)) {
		sci_base_state_machine_change_state(sm,
			SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE);
	} else if (task && task->task_proto == SAS_PROTOCOL_SMP) {
		sci_base_state_machine_change_state(sm,
			SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE);
	} else if (task && sas_protocol_ata(task->task_proto) &&
		   !task->ata_task.use_ncq) {
		u32 state;

		if (task->data_dir == DMA_NONE)
			 state = SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE;
		else if (task->ata_task.dma_xfer)
			state = SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE;
		else /* PIO */
			state = SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE;

		sci_base_state_machine_change_state(sm, state);
	}
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
	struct scic_sds_request *sci_req = object;
	struct scic_sds_controller *scic =
		scic_sds_request_get_controller(sci_req);
	struct isci_host *ihost = scic_to_ihost(scic);
	struct isci_request *ireq = sci_req_to_ireq(sci_req);

	SET_STATE_HANDLER(sci_req,
			  scic_sds_request_state_handler_table,
			  SCI_BASE_REQUEST_STATE_COMPLETED);

	/* Tell the SCI_USER that the IO request is complete */
	if (sci_req->is_task_management_request == false)
		isci_request_io_request_complete(ihost, ireq,
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
	struct scic_sds_request *sci_req = object;

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
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCI_BASE_REQUEST_STATE_FINAL
		);
}

static void scic_sds_io_request_started_task_mgmt_await_tc_completion_substate_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION
		);
}

static void scic_sds_io_request_started_task_mgmt_await_task_response_substate_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE
		);
}

static void scic_sds_smp_request_started_await_response_substate_enter(void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE
		);
}

static void scic_sds_smp_request_started_await_tc_completion_substate_enter(void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION
		);
}

static void scic_sds_stp_request_started_non_data_await_h2d_completion_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
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
		scic_sds_request_state_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE
		);
}



static void scic_sds_stp_request_started_pio_await_h2d_completion_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
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
		scic_sds_request_state_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE
		);
}

static void scic_sds_stp_request_started_pio_data_in_await_data_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE
		);
}

static void scic_sds_stp_request_started_pio_data_out_transmit_data_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE
		);
}



static void scic_sds_stp_request_started_udma_await_tc_completion_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
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
		scic_sds_request_state_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE
		);
}



static void scic_sds_stp_request_started_soft_reset_await_h2d_asserted_completion_enter(
	void *object)
{
	struct scic_sds_request *sci_req = object;

	SET_STATE_HANDLER(
		sci_req,
		scic_sds_request_state_handler_table,
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
	h2d_fis = &sci_req->stp.cmd;
	h2d_fis->control = 0;

	/* Clear the TC control bit */
	task_context = scic_sds_controller_get_task_context_buffer(
		sci_req->owning_controller, sci_req->io_tag);
	task_context->control_frame = 0;

	status = scic_controller_continue_io(sci_req);
	if (status == SCI_SUCCESS) {
		SET_STATE_HANDLER(
			sci_req,
			scic_sds_request_state_handler_table,
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
		scic_sds_request_state_handler_table,
		SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE
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
	},
	[SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_non_data_await_h2d_completion_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_non_data_await_d2h_enter,
	},
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
	},
	[SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_udma_await_tc_completion_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_udma_await_d2h_reg_fis_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_soft_reset_await_h2d_asserted_completion_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_soft_reset_await_h2d_diagnostic_completion_enter,
	},
	[SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE] = {
		.enter_state = scic_sds_stp_request_started_soft_reset_await_d2h_response_enter,
	},
	[SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION] = {
		.enter_state = scic_sds_io_request_started_task_mgmt_await_tc_completion_substate_enter,
	},
	[SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE] = {
		.enter_state = scic_sds_io_request_started_task_mgmt_await_task_response_substate_enter,
	},
	[SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE] = {
		.enter_state = scic_sds_smp_request_started_await_response_substate_enter,
	},
	[SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION] = {
		.enter_state = scic_sds_smp_request_started_await_tc_completion_substate_enter,
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
					       u16 io_tag, struct scic_sds_request *sci_req)
{
	sci_base_state_machine_construct(&sci_req->state_machine, sci_req,
			scic_sds_request_state_table, SCI_BASE_REQUEST_STATE_INITIAL);
	sci_base_state_machine_start(&sci_req->state_machine);

	sci_req->io_tag = io_tag;
	sci_req->owning_controller = scic;
	sci_req->target_device = sci_dev;
	sci_req->protocol = SCIC_NO_PROTOCOL;
	sci_req->saved_rx_frame_index = SCU_INVALID_FRAME_INDEX;
	sci_req->device_sequence = scic_sds_remote_device_get_sequence(sci_dev);

	sci_req->sci_status   = SCI_SUCCESS;
	sci_req->scu_status   = 0;
	sci_req->post_context = 0xFFFFFFFF;

	sci_req->is_task_management_request = false;

	if (io_tag == SCI_CONTROLLER_INVALID_IO_TAG) {
		sci_req->was_tag_assigned_by_user = false;
		sci_req->task_context_buffer = &sci_req->tc;
	} else {
		sci_req->was_tag_assigned_by_user = true;

		sci_req->task_context_buffer =
			scic_sds_controller_get_task_context_buffer(scic, io_tag);
	}
}

static enum sci_status
scic_io_request_construct(struct scic_sds_controller *scic,
			  struct scic_sds_remote_device *sci_dev,
			  u16 io_tag, struct scic_sds_request *sci_req)
{
	struct domain_device *dev = sci_dev_to_domain(sci_dev);
	enum sci_status status = SCI_SUCCESS;

	/* Build the common part of the request */
	scic_sds_general_request_construct(scic, sci_dev, io_tag, sci_req);

	if (sci_dev->rnc.remote_node_index == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX)
		return SCI_FAILURE_INVALID_REMOTE_DEVICE;

	if (dev->dev_type == SAS_END_DEV)
		/* pass */;
	else if (dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP))
		memset(&sci_req->stp.cmd, 0, sizeof(sci_req->stp.cmd));
	else if (dev_is_expander(dev))
		memset(&sci_req->smp.cmd, 0, sizeof(sci_req->smp.cmd));
	else
		return SCI_FAILURE_UNSUPPORTED_PROTOCOL;

	memset(sci_req->task_context_buffer, 0,
	       offsetof(struct scu_task_context, sgl_pair_ab));

	return status;
}

enum sci_status scic_task_request_construct(struct scic_sds_controller *scic,
					    struct scic_sds_remote_device *sci_dev,
					    u16 io_tag, struct scic_sds_request *sci_req)
{
	struct domain_device *dev = sci_dev_to_domain(sci_dev);
	enum sci_status status = SCI_SUCCESS;

	/* Build the common part of the request */
	scic_sds_general_request_construct(scic, sci_dev, io_tag, sci_req);

	if (dev->dev_type == SAS_END_DEV ||
	    dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP)) {
		sci_req->is_task_management_request = true;
		memset(sci_req->task_context_buffer, 0, sizeof(struct scu_task_context));
	} else
		status = SCI_FAILURE_UNSUPPORTED_PROTOCOL;

	return status;
}

static enum sci_status isci_request_ssp_request_construct(
	struct isci_request *request)
{
	enum sci_status status;

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: request = %p\n",
		__func__,
		request);
	status = scic_io_request_construct_basic_ssp(&request->sci);
	return status;
}

static enum sci_status isci_request_stp_request_construct(
	struct isci_request *request)
{
	struct sas_task *task = isci_request_access_task(request);
	enum sci_status status;
	struct host_to_dev_fis *register_fis;

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: request = %p\n",
		__func__,
		request);

	/* Get the host_to_dev_fis from the core and copy
	 * the fis from the task into it.
	 */
	register_fis = isci_sata_task_to_fis_copy(task);

	status = scic_io_request_construct_basic_sata(&request->sci);

	/* Set the ncq tag in the fis, from the queue
	 * command in the task.
	 */
	if (isci_sata_is_task_ncq(task)) {

		isci_sata_set_ncq_tag(
			register_fis,
			task
			);
	}

	return status;
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
	sci_swab32_cpy(&sci_req->smp.cmd, smp_req,
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
	memcpy(&task_context->type.smp, &sci_req->smp.cmd, sizeof(u32));

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
						((char *) &sci_req->smp.cmd) +
						sizeof(u32));

	task_context->command_iu_upper = upper_32_bits(dma_addr);
	task_context->command_iu_lower = lower_32_bits(dma_addr);

	/* SMP response comes as UF, so no need to set response IU address. */
	task_context->response_iu_upper = 0;
	task_context->response_iu_lower = 0;
}

static enum sci_status scic_io_request_construct_smp(struct scic_sds_request *sci_req)
{
	struct smp_req *smp_req = kmalloc(sizeof(*smp_req), GFP_KERNEL);

	if (!smp_req)
		return SCI_FAILURE_INSUFFICIENT_RESOURCES;

	sci_req->protocol = SCIC_SMP_PROTOCOL;

	/* Construct the SMP SCU Task Context */
	memcpy(smp_req, &sci_req->smp.cmd, sizeof(*smp_req));

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

/*
 * isci_smp_request_build() - This function builds the smp request.
 * @ireq: This parameter points to the isci_request allocated in the
 *    request construct function.
 *
 * SCI_SUCCESS on successfull completion, or specific failure code.
 */
static enum sci_status isci_smp_request_build(struct isci_request *ireq)
{
	enum sci_status status = SCI_FAILURE;
	struct sas_task *task = isci_request_access_task(ireq);
	struct scic_sds_request *sci_req = &ireq->sci;

	dev_dbg(&ireq->isci_host->pdev->dev,
		"%s: request = %p\n", __func__, ireq);

	dev_dbg(&ireq->isci_host->pdev->dev,
		"%s: smp_req len = %d\n",
		__func__,
		task->smp_task.smp_req.length);

	/* copy the smp_command to the address; */
	sg_copy_to_buffer(&task->smp_task.smp_req, 1,
			  &sci_req->smp.cmd,
			  sizeof(struct smp_req));

	status = scic_io_request_construct_smp(sci_req);
	if (status != SCI_SUCCESS)
		dev_warn(&ireq->isci_host->pdev->dev,
			 "%s: failed with status = %d\n",
			 __func__,
			 status);

	return status;
}

/**
 * isci_io_request_build() - This function builds the io request object.
 * @isci_host: This parameter specifies the ISCI host object
 * @request: This parameter points to the isci_request object allocated in the
 *    request construct function.
 * @sci_device: This parameter is the handle for the sci core's remote device
 *    object that is the destination for this request.
 *
 * SCI_SUCCESS on successfull completion, or specific failure code.
 */
static enum sci_status isci_io_request_build(
	struct isci_host *isci_host,
	struct isci_request *request,
	struct isci_remote_device *isci_device)
{
	enum sci_status status = SCI_SUCCESS;
	struct sas_task *task = isci_request_access_task(request);
	struct scic_sds_remote_device *sci_device = &isci_device->sci;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = 0x%p; request = %p, "
		"num_scatter = %d\n",
		__func__,
		isci_device,
		request,
		task->num_scatter);

	/* map the sgl addresses, if present.
	 * libata does the mapping for sata devices
	 * before we get the request.
	 */
	if (task->num_scatter &&
	    !sas_protocol_ata(task->task_proto) &&
	    !(SAS_PROTOCOL_SMP & task->task_proto)) {

		request->num_sg_entries = dma_map_sg(
			&isci_host->pdev->dev,
			task->scatter,
			task->num_scatter,
			task->data_dir
			);

		if (request->num_sg_entries == 0)
			return SCI_FAILURE_INSUFFICIENT_RESOURCES;
	}

	/* build the common request object. For now,
	 * we will let the core allocate the IO tag.
	 */
	status = scic_io_request_construct(&isci_host->sci, sci_device,
					   SCI_CONTROLLER_INVALID_IO_TAG,
					   &request->sci);

	if (status != SCI_SUCCESS) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: failed request construct\n",
			 __func__);
		return SCI_FAILURE;
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		status = isci_smp_request_build(request);
		break;
	case SAS_PROTOCOL_SSP:
		status = isci_request_ssp_request_construct(request);
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
		status = isci_request_stp_request_construct(request);
		break;
	default:
		dev_warn(&isci_host->pdev->dev,
			 "%s: unknown protocol\n", __func__);
		return SCI_FAILURE;
	}

	return SCI_SUCCESS;
}

/**
 * isci_request_alloc_core() - This function gets the request object from the
 *    isci_host dma cache.
 * @isci_host: This parameter specifies the ISCI host object
 * @isci_request: This parameter will contain the pointer to the new
 *    isci_request object.
 * @isci_device: This parameter is the pointer to the isci remote device object
 *    that is the destination for this request.
 * @gfp_flags: This parameter specifies the os allocation flags.
 *
 * SCI_SUCCESS on successfull completion, or specific failure code.
 */
static int isci_request_alloc_core(
	struct isci_host *isci_host,
	struct isci_request **isci_request,
	struct isci_remote_device *isci_device,
	gfp_t gfp_flags)
{
	int ret = 0;
	dma_addr_t handle;
	struct isci_request *request;


	/* get pointer to dma memory. This actually points
	 * to both the isci_remote_device object and the
	 * sci object. The isci object is at the beginning
	 * of the memory allocated here.
	 */
	request = dma_pool_alloc(isci_host->dma_pool, gfp_flags, &handle);
	if (!request) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: dma_pool_alloc returned NULL\n", __func__);
		return -ENOMEM;
	}

	/* initialize the request object.	*/
	spin_lock_init(&request->state_lock);
	request->request_daddr = handle;
	request->isci_host = isci_host;
	request->isci_device = isci_device;
	request->io_request_completion = NULL;
	request->terminated = false;

	request->num_sg_entries = 0;

	request->complete_in_target = false;

	INIT_LIST_HEAD(&request->completed_node);
	INIT_LIST_HEAD(&request->dev_node);

	*isci_request = request;
	isci_request_change_state(request, allocated);

	return ret;
}

static int isci_request_alloc_io(
	struct isci_host *isci_host,
	struct sas_task *task,
	struct isci_request **isci_request,
	struct isci_remote_device *isci_device,
	gfp_t gfp_flags)
{
	int retval = isci_request_alloc_core(isci_host, isci_request,
					     isci_device, gfp_flags);

	if (!retval) {
		(*isci_request)->ttype_ptr.io_task_ptr = task;
		(*isci_request)->ttype                 = io_task;

		task->lldd_task = *isci_request;
	}
	return retval;
}

/**
 * isci_request_alloc_tmf() - This function gets the request object from the
 *    isci_host dma cache and initializes the relevant fields as a sas_task.
 * @isci_host: This parameter specifies the ISCI host object
 * @sas_task: This parameter is the task struct from the upper layer driver.
 * @isci_request: This parameter will contain the pointer to the new
 *    isci_request object.
 * @isci_device: This parameter is the pointer to the isci remote device object
 *    that is the destination for this request.
 * @gfp_flags: This parameter specifies the os allocation flags.
 *
 * SCI_SUCCESS on successfull completion, or specific failure code.
 */
int isci_request_alloc_tmf(
	struct isci_host *isci_host,
	struct isci_tmf *isci_tmf,
	struct isci_request **isci_request,
	struct isci_remote_device *isci_device,
	gfp_t gfp_flags)
{
	int retval = isci_request_alloc_core(isci_host, isci_request,
					     isci_device, gfp_flags);

	if (!retval) {

		(*isci_request)->ttype_ptr.tmf_task_ptr = isci_tmf;
		(*isci_request)->ttype = tmf_task;
	}
	return retval;
}

/**
 * isci_request_execute() - This function allocates the isci_request object,
 *    all fills in some common fields.
 * @isci_host: This parameter specifies the ISCI host object
 * @sas_task: This parameter is the task struct from the upper layer driver.
 * @isci_request: This parameter will contain the pointer to the new
 *    isci_request object.
 * @gfp_flags: This parameter specifies the os allocation flags.
 *
 * SCI_SUCCESS on successfull completion, or specific failure code.
 */
int isci_request_execute(
	struct isci_host *isci_host,
	struct sas_task *task,
	struct isci_request **isci_request,
	gfp_t gfp_flags)
{
	int ret = 0;
	struct scic_sds_remote_device *sci_device;
	enum sci_status status = SCI_FAILURE_UNSUPPORTED_PROTOCOL;
	struct isci_remote_device *isci_device;
	struct isci_request *request;
	unsigned long flags;

	isci_device = task->dev->lldd_dev;
	sci_device = &isci_device->sci;

	/* do common allocation and init of request object. */
	ret = isci_request_alloc_io(
		isci_host,
		task,
		&request,
		isci_device,
		gfp_flags
		);

	if (ret)
		goto out;

	status = isci_io_request_build(isci_host, request, isci_device);
	if (status != SCI_SUCCESS) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: request_construct failed - status = 0x%x\n",
			 __func__,
			 status);
		goto out;
	}

	spin_lock_irqsave(&isci_host->scic_lock, flags);

	/* send the request, let the core assign the IO TAG.	*/
	status = scic_controller_start_io(&isci_host->sci, sci_device,
					  &request->sci,
					  SCI_CONTROLLER_INVALID_IO_TAG);
	if (status != SCI_SUCCESS &&
	    status != SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: failed request start (0x%x)\n",
			 __func__, status);
		spin_unlock_irqrestore(&isci_host->scic_lock, flags);
		goto out;
	}

	/* Either I/O started OK, or the core has signaled that
	 * the device needs a target reset.
	 *
	 * In either case, hold onto the I/O for later.
	 *
	 * Update it's status and add it to the list in the
	 * remote device object.
	 */
	isci_request_change_state(request, started);
	list_add(&request->dev_node, &isci_device->reqs_in_process);

	if (status == SCI_SUCCESS) {
		/* Save the tag for possible task mgmt later. */
		request->io_tag = request->sci.io_tag;
	} else {
		/* The request did not really start in the
		 * hardware, so clear the request handle
		 * here so no terminations will be done.
		 */
		request->terminated = true;
	}
	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	if (status ==
	    SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED) {
		/* Signal libsas that we need the SCSI error
		* handler thread to work on this I/O and that
		* we want a device reset.
		*/
		spin_lock_irqsave(&task->task_state_lock, flags);
		task->task_state_flags |= SAS_TASK_NEED_DEV_RESET;
		spin_unlock_irqrestore(&task->task_state_lock, flags);

		/* Cause this task to be scheduled in the SCSI error
		* handler thread.
		*/
		isci_execpath_callback(isci_host, task,
				       sas_task_abort);

		/* Change the status, since we are holding
		* the I/O until it is managed by the SCSI
		* error handler.
		*/
		status = SCI_SUCCESS;
	}

 out:
	if (status != SCI_SUCCESS) {
		/* release dma memory on failure. */
		isci_request_free(isci_host, request);
		request = NULL;
		ret = SCI_FAILURE;
	}

	*isci_request = request;
	return ret;
}
