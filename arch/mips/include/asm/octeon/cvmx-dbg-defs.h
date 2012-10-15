/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2012 Cavium Networks
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

#ifndef __CVMX_DBG_DEFS_H__
#define __CVMX_DBG_DEFS_H__

#define CVMX_DBG_DATA (CVMX_ADD_IO_SEG(0x00011F00000001E8ull))

union cvmx_dbg_data {
	uint64_t u64;
	struct cvmx_dbg_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t c_mul:5;
		uint64_t dsel_ext:1;
		uint64_t data:17;
#else
		uint64_t data:17;
		uint64_t dsel_ext:1;
		uint64_t c_mul:5;
		uint64_t reserved_23_63:41;
#endif
	} s;
	struct cvmx_dbg_data_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_31_63:33;
		uint64_t pll_mul:3;
		uint64_t reserved_23_27:5;
		uint64_t c_mul:5;
		uint64_t dsel_ext:1;
		uint64_t data:17;
#else
		uint64_t data:17;
		uint64_t dsel_ext:1;
		uint64_t c_mul:5;
		uint64_t reserved_23_27:5;
		uint64_t pll_mul:3;
		uint64_t reserved_31_63:33;
#endif
	} cn30xx;
	struct cvmx_dbg_data_cn30xx cn31xx;
	struct cvmx_dbg_data_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t d_mul:4;
		uint64_t dclk_mul2:1;
		uint64_t cclk_div2:1;
		uint64_t c_mul:5;
		uint64_t dsel_ext:1;
		uint64_t data:17;
#else
		uint64_t data:17;
		uint64_t dsel_ext:1;
		uint64_t c_mul:5;
		uint64_t cclk_div2:1;
		uint64_t dclk_mul2:1;
		uint64_t d_mul:4;
		uint64_t reserved_29_63:35;
#endif
	} cn38xx;
	struct cvmx_dbg_data_cn38xx cn38xxp2;
	struct cvmx_dbg_data_cn30xx cn50xx;
	struct cvmx_dbg_data_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t rem:6;
		uint64_t c_mul:5;
		uint64_t dsel_ext:1;
		uint64_t data:17;
#else
		uint64_t data:17;
		uint64_t dsel_ext:1;
		uint64_t c_mul:5;
		uint64_t rem:6;
		uint64_t reserved_29_63:35;
#endif
	} cn58xx;
	struct cvmx_dbg_data_cn58xx cn58xxp1;
};

#endif
