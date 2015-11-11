/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

/**
 *
 * Helper functions to abstract board specific data about
 * network ports from the rest of the cvmx-helper files.
 *
 */
#ifndef __CVMX_HELPER_BOARD_H__
#define __CVMX_HELPER_BOARD_H__

#include <asm/octeon/cvmx-helper.h>

enum cvmx_helper_board_usb_clock_types {
	USB_CLOCK_TYPE_REF_12,
	USB_CLOCK_TYPE_REF_24,
	USB_CLOCK_TYPE_REF_48,
	USB_CLOCK_TYPE_CRYSTAL_12,
};

typedef enum {
	set_phy_link_flags_autoneg = 0x1,
	set_phy_link_flags_flow_control_dont_touch = 0x0 << 1,
	set_phy_link_flags_flow_control_enable = 0x1 << 1,
	set_phy_link_flags_flow_control_disable = 0x2 << 1,
	set_phy_link_flags_flow_control_mask = 0x3 << 1,	/* Mask for 2 bit wide flow control field */
} cvmx_helper_board_set_phy_link_flags_types_t;

/*
 * Fake IPD port, the RGMII/MII interface may use different PHY, use
 * this macro to return appropriate MIX address to read the PHY.
 */
#define CVMX_HELPER_BOARD_MGMT_IPD_PORT	    -10

/**
 * cvmx_override_board_link_get(int ipd_port) is a function
 * pointer. It is meant to allow customization of the process of
 * talking to a PHY to determine link speed. It is called every
 * time a PHY must be polled for link status. Users should set
 * this pointer to a function before calling any cvmx-helper
 * operations.
 */
extern cvmx_helper_link_info_t(*cvmx_override_board_link_get) (int ipd_port);

/**
 * Return the MII PHY address associated with the given IPD
 * port. A result of -1 means there isn't a MII capable PHY
 * connected to this port. On chips supporting multiple MII
 * busses the bus number is encoded in bits <15:8>.
 *
 * This function must be modifed for every new Octeon board.
 * Internally it uses switch statements based on the cvmx_sysinfo
 * data to determine board types and revisions. It relys on the
 * fact that every Octeon board receives a unique board type
 * enumeration from the bootloader.
 *
 * @ipd_port: Octeon IPD port to get the MII address for.
 *
 * Returns MII PHY address and bus number or -1.
 */
extern int cvmx_helper_board_get_mii_address(int ipd_port);

/**
 * This function as a board specific method of changing the PHY
 * speed, duplex, and autonegotiation. This programs the PHY and
 * not Octeon. This can be used to force Octeon's links to
 * specific settings.
 *
 * @phy_addr:  The address of the PHY to program
 * @link_flags:
 *		    Flags to control autonegotiation.  Bit 0 is autonegotiation
 *		    enable/disable to maintain backware compatibility.
 * @link_info: Link speed to program. If the speed is zero and autonegotiation
 *		    is enabled, all possible negotiation speeds are advertised.
 *
 * Returns Zero on success, negative on failure
 */
int cvmx_helper_board_link_set_phy(int phy_addr,
				   cvmx_helper_board_set_phy_link_flags_types_t
				   link_flags,
				   cvmx_helper_link_info_t link_info);

/**
 * This function is the board specific method of determining an
 * ethernet ports link speed. Most Octeon boards have Marvell PHYs
 * and are handled by the fall through case. This function must be
 * updated for boards that don't have the normal Marvell PHYs.
 *
 * This function must be modifed for every new Octeon board.
 * Internally it uses switch statements based on the cvmx_sysinfo
 * data to determine board types and revisions. It relys on the
 * fact that every Octeon board receives a unique board type
 * enumeration from the bootloader.
 *
 * @ipd_port: IPD input port associated with the port we want to get link
 *		   status for.
 *
 * Returns The ports link status. If the link isn't fully resolved, this must
 *	   return zero.
 */
extern cvmx_helper_link_info_t __cvmx_helper_board_link_get(int ipd_port);

/**
 * This function is called by cvmx_helper_interface_probe() after it
 * determines the number of ports Octeon can support on a specific
 * interface. This function is the per board location to override
 * this value. It is called with the number of ports Octeon might
 * support and should return the number of actual ports on the
 * board.
 *
 * This function must be modifed for every new Octeon board.
 * Internally it uses switch statements based on the cvmx_sysinfo
 * data to determine board types and revisions. It relys on the
 * fact that every Octeon board receives a unique board type
 * enumeration from the bootloader.
 *
 * @interface: Interface to probe
 * @supported_ports:
 *		    Number of ports Octeon supports.
 *
 * Returns Number of ports the actual board supports. Many times this will
 *	   simple be "support_ports".
 */
extern int __cvmx_helper_board_interface_probe(int interface,
					       int supported_ports);

/**
 * Enable packet input/output from the hardware. This function is
 * called after by cvmx_helper_packet_hardware_enable() to
 * perform board specific initialization. For most boards
 * nothing is needed.
 *
 * @interface: Interface to enable
 *
 * Returns Zero on success, negative on failure
 */
extern int __cvmx_helper_board_hardware_enable(int interface);

enum cvmx_helper_board_usb_clock_types __cvmx_helper_board_usb_get_clock_type(void);

#endif /* __CVMX_HELPER_BOARD_H__ */
