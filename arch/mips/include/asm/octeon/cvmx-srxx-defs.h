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

#ifndef __CVMX_SRXX_DEFS_H__
#define __CVMX_SRXX_DEFS_H__

#define CVMX_SRXX_COM_CTL(block_id) \
	 CVMX_ADD_IO_SEG(0x0001180090000200ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_SRXX_IGN_RX_FULL(block_id) \
	 CVMX_ADD_IO_SEG(0x0001180090000218ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_SRXX_SPI4_CALX(offset, block_id) \
	 CVMX_ADD_IO_SEG(0x0001180090000000ull + (((offset) & 31) * 8) + (((block_id) & 1) * 0x8000000ull))
#define CVMX_SRXX_SPI4_STAT(block_id) \
	 CVMX_ADD_IO_SEG(0x0001180090000208ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_SRXX_SW_TICK_CTL(block_id) \
	 CVMX_ADD_IO_SEG(0x0001180090000220ull + (((block_id) & 1) * 0x8000000ull))
#define CVMX_SRXX_SW_TICK_DAT(block_id) \
	 CVMX_ADD_IO_SEG(0x0001180090000228ull + (((block_id) & 1) * 0x8000000ull))

union cvmx_srxx_com_ctl {
	uint64_t u64;
	struct cvmx_srxx_com_ctl_s {
		uint64_t reserved_8_63:56;
		uint64_t prts:4;
		uint64_t st_en:1;
		uint64_t reserved_1_2:2;
		uint64_t inf_en:1;
	} s;
	struct cvmx_srxx_com_ctl_s cn38xx;
	struct cvmx_srxx_com_ctl_s cn38xxp2;
	struct cvmx_srxx_com_ctl_s cn58xx;
	struct cvmx_srxx_com_ctl_s cn58xxp1;
};

union cvmx_srxx_ign_rx_full {
	uint64_t u64;
	struct cvmx_srxx_ign_rx_full_s {
		uint64_t reserved_16_63:48;
		uint64_t ignore:16;
	} s;
	struct cvmx_srxx_ign_rx_full_s cn38xx;
	struct cvmx_srxx_ign_rx_full_s cn38xxp2;
	struct cvmx_srxx_ign_rx_full_s cn58xx;
	struct cvmx_srxx_ign_rx_full_s cn58xxp1;
};

union cvmx_srxx_spi4_calx {
	uint64_t u64;
	struct cvmx_srxx_spi4_calx_s {
		uint64_t reserved_17_63:47;
		uint64_t oddpar:1;
		uint64_t prt3:4;
		uint64_t prt2:4;
		uint64_t prt1:4;
		uint64_t prt0:4;
	} s;
	struct cvmx_srxx_spi4_calx_s cn38xx;
	struct cvmx_srxx_spi4_calx_s cn38xxp2;
	struct cvmx_srxx_spi4_calx_s cn58xx;
	struct cvmx_srxx_spi4_calx_s cn58xxp1;
};

union cvmx_srxx_spi4_stat {
	uint64_t u64;
	struct cvmx_srxx_spi4_stat_s {
		uint64_t reserved_16_63:48;
		uint64_t m:8;
		uint64_t reserved_7_7:1;
		uint64_t len:7;
	} s;
	struct cvmx_srxx_spi4_stat_s cn38xx;
	struct cvmx_srxx_spi4_stat_s cn38xxp2;
	struct cvmx_srxx_spi4_stat_s cn58xx;
	struct cvmx_srxx_spi4_stat_s cn58xxp1;
};

union cvmx_srxx_sw_tick_ctl {
	uint64_t u64;
	struct cvmx_srxx_sw_tick_ctl_s {
		uint64_t reserved_14_63:50;
		uint64_t eop:1;
		uint64_t sop:1;
		uint64_t mod:4;
		uint64_t opc:4;
		uint64_t adr:4;
	} s;
	struct cvmx_srxx_sw_tick_ctl_s cn38xx;
	struct cvmx_srxx_sw_tick_ctl_s cn58xx;
	struct cvmx_srxx_sw_tick_ctl_s cn58xxp1;
};

union cvmx_srxx_sw_tick_dat {
	uint64_t u64;
	struct cvmx_srxx_sw_tick_dat_s {
		uint64_t dat:64;
	} s;
	struct cvmx_srxx_sw_tick_dat_s cn38xx;
	struct cvmx_srxx_sw_tick_dat_s cn58xx;
	struct cvmx_srxx_sw_tick_dat_s cn58xxp1;
};

#endif
