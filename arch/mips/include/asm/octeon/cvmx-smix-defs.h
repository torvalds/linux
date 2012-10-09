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

#ifndef __CVMX_SMIX_DEFS_H__
#define __CVMX_SMIX_DEFS_H__

static inline uint64_t CVMX_SMIX_CLK(unsigned long offset)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000001818ull) + (offset) * 256;
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000001818ull) + (offset) * 256;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000003818ull) + (offset) * 128;
	}
	return CVMX_ADD_IO_SEG(0x0001180000001818ull) + (offset) * 256;
}

static inline uint64_t CVMX_SMIX_CMD(unsigned long offset)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000001800ull) + (offset) * 256;
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000001800ull) + (offset) * 256;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000003800ull) + (offset) * 128;
	}
	return CVMX_ADD_IO_SEG(0x0001180000001800ull) + (offset) * 256;
}

static inline uint64_t CVMX_SMIX_EN(unsigned long offset)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000001820ull) + (offset) * 256;
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000001820ull) + (offset) * 256;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000003820ull) + (offset) * 128;
	}
	return CVMX_ADD_IO_SEG(0x0001180000001820ull) + (offset) * 256;
}

static inline uint64_t CVMX_SMIX_RD_DAT(unsigned long offset)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000001810ull) + (offset) * 256;
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000001810ull) + (offset) * 256;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000003810ull) + (offset) * 128;
	}
	return CVMX_ADD_IO_SEG(0x0001180000001810ull) + (offset) * 256;
}

static inline uint64_t CVMX_SMIX_WR_DAT(unsigned long offset)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000001808ull) + (offset) * 256;
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000001808ull) + (offset) * 256;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001180000003808ull) + (offset) * 128;
	}
	return CVMX_ADD_IO_SEG(0x0001180000001808ull) + (offset) * 256;
}

union cvmx_smix_clk {
	uint64_t u64;
	struct cvmx_smix_clk_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t mode:1;
		uint64_t reserved_21_23:3;
		uint64_t sample_hi:5;
		uint64_t sample_mode:1;
		uint64_t reserved_14_14:1;
		uint64_t clk_idle:1;
		uint64_t preamble:1;
		uint64_t sample:4;
		uint64_t phase:8;
#else
		uint64_t phase:8;
		uint64_t sample:4;
		uint64_t preamble:1;
		uint64_t clk_idle:1;
		uint64_t reserved_14_14:1;
		uint64_t sample_mode:1;
		uint64_t sample_hi:5;
		uint64_t reserved_21_23:3;
		uint64_t mode:1;
		uint64_t reserved_25_63:39;
#endif
	} s;
	struct cvmx_smix_clk_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_21_63:43;
		uint64_t sample_hi:5;
		uint64_t sample_mode:1;
		uint64_t reserved_14_14:1;
		uint64_t clk_idle:1;
		uint64_t preamble:1;
		uint64_t sample:4;
		uint64_t phase:8;
#else
		uint64_t phase:8;
		uint64_t sample:4;
		uint64_t preamble:1;
		uint64_t clk_idle:1;
		uint64_t reserved_14_14:1;
		uint64_t sample_mode:1;
		uint64_t sample_hi:5;
		uint64_t reserved_21_63:43;
#endif
	} cn30xx;
	struct cvmx_smix_clk_cn30xx cn31xx;
	struct cvmx_smix_clk_cn30xx cn38xx;
	struct cvmx_smix_clk_cn30xx cn38xxp2;
	struct cvmx_smix_clk_s cn50xx;
	struct cvmx_smix_clk_s cn52xx;
	struct cvmx_smix_clk_s cn52xxp1;
	struct cvmx_smix_clk_s cn56xx;
	struct cvmx_smix_clk_s cn56xxp1;
	struct cvmx_smix_clk_cn30xx cn58xx;
	struct cvmx_smix_clk_cn30xx cn58xxp1;
	struct cvmx_smix_clk_s cn61xx;
	struct cvmx_smix_clk_s cn63xx;
	struct cvmx_smix_clk_s cn63xxp1;
	struct cvmx_smix_clk_s cn66xx;
	struct cvmx_smix_clk_s cn68xx;
	struct cvmx_smix_clk_s cn68xxp1;
	struct cvmx_smix_clk_s cnf71xx;
};

union cvmx_smix_cmd {
	uint64_t u64;
	struct cvmx_smix_cmd_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t phy_op:2;
		uint64_t reserved_13_15:3;
		uint64_t phy_adr:5;
		uint64_t reserved_5_7:3;
		uint64_t reg_adr:5;
#else
		uint64_t reg_adr:5;
		uint64_t reserved_5_7:3;
		uint64_t phy_adr:5;
		uint64_t reserved_13_15:3;
		uint64_t phy_op:2;
		uint64_t reserved_18_63:46;
#endif
	} s;
	struct cvmx_smix_cmd_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t phy_op:1;
		uint64_t reserved_13_15:3;
		uint64_t phy_adr:5;
		uint64_t reserved_5_7:3;
		uint64_t reg_adr:5;
#else
		uint64_t reg_adr:5;
		uint64_t reserved_5_7:3;
		uint64_t phy_adr:5;
		uint64_t reserved_13_15:3;
		uint64_t phy_op:1;
		uint64_t reserved_17_63:47;
#endif
	} cn30xx;
	struct cvmx_smix_cmd_cn30xx cn31xx;
	struct cvmx_smix_cmd_cn30xx cn38xx;
	struct cvmx_smix_cmd_cn30xx cn38xxp2;
	struct cvmx_smix_cmd_s cn50xx;
	struct cvmx_smix_cmd_s cn52xx;
	struct cvmx_smix_cmd_s cn52xxp1;
	struct cvmx_smix_cmd_s cn56xx;
	struct cvmx_smix_cmd_s cn56xxp1;
	struct cvmx_smix_cmd_cn30xx cn58xx;
	struct cvmx_smix_cmd_cn30xx cn58xxp1;
	struct cvmx_smix_cmd_s cn61xx;
	struct cvmx_smix_cmd_s cn63xx;
	struct cvmx_smix_cmd_s cn63xxp1;
	struct cvmx_smix_cmd_s cn66xx;
	struct cvmx_smix_cmd_s cn68xx;
	struct cvmx_smix_cmd_s cn68xxp1;
	struct cvmx_smix_cmd_s cnf71xx;
};

union cvmx_smix_en {
	uint64_t u64;
	struct cvmx_smix_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_smix_en_s cn30xx;
	struct cvmx_smix_en_s cn31xx;
	struct cvmx_smix_en_s cn38xx;
	struct cvmx_smix_en_s cn38xxp2;
	struct cvmx_smix_en_s cn50xx;
	struct cvmx_smix_en_s cn52xx;
	struct cvmx_smix_en_s cn52xxp1;
	struct cvmx_smix_en_s cn56xx;
	struct cvmx_smix_en_s cn56xxp1;
	struct cvmx_smix_en_s cn58xx;
	struct cvmx_smix_en_s cn58xxp1;
	struct cvmx_smix_en_s cn61xx;
	struct cvmx_smix_en_s cn63xx;
	struct cvmx_smix_en_s cn63xxp1;
	struct cvmx_smix_en_s cn66xx;
	struct cvmx_smix_en_s cn68xx;
	struct cvmx_smix_en_s cn68xxp1;
	struct cvmx_smix_en_s cnf71xx;
};

union cvmx_smix_rd_dat {
	uint64_t u64;
	struct cvmx_smix_rd_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t pending:1;
		uint64_t val:1;
		uint64_t dat:16;
#else
		uint64_t dat:16;
		uint64_t val:1;
		uint64_t pending:1;
		uint64_t reserved_18_63:46;
#endif
	} s;
	struct cvmx_smix_rd_dat_s cn30xx;
	struct cvmx_smix_rd_dat_s cn31xx;
	struct cvmx_smix_rd_dat_s cn38xx;
	struct cvmx_smix_rd_dat_s cn38xxp2;
	struct cvmx_smix_rd_dat_s cn50xx;
	struct cvmx_smix_rd_dat_s cn52xx;
	struct cvmx_smix_rd_dat_s cn52xxp1;
	struct cvmx_smix_rd_dat_s cn56xx;
	struct cvmx_smix_rd_dat_s cn56xxp1;
	struct cvmx_smix_rd_dat_s cn58xx;
	struct cvmx_smix_rd_dat_s cn58xxp1;
	struct cvmx_smix_rd_dat_s cn61xx;
	struct cvmx_smix_rd_dat_s cn63xx;
	struct cvmx_smix_rd_dat_s cn63xxp1;
	struct cvmx_smix_rd_dat_s cn66xx;
	struct cvmx_smix_rd_dat_s cn68xx;
	struct cvmx_smix_rd_dat_s cn68xxp1;
	struct cvmx_smix_rd_dat_s cnf71xx;
};

union cvmx_smix_wr_dat {
	uint64_t u64;
	struct cvmx_smix_wr_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_18_63:46;
		uint64_t pending:1;
		uint64_t val:1;
		uint64_t dat:16;
#else
		uint64_t dat:16;
		uint64_t val:1;
		uint64_t pending:1;
		uint64_t reserved_18_63:46;
#endif
	} s;
	struct cvmx_smix_wr_dat_s cn30xx;
	struct cvmx_smix_wr_dat_s cn31xx;
	struct cvmx_smix_wr_dat_s cn38xx;
	struct cvmx_smix_wr_dat_s cn38xxp2;
	struct cvmx_smix_wr_dat_s cn50xx;
	struct cvmx_smix_wr_dat_s cn52xx;
	struct cvmx_smix_wr_dat_s cn52xxp1;
	struct cvmx_smix_wr_dat_s cn56xx;
	struct cvmx_smix_wr_dat_s cn56xxp1;
	struct cvmx_smix_wr_dat_s cn58xx;
	struct cvmx_smix_wr_dat_s cn58xxp1;
	struct cvmx_smix_wr_dat_s cn61xx;
	struct cvmx_smix_wr_dat_s cn63xx;
	struct cvmx_smix_wr_dat_s cn63xxp1;
	struct cvmx_smix_wr_dat_s cn66xx;
	struct cvmx_smix_wr_dat_s cn68xx;
	struct cvmx_smix_wr_dat_s cn68xxp1;
	struct cvmx_smix_wr_dat_s cnf71xx;
};

#endif
