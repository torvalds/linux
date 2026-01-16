// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_print.h>

#include "i915_reg.h"
#include "intel_cx0_phy.h"
#include "intel_cx0_phy_regs.h"
#include "intel_ddi.h"
#include "intel_ddi_buf_trans.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_display_utils.h"
#include "intel_dpll_mgr.h"
#include "intel_hdmi.h"
#include "intel_lt_phy.h"
#include "intel_lt_phy_regs.h"
#include "intel_panel.h"
#include "intel_psr.h"
#include "intel_tc.h"

#define for_each_lt_phy_lane_in_mask(__lane_mask, __lane) \
	for ((__lane) = 0; (__lane) < 2; (__lane)++) \
		for_each_if((__lane_mask) & BIT(__lane))

#define INTEL_LT_PHY_LANE0		BIT(0)
#define INTEL_LT_PHY_LANE1		BIT(1)
#define INTEL_LT_PHY_BOTH_LANES		(INTEL_LT_PHY_LANE1 |\
					 INTEL_LT_PHY_LANE0)
#define MODE_DP				3
#define MODE_HDMI_20			4
#define Q32_TO_INT(x)	((x) >> 32)
#define Q32_TO_FRAC(x)	((x) & 0xFFFFFFFF)
#define DCO_MIN_FREQ_MHZ	11850
#define REF_CLK_KHZ	38400
#define TDC_RES_MULTIPLIER	10000000ULL

struct phy_param_t {
	u32 val;
	u32 addr;
};

struct lt_phy_params {
	struct phy_param_t pll_reg4;
	struct phy_param_t pll_reg3;
	struct phy_param_t pll_reg5;
	struct phy_param_t pll_reg57;
	struct phy_param_t lf;
	struct phy_param_t tdc;
	struct phy_param_t ssc;
	struct phy_param_t bias2;
	struct phy_param_t bias_trim;
	struct phy_param_t dco_med;
	struct phy_param_t dco_fine;
	struct phy_param_t ssc_inj;
	struct phy_param_t surv_bonus;
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_dp_rbr = {
	.clock = 162000,
	.config = {
		0x83,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x5,  0xa,  0x2a, 0x20 },
		{ 0x80, 0x0,  0x0,  0x0  },
		{ 0x4,  0x4,  0x82, 0x28 },
		{ 0xfa, 0x16, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x5,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x4b, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x13, 0x29, 0x13 },
		{ 0x0,  0x5b, 0xe0, 0x0a },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_dp_hbr1 = {
	.clock = 270000,
	.config = {
		0x8b,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x3,  0xca, 0x34, 0xa0 },
		{ 0xe0, 0x0,  0x0,  0x0  },
		{ 0x5,  0x4,  0x81, 0xad },
		{ 0xfa, 0x11, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x7,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x43, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x13, 0x29, 0x13 },
		{ 0x0,  0x5b, 0xe0, 0x0d },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_dp_hbr2 = {
	.clock = 540000,
	.config = {
		0x93,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x1,  0x4d, 0x34, 0xa0 },
		{ 0xe0, 0x0,  0x0,  0x0  },
		{ 0xa,  0x4,  0x81, 0xda },
		{ 0xfa, 0x11, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x7,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x43, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x13, 0x29, 0x13 },
		{ 0x0,  0x5b, 0xe0, 0x0d },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_dp_hbr3 = {
	.clock = 810000,
	.config = {
		0x9b,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x1,  0x4a, 0x34, 0xa0 },
		{ 0xe0, 0x0,  0x0,  0x0  },
		{ 0x5,  0x4,  0x80, 0xa8 },
		{ 0xfa, 0x11, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x7,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x43, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x13, 0x29, 0x13 },
		{ 0x0,  0x5b, 0xe0, 0x0d },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_dp_uhbr10 = {
	.clock = 1000000,
	.config = {
		0x43,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x85,
		0x85,
		0x85,
		0x85,
		0x86,
		0x86,
		0x86,
		0x86,
		0x86,
		0x86,
		0x86,
		0x86,
		0x86,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x1,  0xa,  0x20, 0x80 },
		{ 0x6a, 0xaa, 0xaa, 0xab },
		{ 0x0,  0x3,  0x4,  0x94 },
		{ 0xfa, 0x1c, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x4,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x45, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x14, 0x2a, 0x14 },
		{ 0x0,  0x5b, 0xe0, 0x8  },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_dp_uhbr13_5 = {
	.clock = 1350000,
	.config = {
		0xcb,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x2,  0x9,  0x2b, 0xe0 },
		{ 0x90, 0x0,  0x0,  0x0  },
		{ 0x8,  0x4,  0x80, 0xe0 },
		{ 0xfa, 0x15, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x6,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x49, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x13, 0x29, 0x13 },
		{ 0x0,  0x57, 0xe0, 0x0c },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_dp_uhbr20 = {
	.clock = 2000000,
	.config = {
		0x53,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x85,
		0x85,
		0x85,
		0x85,
		0x86,
		0x86,
		0x86,
		0x86,
		0x86,
		0x86,
		0x86,
		0x86,
		0x86,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x1,  0xa,  0x20, 0x80 },
		{ 0x6a, 0xaa, 0xaa, 0xab },
		{ 0x0,  0x3,  0x4,  0x94 },
		{ 0xfa, 0x1c, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x4,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x45, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x14, 0x2a, 0x14 },
		{ 0x0,  0x5b, 0xe0, 0x8  },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state * const xe3plpd_lt_dp_tables[] = {
	&xe3plpd_lt_dp_rbr,
	&xe3plpd_lt_dp_hbr1,
	&xe3plpd_lt_dp_hbr2,
	&xe3plpd_lt_dp_hbr3,
	&xe3plpd_lt_dp_uhbr10,
	&xe3plpd_lt_dp_uhbr13_5,
	&xe3plpd_lt_dp_uhbr20,
	NULL,
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_edp_2_16 = {
	.clock = 216000,
	.config = {
		0xa3,
		0x2d,
		0x1,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x3,  0xca, 0x2a, 0x20 },
		{ 0x80, 0x0,  0x0,  0x0  },
		{ 0x6,  0x4,  0x81, 0xbc },
		{ 0xfa, 0x16, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x5,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x4b, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x13, 0x29, 0x13 },
		{ 0x0,  0x5b, 0xe0, 0x0a },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_edp_2_43 = {
	.clock = 243000,
	.config = {
		0xab,
		0x2d,
		0x1,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x3,  0xca, 0x2f, 0x60 },
		{ 0xb0, 0x0,  0x0,  0x0  },
		{ 0x6,  0x4,  0x81, 0xbc },
		{ 0xfa, 0x13, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x6,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x47, 0x48, 0x0,  0x0  },
		{ 0x0,  0x0,  0x0,  0x0  },
		{ 0x5a, 0x13, 0x29, 0x13 },
		{ 0x0,  0x5b, 0xe0, 0x0c },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_edp_3_24 = {
	.clock = 324000,
	.config = {
		0xb3,
		0x2d,
		0x1,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x2,  0x8a, 0x2a, 0x20 },
		{ 0x80, 0x0,  0x0,  0x0  },
		{ 0x6,  0x4,  0x81, 0x28 },
		{ 0xfa, 0x16, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x5,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x4b, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x13, 0x29, 0x13 },
		{ 0x0,  0x5b, 0xe0, 0x0a },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_edp_4_32 = {
	.clock = 432000,
	.config = {
		0xbb,
		0x2d,
		0x1,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x1,  0x4d, 0x2a, 0x20 },
		{ 0x80, 0x0,  0x0,  0x0  },
		{ 0xc,  0x4,  0x81, 0xbc },
		{ 0xfa, 0x16, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x5,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x4b, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x13, 0x29, 0x13 },
		{ 0x0,  0x5b, 0xe0, 0x0a },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_edp_6_75 = {
	.clock = 675000,
	.config = {
		0xdb,
		0x2d,
		0x1,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x1,  0x4a, 0x2b, 0xe0 },
		{ 0x90, 0x0,  0x0,  0x0  },
		{ 0x6,  0x4,  0x80, 0xa8 },
		{ 0xfa, 0x15, 0x83, 0x11 },
		{ 0x80, 0x0f, 0xf9, 0x53 },
		{ 0x84, 0x26, 0x6,  0x4  },
		{ 0x0,  0xe0, 0x1,  0x0  },
		{ 0x49, 0x48, 0x0,  0x0  },
		{ 0x27, 0x8,  0x0,  0x0  },
		{ 0x5a, 0x13, 0x29, 0x13 },
		{ 0x0,  0x57, 0xe0, 0x0c },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state * const xe3plpd_lt_edp_tables[] = {
	&xe3plpd_lt_dp_rbr,
	&xe3plpd_lt_edp_2_16,
	&xe3plpd_lt_edp_2_43,
	&xe3plpd_lt_dp_hbr1,
	&xe3plpd_lt_edp_3_24,
	&xe3plpd_lt_edp_4_32,
	&xe3plpd_lt_dp_hbr2,
	&xe3plpd_lt_edp_6_75,
	&xe3plpd_lt_dp_hbr3,
	NULL,
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_hdmi_252 = {
	.clock = 25200,
	.config = {
		0x84,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x0c, 0x15, 0x27, 0x60 },
		{ 0x0,  0x0,  0x0,  0x0  },
		{ 0x8,  0x4,  0x98, 0x28 },
		{ 0x42, 0x0,  0x84, 0x10 },
		{ 0x80, 0x0f, 0xd9, 0xb5 },
		{ 0x86, 0x0,  0x0,  0x0  },
		{ 0x1,  0xa0, 0x1,  0x0  },
		{ 0x4b, 0x0,  0x0,  0x0  },
		{ 0x28, 0x0,  0x0,  0x0  },
		{ 0x0,  0x14, 0x2a, 0x14 },
		{ 0x0,  0x0,  0x0,  0x0  },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_hdmi_272 = {
	.clock = 27200,
	.config = {
		0x84,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x0b, 0x15, 0x26, 0xa0 },
		{ 0x60, 0x0,  0x0,  0x0  },
		{ 0x8,  0x4,  0x96, 0x28 },
		{ 0xfa, 0x0c, 0x84, 0x11 },
		{ 0x80, 0x0f, 0xd9, 0x53 },
		{ 0x86, 0x0,  0x0,  0x0  },
		{ 0x1,  0xa0, 0x1,  0x0  },
		{ 0x4b, 0x0,  0x0,  0x0  },
		{ 0x28, 0x0,  0x0,  0x0  },
		{ 0x0,  0x14, 0x2a, 0x14 },
		{ 0x0,  0x0,  0x0,  0x0  },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_hdmi_742p5 = {
	.clock = 74250,
	.config = {
		0x84,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x4,  0x15, 0x26, 0xa0 },
		{ 0x60, 0x0,  0x0,  0x0  },
		{ 0x8,  0x4,  0x88, 0x28 },
		{ 0xfa, 0x0c, 0x84, 0x11 },
		{ 0x80, 0x0f, 0xd9, 0x53 },
		{ 0x86, 0x0,  0x0,  0x0  },
		{ 0x1,  0xa0, 0x1,  0x0  },
		{ 0x4b, 0x0,  0x0,  0x0  },
		{ 0x28, 0x0,  0x0,  0x0  },
		{ 0x0,  0x14, 0x2a, 0x14 },
		{ 0x0,  0x0,  0x0,  0x0  },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_hdmi_1p485 = {
	.clock = 148500,
	.config = {
		0x84,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x2,  0x15, 0x26, 0xa0 },
		{ 0x60, 0x0,  0x0,  0x0  },
		{ 0x8,  0x4,  0x84, 0x28 },
		{ 0xfa, 0x0c, 0x84, 0x11 },
		{ 0x80, 0x0f, 0xd9, 0x53 },
		{ 0x86, 0x0,  0x0,  0x0  },
		{ 0x1,  0xa0, 0x1,  0x0  },
		{ 0x4b, 0x0,  0x0,  0x0  },
		{ 0x28, 0x0,  0x0,  0x0  },
		{ 0x0,  0x14, 0x2a, 0x14 },
		{ 0x0,  0x0,  0x0,  0x0  },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state xe3plpd_lt_hdmi_5p94 = {
	.clock = 594000,
	.config = {
		0x84,
		0x2d,
		0x0,
	},
	.addr_msb = {
		0x87,
		0x87,
		0x87,
		0x87,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
		0x88,
	},
	.addr_lsb = {
		0x10,
		0x0c,
		0x14,
		0xe4,
		0x0c,
		0x10,
		0x14,
		0x18,
		0x48,
		0x40,
		0x4c,
		0x24,
		0x44,
	},
	.data = {
		{ 0x0,  0x4c, 0x2,  0x0  },
		{ 0x0,  0x95, 0x26, 0xa0 },
		{ 0x60, 0x0,  0x0,  0x0  },
		{ 0x8,  0x4,  0x81, 0x28 },
		{ 0xfa, 0x0c, 0x84, 0x11 },
		{ 0x80, 0x0f, 0xd9, 0x53 },
		{ 0x86, 0x0,  0x0,  0x0  },
		{ 0x1,  0xa0, 0x1,  0x0  },
		{ 0x4b, 0x0,  0x0,  0x0  },
		{ 0x28, 0x0,  0x0,  0x0  },
		{ 0x0,  0x14, 0x2a, 0x14 },
		{ 0x0,  0x0,  0x0,  0x0  },
		{ 0x0,  0x0,  0x0,  0x0  },
	},
};

static const struct intel_lt_phy_pll_state * const xe3plpd_lt_hdmi_tables[] = {
	&xe3plpd_lt_hdmi_252,
	&xe3plpd_lt_hdmi_272,
	&xe3plpd_lt_hdmi_742p5,
	&xe3plpd_lt_hdmi_1p485,
	&xe3plpd_lt_hdmi_5p94,
	NULL,
};

static u8 intel_lt_phy_get_owned_lane_mask(struct intel_encoder *encoder)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	if (!intel_tc_port_in_dp_alt_mode(dig_port))
		return INTEL_LT_PHY_BOTH_LANES;

	return intel_tc_port_max_lane_count(dig_port) > 2
		? INTEL_LT_PHY_BOTH_LANES : INTEL_LT_PHY_LANE0;
}

static u8 intel_lt_phy_read(struct intel_encoder *encoder, u8 lane_mask, u16 addr)
{
	return intel_cx0_read(encoder, lane_mask, addr);
}

static void intel_lt_phy_write(struct intel_encoder *encoder,
			       u8 lane_mask, u16 addr, u8 data, bool committed)
{
	intel_cx0_write(encoder, lane_mask, addr, data, committed);
}

static void intel_lt_phy_rmw(struct intel_encoder *encoder,
			     u8 lane_mask, u16 addr, u8 clear, u8 set, bool committed)
{
	intel_cx0_rmw(encoder, lane_mask, addr, clear, set, committed);
}

static void intel_lt_phy_clear_status_p2p(struct intel_encoder *encoder,
					  int lane)
{
	struct intel_display *display = to_intel_display(encoder);

	intel_de_rmw(display,
		     XE3PLPD_PORT_P2M_MSGBUS_STATUS_P2P(encoder->port, lane),
		     XELPDP_PORT_P2M_RESPONSE_READY, 0);
}

static void
assert_dc_off(struct intel_display *display)
{
	bool enabled;

	enabled = intel_display_power_is_enabled(display, POWER_DOMAIN_DC_OFF);
	drm_WARN_ON(display->drm, !enabled);
}

static int __intel_lt_phy_p2p_write_once(struct intel_encoder *encoder,
					 int lane, u16 addr, u8 data,
					 i915_reg_t mac_reg_addr,
					 u8 expected_mac_val)
{
	struct intel_display *display = to_intel_display(encoder);
	enum port port = encoder->port;
	enum phy phy = intel_encoder_to_phy(encoder);
	int ack;
	u32 val;

	if (intel_de_wait_for_clear_ms(display, XELPDP_PORT_M2P_MSGBUS_CTL(display, port, lane),
				       XELPDP_PORT_P2P_TRANSACTION_PENDING,
				       XELPDP_MSGBUS_TIMEOUT_MS)) {
		drm_dbg_kms(display->drm,
			    "PHY %c Timeout waiting for previous transaction to complete. Resetting bus.\n",
			    phy_name(phy));
		intel_cx0_bus_reset(encoder, lane);
		return -ETIMEDOUT;
	}

	intel_de_rmw(display, XELPDP_PORT_P2M_MSGBUS_STATUS(display, port, lane), 0, 0);

	intel_de_write(display, XELPDP_PORT_M2P_MSGBUS_CTL(display, port, lane),
		       XELPDP_PORT_P2P_TRANSACTION_PENDING |
		       XELPDP_PORT_M2P_COMMAND_WRITE_COMMITTED |
		       XELPDP_PORT_M2P_DATA(data) |
		       XELPDP_PORT_M2P_ADDRESS(addr));

	ack = intel_cx0_wait_for_ack(encoder, XELPDP_PORT_P2M_COMMAND_WRITE_ACK, lane, &val);
	if (ack < 0)
		return ack;

	if (val & XELPDP_PORT_P2M_ERROR_SET) {
		drm_dbg_kms(display->drm,
			    "PHY %c Error occurred during P2P write command. Status: 0x%x\n",
			    phy_name(phy), val);
		intel_lt_phy_clear_status_p2p(encoder, lane);
		intel_cx0_bus_reset(encoder, lane);
		return -EINVAL;
	}

	/*
	 * RE-VISIT:
	 * This needs to be added to give PHY time to set everything up this was a requirement
	 * to get the display up and running
	 * This is the time PHY takes to settle down after programming the PHY.
	 */
	udelay(150);
	intel_clear_response_ready_flag(encoder, lane);
	intel_lt_phy_clear_status_p2p(encoder, lane);

	return 0;
}

static void __intel_lt_phy_p2p_write(struct intel_encoder *encoder,
				     int lane, u16 addr, u8 data,
				     i915_reg_t mac_reg_addr,
				     u8 expected_mac_val)
{
	struct intel_display *display = to_intel_display(encoder);
	enum phy phy = intel_encoder_to_phy(encoder);
	int i, status;

	assert_dc_off(display);

	/* 3 tries is assumed to be enough to write successfully */
	for (i = 0; i < 3; i++) {
		status = __intel_lt_phy_p2p_write_once(encoder, lane, addr, data, mac_reg_addr,
						       expected_mac_val);

		if (status == 0)
			return;
	}

	drm_err_once(display->drm,
		     "PHY %c P2P Write %04x failed after %d retries.\n", phy_name(phy), addr, i);
}

static void intel_lt_phy_p2p_write(struct intel_encoder *encoder,
				   u8 lane_mask, u16 addr, u8 data,
				   i915_reg_t mac_reg_addr,
				   u8 expected_mac_val)
{
	int lane;

	for_each_lt_phy_lane_in_mask(lane_mask, lane)
		__intel_lt_phy_p2p_write(encoder, lane, addr, data, mac_reg_addr, expected_mac_val);
}

static void
intel_lt_phy_setup_powerdown(struct intel_encoder *encoder, u8 lane_count)
{
	/*
	 * The new PORT_BUF_CTL6 stuff for dc5 entry and exit needs to be handled
	 * by dmc firmware not explicitly mentioned in Bspec. This leaves this
	 * function as a wrapper only but keeping it expecting future changes.
	 */
	intel_cx0_setup_powerdown(encoder);
}

static void
intel_lt_phy_powerdown_change_sequence(struct intel_encoder *encoder,
				       u8 lane_mask, u8 state)
{
	intel_cx0_powerdown_change_sequence(encoder, lane_mask, state);
}

static void
intel_lt_phy_lane_reset(struct intel_encoder *encoder,
			u8 lane_count)
{
	struct intel_display *display = to_intel_display(encoder);
	enum port port = encoder->port;
	enum phy phy = intel_encoder_to_phy(encoder);
	u8 owned_lane_mask = intel_lt_phy_get_owned_lane_mask(encoder);
	u32 lane_pipe_reset = owned_lane_mask == INTEL_LT_PHY_BOTH_LANES
				? XELPDP_LANE_PIPE_RESET(0) | XELPDP_LANE_PIPE_RESET(1)
				: XELPDP_LANE_PIPE_RESET(0);
	u32 lane_phy_current_status = owned_lane_mask == INTEL_LT_PHY_BOTH_LANES
					? (XELPDP_LANE_PHY_CURRENT_STATUS(0) |
					   XELPDP_LANE_PHY_CURRENT_STATUS(1))
					: XELPDP_LANE_PHY_CURRENT_STATUS(0);
	u32 lane_phy_pulse_status = owned_lane_mask == INTEL_LT_PHY_BOTH_LANES
					? (XE3PLPDP_LANE_PHY_PULSE_STATUS(0) |
					   XE3PLPDP_LANE_PHY_PULSE_STATUS(1))
					: XE3PLPDP_LANE_PHY_PULSE_STATUS(0);

	intel_de_rmw(display, XE3PLPD_PORT_BUF_CTL5(port),
		     XE3PLPD_MACCLK_RATE_MASK, XE3PLPD_MACCLK_RATE_DEF);

	intel_de_rmw(display, XELPDP_PORT_BUF_CTL1(display, port),
		     XE3PLPDP_PHY_MODE_MASK, XE3PLPDP_PHY_MODE_DP);

	intel_lt_phy_setup_powerdown(encoder, lane_count);
	intel_lt_phy_powerdown_change_sequence(encoder, owned_lane_mask,
					       XELPDP_P2_STATE_RESET);

	intel_de_rmw(display, XE3PLPD_PORT_BUF_CTL5(port),
		     XE3PLPD_MACCLK_RESET_0, 0);

	intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, port),
		     XELPDP_LANE_PCLK_PLL_REQUEST(0),
		     XELPDP_LANE_PCLK_PLL_REQUEST(0));

	if (intel_de_wait_for_set_ms(display, XELPDP_PORT_CLOCK_CTL(display, port),
				     XELPDP_LANE_PCLK_PLL_ACK(0),
				     XE3PLPD_MACCLK_TURNON_LATENCY_MS))
		drm_warn(display->drm, "PHY %c PLL MacCLK assertion ack not done\n",
			 phy_name(phy));

	intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, port),
		     XELPDP_FORWARD_CLOCK_UNGATE,
		     XELPDP_FORWARD_CLOCK_UNGATE);

	intel_de_rmw(display, XELPDP_PORT_BUF_CTL2(display, port),
		     lane_pipe_reset | lane_phy_pulse_status, 0);

	if (intel_de_wait_for_clear_ms(display, XELPDP_PORT_BUF_CTL2(display, port),
				       lane_phy_current_status,
				       XE3PLPD_RESET_END_LATENCY_MS))
		drm_warn(display->drm, "PHY %c failed to bring out of lane reset\n",
			 phy_name(phy));

	if (intel_de_wait_for_set_ms(display, XELPDP_PORT_BUF_CTL2(display, port),
				     lane_phy_pulse_status,
				     XE3PLPD_RATE_CALIB_DONE_LATENCY_MS))
		drm_warn(display->drm, "PHY %c PLL rate not changed\n",
			 phy_name(phy));

	intel_de_rmw(display, XELPDP_PORT_BUF_CTL2(display, port), lane_phy_pulse_status, 0);
}

static void
intel_lt_phy_program_port_clock_ctl(struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state,
				    bool lane_reversal)
{
	struct intel_display *display = to_intel_display(encoder);
	u32 val = 0;

	intel_de_rmw(display, XELPDP_PORT_BUF_CTL1(display, encoder->port),
		     XELPDP_PORT_REVERSAL,
		     lane_reversal ? XELPDP_PORT_REVERSAL : 0);

	val |= XELPDP_FORWARD_CLOCK_UNGATE;

	/*
	 * We actually mean MACCLK here and not MAXPCLK when using LT Phy
	 * but since the register bits still remain the same we use
	 * the same definition
	 */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI) &&
	    intel_hdmi_is_frl(crtc_state->port_clock))
		val |= XELPDP_DDI_CLOCK_SELECT_PREP(display, XELPDP_DDI_CLOCK_SELECT_DIV18CLK);
	else
		val |= XELPDP_DDI_CLOCK_SELECT_PREP(display, XELPDP_DDI_CLOCK_SELECT_MAXPCLK);

	 /* DP2.0 10G and 20G rates enable MPLLA*/
	if (crtc_state->port_clock == 1000000 || crtc_state->port_clock == 2000000)
		val |= XELPDP_SSC_ENABLE_PLLA;
	else
		val |= crtc_state->dpll_hw_state.ltpll.ssc_enabled ? XELPDP_SSC_ENABLE_PLLB : 0;

	intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, encoder->port),
		     XELPDP_LANE1_PHY_CLOCK_SELECT | XELPDP_FORWARD_CLOCK_UNGATE |
		     XELPDP_DDI_CLOCK_SELECT_MASK(display) | XELPDP_SSC_ENABLE_PLLA |
		     XELPDP_SSC_ENABLE_PLLB, val);
}

static u32 intel_lt_phy_get_dp_clock(u8 rate)
{
	switch (rate) {
	case 0:
		return 162000;
	case 1:
		return 270000;
	case 2:
		return 540000;
	case 3:
		return 810000;
	case 4:
		return 216000;
	case 5:
		return 243000;
	case 6:
		return 324000;
	case 7:
		return 432000;
	case 8:
		return 1000000;
	case 9:
		return 1350000;
	case 10:
		return 2000000;
	case 11:
		return 675000;
	default:
		MISSING_CASE(rate);
		return 0;
	}
}

static bool
intel_lt_phy_config_changed(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state)
{
	u8 val, rate;
	u32 clock;

	val = intel_lt_phy_read(encoder, INTEL_LT_PHY_LANE0,
				LT_PHY_VDR_0_CONFIG);
	rate = REG_FIELD_GET8(LT_PHY_VDR_RATE_ENCODING_MASK, val);

	/*
	 * The only time we do not reconfigure the PLL is when we are
	 * using 1.62 Gbps clock since PHY PLL defaults to that
	 * otherwise we always need to reconfigure it.
	 */
	if (intel_crtc_has_dp_encoder(crtc_state)) {
		clock = intel_lt_phy_get_dp_clock(rate);
		if (crtc_state->port_clock == 1620000 && crtc_state->port_clock == clock)
			return false;
	}

	return true;
}

static struct ref_tracker *intel_lt_phy_transaction_begin(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct ref_tracker *wakeref;

	intel_psr_pause(intel_dp);
	wakeref = intel_display_power_get(display, POWER_DOMAIN_DC_OFF);

	return wakeref;
}

static void intel_lt_phy_transaction_end(struct intel_encoder *encoder, struct ref_tracker *wakeref)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_psr_resume(intel_dp);
	intel_display_power_put(display, POWER_DOMAIN_DC_OFF, wakeref);
}

static const struct intel_lt_phy_pll_state * const *
intel_lt_phy_pll_tables_get(struct intel_crtc_state *crtc_state,
			    struct intel_encoder *encoder)
{
	if (intel_crtc_has_dp_encoder(crtc_state)) {
		if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
			return xe3plpd_lt_edp_tables;

		return xe3plpd_lt_dp_tables;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		return xe3plpd_lt_hdmi_tables;
	}

	MISSING_CASE(encoder->type);
	return NULL;
}

static bool
intel_lt_phy_pll_is_ssc_enabled(struct intel_crtc_state *crtc_state,
				struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);

	if (intel_crtc_has_dp_encoder(crtc_state)) {
		if (intel_panel_use_ssc(display)) {
			struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

			return (intel_dp->dpcd[DP_MAX_DOWNSPREAD] & DP_MAX_DOWNSPREAD_0_5);
		}
	}

	return false;
}

static u64 mul_q32_u32(u64 a_q32, u32 b)
{
	u64 p0, p1, carry, result;
	u64 x_hi = a_q32 >> 32;
	u64 x_lo = a_q32 & 0xFFFFFFFFULL;

	p0 = x_lo * (u64)b;
	p1 = x_hi * (u64)b;
	carry = p0 >> 32;
	result = (p1 << 32) + (carry << 32) + (p0 & 0xFFFFFFFFULL);

	return result;
}

static bool
calculate_target_dco_and_loop_cnt(u32 frequency_khz, u64 *target_dco_mhz, u32 *loop_cnt)
{
	u32 ppm_value = 1;
	u32 dco_min_freq = DCO_MIN_FREQ_MHZ;
	u32 dco_max_freq = 16200;
	u32 dco_min_freq_low = 10000;
	u32 dco_max_freq_low = 12000;
	u64 val = 0;
	u64 refclk_khz = REF_CLK_KHZ;
	u64 m2div = 0;
	u64 val_with_frac = 0;
	u64 ppm = 0;
	u64 temp0 = 0, temp1, scale;
	int ppm_cnt, dco_count, y;

	for (ppm_cnt = 0; ppm_cnt < 5; ppm_cnt++) {
		ppm_value = ppm_cnt == 2 ? 2 : 1;
		for (dco_count = 0; dco_count < 2; dco_count++) {
			if (dco_count == 1) {
				dco_min_freq = dco_min_freq_low;
				dco_max_freq = dco_max_freq_low;
			}
			for (y = 2; y <= 255; y += 2) {
				val = div64_u64((u64)y * frequency_khz, 200);
				m2div = div64_u64(((u64)(val) << 32), refclk_khz);
				m2div = mul_q32_u32(m2div, 500);
				val_with_frac = mul_q32_u32(m2div, refclk_khz);
				val_with_frac = div64_u64(val_with_frac, 500);
				temp1 = Q32_TO_INT(val_with_frac);
				temp0 = (temp1 > val) ? (temp1 - val) :
					(val - temp1);
				ppm = div64_u64(temp0, val);
				if (temp1 >= dco_min_freq &&
				    temp1 <= dco_max_freq &&
				    ppm < ppm_value) {
					/* Round to two places */
					scale = (1ULL << 32) / 100;
					temp0 = DIV_ROUND_UP_ULL(val_with_frac,
								 scale);
					*target_dco_mhz = temp0 * scale;
					*loop_cnt = y;
					return true;
				}
			}
		}
	}

	return false;
}

static void set_phy_vdr_addresses(struct lt_phy_params *p, int pll_type)
{
	p->pll_reg4.addr = PLL_REG_ADDR(PLL_REG4_ADDR, pll_type);
	p->pll_reg3.addr = PLL_REG_ADDR(PLL_REG3_ADDR, pll_type);
	p->pll_reg5.addr = PLL_REG_ADDR(PLL_REG5_ADDR, pll_type);
	p->pll_reg57.addr = PLL_REG_ADDR(PLL_REG57_ADDR, pll_type);
	p->lf.addr = PLL_REG_ADDR(PLL_LF_ADDR, pll_type);
	p->tdc.addr = PLL_REG_ADDR(PLL_TDC_ADDR, pll_type);
	p->ssc.addr = PLL_REG_ADDR(PLL_SSC_ADDR, pll_type);
	p->bias2.addr = PLL_REG_ADDR(PLL_BIAS2_ADDR, pll_type);
	p->bias_trim.addr = PLL_REG_ADDR(PLL_BIAS_TRIM_ADDR, pll_type);
	p->dco_med.addr = PLL_REG_ADDR(PLL_DCO_MED_ADDR, pll_type);
	p->dco_fine.addr = PLL_REG_ADDR(PLL_DCO_FINE_ADDR, pll_type);
	p->ssc_inj.addr = PLL_REG_ADDR(PLL_SSC_INJ_ADDR, pll_type);
	p->surv_bonus.addr = PLL_REG_ADDR(PLL_SURV_BONUS_ADDR, pll_type);
}

static void compute_ssc(struct lt_phy_params *p, u32 ana_cfg)
{
	int ssc_stepsize = 0;
	int ssc_steplen = 0;
	int ssc_steplog = 0;

	p->ssc.val = (1 << 31) | (ana_cfg << 24) | (ssc_steplog << 16) |
		(ssc_stepsize << 8) | ssc_steplen;
}

static void compute_bias2(struct lt_phy_params *p)
{
	u32 ssc_en_local = 0;
	u64 dynctrl_ovrd_en = 0;

	p->bias2.val = (dynctrl_ovrd_en << 31) | (ssc_en_local << 30) |
		(1 << 23) | (1 << 24) | (32 << 16) | (1 << 8);
}

static void compute_tdc(struct lt_phy_params *p, u64 tdc_fine)
{
	u32 settling_time = 15;
	u32 bias_ovr_en = 1;
	u32 coldstart = 1;
	u32 true_lock = 2;
	u32 early_lock = 1;
	u32 lock_ovr_en = 1;
	u32 lock_thr = tdc_fine ? 3 : 5;
	u32 unlock_thr = tdc_fine ? 5 : 11;

	p->tdc.val = (u32)((2 << 30) + (settling_time << 16) + (bias_ovr_en << 15) +
		    (lock_ovr_en << 14) + (coldstart << 12) + (true_lock << 10) +
		    (early_lock << 8) + (unlock_thr << 4) + lock_thr);
}

static void compute_dco_med(struct lt_phy_params *p)
{
	u32 cselmed_en = 0;
	u32 cselmed_dyn_adj = 0;
	u32 cselmed_ratio = 39;
	u32 cselmed_thr = 8;

	p->dco_med.val = (cselmed_en << 31) + (cselmed_dyn_adj << 30) +
		(cselmed_ratio << 24) + (cselmed_thr << 21);
}

static void compute_dco_fine(struct lt_phy_params *p, u32 dco_12g)
{
	u32 dco_fine0_tune_2_0 = 0;
	u32 dco_fine1_tune_2_0 = 0;
	u32 dco_fine2_tune_2_0 = 0;
	u32 dco_fine3_tune_2_0 = 0;
	u32 dco_dith0_tune_2_0 = 0;
	u32 dco_dith1_tune_2_0 = 0;

	dco_fine0_tune_2_0 = dco_12g ? 4 : 3;
	dco_fine1_tune_2_0 = 2;
	dco_fine2_tune_2_0 = dco_12g ? 2 : 1;
	dco_fine3_tune_2_0 = 5;
	dco_dith0_tune_2_0 = dco_12g ? 4 : 3;
	dco_dith1_tune_2_0 = 2;

	p->dco_fine.val = (dco_dith1_tune_2_0 << 19) +
		(dco_dith0_tune_2_0 << 16) +
		(dco_fine3_tune_2_0 << 11) +
		(dco_fine2_tune_2_0 << 8) +
		(dco_fine1_tune_2_0 << 3) +
		dco_fine0_tune_2_0;
}

int
intel_lt_phy_calculate_hdmi_state(struct intel_lt_phy_pll_state *lt_state,
				  u32 frequency_khz)
{
#define DATA_ASSIGN(i, pll_reg)	\
	do {			\
		lt_state->data[i][0] = (u8)((((pll_reg).val) & 0xFF000000) >> 24); \
		lt_state->data[i][1] = (u8)((((pll_reg).val) & 0x00FF0000) >> 16); \
		lt_state->data[i][2] = (u8)((((pll_reg).val) & 0x0000FF00) >> 8); \
		lt_state->data[i][3] = (u8)((((pll_reg).val) & 0x000000FF));	\
	} while (0)
#define ADDR_ASSIGN(i, pll_reg)	\
	do {			\
		lt_state->addr_msb[i] = ((pll_reg).addr >> 8) & 0xFF;	\
		lt_state->addr_lsb[i] = (pll_reg).addr & 0xFF;		\
	} while (0)

	bool found = false;
	struct lt_phy_params p;
	u32 dco_fmin = DCO_MIN_FREQ_MHZ;
	u64 refclk_khz = REF_CLK_KHZ;
	u32 refclk_mhz_int = REF_CLK_KHZ / 1000;
	u64 m2div = 0;
	u64 target_dco_mhz = 0;
	u64 tdc_fine, tdc_targetcnt;
	u64 feedfwd_gain ,feedfwd_cal_en;
	u64 tdc_res = 30;
	u32 prop_coeff;
	u32 int_coeff;
	u32 ndiv = 1;
	u32 m1div = 1, m2div_int, m2div_frac;
	u32 frac_en;
	u32 ana_cfg;
	u32 loop_cnt = 0;
	u32 gain_ctrl = 2;
	u32 postdiv = 0;
	u32 dco_12g = 0;
	u32 pll_type = 0;
	u32 d1 = 2, d3 = 5, d4 = 0, d5 = 0;
	u32 d6 = 0, d6_new = 0;
	u32 d7, d8 = 0;
	u32 bonus_7_0 = 0;
	u32 csel2fo = 11;
	u32 csel2fo_ovrd_en = 1;
	u64 temp0, temp1, temp2, temp3;

	p.surv_bonus.val = (bonus_7_0 << 16);
	p.pll_reg4.val = (refclk_mhz_int << 17) +
		(ndiv << 9) + (1 << 4);
	p.bias_trim.val = (csel2fo_ovrd_en << 30) + (csel2fo << 24);
	p.ssc_inj.val = 0;
	found = calculate_target_dco_and_loop_cnt(frequency_khz, &target_dco_mhz, &loop_cnt);
	if (!found)
		return -EINVAL;

	m2div = div64_u64(target_dco_mhz, (refclk_khz * ndiv * m1div));
	m2div = mul_q32_u32(m2div, 1000);
	if (Q32_TO_INT(m2div) > 511)
		return -EINVAL;

	m2div_int = (u32)Q32_TO_INT(m2div);
	m2div_frac = (u32)(Q32_TO_FRAC(m2div));
	frac_en = (m2div_frac > 0) ? 1 : 0;

	if (frac_en > 0)
		tdc_res = 70;
	else
		tdc_res = 36;
	tdc_fine = tdc_res > 50 ? 1 : 0;
	temp0 = tdc_res * 40 * 11;
	temp1 = div64_u64(((4 * TDC_RES_MULTIPLIER) + temp0) * 500, temp0 * refclk_khz);
	temp2 = div64_u64(temp0 * refclk_khz, 1000);
	temp3 = div64_u64(((8 * TDC_RES_MULTIPLIER) + temp2), temp2);
	tdc_targetcnt = tdc_res < 50 ? (int)(temp1) : (int)(temp3);
	tdc_targetcnt = (int)(tdc_targetcnt / 2);
	temp0 = mul_q32_u32(target_dco_mhz, tdc_res);
	temp0 >>= 32;
	feedfwd_gain = (m2div_frac > 0) ? div64_u64(m1div * TDC_RES_MULTIPLIER, temp0) : 0;
	feedfwd_cal_en = frac_en;

	temp0 = (u32)Q32_TO_INT(target_dco_mhz);
	prop_coeff = (temp0 >= dco_fmin) ? 3 : 4;
	int_coeff = (temp0 >= dco_fmin) ? 7 : 8;
	ana_cfg = (temp0 >= dco_fmin) ? 8 : 6;
	dco_12g = (temp0 >= dco_fmin) ? 0 : 1;

	if (temp0 > 12960)
		d7 = 10;
	else
		d7 = 8;

	d8 = loop_cnt / 2;
	d4 = d8 * 2;

	/* Compute pll_reg3,5,57 & lf */
	p.pll_reg3.val = (u32)((d4 << 21) + (d3 << 18) + (d1 << 15) + (m2div_int << 5));
	p.pll_reg5.val = m2div_frac;
	postdiv = (d5 == 0) ? 9 : d5;
	d6_new = (d6 == 0) ? 40 : d6;
	p.pll_reg57.val = (d7 << 24) + (postdiv << 15) + (d8 << 7) + d6_new;
	p.lf.val = (u32)((frac_en << 31) + (1 << 30) + (frac_en << 29) +
		   (feedfwd_cal_en << 28) + (tdc_fine << 27) +
		   (gain_ctrl << 24) + (feedfwd_gain << 16) +
		   (int_coeff << 12) + (prop_coeff << 8) + tdc_targetcnt);

	compute_ssc(&p, ana_cfg);
	compute_bias2(&p);
	compute_tdc(&p, tdc_fine);
	compute_dco_med(&p);
	compute_dco_fine(&p, dco_12g);

	pll_type = ((frequency_khz == 10000) || (frequency_khz == 20000) ||
		    (frequency_khz == 2500) || (dco_12g == 1)) ? 0 : 1;
	set_phy_vdr_addresses(&p, pll_type);

	lt_state->config[0] = 0x84;
	lt_state->config[1] = 0x2d;
	ADDR_ASSIGN(0, p.pll_reg4);
	ADDR_ASSIGN(1, p.pll_reg3);
	ADDR_ASSIGN(2, p.pll_reg5);
	ADDR_ASSIGN(3, p.pll_reg57);
	ADDR_ASSIGN(4, p.lf);
	ADDR_ASSIGN(5, p.tdc);
	ADDR_ASSIGN(6, p.ssc);
	ADDR_ASSIGN(7, p.bias2);
	ADDR_ASSIGN(8, p.bias_trim);
	ADDR_ASSIGN(9, p.dco_med);
	ADDR_ASSIGN(10, p.dco_fine);
	ADDR_ASSIGN(11, p.ssc_inj);
	ADDR_ASSIGN(12, p.surv_bonus);
	DATA_ASSIGN(0, p.pll_reg4);
	DATA_ASSIGN(1, p.pll_reg3);
	DATA_ASSIGN(2, p.pll_reg5);
	DATA_ASSIGN(3, p.pll_reg57);
	DATA_ASSIGN(4, p.lf);
	DATA_ASSIGN(5, p.tdc);
	DATA_ASSIGN(6, p.ssc);
	DATA_ASSIGN(7, p.bias2);
	DATA_ASSIGN(8, p.bias_trim);
	DATA_ASSIGN(9, p.dco_med);
	DATA_ASSIGN(10, p.dco_fine);
	DATA_ASSIGN(11, p.ssc_inj);
	DATA_ASSIGN(12, p.surv_bonus);

	return 0;
}

static int
intel_lt_phy_calc_hdmi_port_clock(const struct intel_crtc_state *crtc_state)
{
#define REGVAL(i) (				\
	(lt_state->data[i][3])		|	\
	(lt_state->data[i][2] << 8)	|	\
	(lt_state->data[i][1] << 16)	|	\
	(lt_state->data[i][0] << 24)		\
)

	struct intel_display *display = to_intel_display(crtc_state);
	const struct intel_lt_phy_pll_state *lt_state =
		&crtc_state->dpll_hw_state.ltpll;
	int clk = 0;
	u32 d8, pll_reg_5, pll_reg_3, pll_reg_57, m2div_frac, m2div_int;
	u64 temp0, temp1;
	/*
	 * The algorithm uses '+' to combine bitfields when
	 * constructing PLL_reg3 and PLL_reg57:
	 * PLL_reg57 = (D7 << 24) + (postdiv << 15) + (D8 << 7) + D6_new;
	 * PLL_reg3 = (D4 << 21) + (D3 << 18) + (D1 << 15) + (m2div_int << 5);
	 *
	 * However, this is likely intended to be a bitwise OR operation,
	 * as each field occupies distinct, non-overlapping bits in the register.
	 *
	 * PLL_reg57 is composed of following fields packed into a 32-bit value:
	 * - D7: max value 10 -> fits in 4 bits -> placed at bits 24-27
	 * - postdiv: max value 9 -> fits in 4 bits -> placed at bits 15-18
	 * - D8: derived from loop_cnt / 2, max 127 -> fits in 7 bits
	 *	(though 8 bits are given to it) -> placed at bits 7-14
	 * - D6_new: fits in lower 7 bits -> placed at bits 0-6
	 * PLL_reg57 = (D7 << 24) | (postdiv << 15) | (D8 << 7) | D6_new;
	 *
	 * Similarly, PLL_reg3 is packed as:
	 * - D4: max value 256 -> fits in 9 bits -> placed at bits 21-29
	 * - D3: max value 9 -> fits in 4 bits -> placed at bits 18-21
	 * - D1: max value 2 -> fits in 2 bits -> placed at bits 15-16
	 * - m2div_int: max value 511 -> fits in 9 bits (10 bits allocated)
	 *   -> placed at bits 5-14
	 * PLL_reg3 = (D4 << 21) | (D3 << 18) | (D1 << 15) | (m2div_int << 5);
	 */
	pll_reg_5 = REGVAL(2);
	pll_reg_3 = REGVAL(1);
	pll_reg_57 = REGVAL(3);
	m2div_frac = pll_reg_5;

	/*
	 * From forward algorithm we know
	 * m2div = 2 * m2
	 * val = y * frequency * 5
	 * So now,
	 * frequency = (m2 * 2 * refclk_khz / (d8 * 10))
	 * frequency = (m2div * refclk_khz / (d8 * 10))
	 */
	d8 = (pll_reg_57 & REG_GENMASK(14, 7)) >> 7;
	if (d8 == 0) {
		drm_WARN_ON(display->drm,
			    "Invalid port clock using lowest HDMI portclock\n");
		return xe3plpd_lt_hdmi_252.clock;
	}
	m2div_int = (pll_reg_3  & REG_GENMASK(14, 5)) >> 5;
	temp0 = ((u64)m2div_frac * REF_CLK_KHZ) >> 32;
	temp1 = (u64)m2div_int * REF_CLK_KHZ;

	clk = div_u64((temp1 + temp0), d8 * 10);

	return clk;
}

int
intel_lt_phy_calc_port_clock(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	int clk;
	const struct intel_lt_phy_pll_state *lt_state =
		&crtc_state->dpll_hw_state.ltpll;
	u8 mode, rate;

	mode = REG_FIELD_GET8(LT_PHY_VDR_MODE_ENCODING_MASK,
			      lt_state->config[0]);
	/*
	 * For edp/dp read the clock value from the tables
	 * and return the clock as the algorithm used for
	 * calculating the port clock does not exactly matches
	 * with edp/dp clock.
	 */
	if (mode == MODE_DP) {
		rate = REG_FIELD_GET8(LT_PHY_VDR_RATE_ENCODING_MASK,
				      lt_state->config[0]);
		clk = intel_lt_phy_get_dp_clock(rate);
	} else if (mode == MODE_HDMI_20) {
		clk = intel_lt_phy_calc_hdmi_port_clock(crtc_state);
	} else {
		drm_WARN_ON(display->drm, "Unsupported LT PHY Mode!\n");
		clk = xe3plpd_lt_hdmi_252.clock;
	}

	return clk;
}

int
intel_lt_phy_pll_calc_state(struct intel_crtc_state *crtc_state,
			    struct intel_encoder *encoder)
{
	const struct intel_lt_phy_pll_state * const *tables;
	int i;

	tables = intel_lt_phy_pll_tables_get(crtc_state, encoder);
	if (!tables)
		return -EINVAL;

	for (i = 0; tables[i]; i++) {
		if (crtc_state->port_clock == tables[i]->clock) {
			crtc_state->dpll_hw_state.ltpll = *tables[i];
			if (intel_crtc_has_dp_encoder(crtc_state)) {
				if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
					crtc_state->dpll_hw_state.ltpll.config[2] = 1;
			}
			crtc_state->dpll_hw_state.ltpll.ssc_enabled =
				intel_lt_phy_pll_is_ssc_enabled(crtc_state, encoder);
			return 0;
		}
	}

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		return intel_lt_phy_calculate_hdmi_state(&crtc_state->dpll_hw_state.ltpll,
							 crtc_state->port_clock);
	}

	return -EINVAL;
}

static void
intel_lt_phy_program_pll(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state)
{
	u8 owned_lane_mask = intel_lt_phy_get_owned_lane_mask(encoder);
	int i, j, k;

	intel_lt_phy_write(encoder, owned_lane_mask, LT_PHY_VDR_0_CONFIG,
			   crtc_state->dpll_hw_state.ltpll.config[0], MB_WRITE_COMMITTED);
	intel_lt_phy_write(encoder, INTEL_LT_PHY_LANE0, LT_PHY_VDR_1_CONFIG,
			   crtc_state->dpll_hw_state.ltpll.config[1], MB_WRITE_COMMITTED);
	intel_lt_phy_write(encoder, owned_lane_mask, LT_PHY_VDR_2_CONFIG,
			   crtc_state->dpll_hw_state.ltpll.config[2], MB_WRITE_COMMITTED);

	for (i = 0; i <= 12; i++) {
		intel_lt_phy_write(encoder, INTEL_LT_PHY_LANE0, LT_PHY_VDR_X_ADDR_MSB(i),
				   crtc_state->dpll_hw_state.ltpll.addr_msb[i],
				   MB_WRITE_COMMITTED);
		intel_lt_phy_write(encoder, INTEL_LT_PHY_LANE0, LT_PHY_VDR_X_ADDR_LSB(i),
				   crtc_state->dpll_hw_state.ltpll.addr_lsb[i],
				   MB_WRITE_COMMITTED);

		for (j = 3, k = 0; j >= 0; j--, k++)
			intel_lt_phy_write(encoder, INTEL_LT_PHY_LANE0,
					   LT_PHY_VDR_X_DATAY(i, j),
					   crtc_state->dpll_hw_state.ltpll.data[i][k],
					   MB_WRITE_COMMITTED);
	}
}

static void
intel_lt_phy_enable_disable_tx(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool lane_reversal = dig_port->lane_reversal;
	u8 lane_count = crtc_state->lane_count;
	bool is_dp_alt =
		intel_tc_port_in_dp_alt_mode(dig_port);
	enum intel_tc_pin_assignment tc_pin =
		intel_tc_port_get_pin_assignment(dig_port);
	u8 transmitter_mask = 0;

	/*
	 * We have a two transmitters per lane and total of 2 PHY lanes so a total
	 * of 4 transmitters. We prepare a mask of the lanes that need to be activated
	 * and the transmitter which need to be activated for each lane. TX 0,1 correspond
	 * to LANE0 and TX 2, 3 correspond to LANE1.
	 */

	switch (lane_count) {
	case 1:
		transmitter_mask = lane_reversal ? REG_BIT8(3) : REG_BIT8(0);
		if (is_dp_alt) {
			if (tc_pin == INTEL_TC_PIN_ASSIGNMENT_D)
				transmitter_mask = REG_BIT8(0);
			else
				transmitter_mask = REG_BIT8(1);
		}
		break;
	case 2:
		transmitter_mask = lane_reversal ? REG_GENMASK8(3, 2) : REG_GENMASK8(1, 0);
		if (is_dp_alt)
			transmitter_mask = REG_GENMASK8(1, 0);
		break;
	case 3:
		transmitter_mask = lane_reversal ? REG_GENMASK8(3, 1) : REG_GENMASK8(2, 0);
		if (is_dp_alt)
			transmitter_mask = REG_GENMASK8(2, 0);
		break;
	case 4:
		transmitter_mask = REG_GENMASK8(3, 0);
		break;
	default:
		MISSING_CASE(lane_count);
		transmitter_mask = REG_GENMASK8(3, 0);
		break;
	}

	if (transmitter_mask & BIT(0)) {
		intel_lt_phy_p2p_write(encoder, INTEL_LT_PHY_LANE0, LT_PHY_TXY_CTL10(0),
				       LT_PHY_TX_LANE_ENABLE, LT_PHY_TXY_CTL10_MAC(0),
				       LT_PHY_TX_LANE_ENABLE);
	} else {
		intel_lt_phy_p2p_write(encoder, INTEL_LT_PHY_LANE0, LT_PHY_TXY_CTL10(0),
				       0, LT_PHY_TXY_CTL10_MAC(0), 0);
	}

	if (transmitter_mask & BIT(1)) {
		intel_lt_phy_p2p_write(encoder, INTEL_LT_PHY_LANE0, LT_PHY_TXY_CTL10(1),
				       LT_PHY_TX_LANE_ENABLE, LT_PHY_TXY_CTL10_MAC(1),
				       LT_PHY_TX_LANE_ENABLE);
	} else {
		intel_lt_phy_p2p_write(encoder, INTEL_LT_PHY_LANE0, LT_PHY_TXY_CTL10(1),
				       0, LT_PHY_TXY_CTL10_MAC(1), 0);
	}

	if (transmitter_mask & BIT(2)) {
		intel_lt_phy_p2p_write(encoder, INTEL_LT_PHY_LANE1, LT_PHY_TXY_CTL10(0),
				       LT_PHY_TX_LANE_ENABLE, LT_PHY_TXY_CTL10_MAC(0),
				       LT_PHY_TX_LANE_ENABLE);
	} else {
		intel_lt_phy_p2p_write(encoder, INTEL_LT_PHY_LANE1, LT_PHY_TXY_CTL10(0),
				       0, LT_PHY_TXY_CTL10_MAC(0), 0);
	}

	if (transmitter_mask & BIT(3)) {
		intel_lt_phy_p2p_write(encoder, INTEL_LT_PHY_LANE1, LT_PHY_TXY_CTL10(1),
				       LT_PHY_TX_LANE_ENABLE, LT_PHY_TXY_CTL10_MAC(1),
				       LT_PHY_TX_LANE_ENABLE);
	} else {
		intel_lt_phy_p2p_write(encoder, INTEL_LT_PHY_LANE1, LT_PHY_TXY_CTL10(1),
				       0, LT_PHY_TXY_CTL10_MAC(1), 0);
	}
}

void intel_lt_phy_pll_enable(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool lane_reversal = dig_port->lane_reversal;
	u8 owned_lane_mask = intel_lt_phy_get_owned_lane_mask(encoder);
	enum phy phy = intel_encoder_to_phy(encoder);
	enum port port = encoder->port;
	struct ref_tracker *wakeref = 0;
	u32 lane_phy_pulse_status = owned_lane_mask == INTEL_LT_PHY_BOTH_LANES
					? (XE3PLPDP_LANE_PHY_PULSE_STATUS(0) |
					   XE3PLPDP_LANE_PHY_PULSE_STATUS(1))
					: XE3PLPDP_LANE_PHY_PULSE_STATUS(0);
	u8 rate_update;

	wakeref = intel_lt_phy_transaction_begin(encoder);

	/* 1. Enable MacCLK at default 162 MHz frequency. */
	intel_lt_phy_lane_reset(encoder, crtc_state->lane_count);

	/* 2. Program PORT_CLOCK_CTL register to configure clock muxes, gating, and SSC. */
	intel_lt_phy_program_port_clock_ctl(encoder, crtc_state, lane_reversal);

	/* 3. Change owned PHY lanes power to Ready state. */
	intel_lt_phy_powerdown_change_sequence(encoder, owned_lane_mask,
					       XELPDP_P2_STATE_READY);

	/*
	 * 4. Read the PHY message bus VDR register PHY_VDR_0_Config check enabled PLL type,
	 * encoded rate and encoded mode.
	 */
	if (intel_lt_phy_config_changed(encoder, crtc_state)) {
		/*
		 * 5. Program the PHY internal PLL registers over PHY message bus for the desired
		 * frequency and protocol type
		 */
		intel_lt_phy_program_pll(encoder, crtc_state);

		/* 6. Use the P2P transaction flow */
		/*
		 * 6.1. Set the PHY VDR register 0xCC4[Rate Control VDR Update] = 1 over PHY message
		 * bus for Owned PHY Lanes.
		 */
		/*
		 * 6.2. Poll for P2P Transaction Ready = "1" and read the MAC message bus VDR
		 * register at offset 0xC00 for Owned PHY Lanes*.
		 */
		/* 6.3. Clear P2P transaction Ready bit. */
		intel_lt_phy_p2p_write(encoder, owned_lane_mask, LT_PHY_RATE_UPDATE,
				       LT_PHY_RATE_CONTROL_VDR_UPDATE, LT_PHY_MAC_VDR,
				       LT_PHY_PCLKIN_GATE);

		/* 7. Program PORT_CLOCK_CTL[PCLK PLL Request LN0] = 0. */
		intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, port),
			     XELPDP_LANE_PCLK_PLL_REQUEST(0), 0);

		/* 8. Poll for PORT_CLOCK_CTL[PCLK PLL Ack LN0]= 0. */
		if (intel_de_wait_for_clear_us(display, XELPDP_PORT_CLOCK_CTL(display, port),
					       XELPDP_LANE_PCLK_PLL_ACK(0),
					       XE3PLPD_MACCLK_TURNOFF_LATENCY_US))
			drm_warn(display->drm, "PHY %c PLL MacCLK ack deassertion timeout\n",
				 phy_name(phy));

		/*
		 * 9. Follow the Display Voltage Frequency Switching - Sequence Before Frequency
		 * Change. We handle this step in bxt_set_cdclk().
		 */
		/* 10. Program DDI_CLK_VALFREQ to match intended DDI clock frequency. */
		intel_de_write(display, DDI_CLK_VALFREQ(encoder->port),
			       crtc_state->port_clock);

		/* 11. Program PORT_CLOCK_CTL[PCLK PLL Request LN0] = 1. */
		intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, port),
			     XELPDP_LANE_PCLK_PLL_REQUEST(0),
			     XELPDP_LANE_PCLK_PLL_REQUEST(0));

		/* 12. Poll for PORT_CLOCK_CTL[PCLK PLL Ack LN0]= 1. */
		if (intel_de_wait_for_set_ms(display, XELPDP_PORT_CLOCK_CTL(display, port),
					     XELPDP_LANE_PCLK_PLL_ACK(0),
					     XE3PLPD_MACCLK_TURNON_LATENCY_MS))
			drm_warn(display->drm, "PHY %c PLL MacCLK ack assertion timeout\n",
				 phy_name(phy));

		/*
		 * 13. Ungate the forward clock by setting
		 * PORT_CLOCK_CTL[Forward Clock Ungate] = 1.
		 */
		intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, port),
			     XELPDP_FORWARD_CLOCK_UNGATE,
			     XELPDP_FORWARD_CLOCK_UNGATE);

		/* 14. SW clears PORT_BUF_CTL2 [PHY Pulse Status]. */
		intel_de_rmw(display, XELPDP_PORT_BUF_CTL2(display, port),
			     lane_phy_pulse_status,
			     lane_phy_pulse_status);
		/*
		 * 15. Clear the PHY VDR register 0xCC4[Rate Control VDR Update] over
		 * PHY message bus for Owned PHY Lanes.
		 */
		rate_update = intel_lt_phy_read(encoder, INTEL_LT_PHY_LANE0, LT_PHY_RATE_UPDATE);
		rate_update &= ~LT_PHY_RATE_CONTROL_VDR_UPDATE;
		intel_lt_phy_write(encoder, owned_lane_mask, LT_PHY_RATE_UPDATE,
				   rate_update, MB_WRITE_COMMITTED);

		/* 16. Poll for PORT_BUF_CTL2 register PHY Pulse Status = 1 for Owned PHY Lanes. */
		if (intel_de_wait_for_set_ms(display, XELPDP_PORT_BUF_CTL2(display, port),
					     lane_phy_pulse_status,
					     XE3PLPD_RATE_CALIB_DONE_LATENCY_MS))
			drm_warn(display->drm, "PHY %c PLL rate not changed\n",
				 phy_name(phy));

		/* 17. SW clears PORT_BUF_CTL2 [PHY Pulse Status]. */
		intel_de_rmw(display, XELPDP_PORT_BUF_CTL2(display, port),
			     lane_phy_pulse_status,
			     lane_phy_pulse_status);
	} else {
		intel_de_write(display, DDI_CLK_VALFREQ(encoder->port), crtc_state->port_clock);
	}

	/*
	 * 18. Follow the Display Voltage Frequency Switching - Sequence After Frequency Change.
	 * We handle this step in bxt_set_cdclk()
	 */
	/* 19. Move the PHY powerdown state to Active and program to enable/disable transmitters */
	intel_lt_phy_powerdown_change_sequence(encoder, owned_lane_mask,
					       XELPDP_P0_STATE_ACTIVE);

	intel_lt_phy_enable_disable_tx(encoder, crtc_state);
	intel_lt_phy_transaction_end(encoder, wakeref);
}

void intel_lt_phy_pll_disable(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	enum phy phy = intel_encoder_to_phy(encoder);
	enum port port = encoder->port;
	struct ref_tracker *wakeref;
	u8 owned_lane_mask = intel_lt_phy_get_owned_lane_mask(encoder);
	u32 lane_pipe_reset = owned_lane_mask == INTEL_LT_PHY_BOTH_LANES
				? (XELPDP_LANE_PIPE_RESET(0) |
				   XELPDP_LANE_PIPE_RESET(1))
				: XELPDP_LANE_PIPE_RESET(0);
	u32 lane_phy_current_status = owned_lane_mask == INTEL_LT_PHY_BOTH_LANES
					? (XELPDP_LANE_PHY_CURRENT_STATUS(0) |
					   XELPDP_LANE_PHY_CURRENT_STATUS(1))
					: XELPDP_LANE_PHY_CURRENT_STATUS(0);
	u32 lane_phy_pulse_status = owned_lane_mask == INTEL_LT_PHY_BOTH_LANES
					? (XE3PLPDP_LANE_PHY_PULSE_STATUS(0) |
					   XE3PLPDP_LANE_PHY_PULSE_STATUS(1))
					: XE3PLPDP_LANE_PHY_PULSE_STATUS(0);

	wakeref = intel_lt_phy_transaction_begin(encoder);

	/* 1. Clear PORT_BUF_CTL2 [PHY Pulse Status]. */
	intel_de_rmw(display, XELPDP_PORT_BUF_CTL2(display, port),
		     lane_phy_pulse_status,
		     lane_phy_pulse_status);

	/* 2. Set PORT_BUF_CTL2<port> Lane<PHY Lanes Owned> Pipe Reset to 1. */
	intel_de_rmw(display, XELPDP_PORT_BUF_CTL2(display, port), lane_pipe_reset,
		     lane_pipe_reset);

	/* 3. Poll for PORT_BUF_CTL2<port> Lane<PHY Lanes Owned> PHY Current Status == 1. */
	if (intel_de_wait_for_set_us(display, XELPDP_PORT_BUF_CTL2(display, port),
				     lane_phy_current_status,
				     XE3PLPD_RESET_START_LATENCY_US))
		drm_warn(display->drm, "PHY %c failed to reset lane\n",
			 phy_name(phy));

	/* 4. Clear for PHY pulse status on owned PHY lanes. */
	intel_de_rmw(display, XELPDP_PORT_BUF_CTL2(display, port),
		     lane_phy_pulse_status,
		     lane_phy_pulse_status);

	/*
	 * 5. Follow the Display Voltage Frequency Switching -
	 * Sequence Before Frequency Change. We handle this step in bxt_set_cdclk().
	 */
	/* 6. Program PORT_CLOCK_CTL[PCLK PLL Request LN0] = 0. */
	intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, port),
		     XELPDP_LANE_PCLK_PLL_REQUEST(0), 0);

	/* 7. Program DDI_CLK_VALFREQ to 0. */
	intel_de_write(display, DDI_CLK_VALFREQ(encoder->port), 0);

	/* 8. Poll for PORT_CLOCK_CTL[PCLK PLL Ack LN0]= 0. */
	if (intel_de_wait_for_clear_us(display, XELPDP_PORT_CLOCK_CTL(display, port),
				       XELPDP_LANE_PCLK_PLL_ACK(0),
				       XE3PLPD_MACCLK_TURNOFF_LATENCY_US))
		drm_warn(display->drm, "PHY %c PLL MacCLK ack deassertion timeout\n",
			 phy_name(phy));

	/*
	 *  9. Follow the Display Voltage Frequency Switching -
	 *  Sequence After Frequency Change. We handle this step in bxt_set_cdclk().
	 */
	/* 10. Program PORT_CLOCK_CTL register to disable and gate clocks. */
	intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, port),
		     XELPDP_DDI_CLOCK_SELECT_MASK(display) | XELPDP_FORWARD_CLOCK_UNGATE, 0);

	/* 11. Program PORT_BUF_CTL5[MacCLK Reset_0] = 1 to assert MacCLK reset. */
	intel_de_rmw(display, XE3PLPD_PORT_BUF_CTL5(port),
		     XE3PLPD_MACCLK_RESET_0, XE3PLPD_MACCLK_RESET_0);

	intel_lt_phy_transaction_end(encoder, wakeref);
}

void intel_lt_phy_set_signal_levels(struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	const struct intel_ddi_buf_trans *trans;
	u8 owned_lane_mask;
	struct ref_tracker *wakeref;
	int n_entries, ln;
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		return;

	owned_lane_mask = intel_lt_phy_get_owned_lane_mask(encoder);

	wakeref = intel_lt_phy_transaction_begin(encoder);

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(display->drm, !trans)) {
		intel_lt_phy_transaction_end(encoder, wakeref);
		return;
	}

	for (ln = 0; ln < crtc_state->lane_count; ln++) {
		int level = intel_ddi_level(encoder, crtc_state, ln);
		int lane = ln / 2;
		int tx = ln % 2;
		u8 lane_mask = lane == 0 ? INTEL_LT_PHY_LANE0 : INTEL_LT_PHY_LANE1;

		if (!(lane_mask & owned_lane_mask))
			continue;

		intel_lt_phy_rmw(encoder, lane_mask, LT_PHY_TXY_CTL8(tx),
				 LT_PHY_TX_SWING_LEVEL_MASK | LT_PHY_TX_SWING_MASK,
				 LT_PHY_TX_SWING_LEVEL(trans->entries[level].lt.txswing_level) |
				 LT_PHY_TX_SWING(trans->entries[level].lt.txswing),
				 MB_WRITE_COMMITTED);

		intel_lt_phy_rmw(encoder, lane_mask, LT_PHY_TXY_CTL2(tx),
				 LT_PHY_TX_CURSOR_MASK,
				 LT_PHY_TX_CURSOR(trans->entries[level].lt.pre_cursor),
				 MB_WRITE_COMMITTED);
		intel_lt_phy_rmw(encoder, lane_mask, LT_PHY_TXY_CTL3(tx),
				 LT_PHY_TX_CURSOR_MASK,
				 LT_PHY_TX_CURSOR(trans->entries[level].lt.main_cursor),
				 MB_WRITE_COMMITTED);
		intel_lt_phy_rmw(encoder, lane_mask, LT_PHY_TXY_CTL4(tx),
				 LT_PHY_TX_CURSOR_MASK,
				 LT_PHY_TX_CURSOR(trans->entries[level].lt.post_cursor),
				 MB_WRITE_COMMITTED);
	}

	intel_lt_phy_transaction_end(encoder, wakeref);
}

void intel_lt_phy_dump_hw_state(struct intel_display *display,
				const struct intel_lt_phy_pll_state *hw_state)
{
	int i, j;

	drm_dbg_kms(display->drm, "lt_phy_pll_hw_state:\n");
	for (i = 0; i < 3; i++) {
		drm_dbg_kms(display->drm, "config[%d] = 0x%.4x,\n",
			    i, hw_state->config[i]);
	}

	for (i = 0; i <= 12; i++)
		for (j = 3; j >= 0; j--)
			drm_dbg_kms(display->drm, "vdr_data[%d][%d] = 0x%.4x,\n",
				    i, j, hw_state->data[i][j]);
}

bool
intel_lt_phy_pll_compare_hw_state(const struct intel_lt_phy_pll_state *a,
				  const struct intel_lt_phy_pll_state *b)
{
	/*
	 * With LT PHY values other than VDR0_CONFIG and VDR2_CONFIG are
	 * unreliable. They cannot always be read back since internally
	 * after power gating values are not restored back to the
	 * shadow VDR registers. Thus we do not compare the whole state
	 * just the two VDR registers.
	 */
	if (a->config[0] == b->config[0] &&
	    a->config[2] == b->config[2])
		return true;

	return false;
}

void intel_lt_phy_pll_readout_hw_state(struct intel_encoder *encoder,
				       const struct intel_crtc_state *crtc_state,
				       struct intel_lt_phy_pll_state *pll_state)
{
	u8 owned_lane_mask;
	u8 lane;
	struct ref_tracker *wakeref;
	int i, j, k;

	pll_state->tbt_mode = intel_tc_port_in_tbt_alt_mode(enc_to_dig_port(encoder));
	if (pll_state->tbt_mode)
		return;

	owned_lane_mask = intel_lt_phy_get_owned_lane_mask(encoder);
	lane = owned_lane_mask & INTEL_LT_PHY_LANE0 ? : INTEL_LT_PHY_LANE1;
	wakeref = intel_lt_phy_transaction_begin(encoder);

	pll_state->config[0] = intel_lt_phy_read(encoder, lane, LT_PHY_VDR_0_CONFIG);
	pll_state->config[1] = intel_lt_phy_read(encoder, INTEL_LT_PHY_LANE0, LT_PHY_VDR_1_CONFIG);
	pll_state->config[2] = intel_lt_phy_read(encoder, lane, LT_PHY_VDR_2_CONFIG);

	for (i = 0; i <= 12; i++) {
		for (j = 3, k = 0; j >= 0; j--, k++)
			pll_state->data[i][k] =
				intel_lt_phy_read(encoder, INTEL_LT_PHY_LANE0,
						  LT_PHY_VDR_X_DATAY(i, j));
	}

	pll_state->clock =
		intel_lt_phy_calc_port_clock(encoder, crtc_state);
	intel_lt_phy_transaction_end(encoder, wakeref);
}

void intel_lt_phy_pll_state_verify(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_digital_port *dig_port;
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_encoder *encoder;
	struct intel_lt_phy_pll_state pll_hw_state = {};
	const struct intel_lt_phy_pll_state *pll_sw_state = &new_crtc_state->dpll_hw_state.ltpll;

	if (DISPLAY_VER(display) < 35)
		return;

	if (!new_crtc_state->hw.active)
		return;

	/* intel_get_crtc_new_encoder() only works for modeset/fastset commits */
	if (!intel_crtc_needs_modeset(new_crtc_state) &&
	    !intel_crtc_needs_fastset(new_crtc_state))
		return;

	encoder = intel_get_crtc_new_encoder(state, new_crtc_state);
	intel_lt_phy_pll_readout_hw_state(encoder, new_crtc_state, &pll_hw_state);

	dig_port = enc_to_dig_port(encoder);
	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		return;

	INTEL_DISPLAY_STATE_WARN(display, pll_hw_state.config[0] != pll_sw_state->config[0],
				 "[CRTC:%d:%s] mismatch in LT PHY PLL CONFIG 0: (expected 0x%04x, found 0x%04x)",
				 crtc->base.base.id, crtc->base.name,
				 pll_sw_state->config[0], pll_hw_state.config[0]);
	INTEL_DISPLAY_STATE_WARN(display, pll_hw_state.config[2] != pll_sw_state->config[2],
				 "[CRTC:%d:%s] mismatch in LT PHY PLL CONFIG 2: (expected 0x%04x, found 0x%04x)",
				 crtc->base.base.id, crtc->base.name,
				 pll_sw_state->config[2], pll_hw_state.config[2]);
}

void intel_xe3plpd_pll_enable(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		intel_mtl_tbt_pll_enable_clock(encoder, crtc_state->port_clock);
	else
		intel_lt_phy_pll_enable(encoder, crtc_state);
}

void intel_xe3plpd_pll_disable(struct intel_encoder *encoder)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		intel_mtl_tbt_pll_disable_clock(encoder);
	else
		intel_lt_phy_pll_disable(encoder);

}
