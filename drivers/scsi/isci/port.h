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

#ifndef _ISCI_PORT_H_
#define _ISCI_PORT_H_

#include <scsi/libsas.h>
#include "isci.h"
#include "sas.h"
#include "phy.h"

#define SCIC_SDS_DUMMY_PORT   0xFF

#define PF_NOTIFY (1 << 0)
#define PF_RESUME (1 << 1)

struct isci_phy;
struct isci_host;

enum isci_status {
	isci_freed        = 0x00,
	isci_starting     = 0x01,
	isci_ready        = 0x02,
	isci_ready_for_io = 0x03,
	isci_stopping     = 0x04,
	isci_stopped      = 0x05,
};

/**
 * struct isci_port - isci direct attached sas port object
 * @ready_exit: several states constitute 'ready'. When exiting ready we
 *              need to take extra port-teardown actions that are
 *              skipped when exiting to another 'ready' state.
 * @logical_port_index: software port index
 * @physical_port_index: hardware port index
 * @active_phy_mask: identifies phy members
 * @enabled_phy_mask: phy mask for the port
 *                    that are already part of the port
 * @reserved_tag:
 * @reserved_rni: reserver for port task scheduler workaround
 * @started_request_count: reference count for outstanding commands
 * @not_ready_reason: set during state transitions and notified
 * @timer: timeout start/stop operations
 */
struct isci_port {
	struct isci_host *isci_host;
	struct list_head remote_dev_list;
	#define IPORT_RESET_PENDING 0
	unsigned long state;
	enum sci_status hard_reset_status;
	struct sci_base_state_machine sm;
	bool ready_exit;
	u8 logical_port_index;
	u8 physical_port_index;
	u8 active_phy_mask;
	u8 enabled_phy_mask;
	u8 last_active_phy;
	u16 reserved_rni;
	u16 reserved_tag;
	u32 started_request_count;
	u32 assigned_device_count;
	u32 not_ready_reason;
	struct isci_phy *phy_table[SCI_MAX_PHYS];
	struct isci_host *owning_controller;
	struct sci_timer timer;
	struct scu_port_task_scheduler_registers __iomem *port_task_scheduler_registers;
	/* XXX rework: only one register, no need to replicate per-port */
	u32 __iomem *port_pe_configuration_register;
	struct scu_viit_entry __iomem *viit_registers;
};

enum sci_port_not_ready_reason_code {
	SCIC_PORT_NOT_READY_NO_ACTIVE_PHYS,
	SCIC_PORT_NOT_READY_HARD_RESET_REQUESTED,
	SCIC_PORT_NOT_READY_INVALID_PORT_CONFIGURATION,
	SCIC_PORT_NOT_READY_RECONFIGURING,

	SCIC_PORT_NOT_READY_REASON_CODE_MAX
};

struct sci_port_end_point_properties {
	struct sci_sas_address sas_address;
	struct sci_phy_proto protocols;
};

struct sci_port_properties {
	u32 index;
	struct sci_port_end_point_properties local;
	struct sci_port_end_point_properties remote;
	u32 phy_mask;
};

/**
 * enum sci_port_states - port state machine states
 * @SCI_PORT_STOPPED: port has successfully been stopped.  In this state
 *		      no new IO operations are permitted.  This state is
 *		      entered from the STOPPING state.
 * @SCI_PORT_STOPPING: port is in the process of stopping.  In this
 *		       state no new IO operations are permitted, but
 *		       existing IO operations are allowed to complete.
 *		       This state is entered from the READY state.
 * @SCI_PORT_READY: port is now ready.  Thus, the user is able to
 *		    perform IO operations on this port. This state is
 *		    entered from the STARTING state.
 * @SCI_PORT_SUB_WAITING: port is started and ready but has no active
 *			  phys.
 * @SCI_PORT_SUB_OPERATIONAL: port is started and ready and there is at
 *			      least one phy operational.
 * @SCI_PORT_SUB_CONFIGURING: port is started and there was an
 *			      add/remove phy event.  This state is only
 *			      used in Automatic Port Configuration Mode
 *			      (APC)
 * @SCI_PORT_RESETTING: port is in the process of performing a hard
 *			reset.  Thus, the user is unable to perform IO
 *			operations on this port.  This state is entered
 *			from the READY state.
 * @SCI_PORT_FAILED: port has failed a reset request.  This state is
 *		     entered when a port reset request times out. This
 *		     state is entered from the RESETTING state.
 */
#define PORT_STATES {\
	C(PORT_STOPPED),\
	C(PORT_STOPPING),\
	C(PORT_READY),\
	C(PORT_SUB_WAITING),\
	C(PORT_SUB_OPERATIONAL),\
	C(PORT_SUB_CONFIGURING),\
	C(PORT_RESETTING),\
	C(PORT_FAILED),\
	}
#undef C
#define C(a) SCI_##a
enum sci_port_states PORT_STATES;
#undef C

static inline void sci_port_decrement_request_count(struct isci_port *iport)
{
	if (WARN_ONCE(iport->started_request_count == 0,
		       "%s: tried to decrement started_request_count past 0!?",
			__func__))
		/* pass */;
	else
		iport->started_request_count--;
}

#define sci_port_active_phy(port, phy) \
	(((port)->active_phy_mask & (1 << (phy)->phy_index)) != 0)

void sci_port_construct(
	struct isci_port *iport,
	u8 port_index,
	struct isci_host *ihost);

enum sci_status sci_port_start(struct isci_port *iport);
enum sci_status sci_port_stop(struct isci_port *iport);

enum sci_status sci_port_add_phy(
	struct isci_port *iport,
	struct isci_phy *iphy);

enum sci_status sci_port_remove_phy(
	struct isci_port *iport,
	struct isci_phy *iphy);

void sci_port_setup_transports(
	struct isci_port *iport,
	u32 device_id);

void isci_port_bcn_enable(struct isci_host *, struct isci_port *);

void sci_port_deactivate_phy(
	struct isci_port *iport,
	struct isci_phy *iphy,
	bool do_notify_user);

bool sci_port_link_detected(
	struct isci_port *iport,
	struct isci_phy *iphy);

enum sci_status sci_port_get_properties(
	struct isci_port *iport,
	struct sci_port_properties *prop);

enum sci_status sci_port_link_up(struct isci_port *iport,
				      struct isci_phy *iphy);
enum sci_status sci_port_link_down(struct isci_port *iport,
					struct isci_phy *iphy);

struct isci_request;
struct isci_remote_device;
enum sci_status sci_port_start_io(
	struct isci_port *iport,
	struct isci_remote_device *idev,
	struct isci_request *ireq);

enum sci_status sci_port_complete_io(
	struct isci_port *iport,
	struct isci_remote_device *idev,
	struct isci_request *ireq);

enum sas_linkrate sci_port_get_max_allowed_speed(
	struct isci_port *iport);

void sci_port_broadcast_change_received(
	struct isci_port *iport,
	struct isci_phy *iphy);

bool sci_port_is_valid_phy_assignment(
	struct isci_port *iport,
	u32 phy_index);

void sci_port_get_sas_address(
	struct isci_port *iport,
	struct sci_sas_address *sas_address);

void sci_port_get_attached_sas_address(
	struct isci_port *iport,
	struct sci_sas_address *sas_address);

void isci_port_formed(struct asd_sas_phy *);
void isci_port_deformed(struct asd_sas_phy *);

int isci_port_perform_hard_reset(struct isci_host *ihost, struct isci_port *iport,
				 struct isci_phy *iphy);
int isci_ata_check_ready(struct domain_device *dev);
#endif /* !defined(_ISCI_PORT_H_) */
