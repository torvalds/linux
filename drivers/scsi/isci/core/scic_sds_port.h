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

#ifndef _SCIC_SDS_PORT_H_
#define _SCIC_SDS_PORT_H_

#include <linux/kernel.h>
#include "sci_controller_constants.h"
#include "intel_sas.h"
#include "scu_registers.h"

#define SCIC_SDS_DUMMY_PORT   0xFF

struct scic_sds_controller;
struct scic_sds_phy;
struct scic_sds_remote_device;
struct scic_sds_request;

/**
 * This constant defines the value utilized by SCI Components to indicate
 * an invalid handle.
 */
#define SCI_INVALID_HANDLE 0x0

/**
 * enum SCIC_SDS_PORT_READY_SUBSTATES -
 *
 * This enumeration depicts all of the states for the core port ready substate
 * machine.
 */
enum scic_sds_port_ready_substates {
	/**
	 * The substate where the port is started and ready but has no
	 * active phys.
	 */
	SCIC_SDS_PORT_READY_SUBSTATE_WAITING,

	/**
	 * The substate where the port is started and ready and there is
	 * at least one phy operational.
	 */
	SCIC_SDS_PORT_READY_SUBSTATE_OPERATIONAL,

	/**
	 * The substate where the port is started and there was an
	 * add/remove phy event.  This state is only used in Automatic
	 * Port Configuration Mode (APC)
	 */
	SCIC_SDS_PORT_READY_SUBSTATE_CONFIGURING,

	SCIC_SDS_PORT_READY_MAX_SUBSTATES
};

/**
 * enum scic_sds_port_states - This enumeration depicts all the states for the
 *    common port state machine.
 *
 *
 */
enum scic_sds_port_states {
	/**
	 * This state indicates that the port has successfully been stopped.
	 * In this state no new IO operations are permitted.
	 * This state is entered from the STOPPING state.
	 */
	SCI_BASE_PORT_STATE_STOPPED,

	/**
	 * This state indicates that the port is in the process of stopping.
	 * In this state no new IO operations are permitted, but existing IO
	 * operations are allowed to complete.
	 * This state is entered from the READY state.
	 */
	SCI_BASE_PORT_STATE_STOPPING,

	/**
	 * This state indicates the port is now ready.  Thus, the user is
	 * able to perform IO operations on this port.
	 * This state is entered from the STARTING state.
	 */
	SCI_BASE_PORT_STATE_READY,

	/**
	 * This state indicates the port is in the process of performing a hard
	 * reset.  Thus, the user is unable to perform IO operations on this
	 * port.
	 * This state is entered from the READY state.
	 */
	SCI_BASE_PORT_STATE_RESETTING,

	/**
	 * This state indicates the port has failed a reset request.  This state
	 * is entered when a port reset request times out.
	 * This state is entered from the RESETTING state.
	 */
	SCI_BASE_PORT_STATE_FAILED,

	SCI_BASE_PORT_MAX_STATES

};

/**
 * struct scic_sds_port -
 *
 * The core port object provides the the abstraction for an SCU port.
 */
struct scic_sds_port {
	/**
	 * The field specifies that the parent object for the base controller
	 * is the base object itself.
	 */
	struct sci_base_object parent;

	/**
	 * This field contains the information for the base port state machine.
	 */
	struct sci_base_state_machine state_machine;

	/**
	 * This field is the port index that is reported to the SCI USER.
	 * This allows the actual hardware physical port to change without
	 * the SCI USER getting a different answer for the get port index.
	 */
	u8 logical_port_index;

	/**
	 * This field is the port index used to program the SCU hardware.
	 */
	u8 physical_port_index;

	/**
	 * This field contains the active phy mask for the port.
	 * This mask is used in conjunction with the phy state to determine
	 * which phy to select for some port operations.
	 */
	u8 active_phy_mask;

	u16 reserved_rni;
	u16 reserved_tci;

	/**
	 * This field contains the count of the io requests started on this port
	 * object.  It is used to control controller shutdown.
	 */
	u32 started_request_count;

	/**
	 * This field contains the number of devices assigned to this port.
	 * It is used to control port start requests.
	 */
	u32 assigned_device_count;

	/**
	 * This field contains the reason for the port not going ready.  It is
	 * assigned in the state handlers and used in the state transition.
	 */
	u32 not_ready_reason;

	/**
	 * This field is the table of phys assigned to the port.
	 */
	struct scic_sds_phy *phy_table[SCI_MAX_PHYS];

	/**
	 * This field is a pointer back to the controller that owns this
	 * port object.
	 */
	struct scic_sds_controller *owning_controller;

	/**
	 * This field contains the port start/stop timer handle.
	 */
	void *timer_handle;

	/**
	 * This field points to the current set of state handlers for this port
	 * object.  These state handlers are assigned at each enter state of
	 * the state machine.
	 */
	struct scic_sds_port_state_handler *state_handlers;

	/**
	 * This field is the ready substate machine for the port.
	 */
	struct sci_base_state_machine ready_substate_machine;

	/* / Memory mapped hardware register space */

	/**
	 * This field is the pointer to the port task scheduler registers
	 * for the SCU hardware.
	 */
	struct scu_port_task_scheduler_registers __iomem
		*port_task_scheduler_registers;

	/**
	 * This field is identical for all port objects and points to the port
	 * task scheduler group PE configuration registers.
	 * It is used to assign PEs to a port.
	 */
	u32 __iomem *port_pe_configuration_register;

	/**
	 * This field is the VIIT register space for ths port object.
	 */
	struct scu_viit_entry __iomem *viit_registers;

};

typedef enum sci_status (*scic_sds_port_handler_t)(struct scic_sds_port *);

typedef enum sci_status (*scic_sds_port_phy_handler_t)(struct scic_sds_port *,
						       struct scic_sds_phy *);

typedef enum sci_status (*scic_sds_port_reset_handler_t)(struct scic_sds_port *,
							 u32 timeout);

typedef enum sci_status (*scic_sds_port_event_handler_t)(struct scic_sds_port *, u32);

typedef enum sci_status (*scic_sds_port_frame_handler_t)(struct scic_sds_port *, u32);

typedef void (*scic_sds_port_link_handler_t)(struct scic_sds_port *, struct scic_sds_phy *);

typedef enum sci_status (*scic_sds_port_io_request_handler_t)(struct scic_sds_port *,
							      struct scic_sds_remote_device *,
							      struct scic_sds_request *);

struct scic_sds_port_state_handler {
	/**
	 * The start_handler specifies the method invoked when a user
	 * attempts to start a port.
	 */
	scic_sds_port_handler_t start_handler;

	/**
	 * The stop_handler specifies the method invoked when a user
	 * attempts to stop a port.
	 */
	scic_sds_port_handler_t stop_handler;

	/**
	 * The destruct_handler specifies the method invoked when attempting to
	 * destruct a port.
	 */
	scic_sds_port_handler_t destruct_handler;

	/**
	 * The reset_handler specifies the method invoked when a user
	 * attempts to hard reset a port.
	 */
	scic_sds_port_reset_handler_t reset_handler;

	/**
	 * The add_phy_handler specifies the method invoked when a user
	 * attempts to add another phy into the port.
	 */
	scic_sds_port_phy_handler_t add_phy_handler;

	/**
	 * The remove_phy_handler specifies the method invoked when a user
	 * attempts to remove a phy from the port.
	 */
	scic_sds_port_phy_handler_t remove_phy_handler;

	scic_sds_port_frame_handler_t frame_handler;
	scic_sds_port_event_handler_t event_handler;

	scic_sds_port_link_handler_t link_up_handler;
	scic_sds_port_link_handler_t link_down_handler;

	scic_sds_port_io_request_handler_t start_io_handler;
	scic_sds_port_io_request_handler_t complete_io_handler;

};

/**
 * scic_sds_port_get_controller() -
 *
 * Helper macro to get the owning controller of this port
 */
#define scic_sds_port_get_controller(this_port)	\
	((this_port)->owning_controller)

/**
 * scic_sds_port_set_base_state_handlers() -
 *
 * This macro will change the state handlers to those of the specified state id
 */
#define scic_sds_port_set_base_state_handlers(this_port, state_id) \
	scic_sds_port_set_state_handlers(\
		(this_port), &scic_sds_port_state_handler_table[(state_id)])

/**
 * scic_sds_port_set_state_handlers() -
 *
 * Helper macro to set the port object state handlers
 */
#define scic_sds_port_set_state_handlers(this_port, handlers) \
	((this_port)->state_handlers = (handlers))

/**
 * scic_sds_port_get_index() -
 *
 * This macro returns the physical port index for this port object
 */
#define scic_sds_port_get_index(this_port) \
	((this_port)->physical_port_index)


static inline void scic_sds_port_increment_request_count(struct scic_sds_port *sci_port)
{
	sci_port->started_request_count++;
}

static inline void scic_sds_port_decrement_request_count(struct scic_sds_port *sci_port)
{
	if (WARN_ONCE(sci_port->started_request_count == 0,
		       "%s: tried to decrement started_request_count past 0!?",
			__func__))
		/* pass */;
	else
		sci_port->started_request_count--;
}

#define scic_sds_port_active_phy(port, phy) \
	(((port)->active_phy_mask & (1 << (phy)->phy_index)) != 0)

void scic_sds_port_construct(
	struct scic_sds_port *sci_port,
	u8 port_index,
	struct scic_sds_controller *scic);

enum sci_status scic_sds_port_initialize(
	struct scic_sds_port *sci_port,
	void __iomem *port_task_scheduler_registers,
	void __iomem *port_configuration_regsiter,
	void __iomem *viit_registers);

enum sci_status scic_sds_port_add_phy(
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy);

enum sci_status scic_sds_port_remove_phy(
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy);

void scic_sds_port_setup_transports(
	struct scic_sds_port *sci_port,
	u32 device_id);


void scic_sds_port_deactivate_phy(
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy,
	bool do_notify_user);

bool scic_sds_port_link_detected(
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy);

void scic_sds_port_link_up(
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy);

void scic_sds_port_link_down(
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy);

enum sci_status scic_sds_port_start_io(
	struct scic_sds_port *sci_port,
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *sci_req);

enum sci_status scic_sds_port_complete_io(
	struct scic_sds_port *sci_port,
	struct scic_sds_remote_device *sci_dev,
	struct scic_sds_request *sci_req);

enum sas_linkrate scic_sds_port_get_max_allowed_speed(
	struct scic_sds_port *sci_port);

void scic_sds_port_broadcast_change_received(
	struct scic_sds_port *sci_port,
	struct scic_sds_phy *sci_phy);

bool scic_sds_port_is_valid_phy_assignment(
	struct scic_sds_port *sci_port,
	u32 phy_index);

void scic_sds_port_get_sas_address(
	struct scic_sds_port *sci_port,
	struct sci_sas_address *sas_address);

void scic_sds_port_get_attached_sas_address(
	struct scic_sds_port *sci_port,
	struct sci_sas_address *sas_address);

void scic_sds_port_get_attached_protocols(
	struct scic_sds_port *sci_port,
	struct sci_sas_identify_address_frame_protocols *protocols);

#endif /* _SCIC_SDS_PORT_H_ */
