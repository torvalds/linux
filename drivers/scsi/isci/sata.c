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
#include "scic_remote_device.h"
#include "scic_sds_remote_device.h"
#include "scic_io_request.h"
#include "scic_task_request.h"
#include "task.h"
#include "request.h"
#include "sata.h"
#include "intel_sat.h"
#include "intel_ata.h"

static u8 isci_sata_get_management_task_protocol(struct isci_tmf *tmf);


/**
 * isci_sata_task_to_fis_copy() - This function gets the host_to_dev_fis from
 *    the core and copies the fis from the task into it.
 * @task: This parameter is a pointer to the task struct from libsas.
 *
 * pointer to the host_to_dev_fis from the core request object.
 */
struct host_to_dev_fis *isci_sata_task_to_fis_copy(struct sas_task *task)
{
	struct isci_request *request = task->lldd_task;
	struct host_to_dev_fis *register_fis =
		scic_stp_io_request_get_h2d_reg_address(
			request->sci_request_handle
			);

	memcpy(
		(u8 *)register_fis,
		(u8 *)&task->ata_task.fis,
		sizeof(struct host_to_dev_fis)
		);

	if (!task->ata_task.device_control_reg_update)
		register_fis->flags |= 0x80;

	register_fis->flags &= 0xF0;

	return register_fis;
}

/**
 * isci_sata_is_task_ncq() - This function determines if the given stp task is
 *    a ncq request.
 * @task: This parameter is a pointer to the task struct from libsas.
 *
 * true if the task is ncq
 */
bool isci_sata_is_task_ncq(struct sas_task *task)
{
	struct ata_queued_cmd *qc = task->uldd_task;

	bool ret = (qc &&
		    (qc->tf.command == ATA_CMD_FPDMA_WRITE ||
		     qc->tf.command == ATA_CMD_FPDMA_READ));

	return ret;
}

/**
 * isci_sata_set_ncq_tag() - This function sets the ncq tag field in the
 *    host_to_dev_fis equal to the tag in the queue command in the task.
 * @task: This parameter is a pointer to the task struct from libsas.
 * @register_fis: This parameter is a pointer to the host_to_dev_fis from the
 *    core request object.
 *
 */
void isci_sata_set_ncq_tag(
	struct host_to_dev_fis *register_fis,
	struct sas_task *task)
{
	struct ata_queued_cmd *qc = task->uldd_task;
	struct isci_request *request = task->lldd_task;

	register_fis->sector_count = qc->tag << 3;
	scic_stp_io_request_set_ncq_tag(request->sci_request_handle, qc->tag);
}

/**
 * isci_request_process_stp_response() - This function sets the status and
 *    response, in the task struct, from the request object for the upper layer
 *    driver.
 * @sas_task: This parameter is the task struct from the upper layer driver.
 * @response_buffer: This parameter points to the response of the completed
 *    request.
 *
 * none.
 */
void isci_request_process_stp_response(
	struct sas_task *task,
	void *response_buffer)
{
	struct sata_fis_reg_d2h *d2h_reg_fis = (struct sata_fis_reg_d2h *)response_buffer;
	struct task_status_struct *ts = &task->task_status;
	struct ata_task_resp *resp = (void *)&ts->buf[0];

	resp->frame_len = le16_to_cpu(*(__le16 *)(response_buffer + 6));
	memcpy(&resp->ending_fis[0], response_buffer + 16, 24);
	ts->buf_valid_size = sizeof(*resp);

	/**
	 * If the device fault bit is set in the status register, then
	 * set the sense data and return.
	 */
	if (d2h_reg_fis->status & ATA_STATUS_REG_DEVICE_FAULT_BIT)
		ts->stat = SAS_PROTO_RESPONSE;
	else
		ts->stat = SAM_STAT_GOOD;

	ts->resp = SAS_TASK_COMPLETE;
}

/**
 * isci_sata_get_sat_protocol() - retrieve the sat protocol for the request
 * @isci_request: ata request
 *
 * Note: temporary implementation until expert mode removes the callback
 *
 */
u8 isci_sata_get_sat_protocol(struct isci_request *isci_request)
{
	struct sas_task *task;
	struct domain_device *dev;

	dev_dbg(&isci_request->isci_host->pdev->dev,
		"%s: isci_request = %p, ttype = %d\n",
		__func__, isci_request, isci_request->ttype);

	if (tmf_task == isci_request->ttype) {
		struct isci_tmf *tmf = isci_request_access_tmf(isci_request);

		return isci_sata_get_management_task_protocol(tmf);
	}

	task = isci_request_access_task(isci_request);
	dev = task->dev;

	if (!sas_protocol_ata(task->task_proto)) {
		WARN(1, "unhandled task protocol\n");
		return SAT_PROTOCOL_NON_DATA;
	}

	if (task->data_dir == DMA_NONE)
		return SAT_PROTOCOL_NON_DATA;

	/* the "_IN" protocol types are equivalent to their "_OUT"
	 * analogs as far as the core is concerned
	 */
	if (dev->sata_dev.command_set == ATAPI_COMMAND_SET) {
		if (task->ata_task.dma_xfer)
			return SAT_PROTOCOL_PACKET_DMA_DATA_IN;
		else
			return SAT_PROTOCOL_PACKET_PIO_DATA_IN;
	}

	if (task->ata_task.use_ncq)
		return SAT_PROTOCOL_FPDMA;

	if (task->ata_task.dma_xfer)
		return SAT_PROTOCOL_UDMA_DATA_IN;
	else
		return SAT_PROTOCOL_PIO_DATA_IN;
}

static u8 isci_sata_get_management_task_protocol(
	struct isci_tmf *tmf)
{
	u8 ret = 0;

	pr_warn("tmf = %p, func = %d\n", tmf, tmf->tmf_code);

	if ((tmf->tmf_code == isci_tmf_sata_srst_high) ||
	    (tmf->tmf_code == isci_tmf_sata_srst_low)) {
		pr_warn("%s: tmf->tmf_code == TMF_LU_RESET\n", __func__);
		ret = SAT_PROTOCOL_SOFT_RESET;
	}

	return ret;
}

enum sci_status isci_sata_management_task_request_build(
	struct isci_request *isci_request)
{
	struct isci_tmf *isci_tmf;
	enum sci_status status;

	if (tmf_task != isci_request->ttype)
		return SCI_FAILURE;

	isci_tmf = isci_request_access_tmf(isci_request);

	switch (isci_tmf->tmf_code) {

	case isci_tmf_sata_srst_high:
	case isci_tmf_sata_srst_low:
	{
		struct host_to_dev_fis *register_fis =
			scic_stp_io_request_get_h2d_reg_address(
				isci_request->sci_request_handle
				);

		memset(register_fis, 0, sizeof(*register_fis));

		register_fis->fis_type  =  0x27;
		register_fis->flags     &= ~0x80;
		register_fis->flags     &= 0xF0;
		if (isci_tmf->tmf_code == isci_tmf_sata_srst_high)
			register_fis->control |= ATA_SRST;
		else
			register_fis->control &= ~ATA_SRST;
		break;
	}
	/* other management commnd go here... */
	default:
		return SCI_FAILURE;
	}

	/* core builds the protocol specific request
	 *  based on the h2d fis.
	 */
	status = scic_task_request_construct_sata(
		isci_request->sci_request_handle
		);

	return status;
}

/**
 * isci_task_send_lu_reset_sata() - This function is called by of the SAS
 *    Domain Template functions. This is one of the Task Management functoins
 *    called by libsas, to reset the given SAS lun. Note the assumption that
 *    while this call is executing, no I/O will be sent by the host to the
 *    device.
 * @lun: This parameter specifies the lun to be reset.
 *
 * status, zero indicates success.
 */
int isci_task_send_lu_reset_sata(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device,
	u8 *lun)
{
	struct isci_tmf tmf;
	int ret = TMF_RESP_FUNC_FAILED;

	/* Send the soft reset to the target */
	#define ISCI_SRST_TIMEOUT_MS 25000 /* 25 second timeout. */
	isci_task_build_tmf(&tmf, isci_device, isci_tmf_sata_srst_high,
			    NULL, NULL
			    );

	ret = isci_task_execute_tmf(isci_host, &tmf, ISCI_SRST_TIMEOUT_MS);

	if (ret != TMF_RESP_FUNC_COMPLETE) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: Assert SRST failed (%p) = %x",
			 __func__,
			 isci_device,
			 ret);

		/* Return the failure so that the LUN reset is escalated
		 * to a target reset.
		 */
	}
	return ret;
}
