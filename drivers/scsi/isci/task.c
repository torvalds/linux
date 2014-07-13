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
		task->task_done(task);
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
		sas_task_abort(task);
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

					if (test_bit(IDEV_GONE, &idev->flags)) {

						/* Indicate that the device
						 * is gone.
						 */
						isci_task_refuse(ihost, task,
							SAS_TASK_UNDELIVERED,
							SAS_DEVICE_UNKNOWN);
					} else {
						/* Indicate QUEUE_FULL so that
						 * the scsi midlayer retries.
						 * If the request failed for
						 * remote device reasons, it
						 * gets returned as
						 * SAS_TASK_UNDELIVERED next
						 * time through.
						 */
						isci_task_refuse(ihost, task,
							SAS_TASK_COMPLETE,
							SAS_QUEUE_FULL);
					}
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

	return ireq;
}

/**
* isci_request_mark_zombie() - This function must be called with scic_lock held.
*/
static void isci_request_mark_zombie(struct isci_host *ihost, struct isci_request *ireq)
{
	struct completion *tmf_completion = NULL;
	struct completion *req_completion;

	/* Set the request state to "dead". */
	ireq->status = dead;

	req_completion = ireq->io_request_completion;
	ireq->io_request_completion = NULL;

	if (test_bit(IREQ_TMF, &ireq->flags)) {
		/* Break links with the TMF request. */
		struct isci_tmf *tmf = isci_request_access_tmf(ireq);

		/* In the case where a task request is dying,
		 * the thread waiting on the complete will sit and
		 * timeout unless we wake it now.  Since the TMF
		 * has a default error status, complete it here
		 * to wake the waiting thread.
		 */
		if (tmf) {
			tmf_completion = tmf->complete;
			tmf->complete = NULL;
		}
		ireq->ttype_ptr.tmf_task_ptr = NULL;
		dev_dbg(&ihost->pdev->dev, "%s: tmf_code %d, managed tag %#x\n",
			__func__, tmf->tmf_code, tmf->io_tag);
	} else {
		/* Break links with the sas_task - the callback is done
		 * elsewhere.
		 */
		struct sas_task *task = isci_request_access_task(ireq);

		if (task)
			task->lldd_task = NULL;

		ireq->ttype_ptr.io_task_ptr = NULL;
	}

	dev_warn(&ihost->pdev->dev, "task context unrecoverable (tag: %#x)\n",
		 ireq->io_tag);

	/* Don't force waiting threads to timeout. */
	if (req_completion)
		complete(req_completion);

	if (tmf_completion != NULL)
		complete(tmf_completion);
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
	tmf->status = SCI_FAILURE_TIMEOUT;

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
		/* The TMF did not complete - this could be because
		 * of an unplug.  Terminate the TMF request now.
		 */
		spin_lock_irqsave(&ihost->scic_lock, flags);

		if (tmf->cb_state_func != NULL)
			tmf->cb_state_func(isci_tmf_timed_out, tmf,
					   tmf->cb_data);

		sci_controller_terminate_request(ihost, idev, ireq);

		spin_unlock_irqrestore(&ihost->scic_lock, flags);

		timeleft = wait_for_completion_timeout(
			&completion,
			msecs_to_jiffies(ISCI_TERMINATION_TIMEOUT_MSEC));

		if (!timeleft) {
			/* Strange condition - the termination of the TMF
			 * request timed-out.
			 */
			spin_lock_irqsave(&ihost->scic_lock, flags);

			/* If the TMF status has not changed, kill it. */
			if (tmf->status == SCI_FAILURE_TIMEOUT)
				isci_request_mark_zombie(ihost, ireq);

			spin_unlock_irqrestore(&ihost->scic_lock, flags);
		}
	}

	isci_print_tmf(ihost, tmf);

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

static int isci_request_is_dealloc_managed(enum isci_request_status stat)
{
	switch (stat) {
	case aborted:
	case aborting:
	case terminating:
	case completed:
	case dead:
		return true;
	default:
		return false;
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
	unsigned long     flags;
	unsigned long     termination_completed = 1;
	struct completion *io_request_completion;

	dev_dbg(&ihost->pdev->dev,
		"%s: device = %p; request = %p\n",
		__func__, idev, isci_request);

	spin_lock_irqsave(&ihost->scic_lock, flags);

	io_request_completion = isci_request->io_request_completion;

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
			termination_completed
				= wait_for_completion_timeout(
				   io_request_completion,
				   msecs_to_jiffies(ISCI_TERMINATION_TIMEOUT_MSEC));

			if (!termination_completed) {

				/* The request to terminate has timed out.  */
				spin_lock_irqsave(&ihost->scic_lock, flags);

				/* Check for state changes. */
				if (!test_bit(IREQ_TERMINATED,
					      &isci_request->flags)) {

					/* The best we can do is to have the
					 * request die a silent death if it
					 * ever really completes.
					 */
					isci_request_mark_zombie(ihost,
								 isci_request);
					needs_cleanup_handling = true;
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

			needs_cleanup_handling
				= isci_request_is_dealloc_managed(
					isci_request->status);

			spin_unlock_irqrestore(&isci_request->state_lock, flags);

		}
		if (needs_cleanup_handling) {

			dev_dbg(&ihost->pdev->dev,
				"%s: cleanup isci_device=%p, request=%p\n",
				__func__, idev, isci_request);

			if (isci_request != NULL) {
				spin_lock_irqsave(&ihost->scic_lock, flags);
				isci_free_tag(ihost, isci_request->io_tag);
				isci_request_change_state(isci_request, unallocated);
				list_del_init(&isci_request->dev_node);
				spin_unlock_irqrestore(&ihost->scic_lock, flags);
			}
		}
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
			(!test_bit(IREQ_TMF, &ireq->flags)
				? isci_request_access_task(ireq)
				: NULL),
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

int isci_task_lu_reset(struct domain_device *dev, u8 *lun)
{
	struct isci_host *isci_host = dev_to_ihost(dev);
	struct isci_remote_device *isci_device;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&isci_host->scic_lock, flags);
	isci_device = isci_lookup_device(dev);
	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	dev_dbg(&isci_host->pdev->dev,
		"%s: domain_device=%p, isci_host=%p; isci_device=%p\n",
		 __func__, dev, isci_host, isci_device);

	if (!isci_device) {
		/* If the device is gone, stop the escalations. */
		dev_dbg(&isci_host->pdev->dev, "%s: No dev\n", __func__);

		ret = TMF_RESP_FUNC_COMPLETE;
		goto out;
	}

	/* Send the task management part of the reset. */
	if (dev_is_sata(dev)) {
		sas_ata_schedule_reset(dev);
		ret = TMF_RESP_FUNC_COMPLETE;
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
	int                       perform_termination = 0;
	int                       target_done_already = 0;

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
	    old_request) {
		isci_device = isci_lookup_device(task->dev);
		target_done_already = test_bit(IREQ_COMPLETE_IN_TARGET,
					       &old_request->flags);
	}
	spin_unlock(&task->task_state_lock);
	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	dev_dbg(&isci_host->pdev->dev,
		"%s: dev = %p, task = %p, old_request == %p\n",
		__func__, isci_device, task, old_request);

	/* Device reset conditions signalled in task_state_flags are the
	 * responsbility of libsas to observe at the start of the error
	 * handler thread.
	 */
	if (!isci_device || !old_request) {
		/* The request has already completed and there
		* is nothing to do here other than to set the task
		* done bit, and indicate that the task abort function
		* was sucessful.
		*/
		spin_lock_irqsave(&task->task_state_lock, flags);
		task->task_state_flags |= SAS_TASK_STATE_DONE;
		task->task_state_flags &= ~(SAS_TASK_AT_INITIATOR |
					    SAS_TASK_STATE_PENDING);
		spin_unlock_irqrestore(&task->task_state_lock, flags);

		ret = TMF_RESP_FUNC_COMPLETE;

		dev_dbg(&isci_host->pdev->dev,
			"%s: abort task not needed for %p\n",
			__func__, task);
		goto out;
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
	    sas_protocol_ata(task->task_proto) ||
	    target_done_already) {

		spin_unlock_irqrestore(&isci_host->scic_lock, flags);

		dev_dbg(&isci_host->pdev->dev,
			"%s: %s request"
			" or complete_in_target (%d), thus no TMF\n",
			__func__,
			((task->task_proto == SAS_PROTOCOL_SMP)
				? "SMP"
				: (sas_protocol_ata(task->task_proto)
					? "SATA/STP"
					: "<other>")
			 ),
			test_bit(IREQ_COMPLETE_IN_TARGET, &old_request->flags));

		if (test_bit(IREQ_COMPLETE_IN_TARGET, &old_request->flags)) {
			spin_lock_irqsave(&task->task_state_lock, flags);
			task->task_state_flags |= SAS_TASK_STATE_DONE;
			task->task_state_flags &= ~(SAS_TASK_AT_INITIATOR |
						    SAS_TASK_STATE_PENDING);
			spin_unlock_irqrestore(&task->task_state_lock, flags);
			ret = TMF_RESP_FUNC_COMPLETE;
		} else {
			spin_lock_irqsave(&task->task_state_lock, flags);
			task->task_state_flags &= ~(SAS_TASK_AT_INITIATOR |
						    SAS_TASK_STATE_PENDING);
			spin_unlock_irqrestore(&task->task_state_lock, flags);
		}

		/* STP and SMP devices are not sent a TMF, but the
		 * outstanding I/O request is terminated below.  This is
		 * because SATA/STP and SMP discovery path timeouts directly
		 * call the abort task interface for cleanup.
		 */
		perform_termination = 1;

	} else {
		/* Fill in the tmf stucture */
		isci_task_build_abort_task_tmf(&tmf, isci_tmf_ssp_task_abort,
					       isci_abort_task_process_cb,
					       old_request);

		spin_unlock_irqrestore(&isci_host->scic_lock, flags);

		#define ISCI_ABORT_TASK_TIMEOUT_MS 500 /* 1/2 second timeout */
		ret = isci_task_execute_tmf(isci_host, isci_device, &tmf,
					    ISCI_ABORT_TASK_TIMEOUT_MS);

		if (ret == TMF_RESP_FUNC_COMPLETE)
			perform_termination = 1;
		else
			dev_dbg(&isci_host->pdev->dev,
				"%s: isci_task_send_tmf failed\n", __func__);
	}
	if (perform_termination) {
		set_bit(IREQ_COMPLETE_IN_TARGET, &old_request->flags);

		/* Clean up the request on our side, and wait for the aborted
		 * I/O to complete.
		 */
		isci_terminate_request_core(isci_host, isci_device,
					    old_request);
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
	struct completion *tmf_complete = NULL;
	struct completion *request_complete = ireq->io_request_completion;

	dev_dbg(&ihost->pdev->dev,
		"%s: request = %p, status=%d\n",
		__func__, ireq, completion_status);

	isci_request_change_state(ireq, completed);

	set_bit(IREQ_COMPLETE_IN_TARGET, &ireq->flags);

	if (tmf) {
		tmf->status = completion_status;

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
	}
	sci_controller_complete_io(ihost, ireq->target_device, ireq);
	/* set the 'terminated' flag handle to make sure it cannot be terminated
	 *  or completed again.
	 */
	set_bit(IREQ_TERMINATED, &ireq->flags);

	/* As soon as something is in the terminate path, deallocation is
	 * managed there.  Note that the final non-managed state of a task
	 * request is "completed".
	 */
	if ((ireq->status == completed) ||
	    !isci_request_is_dealloc_managed(ireq->status)) {
		isci_request_change_state(ireq, unallocated);
		isci_free_tag(ihost, ireq->io_tag);
		list_del_init(&ireq->dev_node);
	}

	/* "request_complete" is set if the task was being terminated. */
	if (request_complete)
		complete(request_complete);

	/* The task management part completes last. */
	if (tmf_complete)
		complete(tmf_complete);
}

static int isci_reset_device(struct isci_host *ihost,
			     struct domain_device *dev,
			     struct isci_remote_device *idev)
{
	int rc;
	unsigned long flags;
	enum sci_status status;
	struct sas_phy *phy = sas_get_local_phy(dev);
	struct isci_port *iport = dev->port->lldd_port;

	dev_dbg(&ihost->pdev->dev, "%s: idev %p\n", __func__, idev);

	spin_lock_irqsave(&ihost->scic_lock, flags);
	status = sci_remote_device_reset(idev);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	if (status != SCI_SUCCESS) {
		dev_dbg(&ihost->pdev->dev,
			 "%s: sci_remote_device_reset(%p) returned %d!\n",
			 __func__, idev, status);
		rc = TMF_RESP_FUNC_FAILED;
		goto out;
	}

	if (scsi_is_sas_phy_local(phy)) {
		struct isci_phy *iphy = &ihost->phys[phy->number];

		rc = isci_port_perform_hard_reset(ihost, iport, iphy);
	} else
		rc = sas_phy_reset(phy, !dev_is_sata(dev));

	/* Terminate in-progress I/O now. */
	isci_remote_device_nuke_requests(ihost, idev);

	/* Since all pending TCs have been cleaned, resume the RNC. */
	spin_lock_irqsave(&ihost->scic_lock, flags);
	status = sci_remote_device_reset_complete(idev);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	if (status != SCI_SUCCESS) {
		dev_dbg(&ihost->pdev->dev,
			 "%s: sci_remote_device_reset_complete(%p) "
			 "returned %d!\n", __func__, idev, status);
	}

	dev_dbg(&ihost->pdev->dev, "%s: idev %p complete.\n", __func__, idev);
 out:
	sas_put_local_phy(phy);
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

	if (!idev) {
		/* XXX: need to cleanup any ireqs targeting this
		 * domain_device
		 */
		ret = -ENODEV;
		goto out;
	}

	ret = isci_reset_device(ihost, dev, idev);
 out:
	isci_put_device(idev);
	return ret;
}
