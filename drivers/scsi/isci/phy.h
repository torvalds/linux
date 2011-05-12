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
#ifndef _ISCI_PHY_H_
#define _ISCI_PHY_H_

#include <scsi/sas.h>
#include <scsi/libsas.h>
#include "state_machine.h"
#include "sas.h"

/* This is the timeout value for the SATA phy to wait for a SIGNATURE FIS
 * before restarting the starting state machine.  Technically, the old parallel
 * ATA specification required up to 30 seconds for a device to issue its
 * signature FIS as a result of a soft reset.  Now we see that devices respond
 * generally within 15 seconds, but we'll use 25 for now.
 */
#define SCIC_SDS_SIGNATURE_FIS_TIMEOUT    25000

/* This is the timeout for the SATA OOB/SN because the hardware does not
 * recognize a hot plug after OOB signal but before the SN signals.  We need to
 * make sure after a hotplug timeout if we have not received the speed event
 * notification from the hardware that we restart the hardware OOB state
 * machine.
 */
#define SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT  250

enum scic_sds_phy_protocol {
	SCIC_SDS_PHY_PROTOCOL_UNKNOWN,
	SCIC_SDS_PHY_PROTOCOL_SAS,
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
	/**
	 * This field contains the information for the base phy state machine.
	 */
	struct sci_base_state_machine state_machine;

	/**
	 * This field specifies the port object that owns/contains this phy.
	 */
	struct scic_sds_port *owning_port;

	/**
	 * This field indicates whether the phy supports 1.5 Gb/s, 3.0 Gb/s,
	 * or 6.0 Gb/s operation.
	 */
	enum sas_linkrate max_negotiated_speed;

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

	/**
	 * This field contains a reference to the timer utilized in detecting
	 * when a signature FIS timeout has occurred.  The signature FIS is the
	 * first FIS sent by an attached SATA device after OOB/SN.
	 */
	void *sata_timeout_timer;

	const struct scic_sds_phy_state_handler *state_handlers;

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


struct isci_phy {
	struct scic_sds_phy sci;
	struct asd_sas_phy sas_phy;
	struct isci_port *isci_port;
	u8 sas_addr[SAS_ADDR_SIZE];

	union {
		struct sas_identify_frame iaf;
		struct dev_to_host_fis fis;
	} frame_rcvd;
};

static inline struct isci_phy *to_isci_phy(struct asd_sas_phy *sas_phy)
{
	struct isci_phy *iphy = container_of(sas_phy, typeof(*iphy), sas_phy);

	return iphy;
}

static inline struct isci_phy *sci_phy_to_iphy(struct scic_sds_phy *sci_phy)
{
	struct isci_phy *iphy = container_of(sci_phy, typeof(*iphy), sci);

	return iphy;
}

struct scic_phy_cap {
	union {
		struct {
			/*
			 * The SAS specification indicates the start bit shall
			 * always be set to
			 * 1.  This implementation will have the start bit set
			 * to 0 if the PHY CAPABILITIES were either not
			 * received or speed negotiation failed.
			 */
			u8 start:1;
			u8 tx_ssc_type:1;
			u8 res1:2;
			u8 req_logical_linkrate:4;

			u32 gen1_no_ssc:1;
			u32 gen1_ssc:1;
			u32 gen2_no_ssc:1;
			u32 gen2_ssc:1;
			u32 gen3_no_ssc:1;
			u32 gen3_ssc:1;
			u32 res2:17;
			u32 parity:1;
		};
		u32 all;
	};
}  __packed;

/* this data structure reflects the link layer transmit identification reg */
struct scic_phy_proto {
	union {
		struct {
			u16 _r_a:1;
			u16 smp_iport:1;
			u16 stp_iport:1;
			u16 ssp_iport:1;
			u16 _r_b:4;
			u16 _r_c:1;
			u16 smp_tport:1;
			u16 stp_tport:1;
			u16 ssp_tport:1;
			u16 _r_d:4;
		};
		u16 all;
	};
} __packed;


/**
 * struct scic_phy_properties - This structure defines the properties common to
 *    all phys that can be retrieved.
 *
 *
 */
struct scic_phy_properties {
	/**
	 * This field specifies the port that currently contains the
	 * supplied phy.  This field may be set to NULL
	 * if the phy is not currently contained in a port.
	 */
	struct scic_sds_port *owning_port;

	/**
	 * This field specifies the link rate at which the phy is
	 * currently operating.
	 */
	enum sas_linkrate negotiated_link_rate;

	/**
	 * This field specifies the index of the phy in relation to other
	 * phys within the controller.  This index is zero relative.
	 */
	u8 index;
};

/**
 * struct scic_sas_phy_properties - This structure defines the properties,
 *    specific to a SAS phy, that can be retrieved.
 *
 *
 */
struct scic_sas_phy_properties {
	/**
	 * This field delineates the Identify Address Frame received
	 * from the remote end point.
	 */
	struct sas_identify_frame rcvd_iaf;

	/**
	 * This field delineates the Phy capabilities structure received
	 * from the remote end point.
	 */
	struct scic_phy_cap rcvd_cap;

};

/**
 * struct scic_sata_phy_properties - This structure defines the properties,
 *    specific to a SATA phy, that can be retrieved.
 *
 *
 */
struct scic_sata_phy_properties {
	/**
	 * This field delineates the signature FIS received from the
	 * attached target.
	 */
	struct dev_to_host_fis signature_fis;

	/**
	 * This field specifies to the user if a port selector is connected
	 * on the specified phy.
	 */
	bool is_port_selector_present;

};

/**
 * enum scic_phy_counter_id - This enumeration depicts the various pieces of
 *    optional information that can be retrieved for a specific phy.
 *
 *
 */
enum scic_phy_counter_id {
	/**
	 * This PHY information field tracks the number of frames received.
	 */
	SCIC_PHY_COUNTER_RECEIVED_FRAME,

	/**
	 * This PHY information field tracks the number of frames transmitted.
	 */
	SCIC_PHY_COUNTER_TRANSMITTED_FRAME,

	/**
	 * This PHY information field tracks the number of DWORDs received.
	 */
	SCIC_PHY_COUNTER_RECEIVED_FRAME_WORD,

	/**
	 * This PHY information field tracks the number of DWORDs transmitted.
	 */
	SCIC_PHY_COUNTER_TRANSMITTED_FRAME_DWORD,

	/**
	 * This PHY information field tracks the number of times DWORD
	 * synchronization was lost.
	 */
	SCIC_PHY_COUNTER_LOSS_OF_SYNC_ERROR,

	/**
	 * This PHY information field tracks the number of received DWORDs with
	 * running disparity errors.
	 */
	SCIC_PHY_COUNTER_RECEIVED_DISPARITY_ERROR,

	/**
	 * This PHY information field tracks the number of received frames with a
	 * CRC error (not including short or truncated frames).
	 */
	SCIC_PHY_COUNTER_RECEIVED_FRAME_CRC_ERROR,

	/**
	 * This PHY information field tracks the number of DONE (ACK/NAK TIMEOUT)
	 * primitives received.
	 */
	SCIC_PHY_COUNTER_RECEIVED_DONE_ACK_NAK_TIMEOUT,

	/**
	 * This PHY information field tracks the number of DONE (ACK/NAK TIMEOUT)
	 * primitives transmitted.
	 */
	SCIC_PHY_COUNTER_TRANSMITTED_DONE_ACK_NAK_TIMEOUT,

	/**
	 * This PHY information field tracks the number of times the inactivity
	 * timer for connections on the phy has been utilized.
	 */
	SCIC_PHY_COUNTER_INACTIVITY_TIMER_EXPIRED,

	/**
	 * This PHY information field tracks the number of DONE (CREDIT TIMEOUT)
	 * primitives received.
	 */
	SCIC_PHY_COUNTER_RECEIVED_DONE_CREDIT_TIMEOUT,

	/**
	 * This PHY information field tracks the number of DONE (CREDIT TIMEOUT)
	 * primitives transmitted.
	 */
	SCIC_PHY_COUNTER_TRANSMITTED_DONE_CREDIT_TIMEOUT,

	/**
	 * This PHY information field tracks the number of CREDIT BLOCKED
	 * primitives received.
	 * @note Depending on remote device implementation, credit blocks
	 *       may occur regularly.
	 */
	SCIC_PHY_COUNTER_RECEIVED_CREDIT_BLOCKED,

	/**
	 * This PHY information field contains the number of short frames
	 * received.  A short frame is simply a frame smaller then what is
	 * allowed by either the SAS or SATA specification.
	 */
	SCIC_PHY_COUNTER_RECEIVED_SHORT_FRAME,

	/**
	 * This PHY information field contains the number of frames received after
	 * credit has been exhausted.
	 */
	SCIC_PHY_COUNTER_RECEIVED_FRAME_WITHOUT_CREDIT,

	/**
	 * This PHY information field contains the number of frames received after
	 * a DONE has been received.
	 */
	SCIC_PHY_COUNTER_RECEIVED_FRAME_AFTER_DONE,

	/**
	 * This PHY information field contains the number of times the phy
	 * failed to achieve DWORD synchronization during speed negotiation.
	 */
	SCIC_PHY_COUNTER_SN_DWORD_SYNC_ERROR
};

enum scic_sds_phy_states {
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


typedef enum sci_status (*scic_sds_phy_handler_t)(struct scic_sds_phy *);
typedef enum sci_status (*scic_sds_phy_event_handler_t)(struct scic_sds_phy *, u32);
typedef enum sci_status (*scic_sds_phy_frame_handler_t)(struct scic_sds_phy *, u32);
typedef enum sci_status (*scic_sds_phy_power_handler_t)(struct scic_sds_phy *);

struct scic_sds_phy_state_handler {
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

struct scic_phy_proto;
void scic_sds_phy_get_protocols(
	struct scic_sds_phy *sci_phy,
	struct scic_phy_proto *protocols);
enum sas_linkrate sci_phy_linkrate(struct scic_sds_phy *sci_phy);

struct isci_host;
void isci_phy_init(struct isci_phy *iphy, struct isci_host *ihost, int index);
int isci_phy_control(struct asd_sas_phy *phy, enum phy_func func, void *buf);

#endif /* !defined(_ISCI_PHY_H_) */
