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

#ifndef _SCI_BASE_STATE_MACHINE_H_
#define _SCI_BASE_STATE_MACHINE_H_

#include <linux/string.h>

/**
 * This file contains all structures, constants, or method declarations common
 *    to all state machines defined in SCI.
 *
 *
 */


#include "sci_base_state.h"


/**
 * SET_STATE_HANDLER() -
 *
 * This macro simply provides simplified retrieval of an objects state handler.
 */
#define SET_STATE_HANDLER(object, table, state)	\
	(object)->state_handlers = &(table)[(state)]

/**
 * struct sci_base_state_machine - This structure defines the fields common to
 *    all state machines.
 *
 *
 */
struct sci_base_state_machine {
	/**
	 * This field points to the start of the state machine's state table.
	 */
	const struct sci_base_state *state_table;

	/**
	 * This field points to the object to which this state machine is
	 * associated.  It serves as a cookie to be provided to the state
	 * enter/exit methods.
	 */
	struct sci_base_object *state_machine_owner;

	/**
	 * This field simply indicates the state value for the state machine's
	 * initial state.
	 */
	u32 initial_state_id;

	/**
	 * This field indicates the current state of the state machine.
	 */
	u32 current_state_id;

	/**
	 * This field indicates the previous state of the state machine.
	 */
	u32 previous_state_id;

};

/*
 * ******************************************************************************
 * * P R O T E C T E D    M E T H O D S
 * ****************************************************************************** */

void sci_base_state_machine_construct(
	struct sci_base_state_machine *this_state_machine,
	struct sci_base_object *state_machine_owner,
	const struct sci_base_state *state_table,
	u32 initial_state);

void sci_base_state_machine_start(
	struct sci_base_state_machine *this_state_machine);

void sci_base_state_machine_stop(
	struct sci_base_state_machine *this_state_machine);

void sci_base_state_machine_change_state(
	struct sci_base_state_machine *this_state_machine,
	u32 next_state);

u32 sci_base_state_machine_get_state(
	struct sci_base_state_machine *this_state_machine);

#endif /* _SCI_BASE_STATE_MACHINE_H_ */
