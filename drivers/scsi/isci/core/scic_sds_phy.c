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

#include "intel_ata.h"
#include "intel_sata.h"
#include "sci_base_state.h"
#include "sci_base_state_machine.h"
#include "scic_phy.h"
#include "scic_sds_controller.h"
#include "scic_sds_phy.h"
#include "scic_sds_phy_registers.h"
#include "scic_sds_port.h"
#include "scic_sds_remote_node_context.h"
#include "sci_environment.h"
#include "sci_util.h"
#include "scu_event_codes.h"

#define SCIC_SDS_PHY_MIN_TIMER_COUNT  (SCI_MAX_PHYS)
#define SCIC_SDS_PHY_MAX_TIMER_COUNT  (SCI_MAX_PHYS)

/* Maximum arbitration wait time in micro-seconds */
#define SCIC_SDS_PHY_MAX_ARBITRATION_WAIT_TIME  (700)

enum sas_linkrate sci_phy_linkrate(struct scic_sds_phy *sci_phy)
{
	return sci_phy->max_negotiated_speed;
}

/*
 * *****************************************************************************
 * * SCIC SDS PHY Internal Methods
 * ***************************************************************************** */

/**
 * This method will initialize the phy transport layer registers
 * @this_phy:
 * @transport_layer_registers
 *
 * enum sci_status
 */
static enum sci_status scic_sds_phy_transport_layer_initialization(
	struct scic_sds_phy *this_phy,
	struct scu_transport_layer_registers __iomem *transport_layer_registers)
{
	u32 tl_control;

	this_phy->transport_layer_registers = transport_layer_registers;

	SCU_STPTLDARNI_WRITE(this_phy, SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX);

	/* Hardware team recommends that we enable the STP prefetch for all transports */
	tl_control = SCU_TLCR_READ(this_phy);
	tl_control |= SCU_TLCR_GEN_BIT(STP_WRITE_DATA_PREFETCH);
	SCU_TLCR_WRITE(this_phy, tl_control);

	return SCI_SUCCESS;
}

/**
 * This method will initialize the phy link layer registers
 * @sci_phy:
 * @link_layer_registers:
 *
 * enum sci_status
 */
static enum sci_status
scic_sds_phy_link_layer_initialization(struct scic_sds_phy *sci_phy,
				       struct scu_link_layer_registers __iomem *link_layer_registers)
{
	struct scic_sds_controller *scic = sci_phy->owning_port->owning_controller;
	int phy_idx = sci_phy->phy_index;
	struct sci_phy_user_params *phy_user = &scic->user_parameters.sds1.phys[phy_idx];
	struct sci_phy_oem_params *phy_oem = &scic->oem_parameters.sds1.phys[phy_idx];
	u32 phy_configuration;
	struct sas_capabilities phy_capabilities;
	u32 parity_check = 0;
	u32 parity_count = 0;
	u32 llctl, link_rate;
	u32 clksm_value = 0;

	sci_phy->link_layer_registers = link_layer_registers;

	/* Set our IDENTIFY frame data */
	#define SCI_END_DEVICE 0x01

	SCU_SAS_TIID_WRITE(sci_phy, (SCU_SAS_TIID_GEN_BIT(SMP_INITIATOR) |
				     SCU_SAS_TIID_GEN_BIT(SSP_INITIATOR) |
				     SCU_SAS_TIID_GEN_BIT(STP_INITIATOR) |
				     SCU_SAS_TIID_GEN_BIT(DA_SATA_HOST) |
				     SCU_SAS_TIID_GEN_VAL(DEVICE_TYPE, SCI_END_DEVICE)));

	/* Write the device SAS Address */
	SCU_SAS_TIDNH_WRITE(sci_phy, 0xFEDCBA98);
	SCU_SAS_TIDNL_WRITE(sci_phy, phy_idx);

	/* Write the source SAS Address */
	SCU_SAS_TISSAH_WRITE(sci_phy, phy_oem->sas_address.high);
	SCU_SAS_TISSAL_WRITE(sci_phy, phy_oem->sas_address.low);

	/* Clear and Set the PHY Identifier */
	SCU_SAS_TIPID_WRITE(sci_phy, 0x00000000);
	SCU_SAS_TIPID_WRITE(sci_phy, SCU_SAS_TIPID_GEN_VALUE(ID, phy_idx));

	/* Change the initial state of the phy configuration register */
	phy_configuration = SCU_SAS_PCFG_READ(sci_phy);

	/* Hold OOB state machine in reset */
	phy_configuration |=  SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
	SCU_SAS_PCFG_WRITE(sci_phy, phy_configuration);

	/* Configure the SNW capabilities */
	phy_capabilities.u.all = 0;
	phy_capabilities.u.bits.start                      = 1;
	phy_capabilities.u.bits.gen3_without_ssc_supported = 1;
	phy_capabilities.u.bits.gen2_without_ssc_supported = 1;
	phy_capabilities.u.bits.gen1_without_ssc_supported = 1;
	if (scic->oem_parameters.sds1.controller.do_enable_ssc == true) {
		phy_capabilities.u.bits.gen3_with_ssc_supported = 1;
		phy_capabilities.u.bits.gen2_with_ssc_supported = 1;
		phy_capabilities.u.bits.gen1_with_ssc_supported = 1;
	}

	/*
	 * The SAS specification indicates that the phy_capabilities that
	 * are transmitted shall have an even parity.  Calculate the parity. */
	parity_check = phy_capabilities.u.all;
	while (parity_check != 0) {
		if (parity_check & 0x1)
			parity_count++;
		parity_check >>= 1;
	}

	/*
	 * If parity indicates there are an odd number of bits set, then
	 * set the parity bit to 1 in the phy capabilities. */
	if ((parity_count % 2) != 0)
		phy_capabilities.u.bits.parity = 1;

	SCU_SAS_PHYCAP_WRITE(sci_phy, phy_capabilities.u.all);

	/* Set the enable spinup period but disable the ability to send
	 * notify enable spinup
	 */
	SCU_SAS_ENSPINUP_WRITE(sci_phy, SCU_ENSPINUP_GEN_VAL(COUNT,
			       phy_user->notify_enable_spin_up_insertion_frequency));

	/* Write the ALIGN Insertion Ferequency for connected phy and
	 * inpendent of connected state
	 */
	clksm_value = SCU_ALIGN_INSERTION_FREQUENCY_GEN_VAL(CONNECTED,
			phy_user->in_connection_align_insertion_frequency);

	clksm_value |= SCU_ALIGN_INSERTION_FREQUENCY_GEN_VAL(GENERAL,
			phy_user->align_insertion_frequency);

	SCU_SAS_CLKSM_WRITE(sci_phy, clksm_value);

	/* @todo Provide a way to write this register correctly */
	scu_link_layer_register_write(sci_phy, afe_lookup_table_control, 0x02108421);

	llctl = SCU_SAS_LLCTL_GEN_VAL(NO_OUTBOUND_TASK_TIMEOUT,
		(u8)scic->user_parameters.sds1.no_outbound_task_timeout);

	switch(phy_user->max_speed_generation) {
	case SCIC_SDS_PARM_GEN3_SPEED:
		link_rate = SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN3;
		break;
	case SCIC_SDS_PARM_GEN2_SPEED:
		link_rate = SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN2;
		break;
	default:
		link_rate = SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN1;
		break;
	}
	llctl |= SCU_SAS_LLCTL_GEN_VAL(MAX_LINK_RATE, link_rate);

	scu_link_layer_register_write(sci_phy, link_layer_control, llctl);

	if (is_a0() || is_a2()) {
		/* Program the max ARB time for the PHY to 700us so we inter-operate with
		 * the PMC expander which shuts down PHYs if the expander PHY generates too
		 * many breaks.  This time value will guarantee that the initiator PHY will
		 * generate the break.
		 */
		scu_link_layer_register_write(sci_phy,
					      maximum_arbitration_wait_timer_timeout,
					      SCIC_SDS_PHY_MAX_ARBITRATION_WAIT_TIME);
	}

	/*
	 * Set the link layer hang detection to 500ms (0x1F4) from its default
	 * value of 128ms.  Max value is 511 ms. */
	scu_link_layer_register_write(sci_phy, link_layer_hang_detection_timeout,
				      0x1F4);

	/* We can exit the initial state to the stopped state */
	sci_base_state_machine_change_state(&sci_phy->parent.state_machine,
					    SCI_BASE_PHY_STATE_STOPPED);

	return SCI_SUCCESS;
}

/**
 * This function will handle the sata SIGNATURE FIS timeout condition.  It will
 * restart the starting substate machine since we dont know what has actually
 * happening.
 */
static void scic_sds_phy_sata_timeout(void *phy)
{
	struct scic_sds_phy *sci_phy = phy;

	dev_dbg(sciphy_to_dev(sci_phy),
		 "%s: SCIC SDS Phy 0x%p did not receive signature fis before "
		 "timeout.\n",
		 __func__,
		 sci_phy);

	sci_base_state_machine_stop(&sci_phy->starting_substate_machine);

	sci_base_state_machine_change_state(&sci_phy->parent.state_machine,
					    SCI_BASE_PHY_STATE_STARTING);
}

/**
 * This method returns the port currently containing this phy. If the phy is
 *    currently contained by the dummy port, then the phy is considered to not
 *    be part of a port.
 * @this_phy: This parameter specifies the phy for which to retrieve the
 *    containing port.
 *
 * This method returns a handle to a port that contains the supplied phy.
 * NULL This value is returned if the phy is not part of a real
 * port (i.e. it's contained in the dummy port). !NULL All other
 * values indicate a handle/pointer to the port containing the phy.
 */
struct scic_sds_port *scic_sds_phy_get_port(
	struct scic_sds_phy *this_phy)
{
	if (scic_sds_port_get_index(this_phy->owning_port) == SCIC_SDS_DUMMY_PORT)
		return NULL;

	return this_phy->owning_port;
}

/**
 * This method will assign a port to the phy object.
 * @out]: this_phy This parameter specifies the phy for which to assign a port
 *    object.
 *
 *
 */
void scic_sds_phy_set_port(
	struct scic_sds_phy *this_phy,
	struct scic_sds_port *the_port)
{
	this_phy->owning_port = the_port;

	if (this_phy->bcn_received_while_port_unassigned) {
		this_phy->bcn_received_while_port_unassigned = false;
		scic_sds_port_broadcast_change_received(this_phy->owning_port, this_phy);
	}
}

/**
 * This method will initialize the constructed phy
 * @sci_phy:
 * @link_layer_registers:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_initialize(
	struct scic_sds_phy *sci_phy,
	struct scu_transport_layer_registers __iomem *transport_layer_registers,
	struct scu_link_layer_registers __iomem *link_layer_registers)
{
	struct scic_sds_controller *scic = scic_sds_phy_get_controller(sci_phy);
	struct isci_host *ihost = sci_object_get_association(scic);

	/* Create the SIGNATURE FIS Timeout timer for this phy */
	sci_phy->sata_timeout_timer =
		isci_timer_create(
			ihost,
			sci_phy,
			scic_sds_phy_sata_timeout);

	/* Perfrom the initialization of the TL hardware */
	scic_sds_phy_transport_layer_initialization(
			sci_phy,
			transport_layer_registers);

	/* Perofrm the initialization of the PE hardware */
	scic_sds_phy_link_layer_initialization(sci_phy, link_layer_registers);

	/*
	 * There is nothing that needs to be done in this state just
	 * transition to the stopped state. */
	sci_base_state_machine_change_state(&sci_phy->parent.state_machine,
					    SCI_BASE_PHY_STATE_STOPPED);

	return SCI_SUCCESS;
}

/**
 * This method assigns the direct attached device ID for this phy.
 *
 * @this_phy The phy for which the direct attached device id is to
 *       be assigned.
 * @device_id The direct attached device ID to assign to the phy.
 *       This will either be the RNi for the device or an invalid RNi if there
 *       is no current device assigned to the phy.
 */
void scic_sds_phy_setup_transport(
	struct scic_sds_phy *this_phy,
	u32 device_id)
{
	u32 tl_control;

	SCU_STPTLDARNI_WRITE(this_phy, device_id);

	/*
	 * The read should guarantee that the first write gets posted
	 * before the next write
	 */
	tl_control = SCU_TLCR_READ(this_phy);
	tl_control |= SCU_TLCR_GEN_BIT(CLEAR_TCI_NCQ_MAPPING_TABLE);
	SCU_TLCR_WRITE(this_phy, tl_control);
}

/**
 *
 * @this_phy: The phy object to be suspended.
 *
 * This function will perform the register reads/writes to suspend the SCU
 * hardware protocol engine. none
 */
static void scic_sds_phy_suspend(
	struct scic_sds_phy *this_phy)
{
	u32 scu_sas_pcfg_value;

	scu_sas_pcfg_value = SCU_SAS_PCFG_READ(this_phy);
	scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE);
	SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);
	scic_sds_phy_setup_transport(this_phy, SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX);
}

/**
 *
 * @this_phy: The phy object to resume.
 *
 * This function will perform the register reads/writes required to resume the
 * SCU hardware protocol engine. none
 */
void scic_sds_phy_resume(
	struct scic_sds_phy *this_phy)
{
	u32 scu_sas_pcfg_value;

	scu_sas_pcfg_value = SCU_SAS_PCFG_READ(this_phy);

	scu_sas_pcfg_value &= ~SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE);

	SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);
}

/**
 * This method returns the local sas address assigned to this phy.
 * @this_phy: This parameter specifies the phy for which to retrieve the local
 *    SAS address.
 * @sas_address: This parameter specifies the location into which to copy the
 *    local SAS address.
 *
 */
void scic_sds_phy_get_sas_address(
	struct scic_sds_phy *this_phy,
	struct sci_sas_address *sas_address)
{
	sas_address->high = SCU_SAS_TISSAH_READ(this_phy);
	sas_address->low  = SCU_SAS_TISSAL_READ(this_phy);
}

/**
 * This method returns the remote end-point (i.e. attached) sas address
 *    assigned to this phy.
 * @this_phy: This parameter specifies the phy for which to retrieve the remote
 *    end-point SAS address.
 * @sas_address: This parameter specifies the location into which to copy the
 *    remote end-point SAS address.
 *
 */
void scic_sds_phy_get_attached_sas_address(
	struct scic_sds_phy *this_phy,
	struct sci_sas_address *sas_address)
{
	sas_address->high
		= this_phy->phy_type.sas.identify_address_frame_buffer.sas_address.high;
	sas_address->low
		= this_phy->phy_type.sas.identify_address_frame_buffer.sas_address.low;
}

/**
 * This method returns the supported protocols assigned to this phy
 * @this_phy:
 *
 *
 */
void scic_sds_phy_get_protocols(
	struct scic_sds_phy *this_phy,
	struct sci_sas_identify_address_frame_protocols *protocols)
{
	protocols->u.all = (u16)(SCU_SAS_TIID_READ(this_phy) & 0x0000FFFF);
}

/**
 *
 * @this_phy: The parameter is the phy object for which the attached phy
 *    protcols are to be returned.
 *
 * This method returns the supported protocols for the attached phy.  If this
 * is a SAS phy the protocols are returned from the identify address frame. If
 * this is a SATA phy then protocols are made up and the target phy is an STP
 * target phy. The caller will get the entire set of bits for the protocol
 * value.
 */
void scic_sds_phy_get_attached_phy_protocols(
	struct scic_sds_phy *this_phy,
	struct sci_sas_identify_address_frame_protocols *protocols)
{
	protocols->u.all = 0;

	if (this_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SAS) {
		protocols->u.all =
			this_phy->phy_type.sas.identify_address_frame_buffer.protocols.u.all;
	} else if (this_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SATA) {
		protocols->u.bits.stp_target = 1;
	}
}

/*
 * *****************************************************************************
 * * SCIC SDS PHY Handler Redirects
 * ***************************************************************************** */

/**
 * This method will attempt to start the phy object. This request is only valid
 *    when the phy is in the stopped state
 * @sci_phy:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_start(struct scic_sds_phy *sci_phy)
{
	return sci_phy->state_handlers->parent.start_handler(&sci_phy->parent);
}

/**
 * This method will attempt to stop the phy object.
 * @sci_phy:
 *
 * enum sci_status SCI_SUCCESS if the phy is going to stop SCI_INVALID_STATE
 * if the phy is not in a valid state to stop
 */
enum sci_status scic_sds_phy_stop(struct scic_sds_phy *sci_phy)
{
	return sci_phy->state_handlers->parent.stop_handler(&sci_phy->parent);
}

/**
 * This method will attempt to reset the phy.  This request is only valid when
 *    the phy is in an ready state
 * @this_phy:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_reset(
	struct scic_sds_phy *this_phy)
{
	return this_phy->state_handlers->parent.reset_handler(
		       &this_phy->parent
		       );
}

/**
 * This method will process the event code received.
 * @this_phy:
 * @event_code:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_event_handler(
	struct scic_sds_phy *this_phy,
	u32 event_code)
{
	return this_phy->state_handlers->event_handler(this_phy, event_code);
}

/**
 * This method will process the frame index received.
 * @this_phy:
 * @frame_index:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_frame_handler(
	struct scic_sds_phy *this_phy,
	u32 frame_index)
{
	return this_phy->state_handlers->frame_handler(this_phy, frame_index);
}

/**
 * This method will give the phy permission to consume power
 * @this_phy:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_consume_power_handler(
	struct scic_sds_phy *this_phy)
{
	return this_phy->state_handlers->consume_power_handler(this_phy);
}

/*
 * *****************************************************************************
 * * SCIC PHY Public Methods
 * ***************************************************************************** */


enum sci_status scic_sas_phy_get_properties(
	struct scic_sds_phy *sci_phy,
	struct scic_sas_phy_properties *properties)
{
	if (sci_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SAS) {
		memcpy(
			&properties->received_iaf,
			&sci_phy->phy_type.sas.identify_address_frame_buffer,
			sizeof(struct sci_sas_identify_address_frame)
			);

		properties->received_capabilities.u.all
			= SCU_SAS_RECPHYCAP_READ(sci_phy);

		return SCI_SUCCESS;
	}

	return SCI_FAILURE;
}


enum sci_status scic_sata_phy_get_properties(
	struct scic_sds_phy *sci_phy,
	struct scic_sata_phy_properties *properties)
{
	if (sci_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SATA) {
		memcpy(
			&properties->signature_fis,
			&sci_phy->phy_type.sata.signature_fis_buffer,
			sizeof(struct sata_fis_reg_d2h)
			);

		/* / @todo add support for port selectors. */
		properties->is_port_selector_present = false;

		return SCI_SUCCESS;
	}

	return SCI_FAILURE;
}

/*
 * *****************************************************************************
 * * SCIC SDS PHY HELPER FUNCTIONS
 * ***************************************************************************** */


/**
 *
 * @this_phy: The phy object that received SAS PHY DETECTED.
 *
 * This method continues the link training for the phy as if it were a SAS PHY
 * instead of a SATA PHY. This is done because the completion queue had a SAS
 * PHY DETECTED event when the state machine was expecting a SATA PHY event.
 * none
 */
static void scic_sds_phy_start_sas_link_training(
	struct scic_sds_phy *this_phy)
{
	u32 phy_control;

	phy_control = SCU_SAS_PCFG_READ(this_phy);
	phy_control |= SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD);
	SCU_SAS_PCFG_WRITE(this_phy, phy_control);

	sci_base_state_machine_change_state(
		&this_phy->starting_substate_machine,
		SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN
		);

	this_phy->protocol = SCIC_SDS_PHY_PROTOCOL_SAS;
}

/**
 *
 * @this_phy: The phy object that received a SATA SPINUP HOLD event
 *
 * This method continues the link training for the phy as if it were a SATA PHY
 * instead of a SAS PHY.  This is done because the completion queue had a SATA
 * SPINUP HOLD event when the state machine was expecting a SAS PHY event. none
 */
static void scic_sds_phy_start_sata_link_training(
	struct scic_sds_phy *this_phy)
{
	sci_base_state_machine_change_state(
		&this_phy->starting_substate_machine,
		SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER
		);

	this_phy->protocol = SCIC_SDS_PHY_PROTOCOL_SATA;
}

/**
 * scic_sds_phy_complete_link_training - perform processing common to
 *    all protocols upon completion of link training.
 * @sci_phy: This parameter specifies the phy object for which link training
 *    has completed.
 * @max_link_rate: This parameter specifies the maximum link rate to be
 *    associated with this phy.
 * @next_state: This parameter specifies the next state for the phy's starting
 *    sub-state machine.
 *
 */
static void scic_sds_phy_complete_link_training(
	struct scic_sds_phy *sci_phy,
	enum sci_sas_link_rate max_link_rate,
	u32 next_state)
{
	sci_phy->max_negotiated_speed = max_link_rate;

	sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
					    next_state);
}

static void scic_sds_phy_restart_starting_state(
	struct scic_sds_phy *sci_phy)
{
	/* Stop the current substate machine */
	sci_base_state_machine_stop(&sci_phy->starting_substate_machine);

	/* Re-enter the base state machine starting state */
	sci_base_state_machine_change_state(&sci_phy->parent.state_machine,
					    SCI_BASE_PHY_STATE_STARTING);
}

/* ****************************************************************************
   * SCIC SDS PHY general handlers
   ************************************************************************** */
static enum sci_status scic_sds_phy_starting_substate_general_stop_handler(
	struct sci_base_phy *phy)
{
	struct scic_sds_phy *this_phy;
	this_phy = (struct scic_sds_phy *)phy;

	sci_base_state_machine_stop(&this_phy->starting_substate_machine);

	sci_base_state_machine_change_state(&phy->state_machine,
						 SCI_BASE_PHY_STATE_STOPPED);

	return SCI_SUCCESS;
}

/*
 * *****************************************************************************
 * * SCIC SDS PHY EVENT_HANDLERS
 * ***************************************************************************** */

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SPEED_EN. -
 * decode the event - sas phy detected causes a state transition to the wait
 * for speed event notification. - any other events log a warning message and
 * set a failure status enum sci_status SCI_SUCCESS on any valid event notification
 * SCI_FAILURE on any unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_ossp_event_handler(
	struct scic_sds_phy *this_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_SAS_PHY_DETECTED:
		scic_sds_phy_start_sas_link_training(this_phy);
		this_phy->is_in_link_training = true;
		break;

	case SCU_EVENT_SATA_SPINUP_HOLD:
		scic_sds_phy_start_sata_link_training(this_phy);
		this_phy->is_in_link_training = true;
		break;

	default:
		dev_dbg(sciphy_to_dev(this_phy),
			"%s: PHY starting substate machine received "
			"unexpected event_code %x\n",
			__func__,
			event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SPEED_EN. -
 * decode the event - sas phy detected returns us back to this state. - speed
 * event detected causes a state transition to the wait for iaf. - identify
 * timeout is an un-expected event and the state machine is restarted. - link
 * failure events restart the starting state machine - any other events log a
 * warning message and set a failure status enum sci_status SCI_SUCCESS on any valid
 * event notification SCI_FAILURE on any unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sas_phy_speed_event_handler(
	struct scic_sds_phy *this_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_SAS_PHY_DETECTED:
		/*
		 * Why is this being reported again by the controller?
		 * We would re-enter this state so just stay here */
		break;

	case SCU_EVENT_SAS_15:
	case SCU_EVENT_SAS_15_SSC:
		scic_sds_phy_complete_link_training(
			this_phy, SCI_SAS_150_GB, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF
			);
		break;

	case SCU_EVENT_SAS_30:
	case SCU_EVENT_SAS_30_SSC:
		scic_sds_phy_complete_link_training(
			this_phy, SCI_SAS_300_GB, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF
			);
		break;

	case SCU_EVENT_SAS_60:
	case SCU_EVENT_SAS_60_SSC:
		scic_sds_phy_complete_link_training(
			this_phy, SCI_SAS_600_GB, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF
			);
		break;

	case SCU_EVENT_SATA_SPINUP_HOLD:
		/*
		 * We were doing SAS PHY link training and received a SATA PHY event
		 * continue OOB/SN as if this were a SATA PHY */
		scic_sds_phy_start_sata_link_training(this_phy);
		break;

	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(this_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(this_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF. -
 * decode the event - sas phy detected event backs up the state machine to the
 * await speed notification. - identify timeout is an un-expected event and the
 * state machine is restarted. - link failure events restart the starting state
 * machine - any other events log a warning message and set a failure status
 * enum sci_status SCI_SUCCESS on any valid event notification SCI_FAILURE on any
 * unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_iaf_uf_event_handler(
	struct scic_sds_phy *this_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_SAS_PHY_DETECTED:
		/* Backup the state machine */
		scic_sds_phy_start_sas_link_training(this_phy);
		break;

	case SCU_EVENT_SATA_SPINUP_HOLD:
		/*
		 * We were doing SAS PHY link training and received a SATA PHY event
		 * continue OOB/SN as if this were a SATA PHY */
		scic_sds_phy_start_sata_link_training(this_phy);
		break;

	case SCU_EVENT_RECEIVED_IDENTIFY_TIMEOUT:
	case SCU_EVENT_LINK_FAILURE:
	case SCU_EVENT_HARD_RESET_RECEIVED:
		/* Start the oob/sn state machine over again */
		scic_sds_phy_restart_starting_state(this_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(this_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_POWER. -
 * decode the event - link failure events restart the starting state machine -
 * any other events log a warning message and set a failure status enum sci_status
 * SCI_SUCCESS on a link failure event SCI_FAILURE on any unexpected event
 * notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sas_power_event_handler(
	struct scic_sds_phy *this_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(this_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(this_phy),
			"%s: PHY starting substate machine received unexpected "
			"event_code %x\n",
			__func__,
			event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER. -
 * decode the event - link failure events restart the starting state machine -
 * sata spinup hold events are ignored since they are expected - any other
 * events log a warning message and set a failure status enum sci_status SCI_SUCCESS
 * on a link failure event SCI_FAILURE on any unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sata_power_event_handler(
	struct scic_sds_phy *this_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(this_phy);
		break;

	case SCU_EVENT_SATA_SPINUP_HOLD:
		/* These events are received every 10ms and are expected while in this state */
		break;

	case SCU_EVENT_SAS_PHY_DETECTED:
		/*
		 * There has been a change in the phy type before OOB/SN for the
		 * SATA finished start down the SAS link traning path. */
		scic_sds_phy_start_sas_link_training(this_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(this_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 * scic_sds_phy_starting_substate_await_sata_phy_event_handler -
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN. -
 * decode the event - link failure events restart the starting state machine -
 * sata spinup hold events are ignored since they are expected - sata phy
 * detected event change to the wait speed event - any other events log a
 * warning message and set a failure status enum sci_status SCI_SUCCESS on a link
 * failure event SCI_FAILURE on any unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sata_phy_event_handler(
	struct scic_sds_phy *sci_phy, u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(sci_phy);
		break;

	case SCU_EVENT_SATA_SPINUP_HOLD:
		/* These events might be received since we dont know how many may be in
		 * the completion queue while waiting for power
		 */
		break;

	case SCU_EVENT_SATA_PHY_DETECTED:
		sci_phy->protocol = SCIC_SDS_PHY_PROTOCOL_SATA;

		/* We have received the SATA PHY notification change state */
		sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
						    SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN);
		break;

	case SCU_EVENT_SAS_PHY_DETECTED:
		/* There has been a change in the phy type before OOB/SN for the
		 * SATA finished start down the SAS link traning path.
		 */
		scic_sds_phy_start_sas_link_training(sci_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN.
 * - decode the event - sata phy detected returns us back to this state. -
 * speed event detected causes a state transition to the wait for signature. -
 * link failure events restart the starting state machine - any other events
 * log a warning message and set a failure status enum sci_status SCI_SUCCESS on any
 * valid event notification SCI_FAILURE on any unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sata_speed_event_handler(
	struct scic_sds_phy *this_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_SATA_PHY_DETECTED:
		/*
		 * The hardware reports multiple SATA PHY detected events
		 * ignore the extras */
		break;

	case SCU_EVENT_SATA_15:
	case SCU_EVENT_SATA_15_SSC:
		scic_sds_phy_complete_link_training(
			this_phy,
			SCI_SAS_150_GB,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF
			);
		break;

	case SCU_EVENT_SATA_30:
	case SCU_EVENT_SATA_30_SSC:
		scic_sds_phy_complete_link_training(
			this_phy,
			SCI_SAS_300_GB,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF
			);
		break;

	case SCU_EVENT_SATA_60:
	case SCU_EVENT_SATA_60_SSC:
		scic_sds_phy_complete_link_training(
			this_phy,
			SCI_SAS_600_GB,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF
			);
		break;

	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(this_phy);
		break;

	case SCU_EVENT_SAS_PHY_DETECTED:
		/*
		 * There has been a change in the phy type before OOB/SN for the
		 * SATA finished start down the SAS link traning path. */
		scic_sds_phy_start_sas_link_training(this_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(this_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 * scic_sds_phy_starting_substate_await_sig_fis_event_handler -
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF. -
 * decode the event - sas phy detected event backs up the state machine to the
 * await speed notification. - identify timeout is an un-expected event and the
 * state machine is restarted. - link failure events restart the starting state
 * machine - any other events log a warning message and set a failure status
 * enum sci_status SCI_SUCCESS on any valid event notification SCI_FAILURE on any
 * unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sig_fis_event_handler(
	struct scic_sds_phy *sci_phy, u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_SATA_PHY_DETECTED:
		/* Backup the state machine */
		sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
						    SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN);
		break;

	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(sci_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}


/*
 * *****************************************************************************
 * *  SCIC SDS PHY FRAME_HANDLERS
 * ***************************************************************************** */

/**
 *
 * @phy: This is struct scic_sds_phy object which is being requested to decode the
 *    frame data.
 * @frame_index: This is the index of the unsolicited frame which was received
 *    for this phy.
 *
 * This method decodes the unsolicited frame when the struct scic_sds_phy is in the
 * SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF. - Get the UF Header - If the UF
 * is an IAF - Copy IAF data to local phy object IAF data buffer. - Change
 * starting substate to wait power. - else - log warning message of unexpected
 * unsolicted frame - release frame buffer enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_phy_starting_substate_await_iaf_uf_frame_handler(
	struct scic_sds_phy *sci_phy, u32 frame_index)
{
	enum sci_status result;
	u32 *frame_words;
	struct sci_sas_identify_address_frame *identify_frame;

	result = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_phy_get_controller(sci_phy)->uf_control),
		frame_index,
		(void **)&frame_words);

	if (result != SCI_SUCCESS) {
		return result;
	}

	frame_words[0] = SCIC_SWAP_DWORD(frame_words[0]);
	identify_frame = (struct sci_sas_identify_address_frame *)frame_words;

	if (identify_frame->address_frame_type == 0) {
		u32 state;

		/* Byte swap the rest of the frame so we can make
		 * a copy of the buffer
		 */
		frame_words[1] = SCIC_SWAP_DWORD(frame_words[1]);
		frame_words[2] = SCIC_SWAP_DWORD(frame_words[2]);
		frame_words[3] = SCIC_SWAP_DWORD(frame_words[3]);
		frame_words[4] = SCIC_SWAP_DWORD(frame_words[4]);
		frame_words[5] = SCIC_SWAP_DWORD(frame_words[5]);

		memcpy(&sci_phy->phy_type.sas.identify_address_frame_buffer,
			identify_frame,
			sizeof(struct sci_sas_identify_address_frame));

		if (identify_frame->protocols.u.bits.smp_target) {
			/* We got the IAF for an expander PHY go to the final state since
			 * there are no power requirements for expander phys.
			 */
			state = SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL;
		} else {
			/* We got the IAF we can now go to the await spinup semaphore state */
			state = SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER;
		}
		sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
						    state);
		result = SCI_SUCCESS;
	} else
		dev_warn(sciphy_to_dev(sci_phy),
			"%s: PHY starting substate machine received "
			"unexpected frame id %x\n",
			__func__,
			frame_index);

	/* Regardless of the result release this frame since we are done with it */
	scic_sds_controller_release_frame(scic_sds_phy_get_controller(sci_phy),
					  frame_index);

	return result;
}

/**
 *
 * @phy: This is struct scic_sds_phy object which is being requested to decode the
 *    frame data.
 * @frame_index: This is the index of the unsolicited frame which was received
 *    for this phy.
 *
 * This method decodes the unsolicited frame when the struct scic_sds_phy is in the
 * SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF. - Get the UF Header - If
 * the UF is an SIGNATURE FIS - Copy IAF data to local phy object SIGNATURE FIS
 * data buffer. - else - log warning message of unexpected unsolicted frame -
 * release frame buffer enum sci_status SCI_SUCCESS Must decode the SIGNATURE FIS
 * data
 */
static enum sci_status scic_sds_phy_starting_substate_await_sig_fis_frame_handler(
	struct scic_sds_phy *sci_phy,
	u32 frame_index)
{
	enum sci_status result;
	u32 *frame_words;
	struct sata_fis_header *fis_frame_header;
	u32 *fis_frame_data;

	result = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_phy_get_controller(sci_phy)->uf_control),
		frame_index,
		(void **)&frame_words);

	if (result != SCI_SUCCESS) {
		return result;
	}

	fis_frame_header = (struct sata_fis_header *)frame_words;

	if ((fis_frame_header->fis_type == SATA_FIS_TYPE_REGD2H) &&
	    !(fis_frame_header->status & ATA_STATUS_REG_BSY_BIT)) {
		scic_sds_unsolicited_frame_control_get_buffer(
			&(scic_sds_phy_get_controller(sci_phy)->uf_control),
			frame_index,
			(void **)&fis_frame_data);

		scic_sds_controller_copy_sata_response(
			&sci_phy->phy_type.sata.signature_fis_buffer,
			frame_words,
			fis_frame_data);

		/* We got the IAF we can now go to the await spinup semaphore state */
		sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
						    SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL);

		result = SCI_SUCCESS;
	} else
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected frame id %x\n",
			 __func__,
			 frame_index);

	/* Regardless of the result release this frame since we are done with it */
	scic_sds_controller_release_frame(scic_sds_phy_get_controller(sci_phy),
					  frame_index);

	return result;
}

/*
 * *****************************************************************************
 * * SCIC SDS PHY POWER_HANDLERS
 * ***************************************************************************** */

/**
 * scic_sds_phy_starting_substate_await_sas_power_consume_power_handler -
 * @phy: This is the struct sci_base_phy object which is cast into a struct scic_sds_phy
 *    object.
 *
 * This method is called by the struct scic_sds_controller when the phy object is
 * granted power. - The notify enable spinups are turned on for this phy object
 * - The phy state machine is transitioned to the
 * SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_phy_starting_substate_await_sas_power_consume_power_handler(
	struct scic_sds_phy *sci_phy)
{
	u32 enable_spinup;

	enable_spinup = SCU_SAS_ENSPINUP_READ(sci_phy);
	enable_spinup |= SCU_ENSPINUP_GEN_BIT(ENABLE);
	SCU_SAS_ENSPINUP_WRITE(sci_phy, enable_spinup);

	/* Change state to the final state this substate machine has run to completion */
	sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
					    SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL);

	return SCI_SUCCESS;
}

/**
 *
 * @phy: This is the struct sci_base_phy object which is cast into a struct scic_sds_phy
 *    object.
 *
 * This method is called by the struct scic_sds_controller when the phy object is
 * granted power. - The phy state machine is transitioned to the
 * SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_phy_starting_substate_await_sata_power_consume_power_handler(
	struct scic_sds_phy *sci_phy)
{
	u32 scu_sas_pcfg_value;

	/* Release the spinup hold state and reset the OOB state machine */
	scu_sas_pcfg_value = SCU_SAS_PCFG_READ(sci_phy);
	scu_sas_pcfg_value &=
		~(SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD) | SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE));
	scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
	SCU_SAS_PCFG_WRITE(sci_phy, scu_sas_pcfg_value);

	/* Now restart the OOB operation */
	scu_sas_pcfg_value &= ~SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
	scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
	SCU_SAS_PCFG_WRITE(sci_phy, scu_sas_pcfg_value);

	/* Change state to the final state this substate machine has run to completion */
	sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
					    SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN);

	return SCI_SUCCESS;
}

static enum sci_status default_phy_handler(struct sci_base_phy *base_phy, const char *func)
{
	struct scic_sds_phy *sci_phy;

	sci_phy = container_of(base_phy, typeof(*sci_phy), parent);
	dev_dbg(sciphy_to_dev(sci_phy),
		 "%s: in wrong state: %d\n", func,
		 sci_base_state_machine_get_state(&base_phy->state_machine));
	return SCI_FAILURE_INVALID_STATE;
}

static enum sci_status scic_sds_phy_default_start_handler(struct sci_base_phy *base_phy)
{
	return default_phy_handler(base_phy, __func__);
}

static enum sci_status scic_sds_phy_default_stop_handler(struct sci_base_phy *base_phy)
{
	return default_phy_handler(base_phy, __func__);
}

static enum sci_status scic_sds_phy_default_reset_handler(struct sci_base_phy *base_phy)
{
	return default_phy_handler(base_phy, __func__);
}

static enum sci_status scic_sds_phy_default_destroy_handler(struct sci_base_phy *base_phy)
{
	return default_phy_handler(base_phy, __func__);
}

static enum sci_status scic_sds_phy_default_frame_handler(struct scic_sds_phy *sci_phy,
							  u32 frame_index)
{
	struct scic_sds_controller *scic = scic_sds_phy_get_controller(sci_phy);

	default_phy_handler(&sci_phy->parent, __func__);
	scic_sds_controller_release_frame(scic, frame_index);

	return SCI_FAILURE_INVALID_STATE;
}

static enum sci_status scic_sds_phy_default_event_handler(struct scic_sds_phy *sci_phy,
							  u32 event_code)
{
	return default_phy_handler(&sci_phy->parent, __func__);
}

static enum sci_status scic_sds_phy_default_consume_power_handler(struct scic_sds_phy *sci_phy)
{
	return default_phy_handler(&sci_phy->parent, __func__);
}



static const struct scic_sds_phy_state_handler scic_sds_phy_starting_substate_handler_table[] = {
	[SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL] = {
		.parent.start_handler    = scic_sds_phy_default_start_handler,
		.parent.stop_handler     = scic_sds_phy_starting_substate_general_stop_handler,
		.parent.reset_handler    = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler           = scic_sds_phy_default_frame_handler,
		.event_handler           = scic_sds_phy_default_event_handler,
		.consume_power_handler   = scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN] = {
		.parent.start_handler	 = scic_sds_phy_default_start_handler,
		.parent.stop_handler	 = scic_sds_phy_starting_substate_general_stop_handler,
		.parent.reset_handler	 = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_starting_substate_await_ossp_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN] = {
		.parent.start_handler	 = scic_sds_phy_default_start_handler,
		.parent.stop_handler	 = scic_sds_phy_starting_substate_general_stop_handler,
		.parent.reset_handler	 = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_starting_substate_await_sas_phy_speed_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF] = {
		.parent.start_handler	 = scic_sds_phy_default_start_handler,
		.parent.stop_handler	 = scic_sds_phy_default_stop_handler,
		.parent.reset_handler	 = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_starting_substate_await_iaf_uf_frame_handler,
		.event_handler		 = scic_sds_phy_starting_substate_await_iaf_uf_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER] = {
		.parent.start_handler	 = scic_sds_phy_default_start_handler,
		.parent.stop_handler	 = scic_sds_phy_starting_substate_general_stop_handler,
		.parent.reset_handler	 = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_starting_substate_await_sas_power_event_handler,
		.consume_power_handler	 = scic_sds_phy_starting_substate_await_sas_power_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER] = {
		.parent.start_handler	 = scic_sds_phy_default_start_handler,
		.parent.stop_handler	 = scic_sds_phy_starting_substate_general_stop_handler,
		.parent.reset_handler	 = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_starting_substate_await_sata_power_event_handler,
		.consume_power_handler	 = scic_sds_phy_starting_substate_await_sata_power_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN] = {
		.parent.start_handler	 = scic_sds_phy_default_start_handler,
		.parent.stop_handler	 = scic_sds_phy_starting_substate_general_stop_handler,
		.parent.reset_handler	 = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_starting_substate_await_sata_phy_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN] = {
		.parent.start_handler	 = scic_sds_phy_default_start_handler,
		.parent.stop_handler	 = scic_sds_phy_starting_substate_general_stop_handler,
		.parent.reset_handler	 = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_starting_substate_await_sata_speed_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF] = {
		.parent.start_handler	 = scic_sds_phy_default_start_handler,
		.parent.stop_handler	 = scic_sds_phy_starting_substate_general_stop_handler,
		.parent.reset_handler	 = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_starting_substate_await_sig_fis_frame_handler,
		.event_handler		 = scic_sds_phy_starting_substate_await_sig_fis_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL] = {
		.parent.start_handler	 = scic_sds_phy_default_start_handler,
		.parent.stop_handler	 = scic_sds_phy_starting_substate_general_stop_handler,
		.parent.reset_handler	 = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_default_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	}
};

/**
 * scic_sds_phy_set_starting_substate_handlers() -
 *
 * This macro sets the starting substate handlers by state_id
 */
#define scic_sds_phy_set_starting_substate_handlers(phy, state_id) \
	scic_sds_phy_set_state_handlers(\
		(phy), \
		&scic_sds_phy_starting_substate_handler_table[(state_id)] \
		)

/*
 * ****************************************************************************
 * *  PHY STARTING SUBSTATE METHODS
 * **************************************************************************** */

/**
 * scic_sds_phy_starting_initial_substate_enter -
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL. - The initial state
 * handlers are put in place for the struct scic_sds_phy object. - The state is
 * changed to the wait phy type event notification. none
 */
static void scic_sds_phy_starting_initial_substate_enter(struct sci_base_object *object)
{
	struct scic_sds_phy *sci_phy;

	sci_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_starting_substate_handlers(
		sci_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL);

	/* This is just an temporary state go off to the starting state */
	sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
					    SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_PHY_TYPE_EN. - Set the
 * struct scic_sds_phy object state handlers for this state. none
 */
static void scic_sds_phy_starting_await_ossp_en_substate_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_starting_substate_handlers(
		this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SPEED_EN. - Set the
 * struct scic_sds_phy object state handlers for this state. none
 */
static void scic_sds_phy_starting_await_sas_speed_en_substate_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_starting_substate_handlers(
		this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF. - Set the
 * struct scic_sds_phy object state handlers for this state. none
 */
static void scic_sds_phy_starting_await_iaf_uf_substate_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_starting_substate_handlers(
		this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER. - Set the
 * struct scic_sds_phy object state handlers for this state. - Add this phy object to
 * the power control queue none
 */
static void scic_sds_phy_starting_await_sas_power_substate_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_starting_substate_handlers(
		this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER
		);

	scic_sds_controller_power_control_queue_insert(
		scic_sds_phy_get_controller(this_phy),
		this_phy
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on exiting
 * the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER. - Remove the
 * struct scic_sds_phy object from the power control queue. none
 */
static void scic_sds_phy_starting_await_sas_power_substate_exit(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_controller_power_control_queue_remove(
		scic_sds_phy_get_controller(this_phy), this_phy
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER. - Set the
 * struct scic_sds_phy object state handlers for this state. - Add this phy object to
 * the power control queue none
 */
static void scic_sds_phy_starting_await_sata_power_substate_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_starting_substate_handlers(
		this_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER
		);

	scic_sds_controller_power_control_queue_insert(
		scic_sds_phy_get_controller(this_phy),
		this_phy
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on exiting
 * the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER. - Remove the
 * struct scic_sds_phy object from the power control queue. none
 */
static void scic_sds_phy_starting_await_sata_power_substate_exit(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_controller_power_control_queue_remove(
		scic_sds_phy_get_controller(this_phy),
		this_phy
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a
 * struct scic_sds_phy object.
 *
 * This function will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN. - Set the
 * struct scic_sds_phy object state handlers for this state. none
 */
static void scic_sds_phy_starting_await_sata_phy_substate_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *sci_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_starting_substate_handlers(
			sci_phy,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN);

	isci_timer_start(sci_phy->sata_timeout_timer,
			 SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a
 * struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy
 * on exiting
 * the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN. - stop the timer
 * that was started on entry to await sata phy event notification none
 */
static inline void scic_sds_phy_starting_await_sata_phy_substate_exit(
	struct sci_base_object *object)
{
	struct scic_sds_phy *sci_phy = (struct scic_sds_phy *)object;

	isci_timer_stop(sci_phy->sata_timeout_timer);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN. - Set the
 * struct scic_sds_phy object state handlers for this state. none
 */
static void scic_sds_phy_starting_await_sata_speed_substate_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *sci_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_starting_substate_handlers(
			sci_phy,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN);

	isci_timer_start(sci_phy->sata_timeout_timer,
			 SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a
 * struct scic_sds_phy object.
 *
 * This function will perform the actions required by the
 * struct scic_sds_phy on exiting
 * the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN. - stop the timer
 * that was started on entry to await sata phy event notification none
 */
static inline void scic_sds_phy_starting_await_sata_speed_substate_exit(
	struct sci_base_object *object)
{
	struct scic_sds_phy *sci_phy = (struct scic_sds_phy *)object;

	isci_timer_stop(sci_phy->sata_timeout_timer);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a
 * struct scic_sds_phy object.
 *
 * This function will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF. - Set the
 * struct scic_sds_phy object state handlers for this state.
 * - Start the SIGNATURE FIS
 * timeout timer none
 */
static void scic_sds_phy_starting_await_sig_fis_uf_substate_enter(
	struct sci_base_object *object)
{
	bool continue_to_ready_state;
	struct scic_sds_phy *sci_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_starting_substate_handlers(
			sci_phy,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF);

	continue_to_ready_state = scic_sds_port_link_detected(
		sci_phy->owning_port,
		sci_phy);

	if (continue_to_ready_state) {
		/*
		 * Clear the PE suspend condition so we can actually
		 * receive SIG FIS
		 * The hardware will not respond to the XRDY until the PE
		 * suspend condition is cleared.
		 */
		scic_sds_phy_resume(sci_phy);

		isci_timer_start(sci_phy->sata_timeout_timer,
				 SCIC_SDS_SIGNATURE_FIS_TIMEOUT);
	} else
		sci_phy->is_in_link_training = false;
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a
 * struct scic_sds_phy object.
 *
 * This function will perform the actions required by the
 * struct scic_sds_phy on exiting
 * the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF. - Stop the SIGNATURE
 * FIS timeout timer. none
 */
static inline void scic_sds_phy_starting_await_sig_fis_uf_substate_exit(
	struct sci_base_object *object)
{
	struct scic_sds_phy *sci_phy;

	sci_phy = (struct scic_sds_phy *)object;

	isci_timer_stop(sci_phy->sata_timeout_timer);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL. - Set the struct scic_sds_phy
 * object state handlers for this state. - Change base state machine to the
 * ready state. none
 */
static void scic_sds_phy_starting_final_substate_enter(struct sci_base_object *object)
{
	struct scic_sds_phy *sci_phy;

	sci_phy = container_of(object, typeof(*sci_phy), parent.parent);

	scic_sds_phy_set_starting_substate_handlers(sci_phy,
						    SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL);

	/* State machine has run to completion so exit out and change
	 * the base state machine to the ready state
	 */
	sci_base_state_machine_change_state(&sci_phy->parent.state_machine,
					    SCI_BASE_PHY_STATE_READY);
}

/* --------------------------------------------------------------------------- */

static const struct sci_base_state scic_sds_phy_starting_substates[] = {
	[SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL] = {
		.enter_state = scic_sds_phy_starting_initial_substate_enter,
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN] = {
		.enter_state = scic_sds_phy_starting_await_ossp_en_substate_enter,
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN] = {
		.enter_state = scic_sds_phy_starting_await_sas_speed_en_substate_enter,
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF] = {
		.enter_state = scic_sds_phy_starting_await_iaf_uf_substate_enter,
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER] = {
		.enter_state = scic_sds_phy_starting_await_sas_power_substate_enter,
		.exit_state  = scic_sds_phy_starting_await_sas_power_substate_exit,
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER] = {
		.enter_state = scic_sds_phy_starting_await_sata_power_substate_enter,
		.exit_state  = scic_sds_phy_starting_await_sata_power_substate_exit
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN] = {
		.enter_state = scic_sds_phy_starting_await_sata_phy_substate_enter,
		.exit_state  = scic_sds_phy_starting_await_sata_phy_substate_exit
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN] = {
		.enter_state = scic_sds_phy_starting_await_sata_speed_substate_enter,
		.exit_state  = scic_sds_phy_starting_await_sata_speed_substate_exit
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF] = {
		.enter_state = scic_sds_phy_starting_await_sig_fis_uf_substate_enter,
		.exit_state  = scic_sds_phy_starting_await_sig_fis_uf_substate_exit
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL] = {
		.enter_state = scic_sds_phy_starting_final_substate_enter,
	}
};

/**
 *
 * @phy: This is the struct sci_base_phy object which is cast into a
 * struct scic_sds_phy object.
 *
 * This method takes the struct scic_sds_phy from a stopped state and
 * attempts to start it. - The phy state machine is transitioned to the
 * SCI_BASE_PHY_STATE_STARTING. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_phy_stopped_state_start_handler(struct sci_base_phy *base_phy)
{
	struct isci_host *ihost;
	struct scic_sds_phy *sci_phy;
	struct scic_sds_controller *scic;

	sci_phy = container_of(base_phy, typeof(*sci_phy), parent);
	scic = scic_sds_phy_get_controller(sci_phy),
	ihost = sci_object_get_association(scic);

	/* Create the SIGNATURE FIS Timeout timer for this phy */
	sci_phy->sata_timeout_timer = isci_timer_create(ihost, sci_phy,
							scic_sds_phy_sata_timeout);

	if (sci_phy->sata_timeout_timer)
		sci_base_state_machine_change_state(&sci_phy->parent.state_machine,
						    SCI_BASE_PHY_STATE_STARTING);

	return SCI_SUCCESS;
}

static enum sci_status scic_sds_phy_stopped_state_destroy_handler(struct sci_base_phy *base_phy)
{
	return SCI_SUCCESS;
}

static enum sci_status scic_sds_phy_ready_state_stop_handler(struct sci_base_phy *base_phy)
{
	sci_base_state_machine_change_state(&base_phy->state_machine,
					    SCI_BASE_PHY_STATE_STOPPED);

	return SCI_SUCCESS;
}

static enum sci_status scic_sds_phy_ready_state_reset_handler(struct sci_base_phy *base_phy)
{
	sci_base_state_machine_change_state(&base_phy->state_machine,
					    SCI_BASE_PHY_STATE_RESETTING);

	return SCI_SUCCESS;
}

/**
 * scic_sds_phy_ready_state_event_handler -
 * @phy: This is the struct scic_sds_phy object which has received the event.
 *
 * This method request the struct scic_sds_phy handle the received event.  The only
 * event that we are interested in while in the ready state is the link failure
 * event. - decoded event is a link failure - transition the struct scic_sds_phy back
 * to the SCI_BASE_PHY_STATE_STARTING state. - any other event received will
 * report a warning message enum sci_status SCI_SUCCESS if the event received is a
 * link failure SCI_FAILURE_INVALID_STATE for any other event received.
 */
static enum sci_status scic_sds_phy_ready_state_event_handler(struct scic_sds_phy *sci_phy,
							      u32 event_code)
{
	enum sci_status result = SCI_FAILURE;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		sci_base_state_machine_change_state(&sci_phy->parent.state_machine,
						    SCI_BASE_PHY_STATE_STARTING);
		result = SCI_SUCCESS;
		break;

	case SCU_EVENT_BROADCAST_CHANGE:
		/* Broadcast change received. Notify the port. */
		if (scic_sds_phy_get_port(sci_phy) != NULL)
			scic_sds_port_broadcast_change_received(sci_phy->owning_port, sci_phy);
		else
			sci_phy->bcn_received_while_port_unassigned = true;
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%sP SCIC PHY 0x%p ready state machine received "
			 "unexpected event_code %x\n",
			 __func__, sci_phy, event_code);

		result = SCI_FAILURE_INVALID_STATE;
		break;
	}

	return result;
}

static enum sci_status scic_sds_phy_resetting_state_event_handler(struct scic_sds_phy *sci_phy,
								  u32 event_code)
{
	enum sci_status result = SCI_FAILURE;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_HARD_RESET_TRANSMITTED:
		/* Link failure change state back to the starting state */
		sci_base_state_machine_change_state(&sci_phy->parent.state_machine,
						    SCI_BASE_PHY_STATE_STARTING);
		result = SCI_SUCCESS;
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: SCIC PHY 0x%p resetting state machine received "
			 "unexpected event_code %x\n",
			 __func__, sci_phy, event_code);

		result = SCI_FAILURE_INVALID_STATE;
		break;
	}

	return result;
}

/* --------------------------------------------------------------------------- */

static const struct scic_sds_phy_state_handler scic_sds_phy_state_handler_table[] = {
	[SCI_BASE_PHY_STATE_INITIAL] = {
		.parent.start_handler = scic_sds_phy_default_start_handler,
		.parent.stop_handler  = scic_sds_phy_default_stop_handler,
		.parent.reset_handler = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_default_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCI_BASE_PHY_STATE_STOPPED]  = {
		.parent.start_handler = scic_sds_phy_stopped_state_start_handler,
		.parent.stop_handler  = scic_sds_phy_default_stop_handler,
		.parent.reset_handler = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_stopped_state_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_default_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCI_BASE_PHY_STATE_STARTING] = {
		.parent.start_handler = scic_sds_phy_default_start_handler,
		.parent.stop_handler  = scic_sds_phy_default_stop_handler,
		.parent.reset_handler = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_default_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCI_BASE_PHY_STATE_READY] = {
		.parent.start_handler = scic_sds_phy_default_start_handler,
		.parent.stop_handler  = scic_sds_phy_ready_state_stop_handler,
		.parent.reset_handler = scic_sds_phy_ready_state_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_ready_state_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCI_BASE_PHY_STATE_RESETTING] = {
		.parent.start_handler = scic_sds_phy_default_start_handler,
		.parent.stop_handler  = scic_sds_phy_default_stop_handler,
		.parent.reset_handler = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_resetting_state_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCI_BASE_PHY_STATE_FINAL] = {
		.parent.start_handler = scic_sds_phy_default_start_handler,
		.parent.stop_handler  = scic_sds_phy_default_stop_handler,
		.parent.reset_handler = scic_sds_phy_default_reset_handler,
		.parent.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_default_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	}
};

/*
 * ****************************************************************************
 * *  PHY STATE PRIVATE METHODS
 * **************************************************************************** */

/**
 *
 * @this_phy: This is the struct scic_sds_phy object to stop.
 *
 * This method will stop the struct scic_sds_phy object. This does not reset the
 * protocol engine it just suspends it and places it in a state where it will
 * not cause the end device to power up. none
 */
static void scu_link_layer_stop_protocol_engine(
	struct scic_sds_phy *this_phy)
{
	u32 scu_sas_pcfg_value;
	u32 enable_spinup_value;

	/* Suspend the protocol engine and place it in a sata spinup hold state */
	scu_sas_pcfg_value  = SCU_SAS_PCFG_READ(this_phy);
	scu_sas_pcfg_value |= (
		SCU_SAS_PCFG_GEN_BIT(OOB_RESET)
		| SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE)
		| SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD)
		);
	SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);

	/* Disable the notify enable spinup primitives */
	enable_spinup_value = SCU_SAS_ENSPINUP_READ(this_phy);
	enable_spinup_value &= ~SCU_ENSPINUP_GEN_BIT(ENABLE);
	SCU_SAS_ENSPINUP_WRITE(this_phy, enable_spinup_value);
}

/**
 *
 *
 * This method will start the OOB/SN state machine for this struct scic_sds_phy object.
 */
static void scu_link_layer_start_oob(
	struct scic_sds_phy *this_phy)
{
	u32 scu_sas_pcfg_value;

	scu_sas_pcfg_value = SCU_SAS_PCFG_READ(this_phy);
	scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
	scu_sas_pcfg_value &=
		~(SCU_SAS_PCFG_GEN_BIT(OOB_RESET) | SCU_SAS_PCFG_GEN_BIT(HARD_RESET));

	SCU_SAS_PCFG_WRITE(this_phy, scu_sas_pcfg_value);
}

/**
 *
 *
 * This method will transmit a hard reset request on the specified phy. The SCU
 * hardware requires that we reset the OOB state machine and set the hard reset
 * bit in the phy configuration register. We then must start OOB over with the
 * hard reset bit set.
 */
static void scu_link_layer_tx_hard_reset(
	struct scic_sds_phy *this_phy)
{
	u32 phy_configuration_value;

	/*
	 * SAS Phys must wait for the HARD_RESET_TX event notification to transition
	 * to the starting state. */
	phy_configuration_value = SCU_SAS_PCFG_READ(this_phy);
	phy_configuration_value |=
		(SCU_SAS_PCFG_GEN_BIT(HARD_RESET) | SCU_SAS_PCFG_GEN_BIT(OOB_RESET));
	SCU_SAS_PCFG_WRITE(this_phy, phy_configuration_value);

	/* Now take the OOB state machine out of reset */
	phy_configuration_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
	phy_configuration_value &= ~SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
	SCU_SAS_PCFG_WRITE(this_phy, phy_configuration_value);
}

/*
 * ****************************************************************************
 * *  PHY BASE STATE METHODS
 * **************************************************************************** */

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_INITIAL. - This function sets the state
 * handlers for the phy object base state machine initial state. none
 */
static void scic_sds_phy_initial_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_INITIAL);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a
 * struct scic_sds_phy object.
 *
 * This function will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_INITIAL. - This function sets the state
 * handlers for the phy object base state machine initial state. - The SCU
 * hardware is requested to stop the protocol engine. none
 */
static void scic_sds_phy_stopped_state_enter(struct sci_base_object *object)
{
	struct scic_sds_phy *sci_phy = (struct scic_sds_phy *)object;
	struct scic_sds_controller *scic = scic_sds_phy_get_controller(sci_phy);
	struct isci_host *ihost = sci_object_get_association(scic);

	sci_phy = (struct scic_sds_phy *)object;

	/*
	 * @todo We need to get to the controller to place this PE in a
	 * reset state
	 */

	scic_sds_phy_set_base_state_handlers(sci_phy,
					     SCI_BASE_PHY_STATE_STOPPED);

	if (sci_phy->sata_timeout_timer != NULL) {
		isci_del_timer(ihost, sci_phy->sata_timeout_timer);

		sci_phy->sata_timeout_timer = NULL;
	}

	scu_link_layer_stop_protocol_engine(sci_phy);

	if (sci_phy->parent.state_machine.previous_state_id !=
			SCI_BASE_PHY_STATE_INITIAL)
		scic_sds_controller_link_down(
				scic_sds_phy_get_controller(sci_phy),
				scic_sds_phy_get_port(sci_phy),
				sci_phy);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_STARTING. - This function sets the state
 * handlers for the phy object base state machine starting state. - The SCU
 * hardware is requested to start OOB/SN on this protocl engine. - The phy
 * starting substate machine is started. - If the previous state was the ready
 * state then the struct scic_sds_controller is informed that the phy has gone link
 * down. none
 */
static void scic_sds_phy_starting_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_STARTING);

	scu_link_layer_stop_protocol_engine(this_phy);
	scu_link_layer_start_oob(this_phy);

	/* We don't know what kind of phy we are going to be just yet */
	this_phy->protocol = SCIC_SDS_PHY_PROTOCOL_UNKNOWN;
	this_phy->bcn_received_while_port_unassigned = false;

	/* Change over to the starting substate machine to continue */
	sci_base_state_machine_start(&this_phy->starting_substate_machine);

	if (this_phy->parent.state_machine.previous_state_id
	    == SCI_BASE_PHY_STATE_READY) {
		scic_sds_controller_link_down(
			scic_sds_phy_get_controller(this_phy),
			scic_sds_phy_get_port(this_phy),
			this_phy
			);
	}
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_READY. - This function sets the state
 * handlers for the phy object base state machine ready state. - The SCU
 * hardware protocol engine is resumed. - The struct scic_sds_controller is informed
 * that the phy object has gone link up. none
 */
static void scic_sds_phy_ready_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_READY);

	scic_sds_controller_link_up(
		scic_sds_phy_get_controller(this_phy),
		scic_sds_phy_get_port(this_phy),
		this_phy
		);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on exiting
 * the SCI_BASE_PHY_STATE_INITIAL. This function suspends the SCU hardware
 * protocol engine represented by this struct scic_sds_phy object. none
 */
static void scic_sds_phy_ready_state_exit(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_suspend(this_phy);
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_RESETTING. - This function sets the state
 * handlers for the phy object base state machine resetting state. none
 */
static void scic_sds_phy_resetting_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_RESETTING);

	/*
	 * The phy is being reset, therefore deactivate it from the port.
	 * In the resetting state we don't notify the user regarding
	 * link up and link down notifications. */
	scic_sds_port_deactivate_phy(this_phy->owning_port, this_phy, false);

	if (this_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SAS) {
		scu_link_layer_tx_hard_reset(this_phy);
	} else {
		/*
		 * The SCU does not need to have a descrete reset state so just go back to
		 * the starting state. */
		sci_base_state_machine_change_state(
			&this_phy->parent.state_machine,
			SCI_BASE_PHY_STATE_STARTING
			);
	}
}

/**
 *
 * @object: This is the struct sci_base_object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_FINAL. - This function sets the state
 * handlers for the phy object base state machine final state. none
 */
static void scic_sds_phy_final_state_enter(
	struct sci_base_object *object)
{
	struct scic_sds_phy *this_phy;

	this_phy = (struct scic_sds_phy *)object;

	scic_sds_phy_set_base_state_handlers(this_phy, SCI_BASE_PHY_STATE_FINAL);

	/* Nothing to do here */
}

/* --------------------------------------------------------------------------- */

static const struct sci_base_state scic_sds_phy_state_table[] = {
	[SCI_BASE_PHY_STATE_INITIAL] = {
		.enter_state = scic_sds_phy_initial_state_enter,
	},
	[SCI_BASE_PHY_STATE_STOPPED] = {
		.enter_state = scic_sds_phy_stopped_state_enter,
	},
	[SCI_BASE_PHY_STATE_STARTING] = {
		.enter_state = scic_sds_phy_starting_state_enter,
	},
	[SCI_BASE_PHY_STATE_READY] = {
		.enter_state = scic_sds_phy_ready_state_enter,
		.exit_state = scic_sds_phy_ready_state_exit,
	},
	[SCI_BASE_PHY_STATE_RESETTING] = {
		.enter_state = scic_sds_phy_resetting_state_enter,
	},
	[SCI_BASE_PHY_STATE_FINAL] = {
		.enter_state = scic_sds_phy_final_state_enter,
	},
};

void scic_sds_phy_construct(struct scic_sds_phy *sci_phy,
			    struct scic_sds_port *owning_port, u8 phy_index)
{
	/*
	 * Call the base constructor first
	 */
	sci_base_phy_construct(&sci_phy->parent, scic_sds_phy_state_table);

	/* Copy the rest of the input data to our locals */
	sci_phy->owning_port = owning_port;
	sci_phy->phy_index = phy_index;
	sci_phy->bcn_received_while_port_unassigned = false;
	sci_phy->protocol = SCIC_SDS_PHY_PROTOCOL_UNKNOWN;
	sci_phy->link_layer_registers = NULL;
	sci_phy->max_negotiated_speed = SCI_SAS_NO_LINK_RATE;
	sci_phy->sata_timeout_timer = NULL;

	/* Clear out the identification buffer data */
	memset(&sci_phy->phy_type, 0, sizeof(sci_phy->phy_type));

	/* Initialize the the substate machines */
	sci_base_state_machine_construct(&sci_phy->starting_substate_machine,
					 &sci_phy->parent.parent,
					 scic_sds_phy_starting_substates,
					 SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL);
}
