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

#include "host.h"

#define SCIC_SDS_MPC_RECONFIGURATION_TIMEOUT    (10)
#define SCIC_SDS_APC_RECONFIGURATION_TIMEOUT    (10)
#define SCIC_SDS_APC_WAIT_LINK_UP_NOTIFICATION  (100)

enum SCIC_SDS_APC_ACTIVITY {
	SCIC_SDS_APC_SKIP_PHY,
	SCIC_SDS_APC_ADD_PHY,
	SCIC_SDS_APC_START_TIMER,

	SCIC_SDS_APC_ACTIVITY_MAX
};

/*
 * ******************************************************************************
 * General port configuration agent routines
 * ****************************************************************************** */

/**
 *
 * @address_one: A SAS Address to be compared.
 * @address_two: A SAS Address to be compared.
 *
 * Compare the two SAS Address and if SAS Address One is greater than SAS
 * Address Two then return > 0 else if SAS Address One is less than SAS Address
 * Two return < 0 Otherwise they are the same return 0 A signed value of x > 0
 * > y where x is returned for Address One > Address Two y is returned for
 * Address One < Address Two 0 is returned ofr Address One = Address Two
 */
static s32 sci_sas_address_compare(
	struct sci_sas_address address_one,
	struct sci_sas_address address_two)
{
	if (address_one.high > address_two.high) {
		return 1;
	} else if (address_one.high < address_two.high) {
		return -1;
	} else if (address_one.low > address_two.low) {
		return 1;
	} else if (address_one.low < address_two.low) {
		return -1;
	}

	/* The two SAS Address must be identical */
	return 0;
}

/**
 *
 * @controller: The controller object used for the port search.
 * @phy: The phy object to match.
 *
 * This routine will find a matching port for the phy.  This means that the
 * port and phy both have the same broadcast sas address and same received sas
 * address. The port address or the NULL if there is no matching
 * port. port address if the port can be found to match the phy.
 * NULL if there is no matching port for the phy.
 */
static struct isci_port *sci_port_configuration_agent_find_port(
	struct isci_host *ihost,
	struct isci_phy *iphy)
{
	u8 i;
	struct sci_sas_address port_sas_address;
	struct sci_sas_address port_attached_device_address;
	struct sci_sas_address phy_sas_address;
	struct sci_sas_address phy_attached_device_address;

	/*
	 * Since this phy can be a member of a wide port check to see if one or
	 * more phys match the sent and received SAS address as this phy in which
	 * case it should participate in the same port.
	 */
	sci_phy_get_sas_address(iphy, &phy_sas_address);
	sci_phy_get_attached_sas_address(iphy, &phy_attached_device_address);

	for (i = 0; i < ihost->logical_port_entries; i++) {
		struct isci_port *iport = &ihost->ports[i];

		sci_port_get_sas_address(iport, &port_sas_address);
		sci_port_get_attached_sas_address(iport, &port_attached_device_address);

		if (sci_sas_address_compare(port_sas_address, phy_sas_address) == 0 &&
		    sci_sas_address_compare(port_attached_device_address, phy_attached_device_address) == 0)
			return iport;
	}

	return NULL;
}

/**
 *
 * @controller: This is the controller object that contains the port agent
 * @port_agent: This is the port configruation agent for the controller.
 *
 * This routine will validate the port configuration is correct for the SCU
 * hardware.  The SCU hardware allows for port configurations as follows. LP0
 * -> (PE0), (PE0, PE1), (PE0, PE1, PE2, PE3) LP1 -> (PE1) LP2 -> (PE2), (PE2,
 * PE3) LP3 -> (PE3) enum sci_status SCI_SUCCESS the port configuration is valid for
 * this port configuration agent. SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION
 * the port configuration is not valid for this port configuration agent.
 */
static enum sci_status sci_port_configuration_agent_validate_ports(
	struct isci_host *ihost,
	struct sci_port_configuration_agent *port_agent)
{
	struct sci_sas_address first_address;
	struct sci_sas_address second_address;

	/*
	 * Sanity check the max ranges for all the phys the max index
	 * is always equal to the port range index */
	if (port_agent->phy_valid_port_range[0].max_index != 0 ||
	    port_agent->phy_valid_port_range[1].max_index != 1 ||
	    port_agent->phy_valid_port_range[2].max_index != 2 ||
	    port_agent->phy_valid_port_range[3].max_index != 3)
		return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;

	/*
	 * This is a request to configure a single x4 port or at least attempt
	 * to make all the phys into a single port */
	if (port_agent->phy_valid_port_range[0].min_index == 0 &&
	    port_agent->phy_valid_port_range[1].min_index == 0 &&
	    port_agent->phy_valid_port_range[2].min_index == 0 &&
	    port_agent->phy_valid_port_range[3].min_index == 0)
		return SCI_SUCCESS;

	/*
	 * This is a degenerate case where phy 1 and phy 2 are assigned
	 * to the same port this is explicitly disallowed by the hardware
	 * unless they are part of the same x4 port and this condition was
	 * already checked above. */
	if (port_agent->phy_valid_port_range[2].min_index == 1) {
		return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
	}

	/*
	 * PE0 and PE3 can never have the same SAS Address unless they
	 * are part of the same x4 wide port and we have already checked
	 * for this condition. */
	sci_phy_get_sas_address(&ihost->phys[0], &first_address);
	sci_phy_get_sas_address(&ihost->phys[3], &second_address);

	if (sci_sas_address_compare(first_address, second_address) == 0) {
		return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
	}

	/*
	 * PE0 and PE1 are configured into a 2x1 ports make sure that the
	 * SAS Address for PE0 and PE2 are different since they can not be
	 * part of the same port. */
	if (port_agent->phy_valid_port_range[0].min_index == 0 &&
	    port_agent->phy_valid_port_range[1].min_index == 1) {
		sci_phy_get_sas_address(&ihost->phys[0], &first_address);
		sci_phy_get_sas_address(&ihost->phys[2], &second_address);

		if (sci_sas_address_compare(first_address, second_address) == 0) {
			return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
		}
	}

	/*
	 * PE2 and PE3 are configured into a 2x1 ports make sure that the
	 * SAS Address for PE1 and PE3 are different since they can not be
	 * part of the same port. */
	if (port_agent->phy_valid_port_range[2].min_index == 2 &&
	    port_agent->phy_valid_port_range[3].min_index == 3) {
		sci_phy_get_sas_address(&ihost->phys[1], &first_address);
		sci_phy_get_sas_address(&ihost->phys[3], &second_address);

		if (sci_sas_address_compare(first_address, second_address) == 0) {
			return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
		}
	}

	return SCI_SUCCESS;
}

/*
 * ******************************************************************************
 * Manual port configuration agent routines
 * ****************************************************************************** */

/* verify all of the phys in the same port are using the same SAS address */
static enum sci_status
sci_mpc_agent_validate_phy_configuration(struct isci_host *ihost,
					      struct sci_port_configuration_agent *port_agent)
{
	u32 phy_mask;
	u32 assigned_phy_mask;
	struct sci_sas_address sas_address;
	struct sci_sas_address phy_assigned_address;
	u8 port_index;
	u8 phy_index;

	assigned_phy_mask = 0;
	sas_address.high = 0;
	sas_address.low = 0;

	for (port_index = 0; port_index < SCI_MAX_PORTS; port_index++) {
		phy_mask = ihost->oem_parameters.ports[port_index].phy_mask;

		if (!phy_mask)
			continue;
		/*
		 * Make sure that one or more of the phys were not already assinged to
		 * a different port. */
		if ((phy_mask & ~assigned_phy_mask) == 0) {
			return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
		}

		/* Find the starting phy index for this round through the loop */
		for (phy_index = 0; phy_index < SCI_MAX_PHYS; phy_index++) {
			if ((phy_mask & (1 << phy_index)) == 0)
				continue;
			sci_phy_get_sas_address(&ihost->phys[phy_index],
						     &sas_address);

			/*
			 * The phy_index can be used as the starting point for the
			 * port range since the hardware starts all logical ports
			 * the same as the PE index. */
			port_agent->phy_valid_port_range[phy_index].min_index = port_index;
			port_agent->phy_valid_port_range[phy_index].max_index = phy_index;

			if (phy_index != port_index) {
				return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
			}

			break;
		}

		/*
		 * See how many additional phys are being added to this logical port.
		 * Note: We have not moved the current phy_index so we will actually
		 *       compare the startting phy with itself.
		 *       This is expected and required to add the phy to the port. */
		while (phy_index < SCI_MAX_PHYS) {
			if ((phy_mask & (1 << phy_index)) == 0)
				continue;
			sci_phy_get_sas_address(&ihost->phys[phy_index],
						     &phy_assigned_address);

			if (sci_sas_address_compare(sas_address, phy_assigned_address) != 0) {
				/*
				 * The phy mask specified that this phy is part of the same port
				 * as the starting phy and it is not so fail this configuration */
				return SCI_FAILURE_UNSUPPORTED_PORT_CONFIGURATION;
			}

			port_agent->phy_valid_port_range[phy_index].min_index = port_index;
			port_agent->phy_valid_port_range[phy_index].max_index = phy_index;

			sci_port_add_phy(&ihost->ports[port_index],
					      &ihost->phys[phy_index]);

			assigned_phy_mask |= (1 << phy_index);
		}

		phy_index++;
	}

	return sci_port_configuration_agent_validate_ports(ihost, port_agent);
}

static void mpc_agent_timeout(unsigned long data)
{
	u8 index;
	struct sci_timer *tmr = (struct sci_timer *)data;
	struct sci_port_configuration_agent *port_agent;
	struct isci_host *ihost;
	unsigned long flags;
	u16 configure_phy_mask;

	port_agent = container_of(tmr, typeof(*port_agent), timer);
	ihost = container_of(port_agent, typeof(*ihost), port_agent);

	spin_lock_irqsave(&ihost->scic_lock, flags);

	if (tmr->cancel)
		goto done;

	port_agent->timer_pending = false;

	/* Find the mask of phys that are reported read but as yet unconfigured into a port */
	configure_phy_mask = ~port_agent->phy_configured_mask & port_agent->phy_ready_mask;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		struct isci_phy *iphy = &ihost->phys[index];

		if (configure_phy_mask & (1 << index)) {
			port_agent->link_up_handler(ihost, port_agent,
						    phy_get_non_dummy_port(iphy),
						    iphy);
		}
	}

done:
	spin_unlock_irqrestore(&ihost->scic_lock, flags);
}

static void sci_mpc_agent_link_up(struct isci_host *ihost,
				       struct sci_port_configuration_agent *port_agent,
				       struct isci_port *iport,
				       struct isci_phy *iphy)
{
	/* If the port is NULL then the phy was not assigned to a port.
	 * This is because the phy was not given the same SAS Address as
	 * the other PHYs in the port.
	 */
	if (!iport)
		return;

	port_agent->phy_ready_mask |= (1 << iphy->phy_index);
	sci_port_link_up(iport, iphy);
	if ((iport->active_phy_mask & (1 << iphy->phy_index)))
		port_agent->phy_configured_mask |= (1 << iphy->phy_index);
}

/**
 *
 * @controller: This is the controller object that receives the link down
 *    notification.
 * @port: This is the port object associated with the phy.  If the is no
 *    associated port this is an NULL.  The port is an invalid
 *    handle only if the phy was never port of this port.  This happens when
 *    the phy is not broadcasting the same SAS address as the other phys in the
 *    assigned port.
 * @phy: This is the phy object which has gone link down.
 *
 * This function handles the manual port configuration link down notifications.
 * Since all ports and phys are associated at initialization time we just turn
 * around and notifiy the port object of the link down event.  If this PHY is
 * not associated with a port there is no action taken. Is it possible to get a
 * link down notification from a phy that has no assocoated port?
 */
static void sci_mpc_agent_link_down(
	struct isci_host *ihost,
	struct sci_port_configuration_agent *port_agent,
	struct isci_port *iport,
	struct isci_phy *iphy)
{
	if (iport != NULL) {
		/*
		 * If we can form a new port from the remainder of the phys
		 * then we want to start the timer to allow the SCI User to
		 * cleanup old devices and rediscover the port before
		 * rebuilding the port with the phys that remain in the ready
		 * state.
		 */
		port_agent->phy_ready_mask &= ~(1 << iphy->phy_index);
		port_agent->phy_configured_mask &= ~(1 << iphy->phy_index);

		/*
		 * Check to see if there are more phys waiting to be
		 * configured into a port. If there are allow the SCI User
		 * to tear down this port, if necessary, and then reconstruct
		 * the port after the timeout.
		 */
		if ((port_agent->phy_configured_mask == 0x0000) &&
		    (port_agent->phy_ready_mask != 0x0000) &&
		    !port_agent->timer_pending) {
			port_agent->timer_pending = true;

			sci_mod_timer(&port_agent->timer,
				      SCIC_SDS_MPC_RECONFIGURATION_TIMEOUT);
		}

		sci_port_link_down(iport, iphy);
	}
}

/* verify phys are assigned a valid SAS address for automatic port
 * configuration mode.
 */
static enum sci_status
sci_apc_agent_validate_phy_configuration(struct isci_host *ihost,
					      struct sci_port_configuration_agent *port_agent)
{
	u8 phy_index;
	u8 port_index;
	struct sci_sas_address sas_address;
	struct sci_sas_address phy_assigned_address;

	phy_index = 0;

	while (phy_index < SCI_MAX_PHYS) {
		port_index = phy_index;

		/* Get the assigned SAS Address for the first PHY on the controller. */
		sci_phy_get_sas_address(&ihost->phys[phy_index],
					    &sas_address);

		while (++phy_index < SCI_MAX_PHYS) {
			sci_phy_get_sas_address(&ihost->phys[phy_index],
						     &phy_assigned_address);

			/* Verify each of the SAS address are all the same for every PHY */
			if (sci_sas_address_compare(sas_address, phy_assigned_address) == 0) {
				port_agent->phy_valid_port_range[phy_index].min_index = port_index;
				port_agent->phy_valid_port_range[phy_index].max_index = phy_index;
			} else {
				port_agent->phy_valid_port_range[phy_index].min_index = phy_index;
				port_agent->phy_valid_port_range[phy_index].max_index = phy_index;
				break;
			}
		}
	}

	return sci_port_configuration_agent_validate_ports(ihost, port_agent);
}

static void sci_apc_agent_configure_ports(struct isci_host *ihost,
					       struct sci_port_configuration_agent *port_agent,
					       struct isci_phy *iphy,
					       bool start_timer)
{
	u8 port_index;
	enum sci_status status;
	struct isci_port *iport;
	enum SCIC_SDS_APC_ACTIVITY apc_activity = SCIC_SDS_APC_SKIP_PHY;

	iport = sci_port_configuration_agent_find_port(ihost, iphy);

	if (iport) {
		if (sci_port_is_valid_phy_assignment(iport, iphy->phy_index))
			apc_activity = SCIC_SDS_APC_ADD_PHY;
		else
			apc_activity = SCIC_SDS_APC_SKIP_PHY;
	} else {
		/*
		 * There is no matching Port for this PHY so lets search through the
		 * Ports and see if we can add the PHY to its own port or maybe start
		 * the timer and wait to see if a wider port can be made.
		 *
		 * Note the break when we reach the condition of the port id == phy id */
		for (port_index = port_agent->phy_valid_port_range[iphy->phy_index].min_index;
		     port_index <= port_agent->phy_valid_port_range[iphy->phy_index].max_index;
		     port_index++) {

			iport = &ihost->ports[port_index];

			/* First we must make sure that this PHY can be added to this Port. */
			if (sci_port_is_valid_phy_assignment(iport, iphy->phy_index)) {
				/*
				 * Port contains a PHY with a greater PHY ID than the current
				 * PHY that has gone link up.  This phy can not be part of any
				 * port so skip it and move on. */
				if (iport->active_phy_mask > (1 << iphy->phy_index)) {
					apc_activity = SCIC_SDS_APC_SKIP_PHY;
					break;
				}

				/*
				 * We have reached the end of our Port list and have not found
				 * any reason why we should not either add the PHY to the port
				 * or wait for more phys to become active. */
				if (iport->physical_port_index == iphy->phy_index) {
					/*
					 * The Port either has no active PHYs.
					 * Consider that if the port had any active PHYs we would have
					 * or active PHYs with
					 * a lower PHY Id than this PHY. */
					if (apc_activity != SCIC_SDS_APC_START_TIMER) {
						apc_activity = SCIC_SDS_APC_ADD_PHY;
					}

					break;
				}

				/*
				 * The current Port has no active PHYs and this PHY could be part
				 * of this Port.  Since we dont know as yet setup to start the
				 * timer and see if there is a better configuration. */
				if (iport->active_phy_mask == 0) {
					apc_activity = SCIC_SDS_APC_START_TIMER;
				}
			} else if (iport->active_phy_mask != 0) {
				/*
				 * The Port has an active phy and the current Phy can not
				 * participate in this port so skip the PHY and see if
				 * there is a better configuration. */
				apc_activity = SCIC_SDS_APC_SKIP_PHY;
			}
		}
	}

	/*
	 * Check to see if the start timer operations should instead map to an
	 * add phy operation.  This is caused because we have been waiting to
	 * add a phy to a port but could not becuase the automatic port
	 * configuration engine had a choice of possible ports for the phy.
	 * Since we have gone through a timeout we are going to restrict the
	 * choice to the smallest possible port. */
	if (
		(start_timer == false)
		&& (apc_activity == SCIC_SDS_APC_START_TIMER)
		) {
		apc_activity = SCIC_SDS_APC_ADD_PHY;
	}

	switch (apc_activity) {
	case SCIC_SDS_APC_ADD_PHY:
		status = sci_port_add_phy(iport, iphy);

		if (status == SCI_SUCCESS) {
			port_agent->phy_configured_mask |= (1 << iphy->phy_index);
		}
		break;

	case SCIC_SDS_APC_START_TIMER:
		/*
		 * This can occur for either a link down event, or a link
		 * up event where we cannot yet tell the port to which a
		 * phy belongs.
		 */
		if (port_agent->timer_pending)
			sci_del_timer(&port_agent->timer);

		port_agent->timer_pending = true;
		sci_mod_timer(&port_agent->timer,
			      SCIC_SDS_APC_WAIT_LINK_UP_NOTIFICATION);
		break;

	case SCIC_SDS_APC_SKIP_PHY:
	default:
		/* do nothing the PHY can not be made part of a port at this time. */
		break;
	}
}

/**
 * sci_apc_agent_link_up - handle apc link up events
 * @scic: This is the controller object that receives the link up
 *    notification.
 * @sci_port: This is the port object associated with the phy.  If the is no
 *    associated port this is an NULL.
 * @sci_phy: This is the phy object which has gone link up.
 *
 * This method handles the automatic port configuration for link up
 * notifications. Is it possible to get a link down notification from a phy
 * that has no assocoated port?
 */
static void sci_apc_agent_link_up(struct isci_host *ihost,
				       struct sci_port_configuration_agent *port_agent,
				       struct isci_port *iport,
				       struct isci_phy *iphy)
{
	u8 phy_index  = iphy->phy_index;

	if (!iport) {
		/* the phy is not the part of this port */
		port_agent->phy_ready_mask |= 1 << phy_index;
		sci_apc_agent_configure_ports(ihost, port_agent, iphy, true);
	} else {
		/* the phy is already the part of the port */
		u32 port_state = iport->sm.current_state_id;

		/* if the PORT'S state is resetting then the link up is from
		 * port hard reset in this case, we need to tell the port
		 * that link up is recieved
		 */
		BUG_ON(port_state != SCI_PORT_RESETTING);
		port_agent->phy_ready_mask |= 1 << phy_index;
		sci_port_link_up(iport, iphy);
	}
}

/**
 *
 * @controller: This is the controller object that receives the link down
 *    notification.
 * @iport: This is the port object associated with the phy.  If the is no
 *    associated port this is an NULL.
 * @iphy: This is the phy object which has gone link down.
 *
 * This method handles the automatic port configuration link down
 * notifications. not associated with a port there is no action taken. Is it
 * possible to get a link down notification from a phy that has no assocoated
 * port?
 */
static void sci_apc_agent_link_down(
	struct isci_host *ihost,
	struct sci_port_configuration_agent *port_agent,
	struct isci_port *iport,
	struct isci_phy *iphy)
{
	port_agent->phy_ready_mask &= ~(1 << iphy->phy_index);

	if (!iport)
		return;
	if (port_agent->phy_configured_mask & (1 << iphy->phy_index)) {
		enum sci_status status;

		status = sci_port_remove_phy(iport, iphy);

		if (status == SCI_SUCCESS)
			port_agent->phy_configured_mask &= ~(1 << iphy->phy_index);
	}
}

/* configure the phys into ports when the timer fires */
static void apc_agent_timeout(unsigned long data)
{
	u32 index;
	struct sci_timer *tmr = (struct sci_timer *)data;
	struct sci_port_configuration_agent *port_agent;
	struct isci_host *ihost;
	unsigned long flags;
	u16 configure_phy_mask;

	port_agent = container_of(tmr, typeof(*port_agent), timer);
	ihost = container_of(port_agent, typeof(*ihost), port_agent);

	spin_lock_irqsave(&ihost->scic_lock, flags);

	if (tmr->cancel)
		goto done;

	port_agent->timer_pending = false;

	configure_phy_mask = ~port_agent->phy_configured_mask & port_agent->phy_ready_mask;

	if (!configure_phy_mask)
		return;

	for (index = 0; index < SCI_MAX_PHYS; index++) {
		if ((configure_phy_mask & (1 << index)) == 0)
			continue;

		sci_apc_agent_configure_ports(ihost, port_agent,
						   &ihost->phys[index], false);
	}

done:
	spin_unlock_irqrestore(&ihost->scic_lock, flags);
}

/*
 * ******************************************************************************
 * Public port configuration agent routines
 * ****************************************************************************** */

/**
 *
 *
 * This method will construct the port configuration agent for operation. This
 * call is universal for both manual port configuration and automatic port
 * configuration modes.
 */
void sci_port_configuration_agent_construct(
	struct sci_port_configuration_agent *port_agent)
{
	u32 index;

	port_agent->phy_configured_mask = 0x00;
	port_agent->phy_ready_mask = 0x00;

	port_agent->link_up_handler = NULL;
	port_agent->link_down_handler = NULL;

	port_agent->timer_pending = false;

	for (index = 0; index < SCI_MAX_PORTS; index++) {
		port_agent->phy_valid_port_range[index].min_index = 0;
		port_agent->phy_valid_port_range[index].max_index = 0;
	}
}

enum sci_status sci_port_configuration_agent_initialize(
	struct isci_host *ihost,
	struct sci_port_configuration_agent *port_agent)
{
	enum sci_status status;
	enum sci_port_configuration_mode mode;

	mode = ihost->oem_parameters.controller.mode_type;

	if (mode == SCIC_PORT_MANUAL_CONFIGURATION_MODE) {
		status = sci_mpc_agent_validate_phy_configuration(
				ihost, port_agent);

		port_agent->link_up_handler = sci_mpc_agent_link_up;
		port_agent->link_down_handler = sci_mpc_agent_link_down;

		sci_init_timer(&port_agent->timer, mpc_agent_timeout);
	} else {
		status = sci_apc_agent_validate_phy_configuration(
				ihost, port_agent);

		port_agent->link_up_handler = sci_apc_agent_link_up;
		port_agent->link_down_handler = sci_apc_agent_link_down;

		sci_init_timer(&port_agent->timer, apc_agent_timeout);
	}

	return status;
}
