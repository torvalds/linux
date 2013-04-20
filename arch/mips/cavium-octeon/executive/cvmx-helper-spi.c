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

void __cvmx_interrupt_gmxx_enable(int interface);
void __cvmx_interrupt_spxx_int_msk_enable(int index);
void __cvmx_interrupt_stxx_int_msk_enable(int index);

/*
 * Functions for SPI initialization, configuration,
 * and monitoring.
 */
#include <asm/octeon/octeon.h>

#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-spi.h>
#include <asm/octeon/cvmx-helper.h>

#include <asm/octeon/cvmx-pip-defs.h>
#include <asm/octeon/cvmx-pko-defs.h>

/*
 * CVMX_HELPER_SPI_TIMEOUT is used to determine how long the SPI
 * initialization routines wait for SPI training. You can override the
 * value using executive-config.h if necessary.
 */
#ifndef CVMX_HELPER_SPI_TIMEOUT
#define CVMX_HELPER_SPI_TIMEOUT 10
#endif

int __cvmx_helper_spi_enumerate(int interface)
{
	if ((cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM) &&
	    cvmx_spi4000_is_present(interface)) {
		return 10;
	} else {
		return 16;
	}
}

/**
 * Probe a SPI interface and determine the number of ports
 * connected to it. The SPI interface should still be down after
 * this call.
 *
 * @interface: Interface to probe
 *
 * Returns Number of ports on the interface. Zero to disable.
 */
int __cvmx_helper_spi_probe(int interface)
{
	int num_ports = 0;

	if ((cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM) &&
	    cvmx_spi4000_is_present(interface)) {
		num_ports = 10;
	} else {
		union cvmx_pko_reg_crc_enable enable;
		num_ports = 16;
		/*
		 * Unlike the SPI4000, most SPI devices don't
		 * automatically put on the L2 CRC. For everything
		 * except for the SPI4000 have PKO append the L2 CRC
		 * to the packet.
		 */
		enable.u64 = cvmx_read_csr(CVMX_PKO_REG_CRC_ENABLE);
		enable.s.enable |= 0xffff << (interface * 16);
		cvmx_write_csr(CVMX_PKO_REG_CRC_ENABLE, enable.u64);
	}
	__cvmx_helper_setup_gmx(interface, num_ports);
	return num_ports;
}

/**
 * Bringup and enable a SPI interface. After this call packet I/O
 * should be fully functional. This is called with IPD enabled but
 * PKO disabled.
 *
 * @interface: Interface to bring up
 *
 * Returns Zero on success, negative on failure
 */
int __cvmx_helper_spi_enable(int interface)
{
	/*
	 * Normally the ethernet L2 CRC is checked and stripped in the
	 * GMX block.  When you are using SPI, this isn' the case and
	 * IPD needs to check the L2 CRC.
	 */
	int num_ports = cvmx_helper_ports_on_interface(interface);
	int ipd_port;
	for (ipd_port = interface * 16; ipd_port < interface * 16 + num_ports;
	     ipd_port++) {
		union cvmx_pip_prt_cfgx port_config;
		port_config.u64 = cvmx_read_csr(CVMX_PIP_PRT_CFGX(ipd_port));
		port_config.s.crc_en = 1;
		cvmx_write_csr(CVMX_PIP_PRT_CFGX(ipd_port), port_config.u64);
	}

	if (cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM) {
		cvmx_spi_start_interface(interface, CVMX_SPI_MODE_DUPLEX,
					 CVMX_HELPER_SPI_TIMEOUT, num_ports);
		if (cvmx_spi4000_is_present(interface))
			cvmx_spi4000_initialize(interface);
	}
	__cvmx_interrupt_spxx_int_msk_enable(interface);
	__cvmx_interrupt_stxx_int_msk_enable(interface);
	__cvmx_interrupt_gmxx_enable(interface);
	return 0;
}

/**
 * Return the link state of an IPD/PKO port as returned by
 * auto negotiation. The result of this function may not match
 * Octeon's link config if auto negotiation has changed since
 * the last call to cvmx_helper_link_set().
 *
 * @ipd_port: IPD/PKO port to query
 *
 * Returns Link state
 */
cvmx_helper_link_info_t __cvmx_helper_spi_link_get(int ipd_port)
{
	cvmx_helper_link_info_t result;
	int interface = cvmx_helper_get_interface_num(ipd_port);
	int index = cvmx_helper_get_interface_index_num(ipd_port);
	result.u64 = 0;

	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM) {
		/* The simulator gives you a simulated full duplex link */
		result.s.link_up = 1;
		result.s.full_duplex = 1;
		result.s.speed = 10000;
	} else if (cvmx_spi4000_is_present(interface)) {
		union cvmx_gmxx_rxx_rx_inbnd inband =
		    cvmx_spi4000_check_speed(interface, index);
		result.s.link_up = inband.s.status;
		result.s.full_duplex = inband.s.duplex;
		switch (inband.s.speed) {
		case 0: /* 10 Mbps */
			result.s.speed = 10;
			break;
		case 1: /* 100 Mbps */
			result.s.speed = 100;
			break;
		case 2: /* 1 Gbps */
			result.s.speed = 1000;
			break;
		case 3: /* Illegal */
			result.s.speed = 0;
			result.s.link_up = 0;
			break;
		}
	} else {
		/* For generic SPI we can't determine the link, just return some
		   sane results */
		result.s.link_up = 1;
		result.s.full_duplex = 1;
		result.s.speed = 10000;
	}
	return result;
}

/**
 * Configure an IPD/PKO port for the specified link state. This
 * function does not influence auto negotiation at the PHY level.
 * The passed link state must always match the link state returned
 * by cvmx_helper_link_get(). It is normally best to use
 * cvmx_helper_link_autoconf() instead.
 *
 * @ipd_port:  IPD/PKO port to configure
 * @link_info: The new link state
 *
 * Returns Zero on success, negative on failure
 */
int __cvmx_helper_spi_link_set(int ipd_port, cvmx_helper_link_info_t link_info)
{
	/* Nothing to do. If we have a SPI4000 then the setup was already performed
	   by cvmx_spi4000_check_speed(). If not then there isn't any link
	   info */
	return 0;
}
