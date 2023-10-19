/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2017 Cavium, Inc.
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

#ifndef __CVMX_L2D_DEFS_H__
#define __CVMX_L2D_DEFS_H__

#define CVMX_L2D_ERR	(CVMX_ADD_IO_SEG(0x0001180080000010ull))
#define CVMX_L2D_FUS3	(CVMX_ADD_IO_SEG(0x00011800800007B8ull))


union cvmx_l2d_err {
	uint64_t u64;
	struct cvmx_l2d_err_s {
		__BITFIELD_FIELD(uint64_t reserved_6_63:58,
		__BITFIELD_FIELD(uint64_t bmhclsel:1,
		__BITFIELD_FIELD(uint64_t ded_err:1,
		__BITFIELD_FIELD(uint64_t sec_err:1,
		__BITFIELD_FIELD(uint64_t ded_intena:1,
		__BITFIELD_FIELD(uint64_t sec_intena:1,
		__BITFIELD_FIELD(uint64_t ecc_ena:1,
		;)))))))
	} s;
};

union cvmx_l2d_fus3 {
	uint64_t u64;
	struct cvmx_l2d_fus3_s {
		__BITFIELD_FIELD(uint64_t reserved_40_63:24,
		__BITFIELD_FIELD(uint64_t ema_ctl:3,
		__BITFIELD_FIELD(uint64_t reserved_34_36:3,
		__BITFIELD_FIELD(uint64_t q3fus:34,
		;))))
	} s;
};

#endif
