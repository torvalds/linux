/*
 * OMAP3 clock data
 *
 * Copyright (C) 2007-2010, 2012 Texas Instruments, Inc.
 * Copyright (C) 2007-2011 Nokia Corporation
 *
 * Written by Paul Walmsley
 * With many device clock fixes by Kevin Hilman and Jouni Högander
 * DPLL bypass clock support added by Roman Tereshonkov
 *
 */

/*
 * Virtual clocks are introduced as convenient tools.
 * They are sources for other clocks and not supposed
 * to be requested from drivers directly.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/list.h>
#include <linux/io.h>

#include <plat/hardware.h>
#include <plat/clkdev_omap.h>

#include "iomap.h"
#include "clock.h"
#include "clock3xxx.h"
#include "clock34xx.h"
#include "clock36xx.h"
#include "clock3517.h"
#include "cm2xxx_3xxx.h"
#include "cm-regbits-34xx.h"
#include "prm2xxx_3xxx.h"
#include "prm-regbits-34xx.h"
#include "control.h"

/*
 * clocks
 */

#define OMAP_CM_REGADDR		OMAP34XX_CM_REGADDR

/* Maximum DPLL multiplier, divider values for OMAP3 */
#define OMAP3_MAX_DPLL_MULT		2047
#define OMAP3630_MAX_JTYPE_DPLL_MULT	4095
#define OMAP3_MAX_DPLL_DIV		128

/*
 * DPLL1 supplies clock to the MPU.
 * DPLL2 supplies clock to the IVA2.
 * DPLL3 supplies CORE domain clocks.
 * DPLL4 supplies peripheral clocks.
 * DPLL5 supplies other peripheral clocks (USBHOST, USIM).
 */

/* Forward declarations for DPLL bypass clocks */
static struct clk dpll1_fck;
static struct clk dpll2_fck;

/* PRM CLOCKS */

/* According to timer32k.c, this is a 32768Hz clock, not a 32000Hz clock. */
static struct clk omap_32k_fck = {
	.name		= "omap_32k_fck",
	.ops		= &clkops_null,
	.rate		= 32768,
};

static struct clk secure_32k_fck = {
	.name		= "secure_32k_fck",
	.ops		= &clkops_null,
	.rate		= 32768,
};

/* Virtual source clocks for osc_sys_ck */
static struct clk virt_12m_ck = {
	.name		= "virt_12m_ck",
	.ops		= &clkops_null,
	.rate		= 12000000,
};

static struct clk virt_13m_ck = {
	.name		= "virt_13m_ck",
	.ops		= &clkops_null,
	.rate		= 13000000,
};

static struct clk virt_16_8m_ck = {
	.name		= "virt_16_8m_ck",
	.ops		= &clkops_null,
	.rate		= 16800000,
};

static struct clk virt_19_2m_ck = {
	.name		= "virt_19_2m_ck",
	.ops		= &clkops_null,
	.rate		= 19200000,
};

static struct clk virt_26m_ck = {
	.name		= "virt_26m_ck",
	.ops		= &clkops_null,
	.rate		= 26000000,
};

static struct clk virt_38_4m_ck = {
	.name		= "virt_38_4m_ck",
	.ops		= &clkops_null,
	.rate		= 38400000,
};

static const struct clksel_rate osc_sys_12m_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate osc_sys_13m_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate osc_sys_16_8m_rates[] = {
	{ .div = 1, .val = 5, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 0 }
};

static const struct clksel_rate osc_sys_19_2m_rates[] = {
	{ .div = 1, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate osc_sys_26m_rates[] = {
	{ .div = 1, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate osc_sys_38_4m_rates[] = {
	{ .div = 1, .val = 4, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel osc_sys_clksel[] = {
	{ .parent = &virt_12m_ck,   .rates = osc_sys_12m_rates },
	{ .parent = &virt_13m_ck,   .rates = osc_sys_13m_rates },
	{ .parent = &virt_16_8m_ck, .rates = osc_sys_16_8m_rates },
	{ .parent = &virt_19_2m_ck, .rates = osc_sys_19_2m_rates },
	{ .parent = &virt_26m_ck,   .rates = osc_sys_26m_rates },
	{ .parent = &virt_38_4m_ck, .rates = osc_sys_38_4m_rates },
	{ .parent = NULL },
};

/* Oscillator clock */
/* 12, 13, 16.8, 19.2, 26, or 38.4 MHz */
static struct clk osc_sys_ck = {
	.name		= "osc_sys_ck",
	.ops		= &clkops_null,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP3430_PRM_CLKSEL,
	.clksel_mask	= OMAP3430_SYS_CLKIN_SEL_MASK,
	.clksel		= osc_sys_clksel,
	/* REVISIT: deal with autoextclkmode? */
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel_rate div2_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel sys_clksel[] = {
	{ .parent = &osc_sys_ck, .rates = div2_rates },
	{ .parent = NULL }
};

/* Latency: this clock is only enabled after PRM_CLKSETUP.SETUP_TIME */
/* Feeds DPLLs - divided first by PRM_CLKSRC_CTRL.SYSCLKDIV? */
static struct clk sys_ck = {
	.name		= "sys_ck",
	.ops		= &clkops_null,
	.parent		= &osc_sys_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP3430_PRM_CLKSRC_CTRL,
	.clksel_mask	= OMAP_SYSCLKDIV_MASK,
	.clksel		= sys_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk sys_altclk = {
	.name		= "sys_altclk",
	.ops		= &clkops_null,
};

/* Optional external clock input for some McBSPs */
static struct clk mcbsp_clks = {
	.name		= "mcbsp_clks",
	.ops		= &clkops_null,
};

/* PRM EXTERNAL CLOCK OUTPUT */

static struct clk sys_clkout1 = {
	.name		= "sys_clkout1",
	.ops		= &clkops_omap2_dflt,
	.parent		= &osc_sys_ck,
	.enable_reg	= OMAP3430_PRM_CLKOUT_CTRL,
	.enable_bit	= OMAP3430_CLKOUT_EN_SHIFT,
	.recalc		= &followparent_recalc,
};

/* DPLLS */

/* CM CLOCKS */

static const struct clksel_rate div16_dpll_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 3, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 4, .val = 4, .flags = RATE_IN_3XXX },
	{ .div = 5, .val = 5, .flags = RATE_IN_3XXX },
	{ .div = 6, .val = 6, .flags = RATE_IN_3XXX },
	{ .div = 7, .val = 7, .flags = RATE_IN_3XXX },
	{ .div = 8, .val = 8, .flags = RATE_IN_3XXX },
	{ .div = 9, .val = 9, .flags = RATE_IN_3XXX },
	{ .div = 10, .val = 10, .flags = RATE_IN_3XXX },
	{ .div = 11, .val = 11, .flags = RATE_IN_3XXX },
	{ .div = 12, .val = 12, .flags = RATE_IN_3XXX },
	{ .div = 13, .val = 13, .flags = RATE_IN_3XXX },
	{ .div = 14, .val = 14, .flags = RATE_IN_3XXX },
	{ .div = 15, .val = 15, .flags = RATE_IN_3XXX },
	{ .div = 16, .val = 16, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate dpll4_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 3, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 4, .val = 4, .flags = RATE_IN_3XXX },
	{ .div = 5, .val = 5, .flags = RATE_IN_3XXX },
	{ .div = 6, .val = 6, .flags = RATE_IN_3XXX },
	{ .div = 7, .val = 7, .flags = RATE_IN_3XXX },
	{ .div = 8, .val = 8, .flags = RATE_IN_3XXX },
	{ .div = 9, .val = 9, .flags = RATE_IN_3XXX },
	{ .div = 10, .val = 10, .flags = RATE_IN_3XXX },
	{ .div = 11, .val = 11, .flags = RATE_IN_3XXX },
	{ .div = 12, .val = 12, .flags = RATE_IN_3XXX },
	{ .div = 13, .val = 13, .flags = RATE_IN_3XXX },
	{ .div = 14, .val = 14, .flags = RATE_IN_3XXX },
	{ .div = 15, .val = 15, .flags = RATE_IN_3XXX },
	{ .div = 16, .val = 16, .flags = RATE_IN_3XXX },
	{ .div = 17, .val = 17, .flags = RATE_IN_36XX },
	{ .div = 18, .val = 18, .flags = RATE_IN_36XX },
	{ .div = 19, .val = 19, .flags = RATE_IN_36XX },
	{ .div = 20, .val = 20, .flags = RATE_IN_36XX },
	{ .div = 21, .val = 21, .flags = RATE_IN_36XX },
	{ .div = 22, .val = 22, .flags = RATE_IN_36XX },
	{ .div = 23, .val = 23, .flags = RATE_IN_36XX },
	{ .div = 24, .val = 24, .flags = RATE_IN_36XX },
	{ .div = 25, .val = 25, .flags = RATE_IN_36XX },
	{ .div = 26, .val = 26, .flags = RATE_IN_36XX },
	{ .div = 27, .val = 27, .flags = RATE_IN_36XX },
	{ .div = 28, .val = 28, .flags = RATE_IN_36XX },
	{ .div = 29, .val = 29, .flags = RATE_IN_36XX },
	{ .div = 30, .val = 30, .flags = RATE_IN_36XX },
	{ .div = 31, .val = 31, .flags = RATE_IN_36XX },
	{ .div = 32, .val = 32, .flags = RATE_IN_36XX },
	{ .div = 0 }
};

/* DPLL1 */
/* MPU clock source */
/* Type: DPLL */
static struct dpll_data dpll1_dd = {
	.mult_div1_reg	= OMAP_CM_REGADDR(MPU_MOD, OMAP3430_CM_CLKSEL1_PLL),
	.mult_mask	= OMAP3430_MPU_DPLL_MULT_MASK,
	.div1_mask	= OMAP3430_MPU_DPLL_DIV_MASK,
	.clk_bypass	= &dpll1_fck,
	.clk_ref	= &sys_ck,
	.freqsel_mask	= OMAP3430_MPU_DPLL_FREQSEL_MASK,
	.control_reg	= OMAP_CM_REGADDR(MPU_MOD, OMAP3430_CM_CLKEN_PLL),
	.enable_mask	= OMAP3430_EN_MPU_DPLL_MASK,
	.modes		= (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	.auto_recal_bit	= OMAP3430_EN_MPU_DPLL_DRIFTGUARD_SHIFT,
	.recal_en_bit	= OMAP3430_MPU_DPLL_RECAL_EN_SHIFT,
	.recal_st_bit	= OMAP3430_MPU_DPLL_ST_SHIFT,
	.autoidle_reg	= OMAP_CM_REGADDR(MPU_MOD, OMAP3430_CM_AUTOIDLE_PLL),
	.autoidle_mask	= OMAP3430_AUTO_MPU_DPLL_MASK,
	.idlest_reg	= OMAP_CM_REGADDR(MPU_MOD, OMAP3430_CM_IDLEST_PLL),
	.idlest_mask	= OMAP3430_ST_MPU_CLK_MASK,
	.max_multiplier = OMAP3_MAX_DPLL_MULT,
	.min_divider	= 1,
	.max_divider	= OMAP3_MAX_DPLL_DIV,
};

static struct clk dpll1_ck = {
	.name		= "dpll1_ck",
	.ops		= &clkops_omap3_noncore_dpll_ops,
	.parent		= &sys_ck,
	.dpll_data	= &dpll1_dd,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.clkdm_name	= "dpll1_clkdm",
	.recalc		= &omap3_dpll_recalc,
};

/*
 * This virtual clock provides the CLKOUTX2 output from the DPLL if the
 * DPLL isn't bypassed.
 */
static struct clk dpll1_x2_ck = {
	.name		= "dpll1_x2_ck",
	.ops		= &clkops_null,
	.parent		= &dpll1_ck,
	.clkdm_name	= "dpll1_clkdm",
	.recalc		= &omap3_clkoutx2_recalc,
};

/* On DPLL1, unlike other DPLLs, the divider is downstream from CLKOUTX2 */
static const struct clksel div16_dpll1_x2m2_clksel[] = {
	{ .parent = &dpll1_x2_ck, .rates = div16_dpll_rates },
	{ .parent = NULL }
};

/*
 * Does not exist in the TRM - needed to separate the M2 divider from
 * bypass selection in mpu_ck
 */
static struct clk dpll1_x2m2_ck = {
	.name		= "dpll1_x2m2_ck",
	.ops		= &clkops_null,
	.parent		= &dpll1_x2_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(MPU_MOD, OMAP3430_CM_CLKSEL2_PLL),
	.clksel_mask	= OMAP3430_MPU_DPLL_CLKOUT_DIV_MASK,
	.clksel		= div16_dpll1_x2m2_clksel,
	.clkdm_name	= "dpll1_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* DPLL2 */
/* IVA2 clock source */
/* Type: DPLL */

static struct dpll_data dpll2_dd = {
	.mult_div1_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_CLKSEL1_PLL),
	.mult_mask	= OMAP3430_IVA2_DPLL_MULT_MASK,
	.div1_mask	= OMAP3430_IVA2_DPLL_DIV_MASK,
	.clk_bypass	= &dpll2_fck,
	.clk_ref	= &sys_ck,
	.freqsel_mask	= OMAP3430_IVA2_DPLL_FREQSEL_MASK,
	.control_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_CLKEN_PLL),
	.enable_mask	= OMAP3430_EN_IVA2_DPLL_MASK,
	.modes		= (1 << DPLL_LOW_POWER_STOP) | (1 << DPLL_LOCKED) |
				(1 << DPLL_LOW_POWER_BYPASS),
	.auto_recal_bit	= OMAP3430_EN_IVA2_DPLL_DRIFTGUARD_SHIFT,
	.recal_en_bit	= OMAP3430_PRM_IRQENABLE_MPU_IVA2_DPLL_RECAL_EN_SHIFT,
	.recal_st_bit	= OMAP3430_PRM_IRQSTATUS_MPU_IVA2_DPLL_ST_SHIFT,
	.autoidle_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_AUTOIDLE_PLL),
	.autoidle_mask	= OMAP3430_AUTO_IVA2_DPLL_MASK,
	.idlest_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_IDLEST_PLL),
	.idlest_mask	= OMAP3430_ST_IVA2_CLK_MASK,
	.max_multiplier = OMAP3_MAX_DPLL_MULT,
	.min_divider	= 1,
	.max_divider	= OMAP3_MAX_DPLL_DIV,
};

static struct clk dpll2_ck = {
	.name		= "dpll2_ck",
	.ops		= &clkops_omap3_noncore_dpll_ops,
	.parent		= &sys_ck,
	.dpll_data	= &dpll2_dd,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.clkdm_name	= "dpll2_clkdm",
	.recalc		= &omap3_dpll_recalc,
};

static const struct clksel div16_dpll2_m2x2_clksel[] = {
	{ .parent = &dpll2_ck, .rates = div16_dpll_rates },
	{ .parent = NULL }
};

/*
 * The TRM is conflicted on whether IVA2 clock comes from DPLL2 CLKOUT
 * or CLKOUTX2. CLKOUT seems most plausible.
 */
static struct clk dpll2_m2_ck = {
	.name		= "dpll2_m2_ck",
	.ops		= &clkops_null,
	.parent		= &dpll2_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD,
					  OMAP3430_CM_CLKSEL2_PLL),
	.clksel_mask	= OMAP3430_IVA2_DPLL_CLKOUT_DIV_MASK,
	.clksel		= div16_dpll2_m2x2_clksel,
	.clkdm_name	= "dpll2_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/*
 * DPLL3
 * Source clock for all interfaces and for some device fclks
 * REVISIT: Also supports fast relock bypass - not included below
 */
static struct dpll_data dpll3_dd = {
	.mult_div1_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.mult_mask	= OMAP3430_CORE_DPLL_MULT_MASK,
	.div1_mask	= OMAP3430_CORE_DPLL_DIV_MASK,
	.clk_bypass	= &sys_ck,
	.clk_ref	= &sys_ck,
	.freqsel_mask	= OMAP3430_CORE_DPLL_FREQSEL_MASK,
	.control_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_mask	= OMAP3430_EN_CORE_DPLL_MASK,
	.auto_recal_bit	= OMAP3430_EN_CORE_DPLL_DRIFTGUARD_SHIFT,
	.recal_en_bit	= OMAP3430_CORE_DPLL_RECAL_EN_SHIFT,
	.recal_st_bit	= OMAP3430_CORE_DPLL_ST_SHIFT,
	.autoidle_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_AUTOIDLE),
	.autoidle_mask	= OMAP3430_AUTO_CORE_DPLL_MASK,
	.idlest_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_IDLEST),
	.idlest_mask	= OMAP3430_ST_CORE_CLK_MASK,
	.max_multiplier = OMAP3_MAX_DPLL_MULT,
	.min_divider	= 1,
	.max_divider	= OMAP3_MAX_DPLL_DIV,
};

static struct clk dpll3_ck = {
	.name		= "dpll3_ck",
	.ops		= &clkops_omap3_core_dpll_ops,
	.parent		= &sys_ck,
	.dpll_data	= &dpll3_dd,
	.round_rate	= &omap2_dpll_round_rate,
	.clkdm_name	= "dpll3_clkdm",
	.recalc		= &omap3_dpll_recalc,
};

/*
 * This virtual clock provides the CLKOUTX2 output from the DPLL if the
 * DPLL isn't bypassed
 */
static struct clk dpll3_x2_ck = {
	.name		= "dpll3_x2_ck",
	.ops		= &clkops_null,
	.parent		= &dpll3_ck,
	.clkdm_name	= "dpll3_clkdm",
	.recalc		= &omap3_clkoutx2_recalc,
};

static const struct clksel_rate div31_dpll3_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 3, .val = 3, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 5, .val = 5, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 6, .val = 6, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 7, .val = 7, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 8, .val = 8, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 9, .val = 9, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 10, .val = 10, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 11, .val = 11, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 12, .val = 12, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 13, .val = 13, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 14, .val = 14, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 15, .val = 15, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 16, .val = 16, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 17, .val = 17, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 18, .val = 18, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 19, .val = 19, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 20, .val = 20, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 21, .val = 21, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 22, .val = 22, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 23, .val = 23, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 24, .val = 24, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 25, .val = 25, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 26, .val = 26, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 27, .val = 27, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 28, .val = 28, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 29, .val = 29, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 30, .val = 30, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 31, .val = 31, .flags = RATE_IN_3430ES2PLUS_36XX },
	{ .div = 0 },
};

static const struct clksel div31_dpll3m2_clksel[] = {
	{ .parent = &dpll3_ck, .rates = div31_dpll3_rates },
	{ .parent = NULL }
};

/* DPLL3 output M2 - primary control point for CORE speed */
static struct clk dpll3_m2_ck = {
	.name		= "dpll3_m2_ck",
	.ops		= &clkops_null,
	.parent		= &dpll3_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_CORE_DPLL_CLKOUT_DIV_MASK,
	.clksel		= div31_dpll3m2_clksel,
	.clkdm_name	= "dpll3_clkdm",
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap3_core_dpll_m2_set_rate,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk core_ck = {
	.name		= "core_ck",
	.ops		= &clkops_null,
	.parent		= &dpll3_m2_ck,
	.recalc		= &followparent_recalc,
};

static struct clk dpll3_m2x2_ck = {
	.name		= "dpll3_m2x2_ck",
	.ops		= &clkops_null,
	.parent		= &dpll3_m2_ck,
	.clkdm_name	= "dpll3_clkdm",
	.recalc		= &omap3_clkoutx2_recalc,
};

/* The PWRDN bit is apparently only available on 3430ES2 and above */
static const struct clksel div16_dpll3_clksel[] = {
	{ .parent = &dpll3_ck, .rates = div16_dpll_rates },
	{ .parent = NULL }
};

/* This virtual clock is the source for dpll3_m3x2_ck */
static struct clk dpll3_m3_ck = {
	.name		= "dpll3_m3_ck",
	.ops		= &clkops_null,
	.parent		= &dpll3_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_DIV_DPLL3_MASK,
	.clksel		= div16_dpll3_clksel,
	.clkdm_name	= "dpll3_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* The PWRDN bit is apparently only available on 3430ES2 and above */
static struct clk dpll3_m3x2_ck = {
	.name		= "dpll3_m3x2_ck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &dpll3_m3_ck,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_EMU_CORE_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll3_clkdm",
	.recalc		= &omap3_clkoutx2_recalc,
};

static struct clk emu_core_alwon_ck = {
	.name		= "emu_core_alwon_ck",
	.ops		= &clkops_null,
	.parent		= &dpll3_m3x2_ck,
	.clkdm_name	= "dpll3_clkdm",
	.recalc		= &followparent_recalc,
};

/* DPLL4 */
/* Supplies 96MHz, 54Mhz TV DAC, DSS fclk, CAM sensor clock, emul trace clk */
/* Type: DPLL */
static struct dpll_data dpll4_dd;

static struct dpll_data dpll4_dd_34xx __initdata = {
	.mult_div1_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL2),
	.mult_mask	= OMAP3430_PERIPH_DPLL_MULT_MASK,
	.div1_mask	= OMAP3430_PERIPH_DPLL_DIV_MASK,
	.clk_bypass	= &sys_ck,
	.clk_ref	= &sys_ck,
	.freqsel_mask	= OMAP3430_PERIPH_DPLL_FREQSEL_MASK,
	.control_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_mask	= OMAP3430_EN_PERIPH_DPLL_MASK,
	.modes		= (1 << DPLL_LOW_POWER_STOP) | (1 << DPLL_LOCKED),
	.auto_recal_bit	= OMAP3430_EN_PERIPH_DPLL_DRIFTGUARD_SHIFT,
	.recal_en_bit	= OMAP3430_PERIPH_DPLL_RECAL_EN_SHIFT,
	.recal_st_bit	= OMAP3430_PERIPH_DPLL_ST_SHIFT,
	.autoidle_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_AUTOIDLE),
	.autoidle_mask	= OMAP3430_AUTO_PERIPH_DPLL_MASK,
	.idlest_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_IDLEST),
	.idlest_mask	= OMAP3430_ST_PERIPH_CLK_MASK,
	.max_multiplier = OMAP3_MAX_DPLL_MULT,
	.min_divider	= 1,
	.max_divider	= OMAP3_MAX_DPLL_DIV,
};

static struct dpll_data dpll4_dd_3630 __initdata = {
	.mult_div1_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL2),
	.mult_mask	= OMAP3630_PERIPH_DPLL_MULT_MASK,
	.div1_mask	= OMAP3430_PERIPH_DPLL_DIV_MASK,
	.clk_bypass	= &sys_ck,
	.clk_ref	= &sys_ck,
	.control_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_mask	= OMAP3430_EN_PERIPH_DPLL_MASK,
	.modes		= (1 << DPLL_LOW_POWER_STOP) | (1 << DPLL_LOCKED),
	.auto_recal_bit	= OMAP3430_EN_PERIPH_DPLL_DRIFTGUARD_SHIFT,
	.recal_en_bit	= OMAP3430_PERIPH_DPLL_RECAL_EN_SHIFT,
	.recal_st_bit	= OMAP3430_PERIPH_DPLL_ST_SHIFT,
	.autoidle_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_AUTOIDLE),
	.autoidle_mask	= OMAP3430_AUTO_PERIPH_DPLL_MASK,
	.idlest_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_IDLEST),
	.idlest_mask	= OMAP3430_ST_PERIPH_CLK_MASK,
	.dco_mask	= OMAP3630_PERIPH_DPLL_DCO_SEL_MASK,
	.sddiv_mask	= OMAP3630_PERIPH_DPLL_SD_DIV_MASK,
	.max_multiplier = OMAP3630_MAX_JTYPE_DPLL_MULT,
	.min_divider	= 1,
	.max_divider	= OMAP3_MAX_DPLL_DIV,
	.flags		= DPLL_J_TYPE
};

static struct clk dpll4_ck = {
	.name		= "dpll4_ck",
	.ops		= &clkops_omap3_noncore_dpll_ops,
	.parent		= &sys_ck,
	.dpll_data	= &dpll4_dd,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_dpll4_set_rate,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap3_dpll_recalc,
};

/*
 * This virtual clock provides the CLKOUTX2 output from the DPLL if the
 * DPLL isn't bypassed --
 * XXX does this serve any downstream clocks?
 */
static struct clk dpll4_x2_ck = {
	.name		= "dpll4_x2_ck",
	.ops		= &clkops_null,
	.parent		= &dpll4_ck,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap3_clkoutx2_recalc,
};

static const struct clksel dpll4_clksel[] = {
	{ .parent = &dpll4_ck, .rates = dpll4_rates },
	{ .parent = NULL }
};

/* This virtual clock is the source for dpll4_m2x2_ck */
static struct clk dpll4_m2_ck = {
	.name		= "dpll4_m2_ck",
	.ops		= &clkops_null,
	.parent		= &dpll4_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, OMAP3430_CM_CLKSEL3),
	.clksel_mask	= OMAP3630_DIV_96M_MASK,
	.clksel		= dpll4_clksel,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* The PWRDN bit is apparently only available on 3430ES2 and above */
static struct clk dpll4_m2x2_ck = {
	.name		= "dpll4_m2x2_ck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &dpll4_m2_ck,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_96M_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap3_clkoutx2_recalc,
};

/*
 * DPLL4 generates DPLL4_M2X2_CLK which is then routed into the PRM as
 * PRM_96M_ALWON_(F)CLK.  Two clocks then emerge from the PRM:
 * 96M_ALWON_FCLK (called "omap_96m_alwon_fck" below) and
 * CM_96K_(F)CLK.
 */

/* Adding 192MHz Clock node needed by SGX */
static struct clk omap_192m_alwon_fck = {
	.name		= "omap_192m_alwon_fck",
	.ops		= &clkops_null,
	.parent		= &dpll4_m2x2_ck,
	.recalc		= &followparent_recalc,
};

static const struct clksel_rate omap_96m_alwon_fck_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_36XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_36XX },
	{ .div = 0 }
};

static const struct clksel omap_96m_alwon_fck_clksel[] = {
	{ .parent = &omap_192m_alwon_fck, .rates = omap_96m_alwon_fck_rates },
	{ .parent = NULL }
};

static const struct clksel_rate omap_96m_dpll_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate omap_96m_sys_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static struct clk omap_96m_alwon_fck = {
	.name		= "omap_96m_alwon_fck",
	.ops		= &clkops_null,
	.parent		= &dpll4_m2x2_ck,
	.recalc		= &followparent_recalc,
};

static struct clk omap_96m_alwon_fck_3630 = {
	.name		= "omap_96m_alwon_fck",
	.parent		= &omap_192m_alwon_fck,
	.init		= &omap2_init_clksel_parent,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3630_CLKSEL_96M_MASK,
	.clksel		= omap_96m_alwon_fck_clksel
};

static struct clk cm_96m_fck = {
	.name		= "cm_96m_fck",
	.ops		= &clkops_null,
	.parent		= &omap_96m_alwon_fck,
	.recalc		= &followparent_recalc,
};

static const struct clksel omap_96m_fck_clksel[] = {
	{ .parent = &cm_96m_fck, .rates = omap_96m_dpll_rates },
	{ .parent = &sys_ck,	 .rates = omap_96m_sys_rates },
	{ .parent = NULL }
};

static struct clk omap_96m_fck = {
	.name		= "omap_96m_fck",
	.ops		= &clkops_null,
	.parent		= &sys_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_SOURCE_96M_MASK,
	.clksel		= omap_96m_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/* This virtual clock is the source for dpll4_m3x2_ck */
static struct clk dpll4_m3_ck = {
	.name		= "dpll4_m3_ck",
	.ops		= &clkops_null,
	.parent		= &dpll4_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3630_CLKSEL_TV_MASK,
	.clksel		= dpll4_clksel,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* The PWRDN bit is apparently only available on 3430ES2 and above */
static struct clk dpll4_m3x2_ck = {
	.name		= "dpll4_m3x2_ck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &dpll4_m3_ck,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_TV_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap3_clkoutx2_recalc,
};

static const struct clksel_rate omap_54m_d4m3x2_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate omap_54m_alt_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel omap_54m_clksel[] = {
	{ .parent = &dpll4_m3x2_ck, .rates = omap_54m_d4m3x2_rates },
	{ .parent = &sys_altclk,    .rates = omap_54m_alt_rates },
	{ .parent = NULL }
};

static struct clk omap_54m_fck = {
	.name		= "omap_54m_fck",
	.ops		= &clkops_null,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_SOURCE_54M_MASK,
	.clksel		= omap_54m_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel_rate omap_48m_cm96m_rates[] = {
	{ .div = 2, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate omap_48m_alt_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel omap_48m_clksel[] = {
	{ .parent = &cm_96m_fck, .rates = omap_48m_cm96m_rates },
	{ .parent = &sys_altclk, .rates = omap_48m_alt_rates },
	{ .parent = NULL }
};

static struct clk omap_48m_fck = {
	.name		= "omap_48m_fck",
	.ops		= &clkops_null,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_SOURCE_48M_MASK,
	.clksel		= omap_48m_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk omap_12m_fck = {
	.name		= "omap_12m_fck",
	.ops		= &clkops_null,
	.parent		= &omap_48m_fck,
	.fixed_div	= 4,
	.recalc		= &omap_fixed_divisor_recalc,
};

/* This virtual clock is the source for dpll4_m4x2_ck */
static struct clk dpll4_m4_ck = {
	.name		= "dpll4_m4_ck",
	.ops		= &clkops_null,
	.parent		= &dpll4_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3630_CLKSEL_DSS1_MASK,
	.clksel		= dpll4_clksel,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap2_clksel_recalc,
	.set_rate	= &omap2_clksel_set_rate,
	.round_rate	= &omap2_clksel_round_rate,
};

/* The PWRDN bit is apparently only available on 3430ES2 and above */
static struct clk dpll4_m4x2_ck = {
	.name		= "dpll4_m4x2_ck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &dpll4_m4_ck,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_DSS1_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap3_clkoutx2_recalc,
};

/* This virtual clock is the source for dpll4_m5x2_ck */
static struct clk dpll4_m5_ck = {
	.name		= "dpll4_m5_ck",
	.ops		= &clkops_null,
	.parent		= &dpll4_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_CAM_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3630_CLKSEL_CAM_MASK,
	.clksel		= dpll4_clksel,
	.clkdm_name	= "dpll4_clkdm",
	.set_rate	= &omap2_clksel_set_rate,
	.round_rate	= &omap2_clksel_round_rate,
	.recalc		= &omap2_clksel_recalc,
};

/* The PWRDN bit is apparently only available on 3430ES2 and above */
static struct clk dpll4_m5x2_ck = {
	.name		= "dpll4_m5x2_ck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &dpll4_m5_ck,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_CAM_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap3_clkoutx2_recalc,
};

/* This virtual clock is the source for dpll4_m6x2_ck */
static struct clk dpll4_m6_ck = {
	.name		= "dpll4_m6_ck",
	.ops		= &clkops_null,
	.parent		= &dpll4_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3630_DIV_DPLL4_MASK,
	.clksel		= dpll4_clksel,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* The PWRDN bit is apparently only available on 3430ES2 and above */
static struct clk dpll4_m6x2_ck = {
	.name		= "dpll4_m6x2_ck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &dpll4_m6_ck,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_EMU_PERIPH_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &omap3_clkoutx2_recalc,
};

static struct clk emu_per_alwon_ck = {
	.name		= "emu_per_alwon_ck",
	.ops		= &clkops_null,
	.parent		= &dpll4_m6x2_ck,
	.clkdm_name	= "dpll4_clkdm",
	.recalc		= &followparent_recalc,
};

/* DPLL5 */
/* Supplies 120MHz clock, USIM source clock */
/* Type: DPLL */
/* 3430ES2 only */
static struct dpll_data dpll5_dd = {
	.mult_div1_reg	= OMAP_CM_REGADDR(PLL_MOD, OMAP3430ES2_CM_CLKSEL4),
	.mult_mask	= OMAP3430ES2_PERIPH2_DPLL_MULT_MASK,
	.div1_mask	= OMAP3430ES2_PERIPH2_DPLL_DIV_MASK,
	.clk_bypass	= &sys_ck,
	.clk_ref	= &sys_ck,
	.freqsel_mask	= OMAP3430ES2_PERIPH2_DPLL_FREQSEL_MASK,
	.control_reg	= OMAP_CM_REGADDR(PLL_MOD, OMAP3430ES2_CM_CLKEN2),
	.enable_mask	= OMAP3430ES2_EN_PERIPH2_DPLL_MASK,
	.modes		= (1 << DPLL_LOW_POWER_STOP) | (1 << DPLL_LOCKED),
	.auto_recal_bit	= OMAP3430ES2_EN_PERIPH2_DPLL_DRIFTGUARD_SHIFT,
	.recal_en_bit	= OMAP3430ES2_SND_PERIPH_DPLL_RECAL_EN_SHIFT,
	.recal_st_bit	= OMAP3430ES2_SND_PERIPH_DPLL_ST_SHIFT,
	.autoidle_reg	= OMAP_CM_REGADDR(PLL_MOD, OMAP3430ES2_CM_AUTOIDLE2_PLL),
	.autoidle_mask	= OMAP3430ES2_AUTO_PERIPH2_DPLL_MASK,
	.idlest_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_IDLEST2),
	.idlest_mask	= OMAP3430ES2_ST_PERIPH2_CLK_MASK,
	.max_multiplier = OMAP3_MAX_DPLL_MULT,
	.min_divider	= 1,
	.max_divider	= OMAP3_MAX_DPLL_DIV,
};

static struct clk dpll5_ck = {
	.name		= "dpll5_ck",
	.ops		= &clkops_omap3_noncore_dpll_ops,
	.parent		= &sys_ck,
	.dpll_data	= &dpll5_dd,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.clkdm_name	= "dpll5_clkdm",
	.recalc		= &omap3_dpll_recalc,
};

static const struct clksel div16_dpll5_clksel[] = {
	{ .parent = &dpll5_ck, .rates = div16_dpll_rates },
	{ .parent = NULL }
};

static struct clk dpll5_m2_ck = {
	.name		= "dpll5_m2_ck",
	.ops		= &clkops_null,
	.parent		= &dpll5_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, OMAP3430ES2_CM_CLKSEL5),
	.clksel_mask	= OMAP3430ES2_DIV_120M_MASK,
	.clksel		= div16_dpll5_clksel,
	.clkdm_name	= "dpll5_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* CM EXTERNAL CLOCK OUTPUTS */

static const struct clksel_rate clkout2_src_core_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate clkout2_src_sys_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate clkout2_src_96m_rates[] = {
	{ .div = 1, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate clkout2_src_54m_rates[] = {
	{ .div = 1, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel clkout2_src_clksel[] = {
	{ .parent = &core_ck,		.rates = clkout2_src_core_rates },
	{ .parent = &sys_ck,		.rates = clkout2_src_sys_rates },
	{ .parent = &cm_96m_fck,	.rates = clkout2_src_96m_rates },
	{ .parent = &omap_54m_fck,	.rates = clkout2_src_54m_rates },
	{ .parent = NULL }
};

static struct clk clkout2_src_ck = {
	.name		= "clkout2_src_ck",
	.ops		= &clkops_omap2_dflt,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP3430_CM_CLKOUT_CTRL,
	.enable_bit	= OMAP3430_CLKOUT2_EN_SHIFT,
	.clksel_reg	= OMAP3430_CM_CLKOUT_CTRL,
	.clksel_mask	= OMAP3430_CLKOUT2SOURCE_MASK,
	.clksel		= clkout2_src_clksel,
	.clkdm_name	= "core_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel_rate sys_clkout2_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 4, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 8, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 16, .val = 4, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel sys_clkout2_clksel[] = {
	{ .parent = &clkout2_src_ck, .rates = sys_clkout2_rates },
	{ .parent = NULL },
};

static struct clk sys_clkout2 = {
	.name		= "sys_clkout2",
	.ops		= &clkops_null,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP3430_CM_CLKOUT_CTRL,
	.clksel_mask	= OMAP3430_CLKOUT2_DIV_MASK,
	.clksel		= sys_clkout2_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

/* CM OUTPUT CLOCKS */

static struct clk corex2_fck = {
	.name		= "corex2_fck",
	.ops		= &clkops_null,
	.parent		= &dpll3_m2x2_ck,
	.recalc		= &followparent_recalc,
};

/* DPLL power domain clock controls */

static const struct clksel_rate div4_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 4, .val = 4, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel div4_core_clksel[] = {
	{ .parent = &core_ck, .rates = div4_rates },
	{ .parent = NULL }
};

/*
 * REVISIT: Are these in DPLL power domain or CM power domain? docs
 * may be inconsistent here?
 */
static struct clk dpll1_fck = {
	.name		= "dpll1_fck",
	.ops		= &clkops_null,
	.parent		= &core_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(MPU_MOD, OMAP3430_CM_CLKSEL1_PLL),
	.clksel_mask	= OMAP3430_MPU_CLK_SRC_MASK,
	.clksel		= div4_core_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mpu_ck = {
	.name		= "mpu_ck",
	.ops		= &clkops_null,
	.parent		= &dpll1_x2m2_ck,
	.clkdm_name	= "mpu_clkdm",
	.recalc		= &followparent_recalc,
};

/* arm_fck is divided by two when DPLL1 locked; otherwise, passthrough mpu_ck */
static const struct clksel_rate arm_fck_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel arm_fck_clksel[] = {
	{ .parent = &mpu_ck, .rates = arm_fck_rates },
	{ .parent = NULL }
};

static struct clk arm_fck = {
	.name		= "arm_fck",
	.ops		= &clkops_null,
	.parent		= &mpu_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(MPU_MOD, OMAP3430_CM_IDLEST_PLL),
	.clksel_mask	= OMAP3430_ST_MPU_CLK_MASK,
	.clksel		= arm_fck_clksel,
	.clkdm_name	= "mpu_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* XXX What about neon_clkdm ? */

/*
 * REVISIT: This clock is never specifically defined in the 3430 TRM,
 * although it is referenced - so this is a guess
 */
static struct clk emu_mpu_alwon_ck = {
	.name		= "emu_mpu_alwon_ck",
	.ops		= &clkops_null,
	.parent		= &mpu_ck,
	.recalc		= &followparent_recalc,
};

static struct clk dpll2_fck = {
	.name		= "dpll2_fck",
	.ops		= &clkops_null,
	.parent		= &core_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_CLKSEL1_PLL),
	.clksel_mask	= OMAP3430_IVA2_CLK_SRC_MASK,
	.clksel		= div4_core_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk iva2_ck = {
	.name		= "iva2_ck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &dpll2_m2_ck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_CM_FCLKEN_IVA2_EN_IVA2_SHIFT,
	.clkdm_name	= "iva2_clkdm",
	.recalc		= &followparent_recalc,
};

/* Common interface clocks */

static const struct clksel div2_core_clksel[] = {
	{ .parent = &core_ck, .rates = div2_rates },
	{ .parent = NULL }
};

static struct clk l3_ick = {
	.name		= "l3_ick",
	.ops		= &clkops_null,
	.parent		= &core_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_L3_MASK,
	.clksel		= div2_core_clksel,
	.clkdm_name	= "core_l3_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel div2_l3_clksel[] = {
	{ .parent = &l3_ick, .rates = div2_rates },
	{ .parent = NULL }
};

static struct clk l4_ick = {
	.name		= "l4_ick",
	.ops		= &clkops_null,
	.parent		= &l3_ick,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_L4_MASK,
	.clksel		= div2_l3_clksel,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &omap2_clksel_recalc,

};

static const struct clksel div2_l4_clksel[] = {
	{ .parent = &l4_ick, .rates = div2_rates },
	{ .parent = NULL }
};

static struct clk rm_ick = {
	.name		= "rm_ick",
	.ops		= &clkops_null,
	.parent		= &l4_ick,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_RM_MASK,
	.clksel		= div2_l4_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/* GFX power domain */

/* GFX clocks are in 3430ES1 only. 3430ES2 and later uses the SGX instead */

static const struct clksel gfx_l3_clksel[] = {
	{ .parent = &l3_ick, .rates = gfx_l3_rates },
	{ .parent = NULL }
};

/*
 * Virtual parent clock for gfx_l3_ick and gfx_l3_fck
 * This interface clock does not have a CM_AUTOIDLE bit
 */
static struct clk gfx_l3_ck = {
	.name		= "gfx_l3_ck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &l3_ick,
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_ICLKEN),
	.enable_bit	= OMAP_EN_GFX_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gfx_l3_fck = {
	.name		= "gfx_l3_fck",
	.ops		= &clkops_null,
	.parent		= &gfx_l3_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP_CLKSEL_GFX_MASK,
	.clksel		= gfx_l3_clksel,
	.clkdm_name	= "gfx_3430es1_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gfx_l3_ick = {
	.name		= "gfx_l3_ick",
	.ops		= &clkops_null,
	.parent		= &gfx_l3_ck,
	.clkdm_name	= "gfx_3430es1_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gfx_cg1_ck = {
	.name		= "gfx_cg1_ck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &gfx_l3_fck, /* REVISIT: correct? */
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430ES1_EN_2D_SHIFT,
	.clkdm_name	= "gfx_3430es1_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gfx_cg2_ck = {
	.name		= "gfx_cg2_ck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &gfx_l3_fck, /* REVISIT: correct? */
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430ES1_EN_3D_SHIFT,
	.clkdm_name	= "gfx_3430es1_clkdm",
	.recalc		= &followparent_recalc,
};

/* SGX power domain - 3430ES2 only */

static const struct clksel_rate sgx_core_rates[] = {
	{ .div = 2, .val = 5, .flags = RATE_IN_36XX },
	{ .div = 3, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 4, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 6, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel_rate sgx_192m_rates[] = {
	{ .div = 1,  .val = 4, .flags = RATE_IN_36XX },
	{ .div = 0 },
};

static const struct clksel_rate sgx_corex2_rates[] = {
	{ .div = 3, .val = 6, .flags = RATE_IN_36XX },
	{ .div = 5, .val = 7, .flags = RATE_IN_36XX },
	{ .div = 0 },
};

static const struct clksel_rate sgx_96m_rates[] = {
	{ .div = 1,  .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel sgx_clksel[] = {
	{ .parent = &core_ck,	 .rates = sgx_core_rates },
	{ .parent = &cm_96m_fck, .rates = sgx_96m_rates },
	{ .parent = &omap_192m_alwon_fck, .rates = sgx_192m_rates },
	{ .parent = &corex2_fck, .rates = sgx_corex2_rates },
	{ .parent = NULL }
};

static struct clk sgx_fck = {
	.name		= "sgx_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430ES2_SGX_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430ES2_CM_FCLKEN_SGX_EN_SGX_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430ES2_SGX_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430ES2_CLKSEL_SGX_MASK,
	.clksel		= sgx_clksel,
	.clkdm_name	= "sgx_clkdm",
	.recalc		= &omap2_clksel_recalc,
	.set_rate	= &omap2_clksel_set_rate,
	.round_rate	= &omap2_clksel_round_rate
};

/* This interface clock does not have a CM_AUTOIDLE bit */
static struct clk sgx_ick = {
	.name		= "sgx_ick",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &l3_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430ES2_SGX_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430ES2_CM_ICLKEN_SGX_EN_SGX_SHIFT,
	.clkdm_name	= "sgx_clkdm",
	.recalc		= &followparent_recalc,
};

/* CORE power domain */

static struct clk d2d_26m_fck = {
	.name		= "d2d_26m_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &sys_ck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430ES1_EN_D2D_SHIFT,
	.clkdm_name	= "d2d_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk modem_fck = {
	.name		= "modem_fck",
	.ops		= &clkops_omap2_mdmclk_dflt_wait,
	.parent		= &sys_ck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MODEM_SHIFT,
	.clkdm_name	= "d2d_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk sad2d_ick = {
	.name		= "sad2d_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l3_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_SAD2D_SHIFT,
	.clkdm_name	= "d2d_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mad2d_ick = {
	.name		= "mad2d_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l3_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP3430_EN_MAD2D_SHIFT,
	.clkdm_name	= "d2d_clkdm",
	.recalc		= &followparent_recalc,
};

static const struct clksel omap343x_gpt_clksel[] = {
	{ .parent = &omap_32k_fck, .rates = gpt_32k_rates },
	{ .parent = &sys_ck,	   .rates = gpt_sys_rates },
	{ .parent = NULL}
};

static struct clk gpt10_fck = {
	.name		= "gpt10_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &sys_ck,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_GPT10_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT10_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt11_fck = {
	.name		= "gpt11_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &sys_ck,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_GPT11_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT11_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk cpefuse_fck = {
	.name		= "cpefuse_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &sys_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP3430ES2_CM_FCLKEN3),
	.enable_bit	= OMAP3430ES2_EN_CPEFUSE_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk ts_fck = {
	.name		= "ts_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &omap_32k_fck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP3430ES2_CM_FCLKEN3),
	.enable_bit	= OMAP3430ES2_EN_TS_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk usbtll_fck = {
	.name		= "usbtll_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &dpll5_m2_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP3430ES2_CM_FCLKEN3),
	.enable_bit	= OMAP3430ES2_EN_USBTLL_SHIFT,
	.recalc		= &followparent_recalc,
};

/* CORE 96M FCLK-derived clocks */

static struct clk core_96m_fck = {
	.name		= "core_96m_fck",
	.ops		= &clkops_null,
	.parent		= &omap_96m_fck,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mmchs3_fck = {
	.name		= "mmchs3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_96m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430ES2_EN_MMC3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mmchs2_fck = {
	.name		= "mmchs2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_96m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MMC2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mspro_fck = {
	.name		= "mspro_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_96m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MSPRO_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mmchs1_fck = {
	.name		= "mmchs1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_96m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MMC1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk i2c3_fck = {
	.name		= "i2c3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_96m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_I2C3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk i2c2_fck = {
	.name		= "i2c2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_96m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_I2C2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk i2c1_fck = {
	.name		= "i2c1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_96m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_I2C1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

/*
 * MCBSP 1 & 5 get their 96MHz clock from core_96m_fck;
 * MCBSP 2, 3, 4 get their 96MHz clock from per_96m_fck.
 */
static const struct clksel_rate common_mcbsp_96m_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate common_mcbsp_mcbsp_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel mcbsp_15_clksel[] = {
	{ .parent = &core_96m_fck, .rates = common_mcbsp_96m_rates },
	{ .parent = &mcbsp_clks,   .rates = common_mcbsp_mcbsp_rates },
	{ .parent = NULL }
};

static struct clk mcbsp5_fck = {
	.name		= "mcbsp5_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MCBSP5_SHIFT,
	.clksel_reg	= OMAP343X_CTRL_REGADDR(OMAP343X_CONTROL_DEVCONF1),
	.clksel_mask	= OMAP2_MCBSP5_CLKS_MASK,
	.clksel		= mcbsp_15_clksel,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mcbsp1_fck = {
	.name		= "mcbsp1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MCBSP1_SHIFT,
	.clksel_reg	= OMAP343X_CTRL_REGADDR(OMAP2_CONTROL_DEVCONF0),
	.clksel_mask	= OMAP2_MCBSP1_CLKS_MASK,
	.clksel		= mcbsp_15_clksel,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* CORE_48M_FCK-derived clocks */

static struct clk core_48m_fck = {
	.name		= "core_48m_fck",
	.ops		= &clkops_null,
	.parent		= &omap_48m_fck,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mcspi4_fck = {
	.name		= "mcspi4_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_48m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI4_SHIFT,
	.recalc		= &followparent_recalc,
	.clkdm_name	= "core_l4_clkdm",
};

static struct clk mcspi3_fck = {
	.name		= "mcspi3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_48m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI3_SHIFT,
	.recalc		= &followparent_recalc,
	.clkdm_name	= "core_l4_clkdm",
};

static struct clk mcspi2_fck = {
	.name		= "mcspi2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_48m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI2_SHIFT,
	.recalc		= &followparent_recalc,
	.clkdm_name	= "core_l4_clkdm",
};

static struct clk mcspi1_fck = {
	.name		= "mcspi1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_48m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI1_SHIFT,
	.recalc		= &followparent_recalc,
	.clkdm_name	= "core_l4_clkdm",
};

static struct clk uart2_fck = {
	.name		= "uart2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_48m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_UART2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk uart1_fck = {
	.name		= "uart1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_48m_fck,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_UART1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk fshostusb_fck = {
	.name		= "fshostusb_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_48m_fck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430ES1_EN_FSHOSTUSB_SHIFT,
	.recalc		= &followparent_recalc,
};

/* CORE_12M_FCK based clocks */

static struct clk core_12m_fck = {
	.name		= "core_12m_fck",
	.ops		= &clkops_null,
	.parent		= &omap_12m_fck,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk hdq_fck = {
	.name		= "hdq_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_12m_fck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_HDQ_SHIFT,
	.recalc		= &followparent_recalc,
};

/* DPLL3-derived clock */

static const struct clksel_rate ssi_ssr_corex2_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 3, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 4, .val = 4, .flags = RATE_IN_3XXX },
	{ .div = 6, .val = 6, .flags = RATE_IN_3XXX },
	{ .div = 8, .val = 8, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel ssi_ssr_clksel[] = {
	{ .parent = &corex2_fck, .rates = ssi_ssr_corex2_rates },
	{ .parent = NULL }
};

static struct clk ssi_ssr_fck_3430es1 = {
	.name		= "ssi_ssr_fck",
	.ops		= &clkops_omap2_dflt,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_SSI_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_SSI_MASK,
	.clksel		= ssi_ssr_clksel,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk ssi_ssr_fck_3430es2 = {
	.name		= "ssi_ssr_fck",
	.ops		= &clkops_omap3430es2_ssi_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_SSI_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_SSI_MASK,
	.clksel		= ssi_ssr_clksel,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk ssi_sst_fck_3430es1 = {
	.name		= "ssi_sst_fck",
	.ops		= &clkops_null,
	.parent		= &ssi_ssr_fck_3430es1,
	.fixed_div	= 2,
	.recalc		= &omap_fixed_divisor_recalc,
};

static struct clk ssi_sst_fck_3430es2 = {
	.name		= "ssi_sst_fck",
	.ops		= &clkops_null,
	.parent		= &ssi_ssr_fck_3430es2,
	.fixed_div	= 2,
	.recalc		= &omap_fixed_divisor_recalc,
};



/* CORE_L3_ICK based clocks */

/*
 * XXX must add clk_enable/clk_disable for these if standard code won't
 * handle it
 */
static struct clk core_l3_ick = {
	.name		= "core_l3_ick",
	.ops		= &clkops_null,
	.parent		= &l3_ick,
	.clkdm_name	= "core_l3_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk hsotgusb_ick_3430es1 = {
	.name		= "hsotgusb_ick",
	.ops		= &clkops_omap2_iclk_dflt,
	.parent		= &core_l3_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_HSOTGUSB_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk hsotgusb_ick_3430es2 = {
	.name		= "hsotgusb_ick",
	.ops		= &clkops_omap3430es2_iclk_hsotgusb_wait,
	.parent		= &core_l3_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_HSOTGUSB_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
	.recalc		= &followparent_recalc,
};

/* This interface clock does not have a CM_AUTOIDLE bit */
static struct clk sdrc_ick = {
	.name		= "sdrc_ick",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_l3_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_SDRC_SHIFT,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "core_l3_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpmc_fck = {
	.name		= "gpmc_fck",
	.ops		= &clkops_null,
	.parent		= &core_l3_ick,
	.flags		= ENABLE_ON_INIT, /* huh? */
	.clkdm_name	= "core_l3_clkdm",
	.recalc		= &followparent_recalc,
};

/* SECURITY_L3_ICK based clocks */

static struct clk security_l3_ick = {
	.name		= "security_l3_ick",
	.ops		= &clkops_null,
	.parent		= &l3_ick,
	.recalc		= &followparent_recalc,
};

static struct clk pka_ick = {
	.name		= "pka_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &security_l3_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP3430_EN_PKA_SHIFT,
	.recalc		= &followparent_recalc,
};

/* CORE_L4_ICK based clocks */

static struct clk core_l4_ick = {
	.name		= "core_l4_ick",
	.ops		= &clkops_null,
	.parent		= &l4_ick,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk usbtll_ick = {
	.name		= "usbtll_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP3430ES2_EN_USBTLL_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mmchs3_ick = {
	.name		= "mmchs3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430ES2_EN_MMC3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

/* Intersystem Communication Registers - chassis mode only */
static struct clk icr_ick = {
	.name		= "icr_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_ICR_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk aes2_ick = {
	.name		= "aes2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_AES2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk sha12_ick = {
	.name		= "sha12_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_SHA12_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk des2_ick = {
	.name		= "des2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_DES2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mmchs2_ick = {
	.name		= "mmchs2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MMC2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mmchs1_ick = {
	.name		= "mmchs1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MMC1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mspro_ick = {
	.name		= "mspro_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MSPRO_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk hdq_ick = {
	.name		= "hdq_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_HDQ_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mcspi4_ick = {
	.name		= "mcspi4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI4_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mcspi3_ick = {
	.name		= "mcspi3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mcspi2_ick = {
	.name		= "mcspi2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mcspi1_ick = {
	.name		= "mcspi1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk i2c3_ick = {
	.name		= "i2c3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_I2C3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk i2c2_ick = {
	.name		= "i2c2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_I2C2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk i2c1_ick = {
	.name		= "i2c1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_I2C1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk uart2_ick = {
	.name		= "uart2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_UART2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk uart1_ick = {
	.name		= "uart1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_UART1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt11_ick = {
	.name		= "gpt11_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_GPT11_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt10_ick = {
	.name		= "gpt10_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_GPT10_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp5_ick = {
	.name		= "mcbsp5_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCBSP5_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp1_ick = {
	.name		= "mcbsp1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCBSP1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk fac_ick = {
	.name		= "fac_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430ES1_EN_FAC_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mailboxes_ick = {
	.name		= "mailboxes_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MAILBOXES_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk omapctrl_ick = {
	.name		= "omapctrl_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_OMAPCTRL_SHIFT,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

/* SSI_L4_ICK based clocks */

static struct clk ssi_l4_ick = {
	.name		= "ssi_l4_ick",
	.ops		= &clkops_null,
	.parent		= &l4_ick,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk ssi_ick_3430es1 = {
	.name		= "ssi_ick",
	.ops		= &clkops_omap2_iclk_dflt,
	.parent		= &ssi_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_SSI_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk ssi_ick_3430es2 = {
	.name		= "ssi_ick",
	.ops		= &clkops_omap3430es2_iclk_ssi_wait,
	.parent		= &ssi_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_SSI_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

/* REVISIT: Technically the TRM claims that this is CORE_CLK based,
 * but l4_ick makes more sense to me */

static const struct clksel usb_l4_clksel[] = {
	{ .parent = &l4_ick, .rates = div2_rates },
	{ .parent = NULL },
};

static struct clk usb_l4_ick = {
	.name		= "usb_l4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ick,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430ES1_EN_FSHOSTUSB_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430ES1_CLKSEL_FSHOSTUSB_MASK,
	.clksel		= usb_l4_clksel,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* SECURITY_L4_ICK2 based clocks */

static struct clk security_l4_ick2 = {
	.name		= "security_l4_ick2",
	.ops		= &clkops_null,
	.parent		= &l4_ick,
	.recalc		= &followparent_recalc,
};

static struct clk aes1_ick = {
	.name		= "aes1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &security_l4_ick2,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP3430_EN_AES1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk rng_ick = {
	.name		= "rng_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &security_l4_ick2,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP3430_EN_RNG_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk sha11_ick = {
	.name		= "sha11_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &security_l4_ick2,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP3430_EN_SHA11_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk des1_ick = {
	.name		= "des1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &security_l4_ick2,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP3430_EN_DES1_SHIFT,
	.recalc		= &followparent_recalc,
};

/* DSS */
static struct clk dss1_alwon_fck_3430es1 = {
	.name		= "dss1_alwon_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &dpll4_m4x2_ck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_DSS1_SHIFT,
	.clkdm_name	= "dss_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk dss1_alwon_fck_3430es2 = {
	.name		= "dss1_alwon_fck",
	.ops		= &clkops_omap3430es2_dss_usbhost_wait,
	.parent		= &dpll4_m4x2_ck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_DSS1_SHIFT,
	.clkdm_name	= "dss_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk dss_tv_fck = {
	.name		= "dss_tv_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &omap_54m_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_TV_SHIFT,
	.clkdm_name	= "dss_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk dss_96m_fck = {
	.name		= "dss_96m_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &omap_96m_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_TV_SHIFT,
	.clkdm_name	= "dss_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk dss2_alwon_fck = {
	.name		= "dss2_alwon_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &sys_ck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_DSS2_SHIFT,
	.clkdm_name	= "dss_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk dss_ick_3430es1 = {
	/* Handles both L3 and L4 clocks */
	.name		= "dss_ick",
	.ops		= &clkops_omap2_iclk_dflt,
	.parent		= &l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_CM_ICLKEN_DSS_EN_DSS_SHIFT,
	.clkdm_name	= "dss_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk dss_ick_3430es2 = {
	/* Handles both L3 and L4 clocks */
	.name		= "dss_ick",
	.ops		= &clkops_omap3430es2_iclk_dss_usbhost_wait,
	.parent		= &l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_CM_ICLKEN_DSS_EN_DSS_SHIFT,
	.clkdm_name	= "dss_clkdm",
	.recalc		= &followparent_recalc,
};

/* CAM */

static struct clk cam_mclk = {
	.name		= "cam_mclk",
	.ops		= &clkops_omap2_dflt,
	.parent		= &dpll4_m5x2_ck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_CAM_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_CAM_SHIFT,
	.clkdm_name	= "cam_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk cam_ick = {
	/* Handles both L3 and L4 clocks */
	.name		= "cam_ick",
	.ops		= &clkops_omap2_iclk_dflt,
	.parent		= &l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_CAM_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_CAM_SHIFT,
	.clkdm_name	= "cam_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk csi2_96m_fck = {
	.name		= "csi2_96m_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &core_96m_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_CAM_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_CSI2_SHIFT,
	.clkdm_name	= "cam_clkdm",
	.recalc		= &followparent_recalc,
};

/* USBHOST - 3430ES2 only */

static struct clk usbhost_120m_fck = {
	.name		= "usbhost_120m_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &dpll5_m2_ck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430ES2_USBHOST_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430ES2_EN_USBHOST2_SHIFT,
	.clkdm_name	= "usbhost_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk usbhost_48m_fck = {
	.name		= "usbhost_48m_fck",
	.ops		= &clkops_omap3430es2_dss_usbhost_wait,
	.parent		= &omap_48m_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430ES2_USBHOST_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430ES2_EN_USBHOST1_SHIFT,
	.clkdm_name	= "usbhost_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk usbhost_ick = {
	/* Handles both L3 and L4 clocks */
	.name		= "usbhost_ick",
	.ops		= &clkops_omap3430es2_iclk_dss_usbhost_wait,
	.parent		= &l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430ES2_USBHOST_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430ES2_EN_USBHOST_SHIFT,
	.clkdm_name	= "usbhost_clkdm",
	.recalc		= &followparent_recalc,
};

/* WKUP */

static const struct clksel_rate usim_96m_rates[] = {
	{ .div = 2,  .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 4,  .val = 4, .flags = RATE_IN_3XXX },
	{ .div = 8,  .val = 5, .flags = RATE_IN_3XXX },
	{ .div = 10, .val = 6, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel_rate usim_120m_rates[] = {
	{ .div = 4,  .val = 7,	.flags = RATE_IN_3XXX },
	{ .div = 8,  .val = 8,	.flags = RATE_IN_3XXX },
	{ .div = 16, .val = 9,	.flags = RATE_IN_3XXX },
	{ .div = 20, .val = 10, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel usim_clksel[] = {
	{ .parent = &omap_96m_fck,	.rates = usim_96m_rates },
	{ .parent = &dpll5_m2_ck,	.rates = usim_120m_rates },
	{ .parent = &sys_ck,		.rates = div2_rates },
	{ .parent = NULL },
};

/* 3430ES2 only */
static struct clk usim_fck = {
	.name		= "usim_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430ES2_EN_USIMOCP_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430ES2_CLKSEL_USIMOCP_MASK,
	.clksel		= usim_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/* XXX should gpt1's clksel have wkup_32k_fck as the 32k opt? */
static struct clk gpt1_fck = {
	.name		= "gpt1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPT1_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT1_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk wkup_32k_fck = {
	.name		= "wkup_32k_fck",
	.ops		= &clkops_null,
	.parent		= &omap_32k_fck,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio1_dbck = {
	.name		= "gpio1_dbck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &wkup_32k_fck,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk wdt2_fck = {
	.name		= "wdt2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &wkup_32k_fck,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_WDT2_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk wkup_l4_ick = {
	.name		= "wkup_l4_ick",
	.ops		= &clkops_null,
	.parent		= &sys_ck,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

/* 3430ES2 only */
/* Never specifically named in the TRM, so we have to infer a likely name */
static struct clk usim_ick = {
	.name		= "usim_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wkup_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430ES2_EN_USIMOCP_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk wdt2_ick = {
	.name		= "wdt2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wkup_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_WDT2_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk wdt1_ick = {
	.name		= "wdt1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wkup_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_WDT1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio1_ick = {
	.name		= "gpio1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wkup_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk omap_32ksync_ick = {
	.name		= "omap_32ksync_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wkup_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_32KSYNC_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

/* XXX This clock no longer exists in 3430 TRM rev F */
static struct clk gpt12_ick = {
	.name		= "gpt12_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wkup_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT12_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt1_ick = {
	.name		= "gpt1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wkup_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};



/* PER clock domain */

static struct clk per_96m_fck = {
	.name		= "per_96m_fck",
	.ops		= &clkops_null,
	.parent		= &omap_96m_alwon_fck,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk per_48m_fck = {
	.name		= "per_48m_fck",
	.ops		= &clkops_null,
	.parent		= &omap_48m_fck,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk uart3_fck = {
	.name		= "uart3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &per_48m_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_UART3_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk uart4_fck = {
	.name		= "uart4_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &per_48m_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3630_EN_UART4_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk uart4_fck_am35xx = {
	.name           = "uart4_fck",
	.ops            = &clkops_omap2_dflt_wait,
	.parent         = &per_48m_fck,
	.enable_reg     = OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit     = OMAP3430_EN_UART4_SHIFT,
	.clkdm_name     = "core_l4_clkdm",
	.recalc         = &followparent_recalc,
};

static struct clk gpt2_fck = {
	.name		= "gpt2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPT2_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT2_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt3_fck = {
	.name		= "gpt3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPT3_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT3_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt4_fck = {
	.name		= "gpt4_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPT4_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT4_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt5_fck = {
	.name		= "gpt5_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPT5_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT5_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt6_fck = {
	.name		= "gpt6_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPT6_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT6_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt7_fck = {
	.name		= "gpt7_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPT7_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT7_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt8_fck = {
	.name		= "gpt8_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPT8_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT8_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt9_fck = {
	.name		= "gpt9_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPT9_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3430_CLKSEL_GPT9_MASK,
	.clksel		= omap343x_gpt_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk per_32k_alwon_fck = {
	.name		= "per_32k_alwon_fck",
	.ops		= &clkops_null,
	.parent		= &omap_32k_fck,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio6_dbck = {
	.name		= "gpio6_dbck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &per_32k_alwon_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO6_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio5_dbck = {
	.name		= "gpio5_dbck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &per_32k_alwon_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO5_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio4_dbck = {
	.name		= "gpio4_dbck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &per_32k_alwon_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO4_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio3_dbck = {
	.name		= "gpio3_dbck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &per_32k_alwon_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO3_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio2_dbck = {
	.name		= "gpio2_dbck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &per_32k_alwon_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO2_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk wdt3_fck = {
	.name		= "wdt3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &per_32k_alwon_fck,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_WDT3_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk per_l4_ick = {
	.name		= "per_l4_ick",
	.ops		= &clkops_null,
	.parent		= &l4_ick,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio6_ick = {
	.name		= "gpio6_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO6_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio5_ick = {
	.name		= "gpio5_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO5_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio4_ick = {
	.name		= "gpio4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO4_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio3_ick = {
	.name		= "gpio3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO3_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpio2_ick = {
	.name		= "gpio2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO2_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk wdt3_ick = {
	.name		= "wdt3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_WDT3_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk uart3_ick = {
	.name		= "uart3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_UART3_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk uart4_ick = {
	.name		= "uart4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3630_EN_UART4_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt9_ick = {
	.name		= "gpt9_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT9_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt8_ick = {
	.name		= "gpt8_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT8_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt7_ick = {
	.name		= "gpt7_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT7_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt6_ick = {
	.name		= "gpt6_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT6_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt5_ick = {
	.name		= "gpt5_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT5_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt4_ick = {
	.name		= "gpt4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT4_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt3_ick = {
	.name		= "gpt3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT3_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk gpt2_ick = {
	.name		= "gpt2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT2_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp2_ick = {
	.name		= "mcbsp2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_MCBSP2_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp3_ick = {
	.name		= "mcbsp3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_MCBSP3_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp4_ick = {
	.name		= "mcbsp4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &per_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_MCBSP4_SHIFT,
	.clkdm_name	= "per_clkdm",
	.recalc		= &followparent_recalc,
};

static const struct clksel mcbsp_234_clksel[] = {
	{ .parent = &per_96m_fck,  .rates = common_mcbsp_96m_rates },
	{ .parent = &mcbsp_clks,   .rates = common_mcbsp_mcbsp_rates },
	{ .parent = NULL }
};

static struct clk mcbsp2_fck = {
	.name		= "mcbsp2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_MCBSP2_SHIFT,
	.clksel_reg	= OMAP343X_CTRL_REGADDR(OMAP2_CONTROL_DEVCONF0),
	.clksel_mask	= OMAP2_MCBSP2_CLKS_MASK,
	.clksel		= mcbsp_234_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mcbsp3_fck = {
	.name		= "mcbsp3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_MCBSP3_SHIFT,
	.clksel_reg	= OMAP343X_CTRL_REGADDR(OMAP343X_CONTROL_DEVCONF1),
	.clksel_mask	= OMAP2_MCBSP3_CLKS_MASK,
	.clksel		= mcbsp_234_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mcbsp4_fck = {
	.name		= "mcbsp4_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.init		= &omap2_init_clksel_parent,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_MCBSP4_SHIFT,
	.clksel_reg	= OMAP343X_CTRL_REGADDR(OMAP343X_CONTROL_DEVCONF1),
	.clksel_mask	= OMAP2_MCBSP4_CLKS_MASK,
	.clksel		= mcbsp_234_clksel,
	.clkdm_name	= "per_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* EMU clocks */

/* More information: ARM Cortex-A8 Technical Reference Manual, sect 10.1 */

static const struct clksel_rate emu_src_sys_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel_rate emu_src_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel_rate emu_src_per_rates[] = {
	{ .div = 1, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel_rate emu_src_mpu_rates[] = {
	{ .div = 1, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel emu_src_clksel[] = {
	{ .parent = &sys_ck,		.rates = emu_src_sys_rates },
	{ .parent = &emu_core_alwon_ck, .rates = emu_src_core_rates },
	{ .parent = &emu_per_alwon_ck,	.rates = emu_src_per_rates },
	{ .parent = &emu_mpu_alwon_ck,	.rates = emu_src_mpu_rates },
	{ .parent = NULL },
};

/*
 * Like the clkout_src clocks, emu_src_clk is a virtual clock, existing only
 * to switch the source of some of the EMU clocks.
 * XXX Are there CLKEN bits for these EMU clks?
 */
static struct clk emu_src_ck = {
	.name		= "emu_src_ck",
	.ops		= &clkops_null,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_MUX_CTRL_MASK,
	.clksel		= emu_src_clksel,
	.clkdm_name	= "emu_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel_rate pclk_emu_rates[] = {
	{ .div = 2, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 3, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 4, .val = 4, .flags = RATE_IN_3XXX },
	{ .div = 6, .val = 6, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel pclk_emu_clksel[] = {
	{ .parent = &emu_src_ck, .rates = pclk_emu_rates },
	{ .parent = NULL },
};

static struct clk pclk_fck = {
	.name		= "pclk_fck",
	.ops		= &clkops_null,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_CLKSEL_PCLK_MASK,
	.clksel		= pclk_emu_clksel,
	.clkdm_name	= "emu_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel_rate pclkx2_emu_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 3, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel pclkx2_emu_clksel[] = {
	{ .parent = &emu_src_ck, .rates = pclkx2_emu_rates },
	{ .parent = NULL },
};

static struct clk pclkx2_fck = {
	.name		= "pclkx2_fck",
	.ops		= &clkops_null,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_CLKSEL_PCLKX2_MASK,
	.clksel		= pclkx2_emu_clksel,
	.clkdm_name	= "emu_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel atclk_emu_clksel[] = {
	{ .parent = &emu_src_ck, .rates = div2_rates },
	{ .parent = NULL },
};

static struct clk atclk_fck = {
	.name		= "atclk_fck",
	.ops		= &clkops_null,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_CLKSEL_ATCLK_MASK,
	.clksel		= atclk_emu_clksel,
	.clkdm_name	= "emu_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static struct clk traceclk_src_fck = {
	.name		= "traceclk_src_fck",
	.ops		= &clkops_null,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_TRACE_MUX_CTRL_MASK,
	.clksel		= emu_src_clksel,
	.clkdm_name	= "emu_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel_rate traceclk_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 4, .val = 4, .flags = RATE_IN_3XXX },
	{ .div = 0 },
};

static const struct clksel traceclk_clksel[] = {
	{ .parent = &traceclk_src_fck, .rates = traceclk_rates },
	{ .parent = NULL },
};

static struct clk traceclk_fck = {
	.name		= "traceclk_fck",
	.ops		= &clkops_null,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_CLKSEL_TRACECLK_MASK,
	.clksel		= traceclk_clksel,
	.clkdm_name	= "emu_clkdm",
	.recalc		= &omap2_clksel_recalc,
};

/* SR clocks */

/* SmartReflex fclk (VDD1) */
static struct clk sr1_fck = {
	.name		= "sr1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &sys_ck,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_SR1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

/* SmartReflex fclk (VDD2) */
static struct clk sr2_fck = {
	.name		= "sr2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &sys_ck,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_SR2_SHIFT,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk sr_l4_ick = {
	.name		= "sr_l4_ick",
	.ops		= &clkops_null, /* RMK: missing? */
	.parent		= &l4_ick,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

/* SECURE_32K_FCK clocks */

static struct clk gpt12_fck = {
	.name		= "gpt12_fck",
	.ops		= &clkops_null,
	.parent		= &secure_32k_fck,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk wdt1_fck = {
	.name		= "wdt1_fck",
	.ops		= &clkops_null,
	.parent		= &secure_32k_fck,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

/* Clocks for AM35XX */
static struct clk ipss_ick = {
	.name		= "ipss_ick",
	.ops		= &clkops_am35xx_ipss_wait,
	.parent		= &core_l3_ick,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= AM35XX_EN_IPSS_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk emac_ick = {
	.name		= "emac_ick",
	.ops		= &clkops_am35xx_ipss_module_wait,
	.parent		= &ipss_ick,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_CPGMAC_VBUSP_CLK_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk rmii_ck = {
	.name		= "rmii_ck",
	.ops		= &clkops_null,
	.rate		= 50000000,
};

static struct clk emac_fck = {
	.name		= "emac_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &rmii_ck,
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_CPGMAC_FCLK_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk hsotgusb_ick_am35xx = {
	.name		= "hsotgusb_ick",
	.ops		= &clkops_am35xx_ipss_module_wait,
	.parent		= &ipss_ick,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_USBOTG_VBUSP_CLK_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk hsotgusb_fck_am35xx = {
	.name		= "hsotgusb_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &sys_ck,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_USBOTG_FCLK_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk hecc_ck = {
	.name		= "hecc_ck",
	.ops		= &clkops_am35xx_ipss_module_wait,
	.parent		= &sys_ck,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_HECC_VBUSP_CLK_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk vpfe_ick = {
	.name		= "vpfe_ick",
	.ops		= &clkops_am35xx_ipss_module_wait,
	.parent		= &ipss_ick,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_VPFE_VBUSP_CLK_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk pclk_ck = {
	.name		= "pclk_ck",
	.ops		= &clkops_null,
	.rate		= 27000000,
};

static struct clk vpfe_fck = {
	.name		= "vpfe_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &pclk_ck,
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_VPFE_FCLK_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * The UART1/2 functional clock acts as the functional
 * clock for UART4. No separate fclk control available.
 */
static struct clk uart4_ick_am35xx = {
	.name		= "uart4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l4_ick,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= AM35XX_EN_UART4_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
	.recalc		= &followparent_recalc,
};

static struct clk dummy_apb_pclk = {
	.name		= "apb_pclk",
	.ops		= &clkops_null,
};

/*
 * clkdev
 */

/* XXX At some point we should rename this file to clock3xxx_data.c */
static struct omap_clk omap3xxx_clks[] = {
	CLK(NULL,	"apb_pclk",	&dummy_apb_pclk,	CK_3XXX),
	CLK(NULL,	"omap_32k_fck",	&omap_32k_fck,	CK_3XXX),
	CLK(NULL,	"virt_12m_ck",	&virt_12m_ck,	CK_3XXX),
	CLK(NULL,	"virt_13m_ck",	&virt_13m_ck,	CK_3XXX),
	CLK(NULL,	"virt_16_8m_ck", &virt_16_8m_ck, CK_3430ES2PLUS | CK_AM35XX  | CK_36XX),
	CLK(NULL,	"virt_19_2m_ck", &virt_19_2m_ck, CK_3XXX),
	CLK(NULL,	"virt_26m_ck",	&virt_26m_ck,	CK_3XXX),
	CLK(NULL,	"virt_38_4m_ck", &virt_38_4m_ck, CK_3XXX),
	CLK(NULL,	"osc_sys_ck",	&osc_sys_ck,	CK_3XXX),
	CLK(NULL,	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK(NULL,	"sys_altclk",	&sys_altclk,	CK_3XXX),
	CLK(NULL,	"mcbsp_clks",	&mcbsp_clks,	CK_3XXX),
	CLK(NULL,	"sys_clkout1",	&sys_clkout1,	CK_3XXX),
	CLK(NULL,	"dpll1_ck",	&dpll1_ck,	CK_3XXX),
	CLK(NULL,	"dpll1_x2_ck",	&dpll1_x2_ck,	CK_3XXX),
	CLK(NULL,	"dpll1_x2m2_ck", &dpll1_x2m2_ck, CK_3XXX),
	CLK(NULL,	"dpll2_ck",	&dpll2_ck,	CK_34XX | CK_36XX),
	CLK(NULL,	"dpll2_m2_ck",	&dpll2_m2_ck,	CK_34XX | CK_36XX),
	CLK(NULL,	"dpll3_ck",	&dpll3_ck,	CK_3XXX),
	CLK(NULL,	"core_ck",	&core_ck,	CK_3XXX),
	CLK(NULL,	"dpll3_x2_ck",	&dpll3_x2_ck,	CK_3XXX),
	CLK(NULL,	"dpll3_m2_ck",	&dpll3_m2_ck,	CK_3XXX),
	CLK(NULL,	"dpll3_m2x2_ck", &dpll3_m2x2_ck, CK_3XXX),
	CLK(NULL,	"dpll3_m3_ck",	&dpll3_m3_ck,	CK_3XXX),
	CLK(NULL,	"dpll3_m3x2_ck", &dpll3_m3x2_ck, CK_3XXX),
	CLK("etb",	"emu_core_alwon_ck", &emu_core_alwon_ck, CK_3XXX),
	CLK(NULL,	"dpll4_ck",	&dpll4_ck,	CK_3XXX),
	CLK(NULL,	"dpll4_x2_ck",	&dpll4_x2_ck,	CK_3XXX),
	CLK(NULL,	"omap_192m_alwon_fck", &omap_192m_alwon_fck, CK_36XX),
	CLK(NULL,	"omap_96m_alwon_fck", &omap_96m_alwon_fck, CK_3XXX),
	CLK(NULL,	"omap_96m_fck",	&omap_96m_fck,	CK_3XXX),
	CLK(NULL,	"cm_96m_fck",	&cm_96m_fck,	CK_3XXX),
	CLK(NULL,	"omap_54m_fck",	&omap_54m_fck,	CK_3XXX),
	CLK(NULL,	"omap_48m_fck",	&omap_48m_fck,	CK_3XXX),
	CLK(NULL,	"omap_12m_fck",	&omap_12m_fck,	CK_3XXX),
	CLK(NULL,	"dpll4_m2_ck",	&dpll4_m2_ck,	CK_3XXX),
	CLK(NULL,	"dpll4_m2x2_ck", &dpll4_m2x2_ck, CK_3XXX),
	CLK(NULL,	"dpll4_m3_ck",	&dpll4_m3_ck,	CK_3XXX),
	CLK(NULL,	"dpll4_m3x2_ck", &dpll4_m3x2_ck, CK_3XXX),
	CLK(NULL,	"dpll4_m4_ck",	&dpll4_m4_ck,	CK_3XXX),
	CLK(NULL,	"dpll4_m4x2_ck", &dpll4_m4x2_ck, CK_3XXX),
	CLK(NULL,	"dpll4_m5_ck",	&dpll4_m5_ck,	CK_3XXX),
	CLK(NULL,	"dpll4_m5x2_ck", &dpll4_m5x2_ck, CK_3XXX),
	CLK(NULL,	"dpll4_m6_ck",	&dpll4_m6_ck,	CK_3XXX),
	CLK(NULL,	"dpll4_m6x2_ck", &dpll4_m6x2_ck, CK_3XXX),
	CLK("etb",	"emu_per_alwon_ck", &emu_per_alwon_ck, CK_3XXX),
	CLK(NULL,	"dpll5_ck",	&dpll5_ck,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"dpll5_m2_ck",	&dpll5_m2_ck,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"clkout2_src_ck", &clkout2_src_ck, CK_3XXX),
	CLK(NULL,	"sys_clkout2",	&sys_clkout2,	CK_3XXX),
	CLK(NULL,	"corex2_fck",	&corex2_fck,	CK_3XXX),
	CLK(NULL,	"dpll1_fck",	&dpll1_fck,	CK_3XXX),
	CLK(NULL,	"mpu_ck",	&mpu_ck,	CK_3XXX),
	CLK(NULL,	"arm_fck",	&arm_fck,	CK_3XXX),
	CLK("etb",	"emu_mpu_alwon_ck", &emu_mpu_alwon_ck, CK_3XXX),
	CLK(NULL,	"dpll2_fck",	&dpll2_fck,	CK_34XX | CK_36XX),
	CLK(NULL,	"iva2_ck",	&iva2_ck,	CK_34XX | CK_36XX),
	CLK(NULL,	"l3_ick",	&l3_ick,	CK_3XXX),
	CLK(NULL,	"l4_ick",	&l4_ick,	CK_3XXX),
	CLK(NULL,	"rm_ick",	&rm_ick,	CK_3XXX),
	CLK(NULL,	"gfx_l3_ck",	&gfx_l3_ck,	CK_3430ES1),
	CLK(NULL,	"gfx_l3_fck",	&gfx_l3_fck,	CK_3430ES1),
	CLK(NULL,	"gfx_l3_ick",	&gfx_l3_ick,	CK_3430ES1),
	CLK(NULL,	"gfx_cg1_ck",	&gfx_cg1_ck,	CK_3430ES1),
	CLK(NULL,	"gfx_cg2_ck",	&gfx_cg2_ck,	CK_3430ES1),
	CLK(NULL,	"sgx_fck",	&sgx_fck,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"sgx_ick",	&sgx_ick,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"d2d_26m_fck",	&d2d_26m_fck,	CK_3430ES1),
	CLK(NULL,	"modem_fck",	&modem_fck,	CK_34XX | CK_36XX),
	CLK(NULL,	"sad2d_ick",	&sad2d_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"mad2d_ick",	&mad2d_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"gpt10_fck",	&gpt10_fck,	CK_3XXX),
	CLK(NULL,	"gpt11_fck",	&gpt11_fck,	CK_3XXX),
	CLK(NULL,	"cpefuse_fck",	&cpefuse_fck,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"ts_fck",	&ts_fck,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"usbtll_fck",	&usbtll_fck,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK("usbhs_omap",	"usbtll_fck",	&usbtll_fck,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"core_96m_fck",	&core_96m_fck,	CK_3XXX),
	CLK(NULL,	"mmchs3_fck",	&mmchs3_fck,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"mmchs2_fck",	&mmchs2_fck,	CK_3XXX),
	CLK(NULL,	"mspro_fck",	&mspro_fck,	CK_34XX | CK_36XX),
	CLK(NULL,	"mmchs1_fck",	&mmchs1_fck,	CK_3XXX),
	CLK(NULL,	"i2c3_fck",	&i2c3_fck,	CK_3XXX),
	CLK(NULL,	"i2c2_fck",	&i2c2_fck,	CK_3XXX),
	CLK(NULL,	"i2c1_fck",	&i2c1_fck,	CK_3XXX),
	CLK(NULL,	"mcbsp5_fck",	&mcbsp5_fck,	CK_3XXX),
	CLK(NULL,	"mcbsp1_fck",	&mcbsp1_fck,	CK_3XXX),
	CLK(NULL,	"core_48m_fck",	&core_48m_fck,	CK_3XXX),
	CLK(NULL,	"mcspi4_fck",	&mcspi4_fck,	CK_3XXX),
	CLK(NULL,	"mcspi3_fck",	&mcspi3_fck,	CK_3XXX),
	CLK(NULL,	"mcspi2_fck",	&mcspi2_fck,	CK_3XXX),
	CLK(NULL,	"mcspi1_fck",	&mcspi1_fck,	CK_3XXX),
	CLK(NULL,	"uart2_fck",	&uart2_fck,	CK_3XXX),
	CLK(NULL,	"uart1_fck",	&uart1_fck,	CK_3XXX),
	CLK(NULL,	"fshostusb_fck", &fshostusb_fck, CK_3430ES1),
	CLK(NULL,	"core_12m_fck",	&core_12m_fck,	CK_3XXX),
	CLK("omap_hdq.0",	"fck",	&hdq_fck,	CK_3XXX),
	CLK(NULL,	"ssi_ssr_fck",	&ssi_ssr_fck_3430es1,	CK_3430ES1),
	CLK(NULL,	"ssi_ssr_fck",	&ssi_ssr_fck_3430es2,	CK_3430ES2PLUS | CK_36XX),
	CLK(NULL,	"ssi_sst_fck",	&ssi_sst_fck_3430es1,	CK_3430ES1),
	CLK(NULL,	"ssi_sst_fck",	&ssi_sst_fck_3430es2,	CK_3430ES2PLUS | CK_36XX),
	CLK(NULL,	"core_l3_ick",	&core_l3_ick,	CK_3XXX),
	CLK("musb-omap2430",	"ick",	&hsotgusb_ick_3430es1,	CK_3430ES1),
	CLK("musb-omap2430",	"ick",	&hsotgusb_ick_3430es2,	CK_3430ES2PLUS | CK_36XX),
	CLK(NULL,	"sdrc_ick",	&sdrc_ick,	CK_3XXX),
	CLK(NULL,	"gpmc_fck",	&gpmc_fck,	CK_3XXX),
	CLK(NULL,	"security_l3_ick", &security_l3_ick, CK_34XX | CK_36XX),
	CLK(NULL,	"pka_ick",	&pka_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"core_l4_ick",	&core_l4_ick,	CK_3XXX),
	CLK(NULL,	"usbtll_ick",	&usbtll_ick,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK("usbhs_omap",	"usbtll_ick",	&usbtll_ick,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK("omap_hsmmc.2",	"ick",	&mmchs3_ick,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"icr_ick",	&icr_ick,	CK_34XX | CK_36XX),
	CLK("omap-aes",	"ick",	&aes2_ick,	CK_34XX | CK_36XX),
	CLK("omap-sham",	"ick",	&sha12_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"des2_ick",	&des2_ick,	CK_34XX | CK_36XX),
	CLK("omap_hsmmc.1",	"ick",	&mmchs2_ick,	CK_3XXX),
	CLK("omap_hsmmc.0",	"ick",	&mmchs1_ick,	CK_3XXX),
	CLK(NULL,	"mspro_ick",	&mspro_ick,	CK_34XX | CK_36XX),
	CLK("omap_hdq.0", "ick",	&hdq_ick,	CK_3XXX),
	CLK("omap2_mcspi.4", "ick",	&mcspi4_ick,	CK_3XXX),
	CLK("omap2_mcspi.3", "ick",	&mcspi3_ick,	CK_3XXX),
	CLK("omap2_mcspi.2", "ick",	&mcspi2_ick,	CK_3XXX),
	CLK("omap2_mcspi.1", "ick",	&mcspi1_ick,	CK_3XXX),
	CLK("omap_i2c.3", "ick",	&i2c3_ick,	CK_3XXX),
	CLK("omap_i2c.2", "ick",	&i2c2_ick,	CK_3XXX),
	CLK("omap_i2c.1", "ick",	&i2c1_ick,	CK_3XXX),
	CLK(NULL,	"uart2_ick",	&uart2_ick,	CK_3XXX),
	CLK(NULL,	"uart1_ick",	&uart1_ick,	CK_3XXX),
	CLK(NULL,	"gpt11_ick",	&gpt11_ick,	CK_3XXX),
	CLK(NULL,	"gpt10_ick",	&gpt10_ick,	CK_3XXX),
	CLK("omap-mcbsp.5", "ick",	&mcbsp5_ick,	CK_3XXX),
	CLK("omap-mcbsp.1", "ick",	&mcbsp1_ick,	CK_3XXX),
	CLK(NULL,	"fac_ick",	&fac_ick,	CK_3430ES1),
	CLK(NULL,	"mailboxes_ick", &mailboxes_ick, CK_34XX | CK_36XX),
	CLK(NULL,	"omapctrl_ick",	&omapctrl_ick,	CK_3XXX),
	CLK(NULL,	"ssi_l4_ick",	&ssi_l4_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"ssi_ick",	&ssi_ick_3430es1,	CK_3430ES1),
	CLK(NULL,	"ssi_ick",	&ssi_ick_3430es2,	CK_3430ES2PLUS | CK_36XX),
	CLK(NULL,	"usb_l4_ick",	&usb_l4_ick,	CK_3430ES1),
	CLK(NULL,	"security_l4_ick2", &security_l4_ick2, CK_34XX | CK_36XX),
	CLK(NULL,	"aes1_ick",	&aes1_ick,	CK_34XX | CK_36XX),
	CLK("omap_rng",	"ick",		&rng_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"sha11_ick",	&sha11_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"des1_ick",	&des1_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"dss1_alwon_fck",		&dss1_alwon_fck_3430es1, CK_3430ES1),
	CLK(NULL,	"dss1_alwon_fck",		&dss1_alwon_fck_3430es2, CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"dss_tv_fck",	&dss_tv_fck,	CK_3XXX),
	CLK(NULL,	"dss_96m_fck",	&dss_96m_fck,	CK_3XXX),
	CLK(NULL,	"dss2_alwon_fck",	&dss2_alwon_fck, CK_3XXX),
	CLK("omapdss_dss",	"ick",		&dss_ick_3430es1,	CK_3430ES1),
	CLK("omapdss_dss",	"ick",		&dss_ick_3430es2,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"cam_mclk",	&cam_mclk,	CK_34XX | CK_36XX),
	CLK(NULL,	"cam_ick",	&cam_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"csi2_96m_fck",	&csi2_96m_fck,	CK_34XX | CK_36XX),
	CLK(NULL,	"usbhost_120m_fck", &usbhost_120m_fck, CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"usbhost_48m_fck", &usbhost_48m_fck, CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK(NULL,	"usbhost_ick",	&usbhost_ick,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK("usbhs_omap",	"usbhost_ick",	&usbhost_ick,	CK_3430ES2PLUS | CK_AM35XX | CK_36XX),
	CLK("usbhs_omap",	"utmi_p1_gfclk",	&dummy_ck,	CK_3XXX),
	CLK("usbhs_omap",	"utmi_p2_gfclk",	&dummy_ck,	CK_3XXX),
	CLK("usbhs_omap",	"xclk60mhsp1_ck",	&dummy_ck,	CK_3XXX),
	CLK("usbhs_omap",	"xclk60mhsp2_ck",	&dummy_ck,	CK_3XXX),
	CLK("usbhs_omap",	"usb_host_hs_utmi_p1_clk",	&dummy_ck,	CK_3XXX),
	CLK("usbhs_omap",	"usb_host_hs_utmi_p2_clk",	&dummy_ck,	CK_3XXX),
	CLK("usbhs_omap",	"usb_tll_hs_usb_ch0_clk",	&dummy_ck,	CK_3XXX),
	CLK("usbhs_omap",	"usb_tll_hs_usb_ch1_clk",	&dummy_ck,	CK_3XXX),
	CLK("usbhs_omap",	"init_60m_fclk",	&dummy_ck,	CK_3XXX),
	CLK(NULL,	"usim_fck",	&usim_fck,	CK_3430ES2PLUS | CK_36XX),
	CLK(NULL,	"gpt1_fck",	&gpt1_fck,	CK_3XXX),
	CLK(NULL,	"wkup_32k_fck",	&wkup_32k_fck,	CK_3XXX),
	CLK(NULL,	"gpio1_dbck",	&gpio1_dbck,	CK_3XXX),
	CLK(NULL,	"wdt2_fck",		&wdt2_fck,	CK_3XXX),
	CLK(NULL,	"wkup_l4_ick",	&wkup_l4_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"usim_ick",	&usim_ick,	CK_3430ES2PLUS | CK_36XX),
	CLK("omap_wdt",	"ick",		&wdt2_ick,	CK_3XXX),
	CLK(NULL,	"wdt1_ick",	&wdt1_ick,	CK_3XXX),
	CLK(NULL,	"gpio1_ick",	&gpio1_ick,	CK_3XXX),
	CLK(NULL,	"omap_32ksync_ick", &omap_32ksync_ick, CK_3XXX),
	CLK(NULL,	"gpt12_ick",	&gpt12_ick,	CK_3XXX),
	CLK(NULL,	"gpt1_ick",	&gpt1_ick,	CK_3XXX),
	CLK(NULL,	"per_96m_fck",	&per_96m_fck,	CK_3XXX),
	CLK(NULL,	"per_48m_fck",	&per_48m_fck,	CK_3XXX),
	CLK(NULL,	"uart3_fck",	&uart3_fck,	CK_3XXX),
	CLK(NULL,	"uart4_fck",	&uart4_fck,	CK_36XX),
	CLK(NULL,	"uart4_fck",	&uart4_fck_am35xx, CK_AM35XX),
	CLK(NULL,	"gpt2_fck",	&gpt2_fck,	CK_3XXX),
	CLK(NULL,	"gpt3_fck",	&gpt3_fck,	CK_3XXX),
	CLK(NULL,	"gpt4_fck",	&gpt4_fck,	CK_3XXX),
	CLK(NULL,	"gpt5_fck",	&gpt5_fck,	CK_3XXX),
	CLK(NULL,	"gpt6_fck",	&gpt6_fck,	CK_3XXX),
	CLK(NULL,	"gpt7_fck",	&gpt7_fck,	CK_3XXX),
	CLK(NULL,	"gpt8_fck",	&gpt8_fck,	CK_3XXX),
	CLK(NULL,	"gpt9_fck",	&gpt9_fck,	CK_3XXX),
	CLK(NULL,	"per_32k_alwon_fck", &per_32k_alwon_fck, CK_3XXX),
	CLK(NULL,	"gpio6_dbck",	&gpio6_dbck,	CK_3XXX),
	CLK(NULL,	"gpio5_dbck",	&gpio5_dbck,	CK_3XXX),
	CLK(NULL,	"gpio4_dbck",	&gpio4_dbck,	CK_3XXX),
	CLK(NULL,	"gpio3_dbck",	&gpio3_dbck,	CK_3XXX),
	CLK(NULL,	"gpio2_dbck",	&gpio2_dbck,	CK_3XXX),
	CLK(NULL,	"wdt3_fck",	&wdt3_fck,	CK_3XXX),
	CLK(NULL,	"per_l4_ick",	&per_l4_ick,	CK_3XXX),
	CLK(NULL,	"gpio6_ick",	&gpio6_ick,	CK_3XXX),
	CLK(NULL,	"gpio5_ick",	&gpio5_ick,	CK_3XXX),
	CLK(NULL,	"gpio4_ick",	&gpio4_ick,	CK_3XXX),
	CLK(NULL,	"gpio3_ick",	&gpio3_ick,	CK_3XXX),
	CLK(NULL,	"gpio2_ick",	&gpio2_ick,	CK_3XXX),
	CLK(NULL,	"wdt3_ick",	&wdt3_ick,	CK_3XXX),
	CLK(NULL,	"uart3_ick",	&uart3_ick,	CK_3XXX),
	CLK(NULL,	"uart4_ick",	&uart4_ick,	CK_36XX),
	CLK(NULL,	"gpt9_ick",	&gpt9_ick,	CK_3XXX),
	CLK(NULL,	"gpt8_ick",	&gpt8_ick,	CK_3XXX),
	CLK(NULL,	"gpt7_ick",	&gpt7_ick,	CK_3XXX),
	CLK(NULL,	"gpt6_ick",	&gpt6_ick,	CK_3XXX),
	CLK(NULL,	"gpt5_ick",	&gpt5_ick,	CK_3XXX),
	CLK(NULL,	"gpt4_ick",	&gpt4_ick,	CK_3XXX),
	CLK(NULL,	"gpt3_ick",	&gpt3_ick,	CK_3XXX),
	CLK(NULL,	"gpt2_ick",	&gpt2_ick,	CK_3XXX),
	CLK("omap-mcbsp.2", "ick",	&mcbsp2_ick,	CK_3XXX),
	CLK("omap-mcbsp.3", "ick",	&mcbsp3_ick,	CK_3XXX),
	CLK("omap-mcbsp.4", "ick",	&mcbsp4_ick,	CK_3XXX),
	CLK(NULL,	"mcbsp2_fck",	&mcbsp2_fck,	CK_3XXX),
	CLK(NULL,	"mcbsp3_fck",	&mcbsp3_fck,	CK_3XXX),
	CLK(NULL,	"mcbsp4_fck",	&mcbsp4_fck,	CK_3XXX),
	CLK("etb",	"emu_src_ck",	&emu_src_ck,	CK_3XXX),
	CLK(NULL,	"pclk_fck",	&pclk_fck,	CK_3XXX),
	CLK(NULL,	"pclkx2_fck",	&pclkx2_fck,	CK_3XXX),
	CLK(NULL,	"atclk_fck",	&atclk_fck,	CK_3XXX),
	CLK(NULL,	"traceclk_src_fck", &traceclk_src_fck, CK_3XXX),
	CLK(NULL,	"traceclk_fck",	&traceclk_fck,	CK_3XXX),
	CLK(NULL,	"sr1_fck",	&sr1_fck,	CK_34XX | CK_36XX),
	CLK(NULL,	"sr2_fck",	&sr2_fck,	CK_34XX | CK_36XX),
	CLK(NULL,	"sr_l4_ick",	&sr_l4_ick,	CK_34XX | CK_36XX),
	CLK(NULL,	"secure_32k_fck", &secure_32k_fck, CK_3XXX),
	CLK(NULL,	"gpt12_fck",	&gpt12_fck,	CK_3XXX),
	CLK(NULL,	"wdt1_fck",	&wdt1_fck,	CK_3XXX),
	CLK(NULL,	"ipss_ick",	&ipss_ick,	CK_AM35XX),
	CLK(NULL,	"rmii_ck",	&rmii_ck,	CK_AM35XX),
	CLK(NULL,	"pclk_ck",	&pclk_ck,	CK_AM35XX),
	CLK("davinci_emac",	NULL,	&emac_ick,	CK_AM35XX),
	CLK("davinci_mdio.0",	NULL,	&emac_fck,	CK_AM35XX),
	CLK("vpfe-capture",	"master",	&vpfe_ick,	CK_AM35XX),
	CLK("vpfe-capture",	"slave",	&vpfe_fck,	CK_AM35XX),
	CLK("musb-am35x",	"ick",		&hsotgusb_ick_am35xx,	CK_AM35XX),
	CLK("musb-am35x",	"fck",		&hsotgusb_fck_am35xx,	CK_AM35XX),
	CLK(NULL,	"hecc_ck",	&hecc_ck,	CK_AM35XX),
	CLK(NULL,	"uart4_ick",	&uart4_ick_am35xx,	CK_AM35XX),
	CLK("omap_timer.1",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.2",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.3",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.4",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.5",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.6",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.7",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.8",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.9",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.10",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.11",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.12",	"32k_ck",	&omap_32k_fck,  CK_3XXX),
	CLK("omap_timer.1",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.2",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.3",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.4",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.5",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.6",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.7",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.8",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.9",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.10",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.11",	"sys_ck",	&sys_ck,	CK_3XXX),
	CLK("omap_timer.12",	"sys_ck",	&sys_ck,	CK_3XXX),
};


int __init omap3xxx_clk_init(void)
{
	struct omap_clk *c;
	u32 cpu_clkflg = 0;

	if (soc_is_am35xx()) {
		cpu_mask = RATE_IN_34XX;
		cpu_clkflg = CK_AM35XX;
	} else if (cpu_is_omap3630()) {
		cpu_mask = (RATE_IN_34XX | RATE_IN_36XX);
		cpu_clkflg = CK_36XX;
	} else if (cpu_is_ti816x()) {
		cpu_mask = RATE_IN_TI816X;
		cpu_clkflg = CK_TI816X;
	} else if (cpu_is_am33xx()) {
		cpu_mask = RATE_IN_AM33XX;
	} else if (cpu_is_ti814x()) {
		cpu_mask = RATE_IN_TI814X;
	} else if (cpu_is_omap34xx()) {
		if (omap_rev() == OMAP3430_REV_ES1_0) {
			cpu_mask = RATE_IN_3430ES1;
			cpu_clkflg = CK_3430ES1;
		} else {
			/*
			 * Assume that anything that we haven't matched yet
			 * has 3430ES2-type clocks.
			 */
			cpu_mask = RATE_IN_3430ES2PLUS;
			cpu_clkflg = CK_3430ES2PLUS;
		}
	} else {
		WARN(1, "clock: could not identify OMAP3 variant\n");
	}

	if (omap3_has_192mhz_clk())
		omap_96m_alwon_fck = omap_96m_alwon_fck_3630;

	if (cpu_is_omap3630()) {
		/*
		 * XXX This type of dynamic rewriting of the clock tree is
		 * deprecated and should be revised soon.
		 *
		 * For 3630: override clkops_omap2_dflt_wait for the
		 * clocks affected from PWRDN reset Limitation
		 */
		dpll3_m3x2_ck.ops =
				&clkops_omap36xx_pwrdn_with_hsdiv_wait_restore;
		dpll4_m2x2_ck.ops =
				&clkops_omap36xx_pwrdn_with_hsdiv_wait_restore;
		dpll4_m3x2_ck.ops =
				&clkops_omap36xx_pwrdn_with_hsdiv_wait_restore;
		dpll4_m4x2_ck.ops =
				&clkops_omap36xx_pwrdn_with_hsdiv_wait_restore;
		dpll4_m5x2_ck.ops =
				&clkops_omap36xx_pwrdn_with_hsdiv_wait_restore;
		dpll4_m6x2_ck.ops =
				&clkops_omap36xx_pwrdn_with_hsdiv_wait_restore;
	}

	/*
	 * XXX This type of dynamic rewriting of the clock tree is
	 * deprecated and should be revised soon.
	 */
	if (cpu_is_omap3630())
		dpll4_dd = dpll4_dd_3630;
	else
		dpll4_dd = dpll4_dd_34xx;

	clk_init(&omap2_clk_functions);

	for (c = omap3xxx_clks; c < omap3xxx_clks + ARRAY_SIZE(omap3xxx_clks);
	     c++)
		clk_preinit(c->lk.clk);

	for (c = omap3xxx_clks; c < omap3xxx_clks + ARRAY_SIZE(omap3xxx_clks);
	     c++)
		if (c->cpu & cpu_clkflg) {
			clkdev_add(&c->lk);
			clk_register(c->lk.clk);
			omap2_init_clk_clkdm(c->lk.clk);
		}

	/* Disable autoidle on all clocks; let the PM code enable it later */
	omap_clk_disable_autoidle_all();

	recalculate_root_clocks();

	pr_info("Clocking rate (Crystal/Core/MPU): %ld.%01ld/%ld/%ld MHz\n",
		(osc_sys_ck.rate / 1000000), (osc_sys_ck.rate / 100000) % 10,
		(core_ck.rate / 1000000), (arm_fck.rate / 1000000));

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_enable_init_clocks();

	/*
	 * Lock DPLL5 -- here only until other device init code can
	 * handle this
	 */
	if (!cpu_is_ti81xx() && (omap_rev() >= OMAP3430_REV_ES2_0))
		omap3_clk_lock_dpll5();

	/* Avoid sleeping during omap3_core_dpll_m2_set_rate() */
	sdrc_ick_p = clk_get(NULL, "sdrc_ick");
	arm_fck_p = clk_get(NULL, "arm_fck");

	return 0;
}
