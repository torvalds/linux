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
#include <linux/bitops.h>
#include "isci.h"
#include "port.h"
#include "remote_device.h"
#include "request.h"
#include "remote_node_context.h"
#include "scu_event_codes.h"
#include "task.h"

#undef C
#define C(a) (#a)
const char *dev_state_name(enum sci_remote_device_states state)
{
	static const char * const strings[] = REMOTE_DEV_STATES;

	return strings[state];
}
#undef C

static enum sci_status sci_remote_device_suspend(struct isci_remote_device *idev,
						 enum sci_remote_node_suspension_reasons reason)
{
	return sci_remote_node_context_suspend(&idev->rnc, reason,
					       SCI_SOFTWARE_SUSPEND_EXPECTED_EVENT);
}

/**
 * isci_remote_device_ready() - This function is called by the ihost when the
 *    remote device is ready. We mark the isci device as ready and signal the
 *    waiting proccess.
 * @ihost: our valid isci_host
 * @idev: remote device
 *
 */
static void isci_remote_device_ready(struct isci_host *ihost, struct isci_remote_device *idev)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p\n", __func__, idev);

	clear_bit(IDEV_IO_NCQERROR, &idev->flags);
	set_bit(IDEV_IO_READY, &idev->flags);
	if (test_and_clear_bit(IDEV_START_PENDING, &idev->flags))
		wake_up(&ihost->eventq);
}

static enum sci_status sci_remote_device_terminate_req(
	struct isci_host *ihost,
	struct isci_remote_device *idev,
	int check_abort,
	struct isci_request *ireq)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: idev=%p; flags=%lx; req=%p; req target=%p\n",
		__func__, idev, idev->flags, ireq, ireq->target_device);

	if (!test_bit(IREQ_ACTIVE, &ireq->flags) ||
	    (ireq->target_device != idev) ||
	    (check_abort && !test_bit(IREQ_PENDING_ABORT, &ireq->flags)))
		return SCI_SUCCESS;

	set_bit(IREQ_ABORT_PATH_ACTIVE, &ireq->flags);

	return sci_controller_terminate_request(ihost, idev, ireq);
}

static enum sci_status sci_remote_device_terminate_reqs_checkabort(
	struct isci_remote_device *idev,
	int chk)
{
	struct isci_host *ihost = idev->owning_port->owning_controller;
	enum sci_status status  = SCI_SUCCESS;
	u32 i;

	for (i = 0; i < SCI_MAX_IO_REQUESTS; i++) {
		struct isci_request *ireq = ihost->reqs[i];
		enum sci_status s;

		s = sci_remote_device_terminate_req(ihost, idev, chk, ireq);
		if (s != SCI_SUCCESS)
			status = s;
	}
	return status;
}

enum sci_status isci_remote_device_terminate_requests(
	struct isci_host *ihost,
	struct isci_remote_device *idev,
	struct isci_request *ireq)
{
	enum sci_status status = SCI_SUCCESS;
	unsigned long flags;

	spin_lock_irqsave(&ihost->scic_lock, flags);
	if (isci_get_device(idev) == NULL) {
		dev_dbg(&ihost->pdev->dev, "%s: failed isci_get_device(idev=%p)\n",
			__func__, idev);
		spin_unlock_irqrestore(&ihost->scic_lock, flags);
		status = SCI_FAILURE;
	} else {
		dev_dbg(&ihost->pdev->dev,
			"%s: idev=%p, ireq=%p; started_request_count=%d, "
				"about to wait\n",
			__func__, idev, ireq, idev->started_request_count);
		if (ireq) {
			/* Terminate a specific TC. */
			sci_remote_device_terminate_req(ihost, idev, 0, ireq);
			spin_unlock_irqrestore(&ihost->scic_lock, flags);
			wait_event(ihost->eventq, !test_bit(IREQ_ACTIVE,
							    &ireq->flags));

		} else {
			/* Terminate all TCs. */
			sci_remote_device_terminate_requests(idev);
			spin_unlock_irqrestore(&ihost->scic_lock, flags);
			wait_event(ihost->eventq,
				   idev->started_request_count == 0);
		}
		dev_dbg(&ihost->pdev->dev, "%s: idev=%p, wait done\n",
			__func__, idev);
		isci_put_device(idev);
	}
	return status;
}

/**
* isci_remote_device_not_ready() - This function is called by the ihost when
*    the remote device is not ready. We mark the isci device as ready (not
*    "ready_for_io") and signal the waiting proccess.
* @isci_host: This parameter specifies the isci host object.
* @isci_device: This parameter specifies the remote device
*
* sci_lock is held on entrance to this function.
*/
static void isci_remote_device_not_ready(struct isci_host *ihost,
					 struct isci_remote_device *idev,
					 u32 reason)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p; reason = %d\n", __func__, idev, reason);

	switch (reason) {
	case SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED:
		set_bit(IDEV_IO_NCQERROR, &idev->flags);

		/* Suspend the remote device so the I/O can be terminated. */
		sci_remote_device_suspend(idev, SCI_SW_SUSPEND_NORMAL);

		/* Kill all outstanding requests for the device. */
		sci_remote_device_terminate_requests(idev);

		/* Fall through into the default case... */
	default:
		clear_bit(IDEV_IO_READY, &idev->flags);
		break;
	}
}

/* called once the remote node context is ready to be freed.
 * The remote device can now report that its stop operation is complete. none
 */
static void rnc_destruct_done(void *_dev)
{
	struct isci_remote_device *idev = _dev;

	BUG_ON(idev->started_request_count != 0);
	sci_change_state(&idev->sm, SCI_DEV_STOPPED);
}

enum sci_status sci_remote_device_terminate_requests(
	struct isci_remote_device *idev)
{
	return sci_remote_device_terminate_reqs_checkabort(idev, 0);
}

enum sci_status sci_remote_device_stop(struct isci_remote_device *idev,
					u32 timeout)
{
	struct sci_base_state_machine *sm = &idev->sm;
	enum sci_remote_device_states state = sm->current_state_id;

	switch (state) {
	case SCI_DEV_INITIAL:
	case SCI_DEV_FAILED:
	case SCI_DEV_FINAL:
	default:
		dev_warn(scirdev_to_dev(idev), "%s: in wrong state: %s\n",
			 __func__, dev_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	case SCI_DEV_STOPPED:
		return SCI_SUCCESS;
	case SCI_DEV_STARTING:
		/* device not started so there had better be no requests */
		BUG_ON(idev->started_request_count != 0);
		sci_remote_node_context_destruct(&idev->rnc,
						      rnc_destruct_done, idev);
		/* Transition to the stopping state and wait for the
		 * remote node to complete being posted and invalidated.
		 */
		sci_change_state(sm, SCI_DEV_STOPPING);
		return SCI_SUCCESS;
	case SCI_DEV_READY:
	case SCI_STP_DEV_IDLE:
	case SCI_STP_DEV_CMD:
	case SCI_STP_DEV_NCQ:
	case SCI_STP_DEV_NCQ_ERROR:
	case SCI_STP_DEV_AWAIT_RESET:
	case SCI_SMP_DEV_IDLE:
	case SCI_SMP_DEV_CMD:
		sci_change_state(sm, SCI_DEV_STOPPING);
		if (idev->started_request_count == 0)
			sci_remote_node_context_destruct(&idev->rnc,
							 rnc_destruct_done,
							 idev);
		else {
			sci_remote_device_suspend(
				idev, SCI_SW_SUSPEND_LINKHANG_DETECT);
			sci_remote_device_terminate_requests(idev);
		}
		return SCI_SUCCESS;
	case SCI_DEV_STOPPING:
		/* All requests should have been terminated, but if there is an
		 * attempt to stop a device already in the stopping state, then
		 * try again to terminate.
		 */
		return sci_remote_device_terminate_requests(idev);
	case SCI_DEV_RESETTING:
		sci_change_state(sm, SCI_DEV_STOPPING);
		return SCI_SUCCESS;
	}
}

enum sci_status sci_remote_device_reset(struct isci_remote_device *idev)
{
	struct sci_base_state_machine *sm = &idev->sm;
	enum sci_remote_device_states state = sm->current_state_id;

	switch (state) {
	case SCI_DEV_INITIAL:
	case SCI_DEV_STOPPED:
	case SCI_DEV_STARTING:
	case SCI_SMP_DEV_IDLE:
	case SCI_SMP_DEV_CMD:
	case SCI_DEV_STOPPING:
	case SCI_DEV_FAILED:
	case SCI_DEV_RESETTING:
	case SCI_DEV_FINAL:
	default:
		dev_warn(scirdev_to_dev(idev), "%s: in wrong state: %s\n",
			 __func__, dev_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	case SCI_DEV_READY:
	case SCI_STP_DEV_IDLE:
	case SCI_STP_DEV_CMD:
	case SCI_STP_DEV_NCQ:
	case SCI_STP_DEV_NCQ_ERROR:
	case SCI_STP_DEV_AWAIT_RESET:
		sci_change_state(sm, SCI_DEV_RESETTING);
		return SCI_SUCCESS;
	}
}

enum sci_status sci_remote_device_reset_complete(struct isci_remote_device *idev)
{
	struct sci_base_state_machine *sm = &idev->sm;
	enum sci_remote_device_states state = sm->current_state_id;

	if (state != SCI_DEV_RESETTING) {
		dev_warn(scirdev_to_dev(idev), "%s: in wrong state: %s\n",
			 __func__, dev_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}

	sci_change_state(sm, SCI_DEV_READY);
	return SCI_SUCCESS;
}

enum sci_status sci_remote_device_frame_handler(struct isci_remote_device *idev,
						     u32 frame_index)
{
	struct sci_base_state_machine *sm = &idev->sm;
	enum sci_remote_device_states state = sm->current_state_id;
	struct isci_host *ihost = idev->owning_port->owning_controller;
	enum sci_status status;

	switch (state) {
	case SCI_DEV_INITIAL:
	case SCI_DEV_STOPPED:
	case SCI_DEV_STARTING:
	case SCI_STP_DEV_IDLE:
	case SCI_SMP_DEV_IDLE:
	case SCI_DEV_FINAL:
	default:
		dev_warn(scirdev_to_dev(idev), "%s: in wrong state: %s\n",
			 __func__, dev_state_name(state));
		/* Return the frame back to the controller */
		sci_controller_release_frame(ihost, frame_index);
		return SCI_FAILURE_INVALID_STATE;
	case SCI_DEV_READY:
	case SCI_STP_DEV_NCQ_ERROR:
	case SCI_STP_DEV_AWAIT_RESET:
	case SCI_DEV_STOPPING:
	case SCI_DEV_FAILED:
	case SCI_DEV_RESETTING: {
		struct isci_request *ireq;
		struct ssp_frame_hdr hdr;
		void *frame_header;
		ssize_t word_cnt;

		status = sci_unsolicited_frame_control_get_header(&ihost->uf_control,
								       frame_index,
								       &frame_header);
		if (status != SCI_SUCCESS)
			return status;

		word_cnt = sizeof(hdr) / sizeof(u32);
		sci_swab32_cpy(&hdr, frame_header, word_cnt);

		ireq = sci_request_by_tag(ihost, be16_to_cpu(hdr.tag));
		if (ireq && ireq->target_device == idev) {
			/* The IO request is now in charge of releasing the frame */
			status = sci_io_request_frame_handler(ireq, frame_index);
		} else {
			/* We could not map this tag to a valid IO
			 * request Just toss the frame and continue
			 */
			sci_controller_release_frame(ihost, frame_index);
		}
		break;
	}
	case SCI_STP_DEV_NCQ: {
		struct dev_to_host_fis *hdr;

		status = sci_unsolicited_frame_control_get_header(&ihost->uf_control,
								       frame_index,
								       (void **)&hdr);
		if (status != SCI_SUCCESS)
			return status;

		if (hdr->fis_type == FIS_SETDEVBITS &&
		    (hdr->status & ATA_ERR)) {
			idev->not_ready_reason = SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED;

			/* TODO Check sactive and complete associated IO if any. */
			sci_change_state(sm, SCI_STP_DEV_NCQ_ERROR);
		} else if (hdr->fis_type == FIS_REGD2H &&
			   (hdr->status & ATA_ERR)) {
			/*
			 * Some devices return D2H FIS when an NCQ error is detected.
			 * Treat this like an SDB error FIS ready reason.
			 */
			idev->not_ready_reason = SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED;
			sci_change_state(&idev->sm, SCI_STP_DEV_NCQ_ERROR);
		} else
			status = SCI_FAILURE;

		sci_controller_release_frame(ihost, frame_index);
		break;
	}
	case SCI_STP_DEV_CMD:
	case SCI_SMP_DEV_CMD:
		/* The device does not process any UF received from the hardware while
		 * in this state.  All unsolicited frames are forwarded to the io request
		 * object.
		 */
		status = sci_io_request_frame_handler(idev->working_request, frame_index);
		break;
	}

	return status;
}

static bool is_remote_device_ready(struct isci_remote_device *idev)
{

	struct sci_base_state_machine *sm = &idev->sm;
	enum sci_remote_device_states state = sm->current_state_id;

	switch (state) {
	case SCI_DEV_READY:
	case SCI_STP_DEV_IDLE:
	case SCI_STP_DEV_CMD:
	case SCI_STP_DEV_NCQ:
	case SCI_STP_DEV_NCQ_ERROR:
	case SCI_STP_DEV_AWAIT_RESET:
	case SCI_SMP_DEV_IDLE:
	case SCI_SMP_DEV_CMD:
		return true;
	default:
		return false;
	}
}

/*
 * called once the remote node context has transisitioned to a ready
 * state (after suspending RX and/or TX due to early D2H fis)
 */
static void atapi_remote_device_resume_done(void *_dev)
{
	struct isci_remote_device *idev = _dev;
	struct isci_request *ireq = idev->working_request;

	sci_change_state(&ireq->sm, SCI_REQ_COMPLETED);
}

enum sci_status sci_remote_device_event_handler(struct isci_remote_device *idev,
						     u32 event_code)
{
	enum sci_status status;

	switch (scu_get_event_type(event_code)) {
	case SCU_EVENT_TYPE_RNC_OPS_MISC:
	case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
	case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
		status = sci_remote_node_context_event_handler(&idev->rnc, event_code);
		break;
	case SCU_EVENT_TYPE_PTX_SCHEDULE_EVENT:
		if (scu_get_event_code(event_code) == SCU_EVENT_IT_NEXUS_TIMEOUT) {
			status = SCI_SUCCESS;

			/* Suspend the associated RNC */
			sci_remote_device_suspend(idev, SCI_SW_SUSPEND_NORMAL);

			dev_dbg(scirdev_to_dev(idev),
				"%s: device: %p event code: %x: %s\n",
				__func__, idev, event_code,
				is_remote_device_ready(idev)
				? "I_T_Nexus_Timeout event"
				: "I_T_Nexus_Timeout event in wrong state");

			break;
		}
	/* Else, fall through and treat as unhandled... */
	default:
		dev_dbg(scirdev_to_dev(idev),
			"%s: device: %p event code: %x: %s\n",
			__func__, idev, event_code,
			is_remote_device_ready(idev)
			? "unexpected event"
			: "unexpected event in wrong state");
		status = SCI_FAILURE_INVALID_STATE;
		break;
	}

	if (status != SCI_SUCCESS)
		return status;

	return status;
}

static void sci_remote_device_start_request(struct isci_remote_device *idev,
						 struct isci_request *ireq,
						 enum sci_status status)
{
	struct isci_port *iport = idev->owning_port;

	/* cleanup requests that failed after starting on the port */
	if (status != SCI_SUCCESS)
		sci_port_complete_io(iport, idev, ireq);
	else {
		kref_get(&idev->kref);
		idev->started_request_count++;
	}
}

enum sci_status sci_remote_device_start_io(struct isci_host *ihost,
						struct isci_remote_device *idev,
						struct isci_request *ireq)
{
	struct sci_base_state_machine *sm = &idev->sm;
	enum sci_remote_device_states state = sm->current_state_id;
	struct isci_port *iport = idev->owning_port;
	enum sci_status status;

	switch (state) {
	case SCI_DEV_INITIAL:
	case SCI_DEV_STOPPED:
	case SCI_DEV_STARTING:
	case SCI_STP_DEV_NCQ_ERROR:
	case SCI_DEV_STOPPING:
	case SCI_DEV_FAILED:
	case SCI_DEV_RESETTING:
	case SCI_DEV_FINAL:
	default:
		dev_warn(scirdev_to_dev(idev), "%s: in wrong state: %s\n",
			 __func__, dev_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	case SCI_DEV_READY:
		/* attempt to start an io request for this device object. The remote
		 * device object will issue the start request for the io and if
		 * successful it will start the request for the port object then
		 * increment its own request count.
		 */
		status = sci_port_start_io(iport, idev, ireq);
		if (status != SCI_SUCCESS)
			return status;

		status = sci_remote_node_context_start_io(&idev->rnc, ireq);
		if (status != SCI_SUCCESS)
			break;

		status = sci_request_start(ireq);
		break;
	case SCI_STP_DEV_IDLE: {
		/* handle the start io operation for a sata device that is in
		 * the command idle state. - Evalute the type of IO request to
		 * be started - If its an NCQ request change to NCQ substate -
		 * If its any other command change to the CMD substate
		 *
		 * If this is a softreset we may want to have a different
		 * substate.
		 */
		enum sci_remote_device_states new_state;
		struct sas_task *task = isci_request_access_task(ireq);

		status = sci_port_start_io(iport, idev, ireq);
		if (status != SCI_SUCCESS)
			return status;

		status = sci_remote_node_context_start_io(&idev->rnc, ireq);
		if (status != SCI_SUCCESS)
			break;

		status = sci_request_start(ireq);
		if (status != SCI_SUCCESS)
			break;

		if (task->ata_task.use_ncq)
			new_state = SCI_STP_DEV_NCQ;
		else {
			idev->working_request = ireq;
			new_state = SCI_STP_DEV_CMD;
		}
		sci_change_state(sm, new_state);
		break;
	}
	case SCI_STP_DEV_NCQ: {
		struct sas_task *task = isci_request_access_task(ireq);

		if (task->ata_task.use_ncq) {
			status = sci_port_start_io(iport, idev, ireq);
			if (status != SCI_SUCCESS)
				return status;

			status = sci_remote_node_context_start_io(&idev->rnc, ireq);
			if (status != SCI_SUCCESS)
				break;

			status = sci_request_start(ireq);
		} else
			return SCI_FAILURE_INVALID_STATE;
		break;
	}
	case SCI_STP_DEV_AWAIT_RESET:
		return SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED;
	case SCI_SMP_DEV_IDLE:
		status = sci_port_start_io(iport, idev, ireq);
		if (status != SCI_SUCCESS)
			return status;

		status = sci_remote_node_context_start_io(&idev->rnc, ireq);
		if (status != SCI_SUCCESS)
			break;

		status = sci_request_start(ireq);
		if (status != SCI_SUCCESS)
			break;

		idev->working_request = ireq;
		sci_change_state(&idev->sm, SCI_SMP_DEV_CMD);
		break;
	case SCI_STP_DEV_CMD:
	case SCI_SMP_DEV_CMD:
		/* device is already handling a command it can not accept new commands
		 * until this one is complete.
		 */
		return SCI_FAILURE_INVALID_STATE;
	}

	sci_remote_device_start_request(idev, ireq, status);
	return status;
}

static enum sci_status common_complete_io(struct isci_port *iport,
					  struct isci_remote_device *idev,
					  struct isci_request *ireq)
{
	enum sci_status status;

	status = sci_request_complete(ireq);
	if (status != SCI_SUCCESS)
		return status;

	status = sci_port_complete_io(iport, idev, ireq);
	if (status != SCI_SUCCESS)
		return status;

	sci_remote_device_decrement_request_count(idev);
	return status;
}

enum sci_status sci_remote_device_complete_io(struct isci_host *ihost,
						   struct isci_remote_device *idev,
						   struct isci_request *ireq)
{
	struct sci_base_state_machine *sm = &idev->sm;
	enum sci_remote_device_states state = sm->current_state_id;
	struct isci_port *iport = idev->owning_port;
	enum sci_status status;

	switch (state) {
	case SCI_DEV_INITIAL:
	case SCI_DEV_STOPPED:
	case SCI_DEV_STARTING:
	case SCI_STP_DEV_IDLE:
	case SCI_SMP_DEV_IDLE:
	case SCI_DEV_FAILED:
	case SCI_DEV_FINAL:
	default:
		dev_warn(scirdev_to_dev(idev), "%s: in wrong state: %s\n",
			 __func__, dev_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	case SCI_DEV_READY:
	case SCI_STP_DEV_AWAIT_RESET:
	case SCI_DEV_RESETTING:
		status = common_complete_io(iport, idev, ireq);
		break;
	case SCI_STP_DEV_CMD:
	case SCI_STP_DEV_NCQ:
	case SCI_STP_DEV_NCQ_ERROR:
	case SCI_STP_DEV_ATAPI_ERROR:
		status = common_complete_io(iport, idev, ireq);
		if (status != SCI_SUCCESS)
			break;

		if (ireq->sci_status == SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED) {
			/* This request causes hardware error, device needs to be Lun Reset.
			 * So here we force the state machine to IDLE state so the rest IOs
			 * can reach RNC state handler, these IOs will be completed by RNC with
			 * status of "DEVICE_RESET_REQUIRED", instead of "INVALID STATE".
			 */
			sci_change_state(sm, SCI_STP_DEV_AWAIT_RESET);
		} else if (idev->started_request_count == 0)
			sci_change_state(sm, SCI_STP_DEV_IDLE);
		break;
	case SCI_SMP_DEV_CMD:
		status = common_complete_io(iport, idev, ireq);
		if (status != SCI_SUCCESS)
			break;
		sci_change_state(sm, SCI_SMP_DEV_IDLE);
		break;
	case SCI_DEV_STOPPING:
		status = common_complete_io(iport, idev, ireq);
		if (status != SCI_SUCCESS)
			break;

		if (idev->started_request_count == 0)
			sci_remote_node_context_destruct(&idev->rnc,
							 rnc_destruct_done,
							 idev);
		break;
	}

	if (status != SCI_SUCCESS)
		dev_err(scirdev_to_dev(idev),
			"%s: Port:0x%p Device:0x%p Request:0x%p Status:0x%x "
			"could not complete\n", __func__, iport,
			idev, ireq, status);
	else
		isci_put_device(idev);

	return status;
}

static void sci_remote_device_continue_request(void *dev)
{
	struct isci_remote_device *idev = dev;

	/* we need to check if this request is still valid to continue. */
	if (idev->working_request)
		sci_controller_continue_io(idev->working_request);
}

enum sci_status sci_remote_device_start_task(struct isci_host *ihost,
						  struct isci_remote_device *idev,
						  struct isci_request *ireq)
{
	struct sci_base_state_machine *sm = &idev->sm;
	enum sci_remote_device_states state = sm->current_state_id;
	struct isci_port *iport = idev->owning_port;
	enum sci_status status;

	switch (state) {
	case SCI_DEV_INITIAL:
	case SCI_DEV_STOPPED:
	case SCI_DEV_STARTING:
	case SCI_SMP_DEV_IDLE:
	case SCI_SMP_DEV_CMD:
	case SCI_DEV_STOPPING:
	case SCI_DEV_FAILED:
	case SCI_DEV_RESETTING:
	case SCI_DEV_FINAL:
	default:
		dev_warn(scirdev_to_dev(idev), "%s: in wrong state: %s\n",
			 __func__, dev_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	case SCI_STP_DEV_IDLE:
	case SCI_STP_DEV_CMD:
	case SCI_STP_DEV_NCQ:
	case SCI_STP_DEV_NCQ_ERROR:
	case SCI_STP_DEV_AWAIT_RESET:
		status = sci_port_start_io(iport, idev, ireq);
		if (status != SCI_SUCCESS)
			return status;

		status = sci_request_start(ireq);
		if (status != SCI_SUCCESS)
			goto out;

		/* Note: If the remote device state is not IDLE this will
		 * replace the request that probably resulted in the task
		 * management request.
		 */
		idev->working_request = ireq;
		sci_change_state(sm, SCI_STP_DEV_CMD);

		/* The remote node context must cleanup the TCi to NCQ mapping
		 * table.  The only way to do this correctly is to either write
		 * to the TLCR register or to invalidate and repost the RNC. In
		 * either case the remote node context state machine will take
		 * the correct action when the remote node context is suspended
		 * and later resumed.
		 */
		sci_remote_device_suspend(idev,
					  SCI_SW_SUSPEND_LINKHANG_DETECT);

		status = sci_remote_node_context_start_task(&idev->rnc, ireq,
				sci_remote_device_continue_request, idev);

	out:
		sci_remote_device_start_request(idev, ireq, status);
		/* We need to let the controller start request handler know that
		 * it can't post TC yet. We will provide a callback function to
		 * post TC when RNC gets resumed.
		 */
		return SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS;
	case SCI_DEV_READY:
		status = sci_port_start_io(iport, idev, ireq);
		if (status != SCI_SUCCESS)
			return status;

		/* Resume the RNC as needed: */
		status = sci_remote_node_context_start_task(&idev->rnc, ireq,
							    NULL, NULL);
		if (status != SCI_SUCCESS)
			break;

		status = sci_request_start(ireq);
		break;
	}
	sci_remote_device_start_request(idev, ireq, status);

	return status;
}

void sci_remote_device_post_request(struct isci_remote_device *idev, u32 request)
{
	struct isci_port *iport = idev->owning_port;
	u32 context;

	context = request |
		  (ISCI_PEG << SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) |
		  (iport->physical_port_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT) |
		  idev->rnc.remote_node_index;

	sci_controller_post_request(iport->owning_controller, context);
}

/* called once the remote node context has transisitioned to a
 * ready state.  This is the indication that the remote device object can also
 * transition to ready.
 */
static void remote_device_resume_done(void *_dev)
{
	struct isci_remote_device *idev = _dev;

	if (is_remote_device_ready(idev))
		return;

	/* go 'ready' if we are not already in a ready state */
	sci_change_state(&idev->sm, SCI_DEV_READY);
}

static void sci_stp_remote_device_ready_idle_substate_resume_complete_handler(void *_dev)
{
	struct isci_remote_device *idev = _dev;
	struct isci_host *ihost = idev->owning_port->owning_controller;

	/* For NCQ operation we do not issue a isci_remote_device_not_ready().
	 * As a result, avoid sending the ready notification.
	 */
	if (idev->sm.previous_state_id != SCI_STP_DEV_NCQ)
		isci_remote_device_ready(ihost, idev);
}

static void sci_remote_device_initial_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);

	/* Initial state is a transitional state to the stopped state */
	sci_change_state(&idev->sm, SCI_DEV_STOPPED);
}

/**
 * sci_remote_device_destruct() - free remote node context and destruct
 * @remote_device: This parameter specifies the remote device to be destructed.
 *
 * Remote device objects are a limited resource.  As such, they must be
 * protected.  Thus calls to construct and destruct are mutually exclusive and
 * non-reentrant. The return value shall indicate if the device was
 * successfully destructed or if some failure occurred. enum sci_status This value
 * is returned if the device is successfully destructed.
 * SCI_FAILURE_INVALID_REMOTE_DEVICE This value is returned if the supplied
 * device isn't valid (e.g. it's already been destoryed, the handle isn't
 * valid, etc.).
 */
static enum sci_status sci_remote_device_destruct(struct isci_remote_device *idev)
{
	struct sci_base_state_machine *sm = &idev->sm;
	enum sci_remote_device_states state = sm->current_state_id;
	struct isci_host *ihost;

	if (state != SCI_DEV_STOPPED) {
		dev_warn(scirdev_to_dev(idev), "%s: in wrong state: %s\n",
			 __func__, dev_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}

	ihost = idev->owning_port->owning_controller;
	sci_controller_free_remote_node_context(ihost, idev,
						     idev->rnc.remote_node_index);
	idev->rnc.remote_node_index = SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX;
	sci_change_state(sm, SCI_DEV_FINAL);

	return SCI_SUCCESS;
}

/**
 * isci_remote_device_deconstruct() - This function frees an isci_remote_device.
 * @ihost: This parameter specifies the isci host object.
 * @idev: This parameter specifies the remote device to be freed.
 *
 */
static void isci_remote_device_deconstruct(struct isci_host *ihost, struct isci_remote_device *idev)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	/* There should not be any outstanding io's. All paths to
	 * here should go through isci_remote_device_nuke_requests.
	 * If we hit this condition, we will need a way to complete
	 * io requests in process */
	BUG_ON(idev->started_request_count > 0);

	sci_remote_device_destruct(idev);
	list_del_init(&idev->node);
	isci_put_device(idev);
}

static void sci_remote_device_stopped_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);
	struct isci_host *ihost = idev->owning_port->owning_controller;
	u32 prev_state;

	/* If we are entering from the stopping state let the SCI User know that
	 * the stop operation has completed.
	 */
	prev_state = idev->sm.previous_state_id;
	if (prev_state == SCI_DEV_STOPPING)
		isci_remote_device_deconstruct(ihost, idev);

	sci_controller_remote_device_stopped(ihost, idev);
}

static void sci_remote_device_starting_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);
	struct isci_host *ihost = idev->owning_port->owning_controller;

	isci_remote_device_not_ready(ihost, idev,
				     SCIC_REMOTE_DEVICE_NOT_READY_START_REQUESTED);
}

static void sci_remote_device_ready_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);
	struct isci_host *ihost = idev->owning_port->owning_controller;
	struct domain_device *dev = idev->domain_dev;

	if (dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_SATA)) {
		sci_change_state(&idev->sm, SCI_STP_DEV_IDLE);
	} else if (dev_is_expander(dev)) {
		sci_change_state(&idev->sm, SCI_SMP_DEV_IDLE);
	} else
		isci_remote_device_ready(ihost, idev);
}

static void sci_remote_device_ready_state_exit(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);
	struct domain_device *dev = idev->domain_dev;

	if (dev->dev_type == SAS_END_DEV) {
		struct isci_host *ihost = idev->owning_port->owning_controller;

		isci_remote_device_not_ready(ihost, idev,
					     SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED);
	}
}

static void sci_remote_device_resetting_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);
	struct isci_host *ihost = idev->owning_port->owning_controller;

	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	sci_remote_device_suspend(idev, SCI_SW_SUSPEND_LINKHANG_DETECT);
}

static void sci_remote_device_resetting_state_exit(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);
	struct isci_host *ihost = idev->owning_port->owning_controller;

	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	sci_remote_node_context_resume(&idev->rnc, NULL, NULL);
}

static void sci_stp_remote_device_ready_idle_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);

	idev->working_request = NULL;
	if (sci_remote_node_context_is_ready(&idev->rnc)) {
		/*
		 * Since the RNC is ready, it's alright to finish completion
		 * processing (e.g. signal the remote device is ready). */
		sci_stp_remote_device_ready_idle_substate_resume_complete_handler(idev);
	} else {
		sci_remote_node_context_resume(&idev->rnc,
			sci_stp_remote_device_ready_idle_substate_resume_complete_handler,
			idev);
	}
}

static void sci_stp_remote_device_ready_cmd_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);
	struct isci_host *ihost = idev->owning_port->owning_controller;

	BUG_ON(idev->working_request == NULL);

	isci_remote_device_not_ready(ihost, idev,
				     SCIC_REMOTE_DEVICE_NOT_READY_SATA_REQUEST_STARTED);
}

static void sci_stp_remote_device_ready_ncq_error_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);
	struct isci_host *ihost = idev->owning_port->owning_controller;

	if (idev->not_ready_reason == SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED)
		isci_remote_device_not_ready(ihost, idev,
					     idev->not_ready_reason);
}

static void sci_stp_remote_device_atapi_error_substate_enter(
	struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);

	/* This state is entered when an I/O is decoded with an error
	 * condition.  By this point the RNC expected suspension state is set.
	 * The error conditions suspend the device, so unsuspend here if
	 * possible.
	 */
	sci_remote_node_context_resume(&idev->rnc,
				       atapi_remote_device_resume_done,
				       idev);
}

static void sci_smp_remote_device_ready_idle_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);
	struct isci_host *ihost = idev->owning_port->owning_controller;

	isci_remote_device_ready(ihost, idev);
}

static void sci_smp_remote_device_ready_cmd_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);
	struct isci_host *ihost = idev->owning_port->owning_controller;

	BUG_ON(idev->working_request == NULL);

	isci_remote_device_not_ready(ihost, idev,
				     SCIC_REMOTE_DEVICE_NOT_READY_SMP_REQUEST_STARTED);
}

static void sci_smp_remote_device_ready_cmd_substate_exit(struct sci_base_state_machine *sm)
{
	struct isci_remote_device *idev = container_of(sm, typeof(*idev), sm);

	idev->working_request = NULL;
}

static const struct sci_base_state sci_remote_device_state_table[] = {
	[SCI_DEV_INITIAL] = {
		.enter_state = sci_remote_device_initial_state_enter,
	},
	[SCI_DEV_STOPPED] = {
		.enter_state = sci_remote_device_stopped_state_enter,
	},
	[SCI_DEV_STARTING] = {
		.enter_state = sci_remote_device_starting_state_enter,
	},
	[SCI_DEV_READY] = {
		.enter_state = sci_remote_device_ready_state_enter,
		.exit_state  = sci_remote_device_ready_state_exit
	},
	[SCI_STP_DEV_IDLE] = {
		.enter_state = sci_stp_remote_device_ready_idle_substate_enter,
	},
	[SCI_STP_DEV_CMD] = {
		.enter_state = sci_stp_remote_device_ready_cmd_substate_enter,
	},
	[SCI_STP_DEV_NCQ] = { },
	[SCI_STP_DEV_NCQ_ERROR] = {
		.enter_state = sci_stp_remote_device_ready_ncq_error_substate_enter,
	},
	[SCI_STP_DEV_ATAPI_ERROR] = {
		.enter_state = sci_stp_remote_device_atapi_error_substate_enter,
	},
	[SCI_STP_DEV_AWAIT_RESET] = { },
	[SCI_SMP_DEV_IDLE] = {
		.enter_state = sci_smp_remote_device_ready_idle_substate_enter,
	},
	[SCI_SMP_DEV_CMD] = {
		.enter_state = sci_smp_remote_device_ready_cmd_substate_enter,
		.exit_state  = sci_smp_remote_device_ready_cmd_substate_exit,
	},
	[SCI_DEV_STOPPING] = { },
	[SCI_DEV_FAILED] = { },
	[SCI_DEV_RESETTING] = {
		.enter_state = sci_remote_device_resetting_state_enter,
		.exit_state  = sci_remote_device_resetting_state_exit
	},
	[SCI_DEV_FINAL] = { },
};

/**
 * sci_remote_device_construct() - common construction
 * @sci_port: SAS/SATA port through which this device is accessed.
 * @sci_dev: remote device to construct
 *
 * This routine just performs benign initialization and does not
 * allocate the remote_node_context which is left to
 * sci_remote_device_[de]a_construct().  sci_remote_device_destruct()
 * frees the remote_node_context(s) for the device.
 */
static void sci_remote_device_construct(struct isci_port *iport,
				  struct isci_remote_device *idev)
{
	idev->owning_port = iport;
	idev->started_request_count = 0;

	sci_init_sm(&idev->sm, sci_remote_device_state_table, SCI_DEV_INITIAL);

	sci_remote_node_context_construct(&idev->rnc,
					       SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX);
}

/**
 * sci_remote_device_da_construct() - construct direct attached device.
 *
 * The information (e.g. IAF, Signature FIS, etc.) necessary to build
 * the device is known to the SCI Core since it is contained in the
 * sci_phy object.  Remote node context(s) is/are a global resource
 * allocated by this routine, freed by sci_remote_device_destruct().
 *
 * Returns:
 * SCI_FAILURE_DEVICE_EXISTS - device has already been constructed.
 * SCI_FAILURE_UNSUPPORTED_PROTOCOL - e.g. sas device attached to
 * sata-only controller instance.
 * SCI_FAILURE_INSUFFICIENT_RESOURCES - remote node contexts exhausted.
 */
static enum sci_status sci_remote_device_da_construct(struct isci_port *iport,
						       struct isci_remote_device *idev)
{
	enum sci_status status;
	struct sci_port_properties properties;

	sci_remote_device_construct(iport, idev);

	sci_port_get_properties(iport, &properties);
	/* Get accurate port width from port's phy mask for a DA device. */
	idev->device_port_width = hweight32(properties.phy_mask);

	status = sci_controller_allocate_remote_node_context(iport->owning_controller,
							     idev,
							     &idev->rnc.remote_node_index);

	if (status != SCI_SUCCESS)
		return status;

	idev->connection_rate = sci_port_get_max_allowed_speed(iport);

	return SCI_SUCCESS;
}

/**
 * sci_remote_device_ea_construct() - construct expander attached device
 *
 * Remote node context(s) is/are a global resource allocated by this
 * routine, freed by sci_remote_device_destruct().
 *
 * Returns:
 * SCI_FAILURE_DEVICE_EXISTS - device has already been constructed.
 * SCI_FAILURE_UNSUPPORTED_PROTOCOL - e.g. sas device attached to
 * sata-only controller instance.
 * SCI_FAILURE_INSUFFICIENT_RESOURCES - remote node contexts exhausted.
 */
static enum sci_status sci_remote_device_ea_construct(struct isci_port *iport,
						       struct isci_remote_device *idev)
{
	struct domain_device *dev = idev->domain_dev;
	enum sci_status status;

	sci_remote_device_construct(iport, idev);

	status = sci_controller_allocate_remote_node_context(iport->owning_controller,
								  idev,
								  &idev->rnc.remote_node_index);
	if (status != SCI_SUCCESS)
		return status;

	/* For SAS-2 the physical link rate is actually a logical link
	 * rate that incorporates multiplexing.  The SCU doesn't
	 * incorporate multiplexing and for the purposes of the
	 * connection the logical link rate is that same as the
	 * physical.  Furthermore, the SAS-2 and SAS-1.1 fields overlay
	 * one another, so this code works for both situations.
	 */
	idev->connection_rate = min_t(u16, sci_port_get_max_allowed_speed(iport),
					 dev->linkrate);

	/* / @todo Should I assign the port width by reading all of the phys on the port? */
	idev->device_port_width = 1;

	return SCI_SUCCESS;
}

enum sci_status sci_remote_device_resume(
	struct isci_remote_device *idev,
	scics_sds_remote_node_context_callback cb_fn,
	void *cb_p)
{
	enum sci_status status;

	status = sci_remote_node_context_resume(&idev->rnc, cb_fn, cb_p);
	if (status != SCI_SUCCESS)
		dev_dbg(scirdev_to_dev(idev), "%s: failed to resume: %d\n",
			__func__, status);
	return status;
}

enum sci_status isci_remote_device_resume(
	struct isci_host *ihost,
	struct isci_remote_device *idev,
	scics_sds_remote_node_context_callback cb_fn,
	void *cb_p)
{
	unsigned long flags;
	enum sci_status status;

	spin_lock_irqsave(&ihost->scic_lock, flags);
	status = sci_remote_device_resume(idev, cb_fn, cb_p);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	return status;
}
/**
 * sci_remote_device_start() - This method will start the supplied remote
 *    device.  This method enables normal IO requests to flow through to the
 *    remote device.
 * @remote_device: This parameter specifies the device to be started.
 * @timeout: This parameter specifies the number of milliseconds in which the
 *    start operation should complete.
 *
 * An indication of whether the device was successfully started. SCI_SUCCESS
 * This value is returned if the device was successfully started.
 * SCI_FAILURE_INVALID_PHY This value is returned if the user attempts to start
 * the device when there have been no phys added to it.
 */
static enum sci_status sci_remote_device_start(struct isci_remote_device *idev,
					       u32 timeout)
{
	struct sci_base_state_machine *sm = &idev->sm;
	enum sci_remote_device_states state = sm->current_state_id;
	enum sci_status status;

	if (state != SCI_DEV_STOPPED) {
		dev_warn(scirdev_to_dev(idev), "%s: in wrong state: %s\n",
			 __func__, dev_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}

	status = sci_remote_device_resume(idev, remote_device_resume_done,
					  idev);
	if (status != SCI_SUCCESS)
		return status;

	sci_change_state(sm, SCI_DEV_STARTING);

	return SCI_SUCCESS;
}

static enum sci_status isci_remote_device_construct(struct isci_port *iport,
						    struct isci_remote_device *idev)
{
	struct isci_host *ihost = iport->isci_host;
	struct domain_device *dev = idev->domain_dev;
	enum sci_status status;

	if (dev->parent && dev_is_expander(dev->parent))
		status = sci_remote_device_ea_construct(iport, idev);
	else
		status = sci_remote_device_da_construct(iport, idev);

	if (status != SCI_SUCCESS) {
		dev_dbg(&ihost->pdev->dev, "%s: construct failed: %d\n",
			__func__, status);

		return status;
	}

	/* start the device. */
	status = sci_remote_device_start(idev, ISCI_REMOTE_DEVICE_START_TIMEOUT);

	if (status != SCI_SUCCESS)
		dev_warn(&ihost->pdev->dev, "remote device start failed: %d\n",
			 status);

	return status;
}

/**
 * This function builds the isci_remote_device when a libsas dev_found message
 *    is received.
 * @isci_host: This parameter specifies the isci host object.
 * @port: This parameter specifies the isci_port conected to this device.
 *
 * pointer to new isci_remote_device.
 */
static struct isci_remote_device *
isci_remote_device_alloc(struct isci_host *ihost, struct isci_port *iport)
{
	struct isci_remote_device *idev;
	int i;

	for (i = 0; i < SCI_MAX_REMOTE_DEVICES; i++) {
		idev = &ihost->devices[i];
		if (!test_and_set_bit(IDEV_ALLOCATED, &idev->flags))
			break;
	}

	if (i >= SCI_MAX_REMOTE_DEVICES) {
		dev_warn(&ihost->pdev->dev, "%s: failed\n", __func__);
		return NULL;
	}
	if (WARN_ONCE(!list_empty(&idev->node), "found non-idle remote device\n"))
		return NULL;

	return idev;
}

void isci_remote_device_release(struct kref *kref)
{
	struct isci_remote_device *idev = container_of(kref, typeof(*idev), kref);
	struct isci_host *ihost = idev->isci_port->isci_host;

	idev->domain_dev = NULL;
	idev->isci_port = NULL;
	clear_bit(IDEV_START_PENDING, &idev->flags);
	clear_bit(IDEV_STOP_PENDING, &idev->flags);
	clear_bit(IDEV_IO_READY, &idev->flags);
	clear_bit(IDEV_GONE, &idev->flags);
	smp_mb__before_clear_bit();
	clear_bit(IDEV_ALLOCATED, &idev->flags);
	wake_up(&ihost->eventq);
}

/**
 * isci_remote_device_stop() - This function is called internally to stop the
 *    remote device.
 * @isci_host: This parameter specifies the isci host object.
 * @isci_device: This parameter specifies the remote device.
 *
 * The status of the ihost request to stop.
 */
enum sci_status isci_remote_device_stop(struct isci_host *ihost, struct isci_remote_device *idev)
{
	enum sci_status status;
	unsigned long flags;

	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	spin_lock_irqsave(&ihost->scic_lock, flags);
	idev->domain_dev->lldd_dev = NULL; /* disable new lookups */
	set_bit(IDEV_GONE, &idev->flags);

	set_bit(IDEV_STOP_PENDING, &idev->flags);
	status = sci_remote_device_stop(idev, 50);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/* Wait for the stop complete callback. */
	if (WARN_ONCE(status != SCI_SUCCESS, "failed to stop device\n"))
		/* nothing to wait for */;
	else
		wait_for_device_stop(ihost, idev);

	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p, waiting done.\n", __func__, idev);

	return status;
}

/**
 * isci_remote_device_gone() - This function is called by libsas when a domain
 *    device is removed.
 * @domain_device: This parameter specifies the libsas domain device.
 *
 */
void isci_remote_device_gone(struct domain_device *dev)
{
	struct isci_host *ihost = dev_to_ihost(dev);
	struct isci_remote_device *idev = dev->lldd_dev;

	dev_dbg(&ihost->pdev->dev,
		"%s: domain_device = %p, isci_device = %p, isci_port = %p\n",
		__func__, dev, idev, idev->isci_port);

	isci_remote_device_stop(ihost, idev);
}


/**
 * isci_remote_device_found() - This function is called by libsas when a remote
 *    device is discovered. A remote device object is created and started. the
 *    function then sleeps until the sci core device started message is
 *    received.
 * @domain_device: This parameter specifies the libsas domain device.
 *
 * status, zero indicates success.
 */
int isci_remote_device_found(struct domain_device *dev)
{
	struct isci_host *isci_host = dev_to_ihost(dev);
	struct isci_port *isci_port = dev->port->lldd_port;
	struct isci_remote_device *isci_device;
	enum sci_status status;

	dev_dbg(&isci_host->pdev->dev,
		"%s: domain_device = %p\n", __func__, dev);

	if (!isci_port)
		return -ENODEV;

	isci_device = isci_remote_device_alloc(isci_host, isci_port);
	if (!isci_device)
		return -ENODEV;

	kref_init(&isci_device->kref);
	INIT_LIST_HEAD(&isci_device->node);

	spin_lock_irq(&isci_host->scic_lock);
	isci_device->domain_dev = dev;
	isci_device->isci_port = isci_port;
	list_add_tail(&isci_device->node, &isci_port->remote_dev_list);

	set_bit(IDEV_START_PENDING, &isci_device->flags);
	status = isci_remote_device_construct(isci_port, isci_device);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p\n",
		__func__, isci_device);

	if (status == SCI_SUCCESS) {
		/* device came up, advertise it to the world */
		dev->lldd_dev = isci_device;
	} else
		isci_put_device(isci_device);
	spin_unlock_irq(&isci_host->scic_lock);

	/* wait for the device ready callback. */
	wait_for_device_start(isci_host, isci_device);

	return status == SCI_SUCCESS ? 0 : -ENODEV;
}

enum sci_status isci_remote_device_suspend_terminate(
	struct isci_host *ihost,
	struct isci_remote_device *idev,
	struct isci_request *ireq)
{
	unsigned long flags;
	enum sci_status status;

	/* Put the device into suspension. */
	spin_lock_irqsave(&ihost->scic_lock, flags);
	sci_remote_device_suspend(idev, SCI_SW_SUSPEND_LINKHANG_DETECT);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/* Terminate and wait for the completions. */
	status = isci_remote_device_terminate_requests(ihost, idev, ireq);
	if (status != SCI_SUCCESS)
		dev_dbg(&ihost->pdev->dev,
			"%s: isci_remote_device_terminate_requests(%p) "
				"returned %d!\n",
			__func__, idev, status);

	/* NOTE: RNC resumption is left to the caller! */
	return status;
}

int isci_remote_device_is_safe_to_abort(
	struct isci_remote_device *idev)
{
	return sci_remote_node_context_is_safe_to_abort(&idev->rnc);
}

enum sci_status sci_remote_device_abort_requests_pending_abort(
	struct isci_remote_device *idev)
{
	return sci_remote_device_terminate_reqs_checkabort(idev, 1);
}

enum sci_status isci_remote_device_reset_complete(
	struct isci_host *ihost,
	struct isci_remote_device *idev)
{
	unsigned long flags;
	enum sci_status status;

	spin_lock_irqsave(&ihost->scic_lock, flags);
	status = sci_remote_device_reset_complete(idev);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	return status;
}

