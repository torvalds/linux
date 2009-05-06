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
 * Fixes and workaround for Octeon chip errata. This file
 * contains functions called by cvmx-helper to workaround known
 * chip errata. For the most part, code doesn't need to call
 * these functions directly.
 *
 */
#include <linux/module.h>

#include <asm/octeon/octeon.h>

#include <asm/octeon/cvmx-helper-jtag.h>

/**
 * Due to errata G-720, the 2nd order CDR circuit on CN52XX pass
 * 1 doesn't work properly. The following code disables 2nd order
 * CDR for the specified QLM.
 *
 * @qlm:    QLM to disable 2nd order CDR for.
 */
void __cvmx_helper_errata_qlm_disable_2nd_order_cdr(int qlm)
{
	int lane;
	cvmx_helper_qlm_jtag_init();
	/* We need to load all four lanes of the QLM, a total of 1072 bits */
	for (lane = 0; lane < 4; lane++) {
		/*
		 * Each lane has 268 bits. We need to set
		 * cfg_cdr_incx<67:64> = 3 and cfg_cdr_secord<77> =
		 * 1. All other bits are zero. Bits go in LSB first,
		 * so start off with the zeros for bits <63:0>.
		 */
		cvmx_helper_qlm_jtag_shift_zeros(qlm, 63 - 0 + 1);
		/* cfg_cdr_incx<67:64>=3 */
		cvmx_helper_qlm_jtag_shift(qlm, 67 - 64 + 1, 3);
		/* Zeros for bits <76:68> */
		cvmx_helper_qlm_jtag_shift_zeros(qlm, 76 - 68 + 1);
		/* cfg_cdr_secord<77>=1 */
		cvmx_helper_qlm_jtag_shift(qlm, 77 - 77 + 1, 1);
		/* Zeros for bits <267:78> */
		cvmx_helper_qlm_jtag_shift_zeros(qlm, 267 - 78 + 1);
	}
	cvmx_helper_qlm_jtag_update(qlm);
}
EXPORT_SYMBOL(__cvmx_helper_errata_qlm_disable_2nd_order_cdr);
