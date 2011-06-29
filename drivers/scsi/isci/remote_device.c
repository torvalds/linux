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
#include "port.h"
#include "remote_device.h"
#include "request.h"
#include "remote_node_context.h"
#include "scu_event_codes.h"
#include "task.h"

/**
 * isci_remote_device_not_ready() - This function is called by the scic when
 *    the remote device is not ready. We mark the isci device as ready (not
 *    "ready_for_io") and signal the waiting proccess.
 * @isci_host: This parameter specifies the isci host object.
 * @isci_device: This parameter specifies the remote device
 *
 * scic_lock is held on entrance to this function.
 */
static void isci_remote_device_not_ready(struct isci_host *ihost,
				  struct isci_remote_device *idev, u32 reason)
{
	struct isci_request * ireq;

	dev_dbg(&ihost->pdev->dev,
		"%s: isci_device = %p\n", __func__, idev);

	switch (reason) {
	case SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED:
		set_bit(IDEV_GONE, &idev->flags);
		break;
	case SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED:
		set_bit(IDEV_IO_NCQERROR, &idev->flags);

		/* Kill all outstanding requests for the device. */
		list_for_each_entry(ireq, &idev->reqs_in_process, dev_node) {

			dev_dbg(&ihost->pdev->dev,
				"%s: isci_device = %p request = %p\n",
				__func__, idev, ireq);

			scic_controller_terminate_request(&ihost->sci,
							  &idev->sci,
							  ireq);
		}
		/* Fall through into the default case... */
	default:
		clear_bit(IDEV_IO_READY, &idev->flags);
		break;
	}
}

/**
 * isci_remote_device_ready() - This function is called by the scic when the
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

/* called once the remote node context is ready to be freed.
 * The remote device can now report that its stop operation is complete. none
 */
static void rnc_destruct_done(void *_dev)
{
	struct scic_sds_remote_device *sci_dev = _dev;

	BUG_ON(sci_dev->started_request_count != 0);
	sci_change_state(&sci_dev->sm, SCI_DEV_STOPPED);
}

static enum sci_status scic_sds_remote_device_terminate_requests(struct scic_sds_remote_device *sci_dev)
{
	struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;
	struct isci_host *ihost = scic_to_ihost(scic);
	enum sci_status status  = SCI_SUCCESS;
	u32 i;

	for (i = 0; i < SCI_MAX_IO_REQUESTS; i++) {
		struct isci_request *ireq = ihost->reqs[i];
		enum sci_status s;

		if (!test_bit(IREQ_ACTIVE, &ireq->flags) ||
		    ireq->target_device != sci_dev)
			continue;

		s = scic_controller_terminate_request(scic, sci_dev, ireq);
		if (s != SCI_SUCCESS)
			status = s;
	}

	return status;
}

enum sci_status scic_remote_device_stop(struct scic_sds_remote_device *sci_dev,
					u32 timeout)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;

	switch (state) {
	case SCI_DEV_INITIAL:
	case SCI_DEV_FAILED:
	case SCI_DEV_FINAL:
	default:
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	case SCI_DEV_STOPPED:
		return SCI_SUCCESS;
	case SCI_DEV_STARTING:
		/* device not started so there had better be no requests */
		BUG_ON(sci_dev->started_request_count != 0);
		scic_sds_remote_node_context_destruct(&sci_dev->rnc,
						      rnc_destruct_done, sci_dev);
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
		if (sci_dev->started_request_count == 0) {
			scic_sds_remote_node_context_destruct(&sci_dev->rnc,
							      rnc_destruct_done, sci_dev);
			return SCI_SUCCESS;
		} else
			return scic_sds_remote_device_terminate_requests(sci_dev);
		break;
	case SCI_DEV_STOPPING:
		/* All requests should have been terminated, but if there is an
		 * attempt to stop a device already in the stopping state, then
		 * try again to terminate.
		 */
		return scic_sds_remote_device_terminate_requests(sci_dev);
	case SCI_DEV_RESETTING:
		sci_change_state(sm, SCI_DEV_STOPPING);
		return SCI_SUCCESS;
	}
}

enum sci_status scic_remote_device_reset(struct scic_sds_remote_device *sci_dev)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;

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
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
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

enum sci_status scic_remote_device_reset_complete(struct scic_sds_remote_device *sci_dev)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;

	if (state != SCI_DEV_RESETTING) {
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	}

	sci_change_state(sm, SCI_DEV_READY);
	return SCI_SUCCESS;
}

enum sci_status scic_sds_remote_device_suspend(struct scic_sds_remote_device *sci_dev,
					       u32 suspend_type)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;

	if (state != SCI_STP_DEV_CMD) {
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	}

	return scic_sds_remote_node_context_suspend(&sci_dev->rnc,
						    suspend_type, NULL, NULL);
}

enum sci_status scic_sds_remote_device_frame_handler(struct scic_sds_remote_device *sci_dev,
						     u32 frame_index)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;
	struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;
	enum sci_status status;

	switch (state) {
	case SCI_DEV_INITIAL:
	case SCI_DEV_STOPPED:
	case SCI_DEV_STARTING:
	case SCI_STP_DEV_IDLE:
	case SCI_SMP_DEV_IDLE:
	case SCI_DEV_FINAL:
	default:
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		/* Return the frame back to the controller */
		scic_sds_controller_release_frame(scic, frame_index);
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

		status = scic_sds_unsolicited_frame_control_get_header(&scic->uf_control,
								       frame_index,
								       &frame_header);
		if (status != SCI_SUCCESS)
			return status;

		word_cnt = sizeof(hdr) / sizeof(u32);
		sci_swab32_cpy(&hdr, frame_header, word_cnt);

		ireq = scic_request_by_tag(scic, be16_to_cpu(hdr.tag));
		if (ireq && ireq->target_device == sci_dev) {
			/* The IO request is now in charge of releasing the frame */
			status = scic_sds_io_request_frame_handler(ireq, frame_index);
		} else {
			/* We could not map this tag to a valid IO
			 * request Just toss the frame and continue
			 */
			scic_sds_controller_release_frame(scic, frame_index);
		}
		break;
	}
	case SCI_STP_DEV_NCQ: {
		struct dev_to_host_fis *hdr;

		status = scic_sds_unsolicited_frame_control_get_header(&scic->uf_control,
								       frame_index,
								       (void **)&hdr);
		if (status != SCI_SUCCESS)
			return status;

		if (hdr->fis_type == FIS_SETDEVBITS &&
		    (hdr->status & ATA_ERR)) {
			sci_dev->not_ready_reason = SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED;

			/* TODO Check sactive and complete associated IO if any. */
			sci_change_state(sm, SCI_STP_DEV_NCQ_ERROR);
		} else if (hdr->fis_type == FIS_REGD2H &&
			   (hdr->status & ATA_ERR)) {
			/*
			 * Some devices return D2H FIS when an NCQ error is detected.
			 * Treat this like an SDB error FIS ready reason.
			 */
			sci_dev->not_ready_reason = SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED;
			sci_change_state(&sci_dev->sm, SCI_STP_DEV_NCQ_ERROR);
		} else
			status = SCI_FAILURE;

		scic_sds_controller_release_frame(scic, frame_index);
		break;
	}
	case SCI_STP_DEV_CMD:
	case SCI_SMP_DEV_CMD:
		/* The device does not process any UF received from the hardware while
		 * in this state.  All unsolicited frames are forwarded to the io request
		 * object.
		 */
		status = scic_sds_io_request_frame_handler(sci_dev->working_request, frame_index);
		break;
	}

	return status;
}

static bool is_remote_device_ready(struct scic_sds_remote_device *sci_dev)
{

	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;

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

enum sci_status scic_sds_remote_device_event_handler(struct scic_sds_remote_device *sci_dev,
						     u32 event_code)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;
	enum sci_status status;

	switch (scu_get_event_type(event_code)) {
	case SCU_EVENT_TYPE_RNC_OPS_MISC:
	case SCU_EVENT_TYPE_RNC_SUSPEND_TX:
	case SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX:
		status = scic_sds_remote_node_context_event_handler(&sci_dev->rnc, event_code);
		break;
	case SCU_EVENT_TYPE_PTX_SCHEDULE_EVENT:
		if (scu_get_event_code(event_code) == SCU_EVENT_IT_NEXUS_TIMEOUT) {
			status = SCI_SUCCESS;

			/* Suspend the associated RNC */
			scic_sds_remote_node_context_suspend(&sci_dev->rnc,
							      SCI_SOFTWARE_SUSPENSION,
							      NULL, NULL);

			dev_dbg(scirdev_to_dev(sci_dev),
				"%s: device: %p event code: %x: %s\n",
				__func__, sci_dev, event_code,
				is_remote_device_ready(sci_dev)
				? "I_T_Nexus_Timeout event"
				: "I_T_Nexus_Timeout event in wrong state");

			break;
		}
	/* Else, fall through and treat as unhandled... */
	default:
		dev_dbg(scirdev_to_dev(sci_dev),
			"%s: device: %p event code: %x: %s\n",
			__func__, sci_dev, event_code,
			is_remote_device_ready(sci_dev)
			? "unexpected event"
			: "unexpected event in wrong state");
		status = SCI_FAILURE_INVALID_STATE;
		break;
	}

	if (status != SCI_SUCCESS)
		return status;

	if (state == SCI_STP_DEV_IDLE) {

		/* We pick up suspension events to handle specifically to this
		 * state. We resume the RNC right away.
		 */
		if (scu_get_event_type(event_code) == SCU_EVENT_TYPE_RNC_SUSPEND_TX ||
		    scu_get_event_type(event_code) == SCU_EVENT_TYPE_RNC_SUSPEND_TX_RX)
			status = scic_sds_remote_node_context_resume(&sci_dev->rnc, NULL, NULL);
	}

	return status;
}

static void scic_sds_remote_device_start_request(struct scic_sds_remote_device *sci_dev,
						 struct isci_request *ireq,
						 enum sci_status status)
{
	struct scic_sds_port *sci_port = sci_dev->owning_port;

	/* cleanup requests that failed after starting on the port */
	if (status != SCI_SUCCESS)
		scic_sds_port_complete_io(sci_port, sci_dev, ireq);
	else {
		kref_get(&sci_dev_to_idev(sci_dev)->kref);
		scic_sds_remote_device_increment_request_count(sci_dev);
	}
}

enum sci_status scic_sds_remote_device_start_io(struct scic_sds_controller *scic,
						struct scic_sds_remote_device *sci_dev,
						struct isci_request *ireq)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;
	struct scic_sds_port *sci_port = sci_dev->owning_port;
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
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	case SCI_DEV_READY:
		/* attempt to start an io request for this device object. The remote
		 * device object will issue the start request for the io and if
		 * successful it will start the request for the port object then
		 * increment its own request count.
		 */
		status = scic_sds_port_start_io(sci_port, sci_dev, ireq);
		if (status != SCI_SUCCESS)
			return status;

		status = scic_sds_remote_node_context_start_io(&sci_dev->rnc, ireq);
		if (status != SCI_SUCCESS)
			break;

		status = scic_sds_request_start(ireq);
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
		enum scic_sds_remote_device_states new_state;
		struct sas_task *task = isci_request_access_task(ireq);

		status = scic_sds_port_start_io(sci_port, sci_dev, ireq);
		if (status != SCI_SUCCESS)
			return status;

		status = scic_sds_remote_node_context_start_io(&sci_dev->rnc, ireq);
		if (status != SCI_SUCCESS)
			break;

		status = scic_sds_request_start(ireq);
		if (status != SCI_SUCCESS)
			break;

		if (task->ata_task.use_ncq)
			new_state = SCI_STP_DEV_NCQ;
		else {
			sci_dev->working_request = ireq;
			new_state = SCI_STP_DEV_CMD;
		}
		sci_change_state(sm, new_state);
		break;
	}
	case SCI_STP_DEV_NCQ: {
		struct sas_task *task = isci_request_access_task(ireq);

		if (task->ata_task.use_ncq) {
			status = scic_sds_port_start_io(sci_port, sci_dev, ireq);
			if (status != SCI_SUCCESS)
				return status;

			status = scic_sds_remote_node_context_start_io(&sci_dev->rnc, ireq);
			if (status != SCI_SUCCESS)
				break;

			status = scic_sds_request_start(ireq);
		} else
			return SCI_FAILURE_INVALID_STATE;
		break;
	}
	case SCI_STP_DEV_AWAIT_RESET:
		return SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED;
	case SCI_SMP_DEV_IDLE:
		status = scic_sds_port_start_io(sci_port, sci_dev, ireq);
		if (status != SCI_SUCCESS)
			return status;

		status = scic_sds_remote_node_context_start_io(&sci_dev->rnc, ireq);
		if (status != SCI_SUCCESS)
			break;

		status = scic_sds_request_start(ireq);
		if (status != SCI_SUCCESS)
			break;

		sci_dev->working_request = ireq;
		sci_change_state(&sci_dev->sm, SCI_SMP_DEV_CMD);
		break;
	case SCI_STP_DEV_CMD:
	case SCI_SMP_DEV_CMD:
		/* device is already handling a command it can not accept new commands
		 * until this one is complete.
		 */
		return SCI_FAILURE_INVALID_STATE;
	}

	scic_sds_remote_device_start_request(sci_dev, ireq, status);
	return status;
}

static enum sci_status common_complete_io(struct scic_sds_port *sci_port,
					  struct scic_sds_remote_device *sci_dev,
					  struct isci_request *ireq)
{
	enum sci_status status;

	status = scic_sds_request_complete(ireq);
	if (status != SCI_SUCCESS)
		return status;

	status = scic_sds_port_complete_io(sci_port, sci_dev, ireq);
	if (status != SCI_SUCCESS)
		return status;

	scic_sds_remote_device_decrement_request_count(sci_dev);
	return status;
}

enum sci_status scic_sds_remote_device_complete_io(struct scic_sds_controller *scic,
						   struct scic_sds_remote_device *sci_dev,
						   struct isci_request *ireq)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;
	struct scic_sds_port *sci_port = sci_dev->owning_port;
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
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	case SCI_DEV_READY:
	case SCI_STP_DEV_AWAIT_RESET:
	case SCI_DEV_RESETTING:
		status = common_complete_io(sci_port, sci_dev, ireq);
		break;
	case SCI_STP_DEV_CMD:
	case SCI_STP_DEV_NCQ:
	case SCI_STP_DEV_NCQ_ERROR:
		status = common_complete_io(sci_port, sci_dev, ireq);
		if (status != SCI_SUCCESS)
			break;

		if (ireq->sci_status == SCI_FAILURE_REMOTE_DEVICE_RESET_REQUIRED) {
			/* This request causes hardware error, device needs to be Lun Reset.
			 * So here we force the state machine to IDLE state so the rest IOs
			 * can reach RNC state handler, these IOs will be completed by RNC with
			 * status of "DEVICE_RESET_REQUIRED", instead of "INVALID STATE".
			 */
			sci_change_state(sm, SCI_STP_DEV_AWAIT_RESET);
		} else if (scic_sds_remote_device_get_request_count(sci_dev) == 0)
			sci_change_state(sm, SCI_STP_DEV_IDLE);
		break;
	case SCI_SMP_DEV_CMD:
		status = common_complete_io(sci_port, sci_dev, ireq);
		if (status != SCI_SUCCESS)
			break;
		sci_change_state(sm, SCI_SMP_DEV_IDLE);
		break;
	case SCI_DEV_STOPPING:
		status = common_complete_io(sci_port, sci_dev, ireq);
		if (status != SCI_SUCCESS)
			break;

		if (scic_sds_remote_device_get_request_count(sci_dev) == 0)
			scic_sds_remote_node_context_destruct(&sci_dev->rnc,
							      rnc_destruct_done,
							      sci_dev);
		break;
	}

	if (status != SCI_SUCCESS)
		dev_err(scirdev_to_dev(sci_dev),
			"%s: Port:0x%p Device:0x%p Request:0x%p Status:0x%x "
			"could not complete\n", __func__, sci_port,
			sci_dev, ireq, status);
	else
		isci_put_device(sci_dev_to_idev(sci_dev));

	return status;
}

static void scic_sds_remote_device_continue_request(void *dev)
{
	struct scic_sds_remote_device *sci_dev = dev;

	/* we need to check if this request is still valid to continue. */
	if (sci_dev->working_request)
		scic_controller_continue_io(sci_dev->working_request);
}

enum sci_status scic_sds_remote_device_start_task(struct scic_sds_controller *scic,
						  struct scic_sds_remote_device *sci_dev,
						  struct isci_request *ireq)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;
	struct scic_sds_port *sci_port = sci_dev->owning_port;
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
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	case SCI_STP_DEV_IDLE:
	case SCI_STP_DEV_CMD:
	case SCI_STP_DEV_NCQ:
	case SCI_STP_DEV_NCQ_ERROR:
	case SCI_STP_DEV_AWAIT_RESET:
		status = scic_sds_port_start_io(sci_port, sci_dev, ireq);
		if (status != SCI_SUCCESS)
			return status;

		status = scic_sds_remote_node_context_start_task(&sci_dev->rnc, ireq);
		if (status != SCI_SUCCESS)
			goto out;

		status = scic_sds_request_start(ireq);
		if (status != SCI_SUCCESS)
			goto out;

		/* Note: If the remote device state is not IDLE this will
		 * replace the request that probably resulted in the task
		 * management request.
		 */
		sci_dev->working_request = ireq;
		sci_change_state(sm, SCI_STP_DEV_CMD);

		/* The remote node context must cleanup the TCi to NCQ mapping
		 * table.  The only way to do this correctly is to either write
		 * to the TLCR register or to invalidate and repost the RNC. In
		 * either case the remote node context state machine will take
		 * the correct action when the remote node context is suspended
		 * and later resumed.
		 */
		scic_sds_remote_node_context_suspend(&sci_dev->rnc,
				SCI_SOFTWARE_SUSPENSION, NULL, NULL);
		scic_sds_remote_node_context_resume(&sci_dev->rnc,
				scic_sds_remote_device_continue_request,
						    sci_dev);

	out:
		scic_sds_remote_device_start_request(sci_dev, ireq, status);
		/* We need to let the controller start request handler know that
		 * it can't post TC yet. We will provide a callback function to
		 * post TC when RNC gets resumed.
		 */
		return SCI_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS;
	case SCI_DEV_READY:
		status = scic_sds_port_start_io(sci_port, sci_dev, ireq);
		if (status != SCI_SUCCESS)
			return status;

		status = scic_sds_remote_node_context_start_task(&sci_dev->rnc, ireq);
		if (status != SCI_SUCCESS)
			break;

		status = scic_sds_request_start(ireq);
		break;
	}
	scic_sds_remote_device_start_request(sci_dev, ireq, status);

	return status;
}

/**
 *
 * @sci_dev:
 * @request:
 *
 * This method takes the request and bulids an appropriate SCU context for the
 * request and then requests the controller to post the request. none
 */
void scic_sds_remote_device_post_request(
	struct scic_sds_remote_device *sci_dev,
	u32 request)
{
	u32 context;

	context = scic_sds_remote_device_build_command_context(sci_dev, request);

	scic_sds_controller_post_request(
		scic_sds_remote_device_get_controller(sci_dev),
		context
		);
}

/* called once the remote node context has transisitioned to a
 * ready state.  This is the indication that the remote device object can also
 * transition to ready.
 */
static void remote_device_resume_done(void *_dev)
{
	struct scic_sds_remote_device *sci_dev = _dev;

	if (is_remote_device_ready(sci_dev))
		return;

	/* go 'ready' if we are not already in a ready state */
	sci_change_state(&sci_dev->sm, SCI_DEV_READY);
}

static void scic_sds_stp_remote_device_ready_idle_substate_resume_complete_handler(void *_dev)
{
	struct scic_sds_remote_device *sci_dev = _dev;
	struct isci_remote_device *idev = sci_dev_to_idev(sci_dev);
	struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;

	/* For NCQ operation we do not issue a isci_remote_device_not_ready().
	 * As a result, avoid sending the ready notification.
	 */
	if (sci_dev->sm.previous_state_id != SCI_STP_DEV_NCQ)
		isci_remote_device_ready(scic_to_ihost(scic), idev);
}

static void scic_sds_remote_device_initial_state_enter(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);

	/* Initial state is a transitional state to the stopped state */
	sci_change_state(&sci_dev->sm, SCI_DEV_STOPPED);
}

/**
 * scic_remote_device_destruct() - free remote node context and destruct
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
static enum sci_status scic_remote_device_destruct(struct scic_sds_remote_device *sci_dev)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;
	struct scic_sds_controller *scic;

	if (state != SCI_DEV_STOPPED) {
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	}

	scic = sci_dev->owning_port->owning_controller;
	scic_sds_controller_free_remote_node_context(scic, sci_dev,
						     sci_dev->rnc.remote_node_index);
	sci_dev->rnc.remote_node_index = SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX;
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
	BUG_ON(!list_empty(&idev->reqs_in_process));

	scic_remote_device_destruct(&idev->sci);
	list_del_init(&idev->node);
	isci_put_device(idev);
}

static void scic_sds_remote_device_stopped_state_enter(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);
	struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;
	struct isci_remote_device *idev = sci_dev_to_idev(sci_dev);
	u32 prev_state;

	/* If we are entering from the stopping state let the SCI User know that
	 * the stop operation has completed.
	 */
	prev_state = sci_dev->sm.previous_state_id;
	if (prev_state == SCI_DEV_STOPPING)
		isci_remote_device_deconstruct(scic_to_ihost(scic), idev);

	scic_sds_controller_remote_device_stopped(scic, sci_dev);
}

static void scic_sds_remote_device_starting_state_enter(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);
	struct isci_host *ihost = scic_to_ihost(scic);
	struct isci_remote_device *idev = sci_dev_to_idev(sci_dev);

	isci_remote_device_not_ready(ihost, idev,
				     SCIC_REMOTE_DEVICE_NOT_READY_START_REQUESTED);
}

static void scic_sds_remote_device_ready_state_enter(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);
	struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;
	struct isci_remote_device *idev = sci_dev_to_idev(sci_dev);
	struct domain_device *dev = idev->domain_dev;

	if (dev->dev_type == SATA_DEV || (dev->tproto & SAS_PROTOCOL_SATA)) {
		sci_change_state(&sci_dev->sm, SCI_STP_DEV_IDLE);
	} else if (dev_is_expander(dev)) {
		sci_change_state(&sci_dev->sm, SCI_SMP_DEV_IDLE);
	} else
		isci_remote_device_ready(scic_to_ihost(scic), idev);
}

static void scic_sds_remote_device_ready_state_exit(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);
	struct domain_device *dev = sci_dev_to_domain(sci_dev);

	if (dev->dev_type == SAS_END_DEV) {
		struct scic_sds_controller *scic = sci_dev->owning_port->owning_controller;
		struct isci_remote_device *idev = sci_dev_to_idev(sci_dev);

		isci_remote_device_not_ready(scic_to_ihost(scic), idev,
					     SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED);
	}
}

static void scic_sds_remote_device_resetting_state_enter(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);

	scic_sds_remote_node_context_suspend(
		&sci_dev->rnc, SCI_SOFTWARE_SUSPENSION, NULL, NULL);
}

static void scic_sds_remote_device_resetting_state_exit(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);

	scic_sds_remote_node_context_resume(&sci_dev->rnc, NULL, NULL);
}

static void scic_sds_stp_remote_device_ready_idle_substate_enter(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);

	sci_dev->working_request = NULL;
	if (scic_sds_remote_node_context_is_ready(&sci_dev->rnc)) {
		/*
		 * Since the RNC is ready, it's alright to finish completion
		 * processing (e.g. signal the remote device is ready). */
		scic_sds_stp_remote_device_ready_idle_substate_resume_complete_handler(sci_dev);
	} else {
		scic_sds_remote_node_context_resume(&sci_dev->rnc,
			scic_sds_stp_remote_device_ready_idle_substate_resume_complete_handler,
			sci_dev);
	}
}

static void scic_sds_stp_remote_device_ready_cmd_substate_enter(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);

	BUG_ON(sci_dev->working_request == NULL);

	isci_remote_device_not_ready(scic_to_ihost(scic), sci_dev_to_idev(sci_dev),
				     SCIC_REMOTE_DEVICE_NOT_READY_SATA_REQUEST_STARTED);
}

static void scic_sds_stp_remote_device_ready_ncq_error_substate_enter(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);
	struct isci_remote_device *idev = sci_dev_to_idev(sci_dev);

	if (sci_dev->not_ready_reason == SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED)
		isci_remote_device_not_ready(scic_to_ihost(scic), idev,
					     sci_dev->not_ready_reason);
}

static void scic_sds_smp_remote_device_ready_idle_substate_enter(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);

	isci_remote_device_ready(scic_to_ihost(scic), sci_dev_to_idev(sci_dev));
}

static void scic_sds_smp_remote_device_ready_cmd_substate_enter(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);
	struct scic_sds_controller *scic = scic_sds_remote_device_get_controller(sci_dev);

	BUG_ON(sci_dev->working_request == NULL);

	isci_remote_device_not_ready(scic_to_ihost(scic), sci_dev_to_idev(sci_dev),
				     SCIC_REMOTE_DEVICE_NOT_READY_SMP_REQUEST_STARTED);
}

static void scic_sds_smp_remote_device_ready_cmd_substate_exit(struct sci_base_state_machine *sm)
{
	struct scic_sds_remote_device *sci_dev = container_of(sm, typeof(*sci_dev), sm);

	sci_dev->working_request = NULL;
}

static const struct sci_base_state scic_sds_remote_device_state_table[] = {
	[SCI_DEV_INITIAL] = {
		.enter_state = scic_sds_remote_device_initial_state_enter,
	},
	[SCI_DEV_STOPPED] = {
		.enter_state = scic_sds_remote_device_stopped_state_enter,
	},
	[SCI_DEV_STARTING] = {
		.enter_state = scic_sds_remote_device_starting_state_enter,
	},
	[SCI_DEV_READY] = {
		.enter_state = scic_sds_remote_device_ready_state_enter,
		.exit_state  = scic_sds_remote_device_ready_state_exit
	},
	[SCI_STP_DEV_IDLE] = {
		.enter_state = scic_sds_stp_remote_device_ready_idle_substate_enter,
	},
	[SCI_STP_DEV_CMD] = {
		.enter_state = scic_sds_stp_remote_device_ready_cmd_substate_enter,
	},
	[SCI_STP_DEV_NCQ] = { },
	[SCI_STP_DEV_NCQ_ERROR] = {
		.enter_state = scic_sds_stp_remote_device_ready_ncq_error_substate_enter,
	},
	[SCI_STP_DEV_AWAIT_RESET] = { },
	[SCI_SMP_DEV_IDLE] = {
		.enter_state = scic_sds_smp_remote_device_ready_idle_substate_enter,
	},
	[SCI_SMP_DEV_CMD] = {
		.enter_state = scic_sds_smp_remote_device_ready_cmd_substate_enter,
		.exit_state  = scic_sds_smp_remote_device_ready_cmd_substate_exit,
	},
	[SCI_DEV_STOPPING] = { },
	[SCI_DEV_FAILED] = { },
	[SCI_DEV_RESETTING] = {
		.enter_state = scic_sds_remote_device_resetting_state_enter,
		.exit_state  = scic_sds_remote_device_resetting_state_exit
	},
	[SCI_DEV_FINAL] = { },
};

/**
 * scic_remote_device_construct() - common construction
 * @sci_port: SAS/SATA port through which this device is accessed.
 * @sci_dev: remote device to construct
 *
 * This routine just performs benign initialization and does not
 * allocate the remote_node_context which is left to
 * scic_remote_device_[de]a_construct().  scic_remote_device_destruct()
 * frees the remote_node_context(s) for the device.
 */
static void scic_remote_device_construct(struct scic_sds_port *sci_port,
				  struct scic_sds_remote_device *sci_dev)
{
	sci_dev->owning_port = sci_port;
	sci_dev->started_request_count = 0;

	sci_init_sm(&sci_dev->sm, scic_sds_remote_device_state_table, SCI_DEV_INITIAL);

	scic_sds_remote_node_context_construct(&sci_dev->rnc,
					       SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX);
}

/**
 * scic_remote_device_da_construct() - construct direct attached device.
 *
 * The information (e.g. IAF, Signature FIS, etc.) necessary to build
 * the device is known to the SCI Core since it is contained in the
 * scic_phy object.  Remote node context(s) is/are a global resource
 * allocated by this routine, freed by scic_remote_device_destruct().
 *
 * Returns:
 * SCI_FAILURE_DEVICE_EXISTS - device has already been constructed.
 * SCI_FAILURE_UNSUPPORTED_PROTOCOL - e.g. sas device attached to
 * sata-only controller instance.
 * SCI_FAILURE_INSUFFICIENT_RESOURCES - remote node contexts exhausted.
 */
static enum sci_status scic_remote_device_da_construct(struct scic_sds_port *sci_port,
						       struct scic_sds_remote_device *sci_dev)
{
	enum sci_status status;
	struct domain_device *dev = sci_dev_to_domain(sci_dev);

	scic_remote_device_construct(sci_port, sci_dev);

	/*
	 * This information is request to determine how many remote node context
	 * entries will be needed to store the remote node.
	 */
	sci_dev->is_direct_attached = true;
	status = scic_sds_controller_allocate_remote_node_context(sci_port->owning_controller,
								  sci_dev,
								  &sci_dev->rnc.remote_node_index);

	if (status != SCI_SUCCESS)
		return status;

	if (dev->dev_type == SAS_END_DEV || dev->dev_type == SATA_DEV ||
	    (dev->tproto & SAS_PROTOCOL_STP) || dev_is_expander(dev))
		/* pass */;
	else
		return SCI_FAILURE_UNSUPPORTED_PROTOCOL;

	sci_dev->connection_rate = scic_sds_port_get_max_allowed_speed(sci_port);

	/* / @todo Should I assign the port width by reading all of the phys on the port? */
	sci_dev->device_port_width = 1;

	return SCI_SUCCESS;
}

/**
 * scic_remote_device_ea_construct() - construct expander attached device
 *
 * Remote node context(s) is/are a global resource allocated by this
 * routine, freed by scic_remote_device_destruct().
 *
 * Returns:
 * SCI_FAILURE_DEVICE_EXISTS - device has already been constructed.
 * SCI_FAILURE_UNSUPPORTED_PROTOCOL - e.g. sas device attached to
 * sata-only controller instance.
 * SCI_FAILURE_INSUFFICIENT_RESOURCES - remote node contexts exhausted.
 */
static enum sci_status scic_remote_device_ea_construct(struct scic_sds_port *sci_port,
						       struct scic_sds_remote_device *sci_dev)
{
	struct domain_device *dev = sci_dev_to_domain(sci_dev);
	enum sci_status status;

	scic_remote_device_construct(sci_port, sci_dev);

	status = scic_sds_controller_allocate_remote_node_context(sci_port->owning_controller,
								  sci_dev,
								  &sci_dev->rnc.remote_node_index);
	if (status != SCI_SUCCESS)
		return status;

	if (dev->dev_type == SAS_END_DEV || dev->dev_type == SATA_DEV ||
	    (dev->tproto & SAS_PROTOCOL_STP) || dev_is_expander(dev))
		/* pass */;
	else
		return SCI_FAILURE_UNSUPPORTED_PROTOCOL;

	/*
	 * For SAS-2 the physical link rate is actually a logical link
	 * rate that incorporates multiplexing.  The SCU doesn't
	 * incorporate multiplexing and for the purposes of the
	 * connection the logical link rate is that same as the
	 * physical.  Furthermore, the SAS-2 and SAS-1.1 fields overlay
	 * one another, so this code works for both situations. */
	sci_dev->connection_rate = min_t(u16, scic_sds_port_get_max_allowed_speed(sci_port),
					 dev->linkrate);

	/* / @todo Should I assign the port width by reading all of the phys on the port? */
	sci_dev->device_port_width = 1;

	return SCI_SUCCESS;
}

/**
 * scic_remote_device_start() - This method will start the supplied remote
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
static enum sci_status scic_remote_device_start(struct scic_sds_remote_device *sci_dev,
						u32 timeout)
{
	struct sci_base_state_machine *sm = &sci_dev->sm;
	enum scic_sds_remote_device_states state = sm->current_state_id;
	enum sci_status status;

	if (state != SCI_DEV_STOPPED) {
		dev_warn(scirdev_to_dev(sci_dev), "%s: in wrong state: %d\n",
			 __func__, state);
		return SCI_FAILURE_INVALID_STATE;
	}

	status = scic_sds_remote_node_context_resume(&sci_dev->rnc,
						     remote_device_resume_done,
						     sci_dev);
	if (status != SCI_SUCCESS)
		return status;

	sci_change_state(sm, SCI_DEV_STARTING);

	return SCI_SUCCESS;
}

static enum sci_status isci_remote_device_construct(struct isci_port *iport,
						    struct isci_remote_device *idev)
{
	struct scic_sds_port *sci_port = &iport->sci;
	struct isci_host *ihost = iport->isci_host;
	struct domain_device *dev = idev->domain_dev;
	enum sci_status status;

	if (dev->parent && dev_is_expander(dev->parent))
		status = scic_remote_device_ea_construct(sci_port, &idev->sci);
	else
		status = scic_remote_device_da_construct(sci_port, &idev->sci);

	if (status != SCI_SUCCESS) {
		dev_dbg(&ihost->pdev->dev, "%s: construct failed: %d\n",
			__func__, status);

		return status;
	}

	/* start the device. */
	status = scic_remote_device_start(&idev->sci, ISCI_REMOTE_DEVICE_START_TIMEOUT);

	if (status != SCI_SUCCESS)
		dev_warn(&ihost->pdev->dev, "remote device start failed: %d\n",
			 status);

	return status;
}

void isci_remote_device_nuke_requests(struct isci_host *ihost, struct isci_remote_device *idev)
{
	DECLARE_COMPLETION_ONSTACK(aborted_task_completion);

	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p\n", __func__, idev);

	/* Cleanup all requests pending for this device. */
	isci_terminate_pending_requests(ihost, idev);

	dev_dbg(&ihost->pdev->dev,
		"%s: idev = %p, done\n", __func__, idev);
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

	if (WARN_ONCE(!list_empty(&idev->reqs_in_process), "found requests in process\n"))
		return NULL;

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
	clear_bit(IDEV_EH, &idev->flags);
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
 * The status of the scic request to stop.
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
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/* Kill all outstanding requests. */
	isci_remote_device_nuke_requests(ihost, idev);

	set_bit(IDEV_STOP_PENDING, &idev->flags);

	spin_lock_irqsave(&ihost->scic_lock, flags);
	status = scic_remote_device_stop(&idev->sci, 50);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	/* Wait for the stop complete callback. */
	if (WARN_ONCE(status != SCI_SUCCESS, "failed to stop device\n"))
		/* nothing to wait for */;
	else
		wait_for_device_stop(ihost, idev);

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
int isci_remote_device_found(struct domain_device *domain_dev)
{
	struct isci_host *isci_host = dev_to_ihost(domain_dev);
	struct isci_port *isci_port;
	struct isci_phy *isci_phy;
	struct asd_sas_port *sas_port;
	struct asd_sas_phy *sas_phy;
	struct isci_remote_device *isci_device;
	enum sci_status status;

	dev_dbg(&isci_host->pdev->dev,
		"%s: domain_device = %p\n", __func__, domain_dev);

	wait_for_start(isci_host);

	sas_port = domain_dev->port;
	sas_phy = list_first_entry(&sas_port->phy_list, struct asd_sas_phy,
				   port_phy_el);
	isci_phy = to_iphy(sas_phy);
	isci_port = isci_phy->isci_port;

	/* we are being called for a device on this port,
	 * so it has to come up eventually
	 */
	wait_for_completion(&isci_port->start_complete);

	if ((isci_stopping == isci_port_get_state(isci_port)) ||
	    (isci_stopped == isci_port_get_state(isci_port)))
		return -ENODEV;

	isci_device = isci_remote_device_alloc(isci_host, isci_port);
	if (!isci_device)
		return -ENODEV;

	kref_init(&isci_device->kref);
	INIT_LIST_HEAD(&isci_device->node);

	spin_lock_irq(&isci_host->scic_lock);
	isci_device->domain_dev = domain_dev;
	isci_device->isci_port = isci_port;
	list_add_tail(&isci_device->node, &isci_port->remote_dev_list);

	set_bit(IDEV_START_PENDING, &isci_device->flags);
	status = isci_remote_device_construct(isci_port, isci_device);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p\n",
		__func__, isci_device);

	if (status == SCI_SUCCESS) {
		/* device came up, advertise it to the world */
		domain_dev->lldd_dev = isci_device;
	} else
		isci_put_device(isci_device);
	spin_unlock_irq(&isci_host->scic_lock);

	/* wait for the device ready callback. */
	wait_for_device_start(isci_host, isci_device);

	return status == SCI_SUCCESS ? 0 : -ENODEV;
}
/**
 * isci_device_is_reset_pending() - This function will check if there is any
 *    pending reset condition on the device.
 * @request: This parameter is the isci_device object.
 *
 * true if there is a reset pending for the device.
 */
bool isci_device_is_reset_pending(
	struct isci_host *isci_host,
	struct isci_remote_device *isci_device)
{
	struct isci_request *isci_request;
	struct isci_request *tmp_req;
	bool reset_is_pending = false;
	unsigned long flags;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p\n", __func__, isci_device);

	spin_lock_irqsave(&isci_host->scic_lock, flags);

	/* Check for reset on all pending requests. */
	list_for_each_entry_safe(isci_request, tmp_req,
				 &isci_device->reqs_in_process, dev_node) {
		dev_dbg(&isci_host->pdev->dev,
			"%s: isci_device = %p request = %p\n",
			__func__, isci_device, isci_request);

		if (isci_request->ttype == io_task) {
			struct sas_task *task = isci_request_access_task(
				isci_request);

			spin_lock(&task->task_state_lock);
			if (task->task_state_flags & SAS_TASK_NEED_DEV_RESET)
				reset_is_pending = true;
			spin_unlock(&task->task_state_lock);
		}
	}

	spin_unlock_irqrestore(&isci_host->scic_lock, flags);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_device = %p reset_is_pending = %d\n",
		__func__, isci_device, reset_is_pending);

	return reset_is_pending;
}

/**
 * isci_device_clear_reset_pending() - This function will clear if any pending
 *    reset condition flags on the device.
 * @request: This parameter is the isci_device object.
 *
 * true if there is a reset pending for the device.
 */
void isci_device_clear_reset_pending(struct isci_host *ihost, struct isci_remote_device *idev)
{
	struct isci_request *isci_request;
	struct isci_request *tmp_req;
	unsigned long flags = 0;

	dev_dbg(&ihost->pdev->dev, "%s: idev=%p, ihost=%p\n",
		 __func__, idev, ihost);

	spin_lock_irqsave(&ihost->scic_lock, flags);

	/* Clear reset pending on all pending requests. */
	list_for_each_entry_safe(isci_request, tmp_req,
				 &idev->reqs_in_process, dev_node) {
		dev_dbg(&ihost->pdev->dev, "%s: idev = %p request = %p\n",
			 __func__, idev, isci_request);

		if (isci_request->ttype == io_task) {

			unsigned long flags2;
			struct sas_task *task = isci_request_access_task(
				isci_request);

			spin_lock_irqsave(&task->task_state_lock, flags2);
			task->task_state_flags &= ~SAS_TASK_NEED_DEV_RESET;
			spin_unlock_irqrestore(&task->task_state_lock, flags2);
		}
	}
	spin_unlock_irqrestore(&ihost->scic_lock, flags);
}
