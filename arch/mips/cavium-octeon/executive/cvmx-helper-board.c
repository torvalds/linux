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

/*
 *
 * Helper functions to abstract board specific data about
 * network ports from the rest of the cvmx-helper files.
 */

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-bootinfo.h>

#include <asm/octeon/cvmx-config.h>

#include <asm/octeon/cvmx-mdio.h>

#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-util.h>
#include <asm/octeon/cvmx-helper-board.h>

#include <asm/octeon/cvmx-gmxx-defs.h>
#include <asm/octeon/cvmx-asxx-defs.h>

/**
 * cvmx_override_board_link_get(int ipd_port) is a function
 * pointer. It is meant to allow customization of the process of
 * talking to a PHY to determine link speed. It is called every
 * time a PHY must be polled for link status. Users should set
 * this pointer to a function before calling any cvmx-helper
 * operations.
 */
cvmx_helper_link_info_t(*cvmx_override_board_link_get) (int ipd_port) =
    NULL;

/**
 * Return the MII PHY address associated with the given IPD
 * port. A result of -1 means there isn't a MII capable PHY
 * connected to this port. On chips supporting multiple MII
 * busses the bus number is encoded in bits <15:8>.
 *
 * This function must be modified for every new Octeon board.
 * Internally it uses switch statements based on the cvmx_sysinfo
 * data to determine board types and revisions. It replies on the
 * fact that every Octeon board receives a unique board type
 * enumeration from the bootloader.
 *
 * @ipd_port: Octeon IPD port to get the MII address for.
 *
 * Returns MII PHY address and bus number or -1.
 */
int cvmx_helper_board_get_mii_address(int ipd_port)
{
	switch (cvmx_sysinfo_get()->board_type) {
	case CVMX_BOARD_TYPE_SIM:
		/* Simulator doesn't have MII */
		return -1;
	case CVMX_BOARD_TYPE_EBT3000:
	case CVMX_BOARD_TYPE_EBT5800:
	case CVMX_BOARD_TYPE_THUNDER:
	case CVMX_BOARD_TYPE_NICPRO2:
		/* Interface 0 is SPI4, interface 1 is RGMII */
		if ((ipd_port >= 16) && (ipd_port < 20))
			return ipd_port - 16;
		else
			return -1;
	case CVMX_BOARD_TYPE_KODAMA:
	case CVMX_BOARD_TYPE_EBH3100:
	case CVMX_BOARD_TYPE_HIKARI:
	case CVMX_BOARD_TYPE_CN3010_EVB_HS5:
	case CVMX_BOARD_TYPE_CN3005_EVB_HS5:
	case CVMX_BOARD_TYPE_CN3020_EVB_HS5:
		/*
		 * Port 0 is WAN connected to a PHY, Port 1 is GMII
		 * connected to a switch
		 */
		if (ipd_port == 0)
			return 4;
		else if (ipd_port == 1)
			return 9;
		else
			return -1;
	case CVMX_BOARD_TYPE_NAC38:
		/* Board has 8 RGMII ports PHYs are 0-7 */
		if ((ipd_port >= 0) && (ipd_port < 4))
			return ipd_port;
		else if ((ipd_port >= 16) && (ipd_port < 20))
			return ipd_port - 16 + 4;
		else
			return -1;
	case CVMX_BOARD_TYPE_EBH3000:
		/* Board has dual SPI4 and no PHYs */
		return -1;
	case CVMX_BOARD_TYPE_EBH5200:
	case CVMX_BOARD_TYPE_EBH5201:
	case CVMX_BOARD_TYPE_EBT5200:
		/* Board has 2 management ports */
		if ((ipd_port >= CVMX_HELPER_BOARD_MGMT_IPD_PORT) &&
		    (ipd_port < (CVMX_HELPER_BOARD_MGMT_IPD_PORT + 2)))
			return ipd_port - CVMX_HELPER_BOARD_MGMT_IPD_PORT;
		/*
		 * Board has 4 SGMII ports. The PHYs start right after the MII
		 * ports MII0 = 0, MII1 = 1, SGMII = 2-5.
		 */
		if ((ipd_port >= 0) && (ipd_port < 4))
			return ipd_port + 2;
		else
			return -1;
	case CVMX_BOARD_TYPE_EBH5600:
	case CVMX_BOARD_TYPE_EBH5601:
	case CVMX_BOARD_TYPE_EBH5610:
		/* Board has 1 management port */
		if (ipd_port == CVMX_HELPER_BOARD_MGMT_IPD_PORT)
			return 0;
		/*
		 * Board has 8 SGMII ports. 4 connect out, two connect
		 * to a switch, and 2 loop to each other
		 */
		if ((ipd_port >= 0) && (ipd_port < 4))
			return ipd_port + 1;
		else
			return -1;
	case CVMX_BOARD_TYPE_CUST_NB5:
		if (ipd_port == 2)
			return 4;
		else
			return -1;
	case CVMX_BOARD_TYPE_NIC_XLE_4G:
		/* Board has 4 SGMII ports. connected QLM3(interface 1) */
		if ((ipd_port >= 16) && (ipd_port < 20))
			return ipd_port - 16 + 1;
		else
			return -1;
	case CVMX_BOARD_TYPE_NIC_XLE_10G:
	case CVMX_BOARD_TYPE_NIC10E:
		return -1;
	case CVMX_BOARD_TYPE_NIC4E:
		if (ipd_port >= 0 && ipd_port <= 3)
			return (ipd_port + 0x1f) & 0x1f;
		else
			return -1;
	case CVMX_BOARD_TYPE_NIC2E:
		if (ipd_port >= 0 && ipd_port <= 1)
			return ipd_port + 1;
		else
			return -1;
	case CVMX_BOARD_TYPE_BBGW_REF:
		/*
		 * No PHYs are connected to Octeon, everything is
		 * through switch.
		 */
		return -1;

	case CVMX_BOARD_TYPE_CUST_WSX16:
		if (ipd_port >= 0 && ipd_port <= 3)
			return ipd_port;
		else if (ipd_port >= 16 && ipd_port <= 19)
			return ipd_port - 16 + 4;
		else
			return -1;
	}

	/* Some unknown board. Somebody forgot to update this function... */
	cvmx_dprintf
	    ("cvmx_helper_board_get_mii_address: Unknown board type %d\n",
	     cvmx_sysinfo_get()->board_type);
	return -1;
}

/**
 * This function is the board specific method of determining an
 * ethernet ports link speed. Most Octeon boards have Marvell PHYs
 * and are handled by the fall through case. This function must be
 * updated for boards that don't have the normal Marvell PHYs.
 *
 * This function must be modified for every new Octeon board.
 * Internally it uses switch statements based on the cvmx_sysinfo
 * data to determine board types and revisions. It relies on the
 * fact that every Octeon board receives a unique board type
 * enumeration from the bootloader.
 *
 * @ipd_port: IPD input port associated with the port we want to get link
 *                 status for.
 *
 * Returns The ports link status. If the link isn't fully resolved, this must
 *         return zero.
 */
cvmx_helper_link_info_t __cvmx_helper_board_link_get(int ipd_port)
{
	cvmx_helper_link_info_t result;
	int phy_addr;
	int is_broadcom_phy = 0;

	/* Give the user a chance to override the processing of this function */
	if (cvmx_override_board_link_get)
		return cvmx_override_board_link_get(ipd_port);

	/* Unless we fix it later, all links are defaulted to down */
	result.u64 = 0;

	/*
	 * This switch statement should handle all ports that either don't use
	 * Marvell PHYS, or don't support in-band status.
	 */
	switch (cvmx_sysinfo_get()->board_type) {
	case CVMX_BOARD_TYPE_SIM:
		/* The simulator gives you a simulated 1Gbps full duplex link */
		result.s.link_up = 1;
		result.s.full_duplex = 1;
		result.s.speed = 1000;
		return result;
	case CVMX_BOARD_TYPE_EBH3100:
	case CVMX_BOARD_TYPE_CN3010_EVB_HS5:
	case CVMX_BOARD_TYPE_CN3005_EVB_HS5:
	case CVMX_BOARD_TYPE_CN3020_EVB_HS5:
		/* Port 1 on these boards is always Gigabit */
		if (ipd_port == 1) {
			result.s.link_up = 1;
			result.s.full_duplex = 1;
			result.s.speed = 1000;
			return result;
		}
		/* Fall through to the generic code below */
		break;
	case CVMX_BOARD_TYPE_CUST_NB5:
		/* Port 1 on these boards is always Gigabit */
		if (ipd_port == 1) {
			result.s.link_up = 1;
			result.s.full_duplex = 1;
			result.s.speed = 1000;
			return result;
		} else		/* The other port uses a broadcom PHY */
			is_broadcom_phy = 1;
		break;
	case CVMX_BOARD_TYPE_BBGW_REF:
		/* Port 1 on these boards is always Gigabit */
		if (ipd_port == 2) {
			/* Port 2 is not hooked up */
			result.u64 = 0;
			return result;
		} else {
			/* Ports 0 and 1 connect to the switch */
			result.s.link_up = 1;
			result.s.full_duplex = 1;
			result.s.speed = 1000;
			return result;
		}
		break;
	}

	phy_addr = cvmx_helper_board_get_mii_address(ipd_port);
	if (phy_addr != -1) {
		if (is_broadcom_phy) {
			/*
			 * Below we are going to read SMI/MDIO
			 * register 0x19 which works on Broadcom
			 * parts
			 */
			int phy_status =
			    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
					   0x19);
			switch ((phy_status >> 8) & 0x7) {
			case 0:
				result.u64 = 0;
				break;
			case 1:
				result.s.link_up = 1;
				result.s.full_duplex = 0;
				result.s.speed = 10;
				break;
			case 2:
				result.s.link_up = 1;
				result.s.full_duplex = 1;
				result.s.speed = 10;
				break;
			case 3:
				result.s.link_up = 1;
				result.s.full_duplex = 0;
				result.s.speed = 100;
				break;
			case 4:
				result.s.link_up = 1;
				result.s.full_duplex = 1;
				result.s.speed = 100;
				break;
			case 5:
				result.s.link_up = 1;
				result.s.full_duplex = 1;
				result.s.speed = 100;
				break;
			case 6:
				result.s.link_up = 1;
				result.s.full_duplex = 0;
				result.s.speed = 1000;
				break;
			case 7:
				result.s.link_up = 1;
				result.s.full_duplex = 1;
				result.s.speed = 1000;
				break;
			}
		} else {
			/*
			 * This code assumes we are using a Marvell
			 * Gigabit PHY. All the speed information can
			 * be read from register 17 in one
			 * go. Somebody using a different PHY will
			 * need to handle it above in the board
			 * specific area.
			 */
			int phy_status =
			    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff, 17);

			/*
			 * If the resolve bit 11 isn't set, see if
			 * autoneg is turned off (bit 12, reg 0). The
			 * resolve bit doesn't get set properly when
			 * autoneg is off, so force it.
			 */
			if ((phy_status & (1 << 11)) == 0) {
				int auto_status =
				    cvmx_mdio_read(phy_addr >> 8,
						   phy_addr & 0xff, 0);
				if ((auto_status & (1 << 12)) == 0)
					phy_status |= 1 << 11;
			}

			/*
			 * Only return a link if the PHY has finished
			 * auto negotiation and set the resolved bit
			 * (bit 11)
			 */
			if (phy_status & (1 << 11)) {
				result.s.link_up = 1;
				result.s.full_duplex = ((phy_status >> 13) & 1);
				switch ((phy_status >> 14) & 3) {
				case 0:	/* 10 Mbps */
					result.s.speed = 10;
					break;
				case 1:	/* 100 Mbps */
					result.s.speed = 100;
					break;
				case 2:	/* 1 Gbps */
					result.s.speed = 1000;
					break;
				case 3:	/* Illegal */
					result.u64 = 0;
					break;
				}
			}
		}
	} else if (OCTEON_IS_MODEL(OCTEON_CN3XXX)
		   || OCTEON_IS_MODEL(OCTEON_CN58XX)
		   || OCTEON_IS_MODEL(OCTEON_CN50XX)) {
		/*
		 * We don't have a PHY address, so attempt to use
		 * in-band status. It is really important that boards
		 * not supporting in-band status never get
		 * here. Reading broken in-band status tends to do bad
		 * things
		 */
		union cvmx_gmxx_rxx_rx_inbnd inband_status;
		int interface = cvmx_helper_get_interface_num(ipd_port);
		int index = cvmx_helper_get_interface_index_num(ipd_port);
		inband_status.u64 =
		    cvmx_read_csr(CVMX_GMXX_RXX_RX_INBND(index, interface));

		result.s.link_up = inband_status.s.status;
		result.s.full_duplex = inband_status.s.duplex;
		switch (inband_status.s.speed) {
		case 0:	/* 10 Mbps */
			result.s.speed = 10;
			break;
		case 1:	/* 100 Mbps */
			result.s.speed = 100;
			break;
		case 2:	/* 1 Gbps */
			result.s.speed = 1000;
			break;
		case 3:	/* Illegal */
			result.u64 = 0;
			break;
		}
	} else {
		/*
		 * We don't have a PHY address and we don't have
		 * in-band status. There is no way to determine the
		 * link speed. Return down assuming this port isn't
		 * wired
		 */
		result.u64 = 0;
	}

	/* If link is down, return all fields as zero. */
	if (!result.s.link_up)
		result.u64 = 0;

	return result;
}

/**
 * This function as a board specific method of changing the PHY
 * speed, duplex, and auto-negotiation. This programs the PHY and
 * not Octeon. This can be used to force Octeon's links to
 * specific settings.
 *
 * @phy_addr:  The address of the PHY to program
 * @enable_autoneg:
 *                  Non zero if you want to enable auto-negotiation.
 * @link_info: Link speed to program. If the speed is zero and auto-negotiation
 *                  is enabled, all possible negotiation speeds are advertised.
 *
 * Returns Zero on success, negative on failure
 */
int cvmx_helper_board_link_set_phy(int phy_addr,
				   cvmx_helper_board_set_phy_link_flags_types_t
				   link_flags,
				   cvmx_helper_link_info_t link_info)
{

	/* Set the flow control settings based on link_flags */
	if ((link_flags & set_phy_link_flags_flow_control_mask) !=
	    set_phy_link_flags_flow_control_dont_touch) {
		cvmx_mdio_phy_reg_autoneg_adver_t reg_autoneg_adver;
		reg_autoneg_adver.u16 =
		    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
				   CVMX_MDIO_PHY_REG_AUTONEG_ADVER);
		reg_autoneg_adver.s.asymmetric_pause =
		    (link_flags & set_phy_link_flags_flow_control_mask) ==
		    set_phy_link_flags_flow_control_enable;
		reg_autoneg_adver.s.pause =
		    (link_flags & set_phy_link_flags_flow_control_mask) ==
		    set_phy_link_flags_flow_control_enable;
		cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff,
				CVMX_MDIO_PHY_REG_AUTONEG_ADVER,
				reg_autoneg_adver.u16);
	}

	/* If speed isn't set and autoneg is on advertise all supported modes */
	if ((link_flags & set_phy_link_flags_autoneg)
	    && (link_info.s.speed == 0)) {
		cvmx_mdio_phy_reg_control_t reg_control;
		cvmx_mdio_phy_reg_status_t reg_status;
		cvmx_mdio_phy_reg_autoneg_adver_t reg_autoneg_adver;
		cvmx_mdio_phy_reg_extended_status_t reg_extended_status;
		cvmx_mdio_phy_reg_control_1000_t reg_control_1000;

		reg_status.u16 =
		    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
				   CVMX_MDIO_PHY_REG_STATUS);
		reg_autoneg_adver.u16 =
		    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
				   CVMX_MDIO_PHY_REG_AUTONEG_ADVER);
		reg_autoneg_adver.s.advert_100base_t4 =
		    reg_status.s.capable_100base_t4;
		reg_autoneg_adver.s.advert_10base_tx_full =
		    reg_status.s.capable_10_full;
		reg_autoneg_adver.s.advert_10base_tx_half =
		    reg_status.s.capable_10_half;
		reg_autoneg_adver.s.advert_100base_tx_full =
		    reg_status.s.capable_100base_x_full;
		reg_autoneg_adver.s.advert_100base_tx_half =
		    reg_status.s.capable_100base_x_half;
		cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff,
				CVMX_MDIO_PHY_REG_AUTONEG_ADVER,
				reg_autoneg_adver.u16);
		if (reg_status.s.capable_extended_status) {
			reg_extended_status.u16 =
			    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
					   CVMX_MDIO_PHY_REG_EXTENDED_STATUS);
			reg_control_1000.u16 =
			    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
					   CVMX_MDIO_PHY_REG_CONTROL_1000);
			reg_control_1000.s.advert_1000base_t_full =
			    reg_extended_status.s.capable_1000base_t_full;
			reg_control_1000.s.advert_1000base_t_half =
			    reg_extended_status.s.capable_1000base_t_half;
			cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff,
					CVMX_MDIO_PHY_REG_CONTROL_1000,
					reg_control_1000.u16);
		}
		reg_control.u16 =
		    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
				   CVMX_MDIO_PHY_REG_CONTROL);
		reg_control.s.autoneg_enable = 1;
		reg_control.s.restart_autoneg = 1;
		cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff,
				CVMX_MDIO_PHY_REG_CONTROL, reg_control.u16);
	} else if ((link_flags & set_phy_link_flags_autoneg)) {
		cvmx_mdio_phy_reg_control_t reg_control;
		cvmx_mdio_phy_reg_status_t reg_status;
		cvmx_mdio_phy_reg_autoneg_adver_t reg_autoneg_adver;
		cvmx_mdio_phy_reg_control_1000_t reg_control_1000;

		reg_status.u16 =
		    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
				   CVMX_MDIO_PHY_REG_STATUS);
		reg_autoneg_adver.u16 =
		    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
				   CVMX_MDIO_PHY_REG_AUTONEG_ADVER);
		reg_autoneg_adver.s.advert_100base_t4 = 0;
		reg_autoneg_adver.s.advert_10base_tx_full = 0;
		reg_autoneg_adver.s.advert_10base_tx_half = 0;
		reg_autoneg_adver.s.advert_100base_tx_full = 0;
		reg_autoneg_adver.s.advert_100base_tx_half = 0;
		if (reg_status.s.capable_extended_status) {
			reg_control_1000.u16 =
			    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
					   CVMX_MDIO_PHY_REG_CONTROL_1000);
			reg_control_1000.s.advert_1000base_t_full = 0;
			reg_control_1000.s.advert_1000base_t_half = 0;
		}
		switch (link_info.s.speed) {
		case 10:
			reg_autoneg_adver.s.advert_10base_tx_full =
			    link_info.s.full_duplex;
			reg_autoneg_adver.s.advert_10base_tx_half =
			    !link_info.s.full_duplex;
			break;
		case 100:
			reg_autoneg_adver.s.advert_100base_tx_full =
			    link_info.s.full_duplex;
			reg_autoneg_adver.s.advert_100base_tx_half =
			    !link_info.s.full_duplex;
			break;
		case 1000:
			reg_control_1000.s.advert_1000base_t_full =
			    link_info.s.full_duplex;
			reg_control_1000.s.advert_1000base_t_half =
			    !link_info.s.full_duplex;
			break;
		}
		cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff,
				CVMX_MDIO_PHY_REG_AUTONEG_ADVER,
				reg_autoneg_adver.u16);
		if (reg_status.s.capable_extended_status)
			cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff,
					CVMX_MDIO_PHY_REG_CONTROL_1000,
					reg_control_1000.u16);
		reg_control.u16 =
		    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
				   CVMX_MDIO_PHY_REG_CONTROL);
		reg_control.s.autoneg_enable = 1;
		reg_control.s.restart_autoneg = 1;
		cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff,
				CVMX_MDIO_PHY_REG_CONTROL, reg_control.u16);
	} else {
		cvmx_mdio_phy_reg_control_t reg_control;
		reg_control.u16 =
		    cvmx_mdio_read(phy_addr >> 8, phy_addr & 0xff,
				   CVMX_MDIO_PHY_REG_CONTROL);
		reg_control.s.autoneg_enable = 0;
		reg_control.s.restart_autoneg = 1;
		reg_control.s.duplex = link_info.s.full_duplex;
		if (link_info.s.speed == 1000) {
			reg_control.s.speed_msb = 1;
			reg_control.s.speed_lsb = 0;
		} else if (link_info.s.speed == 100) {
			reg_control.s.speed_msb = 0;
			reg_control.s.speed_lsb = 1;
		} else if (link_info.s.speed == 10) {
			reg_control.s.speed_msb = 0;
			reg_control.s.speed_lsb = 0;
		}
		cvmx_mdio_write(phy_addr >> 8, phy_addr & 0xff,
				CVMX_MDIO_PHY_REG_CONTROL, reg_control.u16);
	}
	return 0;
}

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
 *                  Number of ports Octeon supports.
 *
 * Returns Number of ports the actual board supports. Many times this will
 *         simple be "support_ports".
 */
int __cvmx_helper_board_interface_probe(int interface, int supported_ports)
{
	switch (cvmx_sysinfo_get()->board_type) {
	case CVMX_BOARD_TYPE_CN3005_EVB_HS5:
		if (interface == 0)
			return 2;
		break;
	case CVMX_BOARD_TYPE_BBGW_REF:
		if (interface == 0)
			return 2;
		break;
	case CVMX_BOARD_TYPE_NIC_XLE_4G:
		if (interface == 0)
			return 0;
		break;
		/* The 2nd interface on the EBH5600 is connected to the Marvel switch,
		   which we don't support. Disable ports connected to it */
	case CVMX_BOARD_TYPE_EBH5600:
		if (interface == 1)
			return 0;
		break;
	}
	return supported_ports;
}

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
int __cvmx_helper_board_hardware_enable(int interface)
{
	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CN3005_EVB_HS5) {
		if (interface == 0) {
			/* Different config for switch port */
			cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(1, interface), 0);
			cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(1, interface), 0);
			/*
			 * Boards with gigabit WAN ports need a
			 * different setting that is compatible with
			 * 100 Mbit settings
			 */
			cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(0, interface),
				       0xc);
			cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(0, interface),
				       0xc);
		}
	} else if (cvmx_sysinfo_get()->board_type ==
		   CVMX_BOARD_TYPE_CN3010_EVB_HS5) {
		/*
		 * Broadcom PHYs require differnet ASX
		 * clocks. Unfortunately many boards don't define a
		 * new board Id and simply mangle the
		 * CN3010_EVB_HS5
		 */
		if (interface == 0) {
			/*
			 * Some boards use a hacked up bootloader that
			 * identifies them as CN3010_EVB_HS5
			 * evaluation boards.  This leads to all kinds
			 * of configuration problems.  Detect one
			 * case, and print warning, while trying to do
			 * the right thing.
			 */
			int phy_addr = cvmx_helper_board_get_mii_address(0);
			if (phy_addr != -1) {
				int phy_identifier =
				    cvmx_mdio_read(phy_addr >> 8,
						   phy_addr & 0xff, 0x2);
				/* Is it a Broadcom PHY? */
				if (phy_identifier == 0x0143) {
					cvmx_dprintf("\n");
					cvmx_dprintf("ERROR:\n");
					cvmx_dprintf
					    ("ERROR: Board type is CVMX_BOARD_TYPE_CN3010_EVB_HS5, but Broadcom PHY found.\n");
					cvmx_dprintf
					    ("ERROR: The board type is mis-configured, and software malfunctions are likely.\n");
					cvmx_dprintf
					    ("ERROR: All boards require a unique board type to identify them.\n");
					cvmx_dprintf("ERROR:\n");
					cvmx_dprintf("\n");
					cvmx_wait(1000000000);
					cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX
						       (0, interface), 5);
					cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX
						       (0, interface), 5);
				}
			}
		}
	}
	return 0;
}
