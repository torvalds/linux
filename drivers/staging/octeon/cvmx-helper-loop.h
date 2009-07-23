/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as published by
 * the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful,
 * but AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or NONINFRINGEMENT.
 * See the GNU General Public License for more details.
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
 * Functions for LOOP initialization, configuration,
 * and monitoring.
 *
 */
#ifndef __CVMX_HELPER_LOOP_H__
#define __CVMX_HELPER_LOOP_H__

/**
 * Probe a LOOP interface and determine the number of ports
 * connected to it. The LOOP interface should still be down after
 * this call.
 *
 * @interface: Interface to probe
 *
 * Returns Number of ports on the interface. Zero to disable.
 */
extern int __cvmx_helper_loop_probe(int interface);

/**
 * Bringup and enable a LOOP interface. After this call packet
 * I/O should be fully functional. This is called with IPD
 * enabled but PKO disabled.
 *
 * @interface: Interface to bring up
 *
 * Returns Zero on success, negative on failure
 */
extern int __cvmx_helper_loop_enable(int interface);

#endif
