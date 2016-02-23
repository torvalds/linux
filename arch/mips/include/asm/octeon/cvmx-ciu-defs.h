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

#ifndef __CVMX_CIU_DEFS_H__
#define __CVMX_CIU_DEFS_H__

#define CVMX_CIU_BIST (CVMX_ADD_IO_SEG(0x0001070000000730ull))
#define CVMX_CIU_BLOCK_INT (CVMX_ADD_IO_SEG(0x00010700000007C0ull))
#define CVMX_CIU_DINT (CVMX_ADD_IO_SEG(0x0001070000000720ull))
#define CVMX_CIU_EN2_IOX_INT(offset) (CVMX_ADD_IO_SEG(0x000107000000A600ull) + ((offset) & 1) * 8)
#define CVMX_CIU_EN2_IOX_INT_W1C(offset) (CVMX_ADD_IO_SEG(0x000107000000CE00ull) + ((offset) & 1) * 8)
#define CVMX_CIU_EN2_IOX_INT_W1S(offset) (CVMX_ADD_IO_SEG(0x000107000000AE00ull) + ((offset) & 1) * 8)
#define CVMX_CIU_EN2_PPX_IP2(offset) (CVMX_ADD_IO_SEG(0x000107000000A000ull) + ((offset) & 15) * 8)
#define CVMX_CIU_EN2_PPX_IP2_W1C(offset) (CVMX_ADD_IO_SEG(0x000107000000C800ull) + ((offset) & 15) * 8)
#define CVMX_CIU_EN2_PPX_IP2_W1S(offset) (CVMX_ADD_IO_SEG(0x000107000000A800ull) + ((offset) & 15) * 8)
#define CVMX_CIU_EN2_PPX_IP3(offset) (CVMX_ADD_IO_SEG(0x000107000000A200ull) + ((offset) & 15) * 8)
#define CVMX_CIU_EN2_PPX_IP3_W1C(offset) (CVMX_ADD_IO_SEG(0x000107000000CA00ull) + ((offset) & 15) * 8)
#define CVMX_CIU_EN2_PPX_IP3_W1S(offset) (CVMX_ADD_IO_SEG(0x000107000000AA00ull) + ((offset) & 15) * 8)
#define CVMX_CIU_EN2_PPX_IP4(offset) (CVMX_ADD_IO_SEG(0x000107000000A400ull) + ((offset) & 15) * 8)
#define CVMX_CIU_EN2_PPX_IP4_W1C(offset) (CVMX_ADD_IO_SEG(0x000107000000CC00ull) + ((offset) & 15) * 8)
#define CVMX_CIU_EN2_PPX_IP4_W1S(offset) (CVMX_ADD_IO_SEG(0x000107000000AC00ull) + ((offset) & 15) * 8)
#define CVMX_CIU_FUSE (CVMX_ADD_IO_SEG(0x0001070000000728ull))
#define CVMX_CIU_GSTOP (CVMX_ADD_IO_SEG(0x0001070000000710ull))
#define CVMX_CIU_INT33_SUM0 (CVMX_ADD_IO_SEG(0x0001070000000110ull))
#define CVMX_CIU_INTX_EN0(offset) (CVMX_ADD_IO_SEG(0x0001070000000200ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN0_W1C(offset) (CVMX_ADD_IO_SEG(0x0001070000002200ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN0_W1S(offset) (CVMX_ADD_IO_SEG(0x0001070000006200ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN1(offset) (CVMX_ADD_IO_SEG(0x0001070000000208ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN1_W1C(offset) (CVMX_ADD_IO_SEG(0x0001070000002208ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN1_W1S(offset) (CVMX_ADD_IO_SEG(0x0001070000006208ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN4_0(offset) (CVMX_ADD_IO_SEG(0x0001070000000C80ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_EN4_0_W1C(offset) (CVMX_ADD_IO_SEG(0x0001070000002C80ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_EN4_0_W1S(offset) (CVMX_ADD_IO_SEG(0x0001070000006C80ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_EN4_1(offset) (CVMX_ADD_IO_SEG(0x0001070000000C88ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_EN4_1_W1C(offset) (CVMX_ADD_IO_SEG(0x0001070000002C88ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_EN4_1_W1S(offset) (CVMX_ADD_IO_SEG(0x0001070000006C88ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_SUM0(offset) (CVMX_ADD_IO_SEG(0x0001070000000000ull) + ((offset) & 63) * 8)
#define CVMX_CIU_INTX_SUM4(offset) (CVMX_ADD_IO_SEG(0x0001070000000C00ull) + ((offset) & 15) * 8)
#define CVMX_CIU_INT_DBG_SEL (CVMX_ADD_IO_SEG(0x00010700000007D0ull))
#define CVMX_CIU_INT_SUM1 (CVMX_ADD_IO_SEG(0x0001070000000108ull))
static inline uint64_t CVMX_CIU_MBOX_CLRX(unsigned long offset)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000680ull) + (offset) * 8;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000680ull) + (offset) * 8;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000680ull) + (offset) * 8;
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000680ull) + (offset) * 8;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000680ull) + (offset) * 8;
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000680ull) + (offset) * 8;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000680ull) + (offset) * 8;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070100100600ull) + (offset) * 8;
	}
	return CVMX_ADD_IO_SEG(0x0001070000000680ull) + (offset) * 8;
}

static inline uint64_t CVMX_CIU_MBOX_SETX(unsigned long offset)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000600ull) + (offset) * 8;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000600ull) + (offset) * 8;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000600ull) + (offset) * 8;
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000600ull) + (offset) * 8;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000600ull) + (offset) * 8;
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000600ull) + (offset) * 8;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000600ull) + (offset) * 8;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070100100400ull) + (offset) * 8;
	}
	return CVMX_ADD_IO_SEG(0x0001070000000600ull) + (offset) * 8;
}

#define CVMX_CIU_NMI (CVMX_ADD_IO_SEG(0x0001070000000718ull))
#define CVMX_CIU_PCI_INTA (CVMX_ADD_IO_SEG(0x0001070000000750ull))
#define CVMX_CIU_PP_BIST_STAT (CVMX_ADD_IO_SEG(0x00010700000007E0ull))
#define CVMX_CIU_PP_DBG (CVMX_ADD_IO_SEG(0x0001070000000708ull))
static inline uint64_t CVMX_CIU_PP_POKEX(unsigned long offset)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000580ull) + (offset) * 8;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000580ull) + (offset) * 8;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000580ull) + (offset) * 8;
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000580ull) + (offset) * 8;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000580ull) + (offset) * 8;
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000580ull) + (offset) * 8;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000580ull) + (offset) * 8;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070100100200ull) + (offset) * 8;
	}
	return CVMX_ADD_IO_SEG(0x0001070000000580ull) + (offset) * 8;
}

#define CVMX_CIU_PP_RST (CVMX_ADD_IO_SEG(0x0001070000000700ull))
#define CVMX_CIU_QLM0 (CVMX_ADD_IO_SEG(0x0001070000000780ull))
#define CVMX_CIU_QLM1 (CVMX_ADD_IO_SEG(0x0001070000000788ull))
#define CVMX_CIU_QLM2 (CVMX_ADD_IO_SEG(0x0001070000000790ull))
#define CVMX_CIU_QLM3 (CVMX_ADD_IO_SEG(0x0001070000000798ull))
#define CVMX_CIU_QLM4 (CVMX_ADD_IO_SEG(0x00010700000007A0ull))
#define CVMX_CIU_QLM_DCOK (CVMX_ADD_IO_SEG(0x0001070000000760ull))
#define CVMX_CIU_QLM_JTGC (CVMX_ADD_IO_SEG(0x0001070000000768ull))
#define CVMX_CIU_QLM_JTGD (CVMX_ADD_IO_SEG(0x0001070000000770ull))
#define CVMX_CIU_SOFT_BIST (CVMX_ADD_IO_SEG(0x0001070000000738ull))
#define CVMX_CIU_SOFT_PRST (CVMX_ADD_IO_SEG(0x0001070000000748ull))
#define CVMX_CIU_SOFT_PRST1 (CVMX_ADD_IO_SEG(0x0001070000000758ull))
#define CVMX_CIU_SOFT_PRST2 (CVMX_ADD_IO_SEG(0x00010700000007D8ull))
#define CVMX_CIU_SOFT_PRST3 (CVMX_ADD_IO_SEG(0x00010700000007E0ull))
#define CVMX_CIU_SOFT_RST (CVMX_ADD_IO_SEG(0x0001070000000740ull))
#define CVMX_CIU_SUM1_IOX_INT(offset) (CVMX_ADD_IO_SEG(0x0001070000008600ull) + ((offset) & 1) * 8)
#define CVMX_CIU_SUM1_PPX_IP2(offset) (CVMX_ADD_IO_SEG(0x0001070000008000ull) + ((offset) & 15) * 8)
#define CVMX_CIU_SUM1_PPX_IP3(offset) (CVMX_ADD_IO_SEG(0x0001070000008200ull) + ((offset) & 15) * 8)
#define CVMX_CIU_SUM1_PPX_IP4(offset) (CVMX_ADD_IO_SEG(0x0001070000008400ull) + ((offset) & 15) * 8)
#define CVMX_CIU_SUM2_IOX_INT(offset) (CVMX_ADD_IO_SEG(0x0001070000008E00ull) + ((offset) & 1) * 8)
#define CVMX_CIU_SUM2_PPX_IP2(offset) (CVMX_ADD_IO_SEG(0x0001070000008800ull) + ((offset) & 15) * 8)
#define CVMX_CIU_SUM2_PPX_IP3(offset) (CVMX_ADD_IO_SEG(0x0001070000008A00ull) + ((offset) & 15) * 8)
#define CVMX_CIU_SUM2_PPX_IP4(offset) (CVMX_ADD_IO_SEG(0x0001070000008C00ull) + ((offset) & 15) * 8)
#define CVMX_CIU_TIMX(offset) (CVMX_ADD_IO_SEG(0x0001070000000480ull) + ((offset) & 15) * 8)
#define CVMX_CIU_TIM_MULTI_CAST (CVMX_ADD_IO_SEG(0x000107000000C200ull))
static inline uint64_t CVMX_CIU_WDOGX(unsigned long offset)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000500ull) + (offset) * 8;
	case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
	case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000500ull) + (offset) * 8;
	case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000500ull) + (offset) * 8;
	case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000500ull) + (offset) * 8;
	case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000500ull) + (offset) * 8;
	case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000500ull) + (offset) * 8;
	case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070000000500ull) + (offset) * 8;
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001070100100000ull) + (offset) * 8;
	}
	return CVMX_ADD_IO_SEG(0x0001070000000500ull) + (offset) * 8;
}

union cvmx_ciu_bist {
	uint64_t u64;
	struct cvmx_ciu_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_7_63:57;
		uint64_t bist:7;
#else
		uint64_t bist:7;
		uint64_t reserved_7_63:57;
#endif
	} s;
	struct cvmx_ciu_bist_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t bist:4;
#else
		uint64_t bist:4;
		uint64_t reserved_4_63:60;
#endif
	} cn30xx;
	struct cvmx_ciu_bist_cn30xx cn31xx;
	struct cvmx_ciu_bist_cn30xx cn38xx;
	struct cvmx_ciu_bist_cn30xx cn38xxp2;
	struct cvmx_ciu_bist_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t bist:2;
#else
		uint64_t bist:2;
		uint64_t reserved_2_63:62;
#endif
	} cn50xx;
	struct cvmx_ciu_bist_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t bist:3;
#else
		uint64_t bist:3;
		uint64_t reserved_3_63:61;
#endif
	} cn52xx;
	struct cvmx_ciu_bist_cn52xx cn52xxp1;
	struct cvmx_ciu_bist_cn30xx cn56xx;
	struct cvmx_ciu_bist_cn30xx cn56xxp1;
	struct cvmx_ciu_bist_cn30xx cn58xx;
	struct cvmx_ciu_bist_cn30xx cn58xxp1;
	struct cvmx_ciu_bist_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t bist:6;
#else
		uint64_t bist:6;
		uint64_t reserved_6_63:58;
#endif
	} cn61xx;
	struct cvmx_ciu_bist_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t bist:5;
#else
		uint64_t bist:5;
		uint64_t reserved_5_63:59;
#endif
	} cn63xx;
	struct cvmx_ciu_bist_cn63xx cn63xxp1;
	struct cvmx_ciu_bist_cn61xx cn66xx;
	struct cvmx_ciu_bist_s cn68xx;
	struct cvmx_ciu_bist_s cn68xxp1;
	struct cvmx_ciu_bist_cn61xx cnf71xx;
};

union cvmx_ciu_block_int {
	uint64_t u64;
	struct cvmx_ciu_block_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_43_59:17;
		uint64_t ptp:1;
		uint64_t dpi:1;
		uint64_t dfm:1;
		uint64_t reserved_34_39:6;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t reserved_31_31:1;
		uint64_t iob:1;
		uint64_t reserved_29_29:1;
		uint64_t agl:1;
		uint64_t reserved_27_27:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t reserved_24_24:1;
		uint64_t asxpcs1:1;
		uint64_t asxpcs0:1;
		uint64_t reserved_21_21:1;
		uint64_t pip:1;
		uint64_t reserved_18_19:2;
		uint64_t lmc0:1;
		uint64_t l2c:1;
		uint64_t reserved_15_15:1;
		uint64_t rad:1;
		uint64_t usb:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t reserved_8_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t sli:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
#else
		uint64_t mio:1;
		uint64_t gmx0:1;
		uint64_t gmx1:1;
		uint64_t sli:1;
		uint64_t key:1;
		uint64_t fpa:1;
		uint64_t dfa:1;
		uint64_t zip:1;
		uint64_t reserved_8_8:1;
		uint64_t ipd:1;
		uint64_t pko:1;
		uint64_t tim:1;
		uint64_t pow:1;
		uint64_t usb:1;
		uint64_t rad:1;
		uint64_t reserved_15_15:1;
		uint64_t l2c:1;
		uint64_t lmc0:1;
		uint64_t reserved_18_19:2;
		uint64_t pip:1;
		uint64_t reserved_21_21:1;
		uint64_t asxpcs0:1;
		uint64_t asxpcs1:1;
		uint64_t reserved_24_24:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_27_27:1;
		uint64_t agl:1;
		uint64_t reserved_29_29:1;
		uint64_t iob:1;
		uint64_t reserved_31_31:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t reserved_34_39:6;
		uint64_t dfm:1;
		uint64_t dpi:1;
		uint64_t ptp:1;
		uint64_t reserved_43_59:17;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_63:2;
#endif
	} s;
	struct cvmx_ciu_block_int_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_43_63:21;
		uint64_t ptp:1;
		uint64_t dpi:1;
		uint64_t reserved_31_40:10;
		uint64_t iob:1;
		uint64_t reserved_29_29:1;
		uint64_t agl:1;
		uint64_t reserved_27_27:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t reserved_24_24:1;
		uint64_t asxpcs1:1;
		uint64_t asxpcs0:1;
		uint64_t reserved_21_21:1;
		uint64_t pip:1;
		uint64_t reserved_18_19:2;
		uint64_t lmc0:1;
		uint64_t l2c:1;
		uint64_t reserved_15_15:1;
		uint64_t rad:1;
		uint64_t usb:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t reserved_8_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t sli:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
#else
		uint64_t mio:1;
		uint64_t gmx0:1;
		uint64_t gmx1:1;
		uint64_t sli:1;
		uint64_t key:1;
		uint64_t fpa:1;
		uint64_t dfa:1;
		uint64_t zip:1;
		uint64_t reserved_8_8:1;
		uint64_t ipd:1;
		uint64_t pko:1;
		uint64_t tim:1;
		uint64_t pow:1;
		uint64_t usb:1;
		uint64_t rad:1;
		uint64_t reserved_15_15:1;
		uint64_t l2c:1;
		uint64_t lmc0:1;
		uint64_t reserved_18_19:2;
		uint64_t pip:1;
		uint64_t reserved_21_21:1;
		uint64_t asxpcs0:1;
		uint64_t asxpcs1:1;
		uint64_t reserved_24_24:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_27_27:1;
		uint64_t agl:1;
		uint64_t reserved_29_29:1;
		uint64_t iob:1;
		uint64_t reserved_31_40:10;
		uint64_t dpi:1;
		uint64_t ptp:1;
		uint64_t reserved_43_63:21;
#endif
	} cn61xx;
	struct cvmx_ciu_block_int_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_43_63:21;
		uint64_t ptp:1;
		uint64_t dpi:1;
		uint64_t dfm:1;
		uint64_t reserved_34_39:6;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t reserved_31_31:1;
		uint64_t iob:1;
		uint64_t reserved_29_29:1;
		uint64_t agl:1;
		uint64_t reserved_27_27:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t reserved_23_24:2;
		uint64_t asxpcs0:1;
		uint64_t reserved_21_21:1;
		uint64_t pip:1;
		uint64_t reserved_18_19:2;
		uint64_t lmc0:1;
		uint64_t l2c:1;
		uint64_t reserved_15_15:1;
		uint64_t rad:1;
		uint64_t usb:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t reserved_8_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t sli:1;
		uint64_t reserved_2_2:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
#else
		uint64_t mio:1;
		uint64_t gmx0:1;
		uint64_t reserved_2_2:1;
		uint64_t sli:1;
		uint64_t key:1;
		uint64_t fpa:1;
		uint64_t dfa:1;
		uint64_t zip:1;
		uint64_t reserved_8_8:1;
		uint64_t ipd:1;
		uint64_t pko:1;
		uint64_t tim:1;
		uint64_t pow:1;
		uint64_t usb:1;
		uint64_t rad:1;
		uint64_t reserved_15_15:1;
		uint64_t l2c:1;
		uint64_t lmc0:1;
		uint64_t reserved_18_19:2;
		uint64_t pip:1;
		uint64_t reserved_21_21:1;
		uint64_t asxpcs0:1;
		uint64_t reserved_23_24:2;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_27_27:1;
		uint64_t agl:1;
		uint64_t reserved_29_29:1;
		uint64_t iob:1;
		uint64_t reserved_31_31:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t reserved_34_39:6;
		uint64_t dfm:1;
		uint64_t dpi:1;
		uint64_t ptp:1;
		uint64_t reserved_43_63:21;
#endif
	} cn63xx;
	struct cvmx_ciu_block_int_cn63xx cn63xxp1;
	struct cvmx_ciu_block_int_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_62_63:2;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_43_59:17;
		uint64_t ptp:1;
		uint64_t dpi:1;
		uint64_t dfm:1;
		uint64_t reserved_33_39:7;
		uint64_t srio0:1;
		uint64_t reserved_31_31:1;
		uint64_t iob:1;
		uint64_t reserved_29_29:1;
		uint64_t agl:1;
		uint64_t reserved_27_27:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t reserved_24_24:1;
		uint64_t asxpcs1:1;
		uint64_t asxpcs0:1;
		uint64_t reserved_21_21:1;
		uint64_t pip:1;
		uint64_t reserved_18_19:2;
		uint64_t lmc0:1;
		uint64_t l2c:1;
		uint64_t reserved_15_15:1;
		uint64_t rad:1;
		uint64_t usb:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t reserved_8_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t sli:1;
		uint64_t gmx1:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
#else
		uint64_t mio:1;
		uint64_t gmx0:1;
		uint64_t gmx1:1;
		uint64_t sli:1;
		uint64_t key:1;
		uint64_t fpa:1;
		uint64_t dfa:1;
		uint64_t zip:1;
		uint64_t reserved_8_8:1;
		uint64_t ipd:1;
		uint64_t pko:1;
		uint64_t tim:1;
		uint64_t pow:1;
		uint64_t usb:1;
		uint64_t rad:1;
		uint64_t reserved_15_15:1;
		uint64_t l2c:1;
		uint64_t lmc0:1;
		uint64_t reserved_18_19:2;
		uint64_t pip:1;
		uint64_t reserved_21_21:1;
		uint64_t asxpcs0:1;
		uint64_t asxpcs1:1;
		uint64_t reserved_24_24:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_27_27:1;
		uint64_t agl:1;
		uint64_t reserved_29_29:1;
		uint64_t iob:1;
		uint64_t reserved_31_31:1;
		uint64_t srio0:1;
		uint64_t reserved_33_39:7;
		uint64_t dfm:1;
		uint64_t dpi:1;
		uint64_t ptp:1;
		uint64_t reserved_43_59:17;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_63:2;
#endif
	} cn66xx;
	struct cvmx_ciu_block_int_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_43_63:21;
		uint64_t ptp:1;
		uint64_t dpi:1;
		uint64_t reserved_31_40:10;
		uint64_t iob:1;
		uint64_t reserved_27_29:3;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t reserved_23_24:2;
		uint64_t asxpcs0:1;
		uint64_t reserved_21_21:1;
		uint64_t pip:1;
		uint64_t reserved_18_19:2;
		uint64_t lmc0:1;
		uint64_t l2c:1;
		uint64_t reserved_15_15:1;
		uint64_t rad:1;
		uint64_t usb:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t reserved_6_8:3;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t sli:1;
		uint64_t reserved_2_2:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
#else
		uint64_t mio:1;
		uint64_t gmx0:1;
		uint64_t reserved_2_2:1;
		uint64_t sli:1;
		uint64_t key:1;
		uint64_t fpa:1;
		uint64_t reserved_6_8:3;
		uint64_t ipd:1;
		uint64_t pko:1;
		uint64_t tim:1;
		uint64_t pow:1;
		uint64_t usb:1;
		uint64_t rad:1;
		uint64_t reserved_15_15:1;
		uint64_t l2c:1;
		uint64_t lmc0:1;
		uint64_t reserved_18_19:2;
		uint64_t pip:1;
		uint64_t reserved_21_21:1;
		uint64_t asxpcs0:1;
		uint64_t reserved_23_24:2;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_27_29:3;
		uint64_t iob:1;
		uint64_t reserved_31_40:10;
		uint64_t dpi:1;
		uint64_t ptp:1;
		uint64_t reserved_43_63:21;
#endif
	} cnf71xx;
};

union cvmx_ciu_dint {
	uint64_t u64;
	struct cvmx_ciu_dint_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t dint:32;
#else
		uint64_t dint:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu_dint_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t dint:1;
#else
		uint64_t dint:1;
		uint64_t reserved_1_63:63;
#endif
	} cn30xx;
	struct cvmx_ciu_dint_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t dint:2;
#else
		uint64_t dint:2;
		uint64_t reserved_2_63:62;
#endif
	} cn31xx;
	struct cvmx_ciu_dint_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t dint:16;
#else
		uint64_t dint:16;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_ciu_dint_cn38xx cn38xxp2;
	struct cvmx_ciu_dint_cn31xx cn50xx;
	struct cvmx_ciu_dint_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t dint:4;
#else
		uint64_t dint:4;
		uint64_t reserved_4_63:60;
#endif
	} cn52xx;
	struct cvmx_ciu_dint_cn52xx cn52xxp1;
	struct cvmx_ciu_dint_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t dint:12;
#else
		uint64_t dint:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_dint_cn56xx cn56xxp1;
	struct cvmx_ciu_dint_cn38xx cn58xx;
	struct cvmx_ciu_dint_cn38xx cn58xxp1;
	struct cvmx_ciu_dint_cn52xx cn61xx;
	struct cvmx_ciu_dint_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t dint:6;
#else
		uint64_t dint:6;
		uint64_t reserved_6_63:58;
#endif
	} cn63xx;
	struct cvmx_ciu_dint_cn63xx cn63xxp1;
	struct cvmx_ciu_dint_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t dint:10;
#else
		uint64_t dint:10;
		uint64_t reserved_10_63:54;
#endif
	} cn66xx;
	struct cvmx_ciu_dint_s cn68xx;
	struct cvmx_ciu_dint_s cn68xxp1;
	struct cvmx_ciu_dint_cn52xx cnf71xx;
};

union cvmx_ciu_en2_iox_int {
	uint64_t u64;
	struct cvmx_ciu_en2_iox_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_iox_int_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_iox_int_cn61xx cn66xx;
	struct cvmx_ciu_en2_iox_int_s cnf71xx;
};

union cvmx_ciu_en2_iox_int_w1c {
	uint64_t u64;
	struct cvmx_ciu_en2_iox_int_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_iox_int_w1c_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_iox_int_w1c_cn61xx cn66xx;
	struct cvmx_ciu_en2_iox_int_w1c_s cnf71xx;
};

union cvmx_ciu_en2_iox_int_w1s {
	uint64_t u64;
	struct cvmx_ciu_en2_iox_int_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_iox_int_w1s_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_iox_int_w1s_cn61xx cn66xx;
	struct cvmx_ciu_en2_iox_int_w1s_s cnf71xx;
};

union cvmx_ciu_en2_ppx_ip2 {
	uint64_t u64;
	struct cvmx_ciu_en2_ppx_ip2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_ppx_ip2_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_ppx_ip2_cn61xx cn66xx;
	struct cvmx_ciu_en2_ppx_ip2_s cnf71xx;
};

union cvmx_ciu_en2_ppx_ip2_w1c {
	uint64_t u64;
	struct cvmx_ciu_en2_ppx_ip2_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_ppx_ip2_w1c_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_ppx_ip2_w1c_cn61xx cn66xx;
	struct cvmx_ciu_en2_ppx_ip2_w1c_s cnf71xx;
};

union cvmx_ciu_en2_ppx_ip2_w1s {
	uint64_t u64;
	struct cvmx_ciu_en2_ppx_ip2_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_ppx_ip2_w1s_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_ppx_ip2_w1s_cn61xx cn66xx;
	struct cvmx_ciu_en2_ppx_ip2_w1s_s cnf71xx;
};

union cvmx_ciu_en2_ppx_ip3 {
	uint64_t u64;
	struct cvmx_ciu_en2_ppx_ip3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_ppx_ip3_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_ppx_ip3_cn61xx cn66xx;
	struct cvmx_ciu_en2_ppx_ip3_s cnf71xx;
};

union cvmx_ciu_en2_ppx_ip3_w1c {
	uint64_t u64;
	struct cvmx_ciu_en2_ppx_ip3_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_ppx_ip3_w1c_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_ppx_ip3_w1c_cn61xx cn66xx;
	struct cvmx_ciu_en2_ppx_ip3_w1c_s cnf71xx;
};

union cvmx_ciu_en2_ppx_ip3_w1s {
	uint64_t u64;
	struct cvmx_ciu_en2_ppx_ip3_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_ppx_ip3_w1s_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_ppx_ip3_w1s_cn61xx cn66xx;
	struct cvmx_ciu_en2_ppx_ip3_w1s_s cnf71xx;
};

union cvmx_ciu_en2_ppx_ip4 {
	uint64_t u64;
	struct cvmx_ciu_en2_ppx_ip4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_ppx_ip4_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_ppx_ip4_cn61xx cn66xx;
	struct cvmx_ciu_en2_ppx_ip4_s cnf71xx;
};

union cvmx_ciu_en2_ppx_ip4_w1c {
	uint64_t u64;
	struct cvmx_ciu_en2_ppx_ip4_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_ppx_ip4_w1c_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_ppx_ip4_w1c_cn61xx cn66xx;
	struct cvmx_ciu_en2_ppx_ip4_w1c_s cnf71xx;
};

union cvmx_ciu_en2_ppx_ip4_w1s {
	uint64_t u64;
	struct cvmx_ciu_en2_ppx_ip4_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_en2_ppx_ip4_w1s_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_en2_ppx_ip4_w1s_cn61xx cn66xx;
	struct cvmx_ciu_en2_ppx_ip4_w1s_s cnf71xx;
};

union cvmx_ciu_fuse {
	uint64_t u64;
	struct cvmx_ciu_fuse_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t fuse:32;
#else
		uint64_t fuse:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu_fuse_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t fuse:1;
#else
		uint64_t fuse:1;
		uint64_t reserved_1_63:63;
#endif
	} cn30xx;
	struct cvmx_ciu_fuse_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t fuse:2;
#else
		uint64_t fuse:2;
		uint64_t reserved_2_63:62;
#endif
	} cn31xx;
	struct cvmx_ciu_fuse_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t fuse:16;
#else
		uint64_t fuse:16;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_ciu_fuse_cn38xx cn38xxp2;
	struct cvmx_ciu_fuse_cn31xx cn50xx;
	struct cvmx_ciu_fuse_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t fuse:4;
#else
		uint64_t fuse:4;
		uint64_t reserved_4_63:60;
#endif
	} cn52xx;
	struct cvmx_ciu_fuse_cn52xx cn52xxp1;
	struct cvmx_ciu_fuse_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t fuse:12;
#else
		uint64_t fuse:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_fuse_cn56xx cn56xxp1;
	struct cvmx_ciu_fuse_cn38xx cn58xx;
	struct cvmx_ciu_fuse_cn38xx cn58xxp1;
	struct cvmx_ciu_fuse_cn52xx cn61xx;
	struct cvmx_ciu_fuse_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t fuse:6;
#else
		uint64_t fuse:6;
		uint64_t reserved_6_63:58;
#endif
	} cn63xx;
	struct cvmx_ciu_fuse_cn63xx cn63xxp1;
	struct cvmx_ciu_fuse_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t fuse:10;
#else
		uint64_t fuse:10;
		uint64_t reserved_10_63:54;
#endif
	} cn66xx;
	struct cvmx_ciu_fuse_s cn68xx;
	struct cvmx_ciu_fuse_s cn68xxp1;
	struct cvmx_ciu_fuse_cn52xx cnf71xx;
};

union cvmx_ciu_gstop {
	uint64_t u64;
	struct cvmx_ciu_gstop_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t gstop:1;
#else
		uint64_t gstop:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu_gstop_s cn30xx;
	struct cvmx_ciu_gstop_s cn31xx;
	struct cvmx_ciu_gstop_s cn38xx;
	struct cvmx_ciu_gstop_s cn38xxp2;
	struct cvmx_ciu_gstop_s cn50xx;
	struct cvmx_ciu_gstop_s cn52xx;
	struct cvmx_ciu_gstop_s cn52xxp1;
	struct cvmx_ciu_gstop_s cn56xx;
	struct cvmx_ciu_gstop_s cn56xxp1;
	struct cvmx_ciu_gstop_s cn58xx;
	struct cvmx_ciu_gstop_s cn58xxp1;
	struct cvmx_ciu_gstop_s cn61xx;
	struct cvmx_ciu_gstop_s cn63xx;
	struct cvmx_ciu_gstop_s cn63xxp1;
	struct cvmx_ciu_gstop_s cn66xx;
	struct cvmx_ciu_gstop_s cn68xx;
	struct cvmx_ciu_gstop_s cn68xxp1;
	struct cvmx_ciu_gstop_s cnf71xx;
};

union cvmx_ciu_intx_en0 {
	uint64_t u64;
	struct cvmx_ciu_intx_en0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} s;
	struct cvmx_ciu_intx_en0_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_47_47:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t reserved_47_47:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t reserved_59_63:5;
#endif
	} cn30xx;
	struct cvmx_ciu_intx_en0_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t reserved_59_63:5;
#endif
	} cn31xx;
	struct cvmx_ciu_intx_en0_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t reserved_56_63:8;
#endif
	} cn38xx;
	struct cvmx_ciu_intx_en0_cn38xx cn38xxp2;
	struct cvmx_ciu_intx_en0_cn30xx cn50xx;
	struct cvmx_ciu_intx_en0_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en0_cn52xx cn52xxp1;
	struct cvmx_ciu_intx_en0_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en0_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_en0_cn38xx cn58xx;
	struct cvmx_ciu_intx_en0_cn38xx cn58xxp1;
	struct cvmx_ciu_intx_en0_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en0_cn52xx cn63xx;
	struct cvmx_ciu_intx_en0_cn52xx cn63xxp1;
	struct cvmx_ciu_intx_en0_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t reserved_57_57:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_57:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en0_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t reserved_62_62:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t reserved_62_62:1;
		uint64_t bootdma:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en0_w1c {
	uint64_t u64;
	struct cvmx_ciu_intx_en0_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} s;
	struct cvmx_ciu_intx_en0_w1c_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en0_w1c_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en0_w1c_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t reserved_56_63:8;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_en0_w1c_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en0_w1c_cn52xx cn63xx;
	struct cvmx_ciu_intx_en0_w1c_cn52xx cn63xxp1;
	struct cvmx_ciu_intx_en0_w1c_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t reserved_57_57:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_57:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en0_w1c_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t reserved_62_62:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t reserved_62_62:1;
		uint64_t bootdma:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en0_w1s {
	uint64_t u64;
	struct cvmx_ciu_intx_en0_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} s;
	struct cvmx_ciu_intx_en0_w1s_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en0_w1s_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en0_w1s_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t reserved_56_63:8;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_en0_w1s_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en0_w1s_cn52xx cn63xx;
	struct cvmx_ciu_intx_en0_w1s_cn52xx cn63xxp1;
	struct cvmx_ciu_intx_en0_w1s_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t reserved_57_57:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_57:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en0_w1s_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t reserved_62_62:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t reserved_62_62:1;
		uint64_t bootdma:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en1 {
	uint64_t u64;
	struct cvmx_ciu_intx_en1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_intx_en1_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t wdog:1;
#else
		uint64_t wdog:1;
		uint64_t reserved_1_63:63;
#endif
	} cn30xx;
	struct cvmx_ciu_intx_en1_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t wdog:2;
#else
		uint64_t wdog:2;
		uint64_t reserved_2_63:62;
#endif
	} cn31xx;
	struct cvmx_ciu_intx_en1_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_ciu_intx_en1_cn38xx cn38xxp2;
	struct cvmx_ciu_intx_en1_cn31xx cn50xx;
	struct cvmx_ciu_intx_en1_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_15:12;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en1_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_15:12;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t reserved_19_63:45;
#endif
	} cn52xxp1;
	struct cvmx_ciu_intx_en1_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
#else
		uint64_t wdog:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en1_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_en1_cn38xx cn58xx;
	struct cvmx_ciu_intx_en1_cn38xx cn58xxp1;
	struct cvmx_ciu_intx_en1_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en1_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
#else
		uint64_t wdog:6;
		uint64_t reserved_6_17:12;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_45:9;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_62:6;
		uint64_t rst:1;
#endif
	} cn63xx;
	struct cvmx_ciu_intx_en1_cn63xx cn63xxp1;
	struct cvmx_ciu_intx_en1_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en1_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_41_46:6;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_46:6;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en1_w1c {
	uint64_t u64;
	struct cvmx_ciu_intx_en1_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_intx_en1_w1c_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_15:12;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en1_w1c_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
#else
		uint64_t wdog:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en1_w1c_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t reserved_16_63:48;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_en1_w1c_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en1_w1c_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
#else
		uint64_t wdog:6;
		uint64_t reserved_6_17:12;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_45:9;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_62:6;
		uint64_t rst:1;
#endif
	} cn63xx;
	struct cvmx_ciu_intx_en1_w1c_cn63xx cn63xxp1;
	struct cvmx_ciu_intx_en1_w1c_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en1_w1c_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_41_46:6;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_46:6;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en1_w1s {
	uint64_t u64;
	struct cvmx_ciu_intx_en1_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_intx_en1_w1s_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_15:12;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en1_w1s_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
#else
		uint64_t wdog:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en1_w1s_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t reserved_16_63:48;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_en1_w1s_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en1_w1s_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
#else
		uint64_t wdog:6;
		uint64_t reserved_6_17:12;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_45:9;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_62:6;
		uint64_t rst:1;
#endif
	} cn63xx;
	struct cvmx_ciu_intx_en1_w1s_cn63xx cn63xxp1;
	struct cvmx_ciu_intx_en1_w1s_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en1_w1s_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_41_46:6;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_46:6;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en4_0 {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} s;
	struct cvmx_ciu_intx_en4_0_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_47_47:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t reserved_47_47:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t reserved_59_63:5;
#endif
	} cn50xx;
	struct cvmx_ciu_intx_en4_0_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en4_0_cn52xx cn52xxp1;
	struct cvmx_ciu_intx_en4_0_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en4_0_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_en4_0_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t reserved_56_63:8;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_en4_0_cn58xx cn58xxp1;
	struct cvmx_ciu_intx_en4_0_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en4_0_cn52xx cn63xx;
	struct cvmx_ciu_intx_en4_0_cn52xx cn63xxp1;
	struct cvmx_ciu_intx_en4_0_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t reserved_57_57:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_57:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en4_0_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t reserved_62_62:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t reserved_62_62:1;
		uint64_t bootdma:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en4_0_w1c {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_0_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} s;
	struct cvmx_ciu_intx_en4_0_w1c_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en4_0_w1c_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en4_0_w1c_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t reserved_56_63:8;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_en4_0_w1c_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en4_0_w1c_cn52xx cn63xx;
	struct cvmx_ciu_intx_en4_0_w1c_cn52xx cn63xxp1;
	struct cvmx_ciu_intx_en4_0_w1c_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t reserved_57_57:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_57:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en4_0_w1c_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t reserved_62_62:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t reserved_62_62:1;
		uint64_t bootdma:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en4_0_w1s {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_0_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} s;
	struct cvmx_ciu_intx_en4_0_w1s_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en4_0_w1s_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en4_0_w1s_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t reserved_56_63:8;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_en4_0_w1s_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en4_0_w1s_cn52xx cn63xx;
	struct cvmx_ciu_intx_en4_0_w1s_cn52xx cn63xxp1;
	struct cvmx_ciu_intx_en4_0_w1s_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t reserved_57_57:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_57:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en4_0_w1s_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t reserved_62_62:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t reserved_44_44:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t reserved_62_62:1;
		uint64_t bootdma:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en4_1 {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_intx_en4_1_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t wdog:2;
#else
		uint64_t wdog:2;
		uint64_t reserved_2_63:62;
#endif
	} cn50xx;
	struct cvmx_ciu_intx_en4_1_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_15:12;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en4_1_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_15:12;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t reserved_19_63:45;
#endif
	} cn52xxp1;
	struct cvmx_ciu_intx_en4_1_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
#else
		uint64_t wdog:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en4_1_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_en4_1_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t reserved_16_63:48;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_en4_1_cn58xx cn58xxp1;
	struct cvmx_ciu_intx_en4_1_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en4_1_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
#else
		uint64_t wdog:6;
		uint64_t reserved_6_17:12;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_45:9;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_62:6;
		uint64_t rst:1;
#endif
	} cn63xx;
	struct cvmx_ciu_intx_en4_1_cn63xx cn63xxp1;
	struct cvmx_ciu_intx_en4_1_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en4_1_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_41_46:6;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_46:6;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en4_1_w1c {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_1_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_intx_en4_1_w1c_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_15:12;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en4_1_w1c_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
#else
		uint64_t wdog:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en4_1_w1c_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t reserved_16_63:48;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_en4_1_w1c_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en4_1_w1c_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
#else
		uint64_t wdog:6;
		uint64_t reserved_6_17:12;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_45:9;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_62:6;
		uint64_t rst:1;
#endif
	} cn63xx;
	struct cvmx_ciu_intx_en4_1_w1c_cn63xx cn63xxp1;
	struct cvmx_ciu_intx_en4_1_w1c_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en4_1_w1c_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_41_46:6;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_46:6;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_en4_1_w1s {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_1_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_intx_en4_1_w1s_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_15:12;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_en4_1_w1s_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
#else
		uint64_t wdog:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_en4_1_w1s_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t reserved_16_63:48;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_en4_1_w1s_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_en4_1_w1s_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
#else
		uint64_t wdog:6;
		uint64_t reserved_6_17:12;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_45:9;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_62:6;
		uint64_t rst:1;
#endif
	} cn63xx;
	struct cvmx_ciu_intx_en4_1_w1s_cn63xx cn63xxp1;
	struct cvmx_ciu_intx_en4_1_w1s_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_en4_1_w1s_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_41_46:6;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_46:6;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_sum0 {
	uint64_t u64;
	struct cvmx_ciu_intx_sum0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} s;
	struct cvmx_ciu_intx_sum0_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_47_47:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t reserved_47_47:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t reserved_59_63:5;
#endif
	} cn30xx;
	struct cvmx_ciu_intx_sum0_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t reserved_59_63:5;
#endif
	} cn31xx;
	struct cvmx_ciu_intx_sum0_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t reserved_56_63:8;
#endif
	} cn38xx;
	struct cvmx_ciu_intx_sum0_cn38xx cn38xxp2;
	struct cvmx_ciu_intx_sum0_cn30xx cn50xx;
	struct cvmx_ciu_intx_sum0_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_sum0_cn52xx cn52xxp1;
	struct cvmx_ciu_intx_sum0_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_sum0_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_sum0_cn38xx cn58xx;
	struct cvmx_ciu_intx_sum0_cn38xx cn58xxp1;
	struct cvmx_ciu_intx_sum0_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t sum2:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t sum2:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_sum0_cn52xx cn63xx;
	struct cvmx_ciu_intx_sum0_cn52xx cn63xxp1;
	struct cvmx_ciu_intx_sum0_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t reserved_57_57:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t sum2:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t sum2:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_57:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_sum0_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t reserved_62_62:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t sum2:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t sum2:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t reserved_62_62:1;
		uint64_t bootdma:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_intx_sum4 {
	uint64_t u64;
	struct cvmx_ciu_intx_sum4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} s;
	struct cvmx_ciu_intx_sum4_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_47_47:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t reserved_47_47:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t reserved_59_63:5;
#endif
	} cn50xx;
	struct cvmx_ciu_intx_sum4_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn52xx;
	struct cvmx_ciu_intx_sum4_cn52xx cn52xxp1;
	struct cvmx_ciu_intx_sum4_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn56xx;
	struct cvmx_ciu_intx_sum4_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_sum4_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t key_zero:1;
		uint64_t timer:4;
		uint64_t reserved_56_63:8;
#endif
	} cn58xx;
	struct cvmx_ciu_intx_sum4_cn58xx cn58xxp1;
	struct cvmx_ciu_intx_sum4_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t sum2:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t sum2:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn61xx;
	struct cvmx_ciu_intx_sum4_cn52xx cn63xx;
	struct cvmx_ciu_intx_sum4_cn52xx cn63xxp1;
	struct cvmx_ciu_intx_sum4_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t reserved_57_57:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t sum2:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t sum2:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_57:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn66xx;
	struct cvmx_ciu_intx_sum4_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t reserved_62_62:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t sum2:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t sum2:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t reserved_62_62:1;
		uint64_t bootdma:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_int33_sum0 {
	uint64_t u64;
	struct cvmx_ciu_int33_sum0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t sum2:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t sum2:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} s;
	struct cvmx_ciu_int33_sum0_s cn61xx;
	struct cvmx_ciu_int33_sum0_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_51_51:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_58:2;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn63xx;
	struct cvmx_ciu_int33_sum0_cn63xx cn63xxp1;
	struct cvmx_ciu_int33_sum0_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t reserved_57_57:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t sum2:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:2;
		uint64_t ipd_drp:1;
		uint64_t sum2:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t reserved_57_57:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t mii:1;
		uint64_t bootdma:1;
#endif
	} cn66xx;
	struct cvmx_ciu_int33_sum0_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t bootdma:1;
		uint64_t reserved_62_62:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t sum2:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
#else
		uint64_t workq:16;
		uint64_t gpio:16;
		uint64_t mbox:2;
		uint64_t uart:2;
		uint64_t pci_int:4;
		uint64_t pci_msi:4;
		uint64_t wdog_sum:1;
		uint64_t twsi:1;
		uint64_t rml:1;
		uint64_t trace:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t ipd_drp:1;
		uint64_t sum2:1;
		uint64_t timer:4;
		uint64_t usb:1;
		uint64_t pcm:1;
		uint64_t mpi:1;
		uint64_t twsi2:1;
		uint64_t powiq:1;
		uint64_t ipdppthr:1;
		uint64_t reserved_62_62:1;
		uint64_t bootdma:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_int_dbg_sel {
	uint64_t u64;
	struct cvmx_ciu_int_dbg_sel_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t sel:3;
		uint64_t reserved_10_15:6;
		uint64_t irq:2;
		uint64_t reserved_5_7:3;
		uint64_t pp:5;
#else
		uint64_t pp:5;
		uint64_t reserved_5_7:3;
		uint64_t irq:2;
		uint64_t reserved_10_15:6;
		uint64_t sel:3;
		uint64_t reserved_19_63:45;
#endif
	} s;
	struct cvmx_ciu_int_dbg_sel_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t sel:3;
		uint64_t reserved_10_15:6;
		uint64_t irq:2;
		uint64_t reserved_4_7:4;
		uint64_t pp:4;
#else
		uint64_t pp:4;
		uint64_t reserved_4_7:4;
		uint64_t irq:2;
		uint64_t reserved_10_15:6;
		uint64_t sel:3;
		uint64_t reserved_19_63:45;
#endif
	} cn61xx;
	struct cvmx_ciu_int_dbg_sel_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t sel:3;
		uint64_t reserved_10_15:6;
		uint64_t irq:2;
		uint64_t reserved_3_7:5;
		uint64_t pp:3;
#else
		uint64_t pp:3;
		uint64_t reserved_3_7:5;
		uint64_t irq:2;
		uint64_t reserved_10_15:6;
		uint64_t sel:3;
		uint64_t reserved_19_63:45;
#endif
	} cn63xx;
	struct cvmx_ciu_int_dbg_sel_cn61xx cn66xx;
	struct cvmx_ciu_int_dbg_sel_s cn68xx;
	struct cvmx_ciu_int_dbg_sel_s cn68xxp1;
	struct cvmx_ciu_int_dbg_sel_cn61xx cnf71xx;
};

union cvmx_ciu_int_sum1 {
	uint64_t u64;
	struct cvmx_ciu_int_sum1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_int_sum1_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t wdog:1;
#else
		uint64_t wdog:1;
		uint64_t reserved_1_63:63;
#endif
	} cn30xx;
	struct cvmx_ciu_int_sum1_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t wdog:2;
#else
		uint64_t wdog:2;
		uint64_t reserved_2_63:62;
#endif
	} cn31xx;
	struct cvmx_ciu_int_sum1_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
#else
		uint64_t wdog:16;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_ciu_int_sum1_cn38xx cn38xxp2;
	struct cvmx_ciu_int_sum1_cn31xx cn50xx;
	struct cvmx_ciu_int_sum1_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_15:12;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t reserved_20_63:44;
#endif
	} cn52xx;
	struct cvmx_ciu_int_sum1_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_15:12;
		uint64_t uart2:1;
		uint64_t usb1:1;
		uint64_t mii1:1;
		uint64_t reserved_19_63:45;
#endif
	} cn52xxp1;
	struct cvmx_ciu_int_sum1_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
#else
		uint64_t wdog:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_int_sum1_cn56xx cn56xxp1;
	struct cvmx_ciu_int_sum1_cn38xx cn58xx;
	struct cvmx_ciu_int_sum1_cn38xx cn58xxp1;
	struct cvmx_ciu_int_sum1_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_int_sum1_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
#else
		uint64_t wdog:6;
		uint64_t reserved_6_17:12;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_45:9;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t srio1:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_62:6;
		uint64_t rst:1;
#endif
	} cn63xx;
	struct cvmx_ciu_int_sum1_cn63xx cn63xxp1;
	struct cvmx_ciu_int_sum1_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_int_sum1_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_37_46:10;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_46:10;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_mbox_clrx {
	uint64_t u64;
	struct cvmx_ciu_mbox_clrx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t bits:32;
#else
		uint64_t bits:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu_mbox_clrx_s cn30xx;
	struct cvmx_ciu_mbox_clrx_s cn31xx;
	struct cvmx_ciu_mbox_clrx_s cn38xx;
	struct cvmx_ciu_mbox_clrx_s cn38xxp2;
	struct cvmx_ciu_mbox_clrx_s cn50xx;
	struct cvmx_ciu_mbox_clrx_s cn52xx;
	struct cvmx_ciu_mbox_clrx_s cn52xxp1;
	struct cvmx_ciu_mbox_clrx_s cn56xx;
	struct cvmx_ciu_mbox_clrx_s cn56xxp1;
	struct cvmx_ciu_mbox_clrx_s cn58xx;
	struct cvmx_ciu_mbox_clrx_s cn58xxp1;
	struct cvmx_ciu_mbox_clrx_s cn61xx;
	struct cvmx_ciu_mbox_clrx_s cn63xx;
	struct cvmx_ciu_mbox_clrx_s cn63xxp1;
	struct cvmx_ciu_mbox_clrx_s cn66xx;
	struct cvmx_ciu_mbox_clrx_s cn68xx;
	struct cvmx_ciu_mbox_clrx_s cn68xxp1;
	struct cvmx_ciu_mbox_clrx_s cnf71xx;
};

union cvmx_ciu_mbox_setx {
	uint64_t u64;
	struct cvmx_ciu_mbox_setx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t bits:32;
#else
		uint64_t bits:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu_mbox_setx_s cn30xx;
	struct cvmx_ciu_mbox_setx_s cn31xx;
	struct cvmx_ciu_mbox_setx_s cn38xx;
	struct cvmx_ciu_mbox_setx_s cn38xxp2;
	struct cvmx_ciu_mbox_setx_s cn50xx;
	struct cvmx_ciu_mbox_setx_s cn52xx;
	struct cvmx_ciu_mbox_setx_s cn52xxp1;
	struct cvmx_ciu_mbox_setx_s cn56xx;
	struct cvmx_ciu_mbox_setx_s cn56xxp1;
	struct cvmx_ciu_mbox_setx_s cn58xx;
	struct cvmx_ciu_mbox_setx_s cn58xxp1;
	struct cvmx_ciu_mbox_setx_s cn61xx;
	struct cvmx_ciu_mbox_setx_s cn63xx;
	struct cvmx_ciu_mbox_setx_s cn63xxp1;
	struct cvmx_ciu_mbox_setx_s cn66xx;
	struct cvmx_ciu_mbox_setx_s cn68xx;
	struct cvmx_ciu_mbox_setx_s cn68xxp1;
	struct cvmx_ciu_mbox_setx_s cnf71xx;
};

union cvmx_ciu_nmi {
	uint64_t u64;
	struct cvmx_ciu_nmi_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t nmi:32;
#else
		uint64_t nmi:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu_nmi_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t nmi:1;
#else
		uint64_t nmi:1;
		uint64_t reserved_1_63:63;
#endif
	} cn30xx;
	struct cvmx_ciu_nmi_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t nmi:2;
#else
		uint64_t nmi:2;
		uint64_t reserved_2_63:62;
#endif
	} cn31xx;
	struct cvmx_ciu_nmi_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t nmi:16;
#else
		uint64_t nmi:16;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_ciu_nmi_cn38xx cn38xxp2;
	struct cvmx_ciu_nmi_cn31xx cn50xx;
	struct cvmx_ciu_nmi_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t nmi:4;
#else
		uint64_t nmi:4;
		uint64_t reserved_4_63:60;
#endif
	} cn52xx;
	struct cvmx_ciu_nmi_cn52xx cn52xxp1;
	struct cvmx_ciu_nmi_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t nmi:12;
#else
		uint64_t nmi:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_nmi_cn56xx cn56xxp1;
	struct cvmx_ciu_nmi_cn38xx cn58xx;
	struct cvmx_ciu_nmi_cn38xx cn58xxp1;
	struct cvmx_ciu_nmi_cn52xx cn61xx;
	struct cvmx_ciu_nmi_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t nmi:6;
#else
		uint64_t nmi:6;
		uint64_t reserved_6_63:58;
#endif
	} cn63xx;
	struct cvmx_ciu_nmi_cn63xx cn63xxp1;
	struct cvmx_ciu_nmi_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t nmi:10;
#else
		uint64_t nmi:10;
		uint64_t reserved_10_63:54;
#endif
	} cn66xx;
	struct cvmx_ciu_nmi_s cn68xx;
	struct cvmx_ciu_nmi_s cn68xxp1;
	struct cvmx_ciu_nmi_cn52xx cnf71xx;
};

union cvmx_ciu_pci_inta {
	uint64_t u64;
	struct cvmx_ciu_pci_inta_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t intr:2;
#else
		uint64_t intr:2;
		uint64_t reserved_2_63:62;
#endif
	} s;
	struct cvmx_ciu_pci_inta_s cn30xx;
	struct cvmx_ciu_pci_inta_s cn31xx;
	struct cvmx_ciu_pci_inta_s cn38xx;
	struct cvmx_ciu_pci_inta_s cn38xxp2;
	struct cvmx_ciu_pci_inta_s cn50xx;
	struct cvmx_ciu_pci_inta_s cn52xx;
	struct cvmx_ciu_pci_inta_s cn52xxp1;
	struct cvmx_ciu_pci_inta_s cn56xx;
	struct cvmx_ciu_pci_inta_s cn56xxp1;
	struct cvmx_ciu_pci_inta_s cn58xx;
	struct cvmx_ciu_pci_inta_s cn58xxp1;
	struct cvmx_ciu_pci_inta_s cn61xx;
	struct cvmx_ciu_pci_inta_s cn63xx;
	struct cvmx_ciu_pci_inta_s cn63xxp1;
	struct cvmx_ciu_pci_inta_s cn66xx;
	struct cvmx_ciu_pci_inta_s cn68xx;
	struct cvmx_ciu_pci_inta_s cn68xxp1;
	struct cvmx_ciu_pci_inta_s cnf71xx;
};

union cvmx_ciu_pp_bist_stat {
	uint64_t u64;
	struct cvmx_ciu_pp_bist_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t pp_bist:32;
#else
		uint64_t pp_bist:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu_pp_bist_stat_s cn68xx;
	struct cvmx_ciu_pp_bist_stat_s cn68xxp1;
};

union cvmx_ciu_pp_dbg {
	uint64_t u64;
	struct cvmx_ciu_pp_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t ppdbg:32;
#else
		uint64_t ppdbg:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu_pp_dbg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t ppdbg:1;
#else
		uint64_t ppdbg:1;
		uint64_t reserved_1_63:63;
#endif
	} cn30xx;
	struct cvmx_ciu_pp_dbg_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t ppdbg:2;
#else
		uint64_t ppdbg:2;
		uint64_t reserved_2_63:62;
#endif
	} cn31xx;
	struct cvmx_ciu_pp_dbg_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t ppdbg:16;
#else
		uint64_t ppdbg:16;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_ciu_pp_dbg_cn38xx cn38xxp2;
	struct cvmx_ciu_pp_dbg_cn31xx cn50xx;
	struct cvmx_ciu_pp_dbg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t ppdbg:4;
#else
		uint64_t ppdbg:4;
		uint64_t reserved_4_63:60;
#endif
	} cn52xx;
	struct cvmx_ciu_pp_dbg_cn52xx cn52xxp1;
	struct cvmx_ciu_pp_dbg_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t ppdbg:12;
#else
		uint64_t ppdbg:12;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_pp_dbg_cn56xx cn56xxp1;
	struct cvmx_ciu_pp_dbg_cn38xx cn58xx;
	struct cvmx_ciu_pp_dbg_cn38xx cn58xxp1;
	struct cvmx_ciu_pp_dbg_cn52xx cn61xx;
	struct cvmx_ciu_pp_dbg_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t ppdbg:6;
#else
		uint64_t ppdbg:6;
		uint64_t reserved_6_63:58;
#endif
	} cn63xx;
	struct cvmx_ciu_pp_dbg_cn63xx cn63xxp1;
	struct cvmx_ciu_pp_dbg_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t ppdbg:10;
#else
		uint64_t ppdbg:10;
		uint64_t reserved_10_63:54;
#endif
	} cn66xx;
	struct cvmx_ciu_pp_dbg_s cn68xx;
	struct cvmx_ciu_pp_dbg_s cn68xxp1;
	struct cvmx_ciu_pp_dbg_cn52xx cnf71xx;
};

union cvmx_ciu_pp_pokex {
	uint64_t u64;
	struct cvmx_ciu_pp_pokex_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t poke:64;
#else
		uint64_t poke:64;
#endif
	} s;
	struct cvmx_ciu_pp_pokex_s cn30xx;
	struct cvmx_ciu_pp_pokex_s cn31xx;
	struct cvmx_ciu_pp_pokex_s cn38xx;
	struct cvmx_ciu_pp_pokex_s cn38xxp2;
	struct cvmx_ciu_pp_pokex_s cn50xx;
	struct cvmx_ciu_pp_pokex_s cn52xx;
	struct cvmx_ciu_pp_pokex_s cn52xxp1;
	struct cvmx_ciu_pp_pokex_s cn56xx;
	struct cvmx_ciu_pp_pokex_s cn56xxp1;
	struct cvmx_ciu_pp_pokex_s cn58xx;
	struct cvmx_ciu_pp_pokex_s cn58xxp1;
	struct cvmx_ciu_pp_pokex_s cn61xx;
	struct cvmx_ciu_pp_pokex_s cn63xx;
	struct cvmx_ciu_pp_pokex_s cn63xxp1;
	struct cvmx_ciu_pp_pokex_s cn66xx;
	struct cvmx_ciu_pp_pokex_s cn68xx;
	struct cvmx_ciu_pp_pokex_s cn68xxp1;
	struct cvmx_ciu_pp_pokex_s cnf71xx;
};

union cvmx_ciu_pp_rst {
	uint64_t u64;
	struct cvmx_ciu_pp_rst_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t rst:31;
		uint64_t rst0:1;
#else
		uint64_t rst0:1;
		uint64_t rst:31;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu_pp_rst_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t rst0:1;
#else
		uint64_t rst0:1;
		uint64_t reserved_1_63:63;
#endif
	} cn30xx;
	struct cvmx_ciu_pp_rst_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t rst:1;
		uint64_t rst0:1;
#else
		uint64_t rst0:1;
		uint64_t rst:1;
		uint64_t reserved_2_63:62;
#endif
	} cn31xx;
	struct cvmx_ciu_pp_rst_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t rst:15;
		uint64_t rst0:1;
#else
		uint64_t rst0:1;
		uint64_t rst:15;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_ciu_pp_rst_cn38xx cn38xxp2;
	struct cvmx_ciu_pp_rst_cn31xx cn50xx;
	struct cvmx_ciu_pp_rst_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t rst:3;
		uint64_t rst0:1;
#else
		uint64_t rst0:1;
		uint64_t rst:3;
		uint64_t reserved_4_63:60;
#endif
	} cn52xx;
	struct cvmx_ciu_pp_rst_cn52xx cn52xxp1;
	struct cvmx_ciu_pp_rst_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t rst:11;
		uint64_t rst0:1;
#else
		uint64_t rst0:1;
		uint64_t rst:11;
		uint64_t reserved_12_63:52;
#endif
	} cn56xx;
	struct cvmx_ciu_pp_rst_cn56xx cn56xxp1;
	struct cvmx_ciu_pp_rst_cn38xx cn58xx;
	struct cvmx_ciu_pp_rst_cn38xx cn58xxp1;
	struct cvmx_ciu_pp_rst_cn52xx cn61xx;
	struct cvmx_ciu_pp_rst_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t rst:5;
		uint64_t rst0:1;
#else
		uint64_t rst0:1;
		uint64_t rst:5;
		uint64_t reserved_6_63:58;
#endif
	} cn63xx;
	struct cvmx_ciu_pp_rst_cn63xx cn63xxp1;
	struct cvmx_ciu_pp_rst_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t rst:9;
		uint64_t rst0:1;
#else
		uint64_t rst0:1;
		uint64_t rst:9;
		uint64_t reserved_10_63:54;
#endif
	} cn66xx;
	struct cvmx_ciu_pp_rst_s cn68xx;
	struct cvmx_ciu_pp_rst_s cn68xxp1;
	struct cvmx_ciu_pp_rst_cn52xx cnf71xx;
};

union cvmx_ciu_qlm0 {
	uint64_t u64;
	struct cvmx_ciu_qlm0_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t g2bypass:1;
		uint64_t reserved_53_62:10;
		uint64_t g2deemph:5;
		uint64_t reserved_45_47:3;
		uint64_t g2margin:5;
		uint64_t reserved_32_39:8;
		uint64_t txbypass:1;
		uint64_t reserved_21_30:10;
		uint64_t txdeemph:5;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
#else
		uint64_t lane_en:4;
		uint64_t reserved_4_7:4;
		uint64_t txmargin:5;
		uint64_t reserved_13_15:3;
		uint64_t txdeemph:5;
		uint64_t reserved_21_30:10;
		uint64_t txbypass:1;
		uint64_t reserved_32_39:8;
		uint64_t g2margin:5;
		uint64_t reserved_45_47:3;
		uint64_t g2deemph:5;
		uint64_t reserved_53_62:10;
		uint64_t g2bypass:1;
#endif
	} s;
	struct cvmx_ciu_qlm0_s cn61xx;
	struct cvmx_ciu_qlm0_s cn63xx;
	struct cvmx_ciu_qlm0_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t txbypass:1;
		uint64_t reserved_20_30:11;
		uint64_t txdeemph:4;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
#else
		uint64_t lane_en:4;
		uint64_t reserved_4_7:4;
		uint64_t txmargin:5;
		uint64_t reserved_13_15:3;
		uint64_t txdeemph:4;
		uint64_t reserved_20_30:11;
		uint64_t txbypass:1;
		uint64_t reserved_32_63:32;
#endif
	} cn63xxp1;
	struct cvmx_ciu_qlm0_s cn66xx;
	struct cvmx_ciu_qlm0_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t txbypass:1;
		uint64_t reserved_21_30:10;
		uint64_t txdeemph:5;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
#else
		uint64_t lane_en:4;
		uint64_t reserved_4_7:4;
		uint64_t txmargin:5;
		uint64_t reserved_13_15:3;
		uint64_t txdeemph:5;
		uint64_t reserved_21_30:10;
		uint64_t txbypass:1;
		uint64_t reserved_32_63:32;
#endif
	} cn68xx;
	struct cvmx_ciu_qlm0_cn68xx cn68xxp1;
	struct cvmx_ciu_qlm0_s cnf71xx;
};

union cvmx_ciu_qlm1 {
	uint64_t u64;
	struct cvmx_ciu_qlm1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t g2bypass:1;
		uint64_t reserved_53_62:10;
		uint64_t g2deemph:5;
		uint64_t reserved_45_47:3;
		uint64_t g2margin:5;
		uint64_t reserved_32_39:8;
		uint64_t txbypass:1;
		uint64_t reserved_21_30:10;
		uint64_t txdeemph:5;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
#else
		uint64_t lane_en:4;
		uint64_t reserved_4_7:4;
		uint64_t txmargin:5;
		uint64_t reserved_13_15:3;
		uint64_t txdeemph:5;
		uint64_t reserved_21_30:10;
		uint64_t txbypass:1;
		uint64_t reserved_32_39:8;
		uint64_t g2margin:5;
		uint64_t reserved_45_47:3;
		uint64_t g2deemph:5;
		uint64_t reserved_53_62:10;
		uint64_t g2bypass:1;
#endif
	} s;
	struct cvmx_ciu_qlm1_s cn61xx;
	struct cvmx_ciu_qlm1_s cn63xx;
	struct cvmx_ciu_qlm1_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t txbypass:1;
		uint64_t reserved_20_30:11;
		uint64_t txdeemph:4;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
#else
		uint64_t lane_en:4;
		uint64_t reserved_4_7:4;
		uint64_t txmargin:5;
		uint64_t reserved_13_15:3;
		uint64_t txdeemph:4;
		uint64_t reserved_20_30:11;
		uint64_t txbypass:1;
		uint64_t reserved_32_63:32;
#endif
	} cn63xxp1;
	struct cvmx_ciu_qlm1_s cn66xx;
	struct cvmx_ciu_qlm1_s cn68xx;
	struct cvmx_ciu_qlm1_s cn68xxp1;
	struct cvmx_ciu_qlm1_s cnf71xx;
};

union cvmx_ciu_qlm2 {
	uint64_t u64;
	struct cvmx_ciu_qlm2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t g2bypass:1;
		uint64_t reserved_53_62:10;
		uint64_t g2deemph:5;
		uint64_t reserved_45_47:3;
		uint64_t g2margin:5;
		uint64_t reserved_32_39:8;
		uint64_t txbypass:1;
		uint64_t reserved_21_30:10;
		uint64_t txdeemph:5;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
#else
		uint64_t lane_en:4;
		uint64_t reserved_4_7:4;
		uint64_t txmargin:5;
		uint64_t reserved_13_15:3;
		uint64_t txdeemph:5;
		uint64_t reserved_21_30:10;
		uint64_t txbypass:1;
		uint64_t reserved_32_39:8;
		uint64_t g2margin:5;
		uint64_t reserved_45_47:3;
		uint64_t g2deemph:5;
		uint64_t reserved_53_62:10;
		uint64_t g2bypass:1;
#endif
	} s;
	struct cvmx_ciu_qlm2_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t txbypass:1;
		uint64_t reserved_21_30:10;
		uint64_t txdeemph:5;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
#else
		uint64_t lane_en:4;
		uint64_t reserved_4_7:4;
		uint64_t txmargin:5;
		uint64_t reserved_13_15:3;
		uint64_t txdeemph:5;
		uint64_t reserved_21_30:10;
		uint64_t txbypass:1;
		uint64_t reserved_32_63:32;
#endif
	} cn61xx;
	struct cvmx_ciu_qlm2_cn61xx cn63xx;
	struct cvmx_ciu_qlm2_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t txbypass:1;
		uint64_t reserved_20_30:11;
		uint64_t txdeemph:4;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
#else
		uint64_t lane_en:4;
		uint64_t reserved_4_7:4;
		uint64_t txmargin:5;
		uint64_t reserved_13_15:3;
		uint64_t txdeemph:4;
		uint64_t reserved_20_30:11;
		uint64_t txbypass:1;
		uint64_t reserved_32_63:32;
#endif
	} cn63xxp1;
	struct cvmx_ciu_qlm2_cn61xx cn66xx;
	struct cvmx_ciu_qlm2_s cn68xx;
	struct cvmx_ciu_qlm2_s cn68xxp1;
	struct cvmx_ciu_qlm2_cn61xx cnf71xx;
};

union cvmx_ciu_qlm3 {
	uint64_t u64;
	struct cvmx_ciu_qlm3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t g2bypass:1;
		uint64_t reserved_53_62:10;
		uint64_t g2deemph:5;
		uint64_t reserved_45_47:3;
		uint64_t g2margin:5;
		uint64_t reserved_32_39:8;
		uint64_t txbypass:1;
		uint64_t reserved_21_30:10;
		uint64_t txdeemph:5;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
#else
		uint64_t lane_en:4;
		uint64_t reserved_4_7:4;
		uint64_t txmargin:5;
		uint64_t reserved_13_15:3;
		uint64_t txdeemph:5;
		uint64_t reserved_21_30:10;
		uint64_t txbypass:1;
		uint64_t reserved_32_39:8;
		uint64_t g2margin:5;
		uint64_t reserved_45_47:3;
		uint64_t g2deemph:5;
		uint64_t reserved_53_62:10;
		uint64_t g2bypass:1;
#endif
	} s;
	struct cvmx_ciu_qlm3_s cn68xx;
	struct cvmx_ciu_qlm3_s cn68xxp1;
};

union cvmx_ciu_qlm4 {
	uint64_t u64;
	struct cvmx_ciu_qlm4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t g2bypass:1;
		uint64_t reserved_53_62:10;
		uint64_t g2deemph:5;
		uint64_t reserved_45_47:3;
		uint64_t g2margin:5;
		uint64_t reserved_32_39:8;
		uint64_t txbypass:1;
		uint64_t reserved_21_30:10;
		uint64_t txdeemph:5;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
#else
		uint64_t lane_en:4;
		uint64_t reserved_4_7:4;
		uint64_t txmargin:5;
		uint64_t reserved_13_15:3;
		uint64_t txdeemph:5;
		uint64_t reserved_21_30:10;
		uint64_t txbypass:1;
		uint64_t reserved_32_39:8;
		uint64_t g2margin:5;
		uint64_t reserved_45_47:3;
		uint64_t g2deemph:5;
		uint64_t reserved_53_62:10;
		uint64_t g2bypass:1;
#endif
	} s;
	struct cvmx_ciu_qlm4_s cn68xx;
	struct cvmx_ciu_qlm4_s cn68xxp1;
};

union cvmx_ciu_qlm_dcok {
	uint64_t u64;
	struct cvmx_ciu_qlm_dcok_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t qlm_dcok:4;
#else
		uint64_t qlm_dcok:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu_qlm_dcok_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_2_63:62;
		uint64_t qlm_dcok:2;
#else
		uint64_t qlm_dcok:2;
		uint64_t reserved_2_63:62;
#endif
	} cn52xx;
	struct cvmx_ciu_qlm_dcok_cn52xx cn52xxp1;
	struct cvmx_ciu_qlm_dcok_s cn56xx;
	struct cvmx_ciu_qlm_dcok_s cn56xxp1;
};

union cvmx_ciu_qlm_jtgc {
	uint64_t u64;
	struct cvmx_ciu_qlm_jtgc_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t bypass_ext:1;
		uint64_t reserved_11_15:5;
		uint64_t clk_div:3;
		uint64_t reserved_7_7:1;
		uint64_t mux_sel:3;
		uint64_t bypass:4;
#else
		uint64_t bypass:4;
		uint64_t mux_sel:3;
		uint64_t reserved_7_7:1;
		uint64_t clk_div:3;
		uint64_t reserved_11_15:5;
		uint64_t bypass_ext:1;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_ciu_qlm_jtgc_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t clk_div:3;
		uint64_t reserved_5_7:3;
		uint64_t mux_sel:1;
		uint64_t reserved_2_3:2;
		uint64_t bypass:2;
#else
		uint64_t bypass:2;
		uint64_t reserved_2_3:2;
		uint64_t mux_sel:1;
		uint64_t reserved_5_7:3;
		uint64_t clk_div:3;
		uint64_t reserved_11_63:53;
#endif
	} cn52xx;
	struct cvmx_ciu_qlm_jtgc_cn52xx cn52xxp1;
	struct cvmx_ciu_qlm_jtgc_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t clk_div:3;
		uint64_t reserved_6_7:2;
		uint64_t mux_sel:2;
		uint64_t bypass:4;
#else
		uint64_t bypass:4;
		uint64_t mux_sel:2;
		uint64_t reserved_6_7:2;
		uint64_t clk_div:3;
		uint64_t reserved_11_63:53;
#endif
	} cn56xx;
	struct cvmx_ciu_qlm_jtgc_cn56xx cn56xxp1;
	struct cvmx_ciu_qlm_jtgc_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t clk_div:3;
		uint64_t reserved_6_7:2;
		uint64_t mux_sel:2;
		uint64_t reserved_3_3:1;
		uint64_t bypass:3;
#else
		uint64_t bypass:3;
		uint64_t reserved_3_3:1;
		uint64_t mux_sel:2;
		uint64_t reserved_6_7:2;
		uint64_t clk_div:3;
		uint64_t reserved_11_63:53;
#endif
	} cn61xx;
	struct cvmx_ciu_qlm_jtgc_cn61xx cn63xx;
	struct cvmx_ciu_qlm_jtgc_cn61xx cn63xxp1;
	struct cvmx_ciu_qlm_jtgc_cn61xx cn66xx;
	struct cvmx_ciu_qlm_jtgc_s cn68xx;
	struct cvmx_ciu_qlm_jtgc_s cn68xxp1;
	struct cvmx_ciu_qlm_jtgc_cn61xx cnf71xx;
};

union cvmx_ciu_qlm_jtgd {
	uint64_t u64;
	struct cvmx_ciu_qlm_jtgd_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t capture:1;
		uint64_t shift:1;
		uint64_t update:1;
		uint64_t reserved_45_60:16;
		uint64_t select:5;
		uint64_t reserved_37_39:3;
		uint64_t shft_cnt:5;
		uint64_t shft_reg:32;
#else
		uint64_t shft_reg:32;
		uint64_t shft_cnt:5;
		uint64_t reserved_37_39:3;
		uint64_t select:5;
		uint64_t reserved_45_60:16;
		uint64_t update:1;
		uint64_t shift:1;
		uint64_t capture:1;
#endif
	} s;
	struct cvmx_ciu_qlm_jtgd_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t capture:1;
		uint64_t shift:1;
		uint64_t update:1;
		uint64_t reserved_42_60:19;
		uint64_t select:2;
		uint64_t reserved_37_39:3;
		uint64_t shft_cnt:5;
		uint64_t shft_reg:32;
#else
		uint64_t shft_reg:32;
		uint64_t shft_cnt:5;
		uint64_t reserved_37_39:3;
		uint64_t select:2;
		uint64_t reserved_42_60:19;
		uint64_t update:1;
		uint64_t shift:1;
		uint64_t capture:1;
#endif
	} cn52xx;
	struct cvmx_ciu_qlm_jtgd_cn52xx cn52xxp1;
	struct cvmx_ciu_qlm_jtgd_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t capture:1;
		uint64_t shift:1;
		uint64_t update:1;
		uint64_t reserved_44_60:17;
		uint64_t select:4;
		uint64_t reserved_37_39:3;
		uint64_t shft_cnt:5;
		uint64_t shft_reg:32;
#else
		uint64_t shft_reg:32;
		uint64_t shft_cnt:5;
		uint64_t reserved_37_39:3;
		uint64_t select:4;
		uint64_t reserved_44_60:17;
		uint64_t update:1;
		uint64_t shift:1;
		uint64_t capture:1;
#endif
	} cn56xx;
	struct cvmx_ciu_qlm_jtgd_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t capture:1;
		uint64_t shift:1;
		uint64_t update:1;
		uint64_t reserved_37_60:24;
		uint64_t shft_cnt:5;
		uint64_t shft_reg:32;
#else
		uint64_t shft_reg:32;
		uint64_t shft_cnt:5;
		uint64_t reserved_37_60:24;
		uint64_t update:1;
		uint64_t shift:1;
		uint64_t capture:1;
#endif
	} cn56xxp1;
	struct cvmx_ciu_qlm_jtgd_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t capture:1;
		uint64_t shift:1;
		uint64_t update:1;
		uint64_t reserved_43_60:18;
		uint64_t select:3;
		uint64_t reserved_37_39:3;
		uint64_t shft_cnt:5;
		uint64_t shft_reg:32;
#else
		uint64_t shft_reg:32;
		uint64_t shft_cnt:5;
		uint64_t reserved_37_39:3;
		uint64_t select:3;
		uint64_t reserved_43_60:18;
		uint64_t update:1;
		uint64_t shift:1;
		uint64_t capture:1;
#endif
	} cn61xx;
	struct cvmx_ciu_qlm_jtgd_cn61xx cn63xx;
	struct cvmx_ciu_qlm_jtgd_cn61xx cn63xxp1;
	struct cvmx_ciu_qlm_jtgd_cn61xx cn66xx;
	struct cvmx_ciu_qlm_jtgd_s cn68xx;
	struct cvmx_ciu_qlm_jtgd_s cn68xxp1;
	struct cvmx_ciu_qlm_jtgd_cn61xx cnf71xx;
};

union cvmx_ciu_soft_bist {
	uint64_t u64;
	struct cvmx_ciu_soft_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t soft_bist:1;
#else
		uint64_t soft_bist:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu_soft_bist_s cn30xx;
	struct cvmx_ciu_soft_bist_s cn31xx;
	struct cvmx_ciu_soft_bist_s cn38xx;
	struct cvmx_ciu_soft_bist_s cn38xxp2;
	struct cvmx_ciu_soft_bist_s cn50xx;
	struct cvmx_ciu_soft_bist_s cn52xx;
	struct cvmx_ciu_soft_bist_s cn52xxp1;
	struct cvmx_ciu_soft_bist_s cn56xx;
	struct cvmx_ciu_soft_bist_s cn56xxp1;
	struct cvmx_ciu_soft_bist_s cn58xx;
	struct cvmx_ciu_soft_bist_s cn58xxp1;
	struct cvmx_ciu_soft_bist_s cn61xx;
	struct cvmx_ciu_soft_bist_s cn63xx;
	struct cvmx_ciu_soft_bist_s cn63xxp1;
	struct cvmx_ciu_soft_bist_s cn66xx;
	struct cvmx_ciu_soft_bist_s cn68xx;
	struct cvmx_ciu_soft_bist_s cn68xxp1;
	struct cvmx_ciu_soft_bist_s cnf71xx;
};

union cvmx_ciu_soft_prst {
	uint64_t u64;
	struct cvmx_ciu_soft_prst_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t host64:1;
		uint64_t npi:1;
		uint64_t soft_prst:1;
#else
		uint64_t soft_prst:1;
		uint64_t npi:1;
		uint64_t host64:1;
		uint64_t reserved_3_63:61;
#endif
	} s;
	struct cvmx_ciu_soft_prst_s cn30xx;
	struct cvmx_ciu_soft_prst_s cn31xx;
	struct cvmx_ciu_soft_prst_s cn38xx;
	struct cvmx_ciu_soft_prst_s cn38xxp2;
	struct cvmx_ciu_soft_prst_s cn50xx;
	struct cvmx_ciu_soft_prst_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t soft_prst:1;
#else
		uint64_t soft_prst:1;
		uint64_t reserved_1_63:63;
#endif
	} cn52xx;
	struct cvmx_ciu_soft_prst_cn52xx cn52xxp1;
	struct cvmx_ciu_soft_prst_cn52xx cn56xx;
	struct cvmx_ciu_soft_prst_cn52xx cn56xxp1;
	struct cvmx_ciu_soft_prst_s cn58xx;
	struct cvmx_ciu_soft_prst_s cn58xxp1;
	struct cvmx_ciu_soft_prst_cn52xx cn61xx;
	struct cvmx_ciu_soft_prst_cn52xx cn63xx;
	struct cvmx_ciu_soft_prst_cn52xx cn63xxp1;
	struct cvmx_ciu_soft_prst_cn52xx cn66xx;
	struct cvmx_ciu_soft_prst_cn52xx cn68xx;
	struct cvmx_ciu_soft_prst_cn52xx cn68xxp1;
	struct cvmx_ciu_soft_prst_cn52xx cnf71xx;
};

union cvmx_ciu_soft_prst1 {
	uint64_t u64;
	struct cvmx_ciu_soft_prst1_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t soft_prst:1;
#else
		uint64_t soft_prst:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu_soft_prst1_s cn52xx;
	struct cvmx_ciu_soft_prst1_s cn52xxp1;
	struct cvmx_ciu_soft_prst1_s cn56xx;
	struct cvmx_ciu_soft_prst1_s cn56xxp1;
	struct cvmx_ciu_soft_prst1_s cn61xx;
	struct cvmx_ciu_soft_prst1_s cn63xx;
	struct cvmx_ciu_soft_prst1_s cn63xxp1;
	struct cvmx_ciu_soft_prst1_s cn66xx;
	struct cvmx_ciu_soft_prst1_s cn68xx;
	struct cvmx_ciu_soft_prst1_s cn68xxp1;
	struct cvmx_ciu_soft_prst1_s cnf71xx;
};

union cvmx_ciu_soft_prst2 {
	uint64_t u64;
	struct cvmx_ciu_soft_prst2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t soft_prst:1;
#else
		uint64_t soft_prst:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu_soft_prst2_s cn66xx;
};

union cvmx_ciu_soft_prst3 {
	uint64_t u64;
	struct cvmx_ciu_soft_prst3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t soft_prst:1;
#else
		uint64_t soft_prst:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu_soft_prst3_s cn66xx;
};

union cvmx_ciu_soft_rst {
	uint64_t u64;
	struct cvmx_ciu_soft_rst_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t soft_rst:1;
#else
		uint64_t soft_rst:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu_soft_rst_s cn30xx;
	struct cvmx_ciu_soft_rst_s cn31xx;
	struct cvmx_ciu_soft_rst_s cn38xx;
	struct cvmx_ciu_soft_rst_s cn38xxp2;
	struct cvmx_ciu_soft_rst_s cn50xx;
	struct cvmx_ciu_soft_rst_s cn52xx;
	struct cvmx_ciu_soft_rst_s cn52xxp1;
	struct cvmx_ciu_soft_rst_s cn56xx;
	struct cvmx_ciu_soft_rst_s cn56xxp1;
	struct cvmx_ciu_soft_rst_s cn58xx;
	struct cvmx_ciu_soft_rst_s cn58xxp1;
	struct cvmx_ciu_soft_rst_s cn61xx;
	struct cvmx_ciu_soft_rst_s cn63xx;
	struct cvmx_ciu_soft_rst_s cn63xxp1;
	struct cvmx_ciu_soft_rst_s cn66xx;
	struct cvmx_ciu_soft_rst_s cn68xx;
	struct cvmx_ciu_soft_rst_s cn68xxp1;
	struct cvmx_ciu_soft_rst_s cnf71xx;
};

union cvmx_ciu_sum1_iox_int {
	uint64_t u64;
	struct cvmx_ciu_sum1_iox_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_sum1_iox_int_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_sum1_iox_int_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_sum1_iox_int_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_41_46:6;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_46:6;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_sum1_ppx_ip2 {
	uint64_t u64;
	struct cvmx_ciu_sum1_ppx_ip2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_sum1_ppx_ip2_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_sum1_ppx_ip2_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_sum1_ppx_ip2_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_41_46:6;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_46:6;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_sum1_ppx_ip3 {
	uint64_t u64;
	struct cvmx_ciu_sum1_ppx_ip3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_sum1_ppx_ip3_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_sum1_ppx_ip3_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_sum1_ppx_ip3_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_41_46:6;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_46:6;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_sum1_ppx_ip4 {
	uint64_t u64;
	struct cvmx_ciu_sum1_ppx_ip4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu_sum1_ppx_ip4_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_41_45:5;
		uint64_t dpi_dma:1;
		uint64_t reserved_38_39:2;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_4_17:14;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_17:14;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_39:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_45:5;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cn61xx;
	struct cvmx_ciu_sum1_ppx_ip4_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_62_62:1;
		uint64_t srio3:1;
		uint64_t srio2:1;
		uint64_t reserved_57_59:3;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t reserved_51_51:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_38_45:8;
		uint64_t agx1:1;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_10_17:8;
		uint64_t wdog:10;
#else
		uint64_t wdog:10;
		uint64_t reserved_10_17:8;
		uint64_t mii1:1;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t zip:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t dfa:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t agx1:1;
		uint64_t reserved_38_45:8;
		uint64_t agl:1;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t srio0:1;
		uint64_t reserved_51_51:1;
		uint64_t lmc0:1;
		uint64_t reserved_53_55:3;
		uint64_t dfm:1;
		uint64_t reserved_57_59:3;
		uint64_t srio2:1;
		uint64_t srio3:1;
		uint64_t reserved_62_62:1;
		uint64_t rst:1;
#endif
	} cn66xx;
	struct cvmx_ciu_sum1_ppx_ip4_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_53_62:10;
		uint64_t lmc0:1;
		uint64_t reserved_50_51:2;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t reserved_41_46:6;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t reserved_32_32:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_28_28:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_4_18:15;
		uint64_t wdog:4;
#else
		uint64_t wdog:4;
		uint64_t reserved_4_18:15;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t iob:1;
		uint64_t fpa:1;
		uint64_t pow:1;
		uint64_t l2c:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_28_28:1;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_32_32:1;
		uint64_t usb:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t agx0:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_41_46:6;
		uint64_t ptp:1;
		uint64_t pem0:1;
		uint64_t pem1:1;
		uint64_t reserved_50_51:2;
		uint64_t lmc0:1;
		uint64_t reserved_53_62:10;
		uint64_t rst:1;
#endif
	} cnf71xx;
};

union cvmx_ciu_sum2_iox_int {
	uint64_t u64;
	struct cvmx_ciu_sum2_iox_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_sum2_iox_int_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_sum2_iox_int_cn61xx cn66xx;
	struct cvmx_ciu_sum2_iox_int_s cnf71xx;
};

union cvmx_ciu_sum2_ppx_ip2 {
	uint64_t u64;
	struct cvmx_ciu_sum2_ppx_ip2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_sum2_ppx_ip2_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_sum2_ppx_ip2_cn61xx cn66xx;
	struct cvmx_ciu_sum2_ppx_ip2_s cnf71xx;
};

union cvmx_ciu_sum2_ppx_ip3 {
	uint64_t u64;
	struct cvmx_ciu_sum2_ppx_ip3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_sum2_ppx_ip3_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_sum2_ppx_ip3_cn61xx cn66xx;
	struct cvmx_ciu_sum2_ppx_ip3_s cnf71xx;
};

union cvmx_ciu_sum2_ppx_ip4 {
	uint64_t u64;
	struct cvmx_ciu_sum2_ppx_ip4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t endor:2;
		uint64_t eoi:1;
		uint64_t reserved_10_11:2;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_11:2;
		uint64_t eoi:1;
		uint64_t endor:2;
		uint64_t reserved_15_63:49;
#endif
	} s;
	struct cvmx_ciu_sum2_ppx_ip4_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t timer:6;
		uint64_t reserved_0_3:4;
#else
		uint64_t reserved_0_3:4;
		uint64_t timer:6;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_ciu_sum2_ppx_ip4_cn61xx cn66xx;
	struct cvmx_ciu_sum2_ppx_ip4_s cnf71xx;
};

union cvmx_ciu_timx {
	uint64_t u64;
	struct cvmx_ciu_timx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_37_63:27;
		uint64_t one_shot:1;
		uint64_t len:36;
#else
		uint64_t len:36;
		uint64_t one_shot:1;
		uint64_t reserved_37_63:27;
#endif
	} s;
	struct cvmx_ciu_timx_s cn30xx;
	struct cvmx_ciu_timx_s cn31xx;
	struct cvmx_ciu_timx_s cn38xx;
	struct cvmx_ciu_timx_s cn38xxp2;
	struct cvmx_ciu_timx_s cn50xx;
	struct cvmx_ciu_timx_s cn52xx;
	struct cvmx_ciu_timx_s cn52xxp1;
	struct cvmx_ciu_timx_s cn56xx;
	struct cvmx_ciu_timx_s cn56xxp1;
	struct cvmx_ciu_timx_s cn58xx;
	struct cvmx_ciu_timx_s cn58xxp1;
	struct cvmx_ciu_timx_s cn61xx;
	struct cvmx_ciu_timx_s cn63xx;
	struct cvmx_ciu_timx_s cn63xxp1;
	struct cvmx_ciu_timx_s cn66xx;
	struct cvmx_ciu_timx_s cn68xx;
	struct cvmx_ciu_timx_s cn68xxp1;
	struct cvmx_ciu_timx_s cnf71xx;
};

union cvmx_ciu_tim_multi_cast {
	uint64_t u64;
	struct cvmx_ciu_tim_multi_cast_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu_tim_multi_cast_s cn61xx;
	struct cvmx_ciu_tim_multi_cast_s cn66xx;
	struct cvmx_ciu_tim_multi_cast_s cnf71xx;
};

union cvmx_ciu_wdogx {
	uint64_t u64;
	struct cvmx_ciu_wdogx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_46_63:18;
		uint64_t gstopen:1;
		uint64_t dstop:1;
		uint64_t cnt:24;
		uint64_t len:16;
		uint64_t state:2;
		uint64_t mode:2;
#else
		uint64_t mode:2;
		uint64_t state:2;
		uint64_t len:16;
		uint64_t cnt:24;
		uint64_t dstop:1;
		uint64_t gstopen:1;
		uint64_t reserved_46_63:18;
#endif
	} s;
	struct cvmx_ciu_wdogx_s cn30xx;
	struct cvmx_ciu_wdogx_s cn31xx;
	struct cvmx_ciu_wdogx_s cn38xx;
	struct cvmx_ciu_wdogx_s cn38xxp2;
	struct cvmx_ciu_wdogx_s cn50xx;
	struct cvmx_ciu_wdogx_s cn52xx;
	struct cvmx_ciu_wdogx_s cn52xxp1;
	struct cvmx_ciu_wdogx_s cn56xx;
	struct cvmx_ciu_wdogx_s cn56xxp1;
	struct cvmx_ciu_wdogx_s cn58xx;
	struct cvmx_ciu_wdogx_s cn58xxp1;
	struct cvmx_ciu_wdogx_s cn61xx;
	struct cvmx_ciu_wdogx_s cn63xx;
	struct cvmx_ciu_wdogx_s cn63xxp1;
	struct cvmx_ciu_wdogx_s cn66xx;
	struct cvmx_ciu_wdogx_s cn68xx;
	struct cvmx_ciu_wdogx_s cn68xxp1;
	struct cvmx_ciu_wdogx_s cnf71xx;
};

#endif
