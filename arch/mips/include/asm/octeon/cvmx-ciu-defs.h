/* SPDX-License-Identifier: GPL-2.0 */
/* Octeon CIU definitions
 *
 * Copyright (C) 2003-2018 Cavium, Inc.
 */

#ifndef __CVMX_CIU_DEFS_H__
#define __CVMX_CIU_DEFS_H__

#include <asm/bitfield.h>

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
	case OCTEON_CN70XX & OCTEON_FAMILY_MASK:
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
	case OCTEON_CNF75XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN73XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN78XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001010000030000ull) + (offset) * 8;
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
	case OCTEON_CN70XX & OCTEON_FAMILY_MASK:
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
	case OCTEON_CNF75XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN73XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN78XX & OCTEON_FAMILY_MASK:
		return CVMX_ADD_IO_SEG(0x0001010000020000ull) + (offset) * 8;
	}
	return CVMX_ADD_IO_SEG(0x0001070000000500ull) + (offset) * 8;
}


union cvmx_ciu_qlm {
	uint64_t u64;
	struct cvmx_ciu_qlm_s {
		__BITFIELD_FIELD(uint64_t g2bypass:1,
		__BITFIELD_FIELD(uint64_t reserved_53_62:10,
		__BITFIELD_FIELD(uint64_t g2deemph:5,
		__BITFIELD_FIELD(uint64_t reserved_45_47:3,
		__BITFIELD_FIELD(uint64_t g2margin:5,
		__BITFIELD_FIELD(uint64_t reserved_32_39:8,
		__BITFIELD_FIELD(uint64_t txbypass:1,
		__BITFIELD_FIELD(uint64_t reserved_21_30:10,
		__BITFIELD_FIELD(uint64_t txdeemph:5,
		__BITFIELD_FIELD(uint64_t reserved_13_15:3,
		__BITFIELD_FIELD(uint64_t txmargin:5,
		__BITFIELD_FIELD(uint64_t reserved_4_7:4,
		__BITFIELD_FIELD(uint64_t lane_en:4,
		;)))))))))))))
	} s;
};

union cvmx_ciu_qlm_jtgc {
	uint64_t u64;
	struct cvmx_ciu_qlm_jtgc_s {
		__BITFIELD_FIELD(uint64_t reserved_17_63:47,
		__BITFIELD_FIELD(uint64_t bypass_ext:1,
		__BITFIELD_FIELD(uint64_t reserved_11_15:5,
		__BITFIELD_FIELD(uint64_t clk_div:3,
		__BITFIELD_FIELD(uint64_t reserved_7_7:1,
		__BITFIELD_FIELD(uint64_t mux_sel:3,
		__BITFIELD_FIELD(uint64_t bypass:4,
		;)))))))
	} s;
};

union cvmx_ciu_qlm_jtgd {
	uint64_t u64;
	struct cvmx_ciu_qlm_jtgd_s {
		__BITFIELD_FIELD(uint64_t capture:1,
		__BITFIELD_FIELD(uint64_t shift:1,
		__BITFIELD_FIELD(uint64_t update:1,
		__BITFIELD_FIELD(uint64_t reserved_45_60:16,
		__BITFIELD_FIELD(uint64_t select:5,
		__BITFIELD_FIELD(uint64_t reserved_37_39:3,
		__BITFIELD_FIELD(uint64_t shft_cnt:5,
		__BITFIELD_FIELD(uint64_t shft_reg:32,
		;))))))))
	} s;
};

union cvmx_ciu_soft_prst {
	uint64_t u64;
	struct cvmx_ciu_soft_prst_s {
		__BITFIELD_FIELD(uint64_t reserved_3_63:61,
		__BITFIELD_FIELD(uint64_t host64:1,
		__BITFIELD_FIELD(uint64_t npi:1,
		__BITFIELD_FIELD(uint64_t soft_prst:1,
		;))))
	} s;
};

union cvmx_ciu_timx {
	uint64_t u64;
	struct cvmx_ciu_timx_s {
		__BITFIELD_FIELD(uint64_t reserved_37_63:27,
		__BITFIELD_FIELD(uint64_t one_shot:1,
		__BITFIELD_FIELD(uint64_t len:36,
		;)))
	} s;
};

union cvmx_ciu_wdogx {
	uint64_t u64;
	struct cvmx_ciu_wdogx_s {
		__BITFIELD_FIELD(uint64_t reserved_46_63:18,
		__BITFIELD_FIELD(uint64_t gstopen:1,
		__BITFIELD_FIELD(uint64_t dstop:1,
		__BITFIELD_FIELD(uint64_t cnt:24,
		__BITFIELD_FIELD(uint64_t len:16,
		__BITFIELD_FIELD(uint64_t state:2,
		__BITFIELD_FIELD(uint64_t mode:2,
		;)))))))
	} s;
};

#endif /* __CVMX_CIU_DEFS_H__ */
