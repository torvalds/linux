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

#ifndef __CVMX_HELPER_H__
#define __CVMX_HELPER_H__

#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-fpa.h>
#include <asm/octeon/cvmx-wqe.h>

typedef enum {
	CVMX_HELPER_INTERFACE_MODE_DISABLED,
	CVMX_HELPER_INTERFACE_MODE_RGMII,
	CVMX_HELPER_INTERFACE_MODE_GMII,
	CVMX_HELPER_INTERFACE_MODE_SPI,
	CVMX_HELPER_INTERFACE_MODE_PCIE,
	CVMX_HELPER_INTERFACE_MODE_XAUI,
	CVMX_HELPER_INTERFACE_MODE_SGMII,
	CVMX_HELPER_INTERFACE_MODE_PICMG,
	CVMX_HELPER_INTERFACE_MODE_NPI,
	CVMX_HELPER_INTERFACE_MODE_LOOP,
} cvmx_helper_interface_mode_t;

typedef union {
	uint64_t u64;
	struct {
		uint64_t reserved_20_63:44;
		uint64_t link_up:1;	    /**< Is the physical link up? */
		uint64_t full_duplex:1;	    /**< 1 if the link is full duplex */
		uint64_t speed:18;	    /**< Speed of the link in Mbps */
	} s;
} cvmx_helper_link_info_t;

#include <asm/octeon/cvmx-helper-errata.h>
#include <asm/octeon/cvmx-helper-loop.h>
#include <asm/octeon/cvmx-helper-npi.h>
#include <asm/octeon/cvmx-helper-rgmii.h>
#include <asm/octeon/cvmx-helper-sgmii.h>
#include <asm/octeon/cvmx-helper-spi.h>
#include <asm/octeon/cvmx-helper-util.h>
#include <asm/octeon/cvmx-helper-xaui.h>

/**
 * cvmx_override_pko_queue_priority(int ipd_port, uint64_t
 * priorities[16]) is a function pointer. It is meant to allow
 * customization of the PKO queue priorities based on the port
 * number. Users should set this pointer to a function before
 * calling any cvmx-helper operations.
 */
extern void (*cvmx_override_pko_queue_priority) (int pko_port,
						 uint64_t priorities[16]);

/**
 * cvmx_override_ipd_port_setup(int ipd_port) is a function
 * pointer. It is meant to allow customization of the IPD port
 * setup before packet input/output comes online. It is called
 * after cvmx-helper does the default IPD configuration, but
 * before IPD is enabled. Users should set this pointer to a
 * function before calling any cvmx-helper operations.
 */
extern void (*cvmx_override_ipd_port_setup) (int ipd_port);

/**
 * This function enables the IPD and also enables the packet interfaces.
 * The packet interfaces (RGMII and SPI) must be enabled after the
 * IPD.	 This should be called by the user program after any additional
 * IPD configuration changes are made if CVMX_HELPER_ENABLE_IPD
 * is not set in the executive-config.h file.
 *
 * Returns 0 on success
 *	   -1 on failure
 */
extern int cvmx_helper_ipd_and_packet_input_enable(void);

/**
 * Initialize the PIP, IPD, and PKO hardware to support
 * simple priority based queues for the ethernet ports. Each
 * port is configured with a number of priority queues based
 * on CVMX_PKO_QUEUES_PER_PORT_* where each queue is lower
 * priority than the previous.
 *
 * Returns Zero on success, non-zero on failure
 */
extern int cvmx_helper_initialize_packet_io_global(void);

/**
 * Does core local initialization for packet io
 *
 * Returns Zero on success, non-zero on failure
 */
extern int cvmx_helper_initialize_packet_io_local(void);

/**
 * Returns the number of ports on the given interface.
 * The interface must be initialized before the port count
 * can be returned.
 *
 * @interface: Which interface to return port count for.
 *
 * Returns Port count for interface
 *	   -1 for uninitialized interface
 */
extern int cvmx_helper_ports_on_interface(int interface);

/**
 * Return the number of interfaces the chip has. Each interface
 * may have multiple ports. Most chips support two interfaces,
 * but the CNX0XX and CNX1XX are exceptions. These only support
 * one interface.
 *
 * Returns Number of interfaces on chip
 */
extern int cvmx_helper_get_number_of_interfaces(void);

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
extern cvmx_helper_interface_mode_t cvmx_helper_interface_get_mode(int
								   interface);

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
extern cvmx_helper_link_info_t cvmx_helper_link_get(int ipd_port);

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
extern int cvmx_helper_link_set(int ipd_port,
				cvmx_helper_link_info_t link_info);

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
extern int cvmx_helper_interface_probe(int interface);
extern int cvmx_helper_interface_enumerate(int interface);

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
extern int cvmx_helper_configure_loopback(int ipd_port, int enable_internal,
					  int enable_external);

#endif /* __CVMX_HELPER_H__ */
