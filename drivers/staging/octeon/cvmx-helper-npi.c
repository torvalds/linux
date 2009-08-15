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
 * Functions for NPI initialization, configuration,
 * and monitoring.
 */
#include <asm/octeon/octeon.h>

#include "cvmx-config.h"

#include "cvmx-helper.h"

#include "cvmx-pip-defs.h"

/**
 * Probe a NPI interface and determine the number of ports
 * connected to it. The NPI interface should still be down
 * after this call.
 *
 * @interface: Interface to probe
 *
 * Returns Number of ports on the interface. Zero to disable.
 */
int __cvmx_helper_npi_probe(int interface)
{
#if CVMX_PKO_QUEUES_PER_PORT_PCI > 0
	if (OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX))
		return 4;
	else if (OCTEON_IS_MODEL(OCTEON_CN56XX)
		 && !OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X))
		/* The packet engines didn't exist before pass 2 */
		return 4;
	else if (OCTEON_IS_MODEL(OCTEON_CN52XX)
		 && !OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X))
		/* The packet engines didn't exist before pass 2 */
		return 4;
#if 0
	/*
	 * Technically CN30XX, CN31XX, and CN50XX contain packet
	 * engines, but nobody ever uses them. Since this is the case,
	 * we disable them here.
	 */
	else if (OCTEON_IS_MODEL(OCTEON_CN31XX)
		 || OCTEON_IS_MODEL(OCTEON_CN50XX))
		return 2;
	else if (OCTEON_IS_MODEL(OCTEON_CN30XX))
		return 1;
#endif
#endif
	return 0;
}

/**
 * Bringup and enable a NPI interface. After this call packet
 * I/O should be fully functional. This is called with IPD
 * enabled but PKO disabled.
 *
 * @interface: Interface to bring up
 *
 * Returns Zero on success, negative on failure
 */
int __cvmx_helper_npi_enable(int interface)
{
	/*
	 * On CN50XX, CN52XX, and CN56XX we need to disable length
	 * checking so packet < 64 bytes and jumbo frames don't get
	 * errors.
	 */
	if (!OCTEON_IS_MODEL(OCTEON_CN3XXX) &&
	    !OCTEON_IS_MODEL(OCTEON_CN58XX)) {
		int num_ports = cvmx_helper_ports_on_interface(interface);
		int port;
		for (port = 0; port < num_ports; port++) {
			union cvmx_pip_prt_cfgx port_cfg;
			int ipd_port =
			    cvmx_helper_get_ipd_port(interface, port);
			port_cfg.u64 =
			    cvmx_read_csr(CVMX_PIP_PRT_CFGX(ipd_port));
			port_cfg.s.maxerr_en = 0;
			port_cfg.s.minerr_en = 0;
			cvmx_write_csr(CVMX_PIP_PRT_CFGX(ipd_port),
				       port_cfg.u64);
		}
	}

	/* Enables are controlled by the remote host, so nothing to do here */
	return 0;
}
