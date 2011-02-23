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

#ifndef _SCI_BASE_PHY_H_
#define _SCI_BASE_PHY_H_

/**
 * This file contains all of the structures, constants, and methods common to
 *    all phy object definitions.
 *
 *
 */

#include "sci_base_state_machine.h"

/**
 * enum sci_base_phy_states - This enumeration depicts the standard states
 *    common to all phy state machine implementations.
 *
 *
 */
enum sci_base_phy_states {
	/**
	 * Simply the initial state for the base domain state machine.
	 */
	SCI_BASE_PHY_STATE_INITIAL,

	/**
	 * This state indicates that the phy has successfully been stopped.
	 * In this state no new IO operations are permitted on this phy.
	 * This state is entered from the INITIAL state.
	 * This state is entered from the STARTING state.
	 * This state is entered from the READY state.
	 * This state is entered from the RESETTING state.
	 */
	SCI_BASE_PHY_STATE_STOPPED,

	/**
	 * This state indicates that the phy is in the process of becomming
	 * ready.  In this state no new IO operations are permitted on this phy.
	 * This state is entered from the STOPPED state.
	 * This state is entered from the READY state.
	 * This state is entered from the RESETTING state.
	 */
	SCI_BASE_PHY_STATE_STARTING,

	/**
	 * This state indicates the the phy is now ready.  Thus, the user
	 * is able to perform IO operations utilizing this phy as long as it
	 * is currently part of a valid port.
	 * This state is entered from the STARTING state.
	 */
	SCI_BASE_PHY_STATE_READY,

	/**
	 * This state indicates that the phy is in the process of being reset.
	 * In this state no new IO operations are permitted on this phy.
	 * This state is entered from the READY state.
	 */
	SCI_BASE_PHY_STATE_RESETTING,

	/**
	 * Simply the final state for the base phy state machine.
	 */
	SCI_BASE_PHY_STATE_FINAL,
};

/**
 * struct sci_base_phy - This structure defines all of the fields common to PHY
 *    objects.
 *
 *
 */
struct sci_base_phy {
	/**
	 * This field depicts the parent object (struct sci_base_object) for the phy.
	 */
	struct sci_base_object parent;

	/**
	 * This field contains the information for the base phy state machine.
	 */
	struct sci_base_state_machine state_machine;
};

typedef enum sci_status (*sci_base_phy_handler_t)(struct sci_base_phy *);

/**
 * struct sci_base_phy_state_handler - This structure contains all of the state
 *    handler methods common to base phy state machines.  Handler methods
 *    provide the ability to change the behavior for user requests or
 *    transitions depending on the state the machine is in.
 *
 *
 */
struct sci_base_phy_state_handler {
	/**
	 * The start_handler specifies the method invoked when there is an
	 * attempt to start a phy.
	 */
	sci_base_phy_handler_t start_handler;

	/**
	 * The stop_handler specifies the method invoked when there is an
	 * attempt to stop a phy.
	 */
	sci_base_phy_handler_t stop_handler;

	/**
	 * The reset_handler specifies the method invoked when there is an
	 * attempt to reset a phy.
	 */
	sci_base_phy_handler_t reset_handler;

	/**
	 * The destruct_handler specifies the method invoked when attempting to
	 * destruct a phy.
	 */
	sci_base_phy_handler_t destruct_handler;

};

/**
 * sci_base_phy_construct() - Construct the base phy
 * @this_phy: This parameter specifies the base phy to be constructed.
 * @state_table: This parameter specifies the table of state definitions to be
 *    utilized for the phy state machine.
 *
 */
static inline void sci_base_phy_construct(
	struct sci_base_phy *base_phy,
	const struct sci_base_state *state_table)
{
	base_phy->parent.private = NULL;
	sci_base_state_machine_construct(
		&base_phy->state_machine,
		&base_phy->parent,
		state_table,
		SCI_BASE_PHY_STATE_INITIAL
		);

	sci_base_state_machine_start(
		&base_phy->state_machine
		);
}


#endif /* _SCI_BASE_PHY_H_ */
