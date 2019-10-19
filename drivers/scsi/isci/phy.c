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

#include "isci.h"
#include "host.h"
#include "phy.h"
#include "scu_event_codes.h"
#include "probe_roms.h"

#undef C
#define C(a) (#a)
static const char *phy_state_name(enum sci_phy_states state)
{
	static const char * const strings[] = PHY_STATES;

	return strings[state];
}
#undef C

/* Maximum arbitration wait time in micro-seconds */
#define SCIC_SDS_PHY_MAX_ARBITRATION_WAIT_TIME  (700)

enum sas_linkrate sci_phy_linkrate(struct isci_phy *iphy)
{
	return iphy->max_negotiated_speed;
}

static struct isci_host *phy_to_host(struct isci_phy *iphy)
{
	struct isci_phy *table = iphy - iphy->phy_index;
	struct isci_host *ihost = container_of(table, typeof(*ihost), phys[0]);

	return ihost;
}

static struct device *sciphy_to_dev(struct isci_phy *iphy)
{
	return &phy_to_host(iphy)->pdev->dev;
}

static enum sci_status
sci_phy_transport_layer_initialization(struct isci_phy *iphy,
				       struct scu_transport_layer_registers __iomem *reg)
{
	u32 tl_control;

	iphy->transport_layer_registers = reg;

	writel(SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX,
		&iphy->transport_layer_registers->stp_rni);

	/*
	 * Hardware team recommends that we enable the STP prefetch for all
	 * transports
	 */
	tl_control = readl(&iphy->transport_layer_registers->control);
	tl_control |= SCU_TLCR_GEN_BIT(STP_WRITE_DATA_PREFETCH);
	writel(tl_control, &iphy->transport_layer_registers->control);

	return SCI_SUCCESS;
}

static enum sci_status
sci_phy_link_layer_initialization(struct isci_phy *iphy,
				  struct scu_link_layer_registers __iomem *llr)
{
	struct isci_host *ihost = iphy->owning_port->owning_controller;
	struct sci_phy_user_params *phy_user;
	struct sci_phy_oem_params *phy_oem;
	int phy_idx = iphy->phy_index;
	struct sci_phy_cap phy_cap;
	u32 phy_configuration;
	u32 parity_check = 0;
	u32 parity_count = 0;
	u32 llctl, link_rate;
	u32 clksm_value = 0;
	u32 sp_timeouts = 0;

	phy_user = &ihost->user_parameters.phys[phy_idx];
	phy_oem = &ihost->oem_parameters.phys[phy_idx];
	iphy->link_layer_registers = llr;

	/* Set our IDENTIFY frame data */
	#define SCI_END_DEVICE 0x01

	writel(SCU_SAS_TIID_GEN_BIT(SMP_INITIATOR) |
	       SCU_SAS_TIID_GEN_BIT(SSP_INITIATOR) |
	       SCU_SAS_TIID_GEN_BIT(STP_INITIATOR) |
	       SCU_SAS_TIID_GEN_BIT(DA_SATA_HOST) |
	       SCU_SAS_TIID_GEN_VAL(DEVICE_TYPE, SCI_END_DEVICE),
	       &llr->transmit_identification);

	/* Write the device SAS Address */
	writel(0xFEDCBA98, &llr->sas_device_name_high);
	writel(phy_idx, &llr->sas_device_name_low);

	/* Write the source SAS Address */
	writel(phy_oem->sas_address.high, &llr->source_sas_address_high);
	writel(phy_oem->sas_address.low, &llr->source_sas_address_low);

	/* Clear and Set the PHY Identifier */
	writel(0, &llr->identify_frame_phy_id);
	writel(SCU_SAS_TIPID_GEN_VALUE(ID, phy_idx), &llr->identify_frame_phy_id);

	/* Change the initial state of the phy configuration register */
	phy_configuration = readl(&llr->phy_configuration);

	/* Hold OOB state machine in reset */
	phy_configuration |=  SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
	writel(phy_configuration, &llr->phy_configuration);

	/* Configure the SNW capabilities */
	phy_cap.all = 0;
	phy_cap.start = 1;
	phy_cap.gen3_no_ssc = 1;
	phy_cap.gen2_no_ssc = 1;
	phy_cap.gen1_no_ssc = 1;
	if (ihost->oem_parameters.controller.do_enable_ssc) {
		struct scu_afe_registers __iomem *afe = &ihost->scu_registers->afe;
		struct scu_afe_transceiver __iomem *xcvr = &afe->scu_afe_xcvr[phy_idx];
		struct isci_pci_info *pci_info = to_pci_info(ihost->pdev);
		bool en_sas = false;
		bool en_sata = false;
		u32 sas_type = 0;
		u32 sata_spread = 0x2;
		u32 sas_spread = 0x2;

		phy_cap.gen3_ssc = 1;
		phy_cap.gen2_ssc = 1;
		phy_cap.gen1_ssc = 1;

		if (pci_info->orom->hdr.version < ISCI_ROM_VER_1_1)
			en_sas = en_sata = true;
		else {
			sata_spread = ihost->oem_parameters.controller.ssc_sata_tx_spread_level;
			sas_spread = ihost->oem_parameters.controller.ssc_sas_tx_spread_level;

			if (sata_spread)
				en_sata = true;

			if (sas_spread) {
				en_sas = true;
				sas_type = ihost->oem_parameters.controller.ssc_sas_tx_type;
			}

		}

		if (en_sas) {
			u32 reg;

			reg = readl(&xcvr->afe_xcvr_control0);
			reg |= (0x00100000 | (sas_type << 19));
			writel(reg, &xcvr->afe_xcvr_control0);

			reg = readl(&xcvr->afe_tx_ssc_control);
			reg |= sas_spread << 8;
			writel(reg, &xcvr->afe_tx_ssc_control);
		}

		if (en_sata) {
			u32 reg;

			reg = readl(&xcvr->afe_tx_ssc_control);
			reg |= sata_spread;
			writel(reg, &xcvr->afe_tx_ssc_control);

			reg = readl(&llr->stp_control);
			reg |= 1 << 12;
			writel(reg, &llr->stp_control);
		}
	}

	/* The SAS specification indicates that the phy_capabilities that
	 * are transmitted shall have an even parity.  Calculate the parity.
	 */
	parity_check = phy_cap.all;
	while (parity_check != 0) {
		if (parity_check & 0x1)
			parity_count++;
		parity_check >>= 1;
	}

	/* If parity indicates there are an odd number of bits set, then
	 * set the parity bit to 1 in the phy capabilities.
	 */
	if ((parity_count % 2) != 0)
		phy_cap.parity = 1;

	writel(phy_cap.all, &llr->phy_capabilities);

	/* Set the enable spinup period but disable the ability to send
	 * notify enable spinup
	 */
	writel(SCU_ENSPINUP_GEN_VAL(COUNT,
			phy_user->notify_enable_spin_up_insertion_frequency),
		&llr->notify_enable_spinup_control);

	/* Write the ALIGN Insertion Ferequency for connected phy and
	 * inpendent of connected state
	 */
	clksm_value = SCU_ALIGN_INSERTION_FREQUENCY_GEN_VAL(CONNECTED,
			phy_user->in_connection_align_insertion_frequency);

	clksm_value |= SCU_ALIGN_INSERTION_FREQUENCY_GEN_VAL(GENERAL,
			phy_user->align_insertion_frequency);

	writel(clksm_value, &llr->clock_skew_management);

	if (is_c0(ihost->pdev) || is_c1(ihost->pdev)) {
		writel(0x04210400, &llr->afe_lookup_table_control);
		writel(0x020A7C05, &llr->sas_primitive_timeout);
	} else
		writel(0x02108421, &llr->afe_lookup_table_control);

	llctl = SCU_SAS_LLCTL_GEN_VAL(NO_OUTBOUND_TASK_TIMEOUT,
		(u8)ihost->user_parameters.no_outbound_task_timeout);

	switch (phy_user->max_speed_generation) {
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
	writel(llctl, &llr->link_layer_control);

	sp_timeouts = readl(&llr->sas_phy_timeouts);

	/* Clear the default 0x36 (54us) RATE_CHANGE timeout value. */
	sp_timeouts &= ~SCU_SAS_PHYTOV_GEN_VAL(RATE_CHANGE, 0xFF);

	/* Set RATE_CHANGE timeout value to 0x3B (59us).  This ensures SCU can
	 * lock with 3Gb drive when SCU max rate is set to 1.5Gb.
	 */
	sp_timeouts |= SCU_SAS_PHYTOV_GEN_VAL(RATE_CHANGE, 0x3B);

	writel(sp_timeouts, &llr->sas_phy_timeouts);

	if (is_a2(ihost->pdev)) {
		/* Program the max ARB time for the PHY to 700us so we
		 * inter-operate with the PMC expander which shuts down
		 * PHYs if the expander PHY generates too many breaks.
		 * This time value will guarantee that the initiator PHY
		 * will generate the break.
		 */
		writel(SCIC_SDS_PHY_MAX_ARBITRATION_WAIT_TIME,
		       &llr->maximum_arbitration_wait_timer_timeout);
	}

	/* Disable link layer hang detection, rely on the OS timeout for
	 * I/O timeouts.
	 */
	writel(0, &llr->link_layer_hang_detection_timeout);

	/* We can exit the initial state to the stopped state */
	sci_change_state(&iphy->sm, SCI_PHY_STOPPED);

	return SCI_SUCCESS;
}

static void phy_sata_timeout(struct timer_list *t)
{
	struct sci_timer *tmr = from_timer(tmr, t, timer);
	struct isci_phy *iphy = container_of(tmr, typeof(*iphy), sata_timer);
	struct isci_host *ihost = iphy->owning_port->owning_controller;
	unsigned long flags;

	spin_lock_irqsave(&ihost->scic_lock, flags);

	if (tmr->cancel)
		goto done;

	dev_dbg(sciphy_to_dev(iphy),
		 "%s: SCIC SDS Phy 0x%p did not receive signature fis before "
		 "timeout.\n",
		 __func__,
		 iphy);

	sci_change_state(&iphy->sm, SCI_PHY_STARTING);
done:
	spin_unlock_irqrestore(&ihost->scic_lock, flags);
}

/**
 * This method returns the port currently containing this phy. If the phy is
 *    currently contained by the dummy port, then the phy is considered to not
 *    be part of a port.
 * @sci_phy: This parameter specifies the phy for which to retrieve the
 *    containing port.
 *
 * This method returns a handle to a port that contains the supplied phy.
 * NULL This value is returned if the phy is not part of a real
 * port (i.e. it's contained in the dummy port). !NULL All other
 * values indicate a handle/pointer to the port containing the phy.
 */
struct isci_port *phy_get_non_dummy_port(struct isci_phy *iphy)
{
	struct isci_port *iport = iphy->owning_port;

	if (iport->physical_port_index == SCIC_SDS_DUMMY_PORT)
		return NULL;

	return iphy->owning_port;
}

/**
 * This method will assign a port to the phy object.
 * @out]: iphy This parameter specifies the phy for which to assign a port
 *    object.
 *
 *
 */
void sci_phy_set_port(
	struct isci_phy *iphy,
	struct isci_port *iport)
{
	iphy->owning_port = iport;

	if (iphy->bcn_received_while_port_unassigned) {
		iphy->bcn_received_while_port_unassigned = false;
		sci_port_broadcast_change_received(iphy->owning_port, iphy);
	}
}

enum sci_status sci_phy_initialize(struct isci_phy *iphy,
				   struct scu_transport_layer_registers __iomem *tl,
				   struct scu_link_layer_registers __iomem *ll)
{
	/* Perfrom the initialization of the TL hardware */
	sci_phy_transport_layer_initialization(iphy, tl);

	/* Perofrm the initialization of the PE hardware */
	sci_phy_link_layer_initialization(iphy, ll);

	/* There is nothing that needs to be done in this state just
	 * transition to the stopped state
	 */
	sci_change_state(&iphy->sm, SCI_PHY_STOPPED);

	return SCI_SUCCESS;
}

/**
 * This method assigns the direct attached device ID for this phy.
 *
 * @iphy The phy for which the direct attached device id is to
 *       be assigned.
 * @device_id The direct attached device ID to assign to the phy.
 *       This will either be the RNi for the device or an invalid RNi if there
 *       is no current device assigned to the phy.
 */
void sci_phy_setup_transport(struct isci_phy *iphy, u32 device_id)
{
	u32 tl_control;

	writel(device_id, &iphy->transport_layer_registers->stp_rni);

	/*
	 * The read should guarantee that the first write gets posted
	 * before the next write
	 */
	tl_control = readl(&iphy->transport_layer_registers->control);
	tl_control |= SCU_TLCR_GEN_BIT(CLEAR_TCI_NCQ_MAPPING_TABLE);
	writel(tl_control, &iphy->transport_layer_registers->control);
}

static void sci_phy_suspend(struct isci_phy *iphy)
{
	u32 scu_sas_pcfg_value;

	scu_sas_pcfg_value =
		readl(&iphy->link_layer_registers->phy_configuration);
	scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE);
	writel(scu_sas_pcfg_value,
		&iphy->link_layer_registers->phy_configuration);

	sci_phy_setup_transport(iphy, SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX);
}

void sci_phy_resume(struct isci_phy *iphy)
{
	u32 scu_sas_pcfg_value;

	scu_sas_pcfg_value =
		readl(&iphy->link_layer_registers->phy_configuration);
	scu_sas_pcfg_value &= ~SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE);
	writel(scu_sas_pcfg_value,
		&iphy->link_layer_registers->phy_configuration);
}

void sci_phy_get_sas_address(struct isci_phy *iphy, struct sci_sas_address *sas)
{
	sas->high = readl(&iphy->link_layer_registers->source_sas_address_high);
	sas->low = readl(&iphy->link_layer_registers->source_sas_address_low);
}

void sci_phy_get_attached_sas_address(struct isci_phy *iphy, struct sci_sas_address *sas)
{
	struct sas_identify_frame *iaf;

	iaf = &iphy->frame_rcvd.iaf;
	memcpy(sas, iaf->sas_addr, SAS_ADDR_SIZE);
}

void sci_phy_get_protocols(struct isci_phy *iphy, struct sci_phy_proto *proto)
{
	proto->all = readl(&iphy->link_layer_registers->transmit_identification);
}

enum sci_status sci_phy_start(struct isci_phy *iphy)
{
	enum sci_phy_states state = iphy->sm.current_state_id;

	if (state != SCI_PHY_STOPPED) {
		dev_dbg(sciphy_to_dev(iphy), "%s: in wrong state: %s\n",
			__func__, phy_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}

	sci_change_state(&iphy->sm, SCI_PHY_STARTING);
	return SCI_SUCCESS;
}

enum sci_status sci_phy_stop(struct isci_phy *iphy)
{
	enum sci_phy_states state = iphy->sm.current_state_id;

	switch (state) {
	case SCI_PHY_SUB_INITIAL:
	case SCI_PHY_SUB_AWAIT_OSSP_EN:
	case SCI_PHY_SUB_AWAIT_SAS_SPEED_EN:
	case SCI_PHY_SUB_AWAIT_SAS_POWER:
	case SCI_PHY_SUB_AWAIT_SATA_POWER:
	case SCI_PHY_SUB_AWAIT_SATA_PHY_EN:
	case SCI_PHY_SUB_AWAIT_SATA_SPEED_EN:
	case SCI_PHY_SUB_AWAIT_SIG_FIS_UF:
	case SCI_PHY_SUB_FINAL:
	case SCI_PHY_READY:
		break;
	default:
		dev_dbg(sciphy_to_dev(iphy), "%s: in wrong state: %s\n",
			__func__, phy_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}

	sci_change_state(&iphy->sm, SCI_PHY_STOPPED);
	return SCI_SUCCESS;
}

enum sci_status sci_phy_reset(struct isci_phy *iphy)
{
	enum sci_phy_states state = iphy->sm.current_state_id;

	if (state != SCI_PHY_READY) {
		dev_dbg(sciphy_to_dev(iphy), "%s: in wrong state: %s\n",
			__func__, phy_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}

	sci_change_state(&iphy->sm, SCI_PHY_RESETTING);
	return SCI_SUCCESS;
}

enum sci_status sci_phy_consume_power_handler(struct isci_phy *iphy)
{
	enum sci_phy_states state = iphy->sm.current_state_id;

	switch (state) {
	case SCI_PHY_SUB_AWAIT_SAS_POWER: {
		u32 enable_spinup;

		enable_spinup = readl(&iphy->link_layer_registers->notify_enable_spinup_control);
		enable_spinup |= SCU_ENSPINUP_GEN_BIT(ENABLE);
		writel(enable_spinup, &iphy->link_layer_registers->notify_enable_spinup_control);

		/* Change state to the final state this substate machine has run to completion */
		sci_change_state(&iphy->sm, SCI_PHY_SUB_FINAL);

		return SCI_SUCCESS;
	}
	case SCI_PHY_SUB_AWAIT_SATA_POWER: {
		u32 scu_sas_pcfg_value;

		/* Release the spinup hold state and reset the OOB state machine */
		scu_sas_pcfg_value =
			readl(&iphy->link_layer_registers->phy_configuration);
		scu_sas_pcfg_value &=
			~(SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD) | SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE));
		scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
		writel(scu_sas_pcfg_value,
			&iphy->link_layer_registers->phy_configuration);

		/* Now restart the OOB operation */
		scu_sas_pcfg_value &= ~SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
		scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
		writel(scu_sas_pcfg_value,
			&iphy->link_layer_registers->phy_configuration);

		/* Change state to the final state this substate machine has run to completion */
		sci_change_state(&iphy->sm, SCI_PHY_SUB_AWAIT_SATA_PHY_EN);

		return SCI_SUCCESS;
	}
	default:
		dev_dbg(sciphy_to_dev(iphy), "%s: in wrong state: %s\n",
			__func__, phy_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

static void sci_phy_start_sas_link_training(struct isci_phy *iphy)
{
	/* continue the link training for the phy as if it were a SAS PHY
	 * instead of a SATA PHY. This is done because the completion queue had a SAS
	 * PHY DETECTED event when the state machine was expecting a SATA PHY event.
	 */
	u32 phy_control;

	phy_control = readl(&iphy->link_layer_registers->phy_configuration);
	phy_control |= SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD);
	writel(phy_control,
	       &iphy->link_layer_registers->phy_configuration);

	sci_change_state(&iphy->sm, SCI_PHY_SUB_AWAIT_SAS_SPEED_EN);

	iphy->protocol = SAS_PROTOCOL_SSP;
}

static void sci_phy_start_sata_link_training(struct isci_phy *iphy)
{
	/* This method continues the link training for the phy as if it were a SATA PHY
	 * instead of a SAS PHY.  This is done because the completion queue had a SATA
	 * SPINUP HOLD event when the state machine was expecting a SAS PHY event. none
	 */
	sci_change_state(&iphy->sm, SCI_PHY_SUB_AWAIT_SATA_POWER);

	iphy->protocol = SAS_PROTOCOL_SATA;
}

/**
 * sci_phy_complete_link_training - perform processing common to
 *    all protocols upon completion of link training.
 * @sci_phy: This parameter specifies the phy object for which link training
 *    has completed.
 * @max_link_rate: This parameter specifies the maximum link rate to be
 *    associated with this phy.
 * @next_state: This parameter specifies the next state for the phy's starting
 *    sub-state machine.
 *
 */
static void sci_phy_complete_link_training(struct isci_phy *iphy,
					   enum sas_linkrate max_link_rate,
					   u32 next_state)
{
	iphy->max_negotiated_speed = max_link_rate;

	sci_change_state(&iphy->sm, next_state);
}

static const char *phy_event_name(u32 event_code)
{
	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_PORT_SELECTOR_DETECTED:
		return "port selector";
	case SCU_EVENT_SENT_PORT_SELECTION:
		return "port selection";
	case SCU_EVENT_HARD_RESET_TRANSMITTED:
		return "tx hard reset";
	case SCU_EVENT_HARD_RESET_RECEIVED:
		return "rx hard reset";
	case SCU_EVENT_RECEIVED_IDENTIFY_TIMEOUT:
		return "identify timeout";
	case SCU_EVENT_LINK_FAILURE:
		return "link fail";
	case SCU_EVENT_SATA_SPINUP_HOLD:
		return "sata spinup hold";
	case SCU_EVENT_SAS_15_SSC:
	case SCU_EVENT_SAS_15:
		return "sas 1.5";
	case SCU_EVENT_SAS_30_SSC:
	case SCU_EVENT_SAS_30:
		return "sas 3.0";
	case SCU_EVENT_SAS_60_SSC:
	case SCU_EVENT_SAS_60:
		return "sas 6.0";
	case SCU_EVENT_SATA_15_SSC:
	case SCU_EVENT_SATA_15:
		return "sata 1.5";
	case SCU_EVENT_SATA_30_SSC:
	case SCU_EVENT_SATA_30:
		return "sata 3.0";
	case SCU_EVENT_SATA_60_SSC:
	case SCU_EVENT_SATA_60:
		return "sata 6.0";
	case SCU_EVENT_SAS_PHY_DETECTED:
		return "sas detect";
	case SCU_EVENT_SATA_PHY_DETECTED:
		return "sata detect";
	default:
		return "unknown";
	}
}

#define phy_event_dbg(iphy, state, code) \
	dev_dbg(sciphy_to_dev(iphy), "phy-%d:%d: %s event: %s (%x)\n", \
		phy_to_host(iphy)->id, iphy->phy_index, \
		phy_state_name(state), phy_event_name(code), code)

#define phy_event_warn(iphy, state, code) \
	dev_warn(sciphy_to_dev(iphy), "phy-%d:%d: %s event: %s (%x)\n", \
		phy_to_host(iphy)->id, iphy->phy_index, \
		phy_state_name(state), phy_event_name(code), code)


void scu_link_layer_set_txcomsas_timeout(struct isci_phy *iphy, u32 timeout)
{
	u32 val;

	/* Extend timeout */
	val = readl(&iphy->link_layer_registers->transmit_comsas_signal);
	val &= ~SCU_SAS_LLTXCOMSAS_GEN_VAL(NEGTIME, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_MASK);
	val |= SCU_SAS_LLTXCOMSAS_GEN_VAL(NEGTIME, timeout);

	writel(val, &iphy->link_layer_registers->transmit_comsas_signal);
}

enum sci_status sci_phy_event_handler(struct isci_phy *iphy, u32 event_code)
{
	enum sci_phy_states state = iphy->sm.current_state_id;

	switch (state) {
	case SCI_PHY_SUB_AWAIT_OSSP_EN:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_SAS_PHY_DETECTED:
			sci_phy_start_sas_link_training(iphy);
			iphy->is_in_link_training = true;
			break;
		case SCU_EVENT_SATA_SPINUP_HOLD:
			sci_phy_start_sata_link_training(iphy);
			iphy->is_in_link_training = true;
			break;
		case SCU_EVENT_RECEIVED_IDENTIFY_TIMEOUT:
		       /* Extend timeout value */
		       scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_EXTENDED);

		       /* Start the oob/sn state machine over again */
		       sci_change_state(&iphy->sm, SCI_PHY_STARTING);
		       break;
		default:
			phy_event_dbg(iphy, state, event_code);
			return SCI_FAILURE;
		}
		return SCI_SUCCESS;
	case SCI_PHY_SUB_AWAIT_SAS_SPEED_EN:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_SAS_PHY_DETECTED:
			/*
			 * Why is this being reported again by the controller?
			 * We would re-enter this state so just stay here */
			break;
		case SCU_EVENT_SAS_15:
		case SCU_EVENT_SAS_15_SSC:
			sci_phy_complete_link_training(iphy, SAS_LINK_RATE_1_5_GBPS,
						       SCI_PHY_SUB_AWAIT_IAF_UF);
			break;
		case SCU_EVENT_SAS_30:
		case SCU_EVENT_SAS_30_SSC:
			sci_phy_complete_link_training(iphy, SAS_LINK_RATE_3_0_GBPS,
						       SCI_PHY_SUB_AWAIT_IAF_UF);
			break;
		case SCU_EVENT_SAS_60:
		case SCU_EVENT_SAS_60_SSC:
			sci_phy_complete_link_training(iphy, SAS_LINK_RATE_6_0_GBPS,
						       SCI_PHY_SUB_AWAIT_IAF_UF);
			break;
		case SCU_EVENT_SATA_SPINUP_HOLD:
			/*
			 * We were doing SAS PHY link training and received a SATA PHY event
			 * continue OOB/SN as if this were a SATA PHY */
			sci_phy_start_sata_link_training(iphy);
			break;
		case SCU_EVENT_LINK_FAILURE:
			/* Change the timeout value to default */
			scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_DEFAULT);

			/* Link failure change state back to the starting state */
			sci_change_state(&iphy->sm, SCI_PHY_STARTING);
			break;
		case SCU_EVENT_RECEIVED_IDENTIFY_TIMEOUT:
		       /* Extend the timeout value */
		       scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_EXTENDED);

		       /* Start the oob/sn state machine over again */
		       sci_change_state(&iphy->sm, SCI_PHY_STARTING);
		       break;
		default:
			phy_event_warn(iphy, state, event_code);
			return SCI_FAILURE;
			break;
		}
		return SCI_SUCCESS;
	case SCI_PHY_SUB_AWAIT_IAF_UF:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_SAS_PHY_DETECTED:
			/* Backup the state machine */
			sci_phy_start_sas_link_training(iphy);
			break;
		case SCU_EVENT_SATA_SPINUP_HOLD:
			/* We were doing SAS PHY link training and received a
			 * SATA PHY event continue OOB/SN as if this were a
			 * SATA PHY
			 */
			sci_phy_start_sata_link_training(iphy);
			break;
		case SCU_EVENT_RECEIVED_IDENTIFY_TIMEOUT:
			/* Extend the timeout value */
			scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_EXTENDED);

			/* Start the oob/sn state machine over again */
			sci_change_state(&iphy->sm, SCI_PHY_STARTING);
			break;
		case SCU_EVENT_LINK_FAILURE:
			scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_DEFAULT);
			/* fall through */
		case SCU_EVENT_HARD_RESET_RECEIVED:
			/* Start the oob/sn state machine over again */
			sci_change_state(&iphy->sm, SCI_PHY_STARTING);
			break;
		default:
			phy_event_warn(iphy, state, event_code);
			return SCI_FAILURE;
		}
		return SCI_SUCCESS;
	case SCI_PHY_SUB_AWAIT_SAS_POWER:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_LINK_FAILURE:
			/* Change the timeout value to default */
			scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_DEFAULT);

			/* Link failure change state back to the starting state */
			sci_change_state(&iphy->sm, SCI_PHY_STARTING);
			break;
		default:
			phy_event_warn(iphy, state, event_code);
			return SCI_FAILURE;
		}
		return SCI_SUCCESS;
	case SCI_PHY_SUB_AWAIT_SATA_POWER:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_LINK_FAILURE:
			/* Change the timeout value to default */
			scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_DEFAULT);

			/* Link failure change state back to the starting state */
			sci_change_state(&iphy->sm, SCI_PHY_STARTING);
			break;
		case SCU_EVENT_SATA_SPINUP_HOLD:
			/* These events are received every 10ms and are
			 * expected while in this state
			 */
			break;

		case SCU_EVENT_SAS_PHY_DETECTED:
			/* There has been a change in the phy type before OOB/SN for the
			 * SATA finished start down the SAS link traning path.
			 */
			sci_phy_start_sas_link_training(iphy);
			break;

		default:
			phy_event_warn(iphy, state, event_code);
			return SCI_FAILURE;
		}
		return SCI_SUCCESS;
	case SCI_PHY_SUB_AWAIT_SATA_PHY_EN:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_LINK_FAILURE:
			/* Change the timeout value to default */
			scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_DEFAULT);

			/* Link failure change state back to the starting state */
			sci_change_state(&iphy->sm, SCI_PHY_STARTING);
			break;
		case SCU_EVENT_SATA_SPINUP_HOLD:
			/* These events might be received since we dont know how many may be in
			 * the completion queue while waiting for power
			 */
			break;
		case SCU_EVENT_SATA_PHY_DETECTED:
			iphy->protocol = SAS_PROTOCOL_SATA;

			/* We have received the SATA PHY notification change state */
			sci_change_state(&iphy->sm, SCI_PHY_SUB_AWAIT_SATA_SPEED_EN);
			break;
		case SCU_EVENT_SAS_PHY_DETECTED:
			/* There has been a change in the phy type before OOB/SN for the
			 * SATA finished start down the SAS link traning path.
			 */
			sci_phy_start_sas_link_training(iphy);
			break;
		default:
			phy_event_warn(iphy, state, event_code);
			return SCI_FAILURE;
		}
		return SCI_SUCCESS;
	case SCI_PHY_SUB_AWAIT_SATA_SPEED_EN:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_SATA_PHY_DETECTED:
			/*
			 * The hardware reports multiple SATA PHY detected events
			 * ignore the extras */
			break;
		case SCU_EVENT_SATA_15:
		case SCU_EVENT_SATA_15_SSC:
			sci_phy_complete_link_training(iphy, SAS_LINK_RATE_1_5_GBPS,
						       SCI_PHY_SUB_AWAIT_SIG_FIS_UF);
			break;
		case SCU_EVENT_SATA_30:
		case SCU_EVENT_SATA_30_SSC:
			sci_phy_complete_link_training(iphy, SAS_LINK_RATE_3_0_GBPS,
						       SCI_PHY_SUB_AWAIT_SIG_FIS_UF);
			break;
		case SCU_EVENT_SATA_60:
		case SCU_EVENT_SATA_60_SSC:
			sci_phy_complete_link_training(iphy, SAS_LINK_RATE_6_0_GBPS,
						       SCI_PHY_SUB_AWAIT_SIG_FIS_UF);
			break;
		case SCU_EVENT_LINK_FAILURE:
			/* Change the timeout value to default */
			scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_DEFAULT);

			/* Link failure change state back to the starting state */
			sci_change_state(&iphy->sm, SCI_PHY_STARTING);
			break;
		case SCU_EVENT_SAS_PHY_DETECTED:
			/*
			 * There has been a change in the phy type before OOB/SN for the
			 * SATA finished start down the SAS link traning path. */
			sci_phy_start_sas_link_training(iphy);
			break;
		default:
			phy_event_warn(iphy, state, event_code);
			return SCI_FAILURE;
		}

		return SCI_SUCCESS;
	case SCI_PHY_SUB_AWAIT_SIG_FIS_UF:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_SATA_PHY_DETECTED:
			/* Backup the state machine */
			sci_change_state(&iphy->sm, SCI_PHY_SUB_AWAIT_SATA_SPEED_EN);
			break;

		case SCU_EVENT_LINK_FAILURE:
			/* Change the timeout value to default */
			scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_DEFAULT);

			/* Link failure change state back to the starting state */
			sci_change_state(&iphy->sm, SCI_PHY_STARTING);
			break;

		default:
			phy_event_warn(iphy, state, event_code);
			return SCI_FAILURE;
		}
		return SCI_SUCCESS;
	case SCI_PHY_READY:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_LINK_FAILURE:
			/* Set default timeout */
			scu_link_layer_set_txcomsas_timeout(iphy, SCU_SAS_LINK_LAYER_TXCOMSAS_NEGTIME_DEFAULT);

			/* Link failure change state back to the starting state */
			sci_change_state(&iphy->sm, SCI_PHY_STARTING);
			break;
		case SCU_EVENT_BROADCAST_CHANGE:
		case SCU_EVENT_BROADCAST_SES:
		case SCU_EVENT_BROADCAST_RESERVED0:
		case SCU_EVENT_BROADCAST_RESERVED1:
		case SCU_EVENT_BROADCAST_EXPANDER:
		case SCU_EVENT_BROADCAST_AEN:
			/* Broadcast change received. Notify the port. */
			if (phy_get_non_dummy_port(iphy) != NULL)
				sci_port_broadcast_change_received(iphy->owning_port, iphy);
			else
				iphy->bcn_received_while_port_unassigned = true;
			break;
		case SCU_EVENT_BROADCAST_RESERVED3:
		case SCU_EVENT_BROADCAST_RESERVED4:
		default:
			phy_event_warn(iphy, state, event_code);
			return SCI_FAILURE_INVALID_STATE;
		}
		return SCI_SUCCESS;
	case SCI_PHY_RESETTING:
		switch (scu_get_event_code(event_code)) {
		case SCU_EVENT_HARD_RESET_TRANSMITTED:
			/* Link failure change state back to the starting state */
			sci_change_state(&iphy->sm, SCI_PHY_STARTING);
			break;
		default:
			phy_event_warn(iphy, state, event_code);
			return SCI_FAILURE_INVALID_STATE;
			break;
		}
		return SCI_SUCCESS;
	default:
		dev_dbg(sciphy_to_dev(iphy), "%s: in wrong state: %s\n",
			__func__, phy_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

enum sci_status sci_phy_frame_handler(struct isci_phy *iphy, u32 frame_index)
{
	enum sci_phy_states state = iphy->sm.current_state_id;
	struct isci_host *ihost = iphy->owning_port->owning_controller;
	enum sci_status result;
	unsigned long flags;

	switch (state) {
	case SCI_PHY_SUB_AWAIT_IAF_UF: {
		u32 *frame_words;
		struct sas_identify_frame iaf;

		result = sci_unsolicited_frame_control_get_header(&ihost->uf_control,
								  frame_index,
								  (void **)&frame_words);

		if (result != SCI_SUCCESS)
			return result;

		sci_swab32_cpy(&iaf, frame_words, sizeof(iaf) / sizeof(u32));
		if (iaf.frame_type == 0) {
			u32 state;

			spin_lock_irqsave(&iphy->sas_phy.frame_rcvd_lock, flags);
			memcpy(&iphy->frame_rcvd.iaf, &iaf, sizeof(iaf));
			spin_unlock_irqrestore(&iphy->sas_phy.frame_rcvd_lock, flags);
			if (iaf.smp_tport) {
				/* We got the IAF for an expander PHY go to the final
				 * state since there are no power requirements for
				 * expander phys.
				 */
				state = SCI_PHY_SUB_FINAL;
			} else {
				/* We got the IAF we can now go to the await spinup
				 * semaphore state
				 */
				state = SCI_PHY_SUB_AWAIT_SAS_POWER;
			}
			sci_change_state(&iphy->sm, state);
			result = SCI_SUCCESS;
		} else
			dev_warn(sciphy_to_dev(iphy),
				"%s: PHY starting substate machine received "
				"unexpected frame id %x\n",
				__func__, frame_index);

		sci_controller_release_frame(ihost, frame_index);
		return result;
	}
	case SCI_PHY_SUB_AWAIT_SIG_FIS_UF: {
		struct dev_to_host_fis *frame_header;
		u32 *fis_frame_data;

		result = sci_unsolicited_frame_control_get_header(&ihost->uf_control,
								  frame_index,
								  (void **)&frame_header);

		if (result != SCI_SUCCESS)
			return result;

		if ((frame_header->fis_type == FIS_REGD2H) &&
		    !(frame_header->status & ATA_BUSY)) {
			sci_unsolicited_frame_control_get_buffer(&ihost->uf_control,
								 frame_index,
								 (void **)&fis_frame_data);

			spin_lock_irqsave(&iphy->sas_phy.frame_rcvd_lock, flags);
			sci_controller_copy_sata_response(&iphy->frame_rcvd.fis,
							  frame_header,
							  fis_frame_data);
			spin_unlock_irqrestore(&iphy->sas_phy.frame_rcvd_lock, flags);

			/* got IAF we can now go to the await spinup semaphore state */
			sci_change_state(&iphy->sm, SCI_PHY_SUB_FINAL);

			result = SCI_SUCCESS;
		} else
			dev_warn(sciphy_to_dev(iphy),
				 "%s: PHY starting substate machine received "
				 "unexpected frame id %x\n",
				 __func__, frame_index);

		/* Regardless of the result we are done with this frame with it */
		sci_controller_release_frame(ihost, frame_index);

		return result;
	}
	default:
		dev_dbg(sciphy_to_dev(iphy), "%s: in wrong state: %s\n",
			__func__, phy_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}

}

static void sci_phy_starting_initial_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);

	/* This is just an temporary state go off to the starting state */
	sci_change_state(&iphy->sm, SCI_PHY_SUB_AWAIT_OSSP_EN);
}

static void sci_phy_starting_await_sas_power_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);
	struct isci_host *ihost = iphy->owning_port->owning_controller;

	sci_controller_power_control_queue_insert(ihost, iphy);
}

static void sci_phy_starting_await_sas_power_substate_exit(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);
	struct isci_host *ihost = iphy->owning_port->owning_controller;

	sci_controller_power_control_queue_remove(ihost, iphy);
}

static void sci_phy_starting_await_sata_power_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);
	struct isci_host *ihost = iphy->owning_port->owning_controller;

	sci_controller_power_control_queue_insert(ihost, iphy);
}

static void sci_phy_starting_await_sata_power_substate_exit(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);
	struct isci_host *ihost = iphy->owning_port->owning_controller;

	sci_controller_power_control_queue_remove(ihost, iphy);
}

static void sci_phy_starting_await_sata_phy_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);

	sci_mod_timer(&iphy->sata_timer, SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT);
}

static void sci_phy_starting_await_sata_phy_substate_exit(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);

	sci_del_timer(&iphy->sata_timer);
}

static void sci_phy_starting_await_sata_speed_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);

	sci_mod_timer(&iphy->sata_timer, SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT);
}

static void sci_phy_starting_await_sata_speed_substate_exit(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);

	sci_del_timer(&iphy->sata_timer);
}

static void sci_phy_starting_await_sig_fis_uf_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);

	if (sci_port_link_detected(iphy->owning_port, iphy)) {

		/*
		 * Clear the PE suspend condition so we can actually
		 * receive SIG FIS
		 * The hardware will not respond to the XRDY until the PE
		 * suspend condition is cleared.
		 */
		sci_phy_resume(iphy);

		sci_mod_timer(&iphy->sata_timer,
			      SCIC_SDS_SIGNATURE_FIS_TIMEOUT);
	} else
		iphy->is_in_link_training = false;
}

static void sci_phy_starting_await_sig_fis_uf_substate_exit(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);

	sci_del_timer(&iphy->sata_timer);
}

static void sci_phy_starting_final_substate_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);

	/* State machine has run to completion so exit out and change
	 * the base state machine to the ready state
	 */
	sci_change_state(&iphy->sm, SCI_PHY_READY);
}

/**
 *
 * @sci_phy: This is the struct isci_phy object to stop.
 *
 * This method will stop the struct isci_phy object. This does not reset the
 * protocol engine it just suspends it and places it in a state where it will
 * not cause the end device to power up. none
 */
static void scu_link_layer_stop_protocol_engine(
	struct isci_phy *iphy)
{
	u32 scu_sas_pcfg_value;
	u32 enable_spinup_value;

	/* Suspend the protocol engine and place it in a sata spinup hold state */
	scu_sas_pcfg_value =
		readl(&iphy->link_layer_registers->phy_configuration);
	scu_sas_pcfg_value |=
		(SCU_SAS_PCFG_GEN_BIT(OOB_RESET) |
		 SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE) |
		 SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD));
	writel(scu_sas_pcfg_value,
	       &iphy->link_layer_registers->phy_configuration);

	/* Disable the notify enable spinup primitives */
	enable_spinup_value = readl(&iphy->link_layer_registers->notify_enable_spinup_control);
	enable_spinup_value &= ~SCU_ENSPINUP_GEN_BIT(ENABLE);
	writel(enable_spinup_value, &iphy->link_layer_registers->notify_enable_spinup_control);
}

static void scu_link_layer_start_oob(struct isci_phy *iphy)
{
	struct scu_link_layer_registers __iomem *ll = iphy->link_layer_registers;
	u32 val;

	/** Reset OOB sequence - start */
	val = readl(&ll->phy_configuration);
	val &= ~(SCU_SAS_PCFG_GEN_BIT(OOB_RESET) |
		 SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE) |
		 SCU_SAS_PCFG_GEN_BIT(HARD_RESET));
	writel(val, &ll->phy_configuration);
	readl(&ll->phy_configuration); /* flush */
	/** Reset OOB sequence - end */

	/** Start OOB sequence - start */
	val = readl(&ll->phy_configuration);
	val |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
	writel(val, &ll->phy_configuration);
	readl(&ll->phy_configuration); /* flush */
	/** Start OOB sequence - end */
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
	struct isci_phy *iphy)
{
	u32 phy_configuration_value;

	/*
	 * SAS Phys must wait for the HARD_RESET_TX event notification to transition
	 * to the starting state. */
	phy_configuration_value =
		readl(&iphy->link_layer_registers->phy_configuration);
	phy_configuration_value &= ~(SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE));
	phy_configuration_value |=
		(SCU_SAS_PCFG_GEN_BIT(HARD_RESET) |
		 SCU_SAS_PCFG_GEN_BIT(OOB_RESET));
	writel(phy_configuration_value,
	       &iphy->link_layer_registers->phy_configuration);

	/* Now take the OOB state machine out of reset */
	phy_configuration_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
	phy_configuration_value &= ~SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
	writel(phy_configuration_value,
	       &iphy->link_layer_registers->phy_configuration);
}

static void sci_phy_stopped_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);
	struct isci_port *iport = iphy->owning_port;
	struct isci_host *ihost = iport->owning_controller;

	/*
	 * @todo We need to get to the controller to place this PE in a
	 * reset state
	 */
	sci_del_timer(&iphy->sata_timer);

	scu_link_layer_stop_protocol_engine(iphy);

	if (iphy->sm.previous_state_id != SCI_PHY_INITIAL)
		sci_controller_link_down(ihost, phy_get_non_dummy_port(iphy), iphy);
}

static void sci_phy_starting_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);
	struct isci_port *iport = iphy->owning_port;
	struct isci_host *ihost = iport->owning_controller;

	scu_link_layer_stop_protocol_engine(iphy);
	scu_link_layer_start_oob(iphy);

	/* We don't know what kind of phy we are going to be just yet */
	iphy->protocol = SAS_PROTOCOL_NONE;
	iphy->bcn_received_while_port_unassigned = false;

	if (iphy->sm.previous_state_id == SCI_PHY_READY)
		sci_controller_link_down(ihost, phy_get_non_dummy_port(iphy), iphy);

	sci_change_state(&iphy->sm, SCI_PHY_SUB_INITIAL);
}

static void sci_phy_ready_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);
	struct isci_port *iport = iphy->owning_port;
	struct isci_host *ihost = iport->owning_controller;

	sci_controller_link_up(ihost, phy_get_non_dummy_port(iphy), iphy);
}

static void sci_phy_ready_state_exit(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);

	sci_phy_suspend(iphy);
}

static void sci_phy_resetting_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_phy *iphy = container_of(sm, typeof(*iphy), sm);

	/* The phy is being reset, therefore deactivate it from the port.  In
	 * the resetting state we don't notify the user regarding link up and
	 * link down notifications
	 */
	sci_port_deactivate_phy(iphy->owning_port, iphy, false);

	if (iphy->protocol == SAS_PROTOCOL_SSP) {
		scu_link_layer_tx_hard_reset(iphy);
	} else {
		/* The SCU does not need to have a discrete reset state so
		 * just go back to the starting state.
		 */
		sci_change_state(&iphy->sm, SCI_PHY_STARTING);
	}
}

static const struct sci_base_state sci_phy_state_table[] = {
	[SCI_PHY_INITIAL] = { },
	[SCI_PHY_STOPPED] = {
		.enter_state = sci_phy_stopped_state_enter,
	},
	[SCI_PHY_STARTING] = {
		.enter_state = sci_phy_starting_state_enter,
	},
	[SCI_PHY_SUB_INITIAL] = {
		.enter_state = sci_phy_starting_initial_substate_enter,
	},
	[SCI_PHY_SUB_AWAIT_OSSP_EN] = { },
	[SCI_PHY_SUB_AWAIT_SAS_SPEED_EN] = { },
	[SCI_PHY_SUB_AWAIT_IAF_UF] = { },
	[SCI_PHY_SUB_AWAIT_SAS_POWER] = {
		.enter_state = sci_phy_starting_await_sas_power_substate_enter,
		.exit_state  = sci_phy_starting_await_sas_power_substate_exit,
	},
	[SCI_PHY_SUB_AWAIT_SATA_POWER] = {
		.enter_state = sci_phy_starting_await_sata_power_substate_enter,
		.exit_state  = sci_phy_starting_await_sata_power_substate_exit
	},
	[SCI_PHY_SUB_AWAIT_SATA_PHY_EN] = {
		.enter_state = sci_phy_starting_await_sata_phy_substate_enter,
		.exit_state  = sci_phy_starting_await_sata_phy_substate_exit
	},
	[SCI_PHY_SUB_AWAIT_SATA_SPEED_EN] = {
		.enter_state = sci_phy_starting_await_sata_speed_substate_enter,
		.exit_state  = sci_phy_starting_await_sata_speed_substate_exit
	},
	[SCI_PHY_SUB_AWAIT_SIG_FIS_UF] = {
		.enter_state = sci_phy_starting_await_sig_fis_uf_substate_enter,
		.exit_state  = sci_phy_starting_await_sig_fis_uf_substate_exit
	},
	[SCI_PHY_SUB_FINAL] = {
		.enter_state = sci_phy_starting_final_substate_enter,
	},
	[SCI_PHY_READY] = {
		.enter_state = sci_phy_ready_state_enter,
		.exit_state = sci_phy_ready_state_exit,
	},
	[SCI_PHY_RESETTING] = {
		.enter_state = sci_phy_resetting_state_enter,
	},
	[SCI_PHY_FINAL] = { },
};

void sci_phy_construct(struct isci_phy *iphy,
			    struct isci_port *iport, u8 phy_index)
{
	sci_init_sm(&iphy->sm, sci_phy_state_table, SCI_PHY_INITIAL);

	/* Copy the rest of the input data to our locals */
	iphy->owning_port = iport;
	iphy->phy_index = phy_index;
	iphy->bcn_received_while_port_unassigned = false;
	iphy->protocol = SAS_PROTOCOL_NONE;
	iphy->link_layer_registers = NULL;
	iphy->max_negotiated_speed = SAS_LINK_RATE_UNKNOWN;

	/* Create the SIGNATURE FIS Timeout timer for this phy */
	sci_init_timer(&iphy->sata_timer, phy_sata_timeout);
}

void isci_phy_init(struct isci_phy *iphy, struct isci_host *ihost, int index)
{
	struct sci_oem_params *oem = &ihost->oem_parameters;
	u64 sci_sas_addr;
	__be64 sas_addr;

	sci_sas_addr = oem->phys[index].sas_address.high;
	sci_sas_addr <<= 32;
	sci_sas_addr |= oem->phys[index].sas_address.low;
	sas_addr = cpu_to_be64(sci_sas_addr);
	memcpy(iphy->sas_addr, &sas_addr, sizeof(sas_addr));

	iphy->sas_phy.enabled = 0;
	iphy->sas_phy.id = index;
	iphy->sas_phy.sas_addr = &iphy->sas_addr[0];
	iphy->sas_phy.frame_rcvd = (u8 *)&iphy->frame_rcvd;
	iphy->sas_phy.ha = &ihost->sas_ha;
	iphy->sas_phy.lldd_phy = iphy;
	iphy->sas_phy.enabled = 1;
	iphy->sas_phy.class = SAS;
	iphy->sas_phy.iproto = SAS_PROTOCOL_ALL;
	iphy->sas_phy.tproto = 0;
	iphy->sas_phy.type = PHY_TYPE_PHYSICAL;
	iphy->sas_phy.role = PHY_ROLE_INITIATOR;
	iphy->sas_phy.oob_mode = OOB_NOT_CONNECTED;
	iphy->sas_phy.linkrate = SAS_LINK_RATE_UNKNOWN;
	memset(&iphy->frame_rcvd, 0, sizeof(iphy->frame_rcvd));
}


/**
 * isci_phy_control() - This function is one of the SAS Domain Template
 *    functions. This is a phy management function.
 * @phy: This parameter specifies the sphy being controlled.
 * @func: This parameter specifies the phy control function being invoked.
 * @buf: This parameter is specific to the phy function being invoked.
 *
 * status, zero indicates success.
 */
int isci_phy_control(struct asd_sas_phy *sas_phy,
		     enum phy_func func,
		     void *buf)
{
	int ret = 0;
	struct isci_phy *iphy = sas_phy->lldd_phy;
	struct asd_sas_port *port = sas_phy->port;
	struct isci_host *ihost = sas_phy->ha->lldd_ha;
	unsigned long flags;

	dev_dbg(&ihost->pdev->dev,
		"%s: phy %p; func %d; buf %p; isci phy %p, port %p\n",
		__func__, sas_phy, func, buf, iphy, port);

	switch (func) {
	case PHY_FUNC_DISABLE:
		spin_lock_irqsave(&ihost->scic_lock, flags);
		scu_link_layer_start_oob(iphy);
		sci_phy_stop(iphy);
		spin_unlock_irqrestore(&ihost->scic_lock, flags);
		break;

	case PHY_FUNC_LINK_RESET:
		spin_lock_irqsave(&ihost->scic_lock, flags);
		scu_link_layer_start_oob(iphy);
		sci_phy_stop(iphy);
		sci_phy_start(iphy);
		spin_unlock_irqrestore(&ihost->scic_lock, flags);
		break;

	case PHY_FUNC_HARD_RESET:
		if (!port)
			return -ENODEV;

		ret = isci_port_perform_hard_reset(ihost, port->lldd_port, iphy);

		break;
	case PHY_FUNC_GET_EVENTS: {
		struct scu_link_layer_registers __iomem *r;
		struct sas_phy *phy = sas_phy->phy;

		r = iphy->link_layer_registers;
		phy->running_disparity_error_count = readl(&r->running_disparity_error_count);
		phy->loss_of_dword_sync_count = readl(&r->loss_of_sync_error_count);
		phy->phy_reset_problem_count = readl(&r->phy_reset_problem_count);
		phy->invalid_dword_count = readl(&r->invalid_dword_counter);
		break;
	}

	default:
		dev_dbg(&ihost->pdev->dev,
			   "%s: phy %p; func %d NOT IMPLEMENTED!\n",
			   __func__, sas_phy, func);
		ret = -ENOSYS;
		break;
	}
	return ret;
}
