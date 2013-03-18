/***********************license start***************
 * Author: Cavium Inc.
 *
 * Contact: support@cavium.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2012 Cavium Inc.
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
 * Contact Cavium Inc. for more information
 ***********************license end**************************************/

#ifndef __CVMX_LMCX_DEFS_H__
#define __CVMX_LMCX_DEFS_H__

#define CVMX_LMCX_BIST_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800880000F0ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_BIST_RESULT(block_id) (CVMX_ADD_IO_SEG(0x00011800880000F8ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_CHAR_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000220ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_CHAR_MASK0(block_id) (CVMX_ADD_IO_SEG(0x0001180088000228ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_CHAR_MASK1(block_id) (CVMX_ADD_IO_SEG(0x0001180088000230ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_CHAR_MASK2(block_id) (CVMX_ADD_IO_SEG(0x0001180088000238ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_CHAR_MASK3(block_id) (CVMX_ADD_IO_SEG(0x0001180088000240ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_CHAR_MASK4(block_id) (CVMX_ADD_IO_SEG(0x0001180088000318ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_COMP_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000028ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_COMP_CTL2(block_id) (CVMX_ADD_IO_SEG(0x00011800880001B8ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_CONFIG(block_id) (CVMX_ADD_IO_SEG(0x0001180088000188ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_CONTROL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000190ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000010ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_CTL1(block_id) (CVMX_ADD_IO_SEG(0x0001180088000090ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_DCLK_CNT(block_id) (CVMX_ADD_IO_SEG(0x00011800880001E0ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_DCLK_CNT_HI(block_id) (CVMX_ADD_IO_SEG(0x0001180088000070ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_DCLK_CNT_LO(block_id) (CVMX_ADD_IO_SEG(0x0001180088000068ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_DCLK_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800880000B8ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_DDR2_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000018ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_DDR_PLL_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000258ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_DELAY_CFG(block_id) (CVMX_ADD_IO_SEG(0x0001180088000088ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_DIMMX_PARAMS(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180088000270ull) + (((offset) & 1) + ((block_id) & 3) * 0x200000ull) * 8)
#define CVMX_LMCX_DIMM_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000310ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_DLL_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800880000C0ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_DLL_CTL2(block_id) (CVMX_ADD_IO_SEG(0x00011800880001C8ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_DLL_CTL3(block_id) (CVMX_ADD_IO_SEG(0x0001180088000218ull) + ((block_id) & 3) * 0x1000000ull)
static inline uint64_t CVMX_LMCX_DUAL_MEMCFG(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180088000098ull) + (block_id) * 0x60000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180088000098ull) + (block_id) * 0x60000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180088000098ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180088000098ull) + (block_id) * 0x60000000ull;
}

static inline uint64_t CVMX_LMCX_ECC_SYND(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180088000038ull) + (block_id) * 0x60000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180088000038ull) + (block_id) * 0x60000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180088000038ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180088000038ull) + (block_id) * 0x60000000ull;
}

static inline uint64_t CVMX_LMCX_FADR(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180088000020ull) + (block_id) * 0x60000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180088000020ull) + (block_id) * 0x60000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180088000020ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x0001180088000020ull) + (block_id) * 0x60000000ull;
}

#define CVMX_LMCX_IFB_CNT(block_id) (CVMX_ADD_IO_SEG(0x00011800880001D0ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_IFB_CNT_HI(block_id) (CVMX_ADD_IO_SEG(0x0001180088000050ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_IFB_CNT_LO(block_id) (CVMX_ADD_IO_SEG(0x0001180088000048ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_INT(block_id) (CVMX_ADD_IO_SEG(0x00011800880001F0ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_INT_EN(block_id) (CVMX_ADD_IO_SEG(0x00011800880001E8ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_MEM_CFG0(block_id) (CVMX_ADD_IO_SEG(0x0001180088000000ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_MEM_CFG1(block_id) (CVMX_ADD_IO_SEG(0x0001180088000008ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_MODEREG_PARAMS0(block_id) (CVMX_ADD_IO_SEG(0x00011800880001A8ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_MODEREG_PARAMS1(block_id) (CVMX_ADD_IO_SEG(0x0001180088000260ull) + ((block_id) & 3) * 0x1000000ull)
static inline uint64_t CVMX_LMCX_NXM(unsigned long block_id)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800880000C8ull) + (block_id) * 0x60000000ull;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800880000C8ull) + (block_id) * 0x60000000ull;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x00011800880000C8ull) + (block_id) * 0x1000000ull;
	}
	return CVMX_ADD_IO_SEG(0x00011800880000C8ull) + (block_id) * 0x60000000ull;
}

#define CVMX_LMCX_OPS_CNT(block_id) (CVMX_ADD_IO_SEG(0x00011800880001D8ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_OPS_CNT_HI(block_id) (CVMX_ADD_IO_SEG(0x0001180088000060ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_OPS_CNT_LO(block_id) (CVMX_ADD_IO_SEG(0x0001180088000058ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_PHY_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000210ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_PLL_BWCTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000040ull))
#define CVMX_LMCX_PLL_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800880000A8ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_PLL_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800880000B0ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_READ_LEVEL_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000140ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_READ_LEVEL_DBG(block_id) (CVMX_ADD_IO_SEG(0x0001180088000148ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_READ_LEVEL_RANKX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180088000100ull) + (((offset) & 3) + ((block_id) & 1) * 0xC000000ull) * 8)
#define CVMX_LMCX_RESET_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000180ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_RLEVEL_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800880002A0ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_RLEVEL_DBG(block_id) (CVMX_ADD_IO_SEG(0x00011800880002A8ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_RLEVEL_RANKX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180088000280ull) + (((offset) & 3) + ((block_id) & 3) * 0x200000ull) * 8)
#define CVMX_LMCX_RODT_COMP_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800880000A0ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_RODT_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000078ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_RODT_MASK(block_id) (CVMX_ADD_IO_SEG(0x0001180088000268ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_SCRAMBLED_FADR(block_id) (CVMX_ADD_IO_SEG(0x0001180088000330ull))
#define CVMX_LMCX_SCRAMBLE_CFG0(block_id) (CVMX_ADD_IO_SEG(0x0001180088000320ull))
#define CVMX_LMCX_SCRAMBLE_CFG1(block_id) (CVMX_ADD_IO_SEG(0x0001180088000328ull))
#define CVMX_LMCX_SLOT_CTL0(block_id) (CVMX_ADD_IO_SEG(0x00011800880001F8ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_SLOT_CTL1(block_id) (CVMX_ADD_IO_SEG(0x0001180088000200ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_SLOT_CTL2(block_id) (CVMX_ADD_IO_SEG(0x0001180088000208ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_TIMING_PARAMS0(block_id) (CVMX_ADD_IO_SEG(0x0001180088000198ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_TIMING_PARAMS1(block_id) (CVMX_ADD_IO_SEG(0x00011800880001A0ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_TRO_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000248ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_TRO_STAT(block_id) (CVMX_ADD_IO_SEG(0x0001180088000250ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_WLEVEL_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180088000300ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_WLEVEL_DBG(block_id) (CVMX_ADD_IO_SEG(0x0001180088000308ull) + ((block_id) & 3) * 0x1000000ull)
#define CVMX_LMCX_WLEVEL_RANKX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800880002B0ull) + (((offset) & 3) + ((block_id) & 3) * 0x200000ull) * 8)
#define CVMX_LMCX_WODT_CTL0(block_id) (CVMX_ADD_IO_SEG(0x0001180088000030ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_WODT_CTL1(block_id) (CVMX_ADD_IO_SEG(0x0001180088000080ull) + ((block_id) & 1) * 0x60000000ull)
#define CVMX_LMCX_WODT_MASK(block_id) (CVMX_ADD_IO_SEG(0x00011800880001B0ull) + ((block_id) & 3) * 0x1000000ull)

union cvmx_lmcx_bist_ctl {
	uint64_t u64;
	struct cvmx_lmcx_bist_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t start:1;
#else
		uint64_t start:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_lmcx_bist_ctl_s cn50xx;
	struct cvmx_lmcx_bist_ctl_s cn52xx;
	struct cvmx_lmcx_bist_ctl_s cn52xxp1;
	struct cvmx_lmcx_bist_ctl_s cn56xx;
	struct cvmx_lmcx_bist_ctl_s cn56xxp1;
};

union cvmx_lmcx_bist_result {
	uint64_t u64;
	struct cvmx_lmcx_bist_result_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t csrd2e:1;
		uint64_t csre2d:1;
		uint64_t mwf:1;
		uint64_t mwd:3;
		uint64_t mwc:1;
		uint64_t mrf:1;
		uint64_t mrd:3;
#else
		uint64_t mrd:3;
		uint64_t mrf:1;
		uint64_t mwc:1;
		uint64_t mwd:3;
		uint64_t mwf:1;
		uint64_t csre2d:1;
		uint64_t csrd2e:1;
		uint64_t reserved_11_63:53;
#endif
	} s;
	struct cvmx_lmcx_bist_result_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t mwf:1;
		uint64_t mwd:3;
		uint64_t mwc:1;
		uint64_t mrf:1;
		uint64_t mrd:3;
#else
		uint64_t mrd:3;
		uint64_t mrf:1;
		uint64_t mwc:1;
		uint64_t mwd:3;
		uint64_t mwf:1;
		uint64_t reserved_9_63:55;
#endif
	} cn50xx;
	struct cvmx_lmcx_bist_result_s cn52xx;
	struct cvmx_lmcx_bist_result_s cn52xxp1;
	struct cvmx_lmcx_bist_result_s cn56xx;
	struct cvmx_lmcx_bist_result_s cn56xxp1;
};

union cvmx_lmcx_char_ctl {
	uint64_t u64;
	struct cvmx_lmcx_char_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t dr:1;
		uint64_t skew_on:1;
		uint64_t en:1;
		uint64_t sel:1;
		uint64_t prog:8;
		uint64_t prbs:32;
#else
		uint64_t prbs:32;
		uint64_t prog:8;
		uint64_t sel:1;
		uint64_t en:1;
		uint64_t skew_on:1;
		uint64_t dr:1;
		uint64_t reserved_44_63:20;
#endif
	} s;
	struct cvmx_lmcx_char_ctl_s cn61xx;
	struct cvmx_lmcx_char_ctl_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_42_63:22;
		uint64_t en:1;
		uint64_t sel:1;
		uint64_t prog:8;
		uint64_t prbs:32;
#else
		uint64_t prbs:32;
		uint64_t prog:8;
		uint64_t sel:1;
		uint64_t en:1;
		uint64_t reserved_42_63:22;
#endif
	} cn63xx;
	struct cvmx_lmcx_char_ctl_cn63xx cn63xxp1;
	struct cvmx_lmcx_char_ctl_s cn66xx;
	struct cvmx_lmcx_char_ctl_s cn68xx;
	struct cvmx_lmcx_char_ctl_cn63xx cn68xxp1;
	struct cvmx_lmcx_char_ctl_s cnf71xx;
};

union cvmx_lmcx_char_mask0 {
	uint64_t u64;
	struct cvmx_lmcx_char_mask0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t mask:64;
#else
		uint64_t mask:64;
#endif
	} s;
	struct cvmx_lmcx_char_mask0_s cn61xx;
	struct cvmx_lmcx_char_mask0_s cn63xx;
	struct cvmx_lmcx_char_mask0_s cn63xxp1;
	struct cvmx_lmcx_char_mask0_s cn66xx;
	struct cvmx_lmcx_char_mask0_s cn68xx;
	struct cvmx_lmcx_char_mask0_s cn68xxp1;
	struct cvmx_lmcx_char_mask0_s cnf71xx;
};

union cvmx_lmcx_char_mask1 {
	uint64_t u64;
	struct cvmx_lmcx_char_mask1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t mask:8;
#else
		uint64_t mask:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_lmcx_char_mask1_s cn61xx;
	struct cvmx_lmcx_char_mask1_s cn63xx;
	struct cvmx_lmcx_char_mask1_s cn63xxp1;
	struct cvmx_lmcx_char_mask1_s cn66xx;
	struct cvmx_lmcx_char_mask1_s cn68xx;
	struct cvmx_lmcx_char_mask1_s cn68xxp1;
	struct cvmx_lmcx_char_mask1_s cnf71xx;
};

union cvmx_lmcx_char_mask2 {
	uint64_t u64;
	struct cvmx_lmcx_char_mask2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t mask:64;
#else
		uint64_t mask:64;
#endif
	} s;
	struct cvmx_lmcx_char_mask2_s cn61xx;
	struct cvmx_lmcx_char_mask2_s cn63xx;
	struct cvmx_lmcx_char_mask2_s cn63xxp1;
	struct cvmx_lmcx_char_mask2_s cn66xx;
	struct cvmx_lmcx_char_mask2_s cn68xx;
	struct cvmx_lmcx_char_mask2_s cn68xxp1;
	struct cvmx_lmcx_char_mask2_s cnf71xx;
};

union cvmx_lmcx_char_mask3 {
	uint64_t u64;
	struct cvmx_lmcx_char_mask3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t mask:8;
#else
		uint64_t mask:8;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_lmcx_char_mask3_s cn61xx;
	struct cvmx_lmcx_char_mask3_s cn63xx;
	struct cvmx_lmcx_char_mask3_s cn63xxp1;
	struct cvmx_lmcx_char_mask3_s cn66xx;
	struct cvmx_lmcx_char_mask3_s cn68xx;
	struct cvmx_lmcx_char_mask3_s cn68xxp1;
	struct cvmx_lmcx_char_mask3_s cnf71xx;
};

union cvmx_lmcx_char_mask4 {
	uint64_t u64;
	struct cvmx_lmcx_char_mask4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t reset_n_mask:1;
		uint64_t a_mask:16;
		uint64_t ba_mask:3;
		uint64_t we_n_mask:1;
		uint64_t cas_n_mask:1;
		uint64_t ras_n_mask:1;
		uint64_t odt1_mask:2;
		uint64_t odt0_mask:2;
		uint64_t cs1_n_mask:2;
		uint64_t cs0_n_mask:2;
		uint64_t cke_mask:2;
#else
		uint64_t cke_mask:2;
		uint64_t cs0_n_mask:2;
		uint64_t cs1_n_mask:2;
		uint64_t odt0_mask:2;
		uint64_t odt1_mask:2;
		uint64_t ras_n_mask:1;
		uint64_t cas_n_mask:1;
		uint64_t we_n_mask:1;
		uint64_t ba_mask:3;
		uint64_t a_mask:16;
		uint64_t reset_n_mask:1;
		uint64_t reserved_33_63:31;
#endif
	} s;
	struct cvmx_lmcx_char_mask4_s cn61xx;
	struct cvmx_lmcx_char_mask4_s cn63xx;
	struct cvmx_lmcx_char_mask4_s cn63xxp1;
	struct cvmx_lmcx_char_mask4_s cn66xx;
	struct cvmx_lmcx_char_mask4_s cn68xx;
	struct cvmx_lmcx_char_mask4_s cn68xxp1;
	struct cvmx_lmcx_char_mask4_s cnf71xx;
};

union cvmx_lmcx_comp_ctl {
	uint64_t u64;
	struct cvmx_lmcx_comp_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t nctl_csr:4;
		uint64_t nctl_clk:4;
		uint64_t nctl_cmd:4;
		uint64_t nctl_dat:4;
		uint64_t pctl_csr:4;
		uint64_t pctl_clk:4;
		uint64_t reserved_0_7:8;
#else
		uint64_t reserved_0_7:8;
		uint64_t pctl_clk:4;
		uint64_t pctl_csr:4;
		uint64_t nctl_dat:4;
		uint64_t nctl_cmd:4;
		uint64_t nctl_clk:4;
		uint64_t nctl_csr:4;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_comp_ctl_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t nctl_csr:4;
		uint64_t nctl_clk:4;
		uint64_t nctl_cmd:4;
		uint64_t nctl_dat:4;
		uint64_t pctl_csr:4;
		uint64_t pctl_clk:4;
		uint64_t pctl_cmd:4;
		uint64_t pctl_dat:4;
#else
		uint64_t pctl_dat:4;
		uint64_t pctl_cmd:4;
		uint64_t pctl_clk:4;
		uint64_t pctl_csr:4;
		uint64_t nctl_dat:4;
		uint64_t nctl_cmd:4;
		uint64_t nctl_clk:4;
		uint64_t nctl_csr:4;
		uint64_t reserved_32_63:32;
#endif
	} cn30xx;
	struct cvmx_lmcx_comp_ctl_cn30xx cn31xx;
	struct cvmx_lmcx_comp_ctl_cn30xx cn38xx;
	struct cvmx_lmcx_comp_ctl_cn30xx cn38xxp2;
	struct cvmx_lmcx_comp_ctl_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t nctl_csr:4;
		uint64_t reserved_20_27:8;
		uint64_t nctl_dat:4;
		uint64_t pctl_csr:4;
		uint64_t reserved_5_11:7;
		uint64_t pctl_dat:5;
#else
		uint64_t pctl_dat:5;
		uint64_t reserved_5_11:7;
		uint64_t pctl_csr:4;
		uint64_t nctl_dat:4;
		uint64_t reserved_20_27:8;
		uint64_t nctl_csr:4;
		uint64_t reserved_32_63:32;
#endif
	} cn50xx;
	struct cvmx_lmcx_comp_ctl_cn50xx cn52xx;
	struct cvmx_lmcx_comp_ctl_cn50xx cn52xxp1;
	struct cvmx_lmcx_comp_ctl_cn50xx cn56xx;
	struct cvmx_lmcx_comp_ctl_cn50xx cn56xxp1;
	struct cvmx_lmcx_comp_ctl_cn50xx cn58xx;
	struct cvmx_lmcx_comp_ctl_cn58xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t nctl_csr:4;
		uint64_t reserved_20_27:8;
		uint64_t nctl_dat:4;
		uint64_t pctl_csr:4;
		uint64_t reserved_4_11:8;
		uint64_t pctl_dat:4;
#else
		uint64_t pctl_dat:4;
		uint64_t reserved_4_11:8;
		uint64_t pctl_csr:4;
		uint64_t nctl_dat:4;
		uint64_t reserved_20_27:8;
		uint64_t nctl_csr:4;
		uint64_t reserved_32_63:32;
#endif
	} cn58xxp1;
};

union cvmx_lmcx_comp_ctl2 {
	uint64_t u64;
	struct cvmx_lmcx_comp_ctl2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t ddr__ptune:4;
		uint64_t ddr__ntune:4;
		uint64_t m180:1;
		uint64_t byp:1;
		uint64_t ptune:4;
		uint64_t ntune:4;
		uint64_t rodt_ctl:4;
		uint64_t cmd_ctl:4;
		uint64_t ck_ctl:4;
		uint64_t dqx_ctl:4;
#else
		uint64_t dqx_ctl:4;
		uint64_t ck_ctl:4;
		uint64_t cmd_ctl:4;
		uint64_t rodt_ctl:4;
		uint64_t ntune:4;
		uint64_t ptune:4;
		uint64_t byp:1;
		uint64_t m180:1;
		uint64_t ddr__ntune:4;
		uint64_t ddr__ptune:4;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_lmcx_comp_ctl2_s cn61xx;
	struct cvmx_lmcx_comp_ctl2_s cn63xx;
	struct cvmx_lmcx_comp_ctl2_s cn63xxp1;
	struct cvmx_lmcx_comp_ctl2_s cn66xx;
	struct cvmx_lmcx_comp_ctl2_s cn68xx;
	struct cvmx_lmcx_comp_ctl2_s cn68xxp1;
	struct cvmx_lmcx_comp_ctl2_s cnf71xx;
};

union cvmx_lmcx_config {
	uint64_t u64;
	struct cvmx_lmcx_config_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_61_63:3;
		uint64_t mode32b:1;
		uint64_t scrz:1;
		uint64_t early_unload_d1_r1:1;
		uint64_t early_unload_d1_r0:1;
		uint64_t early_unload_d0_r1:1;
		uint64_t early_unload_d0_r0:1;
		uint64_t init_status:4;
		uint64_t mirrmask:4;
		uint64_t rankmask:4;
		uint64_t rank_ena:1;
		uint64_t sref_with_dll:1;
		uint64_t early_dqx:1;
		uint64_t sequence:3;
		uint64_t ref_zqcs_int:19;
		uint64_t reset:1;
		uint64_t ecc_adr:1;
		uint64_t forcewrite:4;
		uint64_t idlepower:3;
		uint64_t pbank_lsb:4;
		uint64_t row_lsb:3;
		uint64_t ecc_ena:1;
		uint64_t init_start:1;
#else
		uint64_t init_start:1;
		uint64_t ecc_ena:1;
		uint64_t row_lsb:3;
		uint64_t pbank_lsb:4;
		uint64_t idlepower:3;
		uint64_t forcewrite:4;
		uint64_t ecc_adr:1;
		uint64_t reset:1;
		uint64_t ref_zqcs_int:19;
		uint64_t sequence:3;
		uint64_t early_dqx:1;
		uint64_t sref_with_dll:1;
		uint64_t rank_ena:1;
		uint64_t rankmask:4;
		uint64_t mirrmask:4;
		uint64_t init_status:4;
		uint64_t early_unload_d0_r0:1;
		uint64_t early_unload_d0_r1:1;
		uint64_t early_unload_d1_r0:1;
		uint64_t early_unload_d1_r1:1;
		uint64_t scrz:1;
		uint64_t mode32b:1;
		uint64_t reserved_61_63:3;
#endif
	} s;
	struct cvmx_lmcx_config_s cn61xx;
	struct cvmx_lmcx_config_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t early_unload_d1_r1:1;
		uint64_t early_unload_d1_r0:1;
		uint64_t early_unload_d0_r1:1;
		uint64_t early_unload_d0_r0:1;
		uint64_t init_status:4;
		uint64_t mirrmask:4;
		uint64_t rankmask:4;
		uint64_t rank_ena:1;
		uint64_t sref_with_dll:1;
		uint64_t early_dqx:1;
		uint64_t sequence:3;
		uint64_t ref_zqcs_int:19;
		uint64_t reset:1;
		uint64_t ecc_adr:1;
		uint64_t forcewrite:4;
		uint64_t idlepower:3;
		uint64_t pbank_lsb:4;
		uint64_t row_lsb:3;
		uint64_t ecc_ena:1;
		uint64_t init_start:1;
#else
		uint64_t init_start:1;
		uint64_t ecc_ena:1;
		uint64_t row_lsb:3;
		uint64_t pbank_lsb:4;
		uint64_t idlepower:3;
		uint64_t forcewrite:4;
		uint64_t ecc_adr:1;
		uint64_t reset:1;
		uint64_t ref_zqcs_int:19;
		uint64_t sequence:3;
		uint64_t early_dqx:1;
		uint64_t sref_with_dll:1;
		uint64_t rank_ena:1;
		uint64_t rankmask:4;
		uint64_t mirrmask:4;
		uint64_t init_status:4;
		uint64_t early_unload_d0_r0:1;
		uint64_t early_unload_d0_r1:1;
		uint64_t early_unload_d1_r0:1;
		uint64_t early_unload_d1_r1:1;
		uint64_t reserved_59_63:5;
#endif
	} cn63xx;
	struct cvmx_lmcx_config_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_55_63:9;
		uint64_t init_status:4;
		uint64_t mirrmask:4;
		uint64_t rankmask:4;
		uint64_t rank_ena:1;
		uint64_t sref_with_dll:1;
		uint64_t early_dqx:1;
		uint64_t sequence:3;
		uint64_t ref_zqcs_int:19;
		uint64_t reset:1;
		uint64_t ecc_adr:1;
		uint64_t forcewrite:4;
		uint64_t idlepower:3;
		uint64_t pbank_lsb:4;
		uint64_t row_lsb:3;
		uint64_t ecc_ena:1;
		uint64_t init_start:1;
#else
		uint64_t init_start:1;
		uint64_t ecc_ena:1;
		uint64_t row_lsb:3;
		uint64_t pbank_lsb:4;
		uint64_t idlepower:3;
		uint64_t forcewrite:4;
		uint64_t ecc_adr:1;
		uint64_t reset:1;
		uint64_t ref_zqcs_int:19;
		uint64_t sequence:3;
		uint64_t early_dqx:1;
		uint64_t sref_with_dll:1;
		uint64_t rank_ena:1;
		uint64_t rankmask:4;
		uint64_t mirrmask:4;
		uint64_t init_status:4;
		uint64_t reserved_55_63:9;
#endif
	} cn63xxp1;
	struct cvmx_lmcx_config_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_60_63:4;
		uint64_t scrz:1;
		uint64_t early_unload_d1_r1:1;
		uint64_t early_unload_d1_r0:1;
		uint64_t early_unload_d0_r1:1;
		uint64_t early_unload_d0_r0:1;
		uint64_t init_status:4;
		uint64_t mirrmask:4;
		uint64_t rankmask:4;
		uint64_t rank_ena:1;
		uint64_t sref_with_dll:1;
		uint64_t early_dqx:1;
		uint64_t sequence:3;
		uint64_t ref_zqcs_int:19;
		uint64_t reset:1;
		uint64_t ecc_adr:1;
		uint64_t forcewrite:4;
		uint64_t idlepower:3;
		uint64_t pbank_lsb:4;
		uint64_t row_lsb:3;
		uint64_t ecc_ena:1;
		uint64_t init_start:1;
#else
		uint64_t init_start:1;
		uint64_t ecc_ena:1;
		uint64_t row_lsb:3;
		uint64_t pbank_lsb:4;
		uint64_t idlepower:3;
		uint64_t forcewrite:4;
		uint64_t ecc_adr:1;
		uint64_t reset:1;
		uint64_t ref_zqcs_int:19;
		uint64_t sequence:3;
		uint64_t early_dqx:1;
		uint64_t sref_with_dll:1;
		uint64_t rank_ena:1;
		uint64_t rankmask:4;
		uint64_t mirrmask:4;
		uint64_t init_status:4;
		uint64_t early_unload_d0_r0:1;
		uint64_t early_unload_d0_r1:1;
		uint64_t early_unload_d1_r0:1;
		uint64_t early_unload_d1_r1:1;
		uint64_t scrz:1;
		uint64_t reserved_60_63:4;
#endif
	} cn66xx;
	struct cvmx_lmcx_config_cn63xx cn68xx;
	struct cvmx_lmcx_config_cn63xx cn68xxp1;
	struct cvmx_lmcx_config_s cnf71xx;
};

union cvmx_lmcx_control {
	uint64_t u64;
	struct cvmx_lmcx_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t scramble_ena:1;
		uint64_t thrcnt:12;
		uint64_t persub:8;
		uint64_t thrmax:4;
		uint64_t crm_cnt:5;
		uint64_t crm_thr:5;
		uint64_t crm_max:5;
		uint64_t rodt_bprch:1;
		uint64_t wodt_bprch:1;
		uint64_t bprch:2;
		uint64_t ext_zqcs_dis:1;
		uint64_t int_zqcs_dis:1;
		uint64_t auto_dclkdis:1;
		uint64_t xor_bank:1;
		uint64_t max_write_batch:4;
		uint64_t nxm_write_en:1;
		uint64_t elev_prio_dis:1;
		uint64_t inorder_wr:1;
		uint64_t inorder_rd:1;
		uint64_t throttle_wr:1;
		uint64_t throttle_rd:1;
		uint64_t fprch2:2;
		uint64_t pocas:1;
		uint64_t ddr2t:1;
		uint64_t bwcnt:1;
		uint64_t rdimm_ena:1;
#else
		uint64_t rdimm_ena:1;
		uint64_t bwcnt:1;
		uint64_t ddr2t:1;
		uint64_t pocas:1;
		uint64_t fprch2:2;
		uint64_t throttle_rd:1;
		uint64_t throttle_wr:1;
		uint64_t inorder_rd:1;
		uint64_t inorder_wr:1;
		uint64_t elev_prio_dis:1;
		uint64_t nxm_write_en:1;
		uint64_t max_write_batch:4;
		uint64_t xor_bank:1;
		uint64_t auto_dclkdis:1;
		uint64_t int_zqcs_dis:1;
		uint64_t ext_zqcs_dis:1;
		uint64_t bprch:2;
		uint64_t wodt_bprch:1;
		uint64_t rodt_bprch:1;
		uint64_t crm_max:5;
		uint64_t crm_thr:5;
		uint64_t crm_cnt:5;
		uint64_t thrmax:4;
		uint64_t persub:8;
		uint64_t thrcnt:12;
		uint64_t scramble_ena:1;
#endif
	} s;
	struct cvmx_lmcx_control_s cn61xx;
	struct cvmx_lmcx_control_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t rodt_bprch:1;
		uint64_t wodt_bprch:1;
		uint64_t bprch:2;
		uint64_t ext_zqcs_dis:1;
		uint64_t int_zqcs_dis:1;
		uint64_t auto_dclkdis:1;
		uint64_t xor_bank:1;
		uint64_t max_write_batch:4;
		uint64_t nxm_write_en:1;
		uint64_t elev_prio_dis:1;
		uint64_t inorder_wr:1;
		uint64_t inorder_rd:1;
		uint64_t throttle_wr:1;
		uint64_t throttle_rd:1;
		uint64_t fprch2:2;
		uint64_t pocas:1;
		uint64_t ddr2t:1;
		uint64_t bwcnt:1;
		uint64_t rdimm_ena:1;
#else
		uint64_t rdimm_ena:1;
		uint64_t bwcnt:1;
		uint64_t ddr2t:1;
		uint64_t pocas:1;
		uint64_t fprch2:2;
		uint64_t throttle_rd:1;
		uint64_t throttle_wr:1;
		uint64_t inorder_rd:1;
		uint64_t inorder_wr:1;
		uint64_t elev_prio_dis:1;
		uint64_t nxm_write_en:1;
		uint64_t max_write_batch:4;
		uint64_t xor_bank:1;
		uint64_t auto_dclkdis:1;
		uint64_t int_zqcs_dis:1;
		uint64_t ext_zqcs_dis:1;
		uint64_t bprch:2;
		uint64_t wodt_bprch:1;
		uint64_t rodt_bprch:1;
		uint64_t reserved_24_63:40;
#endif
	} cn63xx;
	struct cvmx_lmcx_control_cn63xx cn63xxp1;
	struct cvmx_lmcx_control_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t scramble_ena:1;
		uint64_t reserved_24_62:39;
		uint64_t rodt_bprch:1;
		uint64_t wodt_bprch:1;
		uint64_t bprch:2;
		uint64_t ext_zqcs_dis:1;
		uint64_t int_zqcs_dis:1;
		uint64_t auto_dclkdis:1;
		uint64_t xor_bank:1;
		uint64_t max_write_batch:4;
		uint64_t nxm_write_en:1;
		uint64_t elev_prio_dis:1;
		uint64_t inorder_wr:1;
		uint64_t inorder_rd:1;
		uint64_t throttle_wr:1;
		uint64_t throttle_rd:1;
		uint64_t fprch2:2;
		uint64_t pocas:1;
		uint64_t ddr2t:1;
		uint64_t bwcnt:1;
		uint64_t rdimm_ena:1;
#else
		uint64_t rdimm_ena:1;
		uint64_t bwcnt:1;
		uint64_t ddr2t:1;
		uint64_t pocas:1;
		uint64_t fprch2:2;
		uint64_t throttle_rd:1;
		uint64_t throttle_wr:1;
		uint64_t inorder_rd:1;
		uint64_t inorder_wr:1;
		uint64_t elev_prio_dis:1;
		uint64_t nxm_write_en:1;
		uint64_t max_write_batch:4;
		uint64_t xor_bank:1;
		uint64_t auto_dclkdis:1;
		uint64_t int_zqcs_dis:1;
		uint64_t ext_zqcs_dis:1;
		uint64_t bprch:2;
		uint64_t wodt_bprch:1;
		uint64_t rodt_bprch:1;
		uint64_t reserved_24_62:39;
		uint64_t scramble_ena:1;
#endif
	} cn66xx;
	struct cvmx_lmcx_control_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_63_63:1;
		uint64_t thrcnt:12;
		uint64_t persub:8;
		uint64_t thrmax:4;
		uint64_t crm_cnt:5;
		uint64_t crm_thr:5;
		uint64_t crm_max:5;
		uint64_t rodt_bprch:1;
		uint64_t wodt_bprch:1;
		uint64_t bprch:2;
		uint64_t ext_zqcs_dis:1;
		uint64_t int_zqcs_dis:1;
		uint64_t auto_dclkdis:1;
		uint64_t xor_bank:1;
		uint64_t max_write_batch:4;
		uint64_t nxm_write_en:1;
		uint64_t elev_prio_dis:1;
		uint64_t inorder_wr:1;
		uint64_t inorder_rd:1;
		uint64_t throttle_wr:1;
		uint64_t throttle_rd:1;
		uint64_t fprch2:2;
		uint64_t pocas:1;
		uint64_t ddr2t:1;
		uint64_t bwcnt:1;
		uint64_t rdimm_ena:1;
#else
		uint64_t rdimm_ena:1;
		uint64_t bwcnt:1;
		uint64_t ddr2t:1;
		uint64_t pocas:1;
		uint64_t fprch2:2;
		uint64_t throttle_rd:1;
		uint64_t throttle_wr:1;
		uint64_t inorder_rd:1;
		uint64_t inorder_wr:1;
		uint64_t elev_prio_dis:1;
		uint64_t nxm_write_en:1;
		uint64_t max_write_batch:4;
		uint64_t xor_bank:1;
		uint64_t auto_dclkdis:1;
		uint64_t int_zqcs_dis:1;
		uint64_t ext_zqcs_dis:1;
		uint64_t bprch:2;
		uint64_t wodt_bprch:1;
		uint64_t rodt_bprch:1;
		uint64_t crm_max:5;
		uint64_t crm_thr:5;
		uint64_t crm_cnt:5;
		uint64_t thrmax:4;
		uint64_t persub:8;
		uint64_t thrcnt:12;
		uint64_t reserved_63_63:1;
#endif
	} cn68xx;
	struct cvmx_lmcx_control_cn68xx cn68xxp1;
	struct cvmx_lmcx_control_cn66xx cnf71xx;
};

union cvmx_lmcx_ctl {
	uint64_t u64;
	struct cvmx_lmcx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ddr__nctl:4;
		uint64_t ddr__pctl:4;
		uint64_t slow_scf:1;
		uint64_t xor_bank:1;
		uint64_t max_write_batch:4;
		uint64_t pll_div2:1;
		uint64_t pll_bypass:1;
		uint64_t rdimm_ena:1;
		uint64_t r2r_slot:1;
		uint64_t inorder_mwf:1;
		uint64_t inorder_mrf:1;
		uint64_t reserved_10_11:2;
		uint64_t fprch2:1;
		uint64_t bprch:1;
		uint64_t sil_lat:2;
		uint64_t tskw:2;
		uint64_t qs_dic:2;
		uint64_t dic:2;
#else
		uint64_t dic:2;
		uint64_t qs_dic:2;
		uint64_t tskw:2;
		uint64_t sil_lat:2;
		uint64_t bprch:1;
		uint64_t fprch2:1;
		uint64_t reserved_10_11:2;
		uint64_t inorder_mrf:1;
		uint64_t inorder_mwf:1;
		uint64_t r2r_slot:1;
		uint64_t rdimm_ena:1;
		uint64_t pll_bypass:1;
		uint64_t pll_div2:1;
		uint64_t max_write_batch:4;
		uint64_t xor_bank:1;
		uint64_t slow_scf:1;
		uint64_t ddr__pctl:4;
		uint64_t ddr__nctl:4;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_ctl_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ddr__nctl:4;
		uint64_t ddr__pctl:4;
		uint64_t slow_scf:1;
		uint64_t xor_bank:1;
		uint64_t max_write_batch:4;
		uint64_t pll_div2:1;
		uint64_t pll_bypass:1;
		uint64_t rdimm_ena:1;
		uint64_t r2r_slot:1;
		uint64_t inorder_mwf:1;
		uint64_t inorder_mrf:1;
		uint64_t dreset:1;
		uint64_t mode32b:1;
		uint64_t fprch2:1;
		uint64_t bprch:1;
		uint64_t sil_lat:2;
		uint64_t tskw:2;
		uint64_t qs_dic:2;
		uint64_t dic:2;
#else
		uint64_t dic:2;
		uint64_t qs_dic:2;
		uint64_t tskw:2;
		uint64_t sil_lat:2;
		uint64_t bprch:1;
		uint64_t fprch2:1;
		uint64_t mode32b:1;
		uint64_t dreset:1;
		uint64_t inorder_mrf:1;
		uint64_t inorder_mwf:1;
		uint64_t r2r_slot:1;
		uint64_t rdimm_ena:1;
		uint64_t pll_bypass:1;
		uint64_t pll_div2:1;
		uint64_t max_write_batch:4;
		uint64_t xor_bank:1;
		uint64_t slow_scf:1;
		uint64_t ddr__pctl:4;
		uint64_t ddr__nctl:4;
		uint64_t reserved_32_63:32;
#endif
	} cn30xx;
	struct cvmx_lmcx_ctl_cn30xx cn31xx;
	struct cvmx_lmcx_ctl_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ddr__nctl:4;
		uint64_t ddr__pctl:4;
		uint64_t slow_scf:1;
		uint64_t xor_bank:1;
		uint64_t max_write_batch:4;
		uint64_t reserved_16_17:2;
		uint64_t rdimm_ena:1;
		uint64_t r2r_slot:1;
		uint64_t inorder_mwf:1;
		uint64_t inorder_mrf:1;
		uint64_t set_zero:1;
		uint64_t mode128b:1;
		uint64_t fprch2:1;
		uint64_t bprch:1;
		uint64_t sil_lat:2;
		uint64_t tskw:2;
		uint64_t qs_dic:2;
		uint64_t dic:2;
#else
		uint64_t dic:2;
		uint64_t qs_dic:2;
		uint64_t tskw:2;
		uint64_t sil_lat:2;
		uint64_t bprch:1;
		uint64_t fprch2:1;
		uint64_t mode128b:1;
		uint64_t set_zero:1;
		uint64_t inorder_mrf:1;
		uint64_t inorder_mwf:1;
		uint64_t r2r_slot:1;
		uint64_t rdimm_ena:1;
		uint64_t reserved_16_17:2;
		uint64_t max_write_batch:4;
		uint64_t xor_bank:1;
		uint64_t slow_scf:1;
		uint64_t ddr__pctl:4;
		uint64_t ddr__nctl:4;
		uint64_t reserved_32_63:32;
#endif
	} cn38xx;
	struct cvmx_lmcx_ctl_cn38xx cn38xxp2;
	struct cvmx_lmcx_ctl_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ddr__nctl:4;
		uint64_t ddr__pctl:4;
		uint64_t slow_scf:1;
		uint64_t xor_bank:1;
		uint64_t max_write_batch:4;
		uint64_t reserved_17_17:1;
		uint64_t pll_bypass:1;
		uint64_t rdimm_ena:1;
		uint64_t r2r_slot:1;
		uint64_t inorder_mwf:1;
		uint64_t inorder_mrf:1;
		uint64_t dreset:1;
		uint64_t mode32b:1;
		uint64_t fprch2:1;
		uint64_t bprch:1;
		uint64_t sil_lat:2;
		uint64_t tskw:2;
		uint64_t qs_dic:2;
		uint64_t dic:2;
#else
		uint64_t dic:2;
		uint64_t qs_dic:2;
		uint64_t tskw:2;
		uint64_t sil_lat:2;
		uint64_t bprch:1;
		uint64_t fprch2:1;
		uint64_t mode32b:1;
		uint64_t dreset:1;
		uint64_t inorder_mrf:1;
		uint64_t inorder_mwf:1;
		uint64_t r2r_slot:1;
		uint64_t rdimm_ena:1;
		uint64_t pll_bypass:1;
		uint64_t reserved_17_17:1;
		uint64_t max_write_batch:4;
		uint64_t xor_bank:1;
		uint64_t slow_scf:1;
		uint64_t ddr__pctl:4;
		uint64_t ddr__nctl:4;
		uint64_t reserved_32_63:32;
#endif
	} cn50xx;
	struct cvmx_lmcx_ctl_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ddr__nctl:4;
		uint64_t ddr__pctl:4;
		uint64_t slow_scf:1;
		uint64_t xor_bank:1;
		uint64_t max_write_batch:4;
		uint64_t reserved_16_17:2;
		uint64_t rdimm_ena:1;
		uint64_t r2r_slot:1;
		uint64_t inorder_mwf:1;
		uint64_t inorder_mrf:1;
		uint64_t dreset:1;
		uint64_t mode32b:1;
		uint64_t fprch2:1;
		uint64_t bprch:1;
		uint64_t sil_lat:2;
		uint64_t tskw:2;
		uint64_t qs_dic:2;
		uint64_t dic:2;
#else
		uint64_t dic:2;
		uint64_t qs_dic:2;
		uint64_t tskw:2;
		uint64_t sil_lat:2;
		uint64_t bprch:1;
		uint64_t fprch2:1;
		uint64_t mode32b:1;
		uint64_t dreset:1;
		uint64_t inorder_mrf:1;
		uint64_t inorder_mwf:1;
		uint64_t r2r_slot:1;
		uint64_t rdimm_ena:1;
		uint64_t reserved_16_17:2;
		uint64_t max_write_batch:4;
		uint64_t xor_bank:1;
		uint64_t slow_scf:1;
		uint64_t ddr__pctl:4;
		uint64_t ddr__nctl:4;
		uint64_t reserved_32_63:32;
#endif
	} cn52xx;
	struct cvmx_lmcx_ctl_cn52xx cn52xxp1;
	struct cvmx_lmcx_ctl_cn52xx cn56xx;
	struct cvmx_lmcx_ctl_cn52xx cn56xxp1;
	struct cvmx_lmcx_ctl_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ddr__nctl:4;
		uint64_t ddr__pctl:4;
		uint64_t slow_scf:1;
		uint64_t xor_bank:1;
		uint64_t max_write_batch:4;
		uint64_t reserved_16_17:2;
		uint64_t rdimm_ena:1;
		uint64_t r2r_slot:1;
		uint64_t inorder_mwf:1;
		uint64_t inorder_mrf:1;
		uint64_t dreset:1;
		uint64_t mode128b:1;
		uint64_t fprch2:1;
		uint64_t bprch:1;
		uint64_t sil_lat:2;
		uint64_t tskw:2;
		uint64_t qs_dic:2;
		uint64_t dic:2;
#else
		uint64_t dic:2;
		uint64_t qs_dic:2;
		uint64_t tskw:2;
		uint64_t sil_lat:2;
		uint64_t bprch:1;
		uint64_t fprch2:1;
		uint64_t mode128b:1;
		uint64_t dreset:1;
		uint64_t inorder_mrf:1;
		uint64_t inorder_mwf:1;
		uint64_t r2r_slot:1;
		uint64_t rdimm_ena:1;
		uint64_t reserved_16_17:2;
		uint64_t max_write_batch:4;
		uint64_t xor_bank:1;
		uint64_t slow_scf:1;
		uint64_t ddr__pctl:4;
		uint64_t ddr__nctl:4;
		uint64_t reserved_32_63:32;
#endif
	} cn58xx;
	struct cvmx_lmcx_ctl_cn58xx cn58xxp1;
};

union cvmx_lmcx_ctl1 {
	uint64_t u64;
	struct cvmx_lmcx_ctl1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_21_63:43;
		uint64_t ecc_adr:1;
		uint64_t forcewrite:4;
		uint64_t idlepower:3;
		uint64_t sequence:3;
		uint64_t sil_mode:1;
		uint64_t dcc_enable:1;
		uint64_t reserved_2_7:6;
		uint64_t data_layout:2;
#else
		uint64_t data_layout:2;
		uint64_t reserved_2_7:6;
		uint64_t dcc_enable:1;
		uint64_t sil_mode:1;
		uint64_t sequence:3;
		uint64_t idlepower:3;
		uint64_t forcewrite:4;
		uint64_t ecc_adr:1;
		uint64_t reserved_21_63:43;
#endif
	} s;
	struct cvmx_lmcx_ctl1_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t data_layout:2;
#else
		uint64_t data_layout:2;
		uint64_t reserved_2_63:62;
#endif
	} cn30xx;
	struct cvmx_lmcx_ctl1_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t sil_mode:1;
		uint64_t dcc_enable:1;
		uint64_t reserved_2_7:6;
		uint64_t data_layout:2;
#else
		uint64_t data_layout:2;
		uint64_t reserved_2_7:6;
		uint64_t dcc_enable:1;
		uint64_t sil_mode:1;
		uint64_t reserved_10_63:54;
#endif
	} cn50xx;
	struct cvmx_lmcx_ctl1_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_21_63:43;
		uint64_t ecc_adr:1;
		uint64_t forcewrite:4;
		uint64_t idlepower:3;
		uint64_t sequence:3;
		uint64_t sil_mode:1;
		uint64_t dcc_enable:1;
		uint64_t reserved_0_7:8;
#else
		uint64_t reserved_0_7:8;
		uint64_t dcc_enable:1;
		uint64_t sil_mode:1;
		uint64_t sequence:3;
		uint64_t idlepower:3;
		uint64_t forcewrite:4;
		uint64_t ecc_adr:1;
		uint64_t reserved_21_63:43;
#endif
	} cn52xx;
	struct cvmx_lmcx_ctl1_cn52xx cn52xxp1;
	struct cvmx_lmcx_ctl1_cn52xx cn56xx;
	struct cvmx_lmcx_ctl1_cn52xx cn56xxp1;
	struct cvmx_lmcx_ctl1_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t sil_mode:1;
		uint64_t dcc_enable:1;
		uint64_t reserved_0_7:8;
#else
		uint64_t reserved_0_7:8;
		uint64_t dcc_enable:1;
		uint64_t sil_mode:1;
		uint64_t reserved_10_63:54;
#endif
	} cn58xx;
	struct cvmx_lmcx_ctl1_cn58xx cn58xxp1;
};

union cvmx_lmcx_dclk_cnt {
	uint64_t u64;
	struct cvmx_lmcx_dclk_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t dclkcnt:64;
#else
		uint64_t dclkcnt:64;
#endif
	} s;
	struct cvmx_lmcx_dclk_cnt_s cn61xx;
	struct cvmx_lmcx_dclk_cnt_s cn63xx;
	struct cvmx_lmcx_dclk_cnt_s cn63xxp1;
	struct cvmx_lmcx_dclk_cnt_s cn66xx;
	struct cvmx_lmcx_dclk_cnt_s cn68xx;
	struct cvmx_lmcx_dclk_cnt_s cn68xxp1;
	struct cvmx_lmcx_dclk_cnt_s cnf71xx;
};

union cvmx_lmcx_dclk_cnt_hi {
	uint64_t u64;
	struct cvmx_lmcx_dclk_cnt_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t dclkcnt_hi:32;
#else
		uint64_t dclkcnt_hi:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_dclk_cnt_hi_s cn30xx;
	struct cvmx_lmcx_dclk_cnt_hi_s cn31xx;
	struct cvmx_lmcx_dclk_cnt_hi_s cn38xx;
	struct cvmx_lmcx_dclk_cnt_hi_s cn38xxp2;
	struct cvmx_lmcx_dclk_cnt_hi_s cn50xx;
	struct cvmx_lmcx_dclk_cnt_hi_s cn52xx;
	struct cvmx_lmcx_dclk_cnt_hi_s cn52xxp1;
	struct cvmx_lmcx_dclk_cnt_hi_s cn56xx;
	struct cvmx_lmcx_dclk_cnt_hi_s cn56xxp1;
	struct cvmx_lmcx_dclk_cnt_hi_s cn58xx;
	struct cvmx_lmcx_dclk_cnt_hi_s cn58xxp1;
};

union cvmx_lmcx_dclk_cnt_lo {
	uint64_t u64;
	struct cvmx_lmcx_dclk_cnt_lo_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t dclkcnt_lo:32;
#else
		uint64_t dclkcnt_lo:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_dclk_cnt_lo_s cn30xx;
	struct cvmx_lmcx_dclk_cnt_lo_s cn31xx;
	struct cvmx_lmcx_dclk_cnt_lo_s cn38xx;
	struct cvmx_lmcx_dclk_cnt_lo_s cn38xxp2;
	struct cvmx_lmcx_dclk_cnt_lo_s cn50xx;
	struct cvmx_lmcx_dclk_cnt_lo_s cn52xx;
	struct cvmx_lmcx_dclk_cnt_lo_s cn52xxp1;
	struct cvmx_lmcx_dclk_cnt_lo_s cn56xx;
	struct cvmx_lmcx_dclk_cnt_lo_s cn56xxp1;
	struct cvmx_lmcx_dclk_cnt_lo_s cn58xx;
	struct cvmx_lmcx_dclk_cnt_lo_s cn58xxp1;
};

union cvmx_lmcx_dclk_ctl {
	uint64_t u64;
	struct cvmx_lmcx_dclk_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t off90_ena:1;
		uint64_t dclk90_byp:1;
		uint64_t dclk90_ld:1;
		uint64_t dclk90_vlu:5;
#else
		uint64_t dclk90_vlu:5;
		uint64_t dclk90_ld:1;
		uint64_t dclk90_byp:1;
		uint64_t off90_ena:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_lmcx_dclk_ctl_s cn56xx;
	struct cvmx_lmcx_dclk_ctl_s cn56xxp1;
};

union cvmx_lmcx_ddr2_ctl {
	uint64_t u64;
	struct cvmx_lmcx_ddr2_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t bank8:1;
		uint64_t burst8:1;
		uint64_t addlat:3;
		uint64_t pocas:1;
		uint64_t bwcnt:1;
		uint64_t twr:3;
		uint64_t silo_hc:1;
		uint64_t ddr_eof:4;
		uint64_t tfaw:5;
		uint64_t crip_mode:1;
		uint64_t ddr2t:1;
		uint64_t odt_ena:1;
		uint64_t qdll_ena:1;
		uint64_t dll90_vlu:5;
		uint64_t dll90_byp:1;
		uint64_t rdqs:1;
		uint64_t ddr2:1;
#else
		uint64_t ddr2:1;
		uint64_t rdqs:1;
		uint64_t dll90_byp:1;
		uint64_t dll90_vlu:5;
		uint64_t qdll_ena:1;
		uint64_t odt_ena:1;
		uint64_t ddr2t:1;
		uint64_t crip_mode:1;
		uint64_t tfaw:5;
		uint64_t ddr_eof:4;
		uint64_t silo_hc:1;
		uint64_t twr:3;
		uint64_t bwcnt:1;
		uint64_t pocas:1;
		uint64_t addlat:3;
		uint64_t burst8:1;
		uint64_t bank8:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_ddr2_ctl_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t bank8:1;
		uint64_t burst8:1;
		uint64_t addlat:3;
		uint64_t pocas:1;
		uint64_t bwcnt:1;
		uint64_t twr:3;
		uint64_t silo_hc:1;
		uint64_t ddr_eof:4;
		uint64_t tfaw:5;
		uint64_t crip_mode:1;
		uint64_t ddr2t:1;
		uint64_t odt_ena:1;
		uint64_t qdll_ena:1;
		uint64_t dll90_vlu:5;
		uint64_t dll90_byp:1;
		uint64_t reserved_1_1:1;
		uint64_t ddr2:1;
#else
		uint64_t ddr2:1;
		uint64_t reserved_1_1:1;
		uint64_t dll90_byp:1;
		uint64_t dll90_vlu:5;
		uint64_t qdll_ena:1;
		uint64_t odt_ena:1;
		uint64_t ddr2t:1;
		uint64_t crip_mode:1;
		uint64_t tfaw:5;
		uint64_t ddr_eof:4;
		uint64_t silo_hc:1;
		uint64_t twr:3;
		uint64_t bwcnt:1;
		uint64_t pocas:1;
		uint64_t addlat:3;
		uint64_t burst8:1;
		uint64_t bank8:1;
		uint64_t reserved_32_63:32;
#endif
	} cn30xx;
	struct cvmx_lmcx_ddr2_ctl_cn30xx cn31xx;
	struct cvmx_lmcx_ddr2_ctl_s cn38xx;
	struct cvmx_lmcx_ddr2_ctl_s cn38xxp2;
	struct cvmx_lmcx_ddr2_ctl_s cn50xx;
	struct cvmx_lmcx_ddr2_ctl_s cn52xx;
	struct cvmx_lmcx_ddr2_ctl_s cn52xxp1;
	struct cvmx_lmcx_ddr2_ctl_s cn56xx;
	struct cvmx_lmcx_ddr2_ctl_s cn56xxp1;
	struct cvmx_lmcx_ddr2_ctl_s cn58xx;
	struct cvmx_lmcx_ddr2_ctl_s cn58xxp1;
};

union cvmx_lmcx_ddr_pll_ctl {
	uint64_t u64;
	struct cvmx_lmcx_ddr_pll_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_27_63:37;
		uint64_t jtg_test_mode:1;
		uint64_t dfm_div_reset:1;
		uint64_t dfm_ps_en:3;
		uint64_t ddr_div_reset:1;
		uint64_t ddr_ps_en:3;
		uint64_t diffamp:4;
		uint64_t cps:3;
		uint64_t cpb:3;
		uint64_t reset_n:1;
		uint64_t clkf:7;
#else
		uint64_t clkf:7;
		uint64_t reset_n:1;
		uint64_t cpb:3;
		uint64_t cps:3;
		uint64_t diffamp:4;
		uint64_t ddr_ps_en:3;
		uint64_t ddr_div_reset:1;
		uint64_t dfm_ps_en:3;
		uint64_t dfm_div_reset:1;
		uint64_t jtg_test_mode:1;
		uint64_t reserved_27_63:37;
#endif
	} s;
	struct cvmx_lmcx_ddr_pll_ctl_s cn61xx;
	struct cvmx_lmcx_ddr_pll_ctl_s cn63xx;
	struct cvmx_lmcx_ddr_pll_ctl_s cn63xxp1;
	struct cvmx_lmcx_ddr_pll_ctl_s cn66xx;
	struct cvmx_lmcx_ddr_pll_ctl_s cn68xx;
	struct cvmx_lmcx_ddr_pll_ctl_s cn68xxp1;
	struct cvmx_lmcx_ddr_pll_ctl_s cnf71xx;
};

union cvmx_lmcx_delay_cfg {
	uint64_t u64;
	struct cvmx_lmcx_delay_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t dq:5;
		uint64_t cmd:5;
		uint64_t clk:5;
#else
		uint64_t clk:5;
		uint64_t cmd:5;
		uint64_t dq:5;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_lmcx_delay_cfg_s cn30xx;
	struct cvmx_lmcx_delay_cfg_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t dq:4;
		uint64_t reserved_9_9:1;
		uint64_t cmd:4;
		uint64_t reserved_4_4:1;
		uint64_t clk:4;
#else
		uint64_t clk:4;
		uint64_t reserved_4_4:1;
		uint64_t cmd:4;
		uint64_t reserved_9_9:1;
		uint64_t dq:4;
		uint64_t reserved_14_63:50;
#endif
	} cn38xx;
	struct cvmx_lmcx_delay_cfg_cn38xx cn50xx;
	struct cvmx_lmcx_delay_cfg_cn38xx cn52xx;
	struct cvmx_lmcx_delay_cfg_cn38xx cn52xxp1;
	struct cvmx_lmcx_delay_cfg_cn38xx cn56xx;
	struct cvmx_lmcx_delay_cfg_cn38xx cn56xxp1;
	struct cvmx_lmcx_delay_cfg_cn38xx cn58xx;
	struct cvmx_lmcx_delay_cfg_cn38xx cn58xxp1;
};

union cvmx_lmcx_dimmx_params {
	uint64_t u64;
	struct cvmx_lmcx_dimmx_params_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rc15:4;
		uint64_t rc14:4;
		uint64_t rc13:4;
		uint64_t rc12:4;
		uint64_t rc11:4;
		uint64_t rc10:4;
		uint64_t rc9:4;
		uint64_t rc8:4;
		uint64_t rc7:4;
		uint64_t rc6:4;
		uint64_t rc5:4;
		uint64_t rc4:4;
		uint64_t rc3:4;
		uint64_t rc2:4;
		uint64_t rc1:4;
		uint64_t rc0:4;
#else
		uint64_t rc0:4;
		uint64_t rc1:4;
		uint64_t rc2:4;
		uint64_t rc3:4;
		uint64_t rc4:4;
		uint64_t rc5:4;
		uint64_t rc6:4;
		uint64_t rc7:4;
		uint64_t rc8:4;
		uint64_t rc9:4;
		uint64_t rc10:4;
		uint64_t rc11:4;
		uint64_t rc12:4;
		uint64_t rc13:4;
		uint64_t rc14:4;
		uint64_t rc15:4;
#endif
	} s;
	struct cvmx_lmcx_dimmx_params_s cn61xx;
	struct cvmx_lmcx_dimmx_params_s cn63xx;
	struct cvmx_lmcx_dimmx_params_s cn63xxp1;
	struct cvmx_lmcx_dimmx_params_s cn66xx;
	struct cvmx_lmcx_dimmx_params_s cn68xx;
	struct cvmx_lmcx_dimmx_params_s cn68xxp1;
	struct cvmx_lmcx_dimmx_params_s cnf71xx;
};

union cvmx_lmcx_dimm_ctl {
	uint64_t u64;
	struct cvmx_lmcx_dimm_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_46_63:18;
		uint64_t parity:1;
		uint64_t tcws:13;
		uint64_t dimm1_wmask:16;
		uint64_t dimm0_wmask:16;
#else
		uint64_t dimm0_wmask:16;
		uint64_t dimm1_wmask:16;
		uint64_t tcws:13;
		uint64_t parity:1;
		uint64_t reserved_46_63:18;
#endif
	} s;
	struct cvmx_lmcx_dimm_ctl_s cn61xx;
	struct cvmx_lmcx_dimm_ctl_s cn63xx;
	struct cvmx_lmcx_dimm_ctl_s cn63xxp1;
	struct cvmx_lmcx_dimm_ctl_s cn66xx;
	struct cvmx_lmcx_dimm_ctl_s cn68xx;
	struct cvmx_lmcx_dimm_ctl_s cn68xxp1;
	struct cvmx_lmcx_dimm_ctl_s cnf71xx;
};

union cvmx_lmcx_dll_ctl {
	uint64_t u64;
	struct cvmx_lmcx_dll_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t dreset:1;
		uint64_t dll90_byp:1;
		uint64_t dll90_ena:1;
		uint64_t dll90_vlu:5;
#else
		uint64_t dll90_vlu:5;
		uint64_t dll90_ena:1;
		uint64_t dll90_byp:1;
		uint64_t dreset:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
	struct cvmx_lmcx_dll_ctl_s cn52xx;
	struct cvmx_lmcx_dll_ctl_s cn52xxp1;
	struct cvmx_lmcx_dll_ctl_s cn56xx;
	struct cvmx_lmcx_dll_ctl_s cn56xxp1;
};

union cvmx_lmcx_dll_ctl2 {
	uint64_t u64;
	struct cvmx_lmcx_dll_ctl2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t intf_en:1;
		uint64_t dll_bringup:1;
		uint64_t dreset:1;
		uint64_t quad_dll_ena:1;
		uint64_t byp_sel:4;
		uint64_t byp_setting:8;
#else
		uint64_t byp_setting:8;
		uint64_t byp_sel:4;
		uint64_t quad_dll_ena:1;
		uint64_t dreset:1;
		uint64_t dll_bringup:1;
		uint64_t intf_en:1;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_lmcx_dll_ctl2_s cn61xx;
	struct cvmx_lmcx_dll_ctl2_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t dll_bringup:1;
		uint64_t dreset:1;
		uint64_t quad_dll_ena:1;
		uint64_t byp_sel:4;
		uint64_t byp_setting:8;
#else
		uint64_t byp_setting:8;
		uint64_t byp_sel:4;
		uint64_t quad_dll_ena:1;
		uint64_t dreset:1;
		uint64_t dll_bringup:1;
		uint64_t reserved_15_63:49;
#endif
	} cn63xx;
	struct cvmx_lmcx_dll_ctl2_cn63xx cn63xxp1;
	struct cvmx_lmcx_dll_ctl2_cn63xx cn66xx;
	struct cvmx_lmcx_dll_ctl2_s cn68xx;
	struct cvmx_lmcx_dll_ctl2_s cn68xxp1;
	struct cvmx_lmcx_dll_ctl2_s cnf71xx;
};

union cvmx_lmcx_dll_ctl3 {
	uint64_t u64;
	struct cvmx_lmcx_dll_ctl3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_41_63:23;
		uint64_t dclk90_fwd:1;
		uint64_t ddr_90_dly_byp:1;
		uint64_t dclk90_recal_dis:1;
		uint64_t dclk90_byp_sel:1;
		uint64_t dclk90_byp_setting:8;
		uint64_t dll_fast:1;
		uint64_t dll90_setting:8;
		uint64_t fine_tune_mode:1;
		uint64_t dll_mode:1;
		uint64_t dll90_byte_sel:4;
		uint64_t offset_ena:1;
		uint64_t load_offset:1;
		uint64_t mode_sel:2;
		uint64_t byte_sel:4;
		uint64_t offset:6;
#else
		uint64_t offset:6;
		uint64_t byte_sel:4;
		uint64_t mode_sel:2;
		uint64_t load_offset:1;
		uint64_t offset_ena:1;
		uint64_t dll90_byte_sel:4;
		uint64_t dll_mode:1;
		uint64_t fine_tune_mode:1;
		uint64_t dll90_setting:8;
		uint64_t dll_fast:1;
		uint64_t dclk90_byp_setting:8;
		uint64_t dclk90_byp_sel:1;
		uint64_t dclk90_recal_dis:1;
		uint64_t ddr_90_dly_byp:1;
		uint64_t dclk90_fwd:1;
		uint64_t reserved_41_63:23;
#endif
	} s;
	struct cvmx_lmcx_dll_ctl3_s cn61xx;
	struct cvmx_lmcx_dll_ctl3_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t dll_fast:1;
		uint64_t dll90_setting:8;
		uint64_t fine_tune_mode:1;
		uint64_t dll_mode:1;
		uint64_t dll90_byte_sel:4;
		uint64_t offset_ena:1;
		uint64_t load_offset:1;
		uint64_t mode_sel:2;
		uint64_t byte_sel:4;
		uint64_t offset:6;
#else
		uint64_t offset:6;
		uint64_t byte_sel:4;
		uint64_t mode_sel:2;
		uint64_t load_offset:1;
		uint64_t offset_ena:1;
		uint64_t dll90_byte_sel:4;
		uint64_t dll_mode:1;
		uint64_t fine_tune_mode:1;
		uint64_t dll90_setting:8;
		uint64_t dll_fast:1;
		uint64_t reserved_29_63:35;
#endif
	} cn63xx;
	struct cvmx_lmcx_dll_ctl3_cn63xx cn63xxp1;
	struct cvmx_lmcx_dll_ctl3_cn63xx cn66xx;
	struct cvmx_lmcx_dll_ctl3_s cn68xx;
	struct cvmx_lmcx_dll_ctl3_s cn68xxp1;
	struct cvmx_lmcx_dll_ctl3_s cnf71xx;
};

union cvmx_lmcx_dual_memcfg {
	uint64_t u64;
	struct cvmx_lmcx_dual_memcfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t bank8:1;
		uint64_t row_lsb:3;
		uint64_t reserved_8_15:8;
		uint64_t cs_mask:8;
#else
		uint64_t cs_mask:8;
		uint64_t reserved_8_15:8;
		uint64_t row_lsb:3;
		uint64_t bank8:1;
		uint64_t reserved_20_63:44;
#endif
	} s;
	struct cvmx_lmcx_dual_memcfg_s cn50xx;
	struct cvmx_lmcx_dual_memcfg_s cn52xx;
	struct cvmx_lmcx_dual_memcfg_s cn52xxp1;
	struct cvmx_lmcx_dual_memcfg_s cn56xx;
	struct cvmx_lmcx_dual_memcfg_s cn56xxp1;
	struct cvmx_lmcx_dual_memcfg_s cn58xx;
	struct cvmx_lmcx_dual_memcfg_s cn58xxp1;
	struct cvmx_lmcx_dual_memcfg_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t row_lsb:3;
		uint64_t reserved_8_15:8;
		uint64_t cs_mask:8;
#else
		uint64_t cs_mask:8;
		uint64_t reserved_8_15:8;
		uint64_t row_lsb:3;
		uint64_t reserved_19_63:45;
#endif
	} cn61xx;
	struct cvmx_lmcx_dual_memcfg_cn61xx cn63xx;
	struct cvmx_lmcx_dual_memcfg_cn61xx cn63xxp1;
	struct cvmx_lmcx_dual_memcfg_cn61xx cn66xx;
	struct cvmx_lmcx_dual_memcfg_cn61xx cn68xx;
	struct cvmx_lmcx_dual_memcfg_cn61xx cn68xxp1;
	struct cvmx_lmcx_dual_memcfg_cn61xx cnf71xx;
};

union cvmx_lmcx_ecc_synd {
	uint64_t u64;
	struct cvmx_lmcx_ecc_synd_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t mrdsyn3:8;
		uint64_t mrdsyn2:8;
		uint64_t mrdsyn1:8;
		uint64_t mrdsyn0:8;
#else
		uint64_t mrdsyn0:8;
		uint64_t mrdsyn1:8;
		uint64_t mrdsyn2:8;
		uint64_t mrdsyn3:8;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_ecc_synd_s cn30xx;
	struct cvmx_lmcx_ecc_synd_s cn31xx;
	struct cvmx_lmcx_ecc_synd_s cn38xx;
	struct cvmx_lmcx_ecc_synd_s cn38xxp2;
	struct cvmx_lmcx_ecc_synd_s cn50xx;
	struct cvmx_lmcx_ecc_synd_s cn52xx;
	struct cvmx_lmcx_ecc_synd_s cn52xxp1;
	struct cvmx_lmcx_ecc_synd_s cn56xx;
	struct cvmx_lmcx_ecc_synd_s cn56xxp1;
	struct cvmx_lmcx_ecc_synd_s cn58xx;
	struct cvmx_lmcx_ecc_synd_s cn58xxp1;
	struct cvmx_lmcx_ecc_synd_s cn61xx;
	struct cvmx_lmcx_ecc_synd_s cn63xx;
	struct cvmx_lmcx_ecc_synd_s cn63xxp1;
	struct cvmx_lmcx_ecc_synd_s cn66xx;
	struct cvmx_lmcx_ecc_synd_s cn68xx;
	struct cvmx_lmcx_ecc_synd_s cn68xxp1;
	struct cvmx_lmcx_ecc_synd_s cnf71xx;
};

union cvmx_lmcx_fadr {
	uint64_t u64;
	struct cvmx_lmcx_fadr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_lmcx_fadr_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t fdimm:2;
		uint64_t fbunk:1;
		uint64_t fbank:3;
		uint64_t frow:14;
		uint64_t fcol:12;
#else
		uint64_t fcol:12;
		uint64_t frow:14;
		uint64_t fbank:3;
		uint64_t fbunk:1;
		uint64_t fdimm:2;
		uint64_t reserved_32_63:32;
#endif
	} cn30xx;
	struct cvmx_lmcx_fadr_cn30xx cn31xx;
	struct cvmx_lmcx_fadr_cn30xx cn38xx;
	struct cvmx_lmcx_fadr_cn30xx cn38xxp2;
	struct cvmx_lmcx_fadr_cn30xx cn50xx;
	struct cvmx_lmcx_fadr_cn30xx cn52xx;
	struct cvmx_lmcx_fadr_cn30xx cn52xxp1;
	struct cvmx_lmcx_fadr_cn30xx cn56xx;
	struct cvmx_lmcx_fadr_cn30xx cn56xxp1;
	struct cvmx_lmcx_fadr_cn30xx cn58xx;
	struct cvmx_lmcx_fadr_cn30xx cn58xxp1;
	struct cvmx_lmcx_fadr_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t fdimm:2;
		uint64_t fbunk:1;
		uint64_t fbank:3;
		uint64_t frow:16;
		uint64_t fcol:14;
#else
		uint64_t fcol:14;
		uint64_t frow:16;
		uint64_t fbank:3;
		uint64_t fbunk:1;
		uint64_t fdimm:2;
		uint64_t reserved_36_63:28;
#endif
	} cn61xx;
	struct cvmx_lmcx_fadr_cn61xx cn63xx;
	struct cvmx_lmcx_fadr_cn61xx cn63xxp1;
	struct cvmx_lmcx_fadr_cn61xx cn66xx;
	struct cvmx_lmcx_fadr_cn61xx cn68xx;
	struct cvmx_lmcx_fadr_cn61xx cn68xxp1;
	struct cvmx_lmcx_fadr_cn61xx cnf71xx;
};

union cvmx_lmcx_ifb_cnt {
	uint64_t u64;
	struct cvmx_lmcx_ifb_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t ifbcnt:64;
#else
		uint64_t ifbcnt:64;
#endif
	} s;
	struct cvmx_lmcx_ifb_cnt_s cn61xx;
	struct cvmx_lmcx_ifb_cnt_s cn63xx;
	struct cvmx_lmcx_ifb_cnt_s cn63xxp1;
	struct cvmx_lmcx_ifb_cnt_s cn66xx;
	struct cvmx_lmcx_ifb_cnt_s cn68xx;
	struct cvmx_lmcx_ifb_cnt_s cn68xxp1;
	struct cvmx_lmcx_ifb_cnt_s cnf71xx;
};

union cvmx_lmcx_ifb_cnt_hi {
	uint64_t u64;
	struct cvmx_lmcx_ifb_cnt_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ifbcnt_hi:32;
#else
		uint64_t ifbcnt_hi:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_ifb_cnt_hi_s cn30xx;
	struct cvmx_lmcx_ifb_cnt_hi_s cn31xx;
	struct cvmx_lmcx_ifb_cnt_hi_s cn38xx;
	struct cvmx_lmcx_ifb_cnt_hi_s cn38xxp2;
	struct cvmx_lmcx_ifb_cnt_hi_s cn50xx;
	struct cvmx_lmcx_ifb_cnt_hi_s cn52xx;
	struct cvmx_lmcx_ifb_cnt_hi_s cn52xxp1;
	struct cvmx_lmcx_ifb_cnt_hi_s cn56xx;
	struct cvmx_lmcx_ifb_cnt_hi_s cn56xxp1;
	struct cvmx_lmcx_ifb_cnt_hi_s cn58xx;
	struct cvmx_lmcx_ifb_cnt_hi_s cn58xxp1;
};

union cvmx_lmcx_ifb_cnt_lo {
	uint64_t u64;
	struct cvmx_lmcx_ifb_cnt_lo_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ifbcnt_lo:32;
#else
		uint64_t ifbcnt_lo:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_ifb_cnt_lo_s cn30xx;
	struct cvmx_lmcx_ifb_cnt_lo_s cn31xx;
	struct cvmx_lmcx_ifb_cnt_lo_s cn38xx;
	struct cvmx_lmcx_ifb_cnt_lo_s cn38xxp2;
	struct cvmx_lmcx_ifb_cnt_lo_s cn50xx;
	struct cvmx_lmcx_ifb_cnt_lo_s cn52xx;
	struct cvmx_lmcx_ifb_cnt_lo_s cn52xxp1;
	struct cvmx_lmcx_ifb_cnt_lo_s cn56xx;
	struct cvmx_lmcx_ifb_cnt_lo_s cn56xxp1;
	struct cvmx_lmcx_ifb_cnt_lo_s cn58xx;
	struct cvmx_lmcx_ifb_cnt_lo_s cn58xxp1;
};

union cvmx_lmcx_int {
	uint64_t u64;
	struct cvmx_lmcx_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t ded_err:4;
		uint64_t sec_err:4;
		uint64_t nxm_wr_err:1;
#else
		uint64_t nxm_wr_err:1;
		uint64_t sec_err:4;
		uint64_t ded_err:4;
		uint64_t reserved_9_63:55;
#endif
	} s;
	struct cvmx_lmcx_int_s cn61xx;
	struct cvmx_lmcx_int_s cn63xx;
	struct cvmx_lmcx_int_s cn63xxp1;
	struct cvmx_lmcx_int_s cn66xx;
	struct cvmx_lmcx_int_s cn68xx;
	struct cvmx_lmcx_int_s cn68xxp1;
	struct cvmx_lmcx_int_s cnf71xx;
};

union cvmx_lmcx_int_en {
	uint64_t u64;
	struct cvmx_lmcx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t intr_ded_ena:1;
		uint64_t intr_sec_ena:1;
		uint64_t intr_nxm_wr_ena:1;
#else
		uint64_t intr_nxm_wr_ena:1;
		uint64_t intr_sec_ena:1;
		uint64_t intr_ded_ena:1;
		uint64_t reserved_3_63:61;
#endif
	} s;
	struct cvmx_lmcx_int_en_s cn61xx;
	struct cvmx_lmcx_int_en_s cn63xx;
	struct cvmx_lmcx_int_en_s cn63xxp1;
	struct cvmx_lmcx_int_en_s cn66xx;
	struct cvmx_lmcx_int_en_s cn68xx;
	struct cvmx_lmcx_int_en_s cn68xxp1;
	struct cvmx_lmcx_int_en_s cnf71xx;
};

union cvmx_lmcx_mem_cfg0 {
	uint64_t u64;
	struct cvmx_lmcx_mem_cfg0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t reset:1;
		uint64_t silo_qc:1;
		uint64_t bunk_ena:1;
		uint64_t ded_err:4;
		uint64_t sec_err:4;
		uint64_t intr_ded_ena:1;
		uint64_t intr_sec_ena:1;
		uint64_t tcl:4;
		uint64_t ref_int:6;
		uint64_t pbank_lsb:4;
		uint64_t row_lsb:3;
		uint64_t ecc_ena:1;
		uint64_t init_start:1;
#else
		uint64_t init_start:1;
		uint64_t ecc_ena:1;
		uint64_t row_lsb:3;
		uint64_t pbank_lsb:4;
		uint64_t ref_int:6;
		uint64_t tcl:4;
		uint64_t intr_sec_ena:1;
		uint64_t intr_ded_ena:1;
		uint64_t sec_err:4;
		uint64_t ded_err:4;
		uint64_t bunk_ena:1;
		uint64_t silo_qc:1;
		uint64_t reset:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_mem_cfg0_s cn30xx;
	struct cvmx_lmcx_mem_cfg0_s cn31xx;
	struct cvmx_lmcx_mem_cfg0_s cn38xx;
	struct cvmx_lmcx_mem_cfg0_s cn38xxp2;
	struct cvmx_lmcx_mem_cfg0_s cn50xx;
	struct cvmx_lmcx_mem_cfg0_s cn52xx;
	struct cvmx_lmcx_mem_cfg0_s cn52xxp1;
	struct cvmx_lmcx_mem_cfg0_s cn56xx;
	struct cvmx_lmcx_mem_cfg0_s cn56xxp1;
	struct cvmx_lmcx_mem_cfg0_s cn58xx;
	struct cvmx_lmcx_mem_cfg0_s cn58xxp1;
};

union cvmx_lmcx_mem_cfg1 {
	uint64_t u64;
	struct cvmx_lmcx_mem_cfg1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t comp_bypass:1;
		uint64_t trrd:3;
		uint64_t caslat:3;
		uint64_t tmrd:3;
		uint64_t trfc:5;
		uint64_t trp:4;
		uint64_t twtr:4;
		uint64_t trcd:4;
		uint64_t tras:5;
#else
		uint64_t tras:5;
		uint64_t trcd:4;
		uint64_t twtr:4;
		uint64_t trp:4;
		uint64_t trfc:5;
		uint64_t tmrd:3;
		uint64_t caslat:3;
		uint64_t trrd:3;
		uint64_t comp_bypass:1;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_mem_cfg1_s cn30xx;
	struct cvmx_lmcx_mem_cfg1_s cn31xx;
	struct cvmx_lmcx_mem_cfg1_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_31_63:33;
		uint64_t trrd:3;
		uint64_t caslat:3;
		uint64_t tmrd:3;
		uint64_t trfc:5;
		uint64_t trp:4;
		uint64_t twtr:4;
		uint64_t trcd:4;
		uint64_t tras:5;
#else
		uint64_t tras:5;
		uint64_t trcd:4;
		uint64_t twtr:4;
		uint64_t trp:4;
		uint64_t trfc:5;
		uint64_t tmrd:3;
		uint64_t caslat:3;
		uint64_t trrd:3;
		uint64_t reserved_31_63:33;
#endif
	} cn38xx;
	struct cvmx_lmcx_mem_cfg1_cn38xx cn38xxp2;
	struct cvmx_lmcx_mem_cfg1_s cn50xx;
	struct cvmx_lmcx_mem_cfg1_cn38xx cn52xx;
	struct cvmx_lmcx_mem_cfg1_cn38xx cn52xxp1;
	struct cvmx_lmcx_mem_cfg1_cn38xx cn56xx;
	struct cvmx_lmcx_mem_cfg1_cn38xx cn56xxp1;
	struct cvmx_lmcx_mem_cfg1_cn38xx cn58xx;
	struct cvmx_lmcx_mem_cfg1_cn38xx cn58xxp1;
};

union cvmx_lmcx_modereg_params0 {
	uint64_t u64;
	struct cvmx_lmcx_modereg_params0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t ppd:1;
		uint64_t wrp:3;
		uint64_t dllr:1;
		uint64_t tm:1;
		uint64_t rbt:1;
		uint64_t cl:4;
		uint64_t bl:2;
		uint64_t qoff:1;
		uint64_t tdqs:1;
		uint64_t wlev:1;
		uint64_t al:2;
		uint64_t dll:1;
		uint64_t mpr:1;
		uint64_t mprloc:2;
		uint64_t cwl:3;
#else
		uint64_t cwl:3;
		uint64_t mprloc:2;
		uint64_t mpr:1;
		uint64_t dll:1;
		uint64_t al:2;
		uint64_t wlev:1;
		uint64_t tdqs:1;
		uint64_t qoff:1;
		uint64_t bl:2;
		uint64_t cl:4;
		uint64_t rbt:1;
		uint64_t tm:1;
		uint64_t dllr:1;
		uint64_t wrp:3;
		uint64_t ppd:1;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_lmcx_modereg_params0_s cn61xx;
	struct cvmx_lmcx_modereg_params0_s cn63xx;
	struct cvmx_lmcx_modereg_params0_s cn63xxp1;
	struct cvmx_lmcx_modereg_params0_s cn66xx;
	struct cvmx_lmcx_modereg_params0_s cn68xx;
	struct cvmx_lmcx_modereg_params0_s cn68xxp1;
	struct cvmx_lmcx_modereg_params0_s cnf71xx;
};

union cvmx_lmcx_modereg_params1 {
	uint64_t u64;
	struct cvmx_lmcx_modereg_params1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_48_63:16;
		uint64_t rtt_nom_11:3;
		uint64_t dic_11:2;
		uint64_t rtt_wr_11:2;
		uint64_t srt_11:1;
		uint64_t asr_11:1;
		uint64_t pasr_11:3;
		uint64_t rtt_nom_10:3;
		uint64_t dic_10:2;
		uint64_t rtt_wr_10:2;
		uint64_t srt_10:1;
		uint64_t asr_10:1;
		uint64_t pasr_10:3;
		uint64_t rtt_nom_01:3;
		uint64_t dic_01:2;
		uint64_t rtt_wr_01:2;
		uint64_t srt_01:1;
		uint64_t asr_01:1;
		uint64_t pasr_01:3;
		uint64_t rtt_nom_00:3;
		uint64_t dic_00:2;
		uint64_t rtt_wr_00:2;
		uint64_t srt_00:1;
		uint64_t asr_00:1;
		uint64_t pasr_00:3;
#else
		uint64_t pasr_00:3;
		uint64_t asr_00:1;
		uint64_t srt_00:1;
		uint64_t rtt_wr_00:2;
		uint64_t dic_00:2;
		uint64_t rtt_nom_00:3;
		uint64_t pasr_01:3;
		uint64_t asr_01:1;
		uint64_t srt_01:1;
		uint64_t rtt_wr_01:2;
		uint64_t dic_01:2;
		uint64_t rtt_nom_01:3;
		uint64_t pasr_10:3;
		uint64_t asr_10:1;
		uint64_t srt_10:1;
		uint64_t rtt_wr_10:2;
		uint64_t dic_10:2;
		uint64_t rtt_nom_10:3;
		uint64_t pasr_11:3;
		uint64_t asr_11:1;
		uint64_t srt_11:1;
		uint64_t rtt_wr_11:2;
		uint64_t dic_11:2;
		uint64_t rtt_nom_11:3;
		uint64_t reserved_48_63:16;
#endif
	} s;
	struct cvmx_lmcx_modereg_params1_s cn61xx;
	struct cvmx_lmcx_modereg_params1_s cn63xx;
	struct cvmx_lmcx_modereg_params1_s cn63xxp1;
	struct cvmx_lmcx_modereg_params1_s cn66xx;
	struct cvmx_lmcx_modereg_params1_s cn68xx;
	struct cvmx_lmcx_modereg_params1_s cn68xxp1;
	struct cvmx_lmcx_modereg_params1_s cnf71xx;
};

union cvmx_lmcx_nxm {
	uint64_t u64;
	struct cvmx_lmcx_nxm_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_40_63:24;
		uint64_t mem_msb_d3_r1:4;
		uint64_t mem_msb_d3_r0:4;
		uint64_t mem_msb_d2_r1:4;
		uint64_t mem_msb_d2_r0:4;
		uint64_t mem_msb_d1_r1:4;
		uint64_t mem_msb_d1_r0:4;
		uint64_t mem_msb_d0_r1:4;
		uint64_t mem_msb_d0_r0:4;
		uint64_t cs_mask:8;
#else
		uint64_t cs_mask:8;
		uint64_t mem_msb_d0_r0:4;
		uint64_t mem_msb_d0_r1:4;
		uint64_t mem_msb_d1_r0:4;
		uint64_t mem_msb_d1_r1:4;
		uint64_t mem_msb_d2_r0:4;
		uint64_t mem_msb_d2_r1:4;
		uint64_t mem_msb_d3_r0:4;
		uint64_t mem_msb_d3_r1:4;
		uint64_t reserved_40_63:24;
#endif
	} s;
	struct cvmx_lmcx_nxm_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t cs_mask:8;
#else
		uint64_t cs_mask:8;
		uint64_t reserved_8_63:56;
#endif
	} cn52xx;
	struct cvmx_lmcx_nxm_cn52xx cn56xx;
	struct cvmx_lmcx_nxm_cn52xx cn58xx;
	struct cvmx_lmcx_nxm_s cn61xx;
	struct cvmx_lmcx_nxm_s cn63xx;
	struct cvmx_lmcx_nxm_s cn63xxp1;
	struct cvmx_lmcx_nxm_s cn66xx;
	struct cvmx_lmcx_nxm_s cn68xx;
	struct cvmx_lmcx_nxm_s cn68xxp1;
	struct cvmx_lmcx_nxm_s cnf71xx;
};

union cvmx_lmcx_ops_cnt {
	uint64_t u64;
	struct cvmx_lmcx_ops_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t opscnt:64;
#else
		uint64_t opscnt:64;
#endif
	} s;
	struct cvmx_lmcx_ops_cnt_s cn61xx;
	struct cvmx_lmcx_ops_cnt_s cn63xx;
	struct cvmx_lmcx_ops_cnt_s cn63xxp1;
	struct cvmx_lmcx_ops_cnt_s cn66xx;
	struct cvmx_lmcx_ops_cnt_s cn68xx;
	struct cvmx_lmcx_ops_cnt_s cn68xxp1;
	struct cvmx_lmcx_ops_cnt_s cnf71xx;
};

union cvmx_lmcx_ops_cnt_hi {
	uint64_t u64;
	struct cvmx_lmcx_ops_cnt_hi_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t opscnt_hi:32;
#else
		uint64_t opscnt_hi:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_ops_cnt_hi_s cn30xx;
	struct cvmx_lmcx_ops_cnt_hi_s cn31xx;
	struct cvmx_lmcx_ops_cnt_hi_s cn38xx;
	struct cvmx_lmcx_ops_cnt_hi_s cn38xxp2;
	struct cvmx_lmcx_ops_cnt_hi_s cn50xx;
	struct cvmx_lmcx_ops_cnt_hi_s cn52xx;
	struct cvmx_lmcx_ops_cnt_hi_s cn52xxp1;
	struct cvmx_lmcx_ops_cnt_hi_s cn56xx;
	struct cvmx_lmcx_ops_cnt_hi_s cn56xxp1;
	struct cvmx_lmcx_ops_cnt_hi_s cn58xx;
	struct cvmx_lmcx_ops_cnt_hi_s cn58xxp1;
};

union cvmx_lmcx_ops_cnt_lo {
	uint64_t u64;
	struct cvmx_lmcx_ops_cnt_lo_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t opscnt_lo:32;
#else
		uint64_t opscnt_lo:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_ops_cnt_lo_s cn30xx;
	struct cvmx_lmcx_ops_cnt_lo_s cn31xx;
	struct cvmx_lmcx_ops_cnt_lo_s cn38xx;
	struct cvmx_lmcx_ops_cnt_lo_s cn38xxp2;
	struct cvmx_lmcx_ops_cnt_lo_s cn50xx;
	struct cvmx_lmcx_ops_cnt_lo_s cn52xx;
	struct cvmx_lmcx_ops_cnt_lo_s cn52xxp1;
	struct cvmx_lmcx_ops_cnt_lo_s cn56xx;
	struct cvmx_lmcx_ops_cnt_lo_s cn56xxp1;
	struct cvmx_lmcx_ops_cnt_lo_s cn58xx;
	struct cvmx_lmcx_ops_cnt_lo_s cn58xxp1;
};

union cvmx_lmcx_phy_ctl {
	uint64_t u64;
	struct cvmx_lmcx_phy_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t rx_always_on:1;
		uint64_t lv_mode:1;
		uint64_t ck_tune1:1;
		uint64_t ck_dlyout1:4;
		uint64_t ck_tune0:1;
		uint64_t ck_dlyout0:4;
		uint64_t loopback:1;
		uint64_t loopback_pos:1;
		uint64_t ts_stagger:1;
#else
		uint64_t ts_stagger:1;
		uint64_t loopback_pos:1;
		uint64_t loopback:1;
		uint64_t ck_dlyout0:4;
		uint64_t ck_tune0:1;
		uint64_t ck_dlyout1:4;
		uint64_t ck_tune1:1;
		uint64_t lv_mode:1;
		uint64_t rx_always_on:1;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_lmcx_phy_ctl_s cn61xx;
	struct cvmx_lmcx_phy_ctl_s cn63xx;
	struct cvmx_lmcx_phy_ctl_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_14_63:50;
		uint64_t lv_mode:1;
		uint64_t ck_tune1:1;
		uint64_t ck_dlyout1:4;
		uint64_t ck_tune0:1;
		uint64_t ck_dlyout0:4;
		uint64_t loopback:1;
		uint64_t loopback_pos:1;
		uint64_t ts_stagger:1;
#else
		uint64_t ts_stagger:1;
		uint64_t loopback_pos:1;
		uint64_t loopback:1;
		uint64_t ck_dlyout0:4;
		uint64_t ck_tune0:1;
		uint64_t ck_dlyout1:4;
		uint64_t ck_tune1:1;
		uint64_t lv_mode:1;
		uint64_t reserved_14_63:50;
#endif
	} cn63xxp1;
	struct cvmx_lmcx_phy_ctl_s cn66xx;
	struct cvmx_lmcx_phy_ctl_s cn68xx;
	struct cvmx_lmcx_phy_ctl_s cn68xxp1;
	struct cvmx_lmcx_phy_ctl_s cnf71xx;
};

union cvmx_lmcx_pll_bwctl {
	uint64_t u64;
	struct cvmx_lmcx_pll_bwctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t bwupd:1;
		uint64_t bwctl:4;
#else
		uint64_t bwctl:4;
		uint64_t bwupd:1;
		uint64_t reserved_5_63:59;
#endif
	} s;
	struct cvmx_lmcx_pll_bwctl_s cn30xx;
	struct cvmx_lmcx_pll_bwctl_s cn31xx;
	struct cvmx_lmcx_pll_bwctl_s cn38xx;
	struct cvmx_lmcx_pll_bwctl_s cn38xxp2;
};

union cvmx_lmcx_pll_ctl {
	uint64_t u64;
	struct cvmx_lmcx_pll_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_30_63:34;
		uint64_t bypass:1;
		uint64_t fasten_n:1;
		uint64_t div_reset:1;
		uint64_t reset_n:1;
		uint64_t clkf:12;
		uint64_t clkr:6;
		uint64_t reserved_6_7:2;
		uint64_t en16:1;
		uint64_t en12:1;
		uint64_t en8:1;
		uint64_t en6:1;
		uint64_t en4:1;
		uint64_t en2:1;
#else
		uint64_t en2:1;
		uint64_t en4:1;
		uint64_t en6:1;
		uint64_t en8:1;
		uint64_t en12:1;
		uint64_t en16:1;
		uint64_t reserved_6_7:2;
		uint64_t clkr:6;
		uint64_t clkf:12;
		uint64_t reset_n:1;
		uint64_t div_reset:1;
		uint64_t fasten_n:1;
		uint64_t bypass:1;
		uint64_t reserved_30_63:34;
#endif
	} s;
	struct cvmx_lmcx_pll_ctl_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_29_63:35;
		uint64_t fasten_n:1;
		uint64_t div_reset:1;
		uint64_t reset_n:1;
		uint64_t clkf:12;
		uint64_t clkr:6;
		uint64_t reserved_6_7:2;
		uint64_t en16:1;
		uint64_t en12:1;
		uint64_t en8:1;
		uint64_t en6:1;
		uint64_t en4:1;
		uint64_t en2:1;
#else
		uint64_t en2:1;
		uint64_t en4:1;
		uint64_t en6:1;
		uint64_t en8:1;
		uint64_t en12:1;
		uint64_t en16:1;
		uint64_t reserved_6_7:2;
		uint64_t clkr:6;
		uint64_t clkf:12;
		uint64_t reset_n:1;
		uint64_t div_reset:1;
		uint64_t fasten_n:1;
		uint64_t reserved_29_63:35;
#endif
	} cn50xx;
	struct cvmx_lmcx_pll_ctl_s cn52xx;
	struct cvmx_lmcx_pll_ctl_s cn52xxp1;
	struct cvmx_lmcx_pll_ctl_cn50xx cn56xx;
	struct cvmx_lmcx_pll_ctl_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_28_63:36;
		uint64_t div_reset:1;
		uint64_t reset_n:1;
		uint64_t clkf:12;
		uint64_t clkr:6;
		uint64_t reserved_6_7:2;
		uint64_t en16:1;
		uint64_t en12:1;
		uint64_t en8:1;
		uint64_t en6:1;
		uint64_t en4:1;
		uint64_t en2:1;
#else
		uint64_t en2:1;
		uint64_t en4:1;
		uint64_t en6:1;
		uint64_t en8:1;
		uint64_t en12:1;
		uint64_t en16:1;
		uint64_t reserved_6_7:2;
		uint64_t clkr:6;
		uint64_t clkf:12;
		uint64_t reset_n:1;
		uint64_t div_reset:1;
		uint64_t reserved_28_63:36;
#endif
	} cn56xxp1;
	struct cvmx_lmcx_pll_ctl_cn56xxp1 cn58xx;
	struct cvmx_lmcx_pll_ctl_cn56xxp1 cn58xxp1;
};

union cvmx_lmcx_pll_status {
	uint64_t u64;
	struct cvmx_lmcx_pll_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ddr__nctl:5;
		uint64_t ddr__pctl:5;
		uint64_t reserved_2_21:20;
		uint64_t rfslip:1;
		uint64_t fbslip:1;
#else
		uint64_t fbslip:1;
		uint64_t rfslip:1;
		uint64_t reserved_2_21:20;
		uint64_t ddr__pctl:5;
		uint64_t ddr__nctl:5;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_pll_status_s cn50xx;
	struct cvmx_lmcx_pll_status_s cn52xx;
	struct cvmx_lmcx_pll_status_s cn52xxp1;
	struct cvmx_lmcx_pll_status_s cn56xx;
	struct cvmx_lmcx_pll_status_s cn56xxp1;
	struct cvmx_lmcx_pll_status_s cn58xx;
	struct cvmx_lmcx_pll_status_cn58xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t rfslip:1;
		uint64_t fbslip:1;
#else
		uint64_t fbslip:1;
		uint64_t rfslip:1;
		uint64_t reserved_2_63:62;
#endif
	} cn58xxp1;
};

union cvmx_lmcx_read_level_ctl {
	uint64_t u64;
	struct cvmx_lmcx_read_level_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_44_63:20;
		uint64_t rankmask:4;
		uint64_t pattern:8;
		uint64_t row:16;
		uint64_t col:12;
		uint64_t reserved_3_3:1;
		uint64_t bnk:3;
#else
		uint64_t bnk:3;
		uint64_t reserved_3_3:1;
		uint64_t col:12;
		uint64_t row:16;
		uint64_t pattern:8;
		uint64_t rankmask:4;
		uint64_t reserved_44_63:20;
#endif
	} s;
	struct cvmx_lmcx_read_level_ctl_s cn52xx;
	struct cvmx_lmcx_read_level_ctl_s cn52xxp1;
	struct cvmx_lmcx_read_level_ctl_s cn56xx;
	struct cvmx_lmcx_read_level_ctl_s cn56xxp1;
};

union cvmx_lmcx_read_level_dbg {
	uint64_t u64;
	struct cvmx_lmcx_read_level_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t bitmask:16;
		uint64_t reserved_4_15:12;
		uint64_t byte:4;
#else
		uint64_t byte:4;
		uint64_t reserved_4_15:12;
		uint64_t bitmask:16;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_read_level_dbg_s cn52xx;
	struct cvmx_lmcx_read_level_dbg_s cn52xxp1;
	struct cvmx_lmcx_read_level_dbg_s cn56xx;
	struct cvmx_lmcx_read_level_dbg_s cn56xxp1;
};

union cvmx_lmcx_read_level_rankx {
	uint64_t u64;
	struct cvmx_lmcx_read_level_rankx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_38_63:26;
		uint64_t status:2;
		uint64_t byte8:4;
		uint64_t byte7:4;
		uint64_t byte6:4;
		uint64_t byte5:4;
		uint64_t byte4:4;
		uint64_t byte3:4;
		uint64_t byte2:4;
		uint64_t byte1:4;
		uint64_t byte0:4;
#else
		uint64_t byte0:4;
		uint64_t byte1:4;
		uint64_t byte2:4;
		uint64_t byte3:4;
		uint64_t byte4:4;
		uint64_t byte5:4;
		uint64_t byte6:4;
		uint64_t byte7:4;
		uint64_t byte8:4;
		uint64_t status:2;
		uint64_t reserved_38_63:26;
#endif
	} s;
	struct cvmx_lmcx_read_level_rankx_s cn52xx;
	struct cvmx_lmcx_read_level_rankx_s cn52xxp1;
	struct cvmx_lmcx_read_level_rankx_s cn56xx;
	struct cvmx_lmcx_read_level_rankx_s cn56xxp1;
};

union cvmx_lmcx_reset_ctl {
	uint64_t u64;
	struct cvmx_lmcx_reset_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t ddr3psv:1;
		uint64_t ddr3psoft:1;
		uint64_t ddr3pwarm:1;
		uint64_t ddr3rst:1;
#else
		uint64_t ddr3rst:1;
		uint64_t ddr3pwarm:1;
		uint64_t ddr3psoft:1;
		uint64_t ddr3psv:1;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_lmcx_reset_ctl_s cn61xx;
	struct cvmx_lmcx_reset_ctl_s cn63xx;
	struct cvmx_lmcx_reset_ctl_s cn63xxp1;
	struct cvmx_lmcx_reset_ctl_s cn66xx;
	struct cvmx_lmcx_reset_ctl_s cn68xx;
	struct cvmx_lmcx_reset_ctl_s cn68xxp1;
	struct cvmx_lmcx_reset_ctl_s cnf71xx;
};

union cvmx_lmcx_rlevel_ctl {
	uint64_t u64;
	struct cvmx_lmcx_rlevel_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t delay_unload_3:1;
		uint64_t delay_unload_2:1;
		uint64_t delay_unload_1:1;
		uint64_t delay_unload_0:1;
		uint64_t bitmask:8;
		uint64_t or_dis:1;
		uint64_t offset_en:1;
		uint64_t offset:4;
		uint64_t byte:4;
#else
		uint64_t byte:4;
		uint64_t offset:4;
		uint64_t offset_en:1;
		uint64_t or_dis:1;
		uint64_t bitmask:8;
		uint64_t delay_unload_0:1;
		uint64_t delay_unload_1:1;
		uint64_t delay_unload_2:1;
		uint64_t delay_unload_3:1;
		uint64_t reserved_22_63:42;
#endif
	} s;
	struct cvmx_lmcx_rlevel_ctl_s cn61xx;
	struct cvmx_lmcx_rlevel_ctl_s cn63xx;
	struct cvmx_lmcx_rlevel_ctl_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_9_63:55;
		uint64_t offset_en:1;
		uint64_t offset:4;
		uint64_t byte:4;
#else
		uint64_t byte:4;
		uint64_t offset:4;
		uint64_t offset_en:1;
		uint64_t reserved_9_63:55;
#endif
	} cn63xxp1;
	struct cvmx_lmcx_rlevel_ctl_s cn66xx;
	struct cvmx_lmcx_rlevel_ctl_s cn68xx;
	struct cvmx_lmcx_rlevel_ctl_s cn68xxp1;
	struct cvmx_lmcx_rlevel_ctl_s cnf71xx;
};

union cvmx_lmcx_rlevel_dbg {
	uint64_t u64;
	struct cvmx_lmcx_rlevel_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bitmask:64;
#else
		uint64_t bitmask:64;
#endif
	} s;
	struct cvmx_lmcx_rlevel_dbg_s cn61xx;
	struct cvmx_lmcx_rlevel_dbg_s cn63xx;
	struct cvmx_lmcx_rlevel_dbg_s cn63xxp1;
	struct cvmx_lmcx_rlevel_dbg_s cn66xx;
	struct cvmx_lmcx_rlevel_dbg_s cn68xx;
	struct cvmx_lmcx_rlevel_dbg_s cn68xxp1;
	struct cvmx_lmcx_rlevel_dbg_s cnf71xx;
};

union cvmx_lmcx_rlevel_rankx {
	uint64_t u64;
	struct cvmx_lmcx_rlevel_rankx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t status:2;
		uint64_t byte8:6;
		uint64_t byte7:6;
		uint64_t byte6:6;
		uint64_t byte5:6;
		uint64_t byte4:6;
		uint64_t byte3:6;
		uint64_t byte2:6;
		uint64_t byte1:6;
		uint64_t byte0:6;
#else
		uint64_t byte0:6;
		uint64_t byte1:6;
		uint64_t byte2:6;
		uint64_t byte3:6;
		uint64_t byte4:6;
		uint64_t byte5:6;
		uint64_t byte6:6;
		uint64_t byte7:6;
		uint64_t byte8:6;
		uint64_t status:2;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_lmcx_rlevel_rankx_s cn61xx;
	struct cvmx_lmcx_rlevel_rankx_s cn63xx;
	struct cvmx_lmcx_rlevel_rankx_s cn63xxp1;
	struct cvmx_lmcx_rlevel_rankx_s cn66xx;
	struct cvmx_lmcx_rlevel_rankx_s cn68xx;
	struct cvmx_lmcx_rlevel_rankx_s cn68xxp1;
	struct cvmx_lmcx_rlevel_rankx_s cnf71xx;
};

union cvmx_lmcx_rodt_comp_ctl {
	uint64_t u64;
	struct cvmx_lmcx_rodt_comp_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t enable:1;
		uint64_t reserved_12_15:4;
		uint64_t nctl:4;
		uint64_t reserved_5_7:3;
		uint64_t pctl:5;
#else
		uint64_t pctl:5;
		uint64_t reserved_5_7:3;
		uint64_t nctl:4;
		uint64_t reserved_12_15:4;
		uint64_t enable:1;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_lmcx_rodt_comp_ctl_s cn50xx;
	struct cvmx_lmcx_rodt_comp_ctl_s cn52xx;
	struct cvmx_lmcx_rodt_comp_ctl_s cn52xxp1;
	struct cvmx_lmcx_rodt_comp_ctl_s cn56xx;
	struct cvmx_lmcx_rodt_comp_ctl_s cn56xxp1;
	struct cvmx_lmcx_rodt_comp_ctl_s cn58xx;
	struct cvmx_lmcx_rodt_comp_ctl_s cn58xxp1;
};

union cvmx_lmcx_rodt_ctl {
	uint64_t u64;
	struct cvmx_lmcx_rodt_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t rodt_hi3:4;
		uint64_t rodt_hi2:4;
		uint64_t rodt_hi1:4;
		uint64_t rodt_hi0:4;
		uint64_t rodt_lo3:4;
		uint64_t rodt_lo2:4;
		uint64_t rodt_lo1:4;
		uint64_t rodt_lo0:4;
#else
		uint64_t rodt_lo0:4;
		uint64_t rodt_lo1:4;
		uint64_t rodt_lo2:4;
		uint64_t rodt_lo3:4;
		uint64_t rodt_hi0:4;
		uint64_t rodt_hi1:4;
		uint64_t rodt_hi2:4;
		uint64_t rodt_hi3:4;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_rodt_ctl_s cn30xx;
	struct cvmx_lmcx_rodt_ctl_s cn31xx;
	struct cvmx_lmcx_rodt_ctl_s cn38xx;
	struct cvmx_lmcx_rodt_ctl_s cn38xxp2;
	struct cvmx_lmcx_rodt_ctl_s cn50xx;
	struct cvmx_lmcx_rodt_ctl_s cn52xx;
	struct cvmx_lmcx_rodt_ctl_s cn52xxp1;
	struct cvmx_lmcx_rodt_ctl_s cn56xx;
	struct cvmx_lmcx_rodt_ctl_s cn56xxp1;
	struct cvmx_lmcx_rodt_ctl_s cn58xx;
	struct cvmx_lmcx_rodt_ctl_s cn58xxp1;
};

union cvmx_lmcx_rodt_mask {
	uint64_t u64;
	struct cvmx_lmcx_rodt_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rodt_d3_r1:8;
		uint64_t rodt_d3_r0:8;
		uint64_t rodt_d2_r1:8;
		uint64_t rodt_d2_r0:8;
		uint64_t rodt_d1_r1:8;
		uint64_t rodt_d1_r0:8;
		uint64_t rodt_d0_r1:8;
		uint64_t rodt_d0_r0:8;
#else
		uint64_t rodt_d0_r0:8;
		uint64_t rodt_d0_r1:8;
		uint64_t rodt_d1_r0:8;
		uint64_t rodt_d1_r1:8;
		uint64_t rodt_d2_r0:8;
		uint64_t rodt_d2_r1:8;
		uint64_t rodt_d3_r0:8;
		uint64_t rodt_d3_r1:8;
#endif
	} s;
	struct cvmx_lmcx_rodt_mask_s cn61xx;
	struct cvmx_lmcx_rodt_mask_s cn63xx;
	struct cvmx_lmcx_rodt_mask_s cn63xxp1;
	struct cvmx_lmcx_rodt_mask_s cn66xx;
	struct cvmx_lmcx_rodt_mask_s cn68xx;
	struct cvmx_lmcx_rodt_mask_s cn68xxp1;
	struct cvmx_lmcx_rodt_mask_s cnf71xx;
};

union cvmx_lmcx_scramble_cfg0 {
	uint64_t u64;
	struct cvmx_lmcx_scramble_cfg0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t key:64;
#else
		uint64_t key:64;
#endif
	} s;
	struct cvmx_lmcx_scramble_cfg0_s cn61xx;
	struct cvmx_lmcx_scramble_cfg0_s cn66xx;
	struct cvmx_lmcx_scramble_cfg0_s cnf71xx;
};

union cvmx_lmcx_scramble_cfg1 {
	uint64_t u64;
	struct cvmx_lmcx_scramble_cfg1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t key:64;
#else
		uint64_t key:64;
#endif
	} s;
	struct cvmx_lmcx_scramble_cfg1_s cn61xx;
	struct cvmx_lmcx_scramble_cfg1_s cn66xx;
	struct cvmx_lmcx_scramble_cfg1_s cnf71xx;
};

union cvmx_lmcx_scrambled_fadr {
	uint64_t u64;
	struct cvmx_lmcx_scrambled_fadr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_36_63:28;
		uint64_t fdimm:2;
		uint64_t fbunk:1;
		uint64_t fbank:3;
		uint64_t frow:16;
		uint64_t fcol:14;
#else
		uint64_t fcol:14;
		uint64_t frow:16;
		uint64_t fbank:3;
		uint64_t fbunk:1;
		uint64_t fdimm:2;
		uint64_t reserved_36_63:28;
#endif
	} s;
	struct cvmx_lmcx_scrambled_fadr_s cn61xx;
	struct cvmx_lmcx_scrambled_fadr_s cn66xx;
	struct cvmx_lmcx_scrambled_fadr_s cnf71xx;
};

union cvmx_lmcx_slot_ctl0 {
	uint64_t u64;
	struct cvmx_lmcx_slot_ctl0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t w2w_init:6;
		uint64_t w2r_init:6;
		uint64_t r2w_init:6;
		uint64_t r2r_init:6;
#else
		uint64_t r2r_init:6;
		uint64_t r2w_init:6;
		uint64_t w2r_init:6;
		uint64_t w2w_init:6;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_lmcx_slot_ctl0_s cn61xx;
	struct cvmx_lmcx_slot_ctl0_s cn63xx;
	struct cvmx_lmcx_slot_ctl0_s cn63xxp1;
	struct cvmx_lmcx_slot_ctl0_s cn66xx;
	struct cvmx_lmcx_slot_ctl0_s cn68xx;
	struct cvmx_lmcx_slot_ctl0_s cn68xxp1;
	struct cvmx_lmcx_slot_ctl0_s cnf71xx;
};

union cvmx_lmcx_slot_ctl1 {
	uint64_t u64;
	struct cvmx_lmcx_slot_ctl1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t w2w_xrank_init:6;
		uint64_t w2r_xrank_init:6;
		uint64_t r2w_xrank_init:6;
		uint64_t r2r_xrank_init:6;
#else
		uint64_t r2r_xrank_init:6;
		uint64_t r2w_xrank_init:6;
		uint64_t w2r_xrank_init:6;
		uint64_t w2w_xrank_init:6;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_lmcx_slot_ctl1_s cn61xx;
	struct cvmx_lmcx_slot_ctl1_s cn63xx;
	struct cvmx_lmcx_slot_ctl1_s cn63xxp1;
	struct cvmx_lmcx_slot_ctl1_s cn66xx;
	struct cvmx_lmcx_slot_ctl1_s cn68xx;
	struct cvmx_lmcx_slot_ctl1_s cn68xxp1;
	struct cvmx_lmcx_slot_ctl1_s cnf71xx;
};

union cvmx_lmcx_slot_ctl2 {
	uint64_t u64;
	struct cvmx_lmcx_slot_ctl2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t w2w_xdimm_init:6;
		uint64_t w2r_xdimm_init:6;
		uint64_t r2w_xdimm_init:6;
		uint64_t r2r_xdimm_init:6;
#else
		uint64_t r2r_xdimm_init:6;
		uint64_t r2w_xdimm_init:6;
		uint64_t w2r_xdimm_init:6;
		uint64_t w2w_xdimm_init:6;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_lmcx_slot_ctl2_s cn61xx;
	struct cvmx_lmcx_slot_ctl2_s cn63xx;
	struct cvmx_lmcx_slot_ctl2_s cn63xxp1;
	struct cvmx_lmcx_slot_ctl2_s cn66xx;
	struct cvmx_lmcx_slot_ctl2_s cn68xx;
	struct cvmx_lmcx_slot_ctl2_s cn68xxp1;
	struct cvmx_lmcx_slot_ctl2_s cnf71xx;
};

union cvmx_lmcx_timing_params0 {
	uint64_t u64;
	struct cvmx_lmcx_timing_params0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_47_63:17;
		uint64_t trp_ext:1;
		uint64_t tcksre:4;
		uint64_t trp:4;
		uint64_t tzqinit:4;
		uint64_t tdllk:4;
		uint64_t tmod:4;
		uint64_t tmrd:4;
		uint64_t txpr:4;
		uint64_t tcke:4;
		uint64_t tzqcs:4;
		uint64_t tckeon:10;
#else
		uint64_t tckeon:10;
		uint64_t tzqcs:4;
		uint64_t tcke:4;
		uint64_t txpr:4;
		uint64_t tmrd:4;
		uint64_t tmod:4;
		uint64_t tdllk:4;
		uint64_t tzqinit:4;
		uint64_t trp:4;
		uint64_t tcksre:4;
		uint64_t trp_ext:1;
		uint64_t reserved_47_63:17;
#endif
	} s;
	struct cvmx_lmcx_timing_params0_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_47_63:17;
		uint64_t trp_ext:1;
		uint64_t tcksre:4;
		uint64_t trp:4;
		uint64_t tzqinit:4;
		uint64_t tdllk:4;
		uint64_t tmod:4;
		uint64_t tmrd:4;
		uint64_t txpr:4;
		uint64_t tcke:4;
		uint64_t tzqcs:4;
		uint64_t reserved_0_9:10;
#else
		uint64_t reserved_0_9:10;
		uint64_t tzqcs:4;
		uint64_t tcke:4;
		uint64_t txpr:4;
		uint64_t tmrd:4;
		uint64_t tmod:4;
		uint64_t tdllk:4;
		uint64_t tzqinit:4;
		uint64_t trp:4;
		uint64_t tcksre:4;
		uint64_t trp_ext:1;
		uint64_t reserved_47_63:17;
#endif
	} cn61xx;
	struct cvmx_lmcx_timing_params0_cn61xx cn63xx;
	struct cvmx_lmcx_timing_params0_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_46_63:18;
		uint64_t tcksre:4;
		uint64_t trp:4;
		uint64_t tzqinit:4;
		uint64_t tdllk:4;
		uint64_t tmod:4;
		uint64_t tmrd:4;
		uint64_t txpr:4;
		uint64_t tcke:4;
		uint64_t tzqcs:4;
		uint64_t tckeon:10;
#else
		uint64_t tckeon:10;
		uint64_t tzqcs:4;
		uint64_t tcke:4;
		uint64_t txpr:4;
		uint64_t tmrd:4;
		uint64_t tmod:4;
		uint64_t tdllk:4;
		uint64_t tzqinit:4;
		uint64_t trp:4;
		uint64_t tcksre:4;
		uint64_t reserved_46_63:18;
#endif
	} cn63xxp1;
	struct cvmx_lmcx_timing_params0_cn61xx cn66xx;
	struct cvmx_lmcx_timing_params0_cn61xx cn68xx;
	struct cvmx_lmcx_timing_params0_cn61xx cn68xxp1;
	struct cvmx_lmcx_timing_params0_cn61xx cnf71xx;
};

union cvmx_lmcx_timing_params1 {
	uint64_t u64;
	struct cvmx_lmcx_timing_params1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_47_63:17;
		uint64_t tras_ext:1;
		uint64_t txpdll:5;
		uint64_t tfaw:5;
		uint64_t twldqsen:4;
		uint64_t twlmrd:4;
		uint64_t txp:3;
		uint64_t trrd:3;
		uint64_t trfc:5;
		uint64_t twtr:4;
		uint64_t trcd:4;
		uint64_t tras:5;
		uint64_t tmprr:4;
#else
		uint64_t tmprr:4;
		uint64_t tras:5;
		uint64_t trcd:4;
		uint64_t twtr:4;
		uint64_t trfc:5;
		uint64_t trrd:3;
		uint64_t txp:3;
		uint64_t twlmrd:4;
		uint64_t twldqsen:4;
		uint64_t tfaw:5;
		uint64_t txpdll:5;
		uint64_t tras_ext:1;
		uint64_t reserved_47_63:17;
#endif
	} s;
	struct cvmx_lmcx_timing_params1_s cn61xx;
	struct cvmx_lmcx_timing_params1_s cn63xx;
	struct cvmx_lmcx_timing_params1_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_46_63:18;
		uint64_t txpdll:5;
		uint64_t tfaw:5;
		uint64_t twldqsen:4;
		uint64_t twlmrd:4;
		uint64_t txp:3;
		uint64_t trrd:3;
		uint64_t trfc:5;
		uint64_t twtr:4;
		uint64_t trcd:4;
		uint64_t tras:5;
		uint64_t tmprr:4;
#else
		uint64_t tmprr:4;
		uint64_t tras:5;
		uint64_t trcd:4;
		uint64_t twtr:4;
		uint64_t trfc:5;
		uint64_t trrd:3;
		uint64_t txp:3;
		uint64_t twlmrd:4;
		uint64_t twldqsen:4;
		uint64_t tfaw:5;
		uint64_t txpdll:5;
		uint64_t reserved_46_63:18;
#endif
	} cn63xxp1;
	struct cvmx_lmcx_timing_params1_s cn66xx;
	struct cvmx_lmcx_timing_params1_s cn68xx;
	struct cvmx_lmcx_timing_params1_s cn68xxp1;
	struct cvmx_lmcx_timing_params1_s cnf71xx;
};

union cvmx_lmcx_tro_ctl {
	uint64_t u64;
	struct cvmx_lmcx_tro_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_33_63:31;
		uint64_t rclk_cnt:32;
		uint64_t treset:1;
#else
		uint64_t treset:1;
		uint64_t rclk_cnt:32;
		uint64_t reserved_33_63:31;
#endif
	} s;
	struct cvmx_lmcx_tro_ctl_s cn61xx;
	struct cvmx_lmcx_tro_ctl_s cn63xx;
	struct cvmx_lmcx_tro_ctl_s cn63xxp1;
	struct cvmx_lmcx_tro_ctl_s cn66xx;
	struct cvmx_lmcx_tro_ctl_s cn68xx;
	struct cvmx_lmcx_tro_ctl_s cn68xxp1;
	struct cvmx_lmcx_tro_ctl_s cnf71xx;
};

union cvmx_lmcx_tro_stat {
	uint64_t u64;
	struct cvmx_lmcx_tro_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ring_cnt:32;
#else
		uint64_t ring_cnt:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_tro_stat_s cn61xx;
	struct cvmx_lmcx_tro_stat_s cn63xx;
	struct cvmx_lmcx_tro_stat_s cn63xxp1;
	struct cvmx_lmcx_tro_stat_s cn66xx;
	struct cvmx_lmcx_tro_stat_s cn68xx;
	struct cvmx_lmcx_tro_stat_s cn68xxp1;
	struct cvmx_lmcx_tro_stat_s cnf71xx;
};

union cvmx_lmcx_wlevel_ctl {
	uint64_t u64;
	struct cvmx_lmcx_wlevel_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_22_63:42;
		uint64_t rtt_nom:3;
		uint64_t bitmask:8;
		uint64_t or_dis:1;
		uint64_t sset:1;
		uint64_t lanemask:9;
#else
		uint64_t lanemask:9;
		uint64_t sset:1;
		uint64_t or_dis:1;
		uint64_t bitmask:8;
		uint64_t rtt_nom:3;
		uint64_t reserved_22_63:42;
#endif
	} s;
	struct cvmx_lmcx_wlevel_ctl_s cn61xx;
	struct cvmx_lmcx_wlevel_ctl_s cn63xx;
	struct cvmx_lmcx_wlevel_ctl_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t sset:1;
		uint64_t lanemask:9;
#else
		uint64_t lanemask:9;
		uint64_t sset:1;
		uint64_t reserved_10_63:54;
#endif
	} cn63xxp1;
	struct cvmx_lmcx_wlevel_ctl_s cn66xx;
	struct cvmx_lmcx_wlevel_ctl_s cn68xx;
	struct cvmx_lmcx_wlevel_ctl_s cn68xxp1;
	struct cvmx_lmcx_wlevel_ctl_s cnf71xx;
};

union cvmx_lmcx_wlevel_dbg {
	uint64_t u64;
	struct cvmx_lmcx_wlevel_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t bitmask:8;
		uint64_t byte:4;
#else
		uint64_t byte:4;
		uint64_t bitmask:8;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_lmcx_wlevel_dbg_s cn61xx;
	struct cvmx_lmcx_wlevel_dbg_s cn63xx;
	struct cvmx_lmcx_wlevel_dbg_s cn63xxp1;
	struct cvmx_lmcx_wlevel_dbg_s cn66xx;
	struct cvmx_lmcx_wlevel_dbg_s cn68xx;
	struct cvmx_lmcx_wlevel_dbg_s cn68xxp1;
	struct cvmx_lmcx_wlevel_dbg_s cnf71xx;
};

union cvmx_lmcx_wlevel_rankx {
	uint64_t u64;
	struct cvmx_lmcx_wlevel_rankx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_47_63:17;
		uint64_t status:2;
		uint64_t byte8:5;
		uint64_t byte7:5;
		uint64_t byte6:5;
		uint64_t byte5:5;
		uint64_t byte4:5;
		uint64_t byte3:5;
		uint64_t byte2:5;
		uint64_t byte1:5;
		uint64_t byte0:5;
#else
		uint64_t byte0:5;
		uint64_t byte1:5;
		uint64_t byte2:5;
		uint64_t byte3:5;
		uint64_t byte4:5;
		uint64_t byte5:5;
		uint64_t byte6:5;
		uint64_t byte7:5;
		uint64_t byte8:5;
		uint64_t status:2;
		uint64_t reserved_47_63:17;
#endif
	} s;
	struct cvmx_lmcx_wlevel_rankx_s cn61xx;
	struct cvmx_lmcx_wlevel_rankx_s cn63xx;
	struct cvmx_lmcx_wlevel_rankx_s cn63xxp1;
	struct cvmx_lmcx_wlevel_rankx_s cn66xx;
	struct cvmx_lmcx_wlevel_rankx_s cn68xx;
	struct cvmx_lmcx_wlevel_rankx_s cn68xxp1;
	struct cvmx_lmcx_wlevel_rankx_s cnf71xx;
};

union cvmx_lmcx_wodt_ctl0 {
	uint64_t u64;
	struct cvmx_lmcx_wodt_ctl0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_0_63:64;
#else
		uint64_t reserved_0_63:64;
#endif
	} s;
	struct cvmx_lmcx_wodt_ctl0_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wodt_d1_r1:8;
		uint64_t wodt_d1_r0:8;
		uint64_t wodt_d0_r1:8;
		uint64_t wodt_d0_r0:8;
#else
		uint64_t wodt_d0_r0:8;
		uint64_t wodt_d0_r1:8;
		uint64_t wodt_d1_r0:8;
		uint64_t wodt_d1_r1:8;
		uint64_t reserved_32_63:32;
#endif
	} cn30xx;
	struct cvmx_lmcx_wodt_ctl0_cn30xx cn31xx;
	struct cvmx_lmcx_wodt_ctl0_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wodt_hi3:4;
		uint64_t wodt_hi2:4;
		uint64_t wodt_hi1:4;
		uint64_t wodt_hi0:4;
		uint64_t wodt_lo3:4;
		uint64_t wodt_lo2:4;
		uint64_t wodt_lo1:4;
		uint64_t wodt_lo0:4;
#else
		uint64_t wodt_lo0:4;
		uint64_t wodt_lo1:4;
		uint64_t wodt_lo2:4;
		uint64_t wodt_lo3:4;
		uint64_t wodt_hi0:4;
		uint64_t wodt_hi1:4;
		uint64_t wodt_hi2:4;
		uint64_t wodt_hi3:4;
		uint64_t reserved_32_63:32;
#endif
	} cn38xx;
	struct cvmx_lmcx_wodt_ctl0_cn38xx cn38xxp2;
	struct cvmx_lmcx_wodt_ctl0_cn38xx cn50xx;
	struct cvmx_lmcx_wodt_ctl0_cn30xx cn52xx;
	struct cvmx_lmcx_wodt_ctl0_cn30xx cn52xxp1;
	struct cvmx_lmcx_wodt_ctl0_cn30xx cn56xx;
	struct cvmx_lmcx_wodt_ctl0_cn30xx cn56xxp1;
	struct cvmx_lmcx_wodt_ctl0_cn38xx cn58xx;
	struct cvmx_lmcx_wodt_ctl0_cn38xx cn58xxp1;
};

union cvmx_lmcx_wodt_ctl1 {
	uint64_t u64;
	struct cvmx_lmcx_wodt_ctl1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wodt_d3_r1:8;
		uint64_t wodt_d3_r0:8;
		uint64_t wodt_d2_r1:8;
		uint64_t wodt_d2_r0:8;
#else
		uint64_t wodt_d2_r0:8;
		uint64_t wodt_d2_r1:8;
		uint64_t wodt_d3_r0:8;
		uint64_t wodt_d3_r1:8;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_lmcx_wodt_ctl1_s cn30xx;
	struct cvmx_lmcx_wodt_ctl1_s cn31xx;
	struct cvmx_lmcx_wodt_ctl1_s cn52xx;
	struct cvmx_lmcx_wodt_ctl1_s cn52xxp1;
	struct cvmx_lmcx_wodt_ctl1_s cn56xx;
	struct cvmx_lmcx_wodt_ctl1_s cn56xxp1;
};

union cvmx_lmcx_wodt_mask {
	uint64_t u64;
	struct cvmx_lmcx_wodt_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t wodt_d3_r1:8;
		uint64_t wodt_d3_r0:8;
		uint64_t wodt_d2_r1:8;
		uint64_t wodt_d2_r0:8;
		uint64_t wodt_d1_r1:8;
		uint64_t wodt_d1_r0:8;
		uint64_t wodt_d0_r1:8;
		uint64_t wodt_d0_r0:8;
#else
		uint64_t wodt_d0_r0:8;
		uint64_t wodt_d0_r1:8;
		uint64_t wodt_d1_r0:8;
		uint64_t wodt_d1_r1:8;
		uint64_t wodt_d2_r0:8;
		uint64_t wodt_d2_r1:8;
		uint64_t wodt_d3_r0:8;
		uint64_t wodt_d3_r1:8;
#endif
	} s;
	struct cvmx_lmcx_wodt_mask_s cn61xx;
	struct cvmx_lmcx_wodt_mask_s cn63xx;
	struct cvmx_lmcx_wodt_mask_s cn63xxp1;
	struct cvmx_lmcx_wodt_mask_s cn66xx;
	struct cvmx_lmcx_wodt_mask_s cn68xx;
	struct cvmx_lmcx_wodt_mask_s cn68xxp1;
	struct cvmx_lmcx_wodt_mask_s cnf71xx;
};

#endif
