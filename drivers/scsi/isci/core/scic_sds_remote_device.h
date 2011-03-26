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

#ifndef _SCIC_SDS_REMOTE_DEVICE_H_
#define _SCIC_SDS_REMOTE_DEVICE_H_

/**
 * This file contains the structures, constants, and prototypes for the
 *    struct scic_sds_remote_device object.
 *
 *
 */

#include "intel_sas.h"
#include "sci_base_remote_device.h"
#include "sci_base_request.h"
#include "scu_remote_node_context.h"
#include "scic_sds_remote_node_context.h"

struct scic_sds_controller;
struct scic_sds_port;
struct scic_sds_request;
struct scic_sds_remote_device_state_handler;

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

#if !defined(DISABLE_ATAPI)
	/**
	 * This is the ATAPI error state for the STP ATAPI remote device.  This state is
	 * entered when ATAPI device sends error status FIS without data while the device
	 * object is in CMD state. A suspension event is expected in this state. The device
	 * object will resume right away.
	 */
	SCIC_SDS_STP_REMOTE_DEVICE_READY_SUBSTATE_ATAPI_ERROR,
#endif

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

/**
 * struct scic_sds_remote_device - This structure contains the data for an SCU
 *    implementation of the SCU Core device data.
 *
 *
 */
struct scic_sds_remote_device {
	/**
	 * This field is the common base for all remote device objects.
	 */
	struct sci_base_remote_device parent;

	/**
	 * This field is the programmed device port width.  This value is written to
	 * the RCN data structure to tell the SCU how many open connections this
	 * device can have.
	 */
	u32 device_port_width;

	/**
	 * This field is the programmed connection rate for this remote device.  It is
	 * used to program the TC with the maximum allowed connection rate.
	 */
	enum sci_sas_link_rate connection_rate;

	/**
	 * This field contains the allowed target protocols for this remote device.
	 */
	struct smp_discover_response_protocols target_protocols;

	/**
	 * This field contains the device SAS address.
	 */
	struct sci_sas_address device_address;

	/**
	 * This filed is assinged the value of true if the device is directly
	 * attached to the port.
	 */
	bool is_direct_attached;

#if !defined(DISABLE_ATAPI)
	/**
	 * This filed is assinged the value of true if the device is an ATAPI
	 * device.
	 */
	bool is_atapi;
#endif

	/**
	 * This filed contains a pointer back to the port to which this device
	 * is assigned.
	 */
	struct scic_sds_port *owning_port;

	/**
	 * This field contains the SCU silicon remote node context specific
	 * information.
	 */
	struct scic_sds_remote_node_context *rnc;

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

typedef enum sci_status (*scic_sds_remote_device_handler_t)(
	struct scic_sds_remote_device *this_device);

typedef enum sci_status (*scic_sds_remote_device_suspend_handler_t)(
	struct scic_sds_remote_device *this_device,
	u32 suspend_type);

typedef enum sci_status (*scic_sds_remote_device_resume_handler_t)(
	struct scic_sds_remote_device *this_device);

typedef enum sci_status (*scic_sds_remote_device_frame_handler_t)(
	struct scic_sds_remote_device *this_device,
	u32 frame_index);

typedef enum sci_status (*scic_sds_remote_device_event_handler_t)(
	struct scic_sds_remote_device *this_device,
	u32 event_code);

typedef void (*scic_sds_remote_device_ready_not_ready_handler_t)(
	struct scic_sds_remote_device *this_device);

/**
 * struct scic_sds_remote_device_state_handler - This structure conains the
 *    state handlers that are needed to process requests for the SCU remote
 *    device objects.
 *
 *
 */
struct scic_sds_remote_device_state_handler {
	struct sci_base_remote_device_state_handler parent;
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
#define scic_sds_remote_device_increment_request_count(this_device) \
	((this_device)->started_request_count++)

/**
 * scic_sds_remote_device_decrement_request_count() -
 *
 * This macro decrements the request count for this device.  This count will
 * never decrment past 0.
 */
#define scic_sds_remote_device_decrement_request_count(this_device) \
	((this_device)->started_request_count > 0 ? \
	 (this_device)->started_request_count-- : 0)

/**
 * scic_sds_remote_device_get_request_count() -
 *
 * This is a helper macro to return the current device request count.
 */
#define scic_sds_remote_device_get_request_count(this_device) \
	((this_device)->started_request_count)

/**
 * scic_sds_remote_device_get_port() -
 *
 * This macro returns the owning port of this remote device obejct.
 */
#define scic_sds_remote_device_get_port(this_device) \
	((this_device)->owning_port)

/**
 * scic_sds_remote_device_get_controller() -
 *
 * This macro returns the controller object that contains this device object
 */
#define scic_sds_remote_device_get_controller(this_device) \
	scic_sds_port_get_controller(scic_sds_remote_device_get_port(this_device))

/**
 * scic_sds_remote_device_set_state_handlers() -
 *
 * This macro sets the remote device state handlers pointer and is set on entry
 * to each device state.
 */
#define scic_sds_remote_device_set_state_handlers(this_device, handlers) \
	((this_device)->state_handlers = (handlers))

/**
 * scic_sds_remote_device_get_port() -
 *
 * This macro returns the owning port of this device
 */
#define scic_sds_remote_device_get_port(this_device) \
	((this_device)->owning_port)

/**
 * scic_sds_remote_device_get_sequence() -
 *
 * This macro returns the remote device sequence value
 */
#define scic_sds_remote_device_get_sequence(this_device) \
	(\
		scic_sds_remote_device_get_controller(this_device)-> \
		remote_device_sequence[(this_device)->rnc->remote_node_index] \
	)

/**
 * scic_sds_remote_device_get_controller_peg() -
 *
 * This macro returns the controllers protocol engine group
 */
#define scic_sds_remote_device_get_controller_peg(this_device) \
	(\
		scic_sds_controller_get_protocol_engine_group(\
			scic_sds_port_get_controller(\
				scic_sds_remote_device_get_port(this_device) \
				) \
			) \
	)

/**
 * scic_sds_remote_device_get_port_index() -
 *
 * This macro returns the port index for the devices owning port
 */
#define scic_sds_remote_device_get_port_index(this_device) \
	(scic_sds_port_get_index(scic_sds_remote_device_get_port(this_device)))

/**
 * scic_sds_remote_device_get_index() -
 *
 * This macro returns the remote node index for this device object
 */
#define scic_sds_remote_device_get_index(this_device) \
	((this_device)->rnc->remote_node_index)

/**
 * scic_sds_remote_device_build_command_context() -
 *
 * This macro builds a remote device context for the SCU post request operation
 */
#define scic_sds_remote_device_build_command_context(device, command) \
	((command) \
	 | (scic_sds_remote_device_get_controller_peg((device)) << SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT) \
	 | (scic_sds_remote_device_get_port_index((device)) << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT) \
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
	struct scic_sds_remote_device *this_device,
	u32 frame_index);

enum sci_status scic_sds_remote_device_event_handler(
	struct scic_sds_remote_device *this_device,
	u32 event_code);

enum sci_status scic_sds_remote_device_start_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *this_device,
	struct scic_sds_request *io_request);

enum sci_status scic_sds_remote_device_complete_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *this_device,
	struct scic_sds_request *io_request);

enum sci_status scic_sds_remote_device_resume(
	struct scic_sds_remote_device *this_device);

enum sci_status scic_sds_remote_device_suspend(
	struct scic_sds_remote_device *this_device,
	u32 suspend_type);

enum sci_status scic_sds_remote_device_start_task(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *this_device,
	struct scic_sds_request *io_request);

void scic_sds_remote_device_post_request(
	struct scic_sds_remote_device *this_device,
	u32 request);

#if !defined(DISABLE_ATAPI)
bool scic_sds_remote_device_is_atapi(
	struct scic_sds_remote_device *this_device);
#else /* !defined(DISABLE_ATAPI) */
#define scic_sds_remote_device_is_atapi(this_device) false
#endif /* !defined(DISABLE_ATAPI) */

void scic_sds_remote_device_start_request(
	struct scic_sds_remote_device *this_device,
	struct scic_sds_request *the_request,
	enum sci_status status);

void scic_sds_remote_device_continue_request(void *sci_dev);

enum sci_status scic_sds_remote_device_default_start_handler(
	struct sci_base_remote_device *this_device);

enum sci_status scic_sds_remote_device_default_fail_handler(
	struct sci_base_remote_device *this_device);

enum sci_status scic_sds_remote_device_default_destruct_handler(
	struct sci_base_remote_device *this_device);

enum sci_status scic_sds_remote_device_default_reset_handler(
	struct sci_base_remote_device *device);

enum sci_status scic_sds_remote_device_default_reset_complete_handler(
	struct sci_base_remote_device *device);

enum sci_status scic_sds_remote_device_default_start_request_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request);

enum sci_status scic_sds_remote_device_default_complete_request_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request);

enum sci_status scic_sds_remote_device_default_continue_request_handler(
	struct sci_base_remote_device *device,
	struct sci_base_request *request);

enum sci_status scic_sds_remote_device_default_suspend_handler(
	struct scic_sds_remote_device *this_device,
	u32 suspend_type);

enum sci_status scic_sds_remote_device_default_resume_handler(
	struct scic_sds_remote_device *this_device);


enum sci_status scic_sds_remote_device_default_frame_handler(
	struct scic_sds_remote_device *this_device,
	u32 frame_index);

enum sci_status scic_sds_remote_device_ready_state_stop_handler(
	struct sci_base_remote_device *device);

enum sci_status scic_sds_remote_device_ready_state_reset_handler(
	struct sci_base_remote_device *device);

enum sci_status scic_sds_remote_device_general_frame_handler(
	struct scic_sds_remote_device *this_device,
	u32 frame_index);

enum sci_status scic_sds_remote_device_general_event_handler(
	struct scic_sds_remote_device *this_device,
	u32 event_code);

enum sci_status scic_sds_ssp_remote_device_ready_suspended_substate_resume_handler(
	struct scic_sds_remote_device *this_device);

#endif /* _SCIC_SDS_REMOTE_DEVICE_H_ */
