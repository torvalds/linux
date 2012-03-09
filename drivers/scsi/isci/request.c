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

#include <scsi/scsi_cmnd.h>
#include "isci.h"
#include "task.h"
#include "request.h"
#include "scu_completion_codes.h"
#include "scu_event_codes.h"
#include "sas.h"

#undef C
#define C(a) (#a)
const char *req_state_name(enum sci_base_request_states state)
{
	static const char * const strings[] = REQUEST_STATES;

	return strings[state];
}
#undef C

static struct scu_sgl_element_pair *to_sgl_element_pair(struct isci_request *ireq,
							int idx)
{
	if (idx == 0)
		return &ireq->tc->sgl_pair_ab;
	else if (idx == 1)
		return &ireq->tc->sgl_pair_cd;
	else if (idx < 0)
		return NULL;
	else
		return &ireq->sg_table[idx - 2];
}

static dma_addr_t to_sgl_element_pair_dma(struct isci_host *ihost,
					  struct isci_request *ireq, u32 idx)
{
	u32 offset;

	if (idx == 0) {
		offset = (void *) &ireq->tc->sgl_pair_ab -
			 (void *) &ihost->task_context_table[0];
		return ihost->tc_dma + offset;
	} else if (idx == 1) {
		offset = (void *) &ireq->tc->sgl_pair_cd -
			 (void *) &ihost->task_context_table[0];
		return ihost->tc_dma + offset;
	}

	return sci_io_request_get_dma_addr(ireq, &ireq->sg_table[idx - 2]);
}

static void init_sgl_element(struct scu_sgl_element *e, struct scatterlist *sg)
{
	e->length = sg_dma_len(sg);
	e->address_upper = upper_32_bits(sg_dma_address(sg));
	e->address_lower = lower_32_bits(sg_dma_address(sg));
	e->address_modifier = 0;
}

static void sci_request_build_sgl(struct isci_request *ireq)
{
	struct isci_host *ihost = ireq->isci_host;
	struct sas_task *task = isci_request_access_task(ireq);
	struct scatterlist *sg = NULL;
	dma_addr_t dma_addr;
	u32 sg_idx = 0;
	struct scu_sgl_element_pair *scu_sg   = NULL;
	struct scu_sgl_element_pair *prev_sg  = NULL;

	if (task->num_scatter > 0) {
		sg = task->scatter;

		while (sg) {
			scu_sg = to_sgl_element_pair(ireq, sg_idx);
			init_sgl_element(&scu_sg->A, sg);
			sg = sg_next(sg);
			if (sg) {
				init_sgl_element(&scu_sg->B, sg);
				sg = sg_next(sg);
			} else
				memset(&scu_sg->B, 0, sizeof(scu_sg->B));

			if (prev_sg) {
				dma_addr = to_sgl_element_pair_dma(ihost,
								   ireq,
								   sg_idx);

				prev_sg->next_pair_upper =
					upper_32_bits(dma_addr);
				prev_sg->next_pair_lower =
					lower_32_bits(dma_addr);
			}

			prev_sg = scu_sg;
			sg_idx++;
		}
	} else {	/* handle when no sg */
		scu_sg = to_sgl_element_pair(ireq, sg_idx);

		dma_addr = dma_map_single(&ihost->pdev->dev,
					  task->scatter,
					  task->total_xfer_len,
					  task->data_dir);

		ireq->zero_scatter_daddr = dma_addr;

		scu_sg->A.length = task->total_xfer_len;
		scu_sg->A.address_upper = upper_32_bits(dma_addr);
		scu_sg->A.address_lower = lower_32_bits(dma_addr);
	}

	if (scu_sg) {
		scu_sg->next_pair_upper = 0;
		scu_sg->next_pair_lower = 0;
	}
}

static void sci_io_request_build_ssp_command_iu(struct isci_request *ireq)
{
	struct ssp_cmd_iu *cmd_iu;
	struct sas_task *task = isci_request_access_task(ireq);

	cmd_iu = &ireq->ssp.cmd;

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

static void sci_task_request_build_ssp_task_iu(struct isci_request *ireq)
{
	struct ssp_task_iu *task_iu;
	struct sas_task *task = isci_request_access_task(ireq);
	struct isci_tmf *isci_tmf = isci_request_access_tmf(ireq);

	task_iu = &ireq->ssp.tmf;

	memset(task_iu, 0, sizeof(struct ssp_task_iu));

	memcpy(task_iu->LUN, task->ssp_task.LUN, 8);

	task_iu->task_func = isci_tmf->tmf_code;
	task_iu->task_tag =
		(test_bit(IREQ_TMF, &ireq->flags)) ?
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
	struct isci_request *ireq,
	struct scu_task_context *task_context)
{
	dma_addr_t dma_addr;
	struct isci_remote_device *idev;
	struct isci_port *iport;

	idev = ireq->target_device;
	iport = idev->owning_port;

	/* Fill in the TC with the its required data */
	task_context->abort = 0;
	task_context->priority = 0;
	task_context->initiator_request = 1;
	task_context->connection_rate = idev->connection_rate;
	task_context->protocol_engine_index = ISCI_PEG;
	task_context->logical_port_index = iport->physical_port_index;
	task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_SSP;
	task_context->valid = SCU_TASK_CONTEXT_VALID;
	task_context->context_type = SCU_TASK_CONTEXT_TYPE;

	task_context->remote_node_index = idev->rnc.remote_node_index;
	task_context->command_code = 0;

	task_context->link_layer_control = 0;
	task_context->do_not_dma_ssp_good_response = 1;
	task_context->strict_ordering = 0;
	task_context->control_frame = 0;
	task_context->timeout_enable = 0;
	task_context->block_guard_enable = 0;

	task_context->address_modifier = 0;

	/* task_context->type.ssp.tag = ireq->io_tag; */
	task_context->task_phase = 0x01;

	ireq->post_context = (SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC |
			      (ISCI_PEG << SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) |
			      (iport->physical_port_index <<
			       SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT) |
			      ISCI_TAG_TCI(ireq->io_tag));

	/*
	 * Copy the physical address for the command buffer to the
	 * SCU Task Context
	 */
	dma_addr = sci_io_request_get_dma_addr(ireq, &ireq->ssp.cmd);

	task_context->command_iu_upper = upper_32_bits(dma_addr);
	task_context->command_iu_lower = lower_32_bits(dma_addr);

	/*
	 * Copy the physical address for the response buffer to the
	 * SCU Task Context
	 */
	dma_addr = sci_io_request_get_dma_addr(ireq, &ireq->ssp.rsp);

	task_context->response_iu_upper = upper_32_bits(dma_addr);
	task_context->response_iu_lower = lower_32_bits(dma_addr);
}

static u8 scu_bg_blk_size(struct scsi_device *sdp)
{
	switch (sdp->sector_size) {
	case 512:
		return 0;
	case 1024:
		return 1;
	case 4096:
		return 3;
	default:
		return 0xff;
	}
}

static u32 scu_dif_bytes(u32 len, u32 sector_size)
{
	return (len >> ilog2(sector_size)) * 8;
}

static void scu_ssp_ireq_dif_insert(struct isci_request *ireq, u8 type, u8 op)
{
	struct scu_task_context *tc = ireq->tc;
	struct scsi_cmnd *scmd = ireq->ttype_ptr.io_task_ptr->uldd_task;
	u8 blk_sz = scu_bg_blk_size(scmd->device);

	tc->block_guard_enable = 1;
	tc->blk_prot_en = 1;
	tc->blk_sz = blk_sz;
	/* DIF write insert */
	tc->blk_prot_func = 0x2;

	tc->transfer_length_bytes += scu_dif_bytes(tc->transfer_length_bytes,
						   scmd->device->sector_size);

	/* always init to 0, used by hw */
	tc->interm_crc_val = 0;

	tc->init_crc_seed = 0;
	tc->app_tag_verify = 0;
	tc->app_tag_gen = 0;
	tc->ref_tag_seed_verify = 0;

	/* always init to same as bg_blk_sz */
	tc->UD_bytes_immed_val = scmd->device->sector_size;

	tc->reserved_DC_0 = 0;

	/* always init to 8 */
	tc->DIF_bytes_immed_val = 8;

	tc->reserved_DC_1 = 0;
	tc->bgc_blk_sz = scmd->device->sector_size;
	tc->reserved_E0_0 = 0;
	tc->app_tag_gen_mask = 0;

	/** setup block guard control **/
	tc->bgctl = 0;

	/* DIF write insert */
	tc->bgctl_f.op = 0x2;

	tc->app_tag_verify_mask = 0;

	/* must init to 0 for hw */
	tc->blk_guard_err = 0;

	tc->reserved_E8_0 = 0;

	if ((type & SCSI_PROT_DIF_TYPE1) || (type & SCSI_PROT_DIF_TYPE2))
		tc->ref_tag_seed_gen = scsi_get_lba(scmd) & 0xffffffff;
	else if (type & SCSI_PROT_DIF_TYPE3)
		tc->ref_tag_seed_gen = 0;
}

static void scu_ssp_ireq_dif_strip(struct isci_request *ireq, u8 type, u8 op)
{
	struct scu_task_context *tc = ireq->tc;
	struct scsi_cmnd *scmd = ireq->ttype_ptr.io_task_ptr->uldd_task;
	u8 blk_sz = scu_bg_blk_size(scmd->device);

	tc->block_guard_enable = 1;
	tc->blk_prot_en = 1;
	tc->blk_sz = blk_sz;
	/* DIF read strip */
	tc->blk_prot_func = 0x1;

	tc->transfer_length_bytes += scu_dif_bytes(tc->transfer_length_bytes,
						   scmd->device->sector_size);

	/* always init to 0, used by hw */
	tc->interm_crc_val = 0;

	tc->init_crc_seed = 0;
	tc->app_tag_verify = 0;
	tc->app_tag_gen = 0;

	if ((type & SCSI_PROT_DIF_TYPE1) || (type & SCSI_PROT_DIF_TYPE2))
		tc->ref_tag_seed_verify = scsi_get_lba(scmd) & 0xffffffff;
	else if (type & SCSI_PROT_DIF_TYPE3)
		tc->ref_tag_seed_verify = 0;

	/* always init to same as bg_blk_sz */
	tc->UD_bytes_immed_val = scmd->device->sector_size;

	tc->reserved_DC_0 = 0;

	/* always init to 8 */
	tc->DIF_bytes_immed_val = 8;

	tc->reserved_DC_1 = 0;
	tc->bgc_blk_sz = scmd->device->sector_size;
	tc->reserved_E0_0 = 0;
	tc->app_tag_gen_mask = 0;

	/** setup block guard control **/
	tc->bgctl = 0;

	/* DIF read strip */
	tc->bgctl_f.crc_verify = 1;
	tc->bgctl_f.op = 0x1;
	if ((type & SCSI_PROT_DIF_TYPE1) || (type & SCSI_PROT_DIF_TYPE2)) {
		tc->bgctl_f.ref_tag_chk = 1;
		tc->bgctl_f.app_f_detect = 1;
	} else if (type & SCSI_PROT_DIF_TYPE3)
		tc->bgctl_f.app_ref_f_detect = 1;

	tc->app_tag_verify_mask = 0;

	/* must init to 0 for hw */
	tc->blk_guard_err = 0;

	tc->reserved_E8_0 = 0;
	tc->ref_tag_seed_gen = 0;
}

/**
 * This method is will fill in the SCU Task Context for a SSP IO request.
 * @sci_req:
 *
 */
static void scu_ssp_io_request_construct_task_context(struct isci_request *ireq,
						      enum dma_data_direction dir,
						      u32 len)
{
	struct scu_task_context *task_context = ireq->tc;
	struct sas_task *sas_task = ireq->ttype_ptr.io_task_ptr;
	struct scsi_cmnd *scmd = sas_task->uldd_task;
	u8 prot_type = scsi_get_prot_type(scmd);
	u8 prot_op = scsi_get_prot_op(scmd);

	scu_ssp_reqeust_construct_task_context(ireq, task_context);

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
		sci_request_build_sgl(ireq);

	if (prot_type != SCSI_PROT_DIF_TYPE0) {
		if (prot_op == SCSI_PROT_READ_STRIP)
			scu_ssp_ireq_dif_strip(ireq, prot_type, prot_op);
		else if (prot_op == SCSI_PROT_WRITE_INSERT)
			scu_ssp_ireq_dif_insert(ireq, prot_type, prot_op);
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
static void scu_ssp_task_request_construct_task_context(struct isci_request *ireq)
{
	struct scu_task_context *task_context = ireq->tc;

	scu_ssp_reqeust_construct_task_context(ireq, task_context);

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
	struct isci_request *ireq,
	struct scu_task_context *task_context)
{
	dma_addr_t dma_addr;
	struct isci_remote_device *idev;
	struct isci_port *iport;

	idev = ireq->target_device;
	iport = idev->owning_port;

	/* Fill in the TC with the its required data */
	task_context->abort = 0;
	task_context->priority = SCU_TASK_PRIORITY_NORMAL;
	task_context->initiator_request = 1;
	task_context->connection_rate = idev->connection_rate;
	task_context->protocol_engine_index = ISCI_PEG;
	task_context->logical_port_index = iport->physical_port_index;
	task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_STP;
	task_context->valid = SCU_TASK_CONTEXT_VALID;
	task_context->context_type = SCU_TASK_CONTEXT_TYPE;

	task_context->remote_node_index = idev->rnc.remote_node_index;
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
	task_context->type.words[0] = *(u32 *)&ireq->stp.cmd;

	ireq->post_context = (SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC |
			      (ISCI_PEG << SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) |
			      (iport->physical_port_index <<
			       SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT) |
			      ISCI_TAG_TCI(ireq->io_tag));
	/*
	 * Copy the physical address for the command buffer to the SCU Task
	 * Context. We must offset the command buffer by 4 bytes because the
	 * first 4 bytes are transfered in the body of the TC.
	 */
	dma_addr = sci_io_request_get_dma_addr(ireq,
						((char *) &ireq->stp.cmd) +
						sizeof(u32));

	task_context->command_iu_upper = upper_32_bits(dma_addr);
	task_context->command_iu_lower = lower_32_bits(dma_addr);

	/* SATA Requests do not have a response buffer */
	task_context->response_iu_upper = 0;
	task_context->response_iu_lower = 0;
}

static void scu_stp_raw_request_construct_task_context(struct isci_request *ireq)
{
	struct scu_task_context *task_context = ireq->tc;

	scu_sata_reqeust_construct_task_context(ireq, task_context);

	task_context->control_frame         = 0;
	task_context->priority              = SCU_TASK_PRIORITY_NORMAL;
	task_context->task_type             = SCU_TASK_TYPE_SATA_RAW_FRAME;
	task_context->type.stp.fis_type     = FIS_REGH2D;
	task_context->transfer_length_bytes = sizeof(struct host_to_dev_fis) - sizeof(u32);
}

static enum sci_status sci_stp_pio_request_construct(struct isci_request *ireq,
							  bool copy_rx_frame)
{
	struct isci_stp_request *stp_req = &ireq->stp.req;

	scu_stp_raw_request_construct_task_context(ireq);

	stp_req->status = 0;
	stp_req->sgl.offset = 0;
	stp_req->sgl.set = SCU_SGL_ELEMENT_PAIR_A;

	if (copy_rx_frame) {
		sci_request_build_sgl(ireq);
		stp_req->sgl.index = 0;
	} else {
		/* The user does not want the data copied to the SGL buffer location */
		stp_req->sgl.index = -1;
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
static void sci_stp_optimized_request_construct(struct isci_request *ireq,
						     u8 optimized_task_type,
						     u32 len,
						     enum dma_data_direction dir)
{
	struct scu_task_context *task_context = ireq->tc;

	/* Build the STP task context structure */
	scu_sata_reqeust_construct_task_context(ireq, task_context);

	/* Copy over the SGL elements */
	sci_request_build_sgl(ireq);

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

static void sci_atapi_construct(struct isci_request *ireq)
{
	struct host_to_dev_fis *h2d_fis = &ireq->stp.cmd;
	struct sas_task *task;

	/* To simplify the implementation we take advantage of the
	 * silicon's partial acceleration of atapi protocol (dma data
	 * transfers), so we promote all commands to dma protocol.  This
	 * breaks compatibility with ATA_HORKAGE_ATAPI_MOD16_DMA drives.
	 */
	h2d_fis->features |= ATAPI_PKT_DMA;

	scu_stp_raw_request_construct_task_context(ireq);

	task = isci_request_access_task(ireq);
	if (task->data_dir == DMA_NONE)
		task->total_xfer_len = 0;

	/* clear the response so we can detect arrivial of an
	 * unsolicited h2d fis
	 */
	ireq->stp.rsp.fis_type = 0;
}

static enum sci_status
sci_io_request_construct_sata(struct isci_request *ireq,
			       u32 len,
			       enum dma_data_direction dir,
			       bool copy)
{
	enum sci_status status = SCI_SUCCESS;
	struct sas_task *task = isci_request_access_task(ireq);
	struct domain_device *dev = ireq->target_device->domain_dev;

	/* check for management protocols */
	if (test_bit(IREQ_TMF, &ireq->flags)) {
		struct isci_tmf *tmf = isci_request_access_tmf(ireq);

		dev_err(&ireq->owning_controller->pdev->dev,
			"%s: Request 0x%p received un-handled SAT "
			"management protocol 0x%x.\n",
			__func__, ireq, tmf->tmf_code);

		return SCI_FAILURE;
	}

	if (!sas_protocol_ata(task->task_proto)) {
		dev_err(&ireq->owning_controller->pdev->dev,
			"%s: Non-ATA protocol in SATA path: 0x%x\n",
			__func__,
			task->task_proto);
		return SCI_FAILURE;

	}

	/* ATAPI */
	if (dev->sata_dev.command_set == ATAPI_COMMAND_SET &&
	    task->ata_task.fis.command == ATA_CMD_PACKET) {
		sci_atapi_construct(ireq);
		return SCI_SUCCESS;
	}

	/* non data */
	if (task->data_dir == DMA_NONE) {
		scu_stp_raw_request_construct_task_context(ireq);
		return SCI_SUCCESS;
	}

	/* NCQ */
	if (task->ata_task.use_ncq) {
		sci_stp_optimized_request_construct(ireq,
							 SCU_TASK_TYPE_FPDMAQ_READ,
							 len, dir);
		return SCI_SUCCESS;
	}

	/* DMA */
	if (task->ata_task.dma_xfer) {
		sci_stp_optimized_request_construct(ireq,
							 SCU_TASK_TYPE_DMA_IN,
							 len, dir);
		return SCI_SUCCESS;
	} else /* PIO */
		return sci_stp_pio_request_construct(ireq, copy);

	return status;
}

static enum sci_status sci_io_request_construct_basic_ssp(struct isci_request *ireq)
{
	struct sas_task *task = isci_request_access_task(ireq);

	ireq->protocol = SAS_PROTOCOL_SSP;

	scu_ssp_io_request_construct_task_context(ireq,
						  task->data_dir,
						  task->total_xfer_len);

	sci_io_request_build_ssp_command_iu(ireq);

	sci_change_state(&ireq->sm, SCI_REQ_CONSTRUCTED);

	return SCI_SUCCESS;
}

enum sci_status sci_task_request_construct_ssp(
	struct isci_request *ireq)
{
	/* Construct the SSP Task SCU Task Context */
	scu_ssp_task_request_construct_task_context(ireq);

	/* Fill in the SSP Task IU */
	sci_task_request_build_ssp_task_iu(ireq);

	sci_change_state(&ireq->sm, SCI_REQ_CONSTRUCTED);

	return SCI_SUCCESS;
}

static enum sci_status sci_io_request_construct_basic_sata(struct isci_request *ireq)
{
	enum sci_status status;
	bool copy = false;
	struct sas_task *task = isci_request_access_task(ireq);

	ireq->protocol = SAS_PROTOCOL_STP;

	copy = (task->data_dir == DMA_NONE) ? false : true;

	status = sci_io_request_construct_sata(ireq,
						task->total_xfer_len,
						task->data_dir,
						copy);

	if (status == SCI_SUCCESS)
		sci_change_state(&ireq->sm, SCI_REQ_CONSTRUCTED);

	return status;
}

/**
 * sci_req_tx_bytes - bytes transferred when reply underruns request
 * @ireq: request that was terminated early
 */
#define SCU_TASK_CONTEXT_SRAM 0x200000
static u32 sci_req_tx_bytes(struct isci_request *ireq)
{
	struct isci_host *ihost = ireq->owning_controller;
	u32 ret_val = 0;

	if (readl(&ihost->smu_registers->address_modifier) == 0) {
		void __iomem *scu_reg_base = ihost->scu_registers;

		/* get the bytes of data from the Address == BAR1 + 20002Ch + (256*TCi) where
		 *   BAR1 is the scu_registers
		 *   0x20002C = 0x200000 + 0x2c
		 *            = start of task context SRAM + offset of (type.ssp.data_offset)
		 *   TCi is the io_tag of struct sci_request
		 */
		ret_val = readl(scu_reg_base +
				(SCU_TASK_CONTEXT_SRAM + offsetof(struct scu_task_context, type.ssp.data_offset)) +
				((sizeof(struct scu_task_context)) * ISCI_TAG_TCI(ireq->io_tag)));
	}

	return ret_val;
}

enum sci_status sci_request_start(struct isci_request *ireq)
{
	enum sci_base_request_states state;
	struct scu_task_context *tc = ireq->tc;
	struct isci_host *ihost = ireq->owning_controller;

	state = ireq->sm.current_state_id;
	if (state != SCI_REQ_CONSTRUCTED) {
		dev_warn(&ihost->pdev->dev,
			"%s: SCIC IO Request requested to start while in wrong "
			 "state %d\n", __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	}

	tc->task_index = ISCI_TAG_TCI(ireq->io_tag);

	switch (tc->protocol_type) {
	case SCU_TASK_CONTEXT_PROTOCOL_SMP:
	case SCU_TASK_CONTEXT_PROTOCOL_SSP:
		/* SSP/SMP Frame */
		tc->type.ssp.tag = ireq->io_tag;
		tc->type.ssp.target_port_transfer_tag = 0xFFFF;
		break;

	case SCU_TASK_CONTEXT_PROTOCOL_STP:
		/* STP/SATA Frame
		 * tc->type.stp.ncq_tag = ireq->ncq_tag;
		 */
		break;

	case SCU_TASK_CONTEXT_PROTOCOL_NONE:
		/* / @todo When do we set no protocol type? */
		break;

	default:
		/* This should never happen since we build the IO
		 * requests */
		break;
	}

	/* Add to the post_context the io tag value */
	ireq->post_context |= ISCI_TAG_TCI(ireq->io_tag);

	/* Everything is good go ahead and change state */
	sci_change_state(&ireq->sm, SCI_REQ_STARTED);

	return SCI_SUCCESS;
}

enum sci_status
sci_io_request_terminate(struct isci_request *ireq)
{
	enum sci_base_request_states state;

	state = ireq->sm.current_state_id;

	switch (state) {
	case SCI_REQ_CONSTRUCTED:
		/* Set to make sure no HW terminate posting is done: */
		set_bit(IREQ_TC_ABORT_POSTED, &ireq->flags);
		ireq->scu_status = SCU_TASK_DONE_TASK_ABORT;
		ireq->sci_status = SCI_FAILURE_IO_TERMINATED;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		return SCI_SUCCESS;
	case SCI_REQ_STARTED:
	case SCI_REQ_TASK_WAIT_TC_COMP:
	case SCI_REQ_SMP_WAIT_RESP:
	case SCI_REQ_SMP_WAIT_TC_COMP:
	case SCI_REQ_STP_UDMA_WAIT_TC_COMP:
	case SCI_REQ_STP_UDMA_WAIT_D2H:
	case SCI_REQ_STP_NON_DATA_WAIT_H2D:
	case SCI_REQ_STP_NON_DATA_WAIT_D2H:
	case SCI_REQ_STP_PIO_WAIT_H2D:
	case SCI_REQ_STP_PIO_WAIT_FRAME:
	case SCI_REQ_STP_PIO_DATA_IN:
	case SCI_REQ_STP_PIO_DATA_OUT:
	case SCI_REQ_ATAPI_WAIT_H2D:
	case SCI_REQ_ATAPI_WAIT_PIO_SETUP:
	case SCI_REQ_ATAPI_WAIT_D2H:
	case SCI_REQ_ATAPI_WAIT_TC_COMP:
		/* Fall through and change state to ABORTING... */
	case SCI_REQ_TASK_WAIT_TC_RESP:
		/* The task frame was already confirmed to have been
		 * sent by the SCU HW.  Since the state machine is
		 * now only waiting for the task response itself,
		 * abort the request and complete it immediately
		 * and don't wait for the task response.
		 */
		sci_change_state(&ireq->sm, SCI_REQ_ABORTING);
		/* Fall through and handle like ABORTING... */
	case SCI_REQ_ABORTING:
		if (!isci_remote_device_is_safe_to_abort(ireq->target_device))
			set_bit(IREQ_PENDING_ABORT, &ireq->flags);
		else
			clear_bit(IREQ_PENDING_ABORT, &ireq->flags);
		/* If the request is only waiting on the remote device
		 * suspension, return SUCCESS so the caller will wait too.
		 */
		return SCI_SUCCESS;
	case SCI_REQ_COMPLETED:
	default:
		dev_warn(&ireq->owning_controller->pdev->dev,
			 "%s: SCIC IO Request requested to abort while in wrong "
			 "state %d\n", __func__, ireq->sm.current_state_id);
		break;
	}

	return SCI_FAILURE_INVALID_STATE;
}

enum sci_status sci_request_complete(struct isci_request *ireq)
{
	enum sci_base_request_states state;
	struct isci_host *ihost = ireq->owning_controller;

	state = ireq->sm.current_state_id;
	if (WARN_ONCE(state != SCI_REQ_COMPLETED,
		      "isci: request completion from wrong state (%s)\n",
		      req_state_name(state)))
		return SCI_FAILURE_INVALID_STATE;

	if (ireq->saved_rx_frame_index != SCU_INVALID_FRAME_INDEX)
		sci_controller_release_frame(ihost,
						  ireq->saved_rx_frame_index);

	/* XXX can we just stop the machine and remove the 'final' state? */
	sci_change_state(&ireq->sm, SCI_REQ_FINAL);
	return SCI_SUCCESS;
}

enum sci_status sci_io_request_event_handler(struct isci_request *ireq,
						  u32 event_code)
{
	enum sci_base_request_states state;
	struct isci_host *ihost = ireq->owning_controller;

	state = ireq->sm.current_state_id;

	if (state != SCI_REQ_STP_PIO_DATA_IN) {
		dev_warn(&ihost->pdev->dev, "%s: (%x) in wrong state %s\n",
			 __func__, event_code, req_state_name(state));

		return SCI_FAILURE_INVALID_STATE;
	}

	switch (scu_get_event_specifier(event_code)) {
	case SCU_TASK_DONE_CRC_ERR << SCU_EVENT_SPECIFIC_CODE_SHIFT:
		/* We are waiting for data and the SCU has R_ERR the data frame.
		 * Go back to waiting for the D2H Register FIS
		 */
		sci_change_state(&ireq->sm, SCI_REQ_STP_PIO_WAIT_FRAME);
		return SCI_SUCCESS;
	default:
		dev_err(&ihost->pdev->dev,
			"%s: pio request unexpected event %#x\n",
			__func__, event_code);

		/* TODO Should we fail the PIO request when we get an
		 * unexpected event?
		 */
		return SCI_FAILURE;
	}
}

/*
 * This function copies response data for requests returning response data
 *    instead of sense data.
 * @sci_req: This parameter specifies the request object for which to copy
 *    the response data.
 */
static void sci_io_request_copy_response(struct isci_request *ireq)
{
	void *resp_buf;
	u32 len;
	struct ssp_response_iu *ssp_response;
	struct isci_tmf *isci_tmf = isci_request_access_tmf(ireq);

	ssp_response = &ireq->ssp.rsp;

	resp_buf = &isci_tmf->resp.resp_iu;

	len = min_t(u32,
		    SSP_RESP_IU_MAX_SIZE,
		    be32_to_cpu(ssp_response->response_data_len));

	memcpy(resp_buf, ssp_response->resp_data, len);
}

static enum sci_status
request_started_state_tc_event(struct isci_request *ireq,
			       u32 completion_code)
{
	struct ssp_response_iu *resp_iu;
	u8 datapres;

	/* TODO: Any SDMA return code of other than 0 is bad decode 0x003C0000
	 * to determine SDMA status
	 */
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS;
		break;
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_EARLY_RESP): {
		/* There are times when the SCU hardware will return an early
		 * response because the io request specified more data than is
		 * returned by the target device (mode pages, inquiry data,
		 * etc.).  We must check the response stats to see if this is
		 * truly a failed request or a good request that just got
		 * completed early.
		 */
		struct ssp_response_iu *resp = &ireq->ssp.rsp;
		ssize_t word_cnt = SSP_RESP_IU_MAX_SIZE / sizeof(u32);

		sci_swab32_cpy(&ireq->ssp.rsp,
			       &ireq->ssp.rsp,
			       word_cnt);

		if (resp->status == 0) {
			ireq->scu_status = SCU_TASK_DONE_GOOD;
			ireq->sci_status = SCI_SUCCESS_IO_DONE_EARLY;
		} else {
			ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
			ireq->sci_status = SCI_FAILURE_IO_RESPONSE_VALID;
		}
		break;
	}
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_CHECK_RESPONSE): {
		ssize_t word_cnt = SSP_RESP_IU_MAX_SIZE / sizeof(u32);

		sci_swab32_cpy(&ireq->ssp.rsp,
			       &ireq->ssp.rsp,
			       word_cnt);

		ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
		ireq->sci_status = SCI_FAILURE_IO_RESPONSE_VALID;
		break;
	}

	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_RESP_LEN_ERR):
		/* TODO With TASK_DONE_RESP_LEN_ERR is the response frame
		 * guaranteed to be received before this completion status is
		 * posted?
		 */
		resp_iu = &ireq->ssp.rsp;
		datapres = resp_iu->datapres;

		if (datapres == 1 || datapres == 2) {
			ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
			ireq->sci_status = SCI_FAILURE_IO_RESPONSE_VALID;
		} else {
			ireq->scu_status = SCU_TASK_DONE_GOOD;
			ireq->sci_status = SCI_SUCCESS;
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
		if (ireq->protocol == SAS_PROTOCOL_STP) {
			ireq->scu_status = SCU_GET_COMPLETION_TL_STATUS(completion_code) >>
					   SCU_COMPLETION_TL_STATUS_SHIFT;
			ireq->sci_status = SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED;
		} else {
			ireq->scu_status = SCU_GET_COMPLETION_TL_STATUS(completion_code) >>
					   SCU_COMPLETION_TL_STATUS_SHIFT;
			ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
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
		ireq->scu_status = SCU_GET_COMPLETION_TL_STATUS(completion_code) >>
				   SCU_COMPLETION_TL_STATUS_SHIFT;
		ireq->sci_status = SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED;
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
		ireq->scu_status = SCU_GET_COMPLETION_TL_STATUS(completion_code) >>
				   SCU_COMPLETION_TL_STATUS_SHIFT;
		ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
		break;
	}

	/*
	 * TODO: This is probably wrong for ACK/NAK timeout conditions
	 */

	/* In all cases we will treat this as the completion of the IO req. */
	sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
	return SCI_SUCCESS;
}

static enum sci_status
request_aborting_state_tc_event(struct isci_request *ireq,
				u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case (SCU_TASK_DONE_GOOD << SCU_COMPLETION_TL_STATUS_SHIFT):
	case (SCU_TASK_DONE_TASK_ABORT << SCU_COMPLETION_TL_STATUS_SHIFT):
		ireq->scu_status = SCU_TASK_DONE_TASK_ABORT;
		ireq->sci_status = SCI_FAILURE_IO_TERMINATED;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;

	default:
		/* Unless we get some strange error wait for the task abort to complete
		 * TODO: Should there be a state change for this completion?
		 */
		break;
	}

	return SCI_SUCCESS;
}

static enum sci_status ssp_task_request_await_tc_event(struct isci_request *ireq,
						       u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS;
		sci_change_state(&ireq->sm, SCI_REQ_TASK_WAIT_TC_RESP);
		break;
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_ACK_NAK_TO):
		/* Currently, the decision is to simply allow the task request
		 * to timeout if the task IU wasn't received successfully.
		 * There is a potential for receiving multiple task responses if
		 * we decide to send the task IU again.
		 */
		dev_warn(&ireq->owning_controller->pdev->dev,
			 "%s: TaskRequest:0x%p CompletionCode:%x - "
			 "ACK/NAK timeout\n", __func__, ireq,
			 completion_code);

		sci_change_state(&ireq->sm, SCI_REQ_TASK_WAIT_TC_RESP);
		break;
	default:
		/*
		 * All other completion status cause the IO to be complete.
		 * If a NAK was received, then it is up to the user to retry
		 * the request.
		 */
		ireq->scu_status = SCU_NORMALIZE_COMPLETION_STATUS(completion_code);
		ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	}

	return SCI_SUCCESS;
}

static enum sci_status
smp_request_await_response_tc_event(struct isci_request *ireq,
				    u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		/* In the AWAIT RESPONSE state, any TC completion is
		 * unexpected.  but if the TC has success status, we
		 * complete the IO anyway.
		 */
		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_RESP_TO_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_UFI_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_FRM_TYPE_ERR):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_SMP_LL_RX_ERR):
		/* These status has been seen in a specific LSI
		 * expander, which sometimes is not able to send smp
		 * response within 2 ms. This causes our hardware break
		 * the connection and set TC completion with one of
		 * these SMP_XXX_XX_ERR status. For these type of error,
		 * we ask ihost user to retry the request.
		 */
		ireq->scu_status = SCU_TASK_DONE_SMP_RESP_TO_ERR;
		ireq->sci_status = SCI_FAILURE_RETRY_REQUIRED;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	default:
		/* All other completion status cause the IO to be complete.  If a NAK
		 * was received, then it is up to the user to retry the request
		 */
		ireq->scu_status = SCU_NORMALIZE_COMPLETION_STATUS(completion_code);
		ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	}

	return SCI_SUCCESS;
}

static enum sci_status
smp_request_await_tc_event(struct isci_request *ireq,
			   u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	default:
		/* All other completion status cause the IO to be
		 * complete.  If a NAK was received, then it is up to
		 * the user to retry the request.
		 */
		ireq->scu_status = SCU_NORMALIZE_COMPLETION_STATUS(completion_code);
		ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	}

	return SCI_SUCCESS;
}

static struct scu_sgl_element *pio_sgl_next(struct isci_stp_request *stp_req)
{
	struct scu_sgl_element *sgl;
	struct scu_sgl_element_pair *sgl_pair;
	struct isci_request *ireq = to_ireq(stp_req);
	struct isci_stp_pio_sgl *pio_sgl = &stp_req->sgl;

	sgl_pair = to_sgl_element_pair(ireq, pio_sgl->index);
	if (!sgl_pair)
		sgl = NULL;
	else if (pio_sgl->set == SCU_SGL_ELEMENT_PAIR_A) {
		if (sgl_pair->B.address_lower == 0 &&
		    sgl_pair->B.address_upper == 0) {
			sgl = NULL;
		} else {
			pio_sgl->set = SCU_SGL_ELEMENT_PAIR_B;
			sgl = &sgl_pair->B;
		}
	} else {
		if (sgl_pair->next_pair_lower == 0 &&
		    sgl_pair->next_pair_upper == 0) {
			sgl = NULL;
		} else {
			pio_sgl->index++;
			pio_sgl->set = SCU_SGL_ELEMENT_PAIR_A;
			sgl_pair = to_sgl_element_pair(ireq, pio_sgl->index);
			sgl = &sgl_pair->A;
		}
	}

	return sgl;
}

static enum sci_status
stp_request_non_data_await_h2d_tc_event(struct isci_request *ireq,
					u32 completion_code)
{
	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS;
		sci_change_state(&ireq->sm, SCI_REQ_STP_NON_DATA_WAIT_D2H);
		break;

	default:
		/* All other completion status cause the IO to be
		 * complete.  If a NAK was received, then it is up to
		 * the user to retry the request.
		 */
		ireq->scu_status = SCU_NORMALIZE_COMPLETION_STATUS(completion_code);
		ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	}

	return SCI_SUCCESS;
}

#define SCU_MAX_FRAME_BUFFER_SIZE  0x400  /* 1K is the maximum SCU frame data payload */

/* transmit DATA_FIS from (current sgl + offset) for input
 * parameter length. current sgl and offset is alreay stored in the IO request
 */
static enum sci_status sci_stp_request_pio_data_out_trasmit_data_frame(
	struct isci_request *ireq,
	u32 length)
{
	struct isci_stp_request *stp_req = &ireq->stp.req;
	struct scu_task_context *task_context = ireq->tc;
	struct scu_sgl_element_pair *sgl_pair;
	struct scu_sgl_element *current_sgl;

	/* Recycle the TC and reconstruct it for sending out DATA FIS containing
	 * for the data from current_sgl+offset for the input length
	 */
	sgl_pair = to_sgl_element_pair(ireq, stp_req->sgl.index);
	if (stp_req->sgl.set == SCU_SGL_ELEMENT_PAIR_A)
		current_sgl = &sgl_pair->A;
	else
		current_sgl = &sgl_pair->B;

	/* update the TC */
	task_context->command_iu_upper = current_sgl->address_upper;
	task_context->command_iu_lower = current_sgl->address_lower;
	task_context->transfer_length_bytes = length;
	task_context->type.stp.fis_type = FIS_DATA;

	/* send the new TC out. */
	return sci_controller_continue_io(ireq);
}

static enum sci_status sci_stp_request_pio_data_out_transmit_data(struct isci_request *ireq)
{
	struct isci_stp_request *stp_req = &ireq->stp.req;
	struct scu_sgl_element_pair *sgl_pair;
	enum sci_status status = SCI_SUCCESS;
	struct scu_sgl_element *sgl;
	u32 offset;
	u32 len = 0;

	offset = stp_req->sgl.offset;
	sgl_pair = to_sgl_element_pair(ireq, stp_req->sgl.index);
	if (WARN_ONCE(!sgl_pair, "%s: null sgl element", __func__))
		return SCI_FAILURE;

	if (stp_req->sgl.set == SCU_SGL_ELEMENT_PAIR_A) {
		sgl = &sgl_pair->A;
		len = sgl_pair->A.length - offset;
	} else {
		sgl = &sgl_pair->B;
		len = sgl_pair->B.length - offset;
	}

	if (stp_req->pio_len == 0)
		return SCI_SUCCESS;

	if (stp_req->pio_len >= len) {
		status = sci_stp_request_pio_data_out_trasmit_data_frame(ireq, len);
		if (status != SCI_SUCCESS)
			return status;
		stp_req->pio_len -= len;

		/* update the current sgl, offset and save for future */
		sgl = pio_sgl_next(stp_req);
		offset = 0;
	} else if (stp_req->pio_len < len) {
		sci_stp_request_pio_data_out_trasmit_data_frame(ireq, stp_req->pio_len);

		/* Sgl offset will be adjusted and saved for future */
		offset += stp_req->pio_len;
		sgl->address_lower += stp_req->pio_len;
		stp_req->pio_len = 0;
	}

	stp_req->sgl.offset = offset;

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
sci_stp_request_pio_data_in_copy_data_buffer(struct isci_stp_request *stp_req,
					     u8 *data_buf, u32 len)
{
	struct isci_request *ireq;
	u8 *src_addr;
	int copy_len;
	struct sas_task *task;
	struct scatterlist *sg;
	void *kaddr;
	int total_len = len;

	ireq = to_ireq(stp_req);
	task = isci_request_access_task(ireq);
	src_addr = data_buf;

	if (task->num_scatter > 0) {
		sg = task->scatter;

		while (total_len > 0) {
			struct page *page = sg_page(sg);

			copy_len = min_t(int, total_len, sg_dma_len(sg));
			kaddr = kmap_atomic(page);
			memcpy(kaddr + sg->offset, src_addr, copy_len);
			kunmap_atomic(kaddr);
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
static enum sci_status sci_stp_request_pio_data_in_copy_data(
	struct isci_stp_request *stp_req,
	u8 *data_buffer)
{
	enum sci_status status;

	/*
	 * If there is less than 1K remaining in the transfer request
	 * copy just the data for the transfer */
	if (stp_req->pio_len < SCU_MAX_FRAME_BUFFER_SIZE) {
		status = sci_stp_request_pio_data_in_copy_data_buffer(
			stp_req, data_buffer, stp_req->pio_len);

		if (status == SCI_SUCCESS)
			stp_req->pio_len = 0;
	} else {
		/* We are transfering the whole frame so copy */
		status = sci_stp_request_pio_data_in_copy_data_buffer(
			stp_req, data_buffer, SCU_MAX_FRAME_BUFFER_SIZE);

		if (status == SCI_SUCCESS)
			stp_req->pio_len -= SCU_MAX_FRAME_BUFFER_SIZE;
	}

	return status;
}

static enum sci_status
stp_request_pio_await_h2d_completion_tc_event(struct isci_request *ireq,
					      u32 completion_code)
{
	enum sci_status status = SCI_SUCCESS;

	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS;
		sci_change_state(&ireq->sm, SCI_REQ_STP_PIO_WAIT_FRAME);
		break;

	default:
		/* All other completion status cause the IO to be
		 * complete.  If a NAK was received, then it is up to
		 * the user to retry the request.
		 */
		ireq->scu_status = SCU_NORMALIZE_COMPLETION_STATUS(completion_code);
		ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	}

	return status;
}

static enum sci_status
pio_data_out_tx_done_tc_event(struct isci_request *ireq,
			      u32 completion_code)
{
	enum sci_status status = SCI_SUCCESS;
	bool all_frames_transferred = false;
	struct isci_stp_request *stp_req = &ireq->stp.req;

	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		/* Transmit data */
		if (stp_req->pio_len != 0) {
			status = sci_stp_request_pio_data_out_transmit_data(ireq);
			if (status == SCI_SUCCESS) {
				if (stp_req->pio_len == 0)
					all_frames_transferred = true;
			}
		} else if (stp_req->pio_len == 0) {
			/*
			 * this will happen if the all data is written at the
			 * first time after the pio setup fis is received
			 */
			all_frames_transferred  = true;
		}

		/* all data transferred. */
		if (all_frames_transferred) {
			/*
			 * Change the state to SCI_REQ_STP_PIO_DATA_IN
			 * and wait for PIO_SETUP fis / or D2H REg fis. */
			sci_change_state(&ireq->sm, SCI_REQ_STP_PIO_WAIT_FRAME);
		}
		break;

	default:
		/*
		 * All other completion status cause the IO to be complete.
		 * If a NAK was received, then it is up to the user to retry
		 * the request.
		 */
		ireq->scu_status = SCU_NORMALIZE_COMPLETION_STATUS(completion_code);
		ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	}

	return status;
}

static enum sci_status sci_stp_request_udma_general_frame_handler(struct isci_request *ireq,
								       u32 frame_index)
{
	struct isci_host *ihost = ireq->owning_controller;
	struct dev_to_host_fis *frame_header;
	enum sci_status status;
	u32 *frame_buffer;

	status = sci_unsolicited_frame_control_get_header(&ihost->uf_control,
							       frame_index,
							       (void **)&frame_header);

	if ((status == SCI_SUCCESS) &&
	    (frame_header->fis_type == FIS_REGD2H)) {
		sci_unsolicited_frame_control_get_buffer(&ihost->uf_control,
							      frame_index,
							      (void **)&frame_buffer);

		sci_controller_copy_sata_response(&ireq->stp.rsp,
						       frame_header,
						       frame_buffer);
	}

	sci_controller_release_frame(ihost, frame_index);

	return status;
}

static enum sci_status process_unsolicited_fis(struct isci_request *ireq,
					       u32 frame_index)
{
	struct isci_host *ihost = ireq->owning_controller;
	enum sci_status status;
	struct dev_to_host_fis *frame_header;
	u32 *frame_buffer;

	status = sci_unsolicited_frame_control_get_header(&ihost->uf_control,
							  frame_index,
							  (void **)&frame_header);

	if (status != SCI_SUCCESS)
		return status;

	if (frame_header->fis_type != FIS_REGD2H) {
		dev_err(&ireq->isci_host->pdev->dev,
			"%s ERROR: invalid fis type 0x%X\n",
			__func__, frame_header->fis_type);
		return SCI_FAILURE;
	}

	sci_unsolicited_frame_control_get_buffer(&ihost->uf_control,
						 frame_index,
						 (void **)&frame_buffer);

	sci_controller_copy_sata_response(&ireq->stp.rsp,
					  (u32 *)frame_header,
					  frame_buffer);

	/* Frame has been decoded return it to the controller */
	sci_controller_release_frame(ihost, frame_index);

	return status;
}

static enum sci_status atapi_d2h_reg_frame_handler(struct isci_request *ireq,
						   u32 frame_index)
{
	struct sas_task *task = isci_request_access_task(ireq);
	enum sci_status status;

	status = process_unsolicited_fis(ireq, frame_index);

	if (status == SCI_SUCCESS) {
		if (ireq->stp.rsp.status & ATA_ERR)
			status = SCI_IO_FAILURE_RESPONSE_VALID;
	} else {
		status = SCI_IO_FAILURE_RESPONSE_VALID;
	}

	if (status != SCI_SUCCESS) {
		ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
		ireq->sci_status = status;
	} else {
		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS;
	}

	/* the d2h ufi is the end of non-data commands */
	if (task->data_dir == DMA_NONE)
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);

	return status;
}

static void scu_atapi_reconstruct_raw_frame_task_context(struct isci_request *ireq)
{
	struct ata_device *dev = sas_to_ata_dev(ireq->target_device->domain_dev);
	void *atapi_cdb = ireq->ttype_ptr.io_task_ptr->ata_task.atapi_packet;
	struct scu_task_context *task_context = ireq->tc;

	/* fill in the SCU Task Context for a DATA fis containing CDB in Raw Frame
	 * type. The TC for previous Packet fis was already there, we only need to
	 * change the H2D fis content.
	 */
	memset(&ireq->stp.cmd, 0, sizeof(struct host_to_dev_fis));
	memcpy(((u8 *)&ireq->stp.cmd + sizeof(u32)), atapi_cdb, ATAPI_CDB_LEN);
	memset(&(task_context->type.stp), 0, sizeof(struct stp_task_context));
	task_context->type.stp.fis_type = FIS_DATA;
	task_context->transfer_length_bytes = dev->cdb_len;
}

static void scu_atapi_construct_task_context(struct isci_request *ireq)
{
	struct ata_device *dev = sas_to_ata_dev(ireq->target_device->domain_dev);
	struct sas_task *task = isci_request_access_task(ireq);
	struct scu_task_context *task_context = ireq->tc;
	int cdb_len = dev->cdb_len;

	/* reference: SSTL 1.13.4.2
	 * task_type, sata_direction
	 */
	if (task->data_dir == DMA_TO_DEVICE) {
		task_context->task_type = SCU_TASK_TYPE_PACKET_DMA_OUT;
		task_context->sata_direction = 0;
	} else {
		/* todo: for NO_DATA command, we need to send out raw frame. */
		task_context->task_type = SCU_TASK_TYPE_PACKET_DMA_IN;
		task_context->sata_direction = 1;
	}

	memset(&task_context->type.stp, 0, sizeof(task_context->type.stp));
	task_context->type.stp.fis_type = FIS_DATA;

	memset(&ireq->stp.cmd, 0, sizeof(ireq->stp.cmd));
	memcpy(&ireq->stp.cmd.lbal, task->ata_task.atapi_packet, cdb_len);
	task_context->ssp_command_iu_length = cdb_len / sizeof(u32);

	/* task phase is set to TX_CMD */
	task_context->task_phase = 0x1;

	/* retry counter */
	task_context->stp_retry_count = 0;

	/* data transfer size. */
	task_context->transfer_length_bytes = task->total_xfer_len;

	/* setup sgl */
	sci_request_build_sgl(ireq);
}

enum sci_status
sci_io_request_frame_handler(struct isci_request *ireq,
				  u32 frame_index)
{
	struct isci_host *ihost = ireq->owning_controller;
	struct isci_stp_request *stp_req = &ireq->stp.req;
	enum sci_base_request_states state;
	enum sci_status status;
	ssize_t word_cnt;

	state = ireq->sm.current_state_id;
	switch (state)  {
	case SCI_REQ_STARTED: {
		struct ssp_frame_hdr ssp_hdr;
		void *frame_header;

		sci_unsolicited_frame_control_get_header(&ihost->uf_control,
							      frame_index,
							      &frame_header);

		word_cnt = sizeof(struct ssp_frame_hdr) / sizeof(u32);
		sci_swab32_cpy(&ssp_hdr, frame_header, word_cnt);

		if (ssp_hdr.frame_type == SSP_RESPONSE) {
			struct ssp_response_iu *resp_iu;
			ssize_t word_cnt = SSP_RESP_IU_MAX_SIZE / sizeof(u32);

			sci_unsolicited_frame_control_get_buffer(&ihost->uf_control,
								      frame_index,
								      (void **)&resp_iu);

			sci_swab32_cpy(&ireq->ssp.rsp, resp_iu, word_cnt);

			resp_iu = &ireq->ssp.rsp;

			if (resp_iu->datapres == 0x01 ||
			    resp_iu->datapres == 0x02) {
				ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
				ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
			} else {
				ireq->scu_status = SCU_TASK_DONE_GOOD;
				ireq->sci_status = SCI_SUCCESS;
			}
		} else {
			/* not a response frame, why did it get forwarded? */
			dev_err(&ihost->pdev->dev,
				"%s: SCIC IO Request 0x%p received unexpected "
				"frame %d type 0x%02x\n", __func__, ireq,
				frame_index, ssp_hdr.frame_type);
		}

		/*
		 * In any case we are done with this frame buffer return it to
		 * the controller
		 */
		sci_controller_release_frame(ihost, frame_index);

		return SCI_SUCCESS;
	}

	case SCI_REQ_TASK_WAIT_TC_RESP:
		sci_io_request_copy_response(ireq);
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		sci_controller_release_frame(ihost, frame_index);
		return SCI_SUCCESS;

	case SCI_REQ_SMP_WAIT_RESP: {
		struct sas_task *task = isci_request_access_task(ireq);
		struct scatterlist *sg = &task->smp_task.smp_resp;
		void *frame_header, *kaddr;
		u8 *rsp;

		sci_unsolicited_frame_control_get_header(&ihost->uf_control,
							 frame_index,
							 &frame_header);
		kaddr = kmap_atomic(sg_page(sg));
		rsp = kaddr + sg->offset;
		sci_swab32_cpy(rsp, frame_header, 1);

		if (rsp[0] == SMP_RESPONSE) {
			void *smp_resp;

			sci_unsolicited_frame_control_get_buffer(&ihost->uf_control,
								 frame_index,
								 &smp_resp);

			word_cnt = (sg->length/4)-1;
			if (word_cnt > 0)
				word_cnt = min_t(unsigned int, word_cnt,
						 SCU_UNSOLICITED_FRAME_BUFFER_SIZE/4);
			sci_swab32_cpy(rsp + 4, smp_resp, word_cnt);

			ireq->scu_status = SCU_TASK_DONE_GOOD;
			ireq->sci_status = SCI_SUCCESS;
			sci_change_state(&ireq->sm, SCI_REQ_SMP_WAIT_TC_COMP);
		} else {
			/*
			 * This was not a response frame why did it get
			 * forwarded?
			 */
			dev_err(&ihost->pdev->dev,
				"%s: SCIC SMP Request 0x%p received unexpected "
				"frame %d type 0x%02x\n",
				__func__,
				ireq,
				frame_index,
				rsp[0]);

			ireq->scu_status = SCU_TASK_DONE_SMP_FRM_TYPE_ERR;
			ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
			sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		}
		kunmap_atomic(kaddr);

		sci_controller_release_frame(ihost, frame_index);

		return SCI_SUCCESS;
	}

	case SCI_REQ_STP_UDMA_WAIT_TC_COMP:
		return sci_stp_request_udma_general_frame_handler(ireq,
								       frame_index);

	case SCI_REQ_STP_UDMA_WAIT_D2H:
		/* Use the general frame handler to copy the resposne data */
		status = sci_stp_request_udma_general_frame_handler(ireq, frame_index);

		if (status != SCI_SUCCESS)
			return status;

		ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
		ireq->sci_status = SCI_FAILURE_IO_RESPONSE_VALID;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		return SCI_SUCCESS;

	case SCI_REQ_STP_NON_DATA_WAIT_D2H: {
		struct dev_to_host_fis *frame_header;
		u32 *frame_buffer;

		status = sci_unsolicited_frame_control_get_header(&ihost->uf_control,
								       frame_index,
								       (void **)&frame_header);

		if (status != SCI_SUCCESS) {
			dev_err(&ihost->pdev->dev,
				"%s: SCIC IO Request 0x%p could not get frame "
				"header for frame index %d, status %x\n",
				__func__,
				stp_req,
				frame_index,
				status);

			return status;
		}

		switch (frame_header->fis_type) {
		case FIS_REGD2H:
			sci_unsolicited_frame_control_get_buffer(&ihost->uf_control,
								      frame_index,
								      (void **)&frame_buffer);

			sci_controller_copy_sata_response(&ireq->stp.rsp,
							       frame_header,
							       frame_buffer);

			/* The command has completed with error */
			ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
			ireq->sci_status = SCI_FAILURE_IO_RESPONSE_VALID;
			break;

		default:
			dev_warn(&ihost->pdev->dev,
				 "%s: IO Request:0x%p Frame Id:%d protocol "
				  "violation occurred\n", __func__, stp_req,
				  frame_index);

			ireq->scu_status = SCU_TASK_DONE_UNEXP_FIS;
			ireq->sci_status = SCI_FAILURE_PROTOCOL_VIOLATION;
			break;
		}

		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);

		/* Frame has been decoded return it to the controller */
		sci_controller_release_frame(ihost, frame_index);

		return status;
	}

	case SCI_REQ_STP_PIO_WAIT_FRAME: {
		struct sas_task *task = isci_request_access_task(ireq);
		struct dev_to_host_fis *frame_header;
		u32 *frame_buffer;

		status = sci_unsolicited_frame_control_get_header(&ihost->uf_control,
								       frame_index,
								       (void **)&frame_header);

		if (status != SCI_SUCCESS) {
			dev_err(&ihost->pdev->dev,
				"%s: SCIC IO Request 0x%p could not get frame "
				"header for frame index %d, status %x\n",
				__func__, stp_req, frame_index, status);
			return status;
		}

		switch (frame_header->fis_type) {
		case FIS_PIO_SETUP:
			/* Get from the frame buffer the PIO Setup Data */
			sci_unsolicited_frame_control_get_buffer(&ihost->uf_control,
								      frame_index,
								      (void **)&frame_buffer);

			/* Get the data from the PIO Setup The SCU Hardware
			 * returns first word in the frame_header and the rest
			 * of the data is in the frame buffer so we need to
			 * back up one dword
			 */

			/* transfer_count: first 16bits in the 4th dword */
			stp_req->pio_len = frame_buffer[3] & 0xffff;

			/* status: 4th byte in the 3rd dword */
			stp_req->status = (frame_buffer[2] >> 24) & 0xff;

			sci_controller_copy_sata_response(&ireq->stp.rsp,
							       frame_header,
							       frame_buffer);

			ireq->stp.rsp.status = stp_req->status;

			/* The next state is dependent on whether the
			 * request was PIO Data-in or Data out
			 */
			if (task->data_dir == DMA_FROM_DEVICE) {
				sci_change_state(&ireq->sm, SCI_REQ_STP_PIO_DATA_IN);
			} else if (task->data_dir == DMA_TO_DEVICE) {
				/* Transmit data */
				status = sci_stp_request_pio_data_out_transmit_data(ireq);
				if (status != SCI_SUCCESS)
					break;
				sci_change_state(&ireq->sm, SCI_REQ_STP_PIO_DATA_OUT);
			}
			break;

		case FIS_SETDEVBITS:
			sci_change_state(&ireq->sm, SCI_REQ_STP_PIO_WAIT_FRAME);
			break;

		case FIS_REGD2H:
			if (frame_header->status & ATA_BUSY) {
				/*
				 * Now why is the drive sending a D2H Register
				 * FIS when it is still busy?  Do nothing since
				 * we are still in the right state.
				 */
				dev_dbg(&ihost->pdev->dev,
					"%s: SCIC PIO Request 0x%p received "
					"D2H Register FIS with BSY status "
					"0x%x\n",
					__func__,
					stp_req,
					frame_header->status);
				break;
			}

			sci_unsolicited_frame_control_get_buffer(&ihost->uf_control,
								      frame_index,
								      (void **)&frame_buffer);

			sci_controller_copy_sata_response(&ireq->stp.req,
							       frame_header,
							       frame_buffer);

			ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
			ireq->sci_status = SCI_FAILURE_IO_RESPONSE_VALID;
			sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
			break;

		default:
			/* FIXME: what do we do here? */
			break;
		}

		/* Frame is decoded return it to the controller */
		sci_controller_release_frame(ihost, frame_index);

		return status;
	}

	case SCI_REQ_STP_PIO_DATA_IN: {
		struct dev_to_host_fis *frame_header;
		struct sata_fis_data *frame_buffer;

		status = sci_unsolicited_frame_control_get_header(&ihost->uf_control,
								       frame_index,
								       (void **)&frame_header);

		if (status != SCI_SUCCESS) {
			dev_err(&ihost->pdev->dev,
				"%s: SCIC IO Request 0x%p could not get frame "
				"header for frame index %d, status %x\n",
				__func__,
				stp_req,
				frame_index,
				status);
			return status;
		}

		if (frame_header->fis_type != FIS_DATA) {
			dev_err(&ihost->pdev->dev,
				"%s: SCIC PIO Request 0x%p received frame %d "
				"with fis type 0x%02x when expecting a data "
				"fis.\n",
				__func__,
				stp_req,
				frame_index,
				frame_header->fis_type);

			ireq->scu_status = SCU_TASK_DONE_GOOD;
			ireq->sci_status = SCI_FAILURE_IO_REQUIRES_SCSI_ABORT;
			sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);

			/* Frame is decoded return it to the controller */
			sci_controller_release_frame(ihost, frame_index);
			return status;
		}

		if (stp_req->sgl.index < 0) {
			ireq->saved_rx_frame_index = frame_index;
			stp_req->pio_len = 0;
		} else {
			sci_unsolicited_frame_control_get_buffer(&ihost->uf_control,
								      frame_index,
								      (void **)&frame_buffer);

			status = sci_stp_request_pio_data_in_copy_data(stp_req,
									    (u8 *)frame_buffer);

			/* Frame is decoded return it to the controller */
			sci_controller_release_frame(ihost, frame_index);
		}

		/* Check for the end of the transfer, are there more
		 * bytes remaining for this data transfer
		 */
		if (status != SCI_SUCCESS || stp_req->pio_len != 0)
			return status;

		if ((stp_req->status & ATA_BUSY) == 0) {
			ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
			ireq->sci_status = SCI_FAILURE_IO_RESPONSE_VALID;
			sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		} else {
			sci_change_state(&ireq->sm, SCI_REQ_STP_PIO_WAIT_FRAME);
		}
		return status;
	}

	case SCI_REQ_ATAPI_WAIT_PIO_SETUP: {
		struct sas_task *task = isci_request_access_task(ireq);

		sci_controller_release_frame(ihost, frame_index);
		ireq->target_device->working_request = ireq;
		if (task->data_dir == DMA_NONE) {
			sci_change_state(&ireq->sm, SCI_REQ_ATAPI_WAIT_TC_COMP);
			scu_atapi_reconstruct_raw_frame_task_context(ireq);
		} else {
			sci_change_state(&ireq->sm, SCI_REQ_ATAPI_WAIT_D2H);
			scu_atapi_construct_task_context(ireq);
		}

		sci_controller_continue_io(ireq);
		return SCI_SUCCESS;
	}
	case SCI_REQ_ATAPI_WAIT_D2H:
		return atapi_d2h_reg_frame_handler(ireq, frame_index);
	case SCI_REQ_ABORTING:
		/*
		 * TODO: Is it even possible to get an unsolicited frame in the
		 * aborting state?
		 */
		sci_controller_release_frame(ihost, frame_index);
		return SCI_SUCCESS;

	default:
		dev_warn(&ihost->pdev->dev,
			 "%s: SCIC IO Request given unexpected frame %x while "
			 "in state %d\n",
			 __func__,
			 frame_index,
			 state);

		sci_controller_release_frame(ihost, frame_index);
		return SCI_FAILURE_INVALID_STATE;
	}
}

static enum sci_status stp_request_udma_await_tc_event(struct isci_request *ireq,
						       u32 completion_code)
{
	enum sci_status status = SCI_SUCCESS;

	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_UNEXP_FIS):
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_REG_ERR):
		/* We must check ther response buffer to see if the D2H
		 * Register FIS was received before we got the TC
		 * completion.
		 */
		if (ireq->stp.rsp.fis_type == FIS_REGD2H) {
			ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
			ireq->sci_status = SCI_FAILURE_IO_RESPONSE_VALID;
			sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		} else {
			/* If we have an error completion status for the
			 * TC then we can expect a D2H register FIS from
			 * the device so we must change state to wait
			 * for it
			 */
			sci_change_state(&ireq->sm, SCI_REQ_STP_UDMA_WAIT_D2H);
		}
		break;

	/* TODO Check to see if any of these completion status need to
	 * wait for the device to host register fis.
	 */
	/* TODO We can retry the command for SCU_TASK_DONE_CMD_LL_R_ERR
	 * - this comes only for B0
	 */
	default:
		/* All other completion status cause the IO to be complete. */
		ireq->scu_status = SCU_NORMALIZE_COMPLETION_STATUS(completion_code);
		ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	}

	return status;
}

static enum sci_status atapi_raw_completion(struct isci_request *ireq, u32 completion_code,
						  enum sci_base_request_states next)
{
	enum sci_status status = SCI_SUCCESS;

	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case SCU_MAKE_COMPLETION_STATUS(SCU_TASK_DONE_GOOD):
		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS;
		sci_change_state(&ireq->sm, next);
		break;
	default:
		/* All other completion status cause the IO to be complete.
		 * If a NAK was received, then it is up to the user to retry
		 * the request.
		 */
		ireq->scu_status = SCU_NORMALIZE_COMPLETION_STATUS(completion_code);
		ireq->sci_status = SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR;

		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;
	}

	return status;
}

static enum sci_status atapi_data_tc_completion_handler(struct isci_request *ireq,
							u32 completion_code)
{
	struct isci_remote_device *idev = ireq->target_device;
	struct dev_to_host_fis *d2h = &ireq->stp.rsp;
	enum sci_status status = SCI_SUCCESS;

	switch (SCU_GET_COMPLETION_TL_STATUS(completion_code)) {
	case (SCU_TASK_DONE_GOOD << SCU_COMPLETION_TL_STATUS_SHIFT):
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;

	case (SCU_TASK_DONE_UNEXP_FIS << SCU_COMPLETION_TL_STATUS_SHIFT): {
		u16 len = sci_req_tx_bytes(ireq);

		/* likely non-error data underrrun, workaround missing
		 * d2h frame from the controller
		 */
		if (d2h->fis_type != FIS_REGD2H) {
			d2h->fis_type = FIS_REGD2H;
			d2h->flags = (1 << 6);
			d2h->status = 0x50;
			d2h->error = 0;
			d2h->lbal = 0;
			d2h->byte_count_low = len & 0xff;
			d2h->byte_count_high = len >> 8;
			d2h->device = 0xa0;
			d2h->lbal_exp = 0;
			d2h->lbam_exp = 0;
			d2h->lbah_exp = 0;
			d2h->_r_a = 0;
			d2h->sector_count = 0x3;
			d2h->sector_count_exp = 0;
			d2h->_r_b = 0;
			d2h->_r_c = 0;
			d2h->_r_d = 0;
		}

		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS_IO_DONE_EARLY;
		status = ireq->sci_status;

		/* the hw will have suspended the rnc, so complete the
		 * request upon pending resume
		 */
		sci_change_state(&idev->sm, SCI_STP_DEV_ATAPI_ERROR);
		break;
	}
	case (SCU_TASK_DONE_EXCESS_DATA << SCU_COMPLETION_TL_STATUS_SHIFT):
		/* In this case, there is no UF coming after.
		 * compelte the IO now.
		 */
		ireq->scu_status = SCU_TASK_DONE_GOOD;
		ireq->sci_status = SCI_SUCCESS;
		sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
		break;

	default:
		if (d2h->fis_type == FIS_REGD2H) {
			/* UF received change the device state to ATAPI_ERROR */
			status = ireq->sci_status;
			sci_change_state(&idev->sm, SCI_STP_DEV_ATAPI_ERROR);
		} else {
			/* If receiving any non-sucess TC status, no UF
			 * received yet, then an UF for the status fis
			 * is coming after (XXX: suspect this is
			 * actually a protocol error or a bug like the
			 * DONE_UNEXP_FIS case)
			 */
			ireq->scu_status = SCU_TASK_DONE_CHECK_RESPONSE;
			ireq->sci_status = SCI_FAILURE_IO_RESPONSE_VALID;

			sci_change_state(&ireq->sm, SCI_REQ_ATAPI_WAIT_D2H);
		}
		break;
	}

	return status;
}

static int sci_request_smp_completion_status_is_tx_suspend(
	unsigned int completion_status)
{
	switch (completion_status) {
	case SCU_TASK_OPEN_REJECT_WRONG_DESTINATION:
	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_1:
	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_2:
	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_3:
	case SCU_TASK_OPEN_REJECT_BAD_DESTINATION:
	case SCU_TASK_OPEN_REJECT_ZONE_VIOLATION:
		return 1;
	}
	return 0;
}

static int sci_request_smp_completion_status_is_tx_rx_suspend(
	unsigned int completion_status)
{
	return 0; /* There are no Tx/Rx SMP suspend conditions. */
}

static int sci_request_ssp_completion_status_is_tx_suspend(
	unsigned int completion_status)
{
	switch (completion_status) {
	case SCU_TASK_DONE_TX_RAW_CMD_ERR:
	case SCU_TASK_DONE_LF_ERR:
	case SCU_TASK_OPEN_REJECT_WRONG_DESTINATION:
	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_1:
	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_2:
	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_3:
	case SCU_TASK_OPEN_REJECT_BAD_DESTINATION:
	case SCU_TASK_OPEN_REJECT_ZONE_VIOLATION:
	case SCU_TASK_OPEN_REJECT_STP_RESOURCES_BUSY:
	case SCU_TASK_OPEN_REJECT_PROTOCOL_NOT_SUPPORTED:
	case SCU_TASK_OPEN_REJECT_CONNECTION_RATE_NOT_SUPPORTED:
		return 1;
	}
	return 0;
}

static int sci_request_ssp_completion_status_is_tx_rx_suspend(
	unsigned int completion_status)
{
	return 0; /* There are no Tx/Rx SSP suspend conditions. */
}

static int sci_request_stpsata_completion_status_is_tx_suspend(
	unsigned int completion_status)
{
	switch (completion_status) {
	case SCU_TASK_DONE_TX_RAW_CMD_ERR:
	case SCU_TASK_DONE_LL_R_ERR:
	case SCU_TASK_DONE_LL_PERR:
	case SCU_TASK_DONE_REG_ERR:
	case SCU_TASK_DONE_SDB_ERR:
	case SCU_TASK_OPEN_REJECT_WRONG_DESTINATION:
	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_1:
	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_2:
	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_3:
	case SCU_TASK_OPEN_REJECT_BAD_DESTINATION:
	case SCU_TASK_OPEN_REJECT_ZONE_VIOLATION:
	case SCU_TASK_OPEN_REJECT_STP_RESOURCES_BUSY:
	case SCU_TASK_OPEN_REJECT_PROTOCOL_NOT_SUPPORTED:
	case SCU_TASK_OPEN_REJECT_CONNECTION_RATE_NOT_SUPPORTED:
		return 1;
	}
	return 0;
}


static int sci_request_stpsata_completion_status_is_tx_rx_suspend(
	unsigned int completion_status)
{
	switch (completion_status) {
	case SCU_TASK_DONE_LF_ERR:
	case SCU_TASK_DONE_LL_SY_TERM:
	case SCU_TASK_DONE_LL_LF_TERM:
	case SCU_TASK_DONE_BREAK_RCVD:
	case SCU_TASK_DONE_INV_FIS_LEN:
	case SCU_TASK_DONE_UNEXP_FIS:
	case SCU_TASK_DONE_UNEXP_SDBFIS:
	case SCU_TASK_DONE_MAX_PLD_ERR:
		return 1;
	}
	return 0;
}

static void sci_request_handle_suspending_completions(
	struct isci_request *ireq,
	u32 completion_code)
{
	int is_tx = 0;
	int is_tx_rx = 0;

	switch (ireq->protocol) {
	case SAS_PROTOCOL_SMP:
		is_tx = sci_request_smp_completion_status_is_tx_suspend(
			completion_code);
		is_tx_rx = sci_request_smp_completion_status_is_tx_rx_suspend(
			completion_code);
		break;
	case SAS_PROTOCOL_SSP:
		is_tx = sci_request_ssp_completion_status_is_tx_suspend(
			completion_code);
		is_tx_rx = sci_request_ssp_completion_status_is_tx_rx_suspend(
			completion_code);
		break;
	case SAS_PROTOCOL_STP:
		is_tx = sci_request_stpsata_completion_status_is_tx_suspend(
			completion_code);
		is_tx_rx =
			sci_request_stpsata_completion_status_is_tx_rx_suspend(
				completion_code);
		break;
	default:
		dev_warn(&ireq->isci_host->pdev->dev,
			 "%s: request %p has no valid protocol\n",
			 __func__, ireq);
		break;
	}
	if (is_tx || is_tx_rx) {
		BUG_ON(is_tx && is_tx_rx);

		sci_remote_node_context_suspend(
			&ireq->target_device->rnc,
			SCI_HW_SUSPEND,
			(is_tx_rx) ? SCU_EVENT_TL_RNC_SUSPEND_TX_RX
				   : SCU_EVENT_TL_RNC_SUSPEND_TX);
	}
}

enum sci_status
sci_io_request_tc_completion(struct isci_request *ireq,
			     u32 completion_code)
{
	enum sci_base_request_states state;
	struct isci_host *ihost = ireq->owning_controller;

	state = ireq->sm.current_state_id;

	/* Decode those completions that signal upcoming suspension events. */
	sci_request_handle_suspending_completions(
		ireq, SCU_GET_COMPLETION_TL_STATUS(completion_code));

	switch (state) {
	case SCI_REQ_STARTED:
		return request_started_state_tc_event(ireq, completion_code);

	case SCI_REQ_TASK_WAIT_TC_COMP:
		return ssp_task_request_await_tc_event(ireq,
						       completion_code);

	case SCI_REQ_SMP_WAIT_RESP:
		return smp_request_await_response_tc_event(ireq,
							   completion_code);

	case SCI_REQ_SMP_WAIT_TC_COMP:
		return smp_request_await_tc_event(ireq, completion_code);

	case SCI_REQ_STP_UDMA_WAIT_TC_COMP:
		return stp_request_udma_await_tc_event(ireq,
						       completion_code);

	case SCI_REQ_STP_NON_DATA_WAIT_H2D:
		return stp_request_non_data_await_h2d_tc_event(ireq,
							       completion_code);

	case SCI_REQ_STP_PIO_WAIT_H2D:
		return stp_request_pio_await_h2d_completion_tc_event(ireq,
								     completion_code);

	case SCI_REQ_STP_PIO_DATA_OUT:
		return pio_data_out_tx_done_tc_event(ireq, completion_code);

	case SCI_REQ_ABORTING:
		return request_aborting_state_tc_event(ireq,
						       completion_code);

	case SCI_REQ_ATAPI_WAIT_H2D:
		return atapi_raw_completion(ireq, completion_code,
					    SCI_REQ_ATAPI_WAIT_PIO_SETUP);

	case SCI_REQ_ATAPI_WAIT_TC_COMP:
		return atapi_raw_completion(ireq, completion_code,
					    SCI_REQ_ATAPI_WAIT_D2H);

	case SCI_REQ_ATAPI_WAIT_D2H:
		return atapi_data_tc_completion_handler(ireq, completion_code);

	default:
		dev_warn(&ihost->pdev->dev, "%s: %x in wrong state %s\n",
			 __func__, completion_code, req_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

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
	enum sas_open_rej_reason open_rej_reason)
{
	/* Task in the target is done. */
	set_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
	*response_ptr                     = SAS_TASK_UNDELIVERED;
	*status_ptr                       = SAS_OPEN_REJECT;
	task->task_status.open_rej_reason = open_rej_reason;
}

/**
 * isci_request_handle_controller_specific_errors() - This function decodes
 *    controller-specific I/O completion error conditions.
 * @request: This parameter is the completed isci_request object.
 * @response_ptr: This parameter specifies the service response for the I/O.
 * @status_ptr: This parameter specifies the exec status for the I/O.
 *
 * none.
 */
static void isci_request_handle_controller_specific_errors(
	struct isci_remote_device *idev,
	struct isci_request *request,
	struct sas_task *task,
	enum service_response *response_ptr,
	enum exec_status *status_ptr)
{
	unsigned int cstatus;

	cstatus = request->scu_status;

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
			if (!idev)
				*status_ptr = SAS_DEVICE_UNKNOWN;
			else
				*status_ptr = SAS_ABORTED_TASK;

			set_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
		} else {
			/* Task in the target is not done. */
			*response_ptr = SAS_TASK_UNDELIVERED;

			if (!idev)
				*status_ptr = SAS_DEVICE_UNKNOWN;
			else
				*status_ptr = SAM_STAT_TASK_ABORTED;

			clear_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
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
		if (!idev)
			*status_ptr = SAS_DEVICE_UNKNOWN;
		else
			*status_ptr = SAS_ABORTED_TASK;

		set_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
		break;


	/* Note that the only open reject completion codes seen here will be
	 * abandon-class codes; all others are automatically retried in the SCU.
	 */
	case SCU_TASK_OPEN_REJECT_WRONG_DESTINATION:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			SAS_OREJ_WRONG_DEST);
		break;

	case SCU_TASK_OPEN_REJECT_ZONE_VIOLATION:

		/* Note - the return of AB0 will change when
		 * libsas implements detection of zone violations.
		 */
		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			SAS_OREJ_RESV_AB0);
		break;

	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_1:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			SAS_OREJ_RESV_AB1);
		break;

	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_2:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			SAS_OREJ_RESV_AB2);
		break;

	case SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_3:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			SAS_OREJ_RESV_AB3);
		break;

	case SCU_TASK_OPEN_REJECT_BAD_DESTINATION:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			SAS_OREJ_BAD_DEST);
		break;

	case SCU_TASK_OPEN_REJECT_STP_RESOURCES_BUSY:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			SAS_OREJ_STP_NORES);
		break;

	case SCU_TASK_OPEN_REJECT_PROTOCOL_NOT_SUPPORTED:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			SAS_OREJ_EPROTO);
		break;

	case SCU_TASK_OPEN_REJECT_CONNECTION_RATE_NOT_SUPPORTED:

		isci_request_set_open_reject_status(
			request, task, response_ptr, status_ptr,
			SAS_OREJ_CONN_RATE);
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

		if (task->task_proto == SAS_PROTOCOL_SMP)
			set_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
		else
			clear_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
		break;
	}
}

static void isci_process_stp_response(struct sas_task *task, struct dev_to_host_fis *fis)
{
	struct task_status_struct *ts = &task->task_status;
	struct ata_task_resp *resp = (void *)&ts->buf[0];

	resp->frame_len = sizeof(*fis);
	memcpy(resp->ending_fis, fis, sizeof(*fis));
	ts->buf_valid_size = sizeof(*resp);

	/* If the device fault bit is set in the status register, then
	 * set the sense data and return.
	 */
	if (fis->status & ATA_DF)
		ts->stat = SAS_PROTO_RESPONSE;
	else if (fis->status & ATA_ERR)
		ts->stat = SAM_STAT_CHECK_CONDITION;
	else
		ts->stat = SAM_STAT_GOOD;

	ts->resp = SAS_TASK_COMPLETE;
}

static void isci_request_io_request_complete(struct isci_host *ihost,
					     struct isci_request *request,
					     enum sci_io_status completion_status)
{
	struct sas_task *task = isci_request_access_task(request);
	struct ssp_response_iu *resp_iu;
	unsigned long task_flags;
	struct isci_remote_device *idev = request->target_device;
	enum service_response response = SAS_TASK_UNDELIVERED;
	enum exec_status status = SAS_ABORTED_TASK;

	dev_dbg(&ihost->pdev->dev,
		"%s: request = %p, task = %p,\n"
		"task->data_dir = %d completion_status = 0x%x\n",
		__func__,
		request,
		task,
		task->data_dir,
		completion_status);

	/* The request is done from an SCU HW perspective. */

	/* This is an active request being completed from the core. */
	switch (completion_status) {

	case SCI_IO_FAILURE_RESPONSE_VALID:
		dev_dbg(&ihost->pdev->dev,
			"%s: SCI_IO_FAILURE_RESPONSE_VALID (%p/%p)\n",
			__func__, request, task);

		if (sas_protocol_ata(task->task_proto)) {
			isci_process_stp_response(task, &request->stp.rsp);
		} else if (SAS_PROTOCOL_SSP == task->task_proto) {

			/* crack the iu response buffer. */
			resp_iu = &request->ssp.rsp;
			isci_request_process_response_iu(task, resp_iu,
							 &ihost->pdev->dev);

		} else if (SAS_PROTOCOL_SMP == task->task_proto) {

			dev_err(&ihost->pdev->dev,
				"%s: SCI_IO_FAILURE_RESPONSE_VALID: "
					"SAS_PROTOCOL_SMP protocol\n",
				__func__);

		} else
			dev_err(&ihost->pdev->dev,
				"%s: unknown protocol\n", __func__);

		/* use the task status set in the task struct by the
		* isci_request_process_response_iu call.
		*/
		set_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
		response = task->task_status.resp;
		status = task->task_status.stat;
		break;

	case SCI_IO_SUCCESS:
	case SCI_IO_SUCCESS_IO_DONE_EARLY:

		response = SAS_TASK_COMPLETE;
		status   = SAM_STAT_GOOD;
		set_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);

		if (completion_status == SCI_IO_SUCCESS_IO_DONE_EARLY) {

			/* This was an SSP / STP / SATA transfer.
			* There is a possibility that less data than
			* the maximum was transferred.
			*/
			u32 transferred_length = sci_req_tx_bytes(request);

			task->task_status.residual
				= task->total_xfer_len - transferred_length;

			/* If there were residual bytes, call this an
			* underrun.
			*/
			if (task->task_status.residual != 0)
				status = SAS_DATA_UNDERRUN;

			dev_dbg(&ihost->pdev->dev,
				"%s: SCI_IO_SUCCESS_IO_DONE_EARLY %d\n",
				__func__, status);

		} else
			dev_dbg(&ihost->pdev->dev, "%s: SCI_IO_SUCCESS\n",
				__func__);
		break;

	case SCI_IO_FAILURE_TERMINATED:

		dev_dbg(&ihost->pdev->dev,
			"%s: SCI_IO_FAILURE_TERMINATED (%p/%p)\n",
			__func__, request, task);

		/* The request was terminated explicitly. */
		set_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
		response = SAS_TASK_UNDELIVERED;

		/* See if the device has been/is being stopped. Note
		* that we ignore the quiesce state, since we are
		* concerned about the actual device state.
		*/
		if (!idev)
			status = SAS_DEVICE_UNKNOWN;
		else
			status = SAS_ABORTED_TASK;
		break;

	case SCI_FAILURE_CONTROLLER_SPECIFIC_IO_ERR:

		isci_request_handle_controller_specific_errors(idev, request,
							       task, &response,
							       &status);
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

		clear_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
		break;

	case SCI_FAILURE_RETRY_REQUIRED:

		/* Fail the I/O so it can be retried. */
		response = SAS_TASK_UNDELIVERED;
		if (!idev)
			status = SAS_DEVICE_UNKNOWN;
		else
			status = SAS_ABORTED_TASK;

		set_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
		break;


	default:
		/* Catch any otherwise unhandled error codes here. */
		dev_dbg(&ihost->pdev->dev,
			"%s: invalid completion code: 0x%x - "
				"isci_request = %p\n",
			__func__, completion_status, request);

		response = SAS_TASK_UNDELIVERED;

		/* See if the device has been/is being stopped. Note
		* that we ignore the quiesce state, since we are
		* concerned about the actual device state.
		*/
		if (!idev)
			status = SAS_DEVICE_UNKNOWN;
		else
			status = SAS_ABORTED_TASK;

		if (SAS_PROTOCOL_SMP == task->task_proto)
			set_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
		else
			clear_bit(IREQ_COMPLETE_IN_TARGET, &request->flags);
		break;
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
		if (task->data_dir == DMA_NONE)
			break;
		if (task->num_scatter == 0)
			/* 0 indicates a single dma address */
			dma_unmap_single(&ihost->pdev->dev,
					 request->zero_scatter_daddr,
					 task->total_xfer_len, task->data_dir);
		else  /* unmap the sgl dma addresses */
			dma_unmap_sg(&ihost->pdev->dev, task->scatter,
				     request->num_sg_entries, task->data_dir);
		break;
	case SAS_PROTOCOL_SMP: {
		struct scatterlist *sg = &task->smp_task.smp_req;
		struct smp_req *smp_req;
		void *kaddr;

		dma_unmap_sg(&ihost->pdev->dev, sg, 1, DMA_TO_DEVICE);

		/* need to swab it back in case the command buffer is re-used */
		kaddr = kmap_atomic(sg_page(sg));
		smp_req = kaddr + sg->offset;
		sci_swab32_cpy(smp_req, smp_req, sg->length / sizeof(u32));
		kunmap_atomic(kaddr);
		break;
	}
	default:
		break;
	}

	spin_lock_irqsave(&task->task_state_lock, task_flags);

	task->task_status.resp = response;
	task->task_status.stat = status;

	if (test_bit(IREQ_COMPLETE_IN_TARGET, &request->flags)) {
		/* Normal notification (task_done) */
		task->task_state_flags |= SAS_TASK_STATE_DONE;
		task->task_state_flags &= ~(SAS_TASK_AT_INITIATOR |
					    SAS_TASK_STATE_PENDING);
	}
	spin_unlock_irqrestore(&task->task_state_lock, task_flags);

	/* Add to the completed list. */
	list_add(&request->completed_node, &ihost->requests_to_complete);

	/* complete the io request to the core. */
	sci_controller_complete_io(ihost, request->target_device, request);

	/* set terminated handle so it cannot be completed or
	 * terminated again, and to cause any calls into abort
	 * task to recognize the already completed case.
	 */
	set_bit(IREQ_TERMINATED, &request->flags);
}

static void sci_request_started_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_request *ireq = container_of(sm, typeof(*ireq), sm);
	struct domain_device *dev = ireq->target_device->domain_dev;
	enum sci_base_request_states state;
	struct sas_task *task;

	/* XXX as hch said always creating an internal sas_task for tmf
	 * requests would simplify the driver
	 */
	task = (test_bit(IREQ_TMF, &ireq->flags)) ? NULL : isci_request_access_task(ireq);

	/* all unaccelerated request types (non ssp or ncq) handled with
	 * substates
	 */
	if (!task && dev->dev_type == SAS_END_DEV) {
		state = SCI_REQ_TASK_WAIT_TC_COMP;
	} else if (task && task->task_proto == SAS_PROTOCOL_SMP) {
		state = SCI_REQ_SMP_WAIT_RESP;
	} else if (task && sas_protocol_ata(task->task_proto) &&
		   !task->ata_task.use_ncq) {
		if (dev->sata_dev.command_set == ATAPI_COMMAND_SET &&
			task->ata_task.fis.command == ATA_CMD_PACKET) {
			state = SCI_REQ_ATAPI_WAIT_H2D;
		} else if (task->data_dir == DMA_NONE) {
			state = SCI_REQ_STP_NON_DATA_WAIT_H2D;
		} else if (task->ata_task.dma_xfer) {
			state = SCI_REQ_STP_UDMA_WAIT_TC_COMP;
		} else /* PIO */ {
			state = SCI_REQ_STP_PIO_WAIT_H2D;
		}
	} else {
		/* SSP or NCQ are fully accelerated, no substates */
		return;
	}
	sci_change_state(sm, state);
}

static void sci_request_completed_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_request *ireq = container_of(sm, typeof(*ireq), sm);
	struct isci_host *ihost = ireq->owning_controller;

	/* Tell the SCI_USER that the IO request is complete */
	if (!test_bit(IREQ_TMF, &ireq->flags))
		isci_request_io_request_complete(ihost, ireq,
						 ireq->sci_status);
	else
		isci_task_request_complete(ihost, ireq, ireq->sci_status);
}

static void sci_request_aborting_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_request *ireq = container_of(sm, typeof(*ireq), sm);

	/* Setting the abort bit in the Task Context is required by the silicon. */
	ireq->tc->abort = 1;
}

static void sci_stp_request_started_non_data_await_h2d_completion_enter(struct sci_base_state_machine *sm)
{
	struct isci_request *ireq = container_of(sm, typeof(*ireq), sm);

	ireq->target_device->working_request = ireq;
}

static void sci_stp_request_started_pio_await_h2d_completion_enter(struct sci_base_state_machine *sm)
{
	struct isci_request *ireq = container_of(sm, typeof(*ireq), sm);

	ireq->target_device->working_request = ireq;
}

static const struct sci_base_state sci_request_state_table[] = {
	[SCI_REQ_INIT] = { },
	[SCI_REQ_CONSTRUCTED] = { },
	[SCI_REQ_STARTED] = {
		.enter_state = sci_request_started_state_enter,
	},
	[SCI_REQ_STP_NON_DATA_WAIT_H2D] = {
		.enter_state = sci_stp_request_started_non_data_await_h2d_completion_enter,
	},
	[SCI_REQ_STP_NON_DATA_WAIT_D2H] = { },
	[SCI_REQ_STP_PIO_WAIT_H2D] = {
		.enter_state = sci_stp_request_started_pio_await_h2d_completion_enter,
	},
	[SCI_REQ_STP_PIO_WAIT_FRAME] = { },
	[SCI_REQ_STP_PIO_DATA_IN] = { },
	[SCI_REQ_STP_PIO_DATA_OUT] = { },
	[SCI_REQ_STP_UDMA_WAIT_TC_COMP] = { },
	[SCI_REQ_STP_UDMA_WAIT_D2H] = { },
	[SCI_REQ_TASK_WAIT_TC_COMP] = { },
	[SCI_REQ_TASK_WAIT_TC_RESP] = { },
	[SCI_REQ_SMP_WAIT_RESP] = { },
	[SCI_REQ_SMP_WAIT_TC_COMP] = { },
	[SCI_REQ_ATAPI_WAIT_H2D] = { },
	[SCI_REQ_ATAPI_WAIT_PIO_SETUP] = { },
	[SCI_REQ_ATAPI_WAIT_D2H] = { },
	[SCI_REQ_ATAPI_WAIT_TC_COMP] = { },
	[SCI_REQ_COMPLETED] = {
		.enter_state = sci_request_completed_state_enter,
	},
	[SCI_REQ_ABORTING] = {
		.enter_state = sci_request_aborting_state_enter,
	},
	[SCI_REQ_FINAL] = { },
};

static void
sci_general_request_construct(struct isci_host *ihost,
				   struct isci_remote_device *idev,
				   struct isci_request *ireq)
{
	sci_init_sm(&ireq->sm, sci_request_state_table, SCI_REQ_INIT);

	ireq->target_device = idev;
	ireq->protocol = SAS_PROTOCOL_NONE;
	ireq->saved_rx_frame_index = SCU_INVALID_FRAME_INDEX;

	ireq->sci_status   = SCI_SUCCESS;
	ireq->scu_status   = 0;
	ireq->post_context = 0xFFFFFFFF;
}

static enum sci_status
sci_io_request_construct(struct isci_host *ihost,
			  struct isci_remote_device *idev,
			  struct isci_request *ireq)
{
	struct domain_device *dev = idev->domain_dev;
	enum sci_status status = SCI_SUCCESS;

	/* Build the common part of the request */
	sci_general_request_construct(ihost, idev, ireq);

	if (idev->rnc.remote_node_index == SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX)
		return SCI_FAILURE_INVALID_REMOTE_DEVICE;

	if (dev->dev_type == SAS_END_DEV)
		/* pass */;
	else if (dev_is_sata(dev))
		memset(&ireq->stp.cmd, 0, sizeof(ireq->stp.cmd));
	else if (dev_is_expander(dev))
		/* pass */;
	else
		return SCI_FAILURE_UNSUPPORTED_PROTOCOL;

	memset(ireq->tc, 0, offsetof(struct scu_task_context, sgl_pair_ab));

	return status;
}

enum sci_status sci_task_request_construct(struct isci_host *ihost,
					    struct isci_remote_device *idev,
					    u16 io_tag, struct isci_request *ireq)
{
	struct domain_device *dev = idev->domain_dev;
	enum sci_status status = SCI_SUCCESS;

	/* Build the common part of the request */
	sci_general_request_construct(ihost, idev, ireq);

	if (dev->dev_type == SAS_END_DEV || dev_is_sata(dev)) {
		set_bit(IREQ_TMF, &ireq->flags);
		memset(ireq->tc, 0, sizeof(struct scu_task_context));

		/* Set the protocol indicator. */
		if (dev_is_sata(dev))
			ireq->protocol = SAS_PROTOCOL_STP;
		else
			ireq->protocol = SAS_PROTOCOL_SSP;
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
	status = sci_io_request_construct_basic_ssp(request);
	return status;
}

static enum sci_status isci_request_stp_request_construct(struct isci_request *ireq)
{
	struct sas_task *task = isci_request_access_task(ireq);
	struct host_to_dev_fis *fis = &ireq->stp.cmd;
	struct ata_queued_cmd *qc = task->uldd_task;
	enum sci_status status;

	dev_dbg(&ireq->isci_host->pdev->dev,
		"%s: ireq = %p\n",
		__func__,
		ireq);

	memcpy(fis, &task->ata_task.fis, sizeof(struct host_to_dev_fis));
	if (!task->ata_task.device_control_reg_update)
		fis->flags |= 0x80;
	fis->flags &= 0xF0;

	status = sci_io_request_construct_basic_sata(ireq);

	if (qc && (qc->tf.command == ATA_CMD_FPDMA_WRITE ||
		   qc->tf.command == ATA_CMD_FPDMA_READ)) {
		fis->sector_count = qc->tag << 3;
		ireq->tc->type.stp.ncq_tag = qc->tag;
	}

	return status;
}

static enum sci_status
sci_io_request_construct_smp(struct device *dev,
			      struct isci_request *ireq,
			      struct sas_task *task)
{
	struct scatterlist *sg = &task->smp_task.smp_req;
	struct isci_remote_device *idev;
	struct scu_task_context *task_context;
	struct isci_port *iport;
	struct smp_req *smp_req;
	void *kaddr;
	u8 req_len;
	u32 cmd;

	kaddr = kmap_atomic(sg_page(sg));
	smp_req = kaddr + sg->offset;
	/*
	 * Look at the SMP requests' header fields; for certain SAS 1.x SMP
	 * functions under SAS 2.0, a zero request length really indicates
	 * a non-zero default length.
	 */
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
	req_len = smp_req->req_len;
	sci_swab32_cpy(smp_req, smp_req, sg->length / sizeof(u32));
	cmd = *(u32 *) smp_req;
	kunmap_atomic(kaddr);

	if (!dma_map_sg(dev, sg, 1, DMA_TO_DEVICE))
		return SCI_FAILURE;

	ireq->protocol = SAS_PROTOCOL_SMP;

	/* byte swap the smp request. */

	task_context = ireq->tc;

	idev = ireq->target_device;
	iport = idev->owning_port;

	/*
	 * Fill in the TC with the its required data
	 * 00h
	 */
	task_context->priority = 0;
	task_context->initiator_request = 1;
	task_context->connection_rate = idev->connection_rate;
	task_context->protocol_engine_index = ISCI_PEG;
	task_context->logical_port_index = iport->physical_port_index;
	task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_SMP;
	task_context->abort = 0;
	task_context->valid = SCU_TASK_CONTEXT_VALID;
	task_context->context_type = SCU_TASK_CONTEXT_TYPE;

	/* 04h */
	task_context->remote_node_index = idev->rnc.remote_node_index;
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
	task_context->ssp_command_iu_length = req_len;

	/* 14h */
	task_context->transfer_length_bytes = 0;

	/*
	 * 18h ~ 30h, protocol specific
	 * since commandIU has been build by framework at this point, we just
	 * copy the frist DWord from command IU to this location. */
	memcpy(&task_context->type.smp, &cmd, sizeof(u32));

	/*
	 * 40h
	 * "For SMP you could program it to zero. We would prefer that way
	 * so that done code will be consistent." - Venki
	 */
	task_context->task_phase = 0;

	ireq->post_context = (SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC |
			      (ISCI_PEG << SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) |
			       (iport->physical_port_index <<
				SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT) |
			      ISCI_TAG_TCI(ireq->io_tag));
	/*
	 * Copy the physical address for the command buffer to the SCU Task
	 * Context command buffer should not contain command header.
	 */
	task_context->command_iu_upper = upper_32_bits(sg_dma_address(sg));
	task_context->command_iu_lower = lower_32_bits(sg_dma_address(sg) + sizeof(u32));

	/* SMP response comes as UF, so no need to set response IU address. */
	task_context->response_iu_upper = 0;
	task_context->response_iu_lower = 0;

	sci_change_state(&ireq->sm, SCI_REQ_CONSTRUCTED);

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
	struct sas_task *task = isci_request_access_task(ireq);
	struct device *dev = &ireq->isci_host->pdev->dev;
	enum sci_status status = SCI_FAILURE;

	status = sci_io_request_construct_smp(dev, ireq, task);
	if (status != SCI_SUCCESS)
		dev_dbg(&ireq->isci_host->pdev->dev,
			 "%s: failed with status = %d\n",
			 __func__,
			 status);

	return status;
}

/**
 * isci_io_request_build() - This function builds the io request object.
 * @ihost: This parameter specifies the ISCI host object
 * @request: This parameter points to the isci_request object allocated in the
 *    request construct function.
 * @sci_device: This parameter is the handle for the sci core's remote device
 *    object that is the destination for this request.
 *
 * SCI_SUCCESS on successfull completion, or specific failure code.
 */
static enum sci_status isci_io_request_build(struct isci_host *ihost,
					     struct isci_request *request,
					     struct isci_remote_device *idev)
{
	enum sci_status status = SCI_SUCCESS;
	struct sas_task *task = isci_request_access_task(request);

	dev_dbg(&ihost->pdev->dev,
		"%s: idev = 0x%p; request = %p, "
		"num_scatter = %d\n",
		__func__,
		idev,
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
			&ihost->pdev->dev,
			task->scatter,
			task->num_scatter,
			task->data_dir
			);

		if (request->num_sg_entries == 0)
			return SCI_FAILURE_INSUFFICIENT_RESOURCES;
	}

	status = sci_io_request_construct(ihost, idev, request);

	if (status != SCI_SUCCESS) {
		dev_dbg(&ihost->pdev->dev,
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
		dev_dbg(&ihost->pdev->dev,
			 "%s: unknown protocol\n", __func__);
		return SCI_FAILURE;
	}

	return SCI_SUCCESS;
}

static struct isci_request *isci_request_from_tag(struct isci_host *ihost, u16 tag)
{
	struct isci_request *ireq;

	ireq = ihost->reqs[ISCI_TAG_TCI(tag)];
	ireq->io_tag = tag;
	ireq->io_request_completion = NULL;
	ireq->flags = 0;
	ireq->num_sg_entries = 0;
	INIT_LIST_HEAD(&ireq->completed_node);

	return ireq;
}

static struct isci_request *isci_io_request_from_tag(struct isci_host *ihost,
						     struct sas_task *task,
						     u16 tag)
{
	struct isci_request *ireq;

	ireq = isci_request_from_tag(ihost, tag);
	ireq->ttype_ptr.io_task_ptr = task;
	clear_bit(IREQ_TMF, &ireq->flags);
	task->lldd_task = ireq;

	return ireq;
}

struct isci_request *isci_tmf_request_from_tag(struct isci_host *ihost,
					       struct isci_tmf *isci_tmf,
					       u16 tag)
{
	struct isci_request *ireq;

	ireq = isci_request_from_tag(ihost, tag);
	ireq->ttype_ptr.tmf_task_ptr = isci_tmf;
	set_bit(IREQ_TMF, &ireq->flags);

	return ireq;
}

int isci_request_execute(struct isci_host *ihost, struct isci_remote_device *idev,
			 struct sas_task *task, u16 tag)
{
	enum sci_status status = SCI_FAILURE_UNSUPPORTED_PROTOCOL;
	struct isci_request *ireq;
	unsigned long flags;
	int ret = 0;

	/* do common allocation and init of request object. */
	ireq = isci_io_request_from_tag(ihost, task, tag);

	status = isci_io_request_build(ihost, ireq, idev);
	if (status != SCI_SUCCESS) {
		dev_dbg(&ihost->pdev->dev,
			 "%s: request_construct failed - status = 0x%x\n",
			 __func__,
			 status);
		return status;
	}

	spin_lock_irqsave(&ihost->scic_lock, flags);

	if (test_bit(IDEV_IO_NCQERROR, &idev->flags)) {

		if (isci_task_is_ncq_recovery(task)) {

			/* The device is in an NCQ recovery state.  Issue the
			 * request on the task side.  Note that it will
			 * complete on the I/O request side because the
			 * request was built that way (ie.
			 * ireq->is_task_management_request is false).
			 */
			status = sci_controller_start_task(ihost,
							    idev,
							    ireq);
		} else {
			status = SCI_FAILURE;
		}
	} else {
		/* send the request, let the core assign the IO TAG.	*/
		status = sci_controller_start_io(ihost, idev,
						  ireq);
	}

	if (status != SCI_SUCCESS &&
	    status != SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED) {
		dev_dbg(&ihost->pdev->dev,
			 "%s: failed request start (0x%x)\n",
			 __func__, status);
		spin_unlock_irqrestore(&ihost->scic_lock, flags);
		return status;
	}
	/* Either I/O started OK, or the core has signaled that
	 * the device needs a target reset.
	 */
	if (status != SCI_SUCCESS) {
		/* The request did not really start in the
		 * hardware, so clear the request handle
		 * here so no terminations will be done.
		 */
		set_bit(IREQ_TERMINATED, &ireq->flags);
	}
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

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
		sas_task_abort(task);

		/* Change the status, since we are holding
		 * the I/O until it is managed by the SCSI
		 * error handler.
		 */
		status = SCI_SUCCESS;
	}

	return ret;
}
