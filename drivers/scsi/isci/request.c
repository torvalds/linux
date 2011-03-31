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
#include "scic_io_request.h"
#include "scic_task_request.h"
#include "scic_port.h"
#include "task.h"
#include "request.h"
#include "sata.h"
#include "scu_completion_codes.h"


static enum sci_status isci_request_ssp_request_construct(
	struct isci_request *request)
{
	enum sci_status status;

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: request = %p\n",
		__func__,
		request);
	status = scic_io_request_construct_basic_ssp(
		request->sci_request_handle
		);
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

	status = scic_io_request_construct_basic_sata(
		request->sci_request_handle
		);

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

/**
 * isci_smp_request_build() - This function builds the smp request object.
 * @isci_host: This parameter specifies the ISCI host object
 * @request: This parameter points to the isci_request object allocated in the
 *    request construct function.
 * @sci_device: This parameter is the handle for the sci core's remote device
 *    object that is the destination for this request.
 *
 * SCI_SUCCESS on successfull completion, or specific failure code.
 */
static enum sci_status isci_smp_request_build(
	struct isci_request *request)
{
	enum sci_status status = SCI_FAILURE;
	struct sas_task *task = isci_request_access_task(request);

	void *command_iu_address =
		scic_io_request_get_command_iu_address(
			request->sci_request_handle
			);

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: request = %p\n",
		__func__,
		request);
	dev_dbg(&request->isci_host->pdev->dev,
		"%s: smp_req len = %d\n",
		__func__,
		task->smp_task.smp_req.length);

	/* copy the smp_command to the address; */
	sg_copy_to_buffer(&task->smp_task.smp_req, 1,
			  (char *)command_iu_address,
			  sizeof(struct smp_request)
			  );

	status = scic_io_request_construct_smp(request->sci_request_handle);
	if (status != SCI_SUCCESS)
		dev_warn(&request->isci_host->pdev->dev,
			 "%s: scic_io_request_construct_smp failed with "
			 "status = %d\n",
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
	struct smp_discover_response_protocols dev_protocols;
	enum sci_status status = SCI_SUCCESS;
	struct sas_task *task = isci_request_access_task(request);
	struct scic_sds_remote_device *sci_device = to_sci_dev(isci_device);

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
	status = scic_io_request_construct(
		isci_host->core_controller,
		sci_device,
		SCI_CONTROLLER_INVALID_IO_TAG,
		request,
		request->sci_request_mem_ptr,
		(struct scic_sds_request **)&request->sci_request_handle
		);

	if (status != SCI_SUCCESS) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: failed request construct\n",
			 __func__);
		return SCI_FAILURE;
	}

	sci_object_set_association(request->sci_request_handle, request);

	/* Determine protocol and call the appropriate basic constructor */
	scic_remote_device_get_protocols(sci_device, &dev_protocols);
	if (dev_protocols.u.bits.attached_ssp_target)
		status = isci_request_ssp_request_construct(request);
	else if (dev_protocols.u.bits.attached_stp_target)
		status = isci_request_stp_request_construct(request);
	else if (dev_protocols.u.bits.attached_smp_target)
		status = isci_smp_request_build(request);
	else {
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
	request->sci_request_mem_ptr = ((u8 *)request) +
				       sizeof(struct isci_request);
	request->request_daddr = handle;
	request->isci_host = isci_host;
	request->isci_device = isci_device;
	request->io_request_completion = NULL;

	request->request_alloc_size = isci_host->dma_pool_alloc_size;
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
	sci_device = to_sci_dev(isci_device);

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
	if (status == SCI_SUCCESS) {

		spin_lock_irqsave(&isci_host->scic_lock, flags);

		/* send the request, let the core assign the IO TAG.	*/
		status = scic_controller_start_io(
			isci_host->core_controller,
			sci_device,
			request->sci_request_handle,
			SCI_CONTROLLER_INVALID_IO_TAG
			);

		if (status == SCI_SUCCESS ||
		    status == SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED) {

			/* Either I/O started OK, or the core has signaled that
			 * the device needs a target reset.
			 *
			 * In either case, hold onto the I/O for later.
			 *
			 * Update it's status and add it to the list in the
			 * remote device object.
			 */
			isci_request_change_state(request, started);
			list_add(&request->dev_node,
				 &isci_device->reqs_in_process);

			if (status == SCI_SUCCESS) {
				/* Save the tag for possible task mgmt later. */
				request->io_tag = scic_io_request_get_io_tag(
						     request->sci_request_handle);
			} else {
				/* The request did not really start in the
				 * hardware, so clear the request handle
				 * here so no terminations will be done.
				 */
				request->sci_request_handle = NULL;
			}

		} else
			dev_warn(&isci_host->pdev->dev,
				 "%s: failed request start (0x%x)\n",
				 __func__, status);

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

	} else
		dev_warn(&isci_host->pdev->dev,
			 "%s: request_construct failed - status = 0x%x\n",
			 __func__,
			 status);

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

	cstatus = scic_request_get_controller_status(
		request->sci_request_handle
		);

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

/**
 * isci_request_io_request_complete() - This function is called by the sci core
 *    when an io request completes.
 * @isci_host: This parameter specifies the ISCI host object
 * @request: This parameter is the completed isci_request object.
 * @completion_status: This parameter specifies the completion status from the
 *    sci core.
 *
 * none.
 */
void isci_request_io_request_complete(
	struct        isci_host *isci_host,
	struct        isci_request *request,
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
				resp_buf
					= scic_stp_io_request_get_d2h_reg_address(
					request->sci_request_handle
					);
				isci_request_process_stp_response(task,
								  resp_buf
								  );

			} else if (SAS_PROTOCOL_SSP == task->task_proto) {

				/* crack the iu response buffer. */
				resp_iu
					= scic_io_request_get_response_iu_address(
					request->sci_request_handle
					);

				isci_request_process_response_iu(task, resp_iu,
								 &isci_host->pdev->dev
								 );

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

				u8 *command_iu_address
					= scic_io_request_get_command_iu_address(
					request->sci_request_handle
					);

				dev_dbg(&isci_host->pdev->dev,
					"%s: SMP protocol completion\n",
					__func__);

				sg_copy_from_buffer(
					&task->smp_task.smp_resp, 1,
					command_iu_address
					+ sizeof(struct smp_request),
					sizeof(struct smp_resp)
					);
			} else if (completion_status
				   == SCI_IO_SUCCESS_IO_DONE_EARLY) {

				/* This was an SSP / STP / SATA transfer.
				 * There is a possibility that less data than
				 * the maximum was transferred.
				 */
				u32 transferred_length
					= scic_io_request_get_number_of_bytes_transferred(
					request->sci_request_handle);

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
	scic_controller_complete_io(
		isci_host->core_controller,
		to_sci_dev(isci_device),
		request->sci_request_handle
		);
	/* NULL the request handle so it cannot be completed or
	 * terminated again, and to cause any calls into abort
	 * task to recognize the already completed case.
	 */
	request->sci_request_handle = NULL;

	isci_host_can_dequeue(isci_host, 1);
}

/**
 * isci_request_io_request_get_transfer_length() - This function is called by
 *    the sci core to retrieve the transfer length for a given request.
 * @request: This parameter is the isci_request object.
 *
 * length of transfer for specified request.
 */
u32 isci_request_io_request_get_transfer_length(struct isci_request *request)
{
	struct sas_task *task = isci_request_access_task(request);

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: total_xfer_len: %d\n",
		__func__,
		task->total_xfer_len);
	return task->total_xfer_len;
}


/**
 * isci_request_io_request_get_data_direction() - This function is called by
 *    the sci core to retrieve the data direction for a given request.
 * @request: This parameter is the isci_request object.
 *
 * data direction for specified request.
 */
enum dma_data_direction isci_request_io_request_get_data_direction(
	struct isci_request *request)
{
	struct sas_task *task = isci_request_access_task(request);

	return task->data_dir;
}

/**
 * isci_request_sge_get_address_field() - This function is called by the sci
 *    core to retrieve the address field contents for a given sge.
 * @request: This parameter is the isci_request object.
 * @sge_address: This parameter is the sge.
 *
 * physical address in the specified sge.
 */


/**
 * isci_request_sge_get_length_field() - This function is called by the sci
 *    core to retrieve the length field contents for a given sge.
 * @request: This parameter is the isci_request object.
 * @sge_address: This parameter is the sge.
 *
 * length field value in the specified sge.
 */


/**
 * isci_request_ssp_io_request_get_cdb_address() - This function is called by
 *    the sci core to retrieve the cdb address for a given request.
 * @request: This parameter is the isci_request object.
 *
 * cdb address for specified request.
 */
void *isci_request_ssp_io_request_get_cdb_address(
	struct isci_request *request)
{
	struct sas_task *task = isci_request_access_task(request);

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: request->task->ssp_task.cdb = %p\n",
		__func__,
		task->ssp_task.cdb);
	return task->ssp_task.cdb;
}


/**
 * isci_request_ssp_io_request_get_cdb_length() - This function is called by
 *    the sci core to retrieve the cdb length for a given request.
 * @request: This parameter is the isci_request object.
 *
 * cdb length for specified request.
 */
u32 isci_request_ssp_io_request_get_cdb_length(
	struct isci_request *request)
{
	return 16;
}


/**
 * isci_request_ssp_io_request_get_lun() - This function is called by the sci
 *    core to retrieve the lun for a given request.
 * @request: This parameter is the isci_request object.
 *
 * lun for specified request.
 */
u32 isci_request_ssp_io_request_get_lun(
	struct isci_request *request)
{
	struct sas_task *task = isci_request_access_task(request);

#ifdef DEBUG
	int i;

	for (i = 0; i < 8; i++)
		dev_dbg(&request->isci_host->pdev->dev,
			"%s: task->ssp_task.LUN[%d] = %x\n",
			__func__, i, task->ssp_task.LUN[i]);

#endif

	return task->ssp_task.LUN[0];
}


/**
 * isci_request_ssp_io_request_get_task_attribute() - This function is called
 *    by the sci core to retrieve the task attribute for a given request.
 * @request: This parameter is the isci_request object.
 *
 * task attribute for specified request.
 */
u32 isci_request_ssp_io_request_get_task_attribute(
	struct isci_request *request)
{
	struct sas_task *task = isci_request_access_task(request);

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: request->task->ssp_task.task_attr = %x\n",
		__func__,
		task->ssp_task.task_attr);

	return task->ssp_task.task_attr;
}


/**
 * isci_request_ssp_io_request_get_command_priority() - This function is called
 *    by the sci core to retrieve the command priority for a given request.
 * @request: This parameter is the isci_request object.
 *
 * command priority for specified request.
 */
u32 isci_request_ssp_io_request_get_command_priority(
	struct isci_request *request)
{
	struct sas_task *task = isci_request_access_task(request);

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: request->task->ssp_task.task_prio = %x\n",
		__func__,
		task->ssp_task.task_prio);

	return task->ssp_task.task_prio;
}
