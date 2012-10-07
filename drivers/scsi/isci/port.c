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
#include "port.h"
#include "request.h"

#define SCIC_SDS_PORT_HARD_RESET_TIMEOUT  (1000)
#define SCU_DUMMY_INDEX    (0xFFFF)

#undef C
#define C(a) (#a)
const char *port_state_name(enum sci_port_states state)
{
	static const char * const strings[] = PORT_STATES;

	return strings[state];
}
#undef C

static struct device *sciport_to_dev(struct isci_port *iport)
{
	int i = iport->physical_port_index;
	struct isci_port *table;
	struct isci_host *ihost;

	if (i == SCIC_SDS_DUMMY_PORT)
		i = SCI_MAX_PORTS+1;

	table = iport - i;
	ihost = container_of(table, typeof(*ihost), ports[0]);

	return &ihost->pdev->dev;
}

static void sci_port_get_protocols(struct isci_port *iport, struct sci_phy_proto *proto)
{
	u8 index;

	proto->all = 0;
	for (index = 0; index < SCI_MAX_PHYS; index++) {
		struct isci_phy *iphy = iport->phy_table[index];

		if (!iphy)
			continue;
		sci_phy_get_protocols(iphy, proto);
	}
}

static u32 sci_port_get_phys(struct isci_port *iport)
{
	u32 index;
	u32 mask;

	mask = 0;
	for (index = 0; index < SCI_MAX_PHYS; index++)
		if (iport->phy_table[index])
			mask |= (1 << index);

	return mask;
}

/**
 * sci_port_get_properties() - This method simply returns the properties
 *    regarding the port, such as: physical index, protocols, sas address, etc.
 * @port: this parameter specifies the port for which to retrieve the physical
 *    index.
 * @properties: This parameter specifies the properties structure into which to
 *    copy the requested information.
 *
 * Indicate if the user specified a valid port. SCI_SUCCESS This value is
 * returned if the specified port was valid. SCI_FAILURE_INVALID_PORT This
 * value is returned if the specified port is not valid.  When this value is
 * returned, no data is copied to the properties output parameter.
 */
enum sci_status sci_port_get_properties(struct isci_port *iport,
						struct sci_port_properties *prop)
{
	if (!iport || iport->logical_port_index == SCIC_SDS_DUMMY_PORT)
		return SCI_FAILURE_INVALID_PORT;

	prop->index = iport->logical_port_index;
	prop->phy_mask = sci_port_get_phys(iport);
	sci_port_get_sas_address(iport, &prop->local.sas_address);
	sci_port_get_protocols(iport, &prop->local.protocols);
	sci_port_get_attached_sas_address(iport, &prop->remote.sas_address);

	return SCI_SUCCESS;
}

static void sci_port_bcn_enable(struct isci_port *iport)
{
	struct isci_phy *iphy;
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(iport->phy_table); i++) {
		iphy = iport->phy_table[i];
		if (!iphy)
			continue;
		val = readl(&iphy->link_layer_registers->link_layer_control);
		/* clear the bit by writing 1. */
		writel(val, &iphy->link_layer_registers->link_layer_control);
	}
}

static void isci_port_bc_change_received(struct isci_host *ihost,
					 struct isci_port *iport,
					 struct isci_phy *iphy)
{
	dev_dbg(&ihost->pdev->dev,
		"%s: isci_phy = %p, sas_phy = %p\n",
		__func__, iphy, &iphy->sas_phy);

	ihost->sas_ha.notify_port_event(&iphy->sas_phy, PORTE_BROADCAST_RCVD);
	sci_port_bcn_enable(iport);
}

static void isci_port_link_up(struct isci_host *isci_host,
			      struct isci_port *iport,
			      struct isci_phy *iphy)
{
	unsigned long flags;
	struct sci_port_properties properties;
	unsigned long success = true;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_port = %p\n",
		__func__, iport);

	spin_lock_irqsave(&iphy->sas_phy.frame_rcvd_lock, flags);

	sci_port_get_properties(iport, &properties);

	if (iphy->protocol == SAS_PROTOCOL_SATA) {
		u64 attached_sas_address;

		iphy->sas_phy.oob_mode = SATA_OOB_MODE;
		iphy->sas_phy.frame_rcvd_size = sizeof(struct dev_to_host_fis);

		/*
		 * For direct-attached SATA devices, the SCI core will
		 * automagically assign a SAS address to the end device
		 * for the purpose of creating a port. This SAS address
		 * will not be the same as assigned to the PHY and needs
		 * to be obtained from struct sci_port_properties properties.
		 */
		attached_sas_address = properties.remote.sas_address.high;
		attached_sas_address <<= 32;
		attached_sas_address |= properties.remote.sas_address.low;
		swab64s(&attached_sas_address);

		memcpy(&iphy->sas_phy.attached_sas_addr,
		       &attached_sas_address, sizeof(attached_sas_address));
	} else if (iphy->protocol == SAS_PROTOCOL_SSP) {
		iphy->sas_phy.oob_mode = SAS_OOB_MODE;
		iphy->sas_phy.frame_rcvd_size = sizeof(struct sas_identify_frame);

		/* Copy the attached SAS address from the IAF */
		memcpy(iphy->sas_phy.attached_sas_addr,
		       iphy->frame_rcvd.iaf.sas_addr, SAS_ADDR_SIZE);
	} else {
		dev_err(&isci_host->pdev->dev, "%s: unknown target\n", __func__);
		success = false;
	}

	iphy->sas_phy.phy->negotiated_linkrate = sci_phy_linkrate(iphy);

	spin_unlock_irqrestore(&iphy->sas_phy.frame_rcvd_lock, flags);

	/* Notify libsas that we have an address frame, if indeed
	 * we've found an SSP, SMP, or STP target */
	if (success)
		isci_host->sas_ha.notify_port_event(&iphy->sas_phy,
						    PORTE_BYTES_DMAED);
}


/**
 * isci_port_link_down() - This function is called by the sci core when a link
 *    becomes inactive.
 * @isci_host: This parameter specifies the isci host object.
 * @phy: This parameter specifies the isci phy with the active link.
 * @port: This parameter specifies the isci port with the active link.
 *
 */
static void isci_port_link_down(struct isci_host *isci_host,
				struct isci_phy *isci_phy,
				struct isci_port *isci_port)
{
	struct isci_remote_device *isci_device;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_port = %p\n", __func__, isci_port);

	if (isci_port) {

		/* check to see if this is the last phy on this port. */
		if (isci_phy->sas_phy.port &&
		    isci_phy->sas_phy.port->num_phys == 1) {
			/* change the state for all devices on this port.  The
			* next task sent to this device will be returned as
			* SAS_TASK_UNDELIVERED, and the scsi mid layer will
			* remove the target
			*/
			list_for_each_entry(isci_device,
					    &isci_port->remote_dev_list,
					    node) {
				dev_dbg(&isci_host->pdev->dev,
					"%s: isci_device = %p\n",
					__func__, isci_device);
				set_bit(IDEV_GONE, &isci_device->flags);
			}
		}
	}

	/* Notify libsas of the borken link, this will trigger calls to our
	 * isci_port_deformed and isci_dev_gone functions.
	 */
	sas_phy_disconnected(&isci_phy->sas_phy);
	isci_host->sas_ha.notify_phy_event(&isci_phy->sas_phy,
					   PHYE_LOSS_OF_SIGNAL);

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_port = %p - Done\n", __func__, isci_port);
}

static bool is_port_ready_state(enum sci_port_states state)
{
	switch (state) {
	case SCI_PORT_READY:
	case SCI_PORT_SUB_WAITING:
	case SCI_PORT_SUB_OPERATIONAL:
	case SCI_PORT_SUB_CONFIGURING:
		return true;
	default:
		return false;
	}
}

/* flag dummy rnc hanling when exiting a ready state */
static void port_state_machine_change(struct isci_port *iport,
				      enum sci_port_states state)
{
	struct sci_base_state_machine *sm = &iport->sm;
	enum sci_port_states old_state = sm->current_state_id;

	if (is_port_ready_state(old_state) && !is_port_ready_state(state))
		iport->ready_exit = true;

	sci_change_state(sm, state);
	iport->ready_exit = false;
}

/**
 * isci_port_hard_reset_complete() - This function is called by the sci core
 *    when the hard reset complete notification has been received.
 * @port: This parameter specifies the sci port with the active link.
 * @completion_status: This parameter specifies the core status for the reset
 *    process.
 *
 */
static void isci_port_hard_reset_complete(struct isci_port *isci_port,
					  enum sci_status completion_status)
{
	struct isci_host *ihost = isci_port->owning_controller;

	dev_dbg(&ihost->pdev->dev,
		"%s: isci_port = %p, completion_status=%x\n",
		     __func__, isci_port, completion_status);

	/* Save the status of the hard reset from the port. */
	isci_port->hard_reset_status = completion_status;

	if (completion_status != SCI_SUCCESS) {

		/* The reset failed.  The port state is now SCI_PORT_FAILED. */
		if (isci_port->active_phy_mask == 0) {
			int phy_idx = isci_port->last_active_phy;
			struct isci_phy *iphy = &ihost->phys[phy_idx];

			/* Generate the link down now to the host, since it
			 * was intercepted by the hard reset state machine when
			 * it really happened.
			 */
			isci_port_link_down(ihost, iphy, isci_port);
		}
		/* Advance the port state so that link state changes will be
		 * noticed.
		 */
		port_state_machine_change(isci_port, SCI_PORT_SUB_WAITING);

	}
	clear_bit(IPORT_RESET_PENDING, &isci_port->state);
	wake_up(&ihost->eventq);

}

/* This method will return a true value if the specified phy can be assigned to
 * this port The following is a list of phys for each port that are allowed: -
 * Port 0 - 3 2 1 0 - Port 1 -     1 - Port 2 - 3 2 - Port 3 - 3 This method
 * doesn't preclude all configurations.  It merely ensures that a phy is part
 * of the allowable set of phy identifiers for that port.  For example, one
 * could assign phy 3 to port 0 and no other phys.  Please refer to
 * sci_port_is_phy_mask_valid() for information regarding whether the
 * phy_mask for a port can be supported. bool true if this is a valid phy
 * assignment for the port false if this is not a valid phy assignment for the
 * port
 */
bool sci_port_is_valid_phy_assignment(struct isci_port *iport, u32 phy_index)
{
	struct isci_host *ihost = iport->owning_controller;
	struct sci_user_parameters *user = &ihost->user_parameters;

	/* Initialize to invalid value. */
	u32 existing_phy_index = SCI_MAX_PHYS;
	u32 index;

	if ((iport->physical_port_index == 1) && (phy_index != 1))
		return false;

	if (iport->physical_port_index == 3 && phy_index != 3)
		return false;

	if (iport->physical_port_index == 2 &&
	    (phy_index == 0 || phy_index == 1))
		return false;

	for (index = 0; index < SCI_MAX_PHYS; index++)
		if (iport->phy_table[index] && index != phy_index)
			existing_phy_index = index;

	/* Ensure that all of the phys in the port are capable of
	 * operating at the same maximum link rate.
	 */
	if (existing_phy_index < SCI_MAX_PHYS &&
	    user->phys[phy_index].max_speed_generation !=
	    user->phys[existing_phy_index].max_speed_generation)
		return false;

	return true;
}

/**
 *
 * @sci_port: This is the port object for which to determine if the phy mask
 *    can be supported.
 *
 * This method will return a true value if the port's phy mask can be supported
 * by the SCU. The following is a list of valid PHY mask configurations for
 * each port: - Port 0 - [[3  2] 1] 0 - Port 1 -        [1] - Port 2 - [[3] 2]
 * - Port 3 -  [3] This method returns a boolean indication specifying if the
 * phy mask can be supported. true if this is a valid phy assignment for the
 * port false if this is not a valid phy assignment for the port
 */
static bool sci_port_is_phy_mask_valid(
	struct isci_port *iport,
	u32 phy_mask)
{
	if (iport->physical_port_index == 0) {
		if (((phy_mask & 0x0F) == 0x0F)
		    || ((phy_mask & 0x03) == 0x03)
		    || ((phy_mask & 0x01) == 0x01)
		    || (phy_mask == 0))
			return true;
	} else if (iport->physical_port_index == 1) {
		if (((phy_mask & 0x02) == 0x02)
		    || (phy_mask == 0))
			return true;
	} else if (iport->physical_port_index == 2) {
		if (((phy_mask & 0x0C) == 0x0C)
		    || ((phy_mask & 0x04) == 0x04)
		    || (phy_mask == 0))
			return true;
	} else if (iport->physical_port_index == 3) {
		if (((phy_mask & 0x08) == 0x08)
		    || (phy_mask == 0))
			return true;
	}

	return false;
}

/*
 * This method retrieves a currently active (i.e. connected) phy contained in
 * the port.  Currently, the lowest order phy that is connected is returned.
 * This method returns a pointer to a SCIS_SDS_PHY object. NULL This value is
 * returned if there are no currently active (i.e. connected to a remote end
 * point) phys contained in the port. All other values specify a struct sci_phy
 * object that is active in the port.
 */
static struct isci_phy *sci_port_get_a_connected_phy(struct isci_port *iport)
{
	u32 index;
	struct isci_phy *iphy;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		/* Ensure that the phy is both part of the port and currently
		 * connected to the remote end-point.
		 */
		iphy = iport->phy_table[index];
		if (iphy && sci_port_active_phy(iport, iphy))
			return iphy;
	}

	return NULL;
}

static enum sci_status sci_port_set_phy(struct isci_port *iport, struct isci_phy *iphy)
{
	/* Check to see if we can add this phy to a port
	 * that means that the phy is not part of a port and that the port does
	 * not already have a phy assinged to the phy index.
	 */
	if (!iport->phy_table[iphy->phy_index] &&
	    !phy_get_non_dummy_port(iphy) &&
	    sci_port_is_valid_phy_assignment(iport, iphy->phy_index)) {
		/* Phy is being added in the stopped state so we are in MPC mode
		 * make logical port index = physical port index
		 */
		iport->logical_port_index = iport->physical_port_index;
		iport->phy_table[iphy->phy_index] = iphy;
		sci_phy_set_port(iphy, iport);

		return SCI_SUCCESS;
	}

	return SCI_FAILURE;
}

static enum sci_status sci_port_clear_phy(struct isci_port *iport, struct isci_phy *iphy)
{
	/* Make sure that this phy is part of this port */
	if (iport->phy_table[iphy->phy_index] == iphy &&
	    phy_get_non_dummy_port(iphy) == iport) {
		struct isci_host *ihost = iport->owning_controller;

		/* Yep it is assigned to this port so remove it */
		sci_phy_set_port(iphy, &ihost->ports[SCI_MAX_PORTS]);
		iport->phy_table[iphy->phy_index] = NULL;
		return SCI_SUCCESS;
	}

	return SCI_FAILURE;
}

void sci_port_get_sas_address(struct isci_port *iport, struct sci_sas_address *sas)
{
	u32 index;

	sas->high = 0;
	sas->low  = 0;
	for (index = 0; index < SCI_MAX_PHYS; index++)
		if (iport->phy_table[index])
			sci_phy_get_sas_address(iport->phy_table[index], sas);
}

void sci_port_get_attached_sas_address(struct isci_port *iport, struct sci_sas_address *sas)
{
	struct isci_phy *iphy;

	/*
	 * Ensure that the phy is both part of the port and currently
	 * connected to the remote end-point.
	 */
	iphy = sci_port_get_a_connected_phy(iport);
	if (iphy) {
		if (iphy->protocol != SAS_PROTOCOL_SATA) {
			sci_phy_get_attached_sas_address(iphy, sas);
		} else {
			sci_phy_get_sas_address(iphy, sas);
			sas->low += iphy->phy_index;
		}
	} else {
		sas->high = 0;
		sas->low  = 0;
	}
}

/**
 * sci_port_construct_dummy_rnc() - create dummy rnc for si workaround
 *
 * @sci_port: logical port on which we need to create the remote node context
 * @rni: remote node index for this remote node context.
 *
 * This routine will construct a dummy remote node context data structure
 * This structure will be posted to the hardware to work around a scheduler
 * error in the hardware.
 */
static void sci_port_construct_dummy_rnc(struct isci_port *iport, u16 rni)
{
	union scu_remote_node_context *rnc;

	rnc = &iport->owning_controller->remote_node_context_table[rni];

	memset(rnc, 0, sizeof(union scu_remote_node_context));

	rnc->ssp.remote_sas_address_hi = 0;
	rnc->ssp.remote_sas_address_lo = 0;

	rnc->ssp.remote_node_index = rni;
	rnc->ssp.remote_node_port_width = 1;
	rnc->ssp.logical_port_index = iport->physical_port_index;

	rnc->ssp.nexus_loss_timer_enable = false;
	rnc->ssp.check_bit = false;
	rnc->ssp.is_valid = true;
	rnc->ssp.is_remote_node_context = true;
	rnc->ssp.function_number = 0;
	rnc->ssp.arbitration_wait_time = 0;
}

/*
 * construct a dummy task context data structure.  This
 * structure will be posted to the hardwre to work around a scheduler error
 * in the hardware.
 */
static void sci_port_construct_dummy_task(struct isci_port *iport, u16 tag)
{
	struct isci_host *ihost = iport->owning_controller;
	struct scu_task_context *task_context;

	task_context = &ihost->task_context_table[ISCI_TAG_TCI(tag)];
	memset(task_context, 0, sizeof(struct scu_task_context));

	task_context->initiator_request = 1;
	task_context->connection_rate = 1;
	task_context->logical_port_index = iport->physical_port_index;
	task_context->protocol_type = SCU_TASK_CONTEXT_PROTOCOL_SSP;
	task_context->task_index = ISCI_TAG_TCI(tag);
	task_context->valid = SCU_TASK_CONTEXT_VALID;
	task_context->context_type = SCU_TASK_CONTEXT_TYPE;
	task_context->remote_node_index = iport->reserved_rni;
	task_context->do_not_dma_ssp_good_response = 1;
	task_context->task_phase = 0x01;
}

static void sci_port_destroy_dummy_resources(struct isci_port *iport)
{
	struct isci_host *ihost = iport->owning_controller;

	if (iport->reserved_tag != SCI_CONTROLLER_INVALID_IO_TAG)
		isci_free_tag(ihost, iport->reserved_tag);

	if (iport->reserved_rni != SCU_DUMMY_INDEX)
		sci_remote_node_table_release_remote_node_index(&ihost->available_remote_nodes,
								     1, iport->reserved_rni);

	iport->reserved_rni = SCU_DUMMY_INDEX;
	iport->reserved_tag = SCI_CONTROLLER_INVALID_IO_TAG;
}

void sci_port_setup_transports(struct isci_port *iport, u32 device_id)
{
	u8 index;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		if (iport->active_phy_mask & (1 << index))
			sci_phy_setup_transport(iport->phy_table[index], device_id);
	}
}

static void sci_port_resume_phy(struct isci_port *iport, struct isci_phy *iphy)
{
	sci_phy_resume(iphy);
	iport->enabled_phy_mask |= 1 << iphy->phy_index;
}

static void sci_port_activate_phy(struct isci_port *iport,
				  struct isci_phy *iphy,
				  u8 flags)
{
	struct isci_host *ihost = iport->owning_controller;

	if (iphy->protocol != SAS_PROTOCOL_SATA && (flags & PF_RESUME))
		sci_phy_resume(iphy);

	iport->active_phy_mask |= 1 << iphy->phy_index;

	sci_controller_clear_invalid_phy(ihost, iphy);

	if (flags & PF_NOTIFY)
		isci_port_link_up(ihost, iport, iphy);
}

void sci_port_deactivate_phy(struct isci_port *iport, struct isci_phy *iphy,
			     bool do_notify_user)
{
	struct isci_host *ihost = iport->owning_controller;

	iport->active_phy_mask &= ~(1 << iphy->phy_index);
	iport->enabled_phy_mask &= ~(1 << iphy->phy_index);
	if (!iport->active_phy_mask)
		iport->last_active_phy = iphy->phy_index;

	iphy->max_negotiated_speed = SAS_LINK_RATE_UNKNOWN;

	/* Re-assign the phy back to the LP as if it were a narrow port for APC
	 * mode. For MPC mode, the phy will remain in the port.
	 */
	if (iport->owning_controller->oem_parameters.controller.mode_type ==
		SCIC_PORT_AUTOMATIC_CONFIGURATION_MODE)
		writel(iphy->phy_index,
			&iport->port_pe_configuration_register[iphy->phy_index]);

	if (do_notify_user == true)
		isci_port_link_down(ihost, iphy, iport);
}

static void sci_port_invalid_link_up(struct isci_port *iport, struct isci_phy *iphy)
{
	struct isci_host *ihost = iport->owning_controller;

	/*
	 * Check to see if we have alreay reported this link as bad and if
	 * not go ahead and tell the SCI_USER that we have discovered an
	 * invalid link.
	 */
	if ((ihost->invalid_phy_mask & (1 << iphy->phy_index)) == 0) {
		ihost->invalid_phy_mask |= 1 << iphy->phy_index;
		dev_warn(&ihost->pdev->dev, "Invalid link up!\n");
	}
}

/**
 * sci_port_general_link_up_handler - phy can be assigned to port?
 * @sci_port: sci_port object for which has a phy that has gone link up.
 * @sci_phy: This is the struct isci_phy object that has gone link up.
 * @flags: PF_RESUME, PF_NOTIFY to sci_port_activate_phy
 *
 * Determine if this phy can be assigned to this port . If the phy is
 * not a valid PHY for this port then the function will notify the user.
 * A PHY can only be part of a port if it's attached SAS ADDRESS is the
 * same as all other PHYs in the same port.
 */
static void sci_port_general_link_up_handler(struct isci_port *iport,
					     struct isci_phy *iphy,
					     u8 flags)
{
	struct sci_sas_address port_sas_address;
	struct sci_sas_address phy_sas_address;

	sci_port_get_attached_sas_address(iport, &port_sas_address);
	sci_phy_get_attached_sas_address(iphy, &phy_sas_address);

	/* If the SAS address of the new phy matches the SAS address of
	 * other phys in the port OR this is the first phy in the port,
	 * then activate the phy and allow it to be used for operations
	 * in this port.
	 */
	if ((phy_sas_address.high == port_sas_address.high &&
	     phy_sas_address.low  == port_sas_address.low) ||
	    iport->active_phy_mask == 0) {
		struct sci_base_state_machine *sm = &iport->sm;

		sci_port_activate_phy(iport, iphy, flags);
		if (sm->current_state_id == SCI_PORT_RESETTING)
			port_state_machine_change(iport, SCI_PORT_READY);
	} else
		sci_port_invalid_link_up(iport, iphy);
}



/**
 * This method returns false if the port only has a single phy object assigned.
 *     If there are no phys or more than one phy then the method will return
 *    true.
 * @sci_port: The port for which the wide port condition is to be checked.
 *
 * bool true Is returned if this is a wide ported port. false Is returned if
 * this is a narrow port.
 */
static bool sci_port_is_wide(struct isci_port *iport)
{
	u32 index;
	u32 phy_count = 0;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		if (iport->phy_table[index] != NULL) {
			phy_count++;
		}
	}

	return phy_count != 1;
}

/**
 * This method is called by the PHY object when the link is detected. if the
 *    port wants the PHY to continue on to the link up state then the port
 *    layer must return true.  If the port object returns false the phy object
 *    must halt its attempt to go link up.
 * @sci_port: The port associated with the phy object.
 * @sci_phy: The phy object that is trying to go link up.
 *
 * true if the phy object can continue to the link up condition. true Is
 * returned if this phy can continue to the ready state. false Is returned if
 * can not continue on to the ready state. This notification is in place for
 * wide ports and direct attached phys.  Since there are no wide ported SATA
 * devices this could become an invalid port configuration.
 */
bool sci_port_link_detected(struct isci_port *iport, struct isci_phy *iphy)
{
	if ((iport->logical_port_index != SCIC_SDS_DUMMY_PORT) &&
	    (iphy->protocol == SAS_PROTOCOL_SATA)) {
		if (sci_port_is_wide(iport)) {
			sci_port_invalid_link_up(iport, iphy);
			return false;
		} else {
			struct isci_host *ihost = iport->owning_controller;
			struct isci_port *dst_port = &(ihost->ports[iphy->phy_index]);
			writel(iphy->phy_index,
			       &dst_port->port_pe_configuration_register[iphy->phy_index]);
		}
	}

	return true;
}

static void port_timeout(unsigned long data)
{
	struct sci_timer *tmr = (struct sci_timer *)data;
	struct isci_port *iport = container_of(tmr, typeof(*iport), timer);
	struct isci_host *ihost = iport->owning_controller;
	unsigned long flags;
	u32 current_state;

	spin_lock_irqsave(&ihost->scic_lock, flags);

	if (tmr->cancel)
		goto done;

	current_state = iport->sm.current_state_id;

	if (current_state == SCI_PORT_RESETTING) {
		/* if the port is still in the resetting state then the timeout
		 * fired before the reset completed.
		 */
		port_state_machine_change(iport, SCI_PORT_FAILED);
	} else if (current_state == SCI_PORT_STOPPED) {
		/* if the port is stopped then the start request failed In this
		 * case stay in the stopped state.
		 */
		dev_err(sciport_to_dev(iport),
			"%s: SCIC Port 0x%p failed to stop before tiemout.\n",
			__func__,
			iport);
	} else if (current_state == SCI_PORT_STOPPING) {
		dev_dbg(sciport_to_dev(iport),
			"%s: port%d: stop complete timeout\n",
			__func__, iport->physical_port_index);
	} else {
		/* The port is in the ready state and we have a timer
		 * reporting a timeout this should not happen.
		 */
		dev_err(sciport_to_dev(iport),
			"%s: SCIC Port 0x%p is processing a timeout operation "
			"in state %d.\n", __func__, iport, current_state);
	}

done:
	spin_unlock_irqrestore(&ihost->scic_lock, flags);
}

/* --------------------------------------------------------------------------- */

/**
 * This function updates the hardwares VIIT entry for this port.
 *
 *
 */
static void sci_port_update_viit_entry(struct isci_port *iport)
{
	struct sci_sas_address sas_address;

	sci_port_get_sas_address(iport, &sas_address);

	writel(sas_address.high,
		&iport->viit_registers->initiator_sas_address_hi);
	writel(sas_address.low,
		&iport->viit_registers->initiator_sas_address_lo);

	/* This value get cleared just in case its not already cleared */
	writel(0, &iport->viit_registers->reserved);

	/* We are required to update the status register last */
	writel(SCU_VIIT_ENTRY_ID_VIIT |
	       SCU_VIIT_IPPT_INITIATOR |
	       ((1 << iport->physical_port_index) << SCU_VIIT_ENTRY_LPVIE_SHIFT) |
	       SCU_VIIT_STATUS_ALL_VALID,
	       &iport->viit_registers->status);
}

enum sas_linkrate sci_port_get_max_allowed_speed(struct isci_port *iport)
{
	u16 index;
	struct isci_phy *iphy;
	enum sas_linkrate max_allowed_speed = SAS_LINK_RATE_6_0_GBPS;

	/*
	 * Loop through all of the phys in this port and find the phy with the
	 * lowest maximum link rate. */
	for (index = 0; index < SCI_MAX_PHYS; index++) {
		iphy = iport->phy_table[index];
		if (iphy && sci_port_active_phy(iport, iphy) &&
		    iphy->max_negotiated_speed < max_allowed_speed)
			max_allowed_speed = iphy->max_negotiated_speed;
	}

	return max_allowed_speed;
}

static void sci_port_suspend_port_task_scheduler(struct isci_port *iport)
{
	u32 pts_control_value;

	pts_control_value = readl(&iport->port_task_scheduler_registers->control);
	pts_control_value |= SCU_PTSxCR_GEN_BIT(SUSPEND);
	writel(pts_control_value, &iport->port_task_scheduler_registers->control);
}

/**
 * sci_port_post_dummy_request() - post dummy/workaround request
 * @sci_port: port to post task
 *
 * Prevent the hardware scheduler from posting new requests to the front
 * of the scheduler queue causing a starvation problem for currently
 * ongoing requests.
 *
 */
static void sci_port_post_dummy_request(struct isci_port *iport)
{
	struct isci_host *ihost = iport->owning_controller;
	u16 tag = iport->reserved_tag;
	struct scu_task_context *tc;
	u32 command;

	tc = &ihost->task_context_table[ISCI_TAG_TCI(tag)];
	tc->abort = 0;

	command = SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC |
		  iport->physical_port_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT |
		  ISCI_TAG_TCI(tag);

	sci_controller_post_request(ihost, command);
}

/**
 * This routine will abort the dummy request.  This will alow the hardware to
 * power down parts of the silicon to save power.
 *
 * @sci_port: The port on which the task must be aborted.
 *
 */
static void sci_port_abort_dummy_request(struct isci_port *iport)
{
	struct isci_host *ihost = iport->owning_controller;
	u16 tag = iport->reserved_tag;
	struct scu_task_context *tc;
	u32 command;

	tc = &ihost->task_context_table[ISCI_TAG_TCI(tag)];
	tc->abort = 1;

	command = SCU_CONTEXT_COMMAND_REQUEST_POST_TC_ABORT |
		  iport->physical_port_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT |
		  ISCI_TAG_TCI(tag);

	sci_controller_post_request(ihost, command);
}

/**
 *
 * @sci_port: This is the struct isci_port object to resume.
 *
 * This method will resume the port task scheduler for this port object. none
 */
static void
sci_port_resume_port_task_scheduler(struct isci_port *iport)
{
	u32 pts_control_value;

	pts_control_value = readl(&iport->port_task_scheduler_registers->control);
	pts_control_value &= ~SCU_PTSxCR_GEN_BIT(SUSPEND);
	writel(pts_control_value, &iport->port_task_scheduler_registers->control);
}

static void sci_port_ready_substate_waiting_enter(struct sci_base_state_machine *sm)
{
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);

	sci_port_suspend_port_task_scheduler(iport);

	iport->not_ready_reason = SCIC_PORT_NOT_READY_NO_ACTIVE_PHYS;

	if (iport->active_phy_mask != 0) {
		/* At least one of the phys on the port is ready */
		port_state_machine_change(iport,
					  SCI_PORT_SUB_OPERATIONAL);
	}
}

static void scic_sds_port_ready_substate_waiting_exit(
					struct sci_base_state_machine *sm)
{
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);
	sci_port_resume_port_task_scheduler(iport);
}

static void sci_port_ready_substate_operational_enter(struct sci_base_state_machine *sm)
{
	u32 index;
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);
	struct isci_host *ihost = iport->owning_controller;

	dev_dbg(&ihost->pdev->dev, "%s: port%d ready\n",
		__func__, iport->physical_port_index);

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		if (iport->phy_table[index]) {
			writel(iport->physical_port_index,
				&iport->port_pe_configuration_register[
					iport->phy_table[index]->phy_index]);
			if (((iport->active_phy_mask^iport->enabled_phy_mask) & (1 << index)) != 0)
				sci_port_resume_phy(iport, iport->phy_table[index]);
		}
	}

	sci_port_update_viit_entry(iport);

	/*
	 * Post the dummy task for the port so the hardware can schedule
	 * io correctly
	 */
	sci_port_post_dummy_request(iport);
}

static void sci_port_invalidate_dummy_remote_node(struct isci_port *iport)
{
	struct isci_host *ihost = iport->owning_controller;
	u8 phys_index = iport->physical_port_index;
	union scu_remote_node_context *rnc;
	u16 rni = iport->reserved_rni;
	u32 command;

	rnc = &ihost->remote_node_context_table[rni];

	rnc->ssp.is_valid = false;

	/* ensure the preceding tc abort request has reached the
	 * controller and give it ample time to act before posting the rnc
	 * invalidate
	 */
	readl(&ihost->smu_registers->interrupt_status); /* flush */
	udelay(10);

	command = SCU_CONTEXT_COMMAND_POST_RNC_INVALIDATE |
		  phys_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT | rni;

	sci_controller_post_request(ihost, command);
}

/**
 *
 * @object: This is the object which is cast to a struct isci_port object.
 *
 * This method will perform the actions required by the struct isci_port on
 * exiting the SCI_PORT_SUB_OPERATIONAL. This function reports
 * the port not ready and suspends the port task scheduler. none
 */
static void sci_port_ready_substate_operational_exit(struct sci_base_state_machine *sm)
{
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);
	struct isci_host *ihost = iport->owning_controller;

	/*
	 * Kill the dummy task for this port if it has not yet posted
	 * the hardware will treat this as a NOP and just return abort
	 * complete.
	 */
	sci_port_abort_dummy_request(iport);

	dev_dbg(&ihost->pdev->dev, "%s: port%d !ready\n",
		__func__, iport->physical_port_index);

	if (iport->ready_exit)
		sci_port_invalidate_dummy_remote_node(iport);
}

static void sci_port_ready_substate_configuring_enter(struct sci_base_state_machine *sm)
{
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);
	struct isci_host *ihost = iport->owning_controller;

	if (iport->active_phy_mask == 0) {
		dev_dbg(&ihost->pdev->dev, "%s: port%d !ready\n",
			__func__, iport->physical_port_index);

		port_state_machine_change(iport, SCI_PORT_SUB_WAITING);
	} else
		port_state_machine_change(iport, SCI_PORT_SUB_OPERATIONAL);
}

enum sci_status sci_port_start(struct isci_port *iport)
{
	struct isci_host *ihost = iport->owning_controller;
	enum sci_status status = SCI_SUCCESS;
	enum sci_port_states state;
	u32 phy_mask;

	state = iport->sm.current_state_id;
	if (state != SCI_PORT_STOPPED) {
		dev_warn(sciport_to_dev(iport), "%s: in wrong state: %s\n",
			 __func__, port_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}

	if (iport->assigned_device_count > 0) {
		/* TODO This is a start failure operation because
		 * there are still devices assigned to this port.
		 * There must be no devices assigned to a port on a
		 * start operation.
		 */
		return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
	}

	if (iport->reserved_rni == SCU_DUMMY_INDEX) {
		u16 rni = sci_remote_node_table_allocate_remote_node(
				&ihost->available_remote_nodes, 1);

		if (rni != SCU_DUMMY_INDEX)
			sci_port_construct_dummy_rnc(iport, rni);
		else
			status = SCI_FAILURE_INSUFFICIENT_RESOURCES;
		iport->reserved_rni = rni;
	}

	if (iport->reserved_tag == SCI_CONTROLLER_INVALID_IO_TAG) {
		u16 tag;

		tag = isci_alloc_tag(ihost);
		if (tag == SCI_CONTROLLER_INVALID_IO_TAG)
			status = SCI_FAILURE_INSUFFICIENT_RESOURCES;
		else
			sci_port_construct_dummy_task(iport, tag);
		iport->reserved_tag = tag;
	}

	if (status == SCI_SUCCESS) {
		phy_mask = sci_port_get_phys(iport);

		/*
		 * There are one or more phys assigned to this port.  Make sure
		 * the port's phy mask is in fact legal and supported by the
		 * silicon.
		 */
		if (sci_port_is_phy_mask_valid(iport, phy_mask) == true) {
			port_state_machine_change(iport,
						  SCI_PORT_READY);

			return SCI_SUCCESS;
		}
		status = SCI_FAILURE;
	}

	if (status != SCI_SUCCESS)
		sci_port_destroy_dummy_resources(iport);

	return status;
}

enum sci_status sci_port_stop(struct isci_port *iport)
{
	enum sci_port_states state;

	state = iport->sm.current_state_id;
	switch (state) {
	case SCI_PORT_STOPPED:
		return SCI_SUCCESS;
	case SCI_PORT_SUB_WAITING:
	case SCI_PORT_SUB_OPERATIONAL:
	case SCI_PORT_SUB_CONFIGURING:
	case SCI_PORT_RESETTING:
		port_state_machine_change(iport,
					  SCI_PORT_STOPPING);
		return SCI_SUCCESS;
	default:
		dev_warn(sciport_to_dev(iport), "%s: in wrong state: %s\n",
			 __func__, port_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

static enum sci_status sci_port_hard_reset(struct isci_port *iport, u32 timeout)
{
	enum sci_status status = SCI_FAILURE_INVALID_PHY;
	struct isci_phy *iphy = NULL;
	enum sci_port_states state;
	u32 phy_index;

	state = iport->sm.current_state_id;
	if (state != SCI_PORT_SUB_OPERATIONAL) {
		dev_warn(sciport_to_dev(iport), "%s: in wrong state: %s\n",
			 __func__, port_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}

	/* Select a phy on which we can send the hard reset request. */
	for (phy_index = 0; phy_index < SCI_MAX_PHYS && !iphy; phy_index++) {
		iphy = iport->phy_table[phy_index];
		if (iphy && !sci_port_active_phy(iport, iphy)) {
			/*
			 * We found a phy but it is not ready select
			 * different phy
			 */
			iphy = NULL;
		}
	}

	/* If we have a phy then go ahead and start the reset procedure */
	if (!iphy)
		return status;
	status = sci_phy_reset(iphy);

	if (status != SCI_SUCCESS)
		return status;

	sci_mod_timer(&iport->timer, timeout);
	iport->not_ready_reason = SCIC_PORT_NOT_READY_HARD_RESET_REQUESTED;

	port_state_machine_change(iport, SCI_PORT_RESETTING);
	return SCI_SUCCESS;
}

/**
 * sci_port_add_phy() -
 * @sci_port: This parameter specifies the port in which the phy will be added.
 * @sci_phy: This parameter is the phy which is to be added to the port.
 *
 * This method will add a PHY to the selected port. This method returns an
 * enum sci_status. SCI_SUCCESS the phy has been added to the port. Any other
 * status is a failure to add the phy to the port.
 */
enum sci_status sci_port_add_phy(struct isci_port *iport,
				      struct isci_phy *iphy)
{
	enum sci_status status;
	enum sci_port_states state;

	sci_port_bcn_enable(iport);

	state = iport->sm.current_state_id;
	switch (state) {
	case SCI_PORT_STOPPED: {
		struct sci_sas_address port_sas_address;

		/* Read the port assigned SAS Address if there is one */
		sci_port_get_sas_address(iport, &port_sas_address);

		if (port_sas_address.high != 0 && port_sas_address.low != 0) {
			struct sci_sas_address phy_sas_address;

			/* Make sure that the PHY SAS Address matches the SAS Address
			 * for this port
			 */
			sci_phy_get_sas_address(iphy, &phy_sas_address);

			if (port_sas_address.high != phy_sas_address.high ||
			    port_sas_address.low  != phy_sas_address.low)
				return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
		}
		return sci_port_set_phy(iport, iphy);
	}
	case SCI_PORT_SUB_WAITING:
	case SCI_PORT_SUB_OPERATIONAL:
		status = sci_port_set_phy(iport, iphy);

		if (status != SCI_SUCCESS)
			return status;

		sci_port_general_link_up_handler(iport, iphy, PF_NOTIFY|PF_RESUME);
		iport->not_ready_reason = SCIC_PORT_NOT_READY_RECONFIGURING;
		port_state_machine_change(iport, SCI_PORT_SUB_CONFIGURING);

		return status;
	case SCI_PORT_SUB_CONFIGURING:
		status = sci_port_set_phy(iport, iphy);

		if (status != SCI_SUCCESS)
			return status;
		sci_port_general_link_up_handler(iport, iphy, PF_NOTIFY);

		/* Re-enter the configuring state since this may be the last phy in
		 * the port.
		 */
		port_state_machine_change(iport,
					  SCI_PORT_SUB_CONFIGURING);
		return SCI_SUCCESS;
	default:
		dev_warn(sciport_to_dev(iport), "%s: in wrong state: %s\n",
			 __func__, port_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

/**
 * sci_port_remove_phy() -
 * @sci_port: This parameter specifies the port in which the phy will be added.
 * @sci_phy: This parameter is the phy which is to be added to the port.
 *
 * This method will remove the PHY from the selected PORT. This method returns
 * an enum sci_status. SCI_SUCCESS the phy has been removed from the port. Any
 * other status is a failure to add the phy to the port.
 */
enum sci_status sci_port_remove_phy(struct isci_port *iport,
					 struct isci_phy *iphy)
{
	enum sci_status status;
	enum sci_port_states state;

	state = iport->sm.current_state_id;

	switch (state) {
	case SCI_PORT_STOPPED:
		return sci_port_clear_phy(iport, iphy);
	case SCI_PORT_SUB_OPERATIONAL:
		status = sci_port_clear_phy(iport, iphy);
		if (status != SCI_SUCCESS)
			return status;

		sci_port_deactivate_phy(iport, iphy, true);
		iport->not_ready_reason = SCIC_PORT_NOT_READY_RECONFIGURING;
		port_state_machine_change(iport,
					  SCI_PORT_SUB_CONFIGURING);
		return SCI_SUCCESS;
	case SCI_PORT_SUB_CONFIGURING:
		status = sci_port_clear_phy(iport, iphy);

		if (status != SCI_SUCCESS)
			return status;
		sci_port_deactivate_phy(iport, iphy, true);

		/* Re-enter the configuring state since this may be the last phy in
		 * the port
		 */
		port_state_machine_change(iport,
					  SCI_PORT_SUB_CONFIGURING);
		return SCI_SUCCESS;
	default:
		dev_warn(sciport_to_dev(iport), "%s: in wrong state: %s\n",
			 __func__, port_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

enum sci_status sci_port_link_up(struct isci_port *iport,
				      struct isci_phy *iphy)
{
	enum sci_port_states state;

	state = iport->sm.current_state_id;
	switch (state) {
	case SCI_PORT_SUB_WAITING:
		/* Since this is the first phy going link up for the port we
		 * can just enable it and continue
		 */
		sci_port_activate_phy(iport, iphy, PF_NOTIFY|PF_RESUME);

		port_state_machine_change(iport,
					  SCI_PORT_SUB_OPERATIONAL);
		return SCI_SUCCESS;
	case SCI_PORT_SUB_OPERATIONAL:
		sci_port_general_link_up_handler(iport, iphy, PF_NOTIFY|PF_RESUME);
		return SCI_SUCCESS;
	case SCI_PORT_RESETTING:
		/* TODO We should  make  sure  that  the phy  that  has gone
		 * link up is the same one on which we sent the reset.  It is
		 * possible that the phy on which we sent  the reset is not the
		 * one that has  gone  link up  and we  want to make sure that
		 * phy being reset  comes  back.  Consider the case where a
		 * reset is sent but before the hardware processes the reset it
		 * get a link up on  the  port because of a hot plug event.
		 * because  of  the reset request this phy will go link down
		 * almost immediately.
		 */

		/* In the resetting state we don't notify the user regarding
		 * link up and link down notifications.
		 */
		sci_port_general_link_up_handler(iport, iphy, PF_RESUME);
		return SCI_SUCCESS;
	default:
		dev_warn(sciport_to_dev(iport), "%s: in wrong state: %s\n",
			 __func__, port_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

enum sci_status sci_port_link_down(struct isci_port *iport,
					struct isci_phy *iphy)
{
	enum sci_port_states state;

	state = iport->sm.current_state_id;
	switch (state) {
	case SCI_PORT_SUB_OPERATIONAL:
		sci_port_deactivate_phy(iport, iphy, true);

		/* If there are no active phys left in the port, then
		 * transition the port to the WAITING state until such time
		 * as a phy goes link up
		 */
		if (iport->active_phy_mask == 0)
			port_state_machine_change(iport,
						  SCI_PORT_SUB_WAITING);
		return SCI_SUCCESS;
	case SCI_PORT_RESETTING:
		/* In the resetting state we don't notify the user regarding
		 * link up and link down notifications. */
		sci_port_deactivate_phy(iport, iphy, false);
		return SCI_SUCCESS;
	default:
		dev_warn(sciport_to_dev(iport), "%s: in wrong state: %s\n",
			 __func__, port_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

enum sci_status sci_port_start_io(struct isci_port *iport,
				  struct isci_remote_device *idev,
				  struct isci_request *ireq)
{
	enum sci_port_states state;

	state = iport->sm.current_state_id;
	switch (state) {
	case SCI_PORT_SUB_WAITING:
		return SCI_FAILURE_INVALID_STATE;
	case SCI_PORT_SUB_OPERATIONAL:
		iport->started_request_count++;
		return SCI_SUCCESS;
	default:
		dev_warn(sciport_to_dev(iport), "%s: in wrong state: %s\n",
			 __func__, port_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	}
}

enum sci_status sci_port_complete_io(struct isci_port *iport,
				     struct isci_remote_device *idev,
				     struct isci_request *ireq)
{
	enum sci_port_states state;

	state = iport->sm.current_state_id;
	switch (state) {
	case SCI_PORT_STOPPED:
		dev_warn(sciport_to_dev(iport), "%s: in wrong state: %s\n",
			 __func__, port_state_name(state));
		return SCI_FAILURE_INVALID_STATE;
	case SCI_PORT_STOPPING:
		sci_port_decrement_request_count(iport);

		if (iport->started_request_count == 0)
			port_state_machine_change(iport,
						  SCI_PORT_STOPPED);
		break;
	case SCI_PORT_READY:
	case SCI_PORT_RESETTING:
	case SCI_PORT_FAILED:
	case SCI_PORT_SUB_WAITING:
	case SCI_PORT_SUB_OPERATIONAL:
		sci_port_decrement_request_count(iport);
		break;
	case SCI_PORT_SUB_CONFIGURING:
		sci_port_decrement_request_count(iport);
		if (iport->started_request_count == 0) {
			port_state_machine_change(iport,
						  SCI_PORT_SUB_OPERATIONAL);
		}
		break;
	}
	return SCI_SUCCESS;
}

static void sci_port_enable_port_task_scheduler(struct isci_port *iport)
{
	u32 pts_control_value;

	 /* enable the port task scheduler in a suspended state */
	pts_control_value = readl(&iport->port_task_scheduler_registers->control);
	pts_control_value |= SCU_PTSxCR_GEN_BIT(ENABLE) | SCU_PTSxCR_GEN_BIT(SUSPEND);
	writel(pts_control_value, &iport->port_task_scheduler_registers->control);
}

static void sci_port_disable_port_task_scheduler(struct isci_port *iport)
{
	u32 pts_control_value;

	pts_control_value = readl(&iport->port_task_scheduler_registers->control);
	pts_control_value &=
		~(SCU_PTSxCR_GEN_BIT(ENABLE) | SCU_PTSxCR_GEN_BIT(SUSPEND));
	writel(pts_control_value, &iport->port_task_scheduler_registers->control);
}

static void sci_port_post_dummy_remote_node(struct isci_port *iport)
{
	struct isci_host *ihost = iport->owning_controller;
	u8 phys_index = iport->physical_port_index;
	union scu_remote_node_context *rnc;
	u16 rni = iport->reserved_rni;
	u32 command;

	rnc = &ihost->remote_node_context_table[rni];
	rnc->ssp.is_valid = true;

	command = SCU_CONTEXT_COMMAND_POST_RNC_32 |
		  phys_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT | rni;

	sci_controller_post_request(ihost, command);

	/* ensure hardware has seen the post rnc command and give it
	 * ample time to act before sending the suspend
	 */
	readl(&ihost->smu_registers->interrupt_status); /* flush */
	udelay(10);

	command = SCU_CONTEXT_COMMAND_POST_RNC_SUSPEND_TX_RX |
		  phys_index << SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT | rni;

	sci_controller_post_request(ihost, command);
}

static void sci_port_stopped_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);

	if (iport->sm.previous_state_id == SCI_PORT_STOPPING) {
		/*
		 * If we enter this state becasuse of a request to stop
		 * the port then we want to disable the hardwares port
		 * task scheduler. */
		sci_port_disable_port_task_scheduler(iport);
	}
}

static void sci_port_stopped_state_exit(struct sci_base_state_machine *sm)
{
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);

	/* Enable and suspend the port task scheduler */
	sci_port_enable_port_task_scheduler(iport);
}

static void sci_port_ready_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);
	struct isci_host *ihost = iport->owning_controller;
	u32 prev_state;

	prev_state = iport->sm.previous_state_id;
	if (prev_state  == SCI_PORT_RESETTING)
		isci_port_hard_reset_complete(iport, SCI_SUCCESS);
	else
		dev_dbg(&ihost->pdev->dev, "%s: port%d !ready\n",
			__func__, iport->physical_port_index);

	/* Post and suspend the dummy remote node context for this port. */
	sci_port_post_dummy_remote_node(iport);

	/* Start the ready substate machine */
	port_state_machine_change(iport,
				  SCI_PORT_SUB_WAITING);
}

static void sci_port_resetting_state_exit(struct sci_base_state_machine *sm)
{
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);

	sci_del_timer(&iport->timer);
}

static void sci_port_stopping_state_exit(struct sci_base_state_machine *sm)
{
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);

	sci_del_timer(&iport->timer);

	sci_port_destroy_dummy_resources(iport);
}

static void sci_port_failed_state_enter(struct sci_base_state_machine *sm)
{
	struct isci_port *iport = container_of(sm, typeof(*iport), sm);

	isci_port_hard_reset_complete(iport, SCI_FAILURE_TIMEOUT);
}

void sci_port_set_hang_detection_timeout(struct isci_port *iport, u32 timeout)
{
	int phy_index;
	u32 phy_mask = iport->active_phy_mask;

	if (timeout)
		++iport->hang_detect_users;
	else if (iport->hang_detect_users > 1)
		--iport->hang_detect_users;
	else
		iport->hang_detect_users = 0;

	if (timeout || (iport->hang_detect_users == 0)) {
		for (phy_index = 0; phy_index < SCI_MAX_PHYS; phy_index++) {
			if ((phy_mask >> phy_index) & 1) {
				writel(timeout,
				       &iport->phy_table[phy_index]
					  ->link_layer_registers
					  ->link_layer_hang_detection_timeout);
			}
		}
	}
}
/* --------------------------------------------------------------------------- */

static const struct sci_base_state sci_port_state_table[] = {
	[SCI_PORT_STOPPED] = {
		.enter_state = sci_port_stopped_state_enter,
		.exit_state  = sci_port_stopped_state_exit
	},
	[SCI_PORT_STOPPING] = {
		.exit_state  = sci_port_stopping_state_exit
	},
	[SCI_PORT_READY] = {
		.enter_state = sci_port_ready_state_enter,
	},
	[SCI_PORT_SUB_WAITING] = {
		.enter_state = sci_port_ready_substate_waiting_enter,
		.exit_state  = scic_sds_port_ready_substate_waiting_exit,
	},
	[SCI_PORT_SUB_OPERATIONAL] = {
		.enter_state = sci_port_ready_substate_operational_enter,
		.exit_state  = sci_port_ready_substate_operational_exit
	},
	[SCI_PORT_SUB_CONFIGURING] = {
		.enter_state = sci_port_ready_substate_configuring_enter
	},
	[SCI_PORT_RESETTING] = {
		.exit_state  = sci_port_resetting_state_exit
	},
	[SCI_PORT_FAILED] = {
		.enter_state = sci_port_failed_state_enter,
	}
};

void sci_port_construct(struct isci_port *iport, u8 index,
			     struct isci_host *ihost)
{
	sci_init_sm(&iport->sm, sci_port_state_table, SCI_PORT_STOPPED);

	iport->logical_port_index  = SCIC_SDS_DUMMY_PORT;
	iport->physical_port_index = index;
	iport->active_phy_mask     = 0;
	iport->enabled_phy_mask    = 0;
	iport->last_active_phy     = 0;
	iport->ready_exit	   = false;

	iport->owning_controller = ihost;

	iport->started_request_count = 0;
	iport->assigned_device_count = 0;
	iport->hang_detect_users = 0;

	iport->reserved_rni = SCU_DUMMY_INDEX;
	iport->reserved_tag = SCI_CONTROLLER_INVALID_IO_TAG;

	sci_init_timer(&iport->timer, port_timeout);

	iport->port_task_scheduler_registers = NULL;

	for (index = 0; index < SCI_MAX_PHYS; index++)
		iport->phy_table[index] = NULL;
}

void sci_port_broadcast_change_received(struct isci_port *iport, struct isci_phy *iphy)
{
	struct isci_host *ihost = iport->owning_controller;

	/* notify the user. */
	isci_port_bc_change_received(ihost, iport, iphy);
}

static void wait_port_reset(struct isci_host *ihost, struct isci_port *iport)
{
	wait_event(ihost->eventq, !test_bit(IPORT_RESET_PENDING, &iport->state));
}

int isci_port_perform_hard_reset(struct isci_host *ihost, struct isci_port *iport,
				 struct isci_phy *iphy)
{
	unsigned long flags;
	enum sci_status status;
	int ret = TMF_RESP_FUNC_COMPLETE;

	dev_dbg(&ihost->pdev->dev, "%s: iport = %p\n",
		__func__, iport);

	spin_lock_irqsave(&ihost->scic_lock, flags);
	set_bit(IPORT_RESET_PENDING, &iport->state);

	#define ISCI_PORT_RESET_TIMEOUT SCIC_SDS_SIGNATURE_FIS_TIMEOUT
	status = sci_port_hard_reset(iport, ISCI_PORT_RESET_TIMEOUT);

	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	if (status == SCI_SUCCESS) {
		wait_port_reset(ihost, iport);

		dev_dbg(&ihost->pdev->dev,
			"%s: iport = %p; hard reset completion\n",
			__func__, iport);

		if (iport->hard_reset_status != SCI_SUCCESS) {
			ret = TMF_RESP_FUNC_FAILED;

			dev_err(&ihost->pdev->dev,
				"%s: iport = %p; hard reset failed (0x%x)\n",
				__func__, iport, iport->hard_reset_status);
		}
	} else {
		clear_bit(IPORT_RESET_PENDING, &iport->state);
		wake_up(&ihost->eventq);
		ret = TMF_RESP_FUNC_FAILED;

		dev_err(&ihost->pdev->dev,
			"%s: iport = %p; sci_port_hard_reset call"
			" failed 0x%x\n",
			__func__, iport, status);

	}
	return ret;
}

int isci_ata_check_ready(struct domain_device *dev)
{
	struct isci_port *iport = dev->port->lldd_port;
	struct isci_host *ihost = dev_to_ihost(dev);
	struct isci_remote_device *idev;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&ihost->scic_lock, flags);
	idev = isci_lookup_device(dev);
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	if (!idev)
		goto out;

	if (test_bit(IPORT_RESET_PENDING, &iport->state))
		goto out;

	rc = !!iport->active_phy_mask;
 out:
	isci_put_device(idev);

	return rc;
}

void isci_port_deformed(struct asd_sas_phy *phy)
{
	struct isci_host *ihost = phy->ha->lldd_ha;
	struct isci_port *iport = phy->port->lldd_port;
	unsigned long flags;
	int i;

	/* we got a port notification on a port that was subsequently
	 * torn down and libsas is just now catching up
	 */
	if (!iport)
		return;

	spin_lock_irqsave(&ihost->scic_lock, flags);
	for (i = 0; i < SCI_MAX_PHYS; i++) {
		if (iport->active_phy_mask & 1 << i)
			break;
	}
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	if (i >= SCI_MAX_PHYS)
		dev_dbg(&ihost->pdev->dev, "%s: port: %ld\n",
			__func__, (long) (iport - &ihost->ports[0]));
}

void isci_port_formed(struct asd_sas_phy *phy)
{
	struct isci_host *ihost = phy->ha->lldd_ha;
	struct isci_phy *iphy = to_iphy(phy);
	struct asd_sas_port *port = phy->port;
	struct isci_port *iport = NULL;
	unsigned long flags;
	int i;

	/* initial ports are formed as the driver is still initializing,
	 * wait for that process to complete
	 */
	wait_for_start(ihost);

	spin_lock_irqsave(&ihost->scic_lock, flags);
	for (i = 0; i < SCI_MAX_PORTS; i++) {
		iport = &ihost->ports[i];
		if (iport->active_phy_mask & 1 << iphy->phy_index)
			break;
	}
	spin_unlock_irqrestore(&ihost->scic_lock, flags);

	if (i >= SCI_MAX_PORTS)
		iport = NULL;

	port->lldd_port = iport;
}
