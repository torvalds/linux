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
	unsigned long flags;

	/* Normal notification (task_done) */
	dev_dbg(&ihost->pdev->dev, "%s: task = %p, response=%d, status=%d\n",
		__func__, task, response, status);

	spin_lock_irqsave(&task->task_state_lock, flags);

	task->task_status.resp = response;
	task->task_status.stat = status;

	/* Normal notification (task_done) */
	task->task_state_flags |= SAS_TASK_STATE_DONE;
	task->task_state_flags &= ~SAS_TASK_STATE_PENDING;
	task->lldd_task = NULL;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	task->task_done(task);
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
 * @gfp_flags: This parameter specifies the context of this call.
 *
 * status, zero indicates success.
 */
int isci_task_execute_task(struct sas_task *task, gfp_t gfp_flags)
{
	struct isci_host *ihost = dev_to_ihost(task->dev);
	struct isci_remote_device *idev;
	unsigned long flags;
	enum sci_status status = SCI_FAILURE;
	bool io_ready;
	u16 tag;

	spin_lock_irqsave(&ihost->scic_lock, flags);
	idev = isci_lookup_device(task->dev);
	io_ready = isci_device_io_ready(idev, task);
	tag = isci_alloc_tag(ihost);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	dev_dbg(&ihost->pdev->dev,
		"task: %p, dev: %p idev: %p:%#lx cmd = %p\n",
		task, task->dev, idev, idev ? idev->flags : 0,
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
			spin_unlock_irqrestore(&task->task_state_lock, flags);

			isci_task_refuse(ihost, task,
					 SAS_TASK_UNDELIVERED,
					 SAS_SAM_STAT_TASK_ABORTED);
		} else {
			struct isci_request *ireq;

			/* do common allocation and init of request object. */
			ireq = isci_io_request_from_tag(ihost, task, tag);
			spin_unlock_irqrestore(&task->task_state_lock, flags);

			/* build and send the request. */
			/* do common allocation and init of request object. */
			status = isci_request_execute(ihost, idev, task, ireq);

			if (status != SCI_SUCCESS) {
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
	if (dev->dev_type == SAS_END_DEVICE) {
		isci_tmf->proto = SAS_PROTOCOL_SSP;
		status = sci_task_request_construct_ssp(ireq);
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
	enum sci_status status = SCI_FAILURE;
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

	if (status != SCI_SUCCESS) {
		dev_dbg(&ihost->pdev->dev,
			 "%s: start_io failed - status = 0x%x, request = %p\n",
			 __func__,
			 status,
			 ireq);
		spin_unlock_irqrestore(&ihost->scic_lock, flags);
		goto err_tci;
	}
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/* The RNC must be unsuspended before the TMF can get a response. */
	isci_remote_device_resume_from_abort(ihost, idev);

	/* Wait for the TMF to complete, or a timeout. */
	timeleft = wait_for_completion_timeout(&completion,
					       msecs_to_jiffies(timeout_ms));

	if (timeleft == 0) {
		/* The TMF did not complete - this could be because
		 * of an unplug.  Terminate the TMF request now.
		 */
		isci_remote_device_suspend_terminate(ihost, idev, ireq);
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
				enum isci_tmf_function_codes code)
{
	memset(tmf, 0, sizeof(*tmf));
	tmf->tmf_code = code;
}

static void isci_task_build_abort_task_tmf(struct isci_tmf *tmf,
					   enum isci_tmf_function_codes code,
					   struct isci_request *old_request)
{
	isci_task_build_tmf(tmf, code);
	tmf->io_tag = old_request->io_tag;
}

/*
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
	isci_task_build_tmf(&tmf, isci_tmf_ssp_lun_reset);

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
	struct isci_host *ihost = dev_to_ihost(dev);
	struct isci_remote_device *idev;
	unsigned long flags;
	int ret = TMF_RESP_FUNC_COMPLETE;

	spin_lock_irqsave(&ihost->scic_lock, flags);
	idev = isci_get_device(dev->lldd_dev);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	dev_dbg(&ihost->pdev->dev,
		"%s: domain_device=%p, isci_host=%p; isci_device=%p\n",
		__func__, dev, ihost, idev);

	if (!idev) {
		/* If the device is gone, escalate to I_T_Nexus_Reset. */
		dev_dbg(&ihost->pdev->dev, "%s: No dev\n", __func__);

		ret = TMF_RESP_FUNC_FAILED;
		goto out;
	}

	/* Suspend the RNC, kill all TCs */
	if (isci_remote_device_suspend_terminate(ihost, idev, NULL)
	    != SCI_SUCCESS) {
		/* The suspend/terminate only fails if isci_get_device fails */
		ret = TMF_RESP_FUNC_FAILED;
		goto out;
	}
	/* All pending I/Os have been terminated and cleaned up. */
	if (!test_bit(IDEV_GONE, &idev->flags)) {
		if (dev_is_sata(dev))
			sas_ata_schedule_reset(dev);
		else
			/* Send the task management part of the reset. */
			ret = isci_task_send_lu_reset_sas(ihost, idev, lun);
	}
 out:
	isci_put_device(idev);
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
 * isci_task_abort_task() - This function is one of the SAS Domain Template
 *    functions. This function is called by libsas to abort a specified task.
 * @task: This parameter specifies the SAS task to abort.
 *
 * status, zero indicates success.
 */
int isci_task_abort_task(struct sas_task *task)
{
	struct isci_host *ihost = dev_to_ihost(task->dev);
	DECLARE_COMPLETION_ONSTACK(aborted_io_completion);
	struct isci_request       *old_request = NULL;
	struct isci_remote_device *idev = NULL;
	struct isci_tmf           tmf;
	int                       ret = TMF_RESP_FUNC_FAILED;
	unsigned long             flags;
	int                       target_done_already = 0;

	/* Get the isci_request reference from the task.  Note that
	 * this check does not depend on the pending request list
	 * in the device, because tasks driving resets may land here
	 * after completion in the core.
	 */
	spin_lock_irqsave(&ihost->scic_lock, flags);
	spin_lock(&task->task_state_lock);

	old_request = task->lldd_task;

	/* If task is already done, the request isn't valid */
	if (!(task->task_state_flags & SAS_TASK_STATE_DONE) &&
	    old_request) {
		idev = isci_get_device(task->dev->lldd_dev);
		target_done_already = test_bit(IREQ_COMPLETE_IN_TARGET,
					       &old_request->flags);
	}
	spin_unlock(&task->task_state_lock);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	dev_warn(&ihost->pdev->dev,
		 "%s: dev = %p (%s%s), task = %p, old_request == %p\n",
		 __func__, idev,
		 (dev_is_sata(task->dev) ? "STP/SATA"
					 : ((dev_is_expander(task->dev->dev_type))
						? "SMP"
						: "SSP")),
		 ((idev) ? ((test_bit(IDEV_GONE, &idev->flags))
			   ? " IDEV_GONE"
			   : "")
			 : " <NULL>"),
		 task, old_request);

	/* Device reset conditions signalled in task_state_flags are the
	 * responsbility of libsas to observe at the start of the error
	 * handler thread.
	 */
	if (!idev || !old_request) {
		/* The request has already completed and there
		* is nothing to do here other than to set the task
		* done bit, and indicate that the task abort function
		* was successful.
		*/
		spin_lock_irqsave(&task->task_state_lock, flags);
		task->task_state_flags |= SAS_TASK_STATE_DONE;
		task->task_state_flags &= ~SAS_TASK_STATE_PENDING;
		spin_unlock_irqrestore(&task->task_state_lock, flags);

		ret = TMF_RESP_FUNC_COMPLETE;

		dev_warn(&ihost->pdev->dev,
			 "%s: abort task not needed for %p\n",
			 __func__, task);
		goto out;
	}
	/* Suspend the RNC, kill the TC */
	if (isci_remote_device_suspend_terminate(ihost, idev, old_request)
	    != SCI_SUCCESS) {
		dev_warn(&ihost->pdev->dev,
			 "%s: isci_remote_device_reset_terminate(dev=%p, "
				 "req=%p, task=%p) failed\n",
			 __func__, idev, old_request, task);
		ret = TMF_RESP_FUNC_FAILED;
		goto out;
	}
	spin_lock_irqsave(&ihost->scic_lock, flags);

	if (task->task_proto == SAS_PROTOCOL_SMP ||
	    sas_protocol_ata(task->task_proto) ||
	    target_done_already ||
	    test_bit(IDEV_GONE, &idev->flags)) {

		spin_unlock_irqrestore(&ihost->scic_lock, flags);

		/* No task to send, so explicitly resume the device here */
		isci_remote_device_resume_from_abort(ihost, idev);

		dev_warn(&ihost->pdev->dev,
			 "%s: %s request"
				 " or complete_in_target (%d), "
				 "or IDEV_GONE (%d), thus no TMF\n",
			 __func__,
			 ((task->task_proto == SAS_PROTOCOL_SMP)
			  ? "SMP"
			  : (sas_protocol_ata(task->task_proto)
				? "SATA/STP"
				: "<other>")
			  ),
			 test_bit(IREQ_COMPLETE_IN_TARGET,
				  &old_request->flags),
			 test_bit(IDEV_GONE, &idev->flags));

		spin_lock_irqsave(&task->task_state_lock, flags);
		task->task_state_flags &= ~SAS_TASK_STATE_PENDING;
		task->task_state_flags |= SAS_TASK_STATE_DONE;
		spin_unlock_irqrestore(&task->task_state_lock, flags);

		ret = TMF_RESP_FUNC_COMPLETE;
	} else {
		/* Fill in the tmf structure */
		isci_task_build_abort_task_tmf(&tmf, isci_tmf_ssp_task_abort,
					       old_request);

		spin_unlock_irqrestore(&ihost->scic_lock, flags);

		/* Send the task management request. */
		#define ISCI_ABORT_TASK_TIMEOUT_MS 500 /* 1/2 second timeout */
		ret = isci_task_execute_tmf(ihost, idev, &tmf,
					    ISCI_ABORT_TASK_TIMEOUT_MS);
	}
out:
	dev_warn(&ihost->pdev->dev,
		 "%s: Done; dev = %p, task = %p , old_request == %p\n",
		 __func__, idev, task, old_request);
	isci_put_device(idev);
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

	dev_dbg(&ihost->pdev->dev,
		"%s: request = %p, status=%d\n",
		__func__, ireq, completion_status);

	set_bit(IREQ_COMPLETE_IN_TARGET, &ireq->flags);

	if (tmf) {
		tmf->status = completion_status;

		if (tmf->proto == SAS_PROTOCOL_SSP) {
			memcpy(tmf->resp.rsp_buf,
			       ireq->ssp.rsp_buf,
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

	if (test_and_clear_bit(IREQ_ABORT_PATH_ACTIVE, &ireq->flags))
		wake_up_all(&ihost->eventq);

	if (!test_bit(IREQ_NO_AUTO_FREE_TAG, &ireq->flags))
		isci_free_tag(ihost, ireq->io_tag);

	/* The task management part completes last. */
	if (tmf_complete)
		complete(tmf_complete);
}

static int isci_reset_device(struct isci_host *ihost,
			     struct domain_device *dev,
			     struct isci_remote_device *idev)
{
	int rc = TMF_RESP_FUNC_COMPLETE, reset_stat = -1;
	struct sas_phy *phy = sas_get_local_phy(dev);
	struct isci_port *iport = dev->port->lldd_port;

	dev_dbg(&ihost->pdev->dev, "%s: idev %p\n", __func__, idev);

	/* Suspend the RNC, terminate all outstanding TCs. */
	if (isci_remote_device_suspend_terminate(ihost, idev, NULL)
	    != SCI_SUCCESS) {
		rc = TMF_RESP_FUNC_FAILED;
		goto out;
	}
	/* Note that since the termination for outstanding requests succeeded,
	 * this function will return success.  This is because the resets will
	 * only fail if the device has been removed (ie. hotplug), and the
	 * primary duty of this function is to cleanup tasks, so that is the
	 * relevant status.
	 */
	if (!test_bit(IDEV_GONE, &idev->flags)) {
		if (scsi_is_sas_phy_local(phy)) {
			struct isci_phy *iphy = &ihost->phys[phy->number];

			reset_stat = isci_port_perform_hard_reset(ihost, iport,
								  iphy);
		} else
			reset_stat = sas_phy_reset(phy, !dev_is_sata(dev));
	}
	/* Explicitly resume the RNC here, since there was no task sent. */
	isci_remote_device_resume_from_abort(ihost, idev);

	dev_dbg(&ihost->pdev->dev, "%s: idev %p complete, reset_stat=%d.\n",
		__func__, idev, reset_stat);
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
	idev = isci_get_device(dev->lldd_dev);
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
