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

#ifndef _SCI_BASE_CONTROLLER_H_
#define _SCI_BASE_CONTROLLER_H_

#include "intel_sas.h"
#include "sci_controller_constants.h"
#include "sci_base_state.h"
#include "sci_base_memory_descriptor_list.h"
#include "sci_base_state_machine.h"
#include "sci_object.h"

struct sci_base_memory_descriptor_list;

/**
 * enum sci_base_controller_states - This enumeration depicts all the states
 *    for the common controller state machine.
 *
 *
 */
enum sci_base_controller_states {
	/**
	 * Simply the initial state for the base controller state machine.
	 */
	SCI_BASE_CONTROLLER_STATE_INITIAL = 0,

	/**
	 * This state indicates that the controller is reset.  The memory for
	 * the controller is in it's initial state, but the controller requires
	 * initialization.
	 * This state is entered from the INITIAL state.
	 * This state is entered from the RESETTING state.
	 */
	SCI_BASE_CONTROLLER_STATE_RESET,

	/**
	 * This state is typically an action state that indicates the controller
	 * is in the process of initialization.  In this state no new IO operations
	 * are permitted.
	 * This state is entered from the RESET state.
	 */
	SCI_BASE_CONTROLLER_STATE_INITIALIZING,

	/**
	 * This state indicates that the controller has been successfully
	 * initialized.  In this state no new IO operations are permitted.
	 * This state is entered from the INITIALIZING state.
	 */
	SCI_BASE_CONTROLLER_STATE_INITIALIZED,

	/**
	 * This state indicates the the controller is in the process of becoming
	 * ready (i.e. starting).  In this state no new IO operations are permitted.
	 * This state is entered from the INITIALIZED state.
	 */
	SCI_BASE_CONTROLLER_STATE_STARTING,

	/**
	 * This state indicates the controller is now ready.  Thus, the user
	 * is able to perform IO operations on the controller.
	 * This state is entered from the STARTING state.
	 */
	SCI_BASE_CONTROLLER_STATE_READY,

	/**
	 * This state is typically an action state that indicates the controller
	 * is in the process of resetting.  Thus, the user is unable to perform
	 * IO operations on the controller.  A reset is considered destructive in
	 * most cases.
	 * This state is entered from the READY state.
	 * This state is entered from the FAILED state.
	 * This state is entered from the STOPPED state.
	 */
	SCI_BASE_CONTROLLER_STATE_RESETTING,

	/**
	 * This state indicates that the controller is in the process of stopping.
	 * In this state no new IO operations are permitted, but existing IO
	 * operations are allowed to complete.
	 * This state is entered from the READY state.
	 */
	SCI_BASE_CONTROLLER_STATE_STOPPING,

	/**
	 * This state indicates that the controller has successfully been stopped.
	 * In this state no new IO operations are permitted.
	 * This state is entered from the STOPPING state.
	 */
	SCI_BASE_CONTROLLER_STATE_STOPPED,

	/**
	 * This state indicates that the controller could not successfully be
	 * initialized.  In this state no new IO operations are permitted.
	 * This state is entered from the INITIALIZING state.
	 * This state is entered from the STARTING state.
	 * This state is entered from the STOPPING state.
	 * This state is entered from the RESETTING state.
	 */
	SCI_BASE_CONTROLLER_STATE_FAILED,

	SCI_BASE_CONTROLLER_MAX_STATES

};

/**
 * struct sci_base_controller - The base controller object abstracts the fields
 *    common to all SCI controller objects.
 *
 *
 */
struct sci_base_controller {
	/**
	 * The field specifies that the parent object for the base controller
	 * is the base object itself.
	 */
	struct sci_base_object parent;

	/**
	 * This field points to the memory descriptor list associated with this
	 * controller.  The MDL indicates the memory requirements necessary for
	 * this controller object.
	 */
	struct sci_base_memory_descriptor_list mdl;

	/**
	 * This field contains the information for the base controller state
	 * machine.
	 */
	struct sci_base_state_machine state_machine;
};

/* Forward declarations */
struct sci_base_remote_device;
struct sci_base_request;

typedef enum sci_status
(*sci_base_controller_handler_t)(struct sci_base_controller *);

typedef enum sci_status
(*sci_base_controller_timed_handler_t)(struct sci_base_controller *, u32);

typedef enum sci_status
(*sci_base_controller_request_handler_t)(struct sci_base_controller *,
					 struct sci_base_remote_device *,
					 struct sci_base_request *);

typedef enum sci_status
(*sci_base_controller_start_request_handler_t)(struct sci_base_controller *,
					       struct sci_base_remote_device *,
					       struct sci_base_request *, u16);

/**
 * struct sci_base_controller_state_handler - This structure contains all of
 *    the state handler methods common to base controller state machines.
 *    Handler methods provide the ability to change the behavior for user
 *    requests or transitions depending on the state the machine is in.
 *
 *
 */
struct sci_base_controller_state_handler {
	/**
	 * The start_handler specifies the method invoked when a user attempts to
	 * start a controller.
	 */
	sci_base_controller_timed_handler_t start;

	/**
	 * The stop_handler specifies the method invoked when a user attempts to
	 * stop a controller.
	 */
	sci_base_controller_timed_handler_t stop;

	/**
	 * The reset_handler specifies the method invoked when a user attempts to
	 * reset a controller.
	 */
	sci_base_controller_handler_t reset;

	/**
	 * The initialize_handler specifies the method invoked when a user
	 * attempts to initialize a controller.
	 */
	sci_base_controller_handler_t initialize;

	/**
	 * The start_io_handler specifies the method invoked when a user
	 * attempts to start an IO request for a controller.
	 */
	sci_base_controller_start_request_handler_t start_io;

	/**
	 * The complete_io_handler specifies the method invoked when a user
	 * attempts to complete an IO request for a controller.
	 */
	sci_base_controller_request_handler_t complete_io;

	/**
	 * The continue_io_handler specifies the method invoked when a user
	 * attempts to continue an IO request for a controller.
	 */
	sci_base_controller_request_handler_t continue_io;

	/**
	 * The start_task_handler specifies the method invoked when a user
	 * attempts to start a task management request for a controller.
	 */
	sci_base_controller_start_request_handler_t start_task;

	/**
	 * The complete_task_handler specifies the method invoked when a user
	 * attempts to complete a task management request for a controller.
	 */
	sci_base_controller_request_handler_t complete_task;

};

/**
 * sci_base_controller_construct() - Construct the base controller
 * @this_controller: This parameter specifies the base controller to be
 *    constructed.
 * @state_table: This parameter specifies the table of state definitions to be
 *    utilized for the controller state machine.
 * @mde_array: This parameter specifies the array of memory descriptor entries
 *    to be managed by this list.
 * @mde_array_length: This parameter specifies the size of the array of entries.
 * @next_mdl: This parameter specifies a subsequent MDL object to be managed by
 *    this MDL object.
 * @oem_parameters: This parameter specifies the original equipment
 *    manufacturer parameters to be utilized by this controller object.
 *
 */
static inline void sci_base_controller_construct(
	struct sci_base_controller *scic_base,
	const struct sci_base_state *state_table,
	struct sci_physical_memory_descriptor *mdes,
	u32 mde_count,
	struct sci_base_memory_descriptor_list *next_mdl)
{
	sci_base_state_machine_construct(
		&scic_base->state_machine,
		&scic_base->parent,
		state_table,
		SCI_BASE_CONTROLLER_STATE_INITIAL
		);

	sci_base_mdl_construct(&scic_base->mdl, mdes, mde_count, next_mdl);

	sci_base_state_machine_start(&scic_base->state_machine);
}

#endif /* _SCI_BASE_CONTROLLER_H_ */
