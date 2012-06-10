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

#ifndef _ISCI_REMOTE_DEVICE_H_
#define _ISCI_REMOTE_DEVICE_H_
#include <scsi/libsas.h>
#include <linux/kref.h>
#include "scu_remote_node_context.h"
#include "remote_node_context.h"
#include "port.h"

enum sci_remote_device_not_ready_reason_code {
	SCIC_REMOTE_DEVICE_NOT_READY_START_REQUESTED,
	SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED,
	SCIC_REMOTE_DEVICE_NOT_READY_SATA_REQUEST_STARTED,
	SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED,
	SCIC_REMOTE_DEVICE_NOT_READY_SMP_REQUEST_STARTED,
	SCIC_REMOTE_DEVICE_NOT_READY_REASON_CODE_MAX
};

/**
 * isci_remote_device - isci representation of a sas expander / end point
 * @device_port_width: hw setting for number of simultaneous connections
 * @connection_rate: per-taskcontext connection rate for this device
 * @working_request: SATA requests have no tag we for unaccelerated
 *                   protocols we need a method to associate unsolicited
 *                   frames with a pending request
 */
struct isci_remote_device {
	#define IDEV_START_PENDING 0
	#define IDEV_STOP_PENDING 1
	#define IDEV_ALLOCATED 2
	#define IDEV_GONE 3
	#define IDEV_IO_READY 4
	#define IDEV_IO_NCQERROR 5
	#define IDEV_RNC_LLHANG_ENABLED 6
	#define IDEV_ABORT_PATH_ACTIVE 7
	#define IDEV_ABORT_PATH_RESUME_PENDING 8
	unsigned long flags;
	struct kref kref;
	struct isci_port *isci_port;
	struct domain_device *domain_dev;
	struct list_head node;
	struct sci_base_state_machine sm;
	u32 device_port_width;
	enum sas_linkrate connection_rate;
	struct isci_port *owning_port;
	struct sci_remote_node_context rnc;
	/* XXX unify with device reference counting and delete */
	u32 started_request_count;
	struct isci_request *working_request;
	u32 not_ready_reason;
	scics_sds_remote_node_context_callback abort_resume_cb;
	void *abort_resume_cbparam;
};

#define ISCI_REMOTE_DEVICE_START_TIMEOUT 5000

/* device reference routines must be called under sci_lock */
static inline struct isci_remote_device *isci_get_device(
	struct isci_remote_device *idev)
{
	if (idev)
		kref_get(&idev->kref);
	return idev;
}

static inline struct isci_remote_device *isci_lookup_device(struct domain_device *dev)
{
	struct isci_remote_device *idev = dev->lldd_dev;

	if (idev && !test_bit(IDEV_GONE, &idev->flags)) {
		kref_get(&idev->kref);
		return idev;
	}

	return NULL;
}

void isci_remote_device_release(struct kref *kref);
static inline void isci_put_device(struct isci_remote_device *idev)
{
	if (idev)
		kref_put(&idev->kref, isci_remote_device_release);
}

enum sci_status isci_remote_device_stop(struct isci_host *ihost,
					struct isci_remote_device *idev);
void isci_remote_device_nuke_requests(struct isci_host *ihost,
				      struct isci_remote_device *idev);
void isci_remote_device_gone(struct domain_device *domain_dev);
int isci_remote_device_found(struct domain_device *domain_dev);

/**
 * sci_remote_device_stop() - This method will stop both transmission and
 *    reception of link activity for the supplied remote device.  This method
 *    disables normal IO requests from flowing through to the remote device.
 * @remote_device: This parameter specifies the device to be stopped.
 * @timeout: This parameter specifies the number of milliseconds in which the
 *    stop operation should complete.
 *
 * An indication of whether the device was successfully stopped. SCI_SUCCESS
 * This value is returned if the transmission and reception for the device was
 * successfully stopped.
 */
enum sci_status sci_remote_device_stop(
	struct isci_remote_device *idev,
	u32 timeout);

/**
 * sci_remote_device_reset() - This method will reset the device making it
 *    ready for operation. This method must be called anytime the device is
 *    reset either through a SMP phy control or a port hard reset request.
 * @remote_device: This parameter specifies the device to be reset.
 *
 * This method does not actually cause the device hardware to be reset. This
 * method resets the software object so that it will be operational after a
 * device hardware reset completes. An indication of whether the device reset
 * was accepted. SCI_SUCCESS This value is returned if the device reset is
 * started.
 */
enum sci_status sci_remote_device_reset(
	struct isci_remote_device *idev);

/**
 * sci_remote_device_reset_complete() - This method informs the device object
 *    that the reset operation is complete and the device can resume operation
 *    again.
 * @remote_device: This parameter specifies the device which is to be informed
 *    of the reset complete operation.
 *
 * An indication that the device is resuming operation. SCI_SUCCESS the device
 * is resuming operation.
 */
enum sci_status sci_remote_device_reset_complete(
	struct isci_remote_device *idev);

/**
 * enum sci_remote_device_states - This enumeration depicts all the states
 *    for the common remote device state machine.
 * @SCI_DEV_INITIAL: Simply the initial state for the base remote device
 * state machine.
 *
 * @SCI_DEV_STOPPED: This state indicates that the remote device has
 * successfully been stopped.  In this state no new IO operations are
 * permitted.  This state is entered from the INITIAL state.  This state
 * is entered from the STOPPING state.
 *
 * @SCI_DEV_STARTING: This state indicates the the remote device is in
 * the process of becoming ready (i.e. starting).  In this state no new
 * IO operations are permitted.  This state is entered from the STOPPED
 * state.
 *
 * @SCI_DEV_READY: This state indicates the remote device is now ready.
 * Thus, the user is able to perform IO operations on the remote device.
 * This state is entered from the STARTING state.
 *
 * @SCI_STP_DEV_IDLE: This is the idle substate for the stp remote
 * device.  When there are no active IO for the device it is is in this
 * state.
 *
 * @SCI_STP_DEV_CMD: This is the command state for for the STP remote
 * device.  This state is entered when the device is processing a
 * non-NCQ command.  The device object will fail any new start IO
 * requests until this command is complete.
 *
 * @SCI_STP_DEV_NCQ: This is the NCQ state for the STP remote device.
 * This state is entered when the device is processing an NCQ reuqest.
 * It will remain in this state so long as there is one or more NCQ
 * requests being processed.
 *
 * @SCI_STP_DEV_NCQ_ERROR: This is the NCQ error state for the STP
 * remote device.  This state is entered when an SDB error FIS is
 * received by the device object while in the NCQ state.  The device
 * object will only accept a READ LOG command while in this state.
 *
 * @SCI_STP_DEV_ATAPI_ERROR: This is the ATAPI error state for the STP
 * ATAPI remote device.  This state is entered when ATAPI device sends
 * error status FIS without data while the device object is in CMD
 * state.  A suspension event is expected in this state.  The device
 * object will resume right away.
 *
 * @SCI_STP_DEV_AWAIT_RESET: This is the READY substate indicates the
 * device is waiting for the RESET task coming to be recovered from
 * certain hardware specific error.
 *
 * @SCI_SMP_DEV_IDLE: This is the ready operational substate for the
 * remote device.  This is the normal operational state for a remote
 * device.
 *
 * @SCI_SMP_DEV_CMD: This is the suspended state for the remote device.
 * This is the state that the device is placed in when a RNC suspend is
 * received by the SCU hardware.
 *
 * @SCI_DEV_STOPPING: This state indicates that the remote device is in
 * the process of stopping.  In this state no new IO operations are
 * permitted, but existing IO operations are allowed to complete.  This
 * state is entered from the READY state.  This state is entered from
 * the FAILED state.
 *
 * @SCI_DEV_FAILED: This state indicates that the remote device has
 * failed.  In this state no new IO operations are permitted.  This
 * state is entered from the INITIALIZING state.  This state is entered
 * from the READY state.
 *
 * @SCI_DEV_RESETTING: This state indicates the device is being reset.
 * In this state no new IO operations are permitted.  This state is
 * entered from the READY state.
 *
 * @SCI_DEV_FINAL: Simply the final state for the base remote device
 * state machine.
 */
#define REMOTE_DEV_STATES {\
	C(DEV_INITIAL),\
	C(DEV_STOPPED),\
	C(DEV_STARTING),\
	C(DEV_READY),\
	C(STP_DEV_IDLE),\
	C(STP_DEV_CMD),\
	C(STP_DEV_NCQ),\
	C(STP_DEV_NCQ_ERROR),\
	C(STP_DEV_ATAPI_ERROR),\
	C(STP_DEV_AWAIT_RESET),\
	C(SMP_DEV_IDLE),\
	C(SMP_DEV_CMD),\
	C(DEV_STOPPING),\
	C(DEV_FAILED),\
	C(DEV_RESETTING),\
	C(DEV_FINAL),\
	}
#undef C
#define C(a) SCI_##a
enum sci_remote_device_states REMOTE_DEV_STATES;
#undef C
const char *dev_state_name(enum sci_remote_device_states state);

static inline struct isci_remote_device *rnc_to_dev(struct sci_remote_node_context *rnc)
{
	struct isci_remote_device *idev;

	idev = container_of(rnc, typeof(*idev), rnc);

	return idev;
}

static inline bool dev_is_expander(struct domain_device *dev)
{
	return dev->dev_type == EDGE_DEV || dev->dev_type == FANOUT_DEV;
}

static inline void sci_remote_device_decrement_request_count(struct isci_remote_device *idev)
{
	/* XXX delete this voodoo when converting to the top-level device
	 * reference count
	 */
	if (WARN_ONCE(idev->started_request_count == 0,
		      "%s: tried to decrement started_request_count past 0!?",
			__func__))
		/* pass */;
	else
		idev->started_request_count--;
}

void isci_dev_set_hang_detection_timeout(struct isci_remote_device *idev, u32 timeout);

enum sci_status sci_remote_device_frame_handler(
	struct isci_remote_device *idev,
	u32 frame_index);

enum sci_status sci_remote_device_event_handler(
	struct isci_remote_device *idev,
	u32 event_code);

enum sci_status sci_remote_device_start_io(
	struct isci_host *ihost,
	struct isci_remote_device *idev,
	struct isci_request *ireq);

enum sci_status sci_remote_device_start_task(
	struct isci_host *ihost,
	struct isci_remote_device *idev,
	struct isci_request *ireq);

enum sci_status sci_remote_device_complete_io(
	struct isci_host *ihost,
	struct isci_remote_device *idev,
	struct isci_request *ireq);

void sci_remote_device_post_request(
	struct isci_remote_device *idev,
	u32 request);

enum sci_status sci_remote_device_terminate_requests(
	struct isci_remote_device *idev);

int isci_remote_device_is_safe_to_abort(
	struct isci_remote_device *idev);

enum sci_status
sci_remote_device_abort_requests_pending_abort(
	struct isci_remote_device *idev);

enum sci_status isci_remote_device_suspend(
	struct isci_host *ihost,
	struct isci_remote_device *idev);

enum sci_status sci_remote_device_resume(
	struct isci_remote_device *idev,
	scics_sds_remote_node_context_callback cb_fn,
	void *cb_p);

enum sci_status isci_remote_device_resume_from_abort(
	struct isci_host *ihost,
	struct isci_remote_device *idev);

enum sci_status isci_remote_device_reset(
	struct isci_host *ihost,
	struct isci_remote_device *idev);

enum sci_status isci_remote_device_reset_complete(
	struct isci_host *ihost,
	struct isci_remote_device *idev);

enum sci_status isci_remote_device_suspend_terminate(
	struct isci_host *ihost,
	struct isci_remote_device *idev,
	struct isci_request *ireq);

enum sci_status isci_remote_device_terminate_requests(
	struct isci_host *ihost,
	struct isci_remote_device *idev,
	struct isci_request *ireq);
enum sci_status sci_remote_device_suspend(struct isci_remote_device *idev,
					  enum sci_remote_node_suspension_reasons reason);
#endif /* !defined(_ISCI_REMOTE_DEVICE_H_) */
