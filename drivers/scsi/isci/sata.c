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
#include "isci.h"
#include "remote_device.h"
#include "task.h"
#include "request.h"
#include "sata.h"

/**
 * isci_sata_task_to_fis_copy() - This function gets the host_to_dev_fis from
 *    the core and copies the fis from the task into it.
 * @task: This parameter is a pointer to the task struct from libsas.
 *
 * pointer to the host_to_dev_fis from the core request object.
 */
struct host_to_dev_fis *isci_sata_task_to_fis_copy(struct sas_task *task)
{
	struct isci_request *ireq = task->lldd_task;
	struct host_to_dev_fis *fis = &ireq->sci.stp.cmd;

	memcpy(fis, &task->ata_task.fis, sizeof(struct host_to_dev_fis));

	if (!task->ata_task.device_control_reg_update)
		fis->flags |= 0x80;

	fis->flags &= 0xF0;

	return fis;
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
	scic_stp_io_request_set_ncq_tag(&request->sci, qc->tag);
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
void isci_request_process_stp_response(struct sas_task *task,
				       void *response_buffer)
{
	struct dev_to_host_fis *d2h_reg_fis = response_buffer;
	struct task_status_struct *ts = &task->task_status;
	struct ata_task_resp *resp = (void *)&ts->buf[0];

	resp->frame_len = le16_to_cpu(*(__le16 *)(response_buffer + 6));
	memcpy(&resp->ending_fis[0], response_buffer + 16, 24);
	ts->buf_valid_size = sizeof(*resp);

	/**
	 * If the device fault bit is set in the status register, then
	 * set the sense data and return.
	 */
	if (d2h_reg_fis->status & ATA_DF)
		ts->stat = SAS_PROTO_RESPONSE;
	else
		ts->stat = SAM_STAT_GOOD;

	ts->resp = SAS_TASK_COMPLETE;
}

enum sci_status isci_sata_management_task_request_build(struct isci_request *ireq)
{
	struct scic_sds_request *sci_req = &ireq->sci;
	struct isci_tmf *isci_tmf;
	enum sci_status status;

	if (tmf_task != ireq->ttype)
		return SCI_FAILURE;

	isci_tmf = isci_request_access_tmf(ireq);

	switch (isci_tmf->tmf_code) {

	case isci_tmf_sata_srst_high:
	case isci_tmf_sata_srst_low: {
		struct host_to_dev_fis *fis = &sci_req->stp.cmd;

		memset(fis, 0, sizeof(*fis));

		fis->fis_type  =  0x27;
		fis->flags     &= ~0x80;
		fis->flags     &= 0xF0;
		if (isci_tmf->tmf_code == isci_tmf_sata_srst_high)
			fis->control |= ATA_SRST;
		else
			fis->control &= ~ATA_SRST;
		break;
	}
	/* other management commnd go here... */
	default:
		return SCI_FAILURE;
	}

	/* core builds the protocol specific request
	 *  based on the h2d fis.
	 */
	status = scic_task_request_construct_sata(&ireq->sci);

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
