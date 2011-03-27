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

#ifndef _SCIC_SDS_PHY_H_
#define _SCIC_SDS_PHY_H_

/**
 * This file contains the structures, constants and prototypes for the
 *    struct scic_sds_phy object.
 *
 *
 */

#include "intel_sata.h"
#include "intel_sas.h"
#include "sci_base_phy.h"
#include "scu_registers.h"

struct scic_sds_port;
/**
 *
 *
 * This is the timeout value for the SATA phy to wait for a SIGNATURE FIS
 * before restarting the starting state machine.  Technically, the old parallel
 * ATA specification required up to 30 seconds for a device to issue its
 * signature FIS as a result of a soft reset.  Now we see that devices respond
 * generally within 15 seconds, but we'll use 25 for now.
 */
#define SCIC_SDS_SIGNATURE_FIS_TIMEOUT    25000

/**
 *
 *
 * This is the timeout for the SATA OOB/SN because the hardware does not
 * recognize a hot plug after OOB signal but before the SN signals.  We need to
 * make sure after a hotplug timeout if we have not received the speed event
 * notification from the hardware that we restart the hardware OOB state
 * machine.
 */
#define SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT  250

/**
 * enum scic_sds_phy_starting_substates -
 *
 *
 */
enum scic_sds_phy_starting_substates {
	/**
	 * Initial state
	 */
	SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL,

	/**
	 * Wait state for the hardware OSSP event type notification
	 */
	SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN,

	/**
	 * Wait state for the PHY speed notification
	 */
	SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN,

	/**
	 * Wait state for the IAF Unsolicited frame notification
	 */
	SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF,

	/**
	 * Wait state for the request to consume power
	 */
	SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER,

	/**
	 * Wait state for request to consume power
	 */
	SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER,

	/**
	 * Wait state for the SATA PHY notification
	 */
	SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN,

	/**
	 * Wait for the SATA PHY speed notification
	 */
	SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN,

	/**
	 * Wait state for the SIGNATURE FIS unsolicited frame notification
	 */
	SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF,

	/**
	 * Exit state for this state machine
	 */
	SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL,
};

struct scic_sds_port;
struct scic_sds_controller;

/**
 * This enumeration provides a named phy type for the state machine
 *
 *
 */
enum scic_sds_phy_protocol {
	/**
	 * This is an unknown phy type since there is either nothing on the other
	 * end or we have not detected the phy type as yet.
	 */
	SCIC_SDS_PHY_PROTOCOL_UNKNOWN,

	/**
	 * This is a SAS PHY
	 */
	SCIC_SDS_PHY_PROTOCOL_SAS,

	/**
	 * This is a SATA PHY
	 */
	SCIC_SDS_PHY_PROTOCOL_SATA,

	SCIC_SDS_MAX_PHY_PROTOCOLS
};

/**
 * struct scic_sds_phy - This structure  contains or references all of the data
 *    necessary to represent the core phy object and SCU harware protocol
 *    engine.
 *
 *
 */
struct scic_sds_phy {
	struct sci_base_phy parent;

	/**
	 * This field specifies the port object that owns/contains this phy.
	 */
	struct scic_sds_port *owning_port;

	/**
	 * This field indicates whether the phy supports 1.5 Gb/s, 3.0 Gb/s,
	 * or 6.0 Gb/s operation.
	 */
	enum sci_sas_link_rate max_negotiated_speed;

	/**
	 * This member specifies the protocol being utilized on this phy.  This
	 * field contains a legitamite value once the PHY has link trained with
	 * a remote phy.
	 */
	enum scic_sds_phy_protocol protocol;

	/**
	 * This field specifies the index with which this phy is associated (0-3).
	 */
	u8 phy_index;

	/**
	 * This member indicates if this particular PHY has received a BCN while
	 * it had no port assignement.  This BCN will be reported once the phy is
	 * assigned to a port.
	 */
	bool bcn_received_while_port_unassigned;

	/**
	 * This field indicates if this PHY is currently in the process of
	 * link training (i.e. it has started OOB, but has yet to perform
	 * IAF exchange/Signature FIS reception).
	 */
	bool is_in_link_training;

	union {
		struct {
			struct sci_sas_identify_address_frame identify_address_frame_buffer;

		} sas;

		struct {
			struct sata_fis_reg_d2h signature_fis_buffer;

		} sata;

	} phy_type;

	/**
	 * This field contains a reference to the timer utilized in detecting
	 * when a signature FIS timeout has occurred.  The signature FIS is the
	 * first FIS sent by an attached SATA device after OOB/SN.
	 */
	void *sata_timeout_timer;

	const struct scic_sds_phy_state_handler *state_handlers;

	struct sci_base_state_machine starting_substate_machine;

	/**
	 * This field is the pointer to the transport layer register for the SCU
	 * hardware.
	 */
	struct scu_transport_layer_registers __iomem *transport_layer_registers;

	/**
	 * This field points to the link layer register set within the SCU.
	 */
	struct scu_link_layer_registers __iomem *link_layer_registers;

};


typedef enum sci_status (*scic_sds_phy_event_handler_t)(struct scic_sds_phy *, u32);
typedef enum sci_status (*scic_sds_phy_frame_handler_t)(struct scic_sds_phy *, u32);
typedef enum sci_status (*scic_sds_phy_power_handler_t)(struct scic_sds_phy *);

/**
 * struct scic_sds_phy_state_handler -
 *
 *
 */
struct scic_sds_phy_state_handler {
	/**
	 * This is the struct sci_base_phy object state handlers.
	 */
	struct sci_base_phy_state_handler parent;

	/**
	 * The state handler for unsolicited frames received from the SCU hardware.
	 */
	scic_sds_phy_frame_handler_t frame_handler;

	/**
	 * The state handler for events received from the SCU hardware.
	 */
	scic_sds_phy_event_handler_t event_handler;

	/**
	 * The state handler for staggered spinup.
	 */
	scic_sds_phy_power_handler_t consume_power_handler;

};

/**
 * scic_sds_phy_get_index() -
 *
 * This macro returns the phy index for the specified phy
 */
#define scic_sds_phy_get_index(phy) \
	((phy)->phy_index)

/**
 * scic_sds_phy_get_controller() - This macro returns the controller for this
 *    phy
 *
 *
 */
#define scic_sds_phy_get_controller(phy) \
	(scic_sds_port_get_controller((phy)->owning_port))

/**
 * scic_sds_phy_set_state_handlers() - This macro sets the state handlers for
 *    this phy object
 *
 *
 */
#define scic_sds_phy_set_state_handlers(phy, handlers) \
	((phy)->state_handlers = (handlers))

/**
 * scic_sds_phy_set_base_state_handlers() -
 *
 * This macro set the base state handlers for the phy object.
 */
#define scic_sds_phy_set_base_state_handlers(phy, state_id) \
	scic_sds_phy_set_state_handlers(\
		(phy), \
		&scic_sds_phy_state_handler_table[(state_id)] \
		)

void scic_sds_phy_construct(
	struct scic_sds_phy *this_phy,
	struct scic_sds_port *owning_port,
	u8 phy_index);

struct scic_sds_port *scic_sds_phy_get_port(
	struct scic_sds_phy *this_phy);

void scic_sds_phy_set_port(
	struct scic_sds_phy *this_phy,
	struct scic_sds_port *owning_port);

enum sci_status scic_sds_phy_initialize(
	struct scic_sds_phy *this_phy,
	struct scu_transport_layer_registers __iomem *transport_layer_registers,
	struct scu_link_layer_registers __iomem *link_layer_registers);

enum sci_status scic_sds_phy_start(
	struct scic_sds_phy *this_phy);

enum sci_status scic_sds_phy_stop(
	struct scic_sds_phy *this_phy);

enum sci_status scic_sds_phy_reset(
	struct scic_sds_phy *this_phy);

void scic_sds_phy_resume(
	struct scic_sds_phy *this_phy);

void scic_sds_phy_setup_transport(
	struct scic_sds_phy *this_phy,
	u32 device_id);

enum sci_status scic_sds_phy_event_handler(
	struct scic_sds_phy *this_phy,
	u32 event_code);

enum sci_status scic_sds_phy_frame_handler(
	struct scic_sds_phy *this_phy,
	u32 frame_index);

enum sci_status scic_sds_phy_consume_power_handler(
	struct scic_sds_phy *this_phy);

void scic_sds_phy_get_sas_address(
	struct scic_sds_phy *this_phy,
	struct sci_sas_address *sas_address);

void scic_sds_phy_get_attached_sas_address(
	struct scic_sds_phy *this_phy,
	struct sci_sas_address *sas_address);

void scic_sds_phy_get_protocols(
	struct scic_sds_phy *this_phy,
	struct sci_sas_identify_address_frame_protocols *protocols);

void scic_sds_phy_get_attached_phy_protocols(
	struct scic_sds_phy *this_phy,
	struct sci_sas_identify_address_frame_protocols *protocols);

#endif /* _SCIC_SDS_PHY_H_ */
