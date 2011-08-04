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
#include "sas.h"
#include <scsi/libsas.h>
#include "remote_device.h"
#include "remote_node_context.h"
#include "isci.h"
#include "request.h"
#include "task.h"
#include "host.h"

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
		/*
		 * No notification because this request is already in the
		 * abort path.
		 */
		dev_dbg(&ihost->pdev->dev,
			"%s: Aborted - task = %p, response=%d, "
			"status=%d\n",
			__func__, task, response, status);
		break;

	case isci_perform_error_io_completion:
		/* Use sas_task_abort */
		dev_dbg(&ihost->pdev->dev,
			"%s: Error - task = %p, response=%d, "
			"status=%d\n",
			__func__, task, response, status);

		isci_execpath_callback(ihost, task, sas_task_abort);
		break;

	default:
		dev_dbg(&ihost->pdev->dev,
			"%s: isci task notification default case!",
			__func__);
		sas_task_abort(task);
		break;
	}
}

#define for_each_sas_task(num, task) \
	for (; num > 0; num--,\
	     task = list_entry(task->list.next, struct sas_task, list))


static inline int isci_device_io_ready(struct isci_remote_device *idev,
				       struct sas_task *task)
{
	return idev ? test_bit(IDEV_IO_READY, &idev->flags) ||
		      (test_bit(IDEV_IO_NCQERROR, &idev->flags) &&
		       isci_task_is_ncq_recovery(task))
		    : 0;
}
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
	struct isci_remote_device *idev;
	unsigned long flags;
	bool io_ready;
	u16 tag;

	dev_dbg(&ihost->pdev->dev, "%s: num=%d\n", __func__, num);

	for_each_sas_task(num, task) {
		enum sci_status status = SCI_FAILURE;

		spin_lock_irqsave(&ihost->scic_lock, flags);
		idev = isci_lookup_device(task->dev);
		io_ready = isci_device_io_ready(idev, task);
		tag = isci_alloc_tag(ihost);
		spin_unlock_irqrestore(&ihost->scic_lock, flags);

		dev_dbg(&ihost->pdev->dev,
			"task: %p, num: %d dev: %p idev: %p:%#lx cmd = %p\n",
			task, num, task->dev, idev, idev ? idev->flags : 0,
			task->uldd_task);

		if (!idev) {
			isci_task_refuse(ihost, task, SAS_TASK_UNDELIVERED,
					 SAS_DEVICE_UNKNOWN);
		} else if (!io_ready || tag == SCI_CONTROLLER_INVALID_IO_TAG) {
			/* Indicate QUEUE_FULL so that the scsi midlayer
			 * retries.
			  */
			isci_task_refuse(ihost, task, SAS_TASK_COMPLETE,
					 SAS_QUEUE_FULL);
		} else {
			/* There is a device and it's ready for I/O. */
			spin_lock_irqsave(&task->task_state_lock, flags);

			if (task->task_state_flags & SAS_TASK_STATE_ABORTED) {
				/* The I/O was aborted. */
				spin_unlock_irqrestore(&task->task_state_lock,
						       flags);

				isci_task_refuse(ihost, task,
						 SAS_TASK_UNDELIVERED,
						 SAM_STAT_TASK_ABORTED);
			} else {
				task->task_state_flags |= SAS_TASK_AT_INITIATOR;
				spin_unlock_irqrestore(&task->task_state_lock, flags);

				/* build and send the request. */
				status = isci_request_execute(ihost, idev, task, tag);

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
				}
			}
		}
		if (status != SCI_SUCCESS && tag != SCI_CONTROLLER_INVALID_IO_TAG) {
			spin_lock_irqsave(&ihost->scic_lock, flags);
			/* command never hit the device, so just free
			 * the tci and skip the sequence increment
			 */
			isci_tci_free(ihost, ISCI_TAG_TCI(tag));
			spin_unlock_irqrestore(&ihost->scic_lock, flags);
		}
		isci_put_device(idev);
	}
	return 0;
}

static enum sci_status isci_sata_management_task_request_build(struct isci_request *ireq)
{
	struct isci_tmf *isci_tmf;
	enum sci_status status;

	if (tmf_task != ireq->ttype)
		return SCI_FAILURE;

	isci_tmf = isci_request_access_tmf(ireq);

	switch (isci_tmf->tmf_code) {

	case isci_tmf_sata_srst_high:
	case isci_tmf_sata_srst_low: {
		struct host_to_dev_fis *fis = &ireq->stp.cmd;

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
	status = sci_task_request_construct_sata(ireq);

	return status;
}

static struct isci_request *isci_task_request_build(struct isci_host *ihost,
						    struct isci_remote_device *idev,
						    u16 tag, struct isci_tmf *isci_tmf)
{
	enum sci_status status = SCI_FAILURE;
	struct isci_request *ireq = NULL;
	struct domain_device *dev;

	dev_dbg(&ihost->pdev->dev,
		"%s: isci_tmf = %p\n", __func__, isci_tmf);

	dev = idev->domain_dev;

	/* do common allocation and init of request object. */
	ireq = isci_tmf_request_from_tag(ihost, isci_tmf, tag);
	if (!ireq)
		return NULL;

	/* let the core do it's construct. */
	status = sci_task_request_construct(ihost, idev, tag,
					     ireq);

	if (status != SCI_SUCCESS) {
		dev_warn(&ihost->pdev->dev,
			 "%s: sci_task_request_construct failed - "
			 "status = 0x%x\n",
			 __func__,
			 status);
		return NULL;
	}

	/* XXX convert to get this from task->tproto like other drivers */
	if (dev->dev_type == SAS_END_DEV) {
		isci_tmf->proto = SAS_PROTOCOL_SSP;
		status = sci_task_request_construct_ssp(ireq);
		if (status != SCI_SUCCESS)
			return NULL;
	}

	if (dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP)) {
		isci_tmf->proto = SAS_PROTOCOL_SATA;
		status = isci_sata_management_task_request_build(ireq);

		if (status != SCI_SUCCESS)
			return NULL;
	}
	return ireq;
}

static int isci_task_execute_tmf(struct isci_host *ihost,
				 struct isci_remote_device *idev,
				 struct isci_tmf *tmf, unsigned long timeout_ms)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	enum sci_task_status status = SCI_TASK_FAILURE;
	struct isci_request *ireq;
	int ret = TMF_RESP_FUNC_FAILED;
	unsigned long flags;
	unsigned long timeleft;
	u16 tag;

	spin_lock_irqsave(&ihost->scic_lock, flags);
	tag = isci_alloc_tag(ihost);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	if (tag == SCI_CONTROLLER_INVALID_IO_TAG)
		return ret;

	/* sanity check, return TMF_RESP_FUNC_FAILED
	 * if the device is not there and ready.
	 */
	if (!idev ||
	    (!test_bit(IDEV_IO_READY, &idev->flags) &&
	     !test_bit(IDEV_IO_NCQERROR, &idev->flags))) {
		dev_dbg(&ihost->pdev->dev,
			"%s: idev = %p not ready (%#lx)\n",
			__func__,
			idev, idev ? idev->flags : 0);
		goto err_tci;
	} else
		dev_dbg(&ihost->pdev->dev,
			"%s: idev = %p\n",
			__func__, idev);

	/* Assign the pointer to the TMF's completion kernel wait structure. */
	tmf->complete = &completion;

	ireq = isci_task_request_build(ihost, idev, tag, tmf);
	if (!ireq)
		goto err_tci;

	spin_lock_irqsave(&ihost->scic_lock, flags);

	/* start the TMF io. */
	status = sci_controller_start_task(ihost, idev, ireq);

	if (status != SCI_TASK_SUCCESS) {
		dev_dbg(&ihost->pdev->dev,
			 "%s: start_io failed - status = 0x%x, request = %p\n",
			 __func__,
			 status,
			 ireq);
		spin_unlock_irqrestore(&ihost->scic_lock, flags);
		goto err_tci;
	}

	if (tmf->cb_state_func != NULL)
		tmf->cb_state_func(isci_tmf_started, tmf, tmf->cb_data);

	isci_request_change_state(ireq, started);

	/* add the request to the remote device request list. */
	list_add(&ireq->dev_node, &idev->reqs_in_process);

	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/* Wait for the TMF to complete, or a timeout. */
	timeleft = wait_for_completion_timeout(&completion,
					       msecs_to_jiffies(timeout_ms));

	if (timeleft == 0) {
		spin_lock_irqsave(&ihost->scic_lock, flags);

		if (tmf->cb_state_func != NULL)
			tmf->cb_state_func(isci_tmf_timed_out, tmf, tmf->cb_data);

		sci_controller_terminate_request(ihost,
						  idev,
						  ireq);

		spin_unlock_irqrestore(&ihost->scic_lock, flags);

		wait_for_completion(tmf->complete);
	}

	isci_print_tmf(tmf);

	if (tmf->status == SCI_SUCCESS)
		ret =  TMF_RESP_FUNC_COMPLETE;
	else if (tmf->status == SCI_FAILURE_IO_RESPONSE_VALID) {
		dev_dbg(&ihost->pdev->dev,
			"%s: tmf.status == "
			"SCI_FAILURE_IO_RESPONSE_VALID\n",
			__func__);
		ret =  TMF_RESP_FUNC_COMPLETE;
	}
	/* Else - leave the default "failed" status alone. */

	dev_dbg(&ihost->pdev->dev,
		"%s: completed request = %p\n",
		__func__,
		ireq);

	return ret;

 err_tci:
	spin_lock_irqsave(&ihost->scic_lock, flags);
	isci_tci_free(ihost, ISCI_TAG_TCI(tag));
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	return ret;
}

static void isci_task_build_tmf(struct isci_tmf *tmf,
				enum isci_tmf_function_codes code,
				void (*tmf_sent_cb)(enum isci_tmf_cb_state,
						    struct isci_tmf *,
						    void *),
				void *cb_data)
{
	memset(tmf, 0, sizeof(*tmf));

	tmf->tmf_code      = code;
	tmf->cb_state_func = tmf_sent_cb;
	tmf->cb_data       = cb_data;
}

static void isci_task_build_abort_task_tmf(struct isci_tmf *tmf,
					   enum isci_tmf_function_codes code,
					   void (*tmf_sent_cb)(enum isci_tmf_cb_state,
							       struct isci_tmf *,
							       void *),
					   struct isci_request *old_request)
{
	isci_task_build_tmf(tmf, code, tmf_sent_cb, old_request);
	tmf->io_tag = old_request->io_tag;
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

/**
* isci_request_cleanup_completed_loiterer() - This function will take care of
*    the final cleanup on any request which has been explicitly terminated.
* @isci_host: This parameter specifies the ISCI host object
* @isci_device: This is the device to which the request is pending.
* @isci_request: This parameter specifies the terminated request object.
* @task: This parameter is the libsas I/O request.
*/
static void isci_request_cleanup_completed_loiterer(
	struct isci_host          *isci_host,
	struct isci_remote_device *isci_device,
	struct isci_request       *isci_request,
	struct sas_task           *task)
{
	unsigned long flags;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device=%p, request=%p, task=%p\n",
		__func__, isci_device, isci_request, task);

	if (task != NULL) {

		spin_lock_irqsave(&task->task_state_lock, flags);
		task->lldd_task = NULL;

		task->task_state_flags &= ~SAS_TASK_NEED_DEV_RESET;

		isci_set_task_doneflags(task);

		/* If this task is not in the abort path, call task_done. */
		if (!(task->task_state_flags & SAS_TASK_STATE_ABORTED)) {

			spin_unlock_irqrestore(&task->task_state_lock, flags);
			task->task_done(task);
		} else
			spin_unlock_irqrestore(&task->task_state_lock, flags);
	}

	if (isci_request != NULL) {
		spin_lock_irqsave(&isci_host->scic_lock, flags);
		list_del_init(&isci_request->dev_node);
		spin_unlock_irqrestore(&isci_host->scic_lock, flags);
	}
}

/**
 * isci_terminate_request_core() - This function will terminate the given
 *    request, and wait for it to complete.  This function must only be called
 *    from a thread that can wait.  Note that the request is terminated and
 *    completed (back to the host, if started there).
 * @ihost: This SCU.
 * @idev: The target.
 * @isci_request: The I/O request to be terminated.
 *
 */
static void isci_terminate_request_core(struct isci_host *ihost,
					struct isci_remote_device *idev,
					struct isci_request *isci_request)
{
	enum sci_status status      = SCI_SUCCESS;
	bool was_terminated         = false;
	bool needs_cleanup_handling = false;
	enum isci_request_status request_status;
	unsigned long     flags;
	unsigned long     termination_completed = 1;
	struct completion *io_request_completion;
	struct sas_task   *task;

	dev_dbg(&ihost->pdev->dev,
		"%s: device = %p; request = %p\n",
		__func__, idev, isci_request);

	spin_lock_irqsave(&ihost->scic_lock, flags);

	io_request_completion = isci_request->io_request_completion;

	task = (isci_request->ttype == io_task)
		? isci_request_access_task(isci_request)
		: NULL;

	/* Note that we are not going to control
	 * the target to abort the request.
	 */
	set_bit(IREQ_COMPLETE_IN_TARGET, &isci_request->flags);

	/* Make sure the request wasn't just sitting around signalling
	 * device condition (if the request handle is NULL, then the
	 * request completed but needed additional handling here).
	 */
	if (!test_bit(IREQ_TERMINATED, &isci_request->flags)) {
		was_terminated = true;
		needs_cleanup_handling = true;
		status = sci_controller_terminate_request(ihost,
							   idev,
							   isci_request);
	}
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/*
	 * The only time the request to terminate will
	 * fail is when the io request is completed and
	 * being aborted.
	 */
	if (status != SCI_SUCCESS) {
		dev_dbg(&ihost->pdev->dev,
			"%s: sci_controller_terminate_request"
			" returned = 0x%x\n",
			__func__, status);

		isci_request->io_request_completion = NULL;

	} else {
		if (was_terminated) {
			dev_dbg(&ihost->pdev->dev,
				"%s: before completion wait (%p/%p)\n",
				__func__, isci_request, io_request_completion);

			/* Wait here for the request to complete. */
			#define TERMINATION_TIMEOUT_MSEC 500
			termination_completed
				= wait_for_completion_timeout(
				   io_request_completion,
				   msecs_to_jiffies(TERMINATION_TIMEOUT_MSEC));

			if (!termination_completed) {

				/* The request to terminate has timed out.  */
				spin_lock_irqsave(&ihost->scic_lock,
						  flags);

				/* Check for state changes. */
				if (!test_bit(IREQ_TERMINATED, &isci_request->flags)) {

					/* The best we can do is to have the
					 * request die a silent death if it
					 * ever really completes.
					 *
					 * Set the request state to "dead",
					 * and clear the task pointer so that
					 * an actual completion event callback
					 * doesn't do anything.
					 */
					isci_request->status = dead;
					isci_request->io_request_completion
						= NULL;

					if (isci_request->ttype == io_task) {

						/* Break links with the
						* sas_task.
						*/
						isci_request->ttype_ptr.io_task_ptr
							= NULL;
					}
				} else
					termination_completed = 1;

				spin_unlock_irqrestore(&ihost->scic_lock,
						       flags);

				if (!termination_completed) {

					dev_dbg(&ihost->pdev->dev,
						"%s: *** Timeout waiting for "
						"termination(%p/%p)\n",
						__func__, io_request_completion,
						isci_request);

					/* The request can no longer be referenced
					 * safely since it may go away if the
					 * termination every really does complete.
					 */
					isci_request = NULL;
				}
			}
			if (termination_completed)
				dev_dbg(&ihost->pdev->dev,
					"%s: after completion wait (%p/%p)\n",
					__func__, isci_request, io_request_completion);
		}

		if (termination_completed) {

			isci_request->io_request_completion = NULL;

			/* Peek at the status of the request.  This will tell
			 * us if there was special handling on the request such that it
			 * needs to be detached and freed here.
			 */
			spin_lock_irqsave(&isci_request->state_lock, flags);
			request_status = isci_request->status;

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

		}
		if (needs_cleanup_handling)
			isci_request_cleanup_completed_loiterer(
				ihost, idev, isci_request, task);
	}
}

/**
 * isci_terminate_pending_requests() - This function will change the all of the
 *    requests on the given device's state to "aborting", will terminate the
 *    requests, and wait for them to complete.  This function must only be
 *    called from a thread that can wait.  Note that the requests are all
 *    terminated and completed (back to the host, if started there).
 * @isci_host: This parameter specifies SCU.
 * @idev: This parameter specifies the target.
 *
 */
void isci_terminate_pending_requests(struct isci_host *ihost,
				     struct isci_remote_device *idev)
{
	struct completion request_completion;
	enum isci_request_status old_state;
	unsigned long flags;
	LIST_HEAD(list);

	spin_lock_irqsave(&ihost->scic_lock, flags);
	list_splice_init(&idev->reqs_in_process, &list);

	/* assumes that isci_terminate_request_core deletes from the list */
	while (!list_empty(&list)) {
		struct isci_request *ireq = list_entry(list.next, typeof(*ireq), dev_node);

		/* Change state to "terminating" if it is currently
		 * "started".
		 */
		old_state = isci_request_change_started_to_newstate(ireq,
								    &request_completion,
								    terminating);
		switch (old_state) {
		case started:
		case completed:
		case aborting:
			break;
		default:
			/* termination in progress, or otherwise dispositioned.
			 * We know the request was on 'list' so should be safe
			 * to move it back to reqs_in_process
			 */
			list_move(&ireq->dev_node, &idev->reqs_in_process);
			ireq = NULL;
			break;
		}

		if (!ireq)
			continue;
		spin_unlock_irqrestore(&ihost->scic_lock, flags);

		init_completion(&request_completion);

		dev_dbg(&ihost->pdev->dev,
			 "%s: idev=%p request=%p; task=%p old_state=%d\n",
			 __func__, idev, ireq,
			ireq->ttype == io_task ? isci_request_access_task(ireq) : NULL,
			old_state);

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
		isci_terminate_request_core(ihost, idev, ireq);
		spin_lock_irqsave(&ihost->scic_lock, flags);
	}
	spin_unlock_irqrestore(&ihost->scic_lock, flags);
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
	isci_task_build_tmf(&tmf, isci_tmf_ssp_lun_reset, NULL, NULL);

	#define ISCI_LU_RESET_TIMEOUT_MS 2000 /* 2 second timeout. */
	ret = isci_task_execute_tmf(isci_host, isci_device, &tmf, ISCI_LU_RESET_TIMEOUT_MS);

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

static int isci_task_send_lu_reset_sata(struct isci_host *ihost,
				 struct isci_remote_device *idev, u8 *lun)
{
	int ret = TMF_RESP_FUNC_FAILED;
	struct isci_tmf tmf;

	/* Send the soft reset to the target */
	#define ISCI_SRST_TIMEOUT_MS 25000 /* 25 second timeout. */
	isci_task_build_tmf(&tmf, isci_tmf_sata_srst_high, NULL, NULL);

	ret = isci_task_execute_tmf(ihost, idev, &tmf, ISCI_SRST_TIMEOUT_MS);

	if (ret != TMF_RESP_FUNC_COMPLETE) {
		dev_dbg(&ihost->pdev->dev,
			 "%s: Assert SRST failed (%p) = %x",
			 __func__, idev, ret);

		/* Return the failure so that the LUN reset is escalated
		 * to a target reset.
		 */
	}
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
	struct isci_remote_device *isci_device;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&isci_host->scic_lock, flags);
	isci_device = isci_lookup_device(domain_device);
	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	dev_dbg(&isci_host->pdev->dev,
		"%s: domain_device=%p, isci_host=%p; isci_device=%p\n",
		 __func__, domain_device, isci_host, isci_device);

	if (isci_device)
		set_bit(IDEV_EH, &isci_device->flags);

	/* If there is a device reset pending on any request in the
	 * device's list, fail this LUN reset request in order to
	 * escalate to the device reset.
	 */
	if (!isci_device ||
	    isci_device_is_reset_pending(isci_host, isci_device)) {
		dev_dbg(&isci_host->pdev->dev,
			 "%s: No dev (%p), or "
			 "RESET PENDING: domain_device=%p\n",
			 __func__, isci_device, domain_device);
		ret = TMF_RESP_FUNC_FAILED;
		goto out;
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
						isci_device);

 out:
	isci_put_device(isci_device);
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
		if ((old_request->status != aborted)
			&& (old_request->status != completed))
			dev_dbg(&old_request->isci_host->pdev->dev,
				"%s: Bad request status (%d): tmf=%p, old_request=%p\n",
				__func__, old_request->status, tmf, old_request);
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
		dev_dbg(&old_request->isci_host->pdev->dev,
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

	/* Get the isci_request reference from the task.  Note that
	 * this check does not depend on the pending request list
	 * in the device, because tasks driving resets may land here
	 * after completion in the core.
	 */
	spin_lock_irqsave(&isci_host->scic_lock, flags);
	spin_lock(&task->task_state_lock);

	old_request = task->lldd_task;

	/* If task is already done, the request isn't valid */
	if (!(task->task_state_flags & SAS_TASK_STATE_DONE) &&
	    (task->task_state_flags & SAS_TASK_AT_INITIATOR) &&
	    old_request)
		isci_device = isci_lookup_device(task->dev);

	spin_unlock(&task->task_state_lock);
	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	dev_dbg(&isci_host->pdev->dev,
		"%s: task = %p\n", __func__, task);

	if (!isci_device || !old_request)
		goto out;

	set_bit(IDEV_EH, &isci_device->flags);

	/* This version of the driver will fail abort requests for
	 * SATA/STP.  Failing the abort request this way will cause the
	 * SCSI error handler thread to escalate to LUN reset
	 */
	if (sas_protocol_ata(task->task_proto)) {
		dev_dbg(&isci_host->pdev->dev,
			    " task %p is for a STP/SATA device;"
			    " returning TMF_RESP_FUNC_FAILED\n"
			    " to cause a LUN reset...\n", task);
		goto out;
	}

	dev_dbg(&isci_host->pdev->dev,
		"%s: old_request == %p\n", __func__, old_request);

	any_dev_reset = isci_device_is_reset_pending(isci_host, isci_device);

	spin_lock_irqsave(&task->task_state_lock, flags);

	any_dev_reset = any_dev_reset || (task->task_state_flags & SAS_TASK_NEED_DEV_RESET);

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
		goto out;
	} else {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
	}

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
		ret = TMF_RESP_FUNC_COMPLETE;
		goto out;
	}
	if (task->task_proto == SAS_PROTOCOL_SMP ||
	    test_bit(IREQ_COMPLETE_IN_TARGET, &old_request->flags)) {

		spin_unlock_irqrestore(&isci_host->scic_lock, flags);

		dev_dbg(&isci_host->pdev->dev,
			"%s: SMP request (%d)"
			" or complete_in_target (%d), thus no TMF\n",
			__func__, (task->task_proto == SAS_PROTOCOL_SMP),
			test_bit(IREQ_COMPLETE_IN_TARGET, &old_request->flags));

		/* Set the state on the task. */
		isci_task_all_done(task);

		ret = TMF_RESP_FUNC_COMPLETE;

		/* Stopping and SMP devices are not sent a TMF, and are not
		 * reset, but the outstanding I/O request is terminated below.
		 */
	} else {
		/* Fill in the tmf stucture */
		isci_task_build_abort_task_tmf(&tmf, isci_tmf_ssp_task_abort,
					       isci_abort_task_process_cb,
					       old_request);

		spin_unlock_irqrestore(&isci_host->scic_lock, flags);

		#define ISCI_ABORT_TASK_TIMEOUT_MS 500 /* half second timeout. */
		ret = isci_task_execute_tmf(isci_host, isci_device, &tmf,
					    ISCI_ABORT_TASK_TIMEOUT_MS);

		if (ret != TMF_RESP_FUNC_COMPLETE)
			dev_dbg(&isci_host->pdev->dev,
				"%s: isci_task_send_tmf failed\n",
				__func__);
	}
	if (ret == TMF_RESP_FUNC_COMPLETE) {
		set_bit(IREQ_COMPLETE_IN_TARGET, &old_request->flags);

		/* Clean up the request on our side, and wait for the aborted
		 * I/O to complete.
		 */
		isci_terminate_request_core(isci_host, isci_device, old_request);
	}

	/* Make sure we do not leave a reference to aborted_io_completion */
	old_request->io_request_completion = NULL;
 out:
	isci_put_device(isci_device);
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

/*
 * isci_task_request_complete() - This function is called by the sci core when
 *    an task request completes.
 * @ihost: This parameter specifies the ISCI host object
 * @ireq: This parameter is the completed isci_request object.
 * @completion_status: This parameter specifies the completion status from the
 *    sci core.
 *
 * none.
 */
void
isci_task_request_complete(struct isci_host *ihost,
			   struct isci_request *ireq,
			   enum sci_task_status completion_status)
{
	struct isci_tmf *tmf = isci_request_access_tmf(ireq);
	struct completion *tmf_complete;

	dev_dbg(&ihost->pdev->dev,
		"%s: request = %p, status=%d\n",
		__func__, ireq, completion_status);

	isci_request_change_state(ireq, completed);

	tmf->status = completion_status;
	set_bit(IREQ_COMPLETE_IN_TARGET, &ireq->flags);

	if (tmf->proto == SAS_PROTOCOL_SSP) {
		memcpy(&tmf->resp.resp_iu,
		       &ireq->ssp.rsp,
		       SSP_RESP_IU_MAX_SIZE);
	} else if (tmf->proto == SAS_PROTOCOL_SATA) {
		memcpy(&tmf->resp.d2h_fis,
		       &ireq->stp.rsp,
		       sizeof(struct dev_to_host_fis));
	}

	/* PRINT_TMF( ((struct isci_tmf *)request->task)); */
	tmf_complete = tmf->complete;

	sci_controller_complete_io(ihost, ireq->target_device, ireq);
	/* set the 'terminated' flag handle to make sure it cannot be terminated
	 *  or completed again.
	 */
	set_bit(IREQ_TERMINATED, &ireq->flags);

	isci_request_change_state(ireq, unallocated);
	list_del_init(&ireq->dev_node);

	/* The task management part completes last. */
	complete(tmf_complete);
}

static void isci_smp_task_timedout(unsigned long _task)
{
	struct sas_task *task = (void *) _task;
	unsigned long flags;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (!(task->task_state_flags & SAS_TASK_STATE_DONE))
		task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	complete(&task->completion);
}

static void isci_smp_task_done(struct sas_task *task)
{
	if (!del_timer(&task->timer))
		return;
	complete(&task->completion);
}

static struct sas_task *isci_alloc_task(void)
{
	struct sas_task *task = kzalloc(sizeof(*task), GFP_KERNEL);

	if (task) {
		INIT_LIST_HEAD(&task->list);
		spin_lock_init(&task->task_state_lock);
		task->task_state_flags = SAS_TASK_STATE_PENDING;
		init_timer(&task->timer);
		init_completion(&task->completion);
	}

	return task;
}

static void isci_free_task(struct isci_host *ihost, struct sas_task  *task)
{
	if (task) {
		BUG_ON(!list_empty(&task->list));
		kfree(task);
	}
}

static int isci_smp_execute_task(struct isci_host *ihost,
				 struct domain_device *dev, void *req,
				 int req_size, void *resp, int resp_size)
{
	int res, retry;
	struct sas_task *task = NULL;

	for (retry = 0; retry < 3; retry++) {
		task = isci_alloc_task();
		if (!task)
			return -ENOMEM;

		task->dev = dev;
		task->task_proto = dev->tproto;
		sg_init_one(&task->smp_task.smp_req, req, req_size);
		sg_init_one(&task->smp_task.smp_resp, resp, resp_size);

		task->task_done = isci_smp_task_done;

		task->timer.data = (unsigned long) task;
		task->timer.function = isci_smp_task_timedout;
		task->timer.expires = jiffies + 10*HZ;
		add_timer(&task->timer);

		res = isci_task_execute_task(task, 1, GFP_KERNEL);

		if (res) {
			del_timer(&task->timer);
			dev_dbg(&ihost->pdev->dev,
				"%s: executing SMP task failed:%d\n",
				__func__, res);
			goto ex_err;
		}

		wait_for_completion(&task->completion);
		res = -ECOMM;
		if ((task->task_state_flags & SAS_TASK_STATE_ABORTED)) {
			dev_dbg(&ihost->pdev->dev,
				"%s: smp task timed out or aborted\n",
				__func__);
			isci_task_abort_task(task);
			if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
				dev_dbg(&ihost->pdev->dev,
					"%s: SMP task aborted and not done\n",
					__func__);
				goto ex_err;
			}
		}
		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		    task->task_status.stat == SAM_STAT_GOOD) {
			res = 0;
			break;
		}
		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		      task->task_status.stat == SAS_DATA_UNDERRUN) {
			/* no error, but return the number of bytes of
			* underrun */
			res = task->task_status.residual;
			break;
		}
		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		      task->task_status.stat == SAS_DATA_OVERRUN) {
			res = -EMSGSIZE;
			break;
		} else {
			dev_dbg(&ihost->pdev->dev,
				"%s: task to dev %016llx response: 0x%x "
				"status 0x%x\n", __func__,
				SAS_ADDR(dev->sas_addr),
				task->task_status.resp,
				task->task_status.stat);
			isci_free_task(ihost, task);
			task = NULL;
		}
	}
ex_err:
	BUG_ON(retry == 3 && task != NULL);
	isci_free_task(ihost, task);
	return res;
}

#define DISCOVER_REQ_SIZE  16
#define DISCOVER_RESP_SIZE 56

int isci_smp_get_phy_attached_dev_type(struct isci_host *ihost,
				       struct domain_device *dev,
				       int phy_id, int *adt)
{
	struct smp_resp *disc_resp;
	u8 *disc_req;
	int res;

	disc_resp = kzalloc(DISCOVER_RESP_SIZE, GFP_KERNEL);
	if (!disc_resp)
		return -ENOMEM;

	disc_req = kzalloc(DISCOVER_REQ_SIZE, GFP_KERNEL);
	if (disc_req) {
		disc_req[0] = SMP_REQUEST;
		disc_req[1] = SMP_DISCOVER;
		disc_req[9] = phy_id;
	} else {
		kfree(disc_resp);
		return -ENOMEM;
	}
	res = isci_smp_execute_task(ihost, dev, disc_req, DISCOVER_REQ_SIZE,
				    disc_resp, DISCOVER_RESP_SIZE);
	if (!res) {
		if (disc_resp->result != SMP_RESP_FUNC_ACC)
			res = disc_resp->result;
		else
			*adt = disc_resp->disc.attached_dev_type;
	}
	kfree(disc_req);
	kfree(disc_resp);

	return res;
}

static void isci_wait_for_smp_phy_reset(struct isci_remote_device *idev, int phy_num)
{
	struct domain_device *dev = idev->domain_dev;
	struct isci_port *iport = idev->isci_port;
	struct isci_host *ihost = iport->isci_host;
	int res, iteration = 0, attached_device_type;
	#define STP_WAIT_MSECS 25000
	unsigned long tmo = msecs_to_jiffies(STP_WAIT_MSECS);
	unsigned long deadline = jiffies + tmo;
	enum {
		SMP_PHYWAIT_PHYDOWN,
		SMP_PHYWAIT_PHYUP,
		SMP_PHYWAIT_DONE
	} phy_state = SMP_PHYWAIT_PHYDOWN;

	/* While there is time, wait for the phy to go away and come back */
	while (time_is_after_jiffies(deadline) && phy_state != SMP_PHYWAIT_DONE) {
		int event = atomic_read(&iport->event);

		++iteration;

		tmo = wait_event_timeout(ihost->eventq,
					 event != atomic_read(&iport->event) ||
					 !test_bit(IPORT_BCN_BLOCKED, &iport->flags),
					 tmo);
		/* link down, stop polling */
		if (!test_bit(IPORT_BCN_BLOCKED, &iport->flags))
			break;

		dev_dbg(&ihost->pdev->dev,
			"%s: iport %p, iteration %d,"
			" phase %d: time_remaining %lu, bcns = %d\n",
			__func__, iport, iteration, phy_state,
			tmo, test_bit(IPORT_BCN_PENDING, &iport->flags));

		res = isci_smp_get_phy_attached_dev_type(ihost, dev, phy_num,
							 &attached_device_type);
		tmo = deadline - jiffies;

		if (res) {
			dev_dbg(&ihost->pdev->dev,
				 "%s: iteration %d, phase %d:"
				 " SMP error=%d, time_remaining=%lu\n",
				 __func__, iteration, phy_state, res, tmo);
			break;
		}
		dev_dbg(&ihost->pdev->dev,
			"%s: iport %p, iteration %d,"
			" phase %d: time_remaining %lu, bcns = %d, "
			"attdevtype = %x\n",
			__func__, iport, iteration, phy_state,
			tmo, test_bit(IPORT_BCN_PENDING, &iport->flags),
			attached_device_type);

		switch (phy_state) {
		case SMP_PHYWAIT_PHYDOWN:
			/* Has the device gone away? */
			if (!attached_device_type)
				phy_state = SMP_PHYWAIT_PHYUP;

			break;

		case SMP_PHYWAIT_PHYUP:
			/* Has the device come back? */
			if (attached_device_type)
				phy_state = SMP_PHYWAIT_DONE;
			break;

		case SMP_PHYWAIT_DONE:
			break;
		}

	}
	dev_dbg(&ihost->pdev->dev, "%s: done\n",  __func__);
}

static int isci_reset_device(struct isci_host *ihost,
			     struct isci_remote_device *idev)
{
	struct sas_phy *phy = sas_find_local_phy(idev->domain_dev);
	struct isci_port *iport = idev->isci_port;
	enum sci_status status;
	unsigned long flags;
	int rc;

	dev_dbg(&ihost->pdev->dev, "%s: idev %p\n", __func__, idev);

	spin_lock_irqsave(&ihost->scic_lock, flags);
	status = sci_remote_device_reset(idev);
	if (status != SCI_SUCCESS) {
		spin_unlock_irqrestore(&ihost->scic_lock, flags);

		dev_dbg(&ihost->pdev->dev,
			 "%s: sci_remote_device_reset(%p) returned %d!\n",
			 __func__, idev, status);

		return TMF_RESP_FUNC_FAILED;
	}
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/* Make sure all pending requests are able to be fully terminated. */
	isci_device_clear_reset_pending(ihost, idev);

	/* If this is a device on an expander, disable BCN processing. */
	if (!scsi_is_sas_phy_local(phy))
		set_bit(IPORT_BCN_BLOCKED, &iport->flags);

	rc = sas_phy_reset(phy, true);

	/* Terminate in-progress I/O now. */
	isci_remote_device_nuke_requests(ihost, idev);

	/* Since all pending TCs have been cleaned, resume the RNC. */
	spin_lock_irqsave(&ihost->scic_lock, flags);
	status = sci_remote_device_reset_complete(idev);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/* If this is a device on an expander, bring the phy back up. */
	if (!scsi_is_sas_phy_local(phy)) {
		/* A phy reset will cause the device to go away then reappear.
		 * Since libsas will take action on incoming BCNs (eg. remove
		 * a device going through an SMP phy-control driven reset),
		 * we need to wait until the phy comes back up before letting
		 * discovery proceed in libsas.
		 */
		isci_wait_for_smp_phy_reset(idev, phy->number);

		spin_lock_irqsave(&ihost->scic_lock, flags);
		isci_port_bcn_enable(ihost, idev->isci_port);
		spin_unlock_irqrestore(&ihost->scic_lock, flags);
	}

	if (status != SCI_SUCCESS) {
		dev_dbg(&ihost->pdev->dev,
			 "%s: sci_remote_device_reset_complete(%p) "
			 "returned %d!\n", __func__, idev, status);
	}

	dev_dbg(&ihost->pdev->dev, "%s: idev %p complete.\n", __func__, idev);

	return rc;
}

int isci_task_I_T_nexus_reset(struct domain_device *dev)
{
	struct isci_host *ihost = dev_to_ihost(dev);
	struct isci_remote_device *idev;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ihost->scic_lock, flags);
	idev = isci_lookup_device(dev);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	if (!idev || !test_bit(IDEV_EH, &idev->flags)) {
		ret = TMF_RESP_FUNC_COMPLETE;
		goto out;
	}

	ret = isci_reset_device(ihost, idev);
 out:
	isci_put_device(idev);
	return ret;
}

int isci_bus_reset_handler(struct scsi_cmnd *cmd)
{
	struct domain_device *dev = sdev_to_domain_dev(cmd->device);
	struct isci_host *ihost = dev_to_ihost(dev);
	struct isci_remote_device *idev;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ihost->scic_lock, flags);
	idev = isci_lookup_device(dev);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	if (!idev) {
		ret = TMF_RESP_FUNC_COMPLETE;
		goto out;
	}

	ret = isci_reset_device(ihost, idev);
 out:
	isci_put_device(idev);
	return ret;
}
