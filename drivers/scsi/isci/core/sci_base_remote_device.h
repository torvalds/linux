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

#ifndef _SCI_BASE_REMOTE_DEVICE_H_
#define _SCI_BASE_REMOTE_DEVICE_H_

/**
 * This file contains all of the structures, constants, and methods common to
 *    all remote device object definitions.
 *
 *
 */

#include "sci_base_state_machine.h"

struct sci_base_request;

/**
 * enum sci_base_remote_device_states - This enumeration depicts all the states
 *    for the common remote device state machine.
 *
 *
 */
enum sci_base_remote_device_states {
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
 * struct sci_base_remote_device - The base remote device object abstracts the
 *    fields common to all SCI remote device objects.
 *
 *
 */
struct sci_base_remote_device {
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
};


typedef enum sci_status (*sci_base_remote_device_handler_t)(
	struct sci_base_remote_device *
	);

typedef enum sci_status (*sci_base_remote_device_request_handler_t)(
	struct sci_base_remote_device *,
	struct sci_base_request *
	);

typedef enum sci_status (*sci_base_remote_device_high_priority_request_complete_handler_t)(
	struct sci_base_remote_device *,
	struct sci_base_request *,
	void *,
	enum sci_io_status
	);

/**
 * struct sci_base_remote_device_state_handler - This structure contains all of
 *    the state handler methods common to base remote device state machines.
 *    Handler methods provide the ability to change the behavior for user
 *    requests or transitions depending on the state the machine is in.
 *
 *
 */
struct sci_base_remote_device_state_handler {
	/**
	 * The start_handler specifies the method invoked when a user attempts to
	 * start a remote device.
	 */
	sci_base_remote_device_handler_t start_handler;

	/**
	 * The stop_handler specifies the method invoked when a user attempts to
	 * stop a remote device.
	 */
	sci_base_remote_device_handler_t stop_handler;

	/**
	 * The fail_handler specifies the method invoked when a remote device
	 * failure has occurred.  A failure may be due to an inability to
	 * initialize/configure the device.
	 */
	sci_base_remote_device_handler_t fail_handler;

	/**
	 * The destruct_handler specifies the method invoked when attempting to
	 * destruct a remote device.
	 */
	sci_base_remote_device_handler_t destruct_handler;

	/**
	 * The reset handler specifies the method invloked when requesting to reset a
	 * remote device.
	 */
	sci_base_remote_device_handler_t reset_handler;

	/**
	 * The reset complete handler specifies the method invloked when reporting
	 * that a reset has completed to the remote device.
	 */
	sci_base_remote_device_handler_t reset_complete_handler;

	/**
	 * The start_io_handler specifies the method invoked when a user
	 * attempts to start an IO request for a remote device.
	 */
	sci_base_remote_device_request_handler_t start_io_handler;

	/**
	 * The complete_io_handler specifies the method invoked when a user
	 * attempts to complete an IO request for a remote device.
	 */
	sci_base_remote_device_request_handler_t complete_io_handler;

	/**
	 * The continue_io_handler specifies the method invoked when a user
	 * attempts to continue an IO request for a remote device.
	 */
	sci_base_remote_device_request_handler_t continue_io_handler;

	/**
	 * The start_task_handler specifies the method invoked when a user
	 * attempts to start a task management request for a remote device.
	 */
	sci_base_remote_device_request_handler_t start_task_handler;

	/**
	 * The complete_task_handler specifies the method invoked when a user
	 * attempts to complete a task management request for a remote device.
	 */
	sci_base_remote_device_request_handler_t complete_task_handler;

};

/**
 * sci_base_remote_device_construct() - Construct the base remote device
 * @this_remote_device: This parameter specifies the base remote device to be
 *    constructed.
 * @state_table: This parameter specifies the table of state definitions to be
 *    utilized for the remote device state machine.
 *
 */
static inline void sci_base_remote_device_construct(
	struct sci_base_remote_device *base_dev,
	const struct sci_base_state *state_table)
{
	base_dev->parent.private = NULL;
	sci_base_state_machine_construct(
		&base_dev->state_machine,
		&base_dev->parent,
		state_table,
		SCI_BASE_REMOTE_DEVICE_STATE_INITIAL
		);

	sci_base_state_machine_start(
		&base_dev->state_machine
		);
}
#endif /* _SCI_BASE_REMOTE_DEVICE_H_ */
