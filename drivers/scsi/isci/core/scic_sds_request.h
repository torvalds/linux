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

#ifndef _SCIC_SDS_IO_REQUEST_H_
#define _SCIC_SDS_IO_REQUEST_H_

/**
 * This file contains the structures, constants and prototypes for the
 *    SCIC_SDS_IO_REQUEST object.
 *
 *
 */

#include "scic_io_request.h"

#include "sci_base_request.h"
#include "scu_task_context.h"
#include "intel_sas.h"

struct scic_sds_controller;
struct scic_sds_remote_device;
struct scic_sds_io_request_state_handler;

/**
 * enum _scic_sds_io_request_started_task_mgmt_substates - This enumeration
 *    depicts all of the substates for a task management request to be
 *    performed in the STARTED super-state.
 *
 *
 */
enum scic_sds_raw_request_started_task_mgmt_substates {
	/**
	 * The AWAIT_TC_COMPLETION sub-state indicates that the started raw
	 * task management request is waiting for the transmission of the
	 * initial frame (i.e. command, task, etc.).
	 */
	SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_COMPLETION,

	/**
	 * This sub-state indicates that the started task management request
	 * is waiting for the reception of an unsolicited frame
	 * (i.e. response IU).
	 */
	SCIC_SDS_IO_REQUEST_STARTED_TASK_MGMT_SUBSTATE_AWAIT_TC_RESPONSE,
};


/**
 * enum _scic_sds_smp_request_started_substates - This enumeration depicts all
 *    of the substates for a SMP request to be performed in the STARTED
 *    super-state.
 *
 *
 */
enum scic_sds_smp_request_started_substates {
	/**
	 * This sub-state indicates that the started task management request
	 * is waiting for the reception of an unsolicited frame
	 * (i.e. response IU).
	 */
	SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_RESPONSE,

	/**
	 * The AWAIT_TC_COMPLETION sub-state indicates that the started SMP request is
	 * waiting for the transmission of the initial frame (i.e. command, task, etc.).
	 */
	SCIC_SDS_SMP_REQUEST_STARTED_SUBSTATE_AWAIT_TC_COMPLETION,
};

/**
 * struct scic_sds_request - This structure contains or references all of
 *    the data necessary to process a task management or normal IO request.
 *
 *
 */
struct scic_sds_request {
	/**
	 * This field indictes the parent object of the request.
	 */
	struct sci_base_request parent;

	void *user_request;

	/**
	 * This field simply points to the controller to which this IO request
	 * is associated.
	 */
	struct scic_sds_controller *owning_controller;

	/**
	 * This field simply points to the remote device to which this IO request
	 * is associated.
	 */
	struct scic_sds_remote_device *target_device;

	/**
	 * This field is utilized to determine if the SCI user is managing
	 * the IO tag for this request or if the core is managing it.
	 */
	bool was_tag_assigned_by_user;

	/**
	 * This field indicates the IO tag for this request.  The IO tag is
	 * comprised of the task_index and a sequence count. The sequence count
	 * is utilized to help identify tasks from one life to another.
	 */
	u16 io_tag;

	/**
	 * This field specifies the protocol being utilized for this
	 * IO request.
	 */
	SCIC_TRANSPORT_PROTOCOL protocol;

	/**
	 * This field indicates the completion status taken from the SCUs
	 * completion code.  It indicates the completion result for the SCU hardware.
	 */
	u32 scu_status;

	/**
	 * This field indicates the completion status returned to the SCI user.  It
	 * indicates the users view of the io request completion.
	 */
	u32 sci_status;

	/**
	 * This field contains the value to be utilized when posting (e.g. Post_TC,
	 * Post_TC_Abort) this request to the silicon.
	 */
	u32 post_context;

	void *command_buffer;
	void *response_buffer;
	struct scu_task_context *task_context_buffer;
	struct scu_sgl_element_pair *sgl_element_pair_buffer;

	/**
	 * This field indicates if this request is a task management request or
	 * normal IO request.
	 */
	bool is_task_management_request;

	/**
	 * This field indicates that this request contains an initialized started
	 * substate machine.
	 */
	bool has_started_substate_machine;

	/**
	 * This field is a pointer to the stored rx frame data.  It is used in STP
	 * internal requests and SMP response frames.  If this field is non-NULL the
	 * saved frame must be released on IO request completion.
	 *
	 * @todo In the future do we want to keep a list of RX frame buffers?
	 */
	u32 saved_rx_frame_index;

	/**
	 * This field specifies the data necessary to manage the sub-state
	 * machine executed while in the SCI_BASE_REQUEST_STATE_STARTED state.
	 */
	struct sci_base_state_machine started_substate_machine;

	/**
	 * This field specifies the current state handlers in place for this
	 * IO Request object.  This field is updated each time the request
	 * changes state.
	 */
	const struct scic_sds_io_request_state_handler *state_handlers;

	/**
	 * This field in the recorded device sequence for the io request.  This is
	 * recorded during the build operation and is compared in the start
	 * operation.  If the sequence is different then there was a change of
	 * devices from the build to start operations.
	 */
	u8 device_sequence;

};


typedef enum sci_status
(*scic_sds_io_request_frame_handler_t)(struct scic_sds_request *req, u32 frame);

typedef enum sci_status
(*scic_sds_io_request_event_handler_t)(struct scic_sds_request *req, u32 event);

typedef enum sci_status
(*scic_sds_io_request_task_completion_handler_t)(struct scic_sds_request *req, u32 completion_code);

/**
 * struct scic_sds_io_request_state_handler - This is the SDS core definition
 *    of the state handlers.
 *
 *
 */
struct scic_sds_io_request_state_handler {
	struct sci_base_request_state_handler parent;

	scic_sds_io_request_task_completion_handler_t tc_completion_handler;
	scic_sds_io_request_event_handler_t event_handler;
	scic_sds_io_request_frame_handler_t frame_handler;

};

extern const struct sci_base_state scic_sds_io_request_started_task_mgmt_substate_table[];

/**
 *
 *
 * This macro returns the maximum number of SGL element paris that we will
 * support in a single IO request.
 */
#define SCU_MAX_SGL_ELEMENT_PAIRS ((SCU_IO_REQUEST_SGE_COUNT + 1) / 2)

/**
 * scic_sds_request_get_controller() -
 *
 * This macro will return the controller for this io request object
 */
#define scic_sds_request_get_controller(this_request) \
	((this_request)->owning_controller)

/**
 * scic_sds_request_get_device() -
 *
 * This macro will return the device for this io request object
 */
#define scic_sds_request_get_device(this_request) \
	((this_request)->target_device)

/**
 * scic_sds_request_get_port() -
 *
 * This macro will return the port for this io request object
 */
#define scic_sds_request_get_port(this_request)	\
	scic_sds_remote_device_get_port(scic_sds_request_get_device(this_request))

/**
 * scic_sds_request_get_post_context() -
 *
 * This macro returns the constructed post context result for the io request.
 */
#define scic_sds_request_get_post_context(this_request)	\
	((this_request)->post_context)

/**
 * scic_sds_request_get_task_context() -
 *
 * This is a helper macro to return the os handle for this request object.
 */
#define scic_sds_request_get_task_context(request) \
	((request)->task_context_buffer)

/**
 * scic_sds_request_set_status() -
 *
 * This macro will set the scu hardware status and sci request completion
 * status for an io request.
 */
#define scic_sds_request_set_status(request, scu_status_code, sci_status_code) \
	{ \
		(request)->scu_status = (scu_status_code); \
		(request)->sci_status = (sci_status_code); \
	}

#define scic_sds_request_complete(a_request) \
	((a_request)->state_handlers->parent.complete_handler(&(a_request)->parent))




/**
 * scic_sds_io_request_tc_completion() -
 *
 * This macro invokes the core state task completion handler for the
 * struct scic_sds_io_request object.
 */
#define scic_sds_io_request_tc_completion(this_request, completion_code) \
	{ \
		if (this_request->parent.state_machine.current_state_id	 \
		    == SCI_BASE_REQUEST_STATE_STARTED \
		    && this_request->has_started_substate_machine \
		    == false) \
			scic_sds_request_started_state_tc_completion_handler(this_request, completion_code); \
		else \
			this_request->state_handlers->tc_completion_handler(this_request, completion_code); \
	}

/**
 * SCU_SGL_ZERO() -
 *
 * This macro zeros the hardware SGL element data
 */
#define SCU_SGL_ZERO(scu_sge) \
	{ \
		(scu_sge).length = 0; \
		(scu_sge).address_lower = 0; \
		(scu_sge).address_upper = 0; \
		(scu_sge).address_modifier = 0;	\
	}

/**
 * SCU_SGL_COPY() -
 *
 * This macro copys the SGL Element data from the host os to the hardware SGL
 * elment data
 */
#define SCU_SGL_COPY(scu_sge, os_sge) \
	{ \
		(scu_sge).length = sg_dma_len(sg); \
		(scu_sge).address_upper = \
			upper_32_bits(sg_dma_address(sg)); \
		(scu_sge).address_lower = \
			lower_32_bits(sg_dma_address(sg)); \
		(scu_sge).address_modifier = 0;	\
	}

/**
 * scic_sds_request_get_user_request() -
 *
 * This is a helper macro to return the os handle for this request object.
 */
#define scic_sds_request_get_user_request(request) \
	((request)->user_request)

/*
 * *****************************************************************************
 * * CORE REQUEST PROTOTYPES
 * ***************************************************************************** */

void scic_sds_request_build_sgl(
	struct scic_sds_request *this_request);



void scic_sds_stp_request_assign_buffers(
	struct scic_sds_request *this_request);

void scic_sds_smp_request_assign_buffers(
	struct scic_sds_request *this_request);

/* --------------------------------------------------------------------------- */

enum sci_status scic_sds_request_start(
	struct scic_sds_request *this_request);

enum sci_status scic_sds_io_request_terminate(
	struct scic_sds_request *this_request);

enum sci_status scic_sds_io_request_complete(
	struct scic_sds_request *this_request);

void scic_sds_io_request_copy_response(
	struct scic_sds_request *this_request);

enum sci_status scic_sds_io_request_event_handler(
	struct scic_sds_request *this_request,
	u32 event_code);

enum sci_status scic_sds_io_request_frame_handler(
	struct scic_sds_request *this_request,
	u32 frame_index);


enum sci_status scic_sds_task_request_terminate(
	struct scic_sds_request *this_request);

/*
 * *****************************************************************************
 * * DEFAULT STATE HANDLERS
 * ***************************************************************************** */

enum sci_status scic_sds_request_default_start_handler(
	struct sci_base_request *this_request);


enum sci_status scic_sds_request_default_complete_handler(
	struct sci_base_request *this_request);

enum sci_status scic_sds_request_default_destruct_handler(
	struct sci_base_request *this_request);

enum sci_status scic_sds_request_default_tc_completion_handler(
	struct scic_sds_request *this_request,
	u32 completion_code);

enum sci_status scic_sds_request_default_event_handler(
	struct scic_sds_request *this_request,
	u32 event_code);

enum sci_status scic_sds_request_default_frame_handler(
	struct scic_sds_request *this_request,
	u32 frame_index);

/*
 * *****************************************************************************
 * * STARTED STATE HANDLERS
 * ***************************************************************************** */

enum sci_status scic_sds_request_started_state_abort_handler(
	struct sci_base_request *this_request);

enum sci_status scic_sds_request_started_state_tc_completion_handler(
	struct scic_sds_request *this_request,
	u32 completion_code);

#endif /* _SCIC_SDS_IO_REQUEST_H_ */
