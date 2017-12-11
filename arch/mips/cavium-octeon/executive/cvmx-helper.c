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
 * Helper functions for common, but complicated tasks.
 *
 */
#include <asm/octeon/octeon.h>

#include <asm/octeon/cvmx-config.h>

#include <asm/octeon/cvmx-fpa.h>
#include <asm/octeon/cvmx-pip.h>
#include <asm/octeon/cvmx-pko.h>
#include <asm/octeon/cvmx-ipd.h>
#include <asm/octeon/cvmx-spi.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-board.h>

#include <asm/octeon/cvmx-pip-defs.h>
#include <asm/octeon/cvmx-smix-defs.h>
#include <asm/octeon/cvmx-asxx-defs.h>

/**
 * cvmx_override_pko_queue_priority(int ipd_port, uint64_t
 * priorities[16]) is a function pointer. It is meant to allow
 * customization of the PKO queue priorities based on the port
 * number. Users should set this pointer to a function before
 * calling any cvmx-helper operations.
 */
void (*cvmx_override_pko_queue_priority) (int pko_port,
					  uint64_t priorities[16]);

/**
 * cvmx_override_ipd_port_setup(int ipd_port) is a function
 * pointer. It is meant to allow customization of the IPD port
 * setup before packet input/output comes online. It is called
 * after cvmx-helper does the default IPD configuration, but
 * before IPD is enabled. Users should set this pointer to a
 * function before calling any cvmx-helper operations.
 */
void (*cvmx_override_ipd_port_setup) (int ipd_port);

/* Port count per interface */
static int interface_port_count[5];

/**
 * Return the number of interfaces the chip has. Each interface
 * may have multiple ports. Most chips support two interfaces,
 * but the CNX0XX and CNX1XX are exceptions. These only support
 * one interface.
 *
 * Returns Number of interfaces on chip
 */
int cvmx_helper_get_number_of_interfaces(void)
{
	if (OCTEON_IS_MODEL(OCTEON_CN68XX))
		return 9;
	if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN52XX))
		return 4;
	if (OCTEON_IS_MODEL(OCTEON_CN7XXX))
		return 5;
	else
		return 3;
}
EXPORT_SYMBOL_GPL(cvmx_helper_get_number_of_interfaces);

/**
 * Return the number of ports on an interface. Depending on the
 * chip and configuration, this can be 1-16. A value of 0
 * specifies that the interface doesn't exist or isn't usable.
 *
 * @interface: Interface to get the port count for
 *
 * Returns Number of ports on interface. Can be Zero.
 */
int cvmx_helper_ports_on_interface(int interface)
{
	return interface_port_count[interface];
}
EXPORT_SYMBOL_GPL(cvmx_helper_ports_on_interface);

/**
 * @INTERNAL
 * Return interface mode for CN68xx.
 */
static cvmx_helper_interface_mode_t __cvmx_get_mode_cn68xx(int interface)
{
	union cvmx_mio_qlmx_cfg qlm_cfg;
	switch (interface) {
	case 0:
		qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(0));
		/* QLM is disabled when QLM SPD is 15. */
		if (qlm_cfg.s.qlm_spd == 15)
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;

		if (qlm_cfg.s.qlm_cfg == 2)
			return CVMX_HELPER_INTERFACE_MODE_SGMII;
		else if (qlm_cfg.s.qlm_cfg == 3)
			return CVMX_HELPER_INTERFACE_MODE_XAUI;
		else
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;
	case 2:
	case 3:
	case 4:
		qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(interface));
		/* QLM is disabled when QLM SPD is 15. */
		if (qlm_cfg.s.qlm_spd == 15)
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;

		if (qlm_cfg.s.qlm_cfg == 2)
			return CVMX_HELPER_INTERFACE_MODE_SGMII;
		else if (qlm_cfg.s.qlm_cfg == 3)
			return CVMX_HELPER_INTERFACE_MODE_XAUI;
		else
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;
	case 7:
		qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(3));
		/* QLM is disabled when QLM SPD is 15. */
		if (qlm_cfg.s.qlm_spd == 15) {
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;
		} else if (qlm_cfg.s.qlm_cfg != 0) {
			qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(1));
			if (qlm_cfg.s.qlm_cfg != 0)
				return CVMX_HELPER_INTERFACE_MODE_DISABLED;
		}
		return CVMX_HELPER_INTERFACE_MODE_NPI;
	case 8:
		return CVMX_HELPER_INTERFACE_MODE_LOOP;
	default:
		return CVMX_HELPER_INTERFACE_MODE_DISABLED;
	}
}

/**
 * @INTERNAL
 * Return interface mode for an Octeon II
 */
static cvmx_helper_interface_mode_t __cvmx_get_mode_octeon2(int interface)
{
	union cvmx_gmxx_inf_mode mode;

	if (OCTEON_IS_MODEL(OCTEON_CN68XX))
		return __cvmx_get_mode_cn68xx(interface);

	if (interface == 2)
		return CVMX_HELPER_INTERFACE_MODE_NPI;

	if (interface == 3)
		return CVMX_HELPER_INTERFACE_MODE_LOOP;

	/* Only present in CN63XX & CN66XX Octeon model */
	if ((OCTEON_IS_MODEL(OCTEON_CN63XX) &&
	     (interface == 4 || interface == 5)) ||
	    (OCTEON_IS_MODEL(OCTEON_CN66XX) &&
	     interface >= 4 && interface <= 7)) {
		return CVMX_HELPER_INTERFACE_MODE_DISABLED;
	}

	if (OCTEON_IS_MODEL(OCTEON_CN66XX)) {
		union cvmx_mio_qlmx_cfg mio_qlm_cfg;

		/* QLM2 is SGMII0 and QLM1 is SGMII1 */
		if (interface == 0)
			mio_qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(2));
		else if (interface == 1)
			mio_qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(1));
		else
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;

		if (mio_qlm_cfg.s.qlm_spd == 15)
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;

		if (mio_qlm_cfg.s.qlm_cfg == 9)
			return CVMX_HELPER_INTERFACE_MODE_SGMII;
		else if (mio_qlm_cfg.s.qlm_cfg == 11)
			return CVMX_HELPER_INTERFACE_MODE_XAUI;
		else
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;
	} else if (OCTEON_IS_MODEL(OCTEON_CN61XX)) {
		union cvmx_mio_qlmx_cfg qlm_cfg;

		if (interface == 0) {
			qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(2));
			if (qlm_cfg.s.qlm_cfg == 2)
				return CVMX_HELPER_INTERFACE_MODE_SGMII;
			else if (qlm_cfg.s.qlm_cfg == 3)
				return CVMX_HELPER_INTERFACE_MODE_XAUI;
			else
				return CVMX_HELPER_INTERFACE_MODE_DISABLED;
		} else if (interface == 1) {
			qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(0));
			if (qlm_cfg.s.qlm_cfg == 2)
				return CVMX_HELPER_INTERFACE_MODE_SGMII;
			else if (qlm_cfg.s.qlm_cfg == 3)
				return CVMX_HELPER_INTERFACE_MODE_XAUI;
			else
				return CVMX_HELPER_INTERFACE_MODE_DISABLED;
		}
	} else if (OCTEON_IS_MODEL(OCTEON_CNF71XX)) {
		if (interface == 0) {
			union cvmx_mio_qlmx_cfg qlm_cfg;
			qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(0));
			if (qlm_cfg.s.qlm_cfg == 2)
				return CVMX_HELPER_INTERFACE_MODE_SGMII;
		}
		return CVMX_HELPER_INTERFACE_MODE_DISABLED;
	}

	if (interface == 1 && OCTEON_IS_MODEL(OCTEON_CN63XX))
		return CVMX_HELPER_INTERFACE_MODE_DISABLED;

	mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));

	if (OCTEON_IS_MODEL(OCTEON_CN63XX)) {
		switch (mode.cn63xx.mode) {
		case 0:
			return CVMX_HELPER_INTERFACE_MODE_SGMII;
		case 1:
			return CVMX_HELPER_INTERFACE_MODE_XAUI;
		default:
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;
		}
	} else {
		if (!mode.s.en)
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;

		if (mode.s.type)
			return CVMX_HELPER_INTERFACE_MODE_GMII;
		else
			return CVMX_HELPER_INTERFACE_MODE_RGMII;
	}
}

/**
 * @INTERNAL
 * Return interface mode for CN7XXX.
 */
static cvmx_helper_interface_mode_t __cvmx_get_mode_cn7xxx(int interface)
{
	union cvmx_gmxx_inf_mode mode;

	mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));

	switch (interface) {
	case 0:
	case 1:
		switch (mode.cn68xx.mode) {
		case 0:
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;
		case 1:
		case 2:
			return CVMX_HELPER_INTERFACE_MODE_SGMII;
		case 3:
			return CVMX_HELPER_INTERFACE_MODE_XAUI;
		default:
			return CVMX_HELPER_INTERFACE_MODE_SGMII;
		}
	case 2:
		return CVMX_HELPER_INTERFACE_MODE_NPI;
	case 3:
		return CVMX_HELPER_INTERFACE_MODE_LOOP;
	case 4:
		return CVMX_HELPER_INTERFACE_MODE_RGMII;
	default:
		return CVMX_HELPER_INTERFACE_MODE_DISABLED;
	}
}

/**
 * Get the operating mode of an interface. Depending on the Octeon
 * chip and configuration, this function returns an enumeration
 * of the type of packet I/O supported by an interface.
 *
 * @interface: Interface to probe
 *
 * Returns Mode of the interface. Unknown or unsupported interfaces return
 *	   DISABLED.
 */
cvmx_helper_interface_mode_t cvmx_helper_interface_get_mode(int interface)
{
	union cvmx_gmxx_inf_mode mode;

	if (interface < 0 ||
	    interface >= cvmx_helper_get_number_of_interfaces())
		return CVMX_HELPER_INTERFACE_MODE_DISABLED;

	/*
	 * OCTEON III models
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN7XXX))
		return __cvmx_get_mode_cn7xxx(interface);

	/*
	 * Octeon II models
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF71XX))
		return __cvmx_get_mode_octeon2(interface);

	/*
	 * Octeon and Octeon Plus models
	 */
	if (interface == 2)
		return CVMX_HELPER_INTERFACE_MODE_NPI;

	if (interface == 3) {
		if (OCTEON_IS_MODEL(OCTEON_CN56XX)
		    || OCTEON_IS_MODEL(OCTEON_CN52XX))
			return CVMX_HELPER_INTERFACE_MODE_LOOP;
		else
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;
	}

	if (interface == 0
	    && cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CN3005_EVB_HS5
	    && cvmx_sysinfo_get()->board_rev_major == 1) {
		/*
		 * Lie about interface type of CN3005 board.  This
		 * board has a switch on port 1 like the other
		 * evaluation boards, but it is connected over RGMII
		 * instead of GMII.  Report GMII mode so that the
		 * speed is forced to 1 Gbit full duplex.  Other than
		 * some initial configuration (which does not use the
		 * output of this function) there is no difference in
		 * setup between GMII and RGMII modes.
		 */
		return CVMX_HELPER_INTERFACE_MODE_GMII;
	}

	/* Interface 1 is always disabled on CN31XX and CN30XX */
	if ((interface == 1)
	    && (OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN30XX)
		|| OCTEON_IS_MODEL(OCTEON_CN50XX)
		|| OCTEON_IS_MODEL(OCTEON_CN52XX)))
		return CVMX_HELPER_INTERFACE_MODE_DISABLED;

	mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));

	if (OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN52XX)) {
		switch (mode.cn56xx.mode) {
		case 0:
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;
		case 1:
			return CVMX_HELPER_INTERFACE_MODE_XAUI;
		case 2:
			return CVMX_HELPER_INTERFACE_MODE_SGMII;
		case 3:
			return CVMX_HELPER_INTERFACE_MODE_PICMG;
		default:
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;
		}
	} else {
		if (!mode.s.en)
			return CVMX_HELPER_INTERFACE_MODE_DISABLED;

		if (mode.s.type) {
			if (OCTEON_IS_MODEL(OCTEON_CN38XX)
			    || OCTEON_IS_MODEL(OCTEON_CN58XX))
				return CVMX_HELPER_INTERFACE_MODE_SPI;
			else
				return CVMX_HELPER_INTERFACE_MODE_GMII;
		} else
			return CVMX_HELPER_INTERFACE_MODE_RGMII;
	}
}
EXPORT_SYMBOL_GPL(cvmx_helper_interface_get_mode);

/**
 * Configure the IPD/PIP tagging and QoS options for a specific
 * port. This function determines the POW work queue entry
 * contents for a port. The setup performed here is controlled by
 * the defines in executive-config.h.
 *
 * @ipd_port: Port to configure. This follows the IPD numbering, not the
 *		   per interface numbering
 *
 * Returns Zero on success, negative on failure
 */
static int __cvmx_helper_port_setup_ipd(int ipd_port)
{
	union cvmx_pip_prt_cfgx port_config;
	union cvmx_pip_prt_tagx tag_config;

	port_config.u64 = cvmx_read_csr(CVMX_PIP_PRT_CFGX(ipd_port));
	tag_config.u64 = cvmx_read_csr(CVMX_PIP_PRT_TAGX(ipd_port));

	/* Have each port go to a different POW queue */
	port_config.s.qos = ipd_port & 0x7;

	/* Process the headers and place the IP header in the work queue */
	port_config.s.mode = CVMX_HELPER_INPUT_PORT_SKIP_MODE;

	tag_config.s.ip6_src_flag = CVMX_HELPER_INPUT_TAG_IPV6_SRC_IP;
	tag_config.s.ip6_dst_flag = CVMX_HELPER_INPUT_TAG_IPV6_DST_IP;
	tag_config.s.ip6_sprt_flag = CVMX_HELPER_INPUT_TAG_IPV6_SRC_PORT;
	tag_config.s.ip6_dprt_flag = CVMX_HELPER_INPUT_TAG_IPV6_DST_PORT;
	tag_config.s.ip6_nxth_flag = CVMX_HELPER_INPUT_TAG_IPV6_NEXT_HEADER;
	tag_config.s.ip4_src_flag = CVMX_HELPER_INPUT_TAG_IPV4_SRC_IP;
	tag_config.s.ip4_dst_flag = CVMX_HELPER_INPUT_TAG_IPV4_DST_IP;
	tag_config.s.ip4_sprt_flag = CVMX_HELPER_INPUT_TAG_IPV4_SRC_PORT;
	tag_config.s.ip4_dprt_flag = CVMX_HELPER_INPUT_TAG_IPV4_DST_PORT;
	tag_config.s.ip4_pctl_flag = CVMX_HELPER_INPUT_TAG_IPV4_PROTOCOL;
	tag_config.s.inc_prt_flag = CVMX_HELPER_INPUT_TAG_INPUT_PORT;
	tag_config.s.tcp6_tag_type = CVMX_HELPER_INPUT_TAG_TYPE;
	tag_config.s.tcp4_tag_type = CVMX_HELPER_INPUT_TAG_TYPE;
	tag_config.s.ip6_tag_type = CVMX_HELPER_INPUT_TAG_TYPE;
	tag_config.s.ip4_tag_type = CVMX_HELPER_INPUT_TAG_TYPE;
	tag_config.s.non_tag_type = CVMX_HELPER_INPUT_TAG_TYPE;
	/* Put all packets in group 0. Other groups can be used by the app */
	tag_config.s.grp = 0;

	cvmx_pip_config_port(ipd_port, port_config, tag_config);

	/* Give the user a chance to override our setting for each port */
	if (cvmx_override_ipd_port_setup)
		cvmx_override_ipd_port_setup(ipd_port);

	return 0;
}

/**
 * This function sets the interface_port_count[interface] correctly,
 * without modifying any hardware configuration.  Hardware setup of
 * the ports will be performed later.
 *
 * @interface: Interface to probe
 *
 * Returns Zero on success, negative on failure
 */
int cvmx_helper_interface_enumerate(int interface)
{
	switch (cvmx_helper_interface_get_mode(interface)) {
		/* These types don't support ports to IPD/PKO */
	case CVMX_HELPER_INTERFACE_MODE_DISABLED:
	case CVMX_HELPER_INTERFACE_MODE_PCIE:
		interface_port_count[interface] = 0;
		break;
		/* XAUI is a single high speed port */
	case CVMX_HELPER_INTERFACE_MODE_XAUI:
		interface_port_count[interface] =
		    __cvmx_helper_xaui_enumerate(interface);
		break;
		/*
		 * RGMII/GMII/MII are all treated about the same. Most
		 * functions refer to these ports as RGMII.
		 */
	case CVMX_HELPER_INTERFACE_MODE_RGMII:
	case CVMX_HELPER_INTERFACE_MODE_GMII:
		interface_port_count[interface] =
		    __cvmx_helper_rgmii_enumerate(interface);
		break;
		/*
		 * SPI4 can have 1-16 ports depending on the device at
		 * the other end.
		 */
	case CVMX_HELPER_INTERFACE_MODE_SPI:
		interface_port_count[interface] =
		    __cvmx_helper_spi_enumerate(interface);
		break;
		/*
		 * SGMII can have 1-4 ports depending on how many are
		 * hooked up.
		 */
	case CVMX_HELPER_INTERFACE_MODE_SGMII:
	case CVMX_HELPER_INTERFACE_MODE_PICMG:
		interface_port_count[interface] =
		    __cvmx_helper_sgmii_enumerate(interface);
		break;
		/* PCI target Network Packet Interface */
	case CVMX_HELPER_INTERFACE_MODE_NPI:
		interface_port_count[interface] =
		    __cvmx_helper_npi_enumerate(interface);
		break;
		/*
		 * Special loopback only ports. These are not the same
		 * as other ports in loopback mode.
		 */
	case CVMX_HELPER_INTERFACE_MODE_LOOP:
		interface_port_count[interface] =
		    __cvmx_helper_loop_enumerate(interface);
		break;
	}

	interface_port_count[interface] =
	    __cvmx_helper_board_interface_probe(interface,
						interface_port_count
						[interface]);

	/* Make sure all global variables propagate to other cores */
	CVMX_SYNCWS;

	return 0;
}

/**
 * This function probes an interface to determine the actual
 * number of hardware ports connected to it. It doesn't setup the
 * ports or enable them. The main goal here is to set the global
 * interface_port_count[interface] correctly. Hardware setup of the
 * ports will be performed later.
 *
 * @interface: Interface to probe
 *
 * Returns Zero on success, negative on failure
 */
int cvmx_helper_interface_probe(int interface)
{
	cvmx_helper_interface_enumerate(interface);
	/* At this stage in the game we don't want packets to be moving yet.
	   The following probe calls should perform hardware setup
	   needed to determine port counts. Receive must still be disabled */
	switch (cvmx_helper_interface_get_mode(interface)) {
		/* These types don't support ports to IPD/PKO */
	case CVMX_HELPER_INTERFACE_MODE_DISABLED:
	case CVMX_HELPER_INTERFACE_MODE_PCIE:
		break;
		/* XAUI is a single high speed port */
	case CVMX_HELPER_INTERFACE_MODE_XAUI:
		__cvmx_helper_xaui_probe(interface);
		break;
		/*
		 * RGMII/GMII/MII are all treated about the same. Most
		 * functions refer to these ports as RGMII.
		 */
	case CVMX_HELPER_INTERFACE_MODE_RGMII:
	case CVMX_HELPER_INTERFACE_MODE_GMII:
		__cvmx_helper_rgmii_probe(interface);
		break;
		/*
		 * SPI4 can have 1-16 ports depending on the device at
		 * the other end.
		 */
	case CVMX_HELPER_INTERFACE_MODE_SPI:
		__cvmx_helper_spi_probe(interface);
		break;
		/*
		 * SGMII can have 1-4 ports depending on how many are
		 * hooked up.
		 */
	case CVMX_HELPER_INTERFACE_MODE_SGMII:
	case CVMX_HELPER_INTERFACE_MODE_PICMG:
		__cvmx_helper_sgmii_probe(interface);
		break;
		/* PCI target Network Packet Interface */
	case CVMX_HELPER_INTERFACE_MODE_NPI:
		__cvmx_helper_npi_probe(interface);
		break;
		/*
		 * Special loopback only ports. These are not the same
		 * as other ports in loopback mode.
		 */
	case CVMX_HELPER_INTERFACE_MODE_LOOP:
		__cvmx_helper_loop_probe(interface);
		break;
	}

	/* Make sure all global variables propagate to other cores */
	CVMX_SYNCWS;

	return 0;
}

/**
 * Setup the IPD/PIP for the ports on an interface. Packet
 * classification and tagging are set for every port on the
 * interface. The number of ports on the interface must already
 * have been probed.
 *
 * @interface: Interface to setup IPD/PIP for
 *
 * Returns Zero on success, negative on failure
 */
static int __cvmx_helper_interface_setup_ipd(int interface)
{
	int ipd_port = cvmx_helper_get_ipd_port(interface, 0);
	int num_ports = interface_port_count[interface];

	while (num_ports--) {
		__cvmx_helper_port_setup_ipd(ipd_port);
		ipd_port++;
	}
	return 0;
}

/**
 * Setup global setting for IPD/PIP not related to a specific
 * interface or port. This must be called before IPD is enabled.
 *
 * Returns Zero on success, negative on failure.
 */
static int __cvmx_helper_global_setup_ipd(void)
{
	/* Setup the global packet input options */
	cvmx_ipd_config(CVMX_FPA_PACKET_POOL_SIZE / 8,
			CVMX_HELPER_FIRST_MBUFF_SKIP / 8,
			CVMX_HELPER_NOT_FIRST_MBUFF_SKIP / 8,
			/* The +8 is to account for the next ptr */
			(CVMX_HELPER_FIRST_MBUFF_SKIP + 8) / 128,
			/* The +8 is to account for the next ptr */
			(CVMX_HELPER_NOT_FIRST_MBUFF_SKIP + 8) / 128,
			CVMX_FPA_WQE_POOL,
			CVMX_IPD_OPC_MODE_STT,
			CVMX_HELPER_ENABLE_BACK_PRESSURE);
	return 0;
}

/**
 * Setup the PKO for the ports on an interface. The number of
 * queues per port and the priority of each PKO output queue
 * is set here. PKO must be disabled when this function is called.
 *
 * @interface: Interface to setup PKO for
 *
 * Returns Zero on success, negative on failure
 */
static int __cvmx_helper_interface_setup_pko(int interface)
{
	/*
	 * Each packet output queue has an associated priority. The
	 * higher the priority, the more often it can send a packet. A
	 * priority of 8 means it can send in all 8 rounds of
	 * contention. We're going to make each queue one less than
	 * the last.  The vector of priorities has been extended to
	 * support CN5xxx CPUs, where up to 16 queues can be
	 * associated to a port.  To keep backward compatibility we
	 * don't change the initial 8 priorities and replicate them in
	 * the second half.  With per-core PKO queues (PKO lockless
	 * operation) all queues have the same priority.
	 */
	uint64_t priorities[16] =
	    { 8, 7, 6, 5, 4, 3, 2, 1, 8, 7, 6, 5, 4, 3, 2, 1 };

	/*
	 * Setup the IPD/PIP and PKO for the ports discovered
	 * above. Here packet classification, tagging and output
	 * priorities are set.
	 */
	int ipd_port = cvmx_helper_get_ipd_port(interface, 0);
	int num_ports = interface_port_count[interface];
	while (num_ports--) {
		/*
		 * Give the user a chance to override the per queue
		 * priorities.
		 */
		if (cvmx_override_pko_queue_priority)
			cvmx_override_pko_queue_priority(ipd_port, priorities);

		cvmx_pko_config_port(ipd_port,
				     cvmx_pko_get_base_queue_per_core(ipd_port,
								      0),
				     cvmx_pko_get_num_queues(ipd_port),
				     priorities);
		ipd_port++;
	}
	return 0;
}

/**
 * Setup global setting for PKO not related to a specific
 * interface or port. This must be called before PKO is enabled.
 *
 * Returns Zero on success, negative on failure.
 */
static int __cvmx_helper_global_setup_pko(void)
{
	/*
	 * Disable tagwait FAU timeout. This needs to be done before
	 * anyone might start packet output using tags.
	 */
	union cvmx_iob_fau_timeout fau_to;
	fau_to.u64 = 0;
	fau_to.s.tout_val = 0xfff;
	fau_to.s.tout_enb = 0;
	cvmx_write_csr(CVMX_IOB_FAU_TIMEOUT, fau_to.u64);

	if (OCTEON_IS_MODEL(OCTEON_CN68XX)) {
		union cvmx_pko_reg_min_pkt min_pkt;

		min_pkt.u64 = 0;
		min_pkt.s.size1 = 59;
		min_pkt.s.size2 = 59;
		min_pkt.s.size3 = 59;
		min_pkt.s.size4 = 59;
		min_pkt.s.size5 = 59;
		min_pkt.s.size6 = 59;
		min_pkt.s.size7 = 59;
		cvmx_write_csr(CVMX_PKO_REG_MIN_PKT, min_pkt.u64);
	}

	return 0;
}

/**
 * Setup global backpressure setting.
 *
 * Returns Zero on success, negative on failure
 */
static int __cvmx_helper_global_setup_backpressure(void)
{
#if CVMX_HELPER_DISABLE_RGMII_BACKPRESSURE
	/* Disable backpressure if configured to do so */
	/* Disable backpressure (pause frame) generation */
	int num_interfaces = cvmx_helper_get_number_of_interfaces();
	int interface;
	for (interface = 0; interface < num_interfaces; interface++) {
		switch (cvmx_helper_interface_get_mode(interface)) {
		case CVMX_HELPER_INTERFACE_MODE_DISABLED:
		case CVMX_HELPER_INTERFACE_MODE_PCIE:
		case CVMX_HELPER_INTERFACE_MODE_NPI:
		case CVMX_HELPER_INTERFACE_MODE_LOOP:
		case CVMX_HELPER_INTERFACE_MODE_XAUI:
			break;
		case CVMX_HELPER_INTERFACE_MODE_RGMII:
		case CVMX_HELPER_INTERFACE_MODE_GMII:
		case CVMX_HELPER_INTERFACE_MODE_SPI:
		case CVMX_HELPER_INTERFACE_MODE_SGMII:
		case CVMX_HELPER_INTERFACE_MODE_PICMG:
			cvmx_gmx_set_backpressure_override(interface, 0xf);
			break;
		}
	}
#endif

	return 0;
}

/**
 * Enable packet input/output from the hardware. This function is
 * called after all internal setup is complete and IPD is enabled.
 * After this function completes, packets will be accepted from the
 * hardware ports. PKO should still be disabled to make sure packets
 * aren't sent out partially setup hardware.
 *
 * @interface: Interface to enable
 *
 * Returns Zero on success, negative on failure
 */
static int __cvmx_helper_packet_hardware_enable(int interface)
{
	int result = 0;
	switch (cvmx_helper_interface_get_mode(interface)) {
		/* These types don't support ports to IPD/PKO */
	case CVMX_HELPER_INTERFACE_MODE_DISABLED:
	case CVMX_HELPER_INTERFACE_MODE_PCIE:
		/* Nothing to do */
		break;
		/* XAUI is a single high speed port */
	case CVMX_HELPER_INTERFACE_MODE_XAUI:
		result = __cvmx_helper_xaui_enable(interface);
		break;
		/*
		 * RGMII/GMII/MII are all treated about the same. Most
		 * functions refer to these ports as RGMII
		 */
	case CVMX_HELPER_INTERFACE_MODE_RGMII:
	case CVMX_HELPER_INTERFACE_MODE_GMII:
		result = __cvmx_helper_rgmii_enable(interface);
		break;
		/*
		 * SPI4 can have 1-16 ports depending on the device at
		 * the other end
		 */
	case CVMX_HELPER_INTERFACE_MODE_SPI:
		result = __cvmx_helper_spi_enable(interface);
		break;
		/*
		 * SGMII can have 1-4 ports depending on how many are
		 * hooked up
		 */
	case CVMX_HELPER_INTERFACE_MODE_SGMII:
	case CVMX_HELPER_INTERFACE_MODE_PICMG:
		result = __cvmx_helper_sgmii_enable(interface);
		break;
		/* PCI target Network Packet Interface */
	case CVMX_HELPER_INTERFACE_MODE_NPI:
		result = __cvmx_helper_npi_enable(interface);
		break;
		/*
		 * Special loopback only ports. These are not the same
		 * as other ports in loopback mode
		 */
	case CVMX_HELPER_INTERFACE_MODE_LOOP:
		result = __cvmx_helper_loop_enable(interface);
		break;
	}
	result |= __cvmx_helper_board_hardware_enable(interface);
	return result;
}

/**
 * Function to adjust internal IPD pointer alignments
 *
 * Returns 0 on success
 *	   !0 on failure
 */
int __cvmx_helper_errata_fix_ipd_ptr_alignment(void)
{
#define FIX_IPD_FIRST_BUFF_PAYLOAD_BYTES \
     (CVMX_FPA_PACKET_POOL_SIZE-8-CVMX_HELPER_FIRST_MBUFF_SKIP)
#define FIX_IPD_NON_FIRST_BUFF_PAYLOAD_BYTES \
	(CVMX_FPA_PACKET_POOL_SIZE-8-CVMX_HELPER_NOT_FIRST_MBUFF_SKIP)
#define FIX_IPD_OUTPORT 0
	/* Ports 0-15 are interface 0, 16-31 are interface 1 */
#define INTERFACE(port) (port >> 4)
#define INDEX(port) (port & 0xf)
	uint64_t *p64;
	cvmx_pko_command_word0_t pko_command;
	union cvmx_buf_ptr g_buffer, pkt_buffer;
	cvmx_wqe_t *work;
	int size, num_segs = 0, wqe_pcnt, pkt_pcnt;
	union cvmx_gmxx_prtx_cfg gmx_cfg;
	int retry_cnt;
	int retry_loop_cnt;
	int i;

	/* Save values for restore at end */
	uint64_t prtx_cfg =
	    cvmx_read_csr(CVMX_GMXX_PRTX_CFG
			  (INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)));
	uint64_t tx_ptr_en =
	    cvmx_read_csr(CVMX_ASXX_TX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)));
	uint64_t rx_ptr_en =
	    cvmx_read_csr(CVMX_ASXX_RX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)));
	uint64_t rxx_jabber =
	    cvmx_read_csr(CVMX_GMXX_RXX_JABBER
			  (INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)));
	uint64_t frame_max =
	    cvmx_read_csr(CVMX_GMXX_RXX_FRM_MAX
			  (INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)));

	/* Configure port to gig FDX as required for loopback mode */
	cvmx_helper_rgmii_internal_loopback(FIX_IPD_OUTPORT);

	/*
	 * Disable reception on all ports so if traffic is present it
	 * will not interfere.
	 */
	cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)), 0);

	__delay(100000000ull);

	for (retry_loop_cnt = 0; retry_loop_cnt < 10; retry_loop_cnt++) {
		retry_cnt = 100000;
		wqe_pcnt = cvmx_read_csr(CVMX_IPD_PTR_COUNT);
		pkt_pcnt = (wqe_pcnt >> 7) & 0x7f;
		wqe_pcnt &= 0x7f;

		num_segs = (2 + pkt_pcnt - wqe_pcnt) & 3;

		if (num_segs == 0)
			goto fix_ipd_exit;

		num_segs += 1;

		size =
		    FIX_IPD_FIRST_BUFF_PAYLOAD_BYTES +
		    ((num_segs - 1) * FIX_IPD_NON_FIRST_BUFF_PAYLOAD_BYTES) -
		    (FIX_IPD_NON_FIRST_BUFF_PAYLOAD_BYTES / 2);

		cvmx_write_csr(CVMX_ASXX_PRT_LOOP(INTERFACE(FIX_IPD_OUTPORT)),
			       1 << INDEX(FIX_IPD_OUTPORT));
		CVMX_SYNC;

		g_buffer.u64 = 0;
		g_buffer.s.addr =
		    cvmx_ptr_to_phys(cvmx_fpa_alloc(CVMX_FPA_WQE_POOL));
		if (g_buffer.s.addr == 0) {
			cvmx_dprintf("WARNING: FIX_IPD_PTR_ALIGNMENT "
				     "buffer allocation failure.\n");
			goto fix_ipd_exit;
		}

		g_buffer.s.pool = CVMX_FPA_WQE_POOL;
		g_buffer.s.size = num_segs;

		pkt_buffer.u64 = 0;
		pkt_buffer.s.addr =
		    cvmx_ptr_to_phys(cvmx_fpa_alloc(CVMX_FPA_PACKET_POOL));
		if (pkt_buffer.s.addr == 0) {
			cvmx_dprintf("WARNING: FIX_IPD_PTR_ALIGNMENT "
				     "buffer allocation failure.\n");
			goto fix_ipd_exit;
		}
		pkt_buffer.s.i = 1;
		pkt_buffer.s.pool = CVMX_FPA_PACKET_POOL;
		pkt_buffer.s.size = FIX_IPD_FIRST_BUFF_PAYLOAD_BYTES;

		p64 = (uint64_t *) cvmx_phys_to_ptr(pkt_buffer.s.addr);
		p64[0] = 0xffffffffffff0000ull;
		p64[1] = 0x08004510ull;
		p64[2] = ((uint64_t) (size - 14) << 48) | 0x5ae740004000ull;
		p64[3] = 0x3a5fc0a81073c0a8ull;

		for (i = 0; i < num_segs; i++) {
			if (i > 0)
				pkt_buffer.s.size =
				    FIX_IPD_NON_FIRST_BUFF_PAYLOAD_BYTES;

			if (i == (num_segs - 1))
				pkt_buffer.s.i = 0;

			*(uint64_t *) cvmx_phys_to_ptr(g_buffer.s.addr +
						       8 * i) = pkt_buffer.u64;
		}

		/* Build the PKO command */
		pko_command.u64 = 0;
		pko_command.s.segs = num_segs;
		pko_command.s.total_bytes = size;
		pko_command.s.dontfree = 0;
		pko_command.s.gather = 1;

		gmx_cfg.u64 =
		    cvmx_read_csr(CVMX_GMXX_PRTX_CFG
				  (INDEX(FIX_IPD_OUTPORT),
				   INTERFACE(FIX_IPD_OUTPORT)));
		gmx_cfg.s.en = 1;
		cvmx_write_csr(CVMX_GMXX_PRTX_CFG
			       (INDEX(FIX_IPD_OUTPORT),
				INTERFACE(FIX_IPD_OUTPORT)), gmx_cfg.u64);
		cvmx_write_csr(CVMX_ASXX_TX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)),
			       1 << INDEX(FIX_IPD_OUTPORT));
		cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)),
			       1 << INDEX(FIX_IPD_OUTPORT));

		cvmx_write_csr(CVMX_GMXX_RXX_JABBER
			       (INDEX(FIX_IPD_OUTPORT),
				INTERFACE(FIX_IPD_OUTPORT)), 65392 - 14 - 4);
		cvmx_write_csr(CVMX_GMXX_RXX_FRM_MAX
			       (INDEX(FIX_IPD_OUTPORT),
				INTERFACE(FIX_IPD_OUTPORT)), 65392 - 14 - 4);

		cvmx_pko_send_packet_prepare(FIX_IPD_OUTPORT,
					     cvmx_pko_get_base_queue
					     (FIX_IPD_OUTPORT),
					     CVMX_PKO_LOCK_CMD_QUEUE);
		cvmx_pko_send_packet_finish(FIX_IPD_OUTPORT,
					    cvmx_pko_get_base_queue
					    (FIX_IPD_OUTPORT), pko_command,
					    g_buffer, CVMX_PKO_LOCK_CMD_QUEUE);

		CVMX_SYNC;

		do {
			work = cvmx_pow_work_request_sync(CVMX_POW_WAIT);
			retry_cnt--;
		} while ((work == NULL) && (retry_cnt > 0));

		if (!retry_cnt)
			cvmx_dprintf("WARNING: FIX_IPD_PTR_ALIGNMENT "
				     "get_work() timeout occurred.\n");

		/* Free packet */
		if (work)
			cvmx_helper_free_packet_data(work);
	}

fix_ipd_exit:

	/* Return CSR configs to saved values */
	cvmx_write_csr(CVMX_GMXX_PRTX_CFG
		       (INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)),
		       prtx_cfg);
	cvmx_write_csr(CVMX_ASXX_TX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)),
		       tx_ptr_en);
	cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)),
		       rx_ptr_en);
	cvmx_write_csr(CVMX_GMXX_RXX_JABBER
		       (INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)),
		       rxx_jabber);
	cvmx_write_csr(CVMX_GMXX_RXX_FRM_MAX
		       (INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)),
		       frame_max);
	cvmx_write_csr(CVMX_ASXX_PRT_LOOP(INTERFACE(FIX_IPD_OUTPORT)), 0);

	CVMX_SYNC;
	if (num_segs)
		cvmx_dprintf("WARNING: FIX_IPD_PTR_ALIGNMENT failed.\n");

	return !!num_segs;

}

/**
 * Called after all internal packet IO paths are setup. This
 * function enables IPD/PIP and begins packet input and output.
 *
 * Returns Zero on success, negative on failure
 */
int cvmx_helper_ipd_and_packet_input_enable(void)
{
	int num_interfaces;
	int interface;

	/* Enable IPD */
	cvmx_ipd_enable();

	/*
	 * Time to enable hardware ports packet input and output. Note
	 * that at this point IPD/PIP must be fully functional and PKO
	 * must be disabled
	 */
	num_interfaces = cvmx_helper_get_number_of_interfaces();
	for (interface = 0; interface < num_interfaces; interface++) {
		if (cvmx_helper_ports_on_interface(interface) > 0)
			__cvmx_helper_packet_hardware_enable(interface);
	}

	/* Finally enable PKO now that the entire path is up and running */
	cvmx_pko_enable();

	if ((OCTEON_IS_MODEL(OCTEON_CN31XX_PASS1)
	     || OCTEON_IS_MODEL(OCTEON_CN30XX_PASS1))
	    && (cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM))
		__cvmx_helper_errata_fix_ipd_ptr_alignment();
	return 0;
}
EXPORT_SYMBOL_GPL(cvmx_helper_ipd_and_packet_input_enable);

/**
 * Initialize the PIP, IPD, and PKO hardware to support
 * simple priority based queues for the ethernet ports. Each
 * port is configured with a number of priority queues based
 * on CVMX_PKO_QUEUES_PER_PORT_* where each queue is lower
 * priority than the previous.
 *
 * Returns Zero on success, non-zero on failure
 */
int cvmx_helper_initialize_packet_io_global(void)
{
	int result = 0;
	int interface;
	union cvmx_l2c_cfg l2c_cfg;
	union cvmx_smix_en smix_en;
	const int num_interfaces = cvmx_helper_get_number_of_interfaces();

	/*
	 * CN52XX pass 1: Due to a bug in 2nd order CDR, it needs to
	 * be disabled.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_0))
		__cvmx_helper_errata_qlm_disable_2nd_order_cdr(1);

	/*
	 * Tell L2 to give the IOB statically higher priority compared
	 * to the cores. This avoids conditions where IO blocks might
	 * be starved under very high L2 loads.
	 */
	l2c_cfg.u64 = cvmx_read_csr(CVMX_L2C_CFG);
	l2c_cfg.s.lrf_arb_mode = 0;
	l2c_cfg.s.rfb_arb_mode = 0;
	cvmx_write_csr(CVMX_L2C_CFG, l2c_cfg.u64);

	/* Make sure SMI/MDIO is enabled so we can query PHYs */
	smix_en.u64 = cvmx_read_csr(CVMX_SMIX_EN(0));
	if (!smix_en.s.en) {
		smix_en.s.en = 1;
		cvmx_write_csr(CVMX_SMIX_EN(0), smix_en.u64);
	}

	/* Newer chips actually have two SMI/MDIO interfaces */
	if (!OCTEON_IS_MODEL(OCTEON_CN3XXX) &&
	    !OCTEON_IS_MODEL(OCTEON_CN58XX) &&
	    !OCTEON_IS_MODEL(OCTEON_CN50XX)) {
		smix_en.u64 = cvmx_read_csr(CVMX_SMIX_EN(1));
		if (!smix_en.s.en) {
			smix_en.s.en = 1;
			cvmx_write_csr(CVMX_SMIX_EN(1), smix_en.u64);
		}
	}

	cvmx_pko_initialize_global();
	for (interface = 0; interface < num_interfaces; interface++) {
		result |= cvmx_helper_interface_probe(interface);
		if (cvmx_helper_ports_on_interface(interface) > 0)
			cvmx_dprintf("Interface %d has %d ports (%s)\n",
				     interface,
				     cvmx_helper_ports_on_interface(interface),
				     cvmx_helper_interface_mode_to_string
				     (cvmx_helper_interface_get_mode
				      (interface)));
		result |= __cvmx_helper_interface_setup_ipd(interface);
		result |= __cvmx_helper_interface_setup_pko(interface);
	}

	result |= __cvmx_helper_global_setup_ipd();
	result |= __cvmx_helper_global_setup_pko();

	/* Enable any flow control and backpressure */
	result |= __cvmx_helper_global_setup_backpressure();

#if CVMX_HELPER_ENABLE_IPD
	result |= cvmx_helper_ipd_and_packet_input_enable();
#endif
	return result;
}
EXPORT_SYMBOL_GPL(cvmx_helper_initialize_packet_io_global);

/**
 * Does core local initialization for packet io
 *
 * Returns Zero on success, non-zero on failure
 */
int cvmx_helper_initialize_packet_io_local(void)
{
	return cvmx_pko_initialize_local();
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
cvmx_helper_link_info_t cvmx_helper_link_get(int ipd_port)
{
	cvmx_helper_link_info_t result;
	int interface = cvmx_helper_get_interface_num(ipd_port);
	int index = cvmx_helper_get_interface_index_num(ipd_port);

	/* The default result will be a down link unless the code below
	   changes it */
	result.u64 = 0;

	if (index >= cvmx_helper_ports_on_interface(interface))
		return result;

	switch (cvmx_helper_interface_get_mode(interface)) {
	case CVMX_HELPER_INTERFACE_MODE_DISABLED:
	case CVMX_HELPER_INTERFACE_MODE_PCIE:
		/* Network links are not supported */
		break;
	case CVMX_HELPER_INTERFACE_MODE_XAUI:
		result = __cvmx_helper_xaui_link_get(ipd_port);
		break;
	case CVMX_HELPER_INTERFACE_MODE_GMII:
		if (index == 0)
			result = __cvmx_helper_rgmii_link_get(ipd_port);
		else {
			result.s.full_duplex = 1;
			result.s.link_up = 1;
			result.s.speed = 1000;
		}
		break;
	case CVMX_HELPER_INTERFACE_MODE_RGMII:
		result = __cvmx_helper_rgmii_link_get(ipd_port);
		break;
	case CVMX_HELPER_INTERFACE_MODE_SPI:
		result = __cvmx_helper_spi_link_get(ipd_port);
		break;
	case CVMX_HELPER_INTERFACE_MODE_SGMII:
	case CVMX_HELPER_INTERFACE_MODE_PICMG:
		result = __cvmx_helper_sgmii_link_get(ipd_port);
		break;
	case CVMX_HELPER_INTERFACE_MODE_NPI:
	case CVMX_HELPER_INTERFACE_MODE_LOOP:
		/* Network links are not supported */
		break;
	}
	return result;
}
EXPORT_SYMBOL_GPL(cvmx_helper_link_get);

/**
 * Configure an IPD/PKO port for the specified link state. This
 * function does not influence auto negotiation at the PHY level.
 * The passed link state must always match the link state returned
 * by cvmx_helper_link_get().
 *
 * @ipd_port:  IPD/PKO port to configure
 * @link_info: The new link state
 *
 * Returns Zero on success, negative on failure
 */
int cvmx_helper_link_set(int ipd_port, cvmx_helper_link_info_t link_info)
{
	int result = -1;
	int interface = cvmx_helper_get_interface_num(ipd_port);
	int index = cvmx_helper_get_interface_index_num(ipd_port);

	if (index >= cvmx_helper_ports_on_interface(interface))
		return -1;

	switch (cvmx_helper_interface_get_mode(interface)) {
	case CVMX_HELPER_INTERFACE_MODE_DISABLED:
	case CVMX_HELPER_INTERFACE_MODE_PCIE:
		break;
	case CVMX_HELPER_INTERFACE_MODE_XAUI:
		result = __cvmx_helper_xaui_link_set(ipd_port, link_info);
		break;
		/*
		 * RGMII/GMII/MII are all treated about the same. Most
		 * functions refer to these ports as RGMII.
		 */
	case CVMX_HELPER_INTERFACE_MODE_RGMII:
	case CVMX_HELPER_INTERFACE_MODE_GMII:
		result = __cvmx_helper_rgmii_link_set(ipd_port, link_info);
		break;
	case CVMX_HELPER_INTERFACE_MODE_SPI:
		result = __cvmx_helper_spi_link_set(ipd_port, link_info);
		break;
	case CVMX_HELPER_INTERFACE_MODE_SGMII:
	case CVMX_HELPER_INTERFACE_MODE_PICMG:
		result = __cvmx_helper_sgmii_link_set(ipd_port, link_info);
		break;
	case CVMX_HELPER_INTERFACE_MODE_NPI:
	case CVMX_HELPER_INTERFACE_MODE_LOOP:
		break;
	}
	return result;
}
EXPORT_SYMBOL_GPL(cvmx_helper_link_set);

/**
 * Configure a port for internal and/or external loopback. Internal loopback
 * causes packets sent by the port to be received by Octeon. External loopback
 * causes packets received from the wire to sent out again.
 *
 * @ipd_port: IPD/PKO port to loopback.
 * @enable_internal:
 *		   Non zero if you want internal loopback
 * @enable_external:
 *		   Non zero if you want external loopback
 *
 * Returns Zero on success, negative on failure.
 */
int cvmx_helper_configure_loopback(int ipd_port, int enable_internal,
				   int enable_external)
{
	int result = -1;
	int interface = cvmx_helper_get_interface_num(ipd_port);
	int index = cvmx_helper_get_interface_index_num(ipd_port);

	if (index >= cvmx_helper_ports_on_interface(interface))
		return -1;

	switch (cvmx_helper_interface_get_mode(interface)) {
	case CVMX_HELPER_INTERFACE_MODE_DISABLED:
	case CVMX_HELPER_INTERFACE_MODE_PCIE:
	case CVMX_HELPER_INTERFACE_MODE_SPI:
	case CVMX_HELPER_INTERFACE_MODE_NPI:
	case CVMX_HELPER_INTERFACE_MODE_LOOP:
		break;
	case CVMX_HELPER_INTERFACE_MODE_XAUI:
		result =
		    __cvmx_helper_xaui_configure_loopback(ipd_port,
							  enable_internal,
							  enable_external);
		break;
	case CVMX_HELPER_INTERFACE_MODE_RGMII:
	case CVMX_HELPER_INTERFACE_MODE_GMII:
		result =
		    __cvmx_helper_rgmii_configure_loopback(ipd_port,
							   enable_internal,
							   enable_external);
		break;
	case CVMX_HELPER_INTERFACE_MODE_SGMII:
	case CVMX_HELPER_INTERFACE_MODE_PICMG:
		result =
		    __cvmx_helper_sgmii_configure_loopback(ipd_port,
							   enable_internal,
							   enable_external);
		break;
	}
	return result;
}
