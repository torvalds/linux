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
#include "sci_status.h"
#include "intel_sas.h"
#include "scu_remote_node_context.h"
#include "remote_node_context.h"
#include "port.h"

enum scic_remote_device_not_ready_reason_code {
	SCIC_REMOTE_DEVICE_NOT_READY_START_REQUESTED,
	SCIC_REMOTE_DEVICE_NOT_READY_STOP_REQUESTED,
	SCIC_REMOTE_DEVICE_NOT_READY_SATA_REQUEST_STARTED,
	SCIC_REMOTE_DEVICE_NOT_READY_SATA_SDB_ERROR_FIS_RECEIVED,
	SCIC_REMOTE_DEVICE_NOT_READY_SMP_REQUEST_STARTED,

	SCIC_REMOTE_DEVICE_NOT_READY_REASON_CODE_MAX

};

struct scic_sds_remote_device {
	/**
	 * The field specifies that the parent object for the base remote
	 * device is the base object itself.
	 */
	struct sci_base_object parent;

	/**
	 * This field contains the information for the base remote device state
	 * machine.
	 */
	struct sci_base_state_machine state_machine;

	/**
	 * This field is the programmed device port width.  This value is
	 * written to the RCN data structure to tell the SCU how many open
	 * connections this device can have.
	 */
	u32 device_port_width;

	/**
	 * This field is the programmed connection rate for this remote device.  It is
	 * used to program the TC with the maximum allowed connection rate.
	 */
	enum sas_linkrate connection_rate;

	/**
	 * This filed is assinged the value of true if the device is directly
	 * attached to the port.
	 */
	bool is_direct_attached;

	/**
	 * This filed contains a pointer back to the port to which this device
	 * is assigned.
	 */
	struct scic_sds_port *owning_port;

	/**
	 * This field contains the SCU silicon remote node context specific
	 * information.
	 */
	struct scic_sds_remote_node_context rnc;

	/**
	 * This field contains the stated request count for the remote device.  The
	 * device can not reach the SCI_BASE_REMOTE_DEVICE_STATE_STOPPED until all
	 * requests are complete and the rnc_posted value is false.
	 */
	u32 started_request_count;

	/**
	 * This field contains a pointer to the working request object.  It is only
	 * used only for SATA requests since the unsolicited frames we get from the
	 * hardware have no Tag value to look up the io request object.
	 */
	struct scic_sds_request *working_request;

	/**
	 * This field contains the reason for the remote device going not_ready.  It is
	 * assigned in the state handlers and used in the state transition.
	 */
	u32 not_ready_reason;

	/**
	 * This field is true if this remote device has an initialzied ready substate
	 * machine. SSP devices do not have a ready substate machine and STP devices
	 * have a ready substate machine.
	 */
	bool has_ready_substate_machine;

	/**
	 * This field contains the state machine for the ready substate machine for
	 * this struct scic_sds_remote_device object.
	 */
	struct sci_base_state_machine ready_substate_machine;

	/**
	 * This field maintains the set of state handlers for the remote device
	 * object.  These are changed each time the remote device enters a new state.
	 */
	const struct scic_sds_remote_device_state_handler *state_handlers;
};

struct isci_remote_device {
	enum isci_status status;
	#define IDEV_START_PENDING 0
	#define IDEV_STOP_PENDING 1
	#define IDEV_ALLOCATED 2
	unsigned long flags;
	struct isci_port *isci_port;
	struct domain_device *domain_dev;
	struct list_head node;
	struct list_head reqs_in_process;
	spinlock_t state_lock;
	struct scic_sds_remote_device sci;
};

#define ISCI_REMOTE_DEVICE_START_TIMEOUT 5000

enum sci_status isci_remote_device_stop(struct isci_host *ihost,
					struct isci_remote_device *idev);
void isci_remote_device_nuke_requests(struct isci_host *ihost,
				      struct isci_remote_device *idev);
void isci_remote_device_ready(struct isci_host *ihost,
			      struct isci_remote_device *idev);
void isci_remote_device_not_ready(struct isci_host *ihost,
				  struct isci_remote_device *idev, u32 reason);
void isci_remote_device_gone(struct domain_device *domain_dev);
int isci_remote_device_found(struct domain_device *domain_dev);
bool isci_device_is_reset_pending(struct isci_host *ihost,
				  struct isci_remote_device *idev);
void isci_device_clear_reset_pending(struct isci_host *ihost,
				     struct isci_remote_device *idev);
void isci_remote_device_change_state(struct isci_remote_device *idev,
				     enum isci_status status);
/**
 * scic_remote_device_stop() - This method will stop both transmission and
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
enum sci_status scic_remote_device_stop(
	struct scic_sds_remote_device *remote_device,
	u32 timeout);

/**
 * scic_remote_device_reset() - This method will reset the device making it
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
enum sci_status scic_remote_device_reset(
	struct scic_sds_remote_device *remote_device);

/**
 * scic_remote_device_reset_complete() - This method informs the device object
 *    that the reset operation is complete and the device can resume operation
 *    again.
 * @remote_device: This parameter specifies the device which is to be informed
 *    of the reset complete operation.
 *
 * An indication that the device is resuming operation. SCI_SUCCESS the device
 * is resuming operation.
 */
enum sci_status scic_remote_device_reset_complete(
	struct scic_sds_remote_device *remote_device);



/**
 * scic_remote_device_get_connection_rate() - This method simply returns the
 *    link rate at which communications to the remote device occur.
 * @remote_device: This parameter specifies the device for which to get the
 *    connection rate.
 *
 * Return the link rate at which we transfer for the supplied remote device.
 */
enum sas_linkrate scic_remote_device_get_connection_rate(
	struct scic_sds_remote_device *remote_device);

#define scic_remote_device_is_atapi(device_handle) false

/**
 * enum scic_sds_remote_device_states - This enumeration depicts all the states
 *    for the common remote device state machine.
 *
 *
 */
enum scic_sds_remote_device_states {
	/**
	 * Simply the initial state for the base remote device state machine.
	 */
	SCI_BASE_REMOTE_DEVICE_STATE_INITIAL,

	/**
	 * This state indicates that the remote device has successfully been
	 * stopped.  In this state no new IO operations are permitted.
	 * This state is entered from the INITIAL state.
	 * This state is entered from the STOPPING state.
	 */
	SCI_BASE_REMOTE_DEVICE_STATE_STOPPED,

	/**
	 * This state indicates the the remote device is in the process of
	 * becoming ready (i.e. starting).  In this state no new IO operations
	 * are permitted.
	 * This state is entered from the STOPPED state.
	 */
	SCI_BASE_REMOTE_DEVICE_STATE_STARTING,

	/**
	 * This state indicates the remote device is now ready.  Thus, the user
	 * is able to perform IO operations on the remote device.
	 * This state is entered from the STARTING state.
	 */
	SCI_BASE_REMOTE_DEVICE_STATE_READY,

	/**
	 * This state indicates that the remote device is in the process of
	 * stopping.  In this state no new IO operations are permitted, but
	 * existing IO operations are allowed to complete.
	 * This state is entered from the READY state.
	 * This state is entered from the FAILED state.
	 */
	SCI_BASE_REMOTE_DEVICE_STATE_STOPPING,

	/**
	 * This state indicates that the remote device has failed.
	 * In this state no new IO operations are permitted.
	 * This state is entered from the INITIALIZING state.
	 * This state is entered from the READY state.
	 */
	SCI_BASE_REMOTE_DEVICE_STATE_FAILED,

	/**
	 * This state indicates the device is being reset.
	 * In this state no new IO operations are permitted.
	 * This state is entered from the READY state.
	 */
	SCI_BASE_REMOTE_DEVICE_STATE_RESETTING,

	/**
	 * Simply the final state for the base remote device state machine.
	 */
	SCI_BASE_REMOTE_DEVICE_STATE_FINAL,
};

/**
 * enum scic_sds_ssp_remote_device_ready_substates -
 *
 * This is the enumeration of the ready substates for the
 * struct scic_sds_remote_device.
 */
enum scic_sds_ssp_remote_device_ready_substates {
	/**
	 * This is the initial state for the remote device ready substate.
	 */
	SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATE_INITIAL,

	/**
	 * This is the ready operational substate for the remote device.
	 * This is the normal operational state for a remote device.
	 */
	SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATE_OPERATIONAL,

	/**
	 * This is the suspended state for the remote device. This is the state
	 * that the device is placed in when a RNC suspend is received by
	 * the SCU hardware.
	 */
	SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATE_SUSPENDED,

	/**
	 * This is the final state that the device is placed in before a change
	 * to the base state machine.
	 */
	SCIC_SDS_SSP_REMOTE_DEVICE_READY_SUBSTATE_FINAL,

	SCIC_SDS_SSP_REMOTE_DEVICE_READY_MAX_SUBSTATES
};

/**
 * enum scic_sds_stp_remote_device_ready_substates -
 *
 * This is the enumeration for the struct scic_sds_remote_device ready substates
 * for the STP remote device.
 */
enum scic_sds_stp_remote_device_ready_substates {
	/**
	 * This is the idle substate for the stp remote device.  When there are no
	 * active IO for the device it is is in this state.
	 */
	SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_IDLE,

	/**
	 * This is the command state for for the STP remote device.  This state is
	 * entered when the device is processing a non-NCQ command.  The device object
	 * will fail any new start IO requests until this command is complete.
	 */
	SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_CMD,

	/**
	 * This is the NCQ state for the STP remote device.  This state is entered
	 * when the device is processing an NCQ reuqest.  It will remain in this state
	 * so long as there is one or more NCQ requests being processed.
	 */
	SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ,

	/**
	 * This is the NCQ error state for the STP remote device.  This state is
	 * entered when an SDB error FIS is received by the device object while in the
	 * NCQ state.  The device object will only accept a READ LOG command while in
	 * this state.
	 */
	SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_NCQ_ERROR,

	/**
	 * This is the READY substate indicates the device is waiting for the RESET task
	 * coming to be recovered from certain hardware specific error.
	 */
	SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_AWAIT_RESET,
};

/**
 * enum scic_sds_smp_remote_device_ready_substates -
 *
 * This is the enumeration of the ready substates for the SMP REMOTE DEVICE.
 */
enum scic_sds_smp_remote_device_ready_substates {
	/**
	 * This is the ready operational substate for the remote device.  This is the
	 * normal operational state for a remote device.
	 */
	SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_IDLE,

	/**
	 * This is the suspended state for the remote device.  This is the state that
	 * the device is placed in when a RNC suspend is received by the SCU hardware.
	 */
	SCIC_SDS_SMP_REMOTE_DEVICE_READY_SUBSTATE_CMD,
};

static inline struct scic_sds_remote_device *rnc_to_dev(struct scic_sds_remote_node_context *rnc)
{
	struct scic_sds_remote_device *sci_dev;

	sci_dev = container_of(rnc, typeof(*sci_dev), rnc);

	return sci_dev;
}

static inline struct domain_device *sci_dev_to_domain(struct scic_sds_remote_device *sci_dev)
{
	struct isci_remote_device *idev = container_of(sci_dev, typeof(*idev), sci);

	return idev->domain_dev;
}

static inline bool dev_is_expander(struct domain_device *dev)
{
	return dev->dev_type == EDGE_DEV || dev->dev_type == FANOUT_DEV;
}

typedef enum sci_status (*scic_sds_remote_device_request_handler_t)(
	struct scic_sds_remote_device *device,
	struct scic_sds_request *request);

typedef enum sci_status (*scic_sds_remote_device_high_priority_request_complete_handler_t)(
	struct scic_sds_remote_device *device,
	struct scic_sds_request *request,
	void *,
	enum sci_io_status);

typedef enum sci_status (*scic_sds_remote_device_handler_t)(
	struct scic_sds_remote_device *sci_dev);

typedef enum sci_status (*scic_sds_remote_device_suspend_handler_t)(
	struct scic_sds_remote_device *sci_dev,
	u32 suspend_type);

typedef enum sci_status (*scic_sds_remote_device_resume_handler_t)(
	struct scic_sds_remote_device *sci_dev);

typedef enum sci_status (*scic_sds_remote_device_frame_handler_t)(
	struct scic_sds_remote_device *sci_dev,
	u32 frame_index);

typedef enum sci_status (*scic_sds_remote_device_event_handler_t)(
	struct scic_sds_remote_device *sci_dev,
	u32 event_code);

typedef void (*scic_sds_remote_device_ready_not_ready_handler_t)(
	struct scic_sds_remote_device *sci_dev);

/**
 * struct scic_sds_remote_device_state_handler - This structure conains the
 *    state handlers that are needed to process requests for the SCU remote
 *    device objects.
 *
 *
 */
struct scic_sds_remote_device_state_handler {
	/**
	 * The start_handler specifies the method invoked when a user
	 * attempts to start a remote device.
	 */
	scic_sds_remote_device_handler_t start_handler;

	/**
	 * The stop_handler specifies the method invoked when a user attempts to
	 * stop a remote device.
	 */
	scic_sds_remote_device_handler_t stop_handler;

	/**
	 * The fail_handler specifies the method invoked when a remote device
	 * failure has occurred.  A failure may be due to an inability to
	 * initialize/configure the device.
	 */
	scic_sds_remote_device_handler_t fail_handler;

	/**
	 * The destruct_handler specifies the method invoked when attempting to
	 * destruct a remote device.
	 */
	scic_sds_remote_device_handler_t destruct_handler;

	/**
	 * The reset handler specifies the method invloked when requesting to
	 * reset a remote device.
	 */
	scic_sds_remote_device_handler_t reset_handler;

	/**
	 * The reset complete handler specifies the method invloked when
	 * reporting that a reset has completed to the remote device.
	 */
	scic_sds_remote_device_handler_t reset_complete_handler;

	/**
	 * The start_io_handler specifies the method invoked when a user
	 * attempts to start an IO request for a remote device.
	 */
	scic_sds_remote_device_request_handler_t start_io_handler;

	/**
	 * The complete_io_handler specifies the method invoked when a user
	 * attempts to complete an IO request for a remote device.
	 */
	scic_sds_remote_device_request_handler_t complete_io_handler;

	/**
	 * The continue_io_handler specifies the method invoked when a user
	 * attempts to continue an IO request for a remote device.
	 */
	scic_sds_remote_device_request_handler_t continue_io_handler;

	/**
	 * The start_task_handler specifies the method invoked when a user
	 * attempts to start a task management request for a remote device.
	 */
	scic_sds_remote_device_request_handler_t start_task_handler;

	/**
	 * The complete_task_handler specifies the method invoked when a user
	 * attempts to complete a task management request for a remote device.
	 */
	scic_sds_remote_device_request_handler_t complete_task_handler;


	scic_sds_remote_device_suspend_handler_t suspend_handler;
	scic_sds_remote_device_resume_handler_t resume_handler;
	scic_sds_remote_device_event_handler_t event_handler;
	scic_sds_remote_device_frame_handler_t frame_handler;
};

extern const struct sci_base_state scic_sds_ssp_remote_device_ready_substate_table[];
extern const struct sci_base_state scic_sds_stp_remote_device_ready_substate_table[];
extern const struct sci_base_state scic_sds_smp_remote_device_ready_substate_table[];

/**
 * scic_sds_remote_device_increment_request_count() -
 *
 * This macro incrments the request count for this device
 */
#define scic_sds_remote_device_increment_request_count(sci_dev) \
	((sci_dev)->started_request_count++)

/**
 * scic_sds_remote_device_decrement_request_count() -
 *
 * This macro decrements the request count for this device.  This count will
 * never decrment past 0.
 */
#define scic_sds_remote_device_decrement_request_count(sci_dev) \
	((sci_dev)->started_request_count > 0 ? \
	 (sci_dev)->started_request_count-- : 0)

/**
 * scic_sds_remote_device_get_request_count() -
 *
 * This is a helper macro to return the current device request count.
 */
#define scic_sds_remote_device_get_request_count(sci_dev) \
	((sci_dev)->started_request_count)

/**
 * scic_sds_remote_device_get_port() -
 *
 * This macro returns the owning port of this remote device obejct.
 */
#define scic_sds_remote_device_get_port(sci_dev) \
	((sci_dev)->owning_port)

/**
 * scic_sds_remote_device_get_controller() -
 *
 * This macro returns the controller object that contains this device object
 */
#define scic_sds_remote_device_get_controller(sci_dev) \
	scic_sds_port_get_controller(scic_sds_remote_device_get_port(sci_dev))

/**
 * scic_sds_remote_device_set_state_handlers() -
 *
 * This macro sets the remote device state handlers pointer and is set on entry
 * to each device state.
 */
#define scic_sds_remote_device_set_state_handlers(sci_dev, handlers) \
	((sci_dev)->state_handlers = (handlers))

/**
 * scic_sds_remote_device_get_port() -
 *
 * This macro returns the owning port of this device
 */
#define scic_sds_remote_device_get_port(sci_dev) \
	((sci_dev)->owning_port)

/**
 * scic_sds_remote_device_get_sequence() -
 *
 * This macro returns the remote device sequence value
 */
#define scic_sds_remote_device_get_sequence(sci_dev) \
	(\
		scic_sds_remote_device_get_controller(sci_dev)-> \
		remote_device_sequence[(sci_dev)->rnc.remote_node_index] \
	)

/**
 * scic_sds_remote_device_get_controller_peg() -
 *
 * This macro returns the controllers protocol engine group
 */
#define scic_sds_remote_device_get_controller_peg(sci_dev) \
	(\
		scic_sds_controller_get_protocol_engine_group(\
			scic_sds_port_get_controller(\
				scic_sds_remote_device_get_port(sci_dev) \
				) \
			) \
	)

/**
 * scic_sds_remote_device_get_index() -
 *
 * This macro returns the remote node index for this device object
 */
#define scic_sds_remote_device_get_index(sci_dev) \
	((sci_dev)->rnc.remote_node_index)

/**
 * scic_sds_remote_device_build_command_context() -
 *
 * This macro builds a remote device context for the SCU post request operation
 */
#define scic_sds_remote_device_build_command_context(device, command) \
	((command) \
	 | (scic_sds_remote_device_get_controller_peg((device)) << SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) \
	 | ((device)->owning_port->physical_port_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT) \
	 | (scic_sds_remote_device_get_index((device)))	\
	)

/**
 * scic_sds_remote_device_set_working_request() -
 *
 * This macro makes the working request assingment for the remote device
 * object. To clear the working request use this macro with a NULL request
 * object.
 */
#define scic_sds_remote_device_set_working_request(device, request) \
	((device)->working_request = (request))

enum sci_status scic_sds_remote_device_frame_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 frame_index);

enum sci_status scic_sds_remote_device_event_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 event_code);

enum sci_status scic_sds_remote_device_start_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *io_request);

enum sci_status scic_sds_remote_device_complete_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *io_request);

enum sci_status scic_sds_remote_device_resume(
	struct scic_sds_remote_device *sci_dev);

enum sci_status scic_sds_remote_device_suspend(
	struct scic_sds_remote_device *sci_dev,
	u32 suspend_type);

enum sci_status scic_sds_remote_device_start_task(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *io_request);

void scic_sds_remote_device_post_request(
	struct scic_sds_remote_device *sci_dev,
	u32 request);

#define scic_sds_remote_device_is_atapi(sci_dev) false

void scic_sds_remote_device_start_request(
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *sci_req,
	enum sci_status status);

void scic_sds_remote_device_continue_request(void *sci_dev);

enum sci_status scic_sds_remote_device_default_start_handler(
	struct scic_sds_remote_device *sci_dev);

enum sci_status scic_sds_remote_device_default_fail_handler(
	struct scic_sds_remote_device *sci_dev);

enum sci_status scic_sds_remote_device_default_destruct_handler(
	struct scic_sds_remote_device *sci_dev);

enum sci_status scic_sds_remote_device_default_reset_handler(
	struct scic_sds_remote_device *device);

enum sci_status scic_sds_remote_device_default_reset_complete_handler(
	struct scic_sds_remote_device *device);

enum sci_status scic_sds_remote_device_default_start_request_handler(
	struct scic_sds_remote_device *device,
	struct scic_sds_request *request);

enum sci_status scic_sds_remote_device_default_complete_request_handler(
	struct scic_sds_remote_device *device,
	struct scic_sds_request *request);

enum sci_status scic_sds_remote_device_default_continue_request_handler(
	struct scic_sds_remote_device *device,
	struct scic_sds_request *request);

enum sci_status scic_sds_remote_device_default_suspend_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 suspend_type);

enum sci_status scic_sds_remote_device_default_resume_handler(
	struct scic_sds_remote_device *sci_dev);


enum sci_status scic_sds_remote_device_default_frame_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 frame_index);

enum sci_status scic_sds_remote_device_ready_state_stop_handler(
	struct scic_sds_remote_device *device);

enum sci_status scic_sds_remote_device_ready_state_reset_handler(
	struct scic_sds_remote_device *device);

enum sci_status scic_sds_remote_device_general_frame_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 frame_index);

enum sci_status scic_sds_remote_device_general_event_handler(
	struct scic_sds_remote_device *sci_dev,
	u32 event_code);

enum sci_status scic_sds_ssp_remote_device_ready_suspended_substate_resume_handler(
	struct scic_sds_remote_device *sci_dev);



#endif /* !defined(_ISCI_REMOTE_DEVICE_H_) */
