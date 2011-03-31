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

#include <linux/completion.h>
#include <linux/irqflags.h>
#include "scic_task_request.h"
#include "scic_remote_device.h"
#include "scic_io_request.h"
#include "scic_sds_remote_device.h"
#include "scic_sds_remote_node_context.h"
#include "isci.h"
#include "request.h"
#include "sata.h"
#include "task.h"

/**
* isci_task_refuse() - complete the request to the upper layer driver in
*     the case where an I/O needs to be completed back in the submit path.
* @ihost: host on which the the request was queued
* @task: request to complete
* @response: response code for the completed task.
* @status: status code for the completed task.
*
*/
static void isci_task_refuse(struct isci_host *ihost, struct sas_task *task,
			     enum service_response response,
			     enum exec_status status)

{
	enum isci_completion_selection disposition;

	disposition = isci_perform_normal_io_completion;
	disposition = isci_task_set_completion_status(task, response, status,
						      disposition);

	/* Tasks aborted specifically by a call to the lldd_abort_task
	 * function should not be completed to the host in the regular path.
	 */
	switch (disposition) {
		case isci_perform_normal_io_completion:
			/* Normal notification (task_done) */
			dev_dbg(&ihost->pdev->dev,
				"%s: Normal - task = %p, response=%d, "
				"status=%d\n",
				__func__, task, response, status);

			task->lldd_task = NULL;

			isci_execpath_callback(ihost, task, task->task_done);
			break;

		case isci_perform_aborted_io_completion:
			/* No notification because this request is already in the
			* abort path.
			*/
			dev_warn(&ihost->pdev->dev,
				 "%s: Aborted - task = %p, response=%d, "
				"status=%d\n",
				 __func__, task, response, status);
			break;

		case isci_perform_error_io_completion:
			/* Use sas_task_abort */
			dev_warn(&ihost->pdev->dev,
				 "%s: Error - task = %p, response=%d, "
				"status=%d\n",
				 __func__, task, response, status);

			isci_execpath_callback(ihost, task, sas_task_abort);
			break;

		default:
			dev_warn(&ihost->pdev->dev,
				 "%s: isci task notification default case!",
				 __func__);
			sas_task_abort(task);
			break;
	}
}

#define for_each_sas_task(num, task) \
	for (; num > 0; num--,\
	     task = list_entry(task->list.next, struct sas_task, list))

/**
 * isci_task_execute_task() - This function is one of the SAS Domain Template
 *    functions. This function is called by libsas to send a task down to
 *    hardware.
 * @task: This parameter specifies the SAS task to send.
 * @num: This parameter specifies the number of tasks to queue.
 * @gfp_flags: This parameter specifies the context of this call.
 *
 * status, zero indicates success.
 */
int isci_task_execute_task(struct sas_task *task, int num, gfp_t gfp_flags)
{
	struct isci_host *ihost = dev_to_ihost(task->dev);
	struct isci_request *request = NULL;
	struct isci_remote_device *device;
	unsigned long flags;
	int ret;
	enum sci_status status;
	enum isci_status device_status;

	dev_dbg(&ihost->pdev->dev, "%s: num=%d\n", __func__, num);

	/* Check if we have room for more tasks */
	ret = isci_host_can_queue(ihost, num);

	if (ret) {
		dev_warn(&ihost->pdev->dev, "%s: queue full\n", __func__);
		return ret;
	}

	for_each_sas_task(num, task) {
		dev_dbg(&ihost->pdev->dev,
			"task = %p, num = %d; dev = %p; cmd = %p\n",
			    task, num, task->dev, task->uldd_task);

		device = task->dev->lldd_dev;

		if (device)
			device_status = device->status;
		else
			device_status = isci_freed;

		/* From this point onward, any process that needs to guarantee
		 * that there is no kernel I/O being started will have to wait
		 * for the quiesce spinlock.
		 */

		if (device_status != isci_ready_for_io) {

			/* Forces a retry from scsi mid layer. */
			dev_warn(&ihost->pdev->dev,
				 "%s: task %p: isci_host->status = %d, "
				 "device = %p; device_status = 0x%x\n\n",
				 __func__,
				 task,
				 isci_host_get_state(ihost),
				 device, device_status);

			if (device_status == isci_ready) {
				/* Indicate QUEUE_FULL so that the scsi midlayer
				* retries.
				*/
				isci_task_refuse(ihost, task,
						 SAS_TASK_COMPLETE,
						 SAS_QUEUE_FULL);
			} else {
				/* Else, the device is going down. */
				isci_task_refuse(ihost, task,
						 SAS_TASK_UNDELIVERED,
						 SAS_DEVICE_UNKNOWN);
			}
			isci_host_can_dequeue(ihost, 1);
		} else {
			/* There is a device and it's ready for I/O. */
			spin_lock_irqsave(&task->task_state_lock, flags);

			if (task->task_state_flags & SAS_TASK_STATE_ABORTED) {

				spin_unlock_irqrestore(&task->task_state_lock,
						       flags);

				isci_task_refuse(ihost, task,
						 SAS_TASK_UNDELIVERED,
						 SAM_STAT_TASK_ABORTED);

				/* The I/O was aborted. */

			} else {
				task->task_state_flags |= SAS_TASK_AT_INITIATOR;
				spin_unlock_irqrestore(&task->task_state_lock, flags);

				/* build and send the request. */
				status = isci_request_execute(ihost, task, &request,
							      gfp_flags);

				if (status != SCI_SUCCESS) {

					spin_lock_irqsave(&task->task_state_lock, flags);
					/* Did not really start this command. */
					task->task_state_flags &= ~SAS_TASK_AT_INITIATOR;
					spin_unlock_irqrestore(&task->task_state_lock, flags);

					/* Indicate QUEUE_FULL so that the scsi
					* midlayer retries. if the request
					* failed for remote device reasons,
					* it gets returned as
					* SAS_TASK_UNDELIVERED next time
					* through.
					*/
					isci_task_refuse(ihost, task,
							 SAS_TASK_COMPLETE,
							 SAS_QUEUE_FULL);
					isci_host_can_dequeue(ihost, 1);
				}
			}
		}
	}
	return 0;
}



/**
 * isci_task_request_build() - This function builds the task request object.
 * @isci_host: This parameter specifies the ISCI host object
 * @request: This parameter points to the isci_request object allocated in the
 *    request construct function.
 * @tmf: This parameter is the task management struct to be built
 *
 * SCI_SUCCESS on successfull completion, or specific failure code.
 */
static enum sci_status isci_task_request_build(
	struct isci_host *isci_host,
	struct isci_request **isci_request,
	struct isci_tmf *isci_tmf)
{
	struct scic_sds_remote_device *sci_device;
	enum sci_status status = SCI_FAILURE;
	struct isci_request *request;
	struct isci_remote_device *isci_device;
/*	struct sci_sas_identify_address_frame_protocols dev_protocols; */
	struct smp_discover_response_protocols dev_protocols;


	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_tmf = %p\n", __func__, isci_tmf);

	isci_device = isci_tmf->device;
	sci_device = to_sci_dev(isci_device);

	/* do common allocation and init of request object. */
	status = isci_request_alloc_tmf(
		isci_host,
		isci_tmf,
		&request,
		isci_device,
		GFP_ATOMIC
		);

	if (status != SCI_SUCCESS)
		goto out;

	/* let the core do it's construct. */
	status = scic_task_request_construct(
		isci_host->core_controller,
		sci_device,
		SCI_CONTROLLER_INVALID_IO_TAG,
		request,
		request->sci_request_mem_ptr,
		&request->sci_request_handle
		);

	if (status != SCI_SUCCESS) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: scic_task_request_construct failed - "
			 "status = 0x%x\n",
			 __func__,
			 status);
		goto errout;
	}

	sci_object_set_association(
		request->sci_request_handle,
		request
		);

	scic_remote_device_get_protocols(
		sci_device,
		&dev_protocols
		);

	/* let the core do it's protocol
	 * specific construction.
	 */
	if (dev_protocols.u.bits.attached_ssp_target) {

		isci_tmf->proto = SAS_PROTOCOL_SSP;
		status = scic_task_request_construct_ssp(
			request->sci_request_handle
			);
		if (status != SCI_SUCCESS)
			goto errout;
	}

	if (dev_protocols.u.bits.attached_stp_target) {

		isci_tmf->proto = SAS_PROTOCOL_SATA;
		status = isci_sata_management_task_request_build(request);

		if (status != SCI_SUCCESS)
			goto errout;
	}

	goto out;

 errout:

	/* release the dma memory if we fail. */
	isci_request_free(isci_host, request);
	request = NULL;

 out:
	*isci_request = request;
	return status;
}

/**
 * isci_tmf_timeout_cb() - This function is called as a kernel callback when
 *    the timeout period for the TMF has expired.
 *
 *
 */
static void isci_tmf_timeout_cb(void *tmf_request_arg)
{
	struct isci_request *request = (struct isci_request *)tmf_request_arg;
	struct isci_tmf *tmf = isci_request_access_tmf(request);
	enum sci_status status;

	BUG_ON(request->ttype != tmf_task);

	/* This task management request has timed-out.  Terminate the request
	 * so that the request eventually completes to the requestor in the
	 * request completion callback path.
	 */
	/* Note - the timer callback function itself has provided spinlock
	 * exclusion from the start and completion paths.  No need to take
	 * the request->isci_host->scic_lock here.
	 */

	if (tmf->timeout_timer != NULL) {
		/* Call the users callback, if any. */
		if (tmf->cb_state_func != NULL)
			tmf->cb_state_func(isci_tmf_timed_out, tmf,
					   tmf->cb_data);

		/* Terminate the TMF transmit request. */
		status = scic_controller_terminate_request(
			request->isci_host->core_controller,
			to_sci_dev(request->isci_device),
			request->sci_request_handle
			);

		dev_dbg(&request->isci_host->pdev->dev,
			"%s: tmf_request = %p; tmf = %p; status = %d\n",
			__func__, request, tmf, status);
	} else
		dev_dbg(&request->isci_host->pdev->dev,
			"%s: timer already canceled! "
			"tmf_request = %p; tmf = %p\n",
			__func__, request, tmf);

	/* No need to unlock since the caller to this callback is doing it for
	 * us.
	 * request->isci_host->scic_lock
	 */
}

/**
 * isci_task_execute_tmf() - This function builds and sends a task request,
 *    then waits for the completion.
 * @isci_host: This parameter specifies the ISCI host object
 * @tmf: This parameter is the pointer to the task management structure for
 *    this request.
 * @timeout_ms: This parameter specifies the timeout period for the task
 *    management request.
 *
 * TMF_RESP_FUNC_COMPLETE on successful completion of the TMF (this includes
 * error conditions reported in the IU status), or TMF_RESP_FUNC_FAILED.
 */
int isci_task_execute_tmf(
	struct isci_host *isci_host,
	struct isci_tmf *tmf,
	unsigned long timeout_ms)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	enum sci_status status = SCI_FAILURE;
	struct scic_sds_remote_device *sci_device;
	struct isci_remote_device *isci_device = tmf->device;
	struct isci_request *request;
	int ret = TMF_RESP_FUNC_FAILED;
	unsigned long flags;

	/* sanity check, return TMF_RESP_FUNC_FAILED
	 * if the device is not there and ready.
	 */
	if (!isci_device || isci_device->status != isci_ready_for_io) {
		dev_dbg(&isci_host->pdev->dev,
			"%s: isci_device = %p not ready (%d)\n",
			__func__,
			isci_device, isci_device->status);
		return TMF_RESP_FUNC_FAILED;
	} else
		dev_dbg(&isci_host->pdev->dev,
			"%s: isci_device = %p\n",
			__func__, isci_device);

	sci_device = to_sci_dev(isci_device);

	/* Assign the pointer to the TMF's completion kernel wait structure. */
	tmf->complete = &completion;

	isci_task_request_build(
		isci_host,
		&request,
		tmf
		);

	if (!request) {
		dev_warn(&isci_host->pdev->dev,
			"%s: isci_task_request_build failed\n",
			__func__);
		return TMF_RESP_FUNC_FAILED;
	}

	/* Allocate the TMF timeout timer. */
	spin_lock_irqsave(&isci_host->scic_lock, flags);
	tmf->timeout_timer = isci_timer_create(isci_host, request, isci_tmf_timeout_cb);

	/* Start the timer. */
	if (tmf->timeout_timer)
		isci_timer_start(tmf->timeout_timer, timeout_ms);
	else
		dev_warn(&isci_host->pdev->dev,
			 "%s: isci_timer_create failed!!!!\n",
			 __func__);

	/* start the TMF io. */
	status = scic_controller_start_task(
		isci_host->core_controller,
		sci_device,
		request->sci_request_handle,
		SCI_CONTROLLER_INVALID_IO_TAG
		);

	if (status != SCI_SUCCESS) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: start_io failed - status = 0x%x, request = %p\n",
			 __func__,
			 status,
			 request);
		goto cleanup_request;
	}

	/* Call the users callback, if any. */
	if (tmf->cb_state_func != NULL)
		tmf->cb_state_func(isci_tmf_started, tmf, tmf->cb_data);

	/* Change the state of the TMF-bearing request to "started". */
	isci_request_change_state(request, started);

	/* add the request to the remote device request list. */
	list_add(&request->dev_node, &isci_device->reqs_in_process);

	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	/* Wait for the TMF to complete, or a timeout. */
	wait_for_completion(&completion);

	isci_print_tmf(tmf);

	if (tmf->status == SCI_SUCCESS)
		ret =  TMF_RESP_FUNC_COMPLETE;
	else if (tmf->status == SCI_FAILURE_IO_RESPONSE_VALID) {
		dev_dbg(&isci_host->pdev->dev,
			"%s: tmf.status == "
			"SCI_FAILURE_IO_RESPONSE_VALID\n",
			__func__);
		ret =  TMF_RESP_FUNC_COMPLETE;
	}
	/* Else - leave the default "failed" status alone. */

	dev_dbg(&isci_host->pdev->dev,
		"%s: completed request = %p\n",
		__func__,
		request);

	if (request->io_request_completion != NULL) {

		/* The fact that this is non-NULL for a TMF request
		 * means there is a thread waiting for this TMF to
		 * finish.
		 */
		complete(request->io_request_completion);
	}

	spin_lock_irqsave(&isci_host->scic_lock, flags);

 cleanup_request:

	/* Clean up the timer if needed. */
	if (tmf->timeout_timer) {
		isci_del_timer(isci_host, tmf->timeout_timer);
		tmf->timeout_timer = NULL;
	}

	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	isci_request_free(isci_host, request);

	return ret;
}

void isci_task_build_tmf(
	struct isci_tmf *tmf,
	struct isci_remote_device *isci_device,
	enum isci_tmf_function_codes code,
	void (*tmf_sent_cb)(enum isci_tmf_cb_state,
			    struct isci_tmf *,
			    void *),
	void *cb_data)
{
	dev_dbg(&isci_device->isci_port->isci_host->pdev->dev,
		"%s: isci_device = %p\n", __func__, isci_device);

	memset(tmf, 0, sizeof(*tmf));

	tmf->device        = isci_device;
	tmf->tmf_code      = code;
	tmf->timeout_timer = NULL;
	tmf->cb_state_func = tmf_sent_cb;
	tmf->cb_data       = cb_data;
}

static void isci_task_build_abort_task_tmf(
	struct isci_tmf *tmf,
	struct isci_remote_device *isci_device,
	enum isci_tmf_function_codes code,
	void (*tmf_sent_cb)(enum isci_tmf_cb_state,
			    struct isci_tmf *,
			    void *),
	struct isci_request *old_request)
{
	isci_task_build_tmf(tmf, isci_device, code, tmf_sent_cb,
			    (void *)old_request);
	tmf->io_tag = old_request->io_tag;
}

static struct isci_request *isci_task_get_request_from_task(
	struct sas_task *task,
	struct isci_remote_device **isci_device)
{

	struct isci_request *request = NULL;
	unsigned long flags;

	spin_lock_irqsave(&task->task_state_lock, flags);

	request = task->lldd_task;

	/* If task is already done, the request isn't valid */
	if (!(task->task_state_flags & SAS_TASK_STATE_DONE) &&
	    (task->task_state_flags & SAS_TASK_AT_INITIATOR) &&
	    (request != NULL)) {

		if (isci_device != NULL)
			*isci_device = request->isci_device;
	}

	spin_unlock_irqrestore(&task->task_state_lock, flags);

	return request;
}

/**
 * isci_task_validate_request_to_abort() - This function checks the given I/O
 *    against the "started" state.  If the request is still "started", it's
 *    state is changed to aborted. NOTE: isci_host->scic_lock MUST BE HELD
 *    BEFORE CALLING THIS FUNCTION.
 * @isci_request: This parameter specifies the request object to control.
 * @isci_host: This parameter specifies the ISCI host object
 * @isci_device: This is the device to which the request is pending.
 * @aborted_io_completion: This is a completion structure that will be added to
 *    the request in case it is changed to aborting; this completion is
 *    triggered when the request is fully completed.
 *
 * Either "started" on successful change of the task status to "aborted", or
 * "unallocated" if the task cannot be controlled.
 */
static enum isci_request_status isci_task_validate_request_to_abort(
	struct isci_request *isci_request,
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device,
	struct completion *aborted_io_completion)
{
	enum isci_request_status old_state = unallocated;

	/* Only abort the task if it's in the
	 *  device's request_in_process list
	 */
	if (isci_request && !list_empty(&isci_request->dev_node)) {
		old_state = isci_request_change_started_to_aborted(
			isci_request, aborted_io_completion);

	}

	return old_state;
}

static void isci_request_cleanup_completed_loiterer(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device,
	struct isci_request *isci_request)
{
	struct sas_task     *task;
	unsigned long       flags;

	task = (isci_request->ttype == io_task)
		? isci_request_access_task(isci_request)
		: NULL;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device=%p, request=%p, task=%p\n",
		__func__, isci_device, isci_request, task);

	spin_lock_irqsave(&isci_host->scic_lock, flags);
	list_del_init(&isci_request->dev_node);
	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	if (task != NULL) {

		spin_lock_irqsave(&task->task_state_lock, flags);
		task->lldd_task = NULL;

		isci_set_task_doneflags(task);

		/* If this task is not in the abort path, call task_done. */
		if (!(task->task_state_flags & SAS_TASK_STATE_ABORTED)) {

			spin_unlock_irqrestore(&task->task_state_lock, flags);
			task->task_done(task);
		} else
			spin_unlock_irqrestore(&task->task_state_lock, flags);
	}
	isci_request_free(isci_host, isci_request);
}

/**
* @isci_termination_timed_out(): this function will deal with a request for
* which the wait for termination has timed-out.
*
* @isci_host    This SCU.
* @isci_request The I/O request being terminated.
*/
static void
isci_termination_timed_out(
	struct isci_host    * host,
	struct isci_request * request
	)
{
	unsigned long state_flags;

	dev_warn(&host->pdev->dev,
		"%s: host = %p; request = %p\n",
		__func__, host, request);

	/* At this point, the request to terminate
	* has timed out. The best we can do is to
	* have the request die a silent death
	* if it ever completes.
	*/
	spin_lock_irqsave(&request->state_lock, state_flags);

	if (request->status == started) {

		/* Set the request state to "dead",
		* and clear the task pointer so that an actual
		* completion event callback doesn't do
		* anything.
		*/
		request->status = dead;

		/* Clear the timeout completion event pointer.*/
		request->io_request_completion = NULL;

		if (request->ttype == io_task) {

			/* Break links with the sas_task. */
			if (request->ttype_ptr.io_task_ptr != NULL) {

				request->ttype_ptr.io_task_ptr->lldd_task = NULL;
				request->ttype_ptr.io_task_ptr            = NULL;
			}
		}
	}
	spin_unlock_irqrestore(&request->state_lock, state_flags);
}


/**
 * isci_terminate_request_core() - This function will terminate the given
 *    request, and wait for it to complete.  This function must only be called
 *    from a thread that can wait.  Note that the request is terminated and
 *    completed (back to the host, if started there).
 * @isci_host: This SCU.
 * @isci_device: The target.
 * @isci_request: The I/O request to be terminated.
 *
 *
 */
static void isci_terminate_request_core(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device,
	struct isci_request *isci_request)
{
	enum sci_status status      = SCI_SUCCESS;
	bool was_terminated         = false;
	bool needs_cleanup_handling = false;
	enum isci_request_status request_status;
	unsigned long flags;
	unsigned long timeout_remaining;


	dev_dbg(&isci_host->pdev->dev,
		"%s: device = %p; request = %p\n",
		__func__, isci_device, isci_request);

	spin_lock_irqsave(&isci_host->scic_lock, flags);

	/* Note that we are not going to control
	* the target to abort the request.
	*/
	isci_request->complete_in_target = true;

	/* Make sure the request wasn't just sitting around signalling
	 * device condition (if the request handle is NULL, then the
	 * request completed but needed additional handling here).
	 */
	if (isci_request->sci_request_handle != NULL) {
		was_terminated = true;
		needs_cleanup_handling = true;
		status = scic_controller_terminate_request(
			isci_host->core_controller,
			to_sci_dev(isci_device),
			isci_request->sci_request_handle
			);
	}
	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	/*
	 * The only time the request to terminate will
	 * fail is when the io request is completed and
	 * being aborted.
	 */
	if (status != SCI_SUCCESS) {
		dev_err(&isci_host->pdev->dev,
			"%s: scic_controller_terminate_request"
			" returned = 0x%x\n",
			__func__,
			status);
		/* Clear the completion pointer from the request. */
		isci_request->io_request_completion = NULL;

	} else {
		if (was_terminated) {
			dev_dbg(&isci_host->pdev->dev,
				"%s: before completion wait (%p)\n",
				__func__,
				isci_request->io_request_completion);

			/* Wait here for the request to complete. */
			#define TERMINATION_TIMEOUT_MSEC 50
			timeout_remaining
				= wait_for_completion_timeout(
				   isci_request->io_request_completion,
				   msecs_to_jiffies(TERMINATION_TIMEOUT_MSEC));

			if (!timeout_remaining) {

				isci_termination_timed_out(isci_host,
							   isci_request);

				dev_err(&isci_host->pdev->dev,
					"%s: *** Timeout waiting for "
					"termination(%p/%p)\n",
					__func__,
					isci_request->io_request_completion,
					isci_request);

			} else
				dev_dbg(&isci_host->pdev->dev,
					"%s: after completion wait (%p)\n",
					__func__,
					isci_request->io_request_completion);
		}
		/* Clear the completion pointer from the request. */
		isci_request->io_request_completion = NULL;

		/* Peek at the status of the request.  This will tell
		* us if there was special handling on the request such that it
		* needs to be detached and freed here.
		*/
		spin_lock_irqsave(&isci_request->state_lock, flags);
		request_status = isci_request_get_state(isci_request);

		if ((isci_request->ttype == io_task) /* TMFs are in their own thread */
		    && ((request_status == aborted)
			|| (request_status == aborting)
			|| (request_status == terminating)
			|| (request_status == completed)
			|| (request_status == dead)
			)
		    ) {

			/* The completion routine won't free a request in
			* the aborted/aborting/etc. states, so we do
			* it here.
			*/
			needs_cleanup_handling = true;
		}
		spin_unlock_irqrestore(&isci_request->state_lock, flags);

		if (needs_cleanup_handling)
			isci_request_cleanup_completed_loiterer(
				isci_host, isci_device, isci_request
				);
	}
}

static void isci_terminate_request(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device,
	struct isci_request *isci_request,
	enum isci_request_status new_request_state)
{
	enum isci_request_status old_state;
	DECLARE_COMPLETION_ONSTACK(request_completion);

	/* Change state to "new_request_state" if it is currently "started" */
	old_state = isci_request_change_started_to_newstate(
		isci_request,
		&request_completion,
		new_request_state
		);

	if ((old_state == started) ||
	    (old_state == completed) ||
	    (old_state == aborting)) {

		/* If the old_state is started:
		 * This request was not already being aborted. If it had been,
		 * then the aborting I/O (ie. the TMF request) would not be in
		 * the aborting state, and thus would be terminated here.  Note
		 * that since the TMF completion's call to the kernel function
		 * "complete()" does not happen until the pending I/O request
		 * terminate fully completes, we do not have to implement a
		 * special wait here for already aborting requests - the
		 * termination of the TMF request will force the request
		 * to finish it's already started terminate.
		 *
		 * If old_state == completed:
		 * This request completed from the SCU hardware perspective
		 * and now just needs cleaning up in terms of freeing the
		 * request and potentially calling up to libsas.
		 *
		 * If old_state == aborting:
		 * This request has already gone through a TMF timeout, but may
		 * not have been terminated; needs cleaning up at least.
		 */
		isci_terminate_request_core(isci_host, isci_device,
					    isci_request);
	}
}

/**
 * isci_terminate_pending_requests() - This function will change the all of the
 *    requests on the given device's state to "aborting", will terminate the
 *    requests, and wait for them to complete.  This function must only be
 *    called from a thread that can wait.  Note that the requests are all
 *    terminated and completed (back to the host, if started there).
 * @isci_host: This parameter specifies SCU.
 * @isci_device: This parameter specifies the target.
 *
 *
 */
void isci_terminate_pending_requests(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device,
	enum isci_request_status new_request_state)
{
	struct isci_request *request;
	struct isci_request *next_request;
	unsigned long       flags;
	struct list_head    aborted_request_list;

	INIT_LIST_HEAD(&aborted_request_list);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p (new request state = %d)\n",
		__func__, isci_device, new_request_state);

	spin_lock_irqsave(&isci_host->scic_lock, flags);

	/* Move all of the pending requests off of the device list. */
	list_splice_init(&isci_device->reqs_in_process,
			 &aborted_request_list);

	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	/* Iterate through the now-local list. */
	list_for_each_entry_safe(request, next_request,
				 &aborted_request_list, dev_node) {

		dev_warn(&isci_host->pdev->dev,
			"%s: isci_device=%p request=%p; task=%p\n",
			__func__,
			isci_device, request,
			((request->ttype == io_task)
				? isci_request_access_task(request)
				: NULL));

		/* Mark all still pending I/O with the selected next
		* state, terminate and free it.
		*/
		isci_terminate_request(isci_host, isci_device,
				       request, new_request_state
				       );
	}
}

/**
 * isci_task_send_lu_reset_sas() - This function is called by of the SAS Domain
 *    Template functions.
 * @lun: This parameter specifies the lun to be reset.
 *
 * status, zero indicates success.
 */
static int isci_task_send_lu_reset_sas(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device,
	u8 *lun)
{
	struct isci_tmf tmf;
	int ret = TMF_RESP_FUNC_FAILED;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_host = %p, isci_device = %p\n",
		__func__, isci_host, isci_device);
	/* Send the LUN reset to the target.  By the time the call returns,
	 * the TMF has fully exected in the target (in which case the return
	 * value is "TMF_RESP_FUNC_COMPLETE", or the request timed-out (or
	 * was otherwise unable to be executed ("TMF_RESP_FUNC_FAILED").
	 */
	isci_task_build_tmf(&tmf, isci_device, isci_tmf_ssp_lun_reset, NULL,
			    NULL);

	#define ISCI_LU_RESET_TIMEOUT_MS 2000 /* 2 second timeout. */
	ret = isci_task_execute_tmf(isci_host, &tmf, ISCI_LU_RESET_TIMEOUT_MS);

	if (ret == TMF_RESP_FUNC_COMPLETE)
		dev_dbg(&isci_host->pdev->dev,
			"%s: %p: TMF_LU_RESET passed\n",
			__func__, isci_device);
	else
		dev_dbg(&isci_host->pdev->dev,
			"%s: %p: TMF_LU_RESET failed (%x)\n",
			__func__, isci_device, ret);

	return ret;
}

/**
 * isci_task_lu_reset() - This function is one of the SAS Domain Template
 *    functions. This is one of the Task Management functoins called by libsas,
 *    to reset the given lun. Note the assumption that while this call is
 *    executing, no I/O will be sent by the host to the device.
 * @lun: This parameter specifies the lun to be reset.
 *
 * status, zero indicates success.
 */
int isci_task_lu_reset(struct domain_device *domain_device, u8 *lun)
{
	struct isci_host *isci_host = dev_to_ihost(domain_device);
	struct isci_remote_device *isci_device = NULL;
	int ret;
	bool device_stopping = false;

	isci_device = domain_device->lldd_dev;

	dev_dbg(&isci_host->pdev->dev,
		"%s: domain_device=%p, isci_host=%p; isci_device=%p\n",
		 __func__, domain_device, isci_host, isci_device);

	if (isci_device != NULL)
		device_stopping = (isci_device->status == isci_stopping)
				  || (isci_device->status == isci_stopped);

	/* If there is a device reset pending on any request in the
	 * device's list, fail this LUN reset request in order to
	 * escalate to the device reset.
	 */
	if (!isci_device || device_stopping ||
	    isci_device_is_reset_pending(isci_host, isci_device)) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: No dev (%p), or "
			 "RESET PENDING: domain_device=%p\n",
			 __func__, isci_device, domain_device);
		return TMF_RESP_FUNC_FAILED;
	}

	/* Send the task management part of the reset. */
	if (sas_protocol_ata(domain_device->tproto)) {
		ret = isci_task_send_lu_reset_sata(isci_host, isci_device, lun);
	} else
		ret = isci_task_send_lu_reset_sas(isci_host, isci_device, lun);

	/* If the LUN reset worked, all the I/O can now be terminated. */
	if (ret == TMF_RESP_FUNC_COMPLETE)
		/* Terminate all I/O now. */
		isci_terminate_pending_requests(isci_host,
						isci_device,
						terminating);

	return ret;
}


/*	 int (*lldd_clear_nexus_port)(struct asd_sas_port *); */
int isci_task_clear_nexus_port(struct asd_sas_port *port)
{
	return TMF_RESP_FUNC_FAILED;
}



int isci_task_clear_nexus_ha(struct sas_ha_struct *ha)
{
	return TMF_RESP_FUNC_FAILED;
}

int isci_task_I_T_nexus_reset(struct domain_device *dev)
{
	return TMF_RESP_FUNC_FAILED;
}


/* Task Management Functions. Must be called from process context.	 */

/**
 * isci_abort_task_process_cb() - This is a helper function for the abort task
 *    TMF command.  It manages the request state with respect to the successful
 *    transmission / completion of the abort task request.
 * @cb_state: This parameter specifies when this function was called - after
 *    the TMF request has been started and after it has timed-out.
 * @tmf: This parameter specifies the TMF in progress.
 *
 *
 */
static void isci_abort_task_process_cb(
	enum isci_tmf_cb_state cb_state,
	struct isci_tmf *tmf,
	void *cb_data)
{
	struct isci_request *old_request;

	old_request = (struct isci_request *)cb_data;

	dev_dbg(&old_request->isci_host->pdev->dev,
		"%s: tmf=%p, old_request=%p\n",
		__func__, tmf, old_request);

	switch (cb_state) {

	case isci_tmf_started:
		/* The TMF has been started.  Nothing to do here, since the
		 * request state was already set to "aborted" by the abort
		 * task function.
		 */
		BUG_ON((old_request->status != aborted)
			&& (old_request->status != completed));
		break;

	case isci_tmf_timed_out:

		/* Set the task's state to "aborting", since the abort task
		 * function thread set it to "aborted" (above) in anticipation
		 * of the task management request working correctly.  Since the
		 * timeout has now fired, the TMF request failed.  We set the
		 * state such that the request completion will indicate the
		 * device is no longer present.
		 */
		isci_request_change_state(old_request, aborting);
		break;

	default:
		dev_err(&old_request->isci_host->pdev->dev,
			"%s: Bad cb_state (%d): tmf=%p, old_request=%p\n",
			__func__, cb_state, tmf, old_request);
		break;
	}
}

/**
 * isci_task_abort_task() - This function is one of the SAS Domain Template
 *    functions. This function is called by libsas to abort a specified task.
 * @task: This parameter specifies the SAS task to abort.
 *
 * status, zero indicates success.
 */
int isci_task_abort_task(struct sas_task *task)
{
	struct isci_host *isci_host = dev_to_ihost(task->dev);
	DECLARE_COMPLETION_ONSTACK(aborted_io_completion);
	struct isci_request       *old_request = NULL;
	enum isci_request_status  old_state;
	struct isci_remote_device *isci_device = NULL;
	struct isci_tmf           tmf;
	int                       ret = TMF_RESP_FUNC_FAILED;
	unsigned long             flags;
	bool                      any_dev_reset = false;
	bool                      device_stopping;

	/* Get the isci_request reference from the task.  Note that
	 * this check does not depend on the pending request list
	 * in the device, because tasks driving resets may land here
	 * after completion in the core.
	 */
	old_request = isci_task_get_request_from_task(task, &isci_device);

	dev_dbg(&isci_host->pdev->dev,
		"%s: task = %p\n", __func__, task);

	/* Check if the device has been / is currently being removed.
	 * If so, no task management will be done, and the I/O will
	 * be terminated.
	 */
	device_stopping = (isci_device->status == isci_stopping)
			  || (isci_device->status == isci_stopped);

	/* This version of the driver will fail abort requests for
	 * SATA/STP.  Failing the abort request this way will cause the
	 * SCSI error handler thread to escalate to LUN reset
	 */
	if (sas_protocol_ata(task->task_proto) && !device_stopping) {
		dev_warn(&isci_host->pdev->dev,
			    " task %p is for a STP/SATA device;"
			    " returning TMF_RESP_FUNC_FAILED\n"
			    " to cause a LUN reset...\n", task);
		return TMF_RESP_FUNC_FAILED;
	}

	dev_dbg(&isci_host->pdev->dev,
		"%s: old_request == %p\n", __func__, old_request);

	if (!device_stopping)
		any_dev_reset = isci_device_is_reset_pending(isci_host,isci_device);

	spin_lock_irqsave(&task->task_state_lock, flags);

	/* Don't do resets to stopping devices. */
	if (device_stopping) {

		task->task_state_flags &= ~SAS_TASK_NEED_DEV_RESET;
		any_dev_reset = false;

	} else	/* See if there is a pending device reset for this device. */
		any_dev_reset = any_dev_reset
			|| (task->task_state_flags & SAS_TASK_NEED_DEV_RESET);

	/* If the extraction of the request reference from the task
	 * failed, then the request has been completed (or if there is a
	 * pending reset then this abort request function must be failed
	 * in order to escalate to the target reset).
	 */
	if ((old_request == NULL) || any_dev_reset) {

		/* If the device reset task flag is set, fail the task
		 * management request.  Otherwise, the original request
		 * has completed.
		 */
		if (any_dev_reset) {

			/* Turn off the task's DONE to make sure this
			 * task is escalated to a target reset.
			 */
			task->task_state_flags &= ~SAS_TASK_STATE_DONE;

			/* Make the reset happen as soon as possible. */
			task->task_state_flags |= SAS_TASK_NEED_DEV_RESET;

			spin_unlock_irqrestore(&task->task_state_lock, flags);

			/* Fail the task management request in order to
			 * escalate to the target reset.
			 */
			ret = TMF_RESP_FUNC_FAILED;

			dev_dbg(&isci_host->pdev->dev,
				"%s: Failing task abort in order to "
				"escalate to target reset because\n"
				"SAS_TASK_NEED_DEV_RESET is set for "
				"task %p on dev %p\n",
				__func__, task, isci_device);


		} else {
			/* The request has already completed and there
			 * is nothing to do here other than to set the task
			 * done bit, and indicate that the task abort function
			 * was sucessful.
			 */
			isci_set_task_doneflags(task);

			spin_unlock_irqrestore(&task->task_state_lock, flags);

			ret = TMF_RESP_FUNC_COMPLETE;

			dev_dbg(&isci_host->pdev->dev,
				"%s: abort task not needed for %p\n",
				__func__, task);
		}

		return ret;
	}
	else
		spin_unlock_irqrestore(&task->task_state_lock, flags);

	spin_lock_irqsave(&isci_host->scic_lock, flags);

	/* Check the request status and change to "aborted" if currently
	 * "starting"; if true then set the I/O kernel completion
	 * struct that will be triggered when the request completes.
	 */
	old_state = isci_task_validate_request_to_abort(
				old_request, isci_host, isci_device,
				&aborted_io_completion);
	if ((old_state != started) &&
	    (old_state != completed) &&
	    (old_state != aborting)) {

		spin_unlock_irqrestore(&isci_host->scic_lock, flags);

		/* The request was already being handled by someone else (because
		* they got to set the state away from started).
		*/
		dev_dbg(&isci_host->pdev->dev,
			"%s:  device = %p; old_request %p already being aborted\n",
			__func__,
			isci_device, old_request);

		return TMF_RESP_FUNC_COMPLETE;
	}
	if ((task->task_proto == SAS_PROTOCOL_SMP)
	    || device_stopping
	    || old_request->complete_in_target
	    ) {

		spin_unlock_irqrestore(&isci_host->scic_lock, flags);

		dev_dbg(&isci_host->pdev->dev,
			"%s: SMP request (%d)"
			" or device is stopping (%d)"
			" or complete_in_target (%d), thus no TMF\n",
			__func__, (task->task_proto == SAS_PROTOCOL_SMP),
			device_stopping, old_request->complete_in_target);

		/* Set the state on the task. */
		isci_task_all_done(task);

		ret = TMF_RESP_FUNC_COMPLETE;

		/* Stopping and SMP devices are not sent a TMF, and are not
		 * reset, but the outstanding I/O request is terminated below.
		 */
	} else {
		/* Fill in the tmf stucture */
		isci_task_build_abort_task_tmf(&tmf, isci_device,
					       isci_tmf_ssp_task_abort,
					       isci_abort_task_process_cb,
					       old_request);

		spin_unlock_irqrestore(&isci_host->scic_lock, flags);

		#define ISCI_ABORT_TASK_TIMEOUT_MS 500 /* half second timeout. */
		ret = isci_task_execute_tmf(isci_host, &tmf,
					    ISCI_ABORT_TASK_TIMEOUT_MS);

		if (ret != TMF_RESP_FUNC_COMPLETE)
			dev_err(&isci_host->pdev->dev,
				"%s: isci_task_send_tmf failed\n",
				__func__);
	}
	if (ret == TMF_RESP_FUNC_COMPLETE) {
		old_request->complete_in_target = true;

		/* Clean up the request on our side, and wait for the aborted I/O to
		* complete.
		*/
		isci_terminate_request_core(isci_host, isci_device, old_request);
	}

	/* Make sure we do not leave a reference to aborted_io_completion */
	old_request->io_request_completion = NULL;
	return ret;
}

/**
 * isci_task_abort_task_set() - This function is one of the SAS Domain Template
 *    functions. This is one of the Task Management functoins called by libsas,
 *    to abort all task for the given lun.
 * @d_device: This parameter specifies the domain device associated with this
 *    request.
 * @lun: This parameter specifies the lun associated with this request.
 *
 * status, zero indicates success.
 */
int isci_task_abort_task_set(
	struct domain_device *d_device,
	u8 *lun)
{
	return TMF_RESP_FUNC_FAILED;
}


/**
 * isci_task_clear_aca() - This function is one of the SAS Domain Template
 *    functions. This is one of the Task Management functoins called by libsas.
 * @d_device: This parameter specifies the domain device associated with this
 *    request.
 * @lun: This parameter specifies the lun	 associated with this request.
 *
 * status, zero indicates success.
 */
int isci_task_clear_aca(
	struct domain_device *d_device,
	u8 *lun)
{
	return TMF_RESP_FUNC_FAILED;
}



/**
 * isci_task_clear_task_set() - This function is one of the SAS Domain Template
 *    functions. This is one of the Task Management functoins called by libsas.
 * @d_device: This parameter specifies the domain device associated with this
 *    request.
 * @lun: This parameter specifies the lun	 associated with this request.
 *
 * status, zero indicates success.
 */
int isci_task_clear_task_set(
	struct domain_device *d_device,
	u8 *lun)
{
	return TMF_RESP_FUNC_FAILED;
}


/**
 * isci_task_query_task() - This function is implemented to cause libsas to
 *    correctly escalate the failed abort to a LUN or target reset (this is
 *    because sas_scsi_find_task libsas function does not correctly interpret
 *    all return codes from the abort task call).  When TMF_RESP_FUNC_SUCC is
 *    returned, libsas turns this into a LUN reset; when FUNC_FAILED is
 *    returned, libsas will turn this into a target reset
 * @task: This parameter specifies the sas task being queried.
 * @lun: This parameter specifies the lun associated with this request.
 *
 * status, zero indicates success.
 */
int isci_task_query_task(
	struct sas_task *task)
{
	/* See if there is a pending device reset for this device. */
	if (task->task_state_flags & SAS_TASK_NEED_DEV_RESET)
		return TMF_RESP_FUNC_FAILED;
	else
		return TMF_RESP_FUNC_SUCC;
}

/**
 * isci_task_request_complete() - This function is called by the sci core when
 *    an task request completes.
 * @isci_host: This parameter specifies the ISCI host object
 * @request: This parameter is the completed isci_request object.
 * @completion_status: This parameter specifies the completion status from the
 *    sci core.
 *
 * none.
 */
void isci_task_request_complete(
	struct isci_host *isci_host,
	struct isci_request *request,
	enum sci_task_status completion_status)
{
	struct isci_remote_device *isci_device = request->isci_device;
	enum isci_request_status old_state;
	struct isci_tmf *tmf = isci_request_access_tmf(request);
	struct completion *tmf_complete;

	dev_dbg(&isci_host->pdev->dev,
		"%s: request = %p, status=%d\n",
		__func__, request, completion_status);

	old_state = isci_request_change_state(request, completed);

	tmf->status = completion_status;
	request->complete_in_target = true;

	if (SAS_PROTOCOL_SSP == tmf->proto) {

		memcpy(&tmf->resp.resp_iu,
		       scic_io_request_get_response_iu_address(
			       request->sci_request_handle
			       ),
		       sizeof(struct sci_ssp_response_iu));

	} else if (SAS_PROTOCOL_SATA == tmf->proto) {

		memcpy(&tmf->resp.d2h_fis,
		       scic_stp_io_request_get_d2h_reg_address(
			       request->sci_request_handle
			       ),
		       sizeof(struct sata_fis_reg_d2h)
		       );
	}

	/* Manage the timer if it is still running. */
	if (tmf->timeout_timer) {
		isci_del_timer(isci_host, tmf->timeout_timer);
		tmf->timeout_timer = NULL;
	}

	/* PRINT_TMF( ((struct isci_tmf *)request->task)); */
	tmf_complete = tmf->complete;

	scic_controller_complete_task(
		isci_host->core_controller,
		to_sci_dev(isci_device),
		request->sci_request_handle
		);
	/* NULL the request handle to make sure it cannot be terminated
	 *  or completed again.
	 */
	request->sci_request_handle = NULL;

	isci_request_change_state(request, unallocated);
	list_del_init(&request->dev_node);

	/* The task management part completes last. */
	complete(tmf_complete);
}


/**
 * isci_task_ssp_request_get_lun() - This function is called by the sci core to
 *    retrieve the lun for a given task request.
 * @request: This parameter is the isci_request object.
 *
 * lun for specified task request.
 */

/**
 * isci_task_ssp_request_get_function() - This function is called by the sci
 *    core to retrieve the function for a given task request.
 * @request: This parameter is the isci_request object.
 *
 * function code for specified task request.
 */
u8 isci_task_ssp_request_get_function(struct isci_request *request)
{
	struct isci_tmf *isci_tmf = isci_request_access_tmf(request);

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: func = %d\n", __func__, isci_tmf->tmf_code);

	return isci_tmf->tmf_code;
}

/**
 * isci_task_ssp_request_get_io_tag_to_manage() - This function is called by
 *    the sci core to retrieve the io tag for a given task request.
 * @request: This parameter is the isci_request object.
 *
 * io tag for specified task request.
 */
u16 isci_task_ssp_request_get_io_tag_to_manage(struct isci_request *request)
{
	u16 io_tag = SCI_CONTROLLER_INVALID_IO_TAG;

	if (tmf_task == request->ttype) {
		struct isci_tmf *tmf = isci_request_access_tmf(request);
		io_tag = tmf->io_tag;
	}

	dev_dbg(&request->isci_host->pdev->dev,
		"%s: request = %p, io_tag = %d\n",
		__func__, request, io_tag);

	return io_tag;
}

/**
 * isci_task_ssp_request_get_response_data_address() - This function is called
 *    by the sci core to retrieve the response data address for a given task
 *    request.
 * @request: This parameter is the isci_request object.
 *
 * response data address for specified task request.
 */
void *isci_task_ssp_request_get_response_data_address(
	struct isci_request *request)
{
	struct isci_tmf *isci_tmf = isci_request_access_tmf(request);

	return &isci_tmf->resp.resp_iu;
}

/**
 * isci_task_ssp_request_get_response_data_length() - This function is called
 *    by the sci core to retrieve the response data length for a given task
 *    request.
 * @request: This parameter is the isci_request object.
 *
 * response data length for specified task request.
 */
u32 isci_task_ssp_request_get_response_data_length(
	struct isci_request *request)
{
	struct isci_tmf *isci_tmf = isci_request_access_tmf(request);

	return sizeof(isci_tmf->resp.resp_iu);
}

/**
 * isci_bus_reset_handler() - This function performs a target reset of the
 *    device referenced by "cmd'.  This function is exported through the
 *    "struct scsi_host_template" structure such that it is called when an I/O
 *    recovery process has escalated to a target reset. Note that this function
 *    is called from the scsi error handler event thread, so may block on calls.
 * @scsi_cmd: This parameter specifies the target to be reset.
 *
 * SUCCESS if the reset process was successful, else FAILED.
 */
int isci_bus_reset_handler(struct scsi_cmnd *cmd)
{
	struct domain_device *dev = cmd_to_domain_dev(cmd);
	struct isci_host *isci_host = dev_to_ihost(dev);
	unsigned long flags = 0;
	enum sci_status status;
	int base_status;
	struct isci_remote_device *isci_dev = dev->lldd_dev;

	dev_dbg(&isci_host->pdev->dev,
		"%s: cmd %p, isci_dev %p\n",
		__func__, cmd, isci_dev);

	if (!isci_dev) {
		dev_warn(&isci_host->pdev->dev,
			 "%s: isci_dev is GONE!\n",
			 __func__);

		return TMF_RESP_FUNC_COMPLETE; /* Nothing to reset. */
	}

	spin_lock_irqsave(&isci_host->scic_lock, flags);
	status = scic_remote_device_reset(to_sci_dev(isci_dev));
	if (status != SCI_SUCCESS) {
		spin_unlock_irqrestore(&isci_host->scic_lock, flags);

		scmd_printk(KERN_WARNING, cmd,
			    "%s: scic_remote_device_reset(%p) returned %d!\n",
			    __func__, isci_dev, status);

		return TMF_RESP_FUNC_FAILED;
	}
	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	/* Make sure all pending requests are able to be fully terminated. */
	isci_device_clear_reset_pending(isci_host, isci_dev);

	/* Terminate in-progress I/O now. */
	isci_remote_device_nuke_requests(isci_host, isci_dev);

	/* Call into the libsas default handler (which calls sas_phy_reset). */
	base_status = sas_eh_bus_reset_handler(cmd);

	if (base_status != SUCCESS) {

		/* There can be cases where the resets to individual devices
		 * behind an expander will fail because of an unplug of the
		 * expander itself.
		 */
		scmd_printk(KERN_WARNING, cmd,
			    "%s: sas_eh_bus_reset_handler(%p) returned %d!\n",
			    __func__, cmd, base_status);
	}

	/* WHAT TO DO HERE IF sas_phy_reset FAILS? */
	spin_lock_irqsave(&isci_host->scic_lock, flags);
	status = scic_remote_device_reset_complete(to_sci_dev(isci_dev));
	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	if (status != SCI_SUCCESS) {
		scmd_printk(KERN_WARNING, cmd,
			    "%s: scic_remote_device_reset_complete(%p) "
			    "returned %d!\n",
			    __func__, isci_dev, status);
	}
	/* WHAT TO DO HERE IF scic_remote_device_reset_complete FAILS? */

	dev_dbg(&isci_host->pdev->dev,
		"%s: cmd %p, isci_dev %p complete.\n",
		__func__, cmd, isci_dev);

	return TMF_RESP_FUNC_COMPLETE;
}
