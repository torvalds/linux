/* SPDX-License-Identifier: GPL-2.0 */
/* Octeon CIU definitions
 *
 * Copyright (C) 2003-2018 Cavium, Inc.
 */

#ifndef __CVMX_CIU_DEFS_H__
#define __CVMX_CIU_DEFS_H__

#include <asm/bitfield.h>

#define CVMX_CIU_ADDR(addr, coreid, coremask, offset)			       \
	(CVMX_ADD_IO_SEG(0x0001070000000000ull + addr##ull) +		       \
	(((coreid) & (coremask)) * offset))

#define CVMX_CIU_EN2_PPX_IP4(c)		CVMX_CIU_ADDR(0xA400, c, 0x0F, 8)
#define CVMX_CIU_EN2_PPX_IP4_W1C(c)	CVMX_CIU_ADDR(0xCC00, c, 0x0F, 8)
#define CVMX_CIU_EN2_PPX_IP4_W1S(c)	CVMX_CIU_ADDR(0xAC00, c, 0x0F, 8)
#define CVMX_CIU_FUSE			CVMX_CIU_ADDR(0x0728, 0, 0x00, 0)
#define CVMX_CIU_INT_SUM1		CVMX_CIU_ADDR(0x0108, 0, 0x00, 0)
#define CVMX_CIU_INTX_EN0(c)		CVMX_CIU_ADDR(0x0200, c, 0x3F, 16)
#define CVMX_CIU_INTX_EN0_W1C(c)	CVMX_CIU_ADDR(0x2200, c, 0x3F, 16)
#define CVMX_CIU_INTX_EN0_W1S(c)	CVMX_CIU_ADDR(0x6200, c, 0x3F, 16)
#define CVMX_CIU_INTX_EN1(c)		CVMX_CIU_ADDR(0x0208, c, 0x3F, 16)
#define CVMX_CIU_INTX_EN1_W1C(c)	CVMX_CIU_ADDR(0x2208, c, 0x3F, 16)
#define CVMX_CIU_INTX_EN1_W1S(c)	CVMX_CIU_ADDR(0x6208, c, 0x3F, 16)
#define CVMX_CIU_INTX_SUM0(c)		CVMX_CIU_ADDR(0x0000, c, 0x3F, 8)
#define CVMX_CIU_NMI			CVMX_CIU_ADDR(0x0718, 0, 0x00, 0)
#define CVMX_CIU_PCI_INTA		CVMX_CIU_ADDR(0x0750, 0, 0x00, 0)
#define CVMX_CIU_PP_BIST_STAT		CVMX_CIU_ADDR(0x07E0, 0, 0x00, 0)
#define CVMX_CIU_PP_DBG			CVMX_CIU_ADDR(0x0708, 0, 0x00, 0)
#define CVMX_CIU_PP_RST			CVMX_CIU_ADDR(0x0700, 0, 0x00, 0)
#define CVMX_CIU_QLM0			CVMX_CIU_ADDR(0x0780, 0, 0x00, 0)
#define CVMX_CIU_QLM1			CVMX_CIU_ADDR(0x0788, 0, 0x00, 0)
#define CVMX_CIU_QLM_JTGC		CVMX_CIU_ADDR(0x0768, 0, 0x00, 0)
#define CVMX_CIU_QLM_JTGD		CVMX_CIU_ADDR(0x0770, 0, 0x00, 0)
#define CVMX_CIU_SOFT_BIST		CVMX_CIU_ADDR(0x0738, 0, 0x00, 0)
#define CVMX_CIU_SOFT_PRST1		CVMX_CIU_ADDR(0x0758, 0, 0x00, 0)
#define CVMX_CIU_SOFT_PRST		CVMX_CIU_ADDR(0x0748, 0, 0x00, 0)
#define CVMX_CIU_SOFT_RST		CVMX_CIU_ADDR(0x0740, 0, 0x00, 0)
#define CVMX_CIU_SUM2_PPX_IP4(c)	CVMX_CIU_ADDR(0x8C00, c, 0x0F, 8)
#define CVMX_CIU_TIM_MULTI_CAST		CVMX_CIU_ADDR(0xC200, 0, 0x00, 0)
#define CVMX_CIU_TIMX(c)		CVMX_CIU_ADDR(0x0480, c, 0x0F, 8)

static inline uint64_t CVMX_CIU_MBOX_CLRX(unsigned int coreid)
{
	if (cvmx_get_octeon_family() == (OCTEON_CN68XX & OCTEON_FAMILY_MASK))
		return CVMX_CIU_ADDR(0x100100600, coreid, 0x0F, 8);
	else
		return CVMX_CIU_ADDR(0x000000680, coreid, 0x0F, 8);
}

static inline uint64_t CVMX_CIU_MBOX_SETX(unsigned int coreid)
{
	if (cvmx_get_octeon_family() == (OCTEON_CN68XX & OCTEON_FAMILY_MASK))
		return CVMX_CIU_ADDR(0x100100400, coreid, 0x0F, 8);
	else
		return CVMX_CIU_ADDR(0x000000600, coreid, 0x0F, 8);
}

static inline uint64_t CVMX_CIU_PP_POKEX(unsigned int coreid)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_CIU_ADDR(0x100100200, coreid, 0x0F, 8);
	case OCTEON_CNF75XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN73XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN78XX & OCTEON_FAMILY_MASK:
		return CVMX_CIU_ADDR(0x000030000, coreid, 0x0F, 8) -
			0x60000000000ull;
	default:
		return CVMX_CIU_ADDR(0x000000580, coreid, 0x0F, 8);
	}
}

static inline uint64_t CVMX_CIU_WDOGX(unsigned int coreid)
{
	switch (cvmx_get_octeon_family()) {
	case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
		return CVMX_CIU_ADDR(0x100100000, coreid, 0x0F, 8);
	case OCTEON_CNF75XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN73XX & OCTEON_FAMILY_MASK:
	case OCTEON_CN78XX & OCTEON_FAMILY_MASK:
		return CVMX_CIU_ADDR(0x000020000, coreid, 0x0F, 8) -
			0x60000000000ull;
	default:
		return CVMX_CIU_ADDR(0x000000500, coreid, 0x0F, 8);
	}
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
