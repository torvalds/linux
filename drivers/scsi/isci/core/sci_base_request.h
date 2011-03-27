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

#ifndef _SCI_BASE_REQUST_H_
#define _SCI_BASE_REQUST_H_

/**
 * This file contains all of the constants, types, and method declarations for
 *    the SCI base IO and task request objects.
 *
 *
 */

#include "sci_base_state_machine.h"

/**
 * enum sci_base_request_states - This enumeration depicts all the states for
 *    the common request state machine.
 *
 *
 */
enum sci_base_request_states {
	/**
	 * Simply the initial state for the base request state machine.
	 */
	SCI_BASE_REQUEST_STATE_INITIAL,

	/**
	 * This state indicates that the request has been constructed. This state
	 * is entered from the INITIAL state.
	 */
	SCI_BASE_REQUEST_STATE_CONSTRUCTED,

	/**
	 * This state indicates that the request has been started. This state is
	 * entered from the CONSTRUCTED state.
	 */
	SCI_BASE_REQUEST_STATE_STARTED,

	/**
	 * This state indicates that the request has completed.
	 * This state is entered from the STARTED state. This state is entered from
	 * the ABORTING state.
	 */
	SCI_BASE_REQUEST_STATE_COMPLETED,

	/**
	 * This state indicates that the request is in the process of being
	 * terminated/aborted.
	 * This state is entered from the CONSTRUCTED state.
	 * This state is entered from the STARTED state.
	 */
	SCI_BASE_REQUEST_STATE_ABORTING,

	/**
	 * Simply the final state for the base request state machine.
	 */
	SCI_BASE_REQUEST_STATE_FINAL,
};

/**
 * struct sci_base_request - The base request object abstracts the fields
 *    common to all SCI IO and task request objects.
 *
 *
 */
struct sci_base_request {
	/**
	 * The field specifies that the parent object for the base request is the
	 * base object itself.
	 */
	struct sci_base_object parent;

	/**
	 * This field contains the information for the base request state machine.
	 */
	struct sci_base_state_machine state_machine;
};

typedef enum sci_status (*sci_base_request_handler_t)(
	struct sci_base_request *this_request
	);

/**
 * struct sci_base_request_state_handler - This structure contains all of the
 *    state handler methods common to base IO and task request state machines.
 *    Handler methods provide the ability to change the behavior for user
 *    requests or transitions depending on the state the machine is in.
 *
 *
 */
struct sci_base_request_state_handler {
	/**
	 * The start_handler specifies the method invoked when a user attempts to
	 * start a request.
	 */
	sci_base_request_handler_t start_handler;

	/**
	 * The abort_handler specifies the method invoked when a user attempts to
	 * abort a request.
	 */
	sci_base_request_handler_t abort_handler;

	/**
	 * The complete_handler specifies the method invoked when a user attempts to
	 * complete a request.
	 */
	sci_base_request_handler_t complete_handler;

	/**
	 * The destruct_handler specifies the method invoked when a user attempts to
	 * destruct a request.
	 */
	sci_base_request_handler_t destruct_handler;

};

/**
 * sci_base_request_construct() - Construct the base request.
 * @this_request: This parameter specifies the base request to be constructed.
 * @state_table: This parameter specifies the table of state definitions to be
 *    utilized for the request state machine.
 *
 */
static inline void sci_base_request_construct(
	struct sci_base_request *base_req,
	const struct sci_base_state *my_state_table)
{
	base_req->parent.private = NULL;
	sci_base_state_machine_construct(
		&base_req->state_machine,
		&base_req->parent,
		my_state_table,
		SCI_BASE_REQUEST_STATE_INITIAL
		);

	sci_base_state_machine_start(
		&base_req->state_machine
		);
}

#endif /* _SCI_BASE_REQUST_H_ */
