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

#ifndef _SCIC_SDS_REMOTE_NODE_CONTEXT_H_
#define _SCIC_SDS_REMOTE_NODE_CONTEXT_H_

/**
 * This file contains the structures, constants, and prototypes associated with
 *    the remote node context in the silicon.  It exists to model and manage
 *    the remote node context in the silicon.
 *
 *
 */

#include "sci_base_state.h"
#include "sci_base_state_machine.h"

/**
 *
 *
 * This constant represents an invalid remote device id, it is used to program
 * the STPDARNI register so the driver knows when it has received a SIGNATURE
 * FIS from the SCU.
 */
#define SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX    0x0FFF

#define SCU_HARDWARE_SUSPENSION  (0)
#define SCI_SOFTWARE_SUSPENSION  (1)

struct scic_sds_request;
struct scic_sds_remote_device;
struct scic_sds_remote_node_context;

typedef void (*scics_sds_remote_node_context_callback)(void *);

typedef enum sci_status (*scic_sds_remote_node_context_operation)(
	struct scic_sds_remote_node_context *this_rnc,
	scics_sds_remote_node_context_callback the_callback,
	void *callback_parameter
	);

typedef enum sci_status (*scic_sds_remote_node_context_suspend_operation)(
	struct scic_sds_remote_node_context *this_rnc,
	u32 suspension_type,
	scics_sds_remote_node_context_callback the_callback,
	void *callback_parameter
	);

typedef enum sci_status (*scic_sds_remote_node_context_io_request)(
	struct scic_sds_remote_node_context *this_rnc,
	struct scic_sds_request *the_request
	);

typedef enum sci_status (*scic_sds_remote_node_context_event_handler)(
	struct scic_sds_remote_node_context *this_rnc,
	u32 event_code
	);

struct scic_sds_remote_node_context_handlers {
	/**
	 * This handle is invoked to stop the RNC.  The callback is invoked when after
	 * the hardware notification that the RNC has been invalidated.
	 */
	scic_sds_remote_node_context_operation destruct_handler;

	/**
	 * This handler is invoked when there is a request to suspend  the RNC.  The
	 * callback is invoked after the hardware notification that the remote node is
	 * suspended.
	 */
	scic_sds_remote_node_context_suspend_operation suspend_handler;

	/**
	 * This handler is invoked when there is a request to resume the RNC.  The
	 * callback is invoked when after the RNC has reached the ready state.
	 */
	scic_sds_remote_node_context_operation resume_handler;

	/**
	 * This handler is invoked when there is a request to start an io request
	 * operation.
	 */
	scic_sds_remote_node_context_io_request start_io_handler;

	/**
	 * This handler is invoked when there is a request to start a task request
	 * operation.
	 */
	scic_sds_remote_node_context_io_request start_task_handler;

	/**
	 * This handler is invoked where there is an RNC event that must be processed.
	 */
	scic_sds_remote_node_context_event_handler event_handler;

};

/**
 * This is the enumeration of the remote node context states.
 */
enum scis_sds_remote_node_context_states {
	/**
	 * This state is the initial state for a remote node context.  On a resume
	 * request the remote node context will transition to the posting state.
	 */
	SCIC_SDS_REMOTE_NODE_CONTEXT_INITIAL_STATE,

	/**
	 * This is a transition state that posts the RNi to the hardware. Once the RNC
	 * is posted the remote node context will be made ready.
	 */
	SCIC_SDS_REMOTE_NODE_CONTEXT_POSTING_STATE,

	/**
	 * This is a transition state that will post an RNC invalidate to the
	 * hardware.  Once the invalidate is complete the remote node context will
	 * transition to the posting state.
	 */
	SCIC_SDS_REMOTE_NODE_CONTEXT_INVALIDATING_STATE,

	/**
	 * This is a transition state that will post an RNC resume to the hardare.
	 * Once the event notification of resume complete is received the remote node
	 * context will transition to the ready state.
	 */
	SCIC_SDS_REMOTE_NODE_CONTEXT_RESUMING_STATE,

	/**
	 * This is the state that the remote node context must be in to accept io
	 * request operations.
	 */
	SCIC_SDS_REMOTE_NODE_CONTEXT_READY_STATE,

	/**
	 * This is the state that the remote node context transitions to when it gets
	 * a TX suspend notification from the hardware.
	 */
	SCIC_SDS_REMOTE_NODE_CONTEXT_TX_SUSPENDED_STATE,

	/**
	 * This is the state that the remote node context transitions to when it gets
	 * a TX RX suspend notification from the hardware.
	 */
	SCIC_SDS_REMOTE_NODE_CONTEXT_TX_RX_SUSPENDED_STATE,

	/**
	 * This state is a wait state for the remote node context that waits for a
	 * suspend notification from the hardware.  This state is entered when either
	 * there is a request to supend the remote node context or when there is a TC
	 * completion where the remote node will be suspended by the hardware.
	 */
	SCIC_SDS_REMOTE_NODE_CONTEXT_AWAIT_SUSPENSION_STATE,

	SCIC_SDS_REMOTE_NODE_CONTEXT_MAX_STATES

};

/**
 *
 *
 * This enumeration is used to define the end destination state for the remote
 * node context.
 */
enum scic_sds_remote_node_context_destination_state {
	SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_UNSPECIFIED,
	SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_READY,
	SCIC_SDS_REMOTE_NODE_DESTINATION_STATE_FINAL
};

/**
 * struct scic_sds_remote_node_context - This structure contains the data
 *    associated with the remote node context object.  The remote node context
 *    (RNC) object models the the remote device information necessary to manage
 *    the silicon RNC.
 */
struct scic_sds_remote_node_context {
	/*
	 * parent object
	 */
	struct sci_base_object parent;

	/**
	 * This pointer simply points to the remote device object containing
	 * this RNC.
	 *
	 * @todo Consider making the device pointer the associated object of the
	 *       the parent object.
	 */
	struct scic_sds_remote_device *device;

	/**
	 * This field indicates the remote node index (RNI) associated with
	 * this RNC.
	 */
	u16 remote_node_index;

	/**
	 * This field is the recored suspension code or the reason for the remote node
	 * context suspension.
	 */
	u32 suspension_code;

	/**
	 * This field is true if the remote node context is resuming from its current
	 * state.  This can cause an automatic resume on receiving a suspension
	 * notification.
	 */
	enum scic_sds_remote_node_context_destination_state destination_state;

	/**
	 * This field contains the callback function that the user requested to be
	 * called when the requested state transition is complete.
	 */
	scics_sds_remote_node_context_callback user_callback;

	/**
	 * This field contains the parameter that is called when the user requested
	 * state transition is completed.
	 */
	void *user_cookie;

	/**
	 * This field contains the data for the object's state machine.
	 */
	struct sci_base_state_machine state_machine;

	struct scic_sds_remote_node_context_handlers *state_handlers;
};

void scic_sds_remote_node_context_construct(
	struct scic_sds_remote_device *device,
	struct scic_sds_remote_node_context *rnc,
	u16 remote_node_index);


bool scic_sds_remote_node_context_is_ready(
	struct scic_sds_remote_node_context *this_rnc);

#define scic_sds_remote_node_context_get_remote_node_index(rcn)	\
	((rnc)->remote_node_index)

#define scic_sds_remote_node_context_event_handler(rnc, event_code) \
	((rnc)->state_handlers->event_handler(rnc, event_code))

#define scic_sds_remote_node_context_resume(rnc, callback, parameter) \
	((rnc)->state_handlers->resume_handler(rnc, callback, parameter))

#define scic_sds_remote_node_context_suspend(rnc, suspend_type, callback, parameter) \
	((rnc)->state_handlers->suspend_handler(rnc, suspend_type, callback, parameter))

#define scic_sds_remote_node_context_destruct(rnc, callback, parameter)	\
	((rnc)->state_handlers->destruct_handler(rnc, callback, parameter))

#define scic_sds_remote_node_context_start_io(rnc, request) \
	((rnc)->state_handlers->start_io_handler(rnc, request))

#define scic_sds_remote_node_context_start_task(rnc, task) \
	((rnc)->state_handlers->start_task_handler(rnc, task))

#endif  /* _SCIC_SDS_REMOTE_NODE_CONTEXT_H_ */
