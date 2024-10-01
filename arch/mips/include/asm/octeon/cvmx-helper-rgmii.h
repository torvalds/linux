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
 * @file
 *
 * Functions for RGMII/GMII/MII initialization, configuration,
 * and monitoring.
 *
 */
#ifndef __CVMX_HELPER_RGMII_H__
#define __CVMX_HELPER_RGMII_H__

/**
 * Probe RGMII ports and determine the number present
 *
 * @interface: Interface to probe
 *
 * Returns Number of RGMII/GMII/MII ports (0-4).
 */
extern int __cvmx_helper_rgmii_probe(int interface);
#define __cvmx_helper_rgmii_enumerate __cvmx_helper_rgmii_probe

/**
 * Put an RGMII interface in loopback mode. Internal packets sent
 * out will be received back again on the same port. Externally
 * received packets will echo back out.
 *
 * @port:   IPD port number to loop.
 */
extern void cvmx_helper_rgmii_internal_loopback(int port);

/**
 * Configure all of the ASX, GMX, and PKO registers required
 * to get RGMII to function on the supplied interface.
 *
 * @interface: PKO Interface to configure (0 or 1)
 *
 * Returns Zero on success
 */
extern int __cvmx_helper_rgmii_enable(int interface);

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
extern union cvmx_helper_link_info __cvmx_helper_rgmii_link_get(int ipd_port);

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
extern int __cvmx_helper_rgmii_link_set(int ipd_port,
					union cvmx_helper_link_info link_info);

#endif
