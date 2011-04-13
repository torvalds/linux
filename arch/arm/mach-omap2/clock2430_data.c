/*
 * OMAP2430 clock data
 *
 * Copyright (C) 2005-2009 Texas Instruments, Inc.
 * Copyright (C) 2004-2011 Nokia Corporation
 *
 * Contacts:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/list.h>

#include <plat/clkdev_omap.h>

#include "clock.h"
#include "clock2xxx.h"
#include "opp2xxx.h"
#include "cm2xxx_3xxx.h"
#include "prm2xxx_3xxx.h"
#include "prm-regbits-24xx.h"
#include "cm-regbits-24xx.h"
#include "sdrc.h"
#include "control.h"

#define OMAP_CM_REGADDR			OMAP2430_CM_REGADDR

/*
 * 2430 clock tree.
 *
 * NOTE:In many cases here we are assigning a 'default' parent. In
 *	many cases the parent is selectable. The set parent calls will
 *	also switch sources.
 *
 *	Several sources are given initial rates which may be wrong, this will
 *	be fixed up in the init func.
 *
 *	Things are broadly separated below by clock domains. It is
 *	noteworthy that most peripherals have dependencies on multiple clock
 *	domains. Many get their interface clocks from the L4 domain, but get
 *	functional clocks from fixed sources or other core domain derived
 *	clocks.
 */

/* Base external input clocks */
static struct clk func_32k_ck = {
	.name		= "func_32k_ck",
	.ops		= &clkops_null,
	.rate		= 32768,
	.clkdm_name	= "wkup_clkdm",
};

static struct clk secure_32k_ck = {
	.name		= "secure_32k_ck",
	.ops		= &clkops_null,
	.rate		= 32768,
	.clkdm_name	= "wkup_clkdm",
};

/* Typical 12/13MHz in standalone mode, will be 26Mhz in chassis mode */
static struct clk osc_ck = {		/* (*12, *13, 19.2, *26, 38.4)MHz */
	.name		= "osc_ck",
	.ops		= &clkops_oscck,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &omap2_osc_clk_recalc,
};

/* Without modem likely 12MHz, with modem likely 13MHz */
static struct clk sys_ck = {		/* (*12, *13, 19.2, 26, 38.4)MHz */
	.name		= "sys_ck",		/* ~ ref_clk also */
	.ops		= &clkops_null,
	.parent		= &osc_ck,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &omap2xxx_sys_clk_recalc,
};

static struct clk alt_ck = {		/* Typical 54M or 48M, may not exist */
	.name		= "alt_ck",
	.ops		= &clkops_null,
	.rate		= 54000000,
	.clkdm_name	= "wkup_clkdm",
};

/* Optional external clock input for McBSP CLKS */
static struct clk mcbsp_clks = {
	.name		= "mcbsp_clks",
	.ops		= &clkops_null,
};

/*
 * Analog domain root source clocks
 */

/* dpll_ck, is broken out in to special cases through clksel */
/* REVISIT: Rate changes on dpll_ck trigger a full set change.	...
 * deal with this
 */

static struct dpll_data dpll_dd = {
	.mult_div1_reg		= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.mult_mask		= OMAP24XX_DPLL_MULT_MASK,
	.div1_mask		= OMAP24XX_DPLL_DIV_MASK,
	.clk_bypass		= &sys_ck,
	.clk_ref		= &sys_ck,
	.control_reg		= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_mask		= OMAP24XX_EN_DPLL_MASK,
	.max_multiplier		= 1023,
	.min_divider		= 1,
	.max_divider		= 16,
};

/*
 * XXX Cannot add round_rate here yet, as this is still a composite clock,
 * not just a DPLL
 */
static struct clk dpll_ck = {
	.name		= "dpll_ck",
	.ops		= &clkops_omap2xxx_dpll_ops,
	.parent		= &sys_ck,		/* Can be func_32k also */
	.dpll_data	= &dpll_dd,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &omap2_dpllcore_recalc,
	.set_rate	= &omap2_reprogram_dpllcore,
};

static struct clk apll96_ck = {
	.name		= "apll96_ck",
	.ops		= &clkops_apll96,
	.parent		= &sys_ck,
	.rate		= 96000000,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP24XX_EN_96M_PLL_SHIFT,
};

static struct clk apll54_ck = {
	.name		= "apll54_ck",
	.ops		= &clkops_apll54,
	.parent		= &sys_ck,
	.rate		= 54000000,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP24XX_EN_54M_PLL_SHIFT,
};

/*
 * PRCM digital base sources
 */

/* func_54m_ck */

static const struct clksel_rate func_54m_apll54_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 0 },
};

static const struct clksel_rate func_54m_alt_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 0 },
};

static const struct clksel func_54m_clksel[] = {
	{ .parent = &apll54_ck, .rates = func_54m_apll54_rates, },
	{ .parent = &alt_ck,	.rates = func_54m_alt_rates, },
	{ .parent = NULL },
};

static struct clk func_54m_ck = {
	.name		= "func_54m_ck",
	.ops		= &clkops_null,
	.parent		= &apll54_ck,	/* can also be alt_clk */
	.clkdm_name	= "wkup_clkdm",
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_54M_SOURCE_MASK,
	.clksel		= func_54m_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk core_ck = {
	.name		= "core_ck",
	.ops		= &clkops_null,
	.parent		= &dpll_ck,		/* can also be 32k */
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

/* func_96m_ck */
static const struct clksel_rate func_96m_apll96_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 0 },
};

static const struct clksel_rate func_96m_alt_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_243X },
	{ .div = 0 },
};

static const struct clksel func_96m_clksel[] = {
	{ .parent = &apll96_ck,	.rates = func_96m_apll96_rates },
	{ .parent = &alt_ck,	.rates = func_96m_alt_rates },
	{ .parent = NULL }
};

static struct clk func_96m_ck = {
	.name		= "func_96m_ck",
	.ops		= &clkops_null,
	.parent		= &apll96_ck,
	.clkdm_name	= "wkup_clkdm",
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP2430_96M_SOURCE_MASK,
	.clksel		= func_96m_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/* func_48m_ck */

static const struct clksel_rate func_48m_apll96_rates[] = {
	{ .div = 2, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 0 },
};

static const struct clksel_rate func_48m_alt_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 0 },
};

static const struct clksel func_48m_clksel[] = {
	{ .parent = &apll96_ck,	.rates = func_48m_apll96_rates },
	{ .parent = &alt_ck, .rates = func_48m_alt_rates },
	{ .parent = NULL }
};

static struct clk func_48m_ck = {
	.name		= "func_48m_ck",
	.ops		= &clkops_null,
	.parent		= &apll96_ck,	 /* 96M or Alt */
	.clkdm_name	= "wkup_clkdm",
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_48M_SOURCE_MASK,
	.clksel		= func_48m_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk func_12m_ck = {
	.name		= "func_12m_ck",
	.ops		= &clkops_null,
	.parent		= &func_48m_ck,
	.fixed_div	= 4,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &omap_fixed_divisor_recalc,
};

/* Secure timer, only available in secure mode */
static struct clk wdt1_osc_ck = {
	.name		= "ck_wdt1_osc",
	.ops		= &clkops_null, /* RMK: missing? */
	.parent		= &osc_ck,
	.recalc		= &followparent_recalc,
};

/*
 * The common_clkout* clksel_rate structs are common to
 * sys_clkout, sys_clkout_src, sys_clkout2, and sys_clkout2_src.
 * sys_clkout2_* are 2420-only, so the
 * clksel_rate flags fields are inaccurate for those clocks. This is
 * harmless since access to those clocks are gated by the struct clk
 * flags fields, which mark them as 2420-only.
 */
static const struct clksel_rate common_clkout_src_core_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel_rate common_clkout_src_sys_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel_rate common_clkout_src_96m_rates[] = {
	{ .div = 1, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel_rate common_clkout_src_54m_rates[] = {
	{ .div = 1, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel common_clkout_src_clksel[] = {
	{ .parent = &core_ck,	  .rates = common_clkout_src_core_rates },
	{ .parent = &sys_ck,	  .rates = common_clkout_src_sys_rates },
	{ .parent = &func_96m_ck, .rates = common_clkout_src_96m_rates },
	{ .parent = &func_54m_ck, .rates = common_clkout_src_54m_rates },
	{ .parent = NULL }
};

static struct clk sys_clkout_src = {
	.name		= "sys_clkout_src",
	.ops		= &clkops_omap2_dflt,
	.parent		= &func_54m_ck,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP2430_PRCM_CLKOUT_CTRL,
	.enable_bit	= OMAP24XX_CLKOUT_EN_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP2430_PRCM_CLKOUT_CTRL,
	.clksel_mask	= OMAP24XX_CLKOUT_SOURCE_MASK,
	.clksel		= common_clkout_src_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static const struct clksel_rate common_clkout_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 8, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 16, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 0 },
};

static const struct clksel sys_clkout_clksel[] = {
	{ .parent = &sys_clkout_src, .rates = common_clkout_rates },
	{ .parent = NULL }
};

static struct clk sys_clkout = {
	.name		= "sys_clkout",
	.ops		= &clkops_null,
	.parent		= &sys_clkout_src,
	.clkdm_name	= "wkup_clkdm",
	.clksel_reg	= OMAP2430_PRCM_CLKOUT_CTRL,
	.clksel_mask	= OMAP24XX_CLKOUT_DIV_MASK,
	.clksel		= sys_clkout_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk emul_ck = {
	.name		= "emul_ck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &func_54m_ck,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP2430_PRCM_CLKEMUL_CTRL,
	.enable_bit	= OMAP24XX_EMULATION_EN_SHIFT,
	.recalc		= &followparent_recalc,

};

/*
 * MPU clock domain
 *	Clocks:
 *		MPU_FCLK, MPU_ICLK
 *		INT_M_FCLK, INT_M_I_CLK
 *
 * - Individual clocks are hardware managed.
 * - Base divider comes from: CM_CLKSEL_MPU
 *
 */
static const struct clksel_rate mpu_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 0 },
};

static const struct clksel mpu_clksel[] = {
	{ .parent = &core_ck, .rates = mpu_core_rates },
	{ .parent = NULL }
};

static struct clk mpu_ck = {	/* Control cpu */
	.name		= "mpu_ck",
	.ops		= &clkops_null,
	.parent		= &core_ck,
	.clkdm_name	= "mpu_clkdm",
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(MPU_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP24XX_CLKSEL_MPU_MASK,
	.clksel		= mpu_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/*
 * DSP (2430-IVA2.1) clock domain
 * Clocks:
 *	2430: IVA2.1_FCLK (really just DSP_FCLK), IVA2.1_ICLK
 *
 * Won't be too specific here. The core clock comes into this block
 * it is divided then tee'ed. One branch goes directly to xyz enable
 * controls. The other branch gets further divided by 2 then possibly
 * routed into a synchronizer and out of clocks abc.
 */
static const struct clksel_rate dsp_fck_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 3, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 0 },
};

static const struct clksel dsp_fck_clksel[] = {
	{ .parent = &core_ck, .rates = dsp_fck_core_rates },
	{ .parent = NULL }
};

static struct clk dsp_fck = {
	.name		= "dsp_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_ck,
	.clkdm_name	= "dsp_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_CM_FCLKEN_DSP_EN_DSP_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP24XX_CLKSEL_DSP_MASK,
	.clksel		= dsp_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel dsp_ick_clksel[] = {
	{ .parent = &dsp_fck, .rates = dsp_ick_rates },
	{ .parent = NULL }
};

/* 2430 only - EN_DSP controls both dsp fclk and iclk on 2430 */
static struct clk iva2_1_ick = {
	.name		= "iva2_1_ick",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &dsp_fck,
	.clkdm_name	= "dsp_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_CM_FCLKEN_DSP_EN_DSP_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP24XX_CLKSEL_DSP_IF_MASK,
	.clksel		= dsp_ick_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/*
 * L3 clock domain
 * L3 clocks are used for both interface and functional clocks to
 * multiple entities. Some of these clocks are completely managed
 * by hardware, and some others allow software control. Hardware
 * managed ones general are based on directly CLK_REQ signals and
 * various auto idle settings. The functional spec sets many of these
 * as 'tie-high' for their enables.
 *
 * I-CLOCKS:
 *	L3-Interconnect, SMS, GPMC, SDRC, OCM_RAM, OCM_ROM, SDMA
 *	CAM, HS-USB.
 * F-CLOCK
 *	SSI.
 *
 * GPMC memories and SDRC have timing and clock sensitive registers which
 * may very well need notification when the clock changes. Currently for low
 * operating points, these are taken care of in sleep.S.
 */
static const struct clksel_rate core_l3_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 6, .val = 6, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel core_l3_clksel[] = {
	{ .parent = &core_ck, .rates = core_l3_core_rates },
	{ .parent = NULL }
};

static struct clk core_l3_ck = {	/* Used for ick and fck, interconnect */
	.name		= "core_l3_ck",
	.ops		= &clkops_null,
	.parent		= &core_ck,
	.clkdm_name	= "core_l3_clkdm",
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_L3_MASK,
	.clksel		= core_l3_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/* usb_l4_ick */
static const struct clksel_rate usb_l4_ick_core_l3_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel usb_l4_ick_clksel[] = {
	{ .parent = &core_l3_ck, .rates = usb_l4_ick_core_l3_rates },
	{ .parent = NULL },
};

/* It is unclear from TRM whether usb_l4_ick is really in L3 or L4 clkdm */
static struct clk usb_l4_ick = {	/* FS-USB interface clock */
	.name		= "usb_l4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l3_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP24XX_EN_USB_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_USB_MASK,
	.clksel		= usb_l4_ick_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/*
 * L4 clock management domain
 *
 * This domain contains lots of interface clocks from the L4 interface, some
 * functional clocks.	Fixed APLL functional source clocks are managed in
 * this domain.
 */
static const struct clksel_rate l4_core_l3_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel l4_clksel[] = {
	{ .parent = &core_l3_ck, .rates = l4_core_l3_rates },
	{ .parent = NULL }
};

static struct clk l4_ck = {		/* used both as an ick and fck */
	.name		= "l4_ck",
	.ops		= &clkops_null,
	.parent		= &core_l3_ck,
	.clkdm_name	= "core_l4_clkdm",
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_L4_MASK,
	.clksel		= l4_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/*
 * SSI is in L3 management domain, its direct parent is core not l3,
 * many core power domain entities are grouped into the L3 clock
 * domain.
 * SSI_SSR_FCLK, SSI_SST_FCLK, SSI_L4_ICLK
 *
 * ssr = core/1/2/3/4/5, sst = 1/2 ssr.
 */
static const struct clksel_rate ssi_ssr_sst_fck_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 3, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 5, .val = 5, .flags = RATE_IN_243X },
	{ .div = 0 }
};

static const struct clksel ssi_ssr_sst_fck_clksel[] = {
	{ .parent = &core_ck, .rates = ssi_ssr_sst_fck_core_rates },
	{ .parent = NULL }
};

static struct clk ssi_ssr_sst_fck = {
	.name		= "ssi_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_ck,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP24XX_EN_SSI_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_SSI_MASK,
	.clksel		= ssi_ssr_sst_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/*
 * Presumably this is the same as SSI_ICLK.
 * TRM contradicts itself on what clockdomain SSI_ICLK is in
 */
static struct clk ssi_l4_ick = {
	.name		= "ssi_l4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP24XX_EN_SSI_SHIFT,
	.recalc		= &followparent_recalc,
};


/*
 * GFX clock domain
 *	Clocks:
 * GFX_FCLK, GFX_ICLK
 * GFX_CG1(2d), GFX_CG2(3d)
 *
 * GFX_FCLK runs from L3, and is divided by (1,2,3,4)
 * The 2d and 3d clocks run at a hardware determined
 * divided value of fclk.
 *
 */

/* This clksel struct is shared between gfx_3d_fck and gfx_2d_fck */
static const struct clksel gfx_fck_clksel[] = {
	{ .parent = &core_l3_ck, .rates = gfx_l3_rates },
	{ .parent = NULL },
};

static struct clk gfx_3d_fck = {
	.name		= "gfx_3d_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_l3_ck,
	.clkdm_name	= "gfx_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_3D_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP_CLKSEL_GFX_MASK,
	.clksel		= gfx_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk gfx_2d_fck = {
	.name		= "gfx_2d_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_l3_ck,
	.clkdm_name	= "gfx_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_2D_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP_CLKSEL_GFX_MASK,
	.clksel		= gfx_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

/* This interface clock does not have a CM_AUTOIDLE bit */
static struct clk gfx_ick = {
	.name		= "gfx_ick",		/* From l3 */
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &core_l3_ck,
	.clkdm_name	= "gfx_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_ICLKEN),
	.enable_bit	= OMAP_EN_GFX_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * Modem clock domain (2430)
 *	CLOCKS:
 *		MDM_OSC_CLK
 *		MDM_ICLK
 * These clocks are usable in chassis mode only.
 */
static const struct clksel_rate mdm_ick_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_243X },
	{ .div = 4, .val = 4, .flags = RATE_IN_243X },
	{ .div = 6, .val = 6, .flags = RATE_IN_243X },
	{ .div = 9, .val = 9, .flags = RATE_IN_243X },
	{ .div = 0 }
};

static const struct clksel mdm_ick_clksel[] = {
	{ .parent = &core_ck, .rates = mdm_ick_core_rates },
	{ .parent = NULL }
};

static struct clk mdm_ick = {		/* used both as a ick and fck */
	.name		= "mdm_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_ck,
	.clkdm_name	= "mdm_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(OMAP2430_MDM_MOD, CM_ICLKEN),
	.enable_bit	= OMAP2430_CM_ICLKEN_MDM_EN_MDM_SHIFT,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP2430_MDM_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP2430_CLKSEL_MDM_MASK,
	.clksel		= mdm_ick_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mdm_osc_ck = {
	.name		= "mdm_osc_ck",
	.ops		= &clkops_omap2_mdmclk_dflt_wait,
	.parent		= &osc_ck,
	.clkdm_name	= "mdm_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(OMAP2430_MDM_MOD, CM_FCLKEN),
	.enable_bit	= OMAP2430_EN_OSC_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * DSS clock domain
 * CLOCKs:
 * DSS_L4_ICLK, DSS_L3_ICLK,
 * DSS_CLK1, DSS_CLK2, DSS_54MHz_CLK
 *
 * DSS is both initiator and target.
 */
/* XXX Add RATE_NOT_VALIDATED */

static const struct clksel_rate dss1_fck_sys_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel_rate dss1_fck_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 3, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 5, .val = 5, .flags = RATE_IN_24XX },
	{ .div = 6, .val = 6, .flags = RATE_IN_24XX },
	{ .div = 8, .val = 8, .flags = RATE_IN_24XX },
	{ .div = 9, .val = 9, .flags = RATE_IN_24XX },
	{ .div = 12, .val = 12, .flags = RATE_IN_24XX },
	{ .div = 16, .val = 16, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel dss1_fck_clksel[] = {
	{ .parent = &sys_ck,  .rates = dss1_fck_sys_rates },
	{ .parent = &core_ck, .rates = dss1_fck_core_rates },
	{ .parent = NULL },
};

static struct clk dss_ick = {		/* Enables both L3,L4 ICLK's */
	.name		= "dss_ick",
	.ops		= &clkops_omap2_iclk_dflt,
	.parent		= &l4_ck,	/* really both l3 and l4 */
	.clkdm_name	= "dss_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_DSS1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk dss1_fck = {
	.name		= "dss1_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &core_ck,		/* Core or sys */
	.clkdm_name	= "dss_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_DSS1_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_DSS1_MASK,
	.clksel		= dss1_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel_rate dss2_fck_sys_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel_rate dss2_fck_48m_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel dss2_fck_clksel[] = {
	{ .parent = &sys_ck,	  .rates = dss2_fck_sys_rates },
	{ .parent = &func_48m_ck, .rates = dss2_fck_48m_rates },
	{ .parent = NULL }
};

static struct clk dss2_fck = {		/* Alt clk used in power management */
	.name		= "dss2_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &sys_ck,		/* fixed at sys_ck or 48MHz */
	.clkdm_name	= "dss_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_DSS2_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_DSS2_MASK,
	.clksel		= dss2_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk dss_54m_fck = {	/* Alt clk used in power management */
	.name		= "dss_54m_fck",	/* 54m tv clk */
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_54m_ck,
	.clkdm_name	= "dss_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_TV_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk wu_l4_ick = {
	.name		= "wu_l4_ick",
	.ops		= &clkops_null,
	.parent		= &sys_ck,
	.clkdm_name	= "wkup_clkdm",
	.recalc		= &followparent_recalc,
};

/*
 * CORE power domain ICLK & FCLK defines.
 * Many of the these can have more than one possible parent. Entries
 * here will likely have an L4 interface parent, and may have multiple
 * functional clock parents.
 */
static const struct clksel_rate gpt_alt_rates[] = {
	{ .div = 1, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel omap24xx_gpt_clksel[] = {
	{ .parent = &func_32k_ck, .rates = gpt_32k_rates },
	{ .parent = &sys_ck,	  .rates = gpt_sys_rates },
	{ .parent = &alt_ck,	  .rates = gpt_alt_rates },
	{ .parent = NULL },
};

static struct clk gpt1_ick = {
	.name		= "gpt1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wu_l4_ick,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_GPT1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt1_fck = {
	.name		= "gpt1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_GPT1_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT1_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate
};

static struct clk gpt2_ick = {
	.name		= "gpt2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt2_fck = {
	.name		= "gpt2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT2_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT2_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt3_ick = {
	.name		= "gpt3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt3_fck = {
	.name		= "gpt3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT3_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT3_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt4_ick = {
	.name		= "gpt4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT4_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt4_fck = {
	.name		= "gpt4_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT4_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT4_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt5_ick = {
	.name		= "gpt5_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT5_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt5_fck = {
	.name		= "gpt5_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT5_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT5_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt6_ick = {
	.name		= "gpt6_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT6_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt6_fck = {
	.name		= "gpt6_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT6_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT6_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt7_ick = {
	.name		= "gpt7_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT7_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt7_fck = {
	.name		= "gpt7_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT7_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT7_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt8_ick = {
	.name		= "gpt8_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT8_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt8_fck = {
	.name		= "gpt8_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT8_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT8_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt9_ick = {
	.name		= "gpt9_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT9_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt9_fck = {
	.name		= "gpt9_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT9_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT9_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt10_ick = {
	.name		= "gpt10_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT10_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt10_fck = {
	.name		= "gpt10_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT10_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT10_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt11_ick = {
	.name		= "gpt11_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT11_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt11_fck = {
	.name		= "gpt11_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT11_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT11_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpt12_ick = {
	.name		= "gpt12_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT12_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpt12_fck = {
	.name		= "gpt12_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &secure_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT12_SHIFT,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
	.clksel_mask	= OMAP24XX_CLKSEL_GPT12_MASK,
	.clksel		= omap24xx_gpt_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mcbsp1_ick = {
	.name		= "mcbsp1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCBSP1_SHIFT,
	.recalc		= &followparent_recalc,
};

static const struct clksel_rate common_mcbsp_96m_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel_rate common_mcbsp_mcbsp_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel mcbsp_fck_clksel[] = {
	{ .parent = &func_96m_ck,  .rates = common_mcbsp_96m_rates },
	{ .parent = &mcbsp_clks,   .rates = common_mcbsp_mcbsp_rates },
	{ .parent = NULL }
};

static struct clk mcbsp1_fck = {
	.name		= "mcbsp1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_96m_ck,
	.init		= &omap2_init_clksel_parent,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MCBSP1_SHIFT,
	.clksel_reg	= OMAP243X_CTRL_REGADDR(OMAP2_CONTROL_DEVCONF0),
	.clksel_mask	= OMAP2_MCBSP1_CLKS_MASK,
	.clksel		= mcbsp_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mcbsp2_ick = {
	.name		= "mcbsp2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCBSP2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp2_fck = {
	.name		= "mcbsp2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_96m_ck,
	.init		= &omap2_init_clksel_parent,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MCBSP2_SHIFT,
	.clksel_reg	= OMAP243X_CTRL_REGADDR(OMAP2_CONTROL_DEVCONF0),
	.clksel_mask	= OMAP2_MCBSP2_CLKS_MASK,
	.clksel		= mcbsp_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mcbsp3_ick = {
	.name		= "mcbsp3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp3_fck = {
	.name		= "mcbsp3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_96m_ck,
	.init		= &omap2_init_clksel_parent,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP3_SHIFT,
	.clksel_reg	= OMAP243X_CTRL_REGADDR(OMAP243X_CONTROL_DEVCONF1),
	.clksel_mask	= OMAP2_MCBSP3_CLKS_MASK,
	.clksel		= mcbsp_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mcbsp4_ick = {
	.name		= "mcbsp4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP4_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp4_fck = {
	.name		= "mcbsp4_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_96m_ck,
	.init		= &omap2_init_clksel_parent,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP4_SHIFT,
	.clksel_reg	= OMAP243X_CTRL_REGADDR(OMAP243X_CONTROL_DEVCONF1),
	.clksel_mask	= OMAP2_MCBSP4_CLKS_MASK,
	.clksel		= mcbsp_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mcbsp5_ick = {
	.name		= "mcbsp5_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP5_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcbsp5_fck = {
	.name		= "mcbsp5_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_96m_ck,
	.init		= &omap2_init_clksel_parent,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MCBSP5_SHIFT,
	.clksel_reg	= OMAP243X_CTRL_REGADDR(OMAP243X_CONTROL_DEVCONF1),
	.clksel_mask	= OMAP2_MCBSP5_CLKS_MASK,
	.clksel		= mcbsp_fck_clksel,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk mcspi1_ick = {
	.name		= "mcspi1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi1_fck = {
	.name		= "mcspi1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_48m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi2_ick = {
	.name		= "mcspi2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi2_fck = {
	.name		= "mcspi2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_48m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi3_ick = {
	.name		= "mcspi3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MCSPI3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mcspi3_fck = {
	.name		= "mcspi3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_48m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MCSPI3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart1_ick = {
	.name		= "uart1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_UART1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart1_fck = {
	.name		= "uart1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_48m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_UART1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart2_ick = {
	.name		= "uart2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_UART2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart2_fck = {
	.name		= "uart2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_48m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_UART2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart3_ick = {
	.name		= "uart3_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP24XX_EN_UART3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk uart3_fck = {
	.name		= "uart3_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_48m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP24XX_EN_UART3_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpios_ick = {
	.name		= "gpios_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wu_l4_ick,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_GPIOS_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpios_fck = {
	.name		= "gpios_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_GPIOS_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mpu_wdt_ick = {
	.name		= "mpu_wdt_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wu_l4_ick,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_MPU_WDT_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mpu_wdt_fck = {
	.name		= "mpu_wdt_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_MPU_WDT_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk sync_32k_ick = {
	.name		= "sync_32k_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.flags		= ENABLE_ON_INIT,
	.parent		= &wu_l4_ick,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_32KSYNC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk wdt1_ick = {
	.name		= "wdt1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wu_l4_ick,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_WDT1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk omapctrl_ick = {
	.name		= "omapctrl_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.flags		= ENABLE_ON_INIT,
	.parent		= &wu_l4_ick,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_OMAPCTRL_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk icr_ick = {
	.name		= "icr_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &wu_l4_ick,
	.clkdm_name	= "wkup_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP2430_EN_ICR_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk cam_ick = {
	.name		= "cam_ick",
	.ops		= &clkops_omap2_iclk_dflt,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_CAM_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * cam_fck controls both CAM_MCLK and CAM_FCLK.  It should probably be
 * split into two separate clocks, since the parent clocks are different
 * and the clockdomains are also different.
 */
static struct clk cam_fck = {
	.name		= "cam_fck",
	.ops		= &clkops_omap2_dflt,
	.parent		= &func_96m_ck,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_CAM_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mailboxes_ick = {
	.name		= "mailboxes_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MAILBOXES_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk wdt4_ick = {
	.name		= "wdt4_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_WDT4_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk wdt4_fck = {
	.name		= "wdt4_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_WDT4_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mspro_ick = {
	.name		= "mspro_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MSPRO_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mspro_fck = {
	.name		= "mspro_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_96m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MSPRO_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk fac_ick = {
	.name		= "fac_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_FAC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk fac_fck = {
	.name		= "fac_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_12m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_FAC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk hdq_ick = {
	.name		= "hdq_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_HDQ_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk hdq_fck = {
	.name		= "hdq_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_12m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_HDQ_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * XXX This is marked as a 2420-only define, but it claims to be present
 * on 2430 also.  Double-check.
 */
static struct clk i2c2_ick = {
	.name		= "i2c2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_I2C2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk i2chs2_fck = {
	.name		= "i2chs2_fck",
	.ops		= &clkops_omap2430_i2chs_wait,
	.parent		= &func_96m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_I2CHS2_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * XXX This is marked as a 2420-only define, but it claims to be present
 * on 2430 also.  Double-check.
 */
static struct clk i2c1_ick = {
	.name		= "i2c1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_I2C1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk i2chs1_fck = {
	.name		= "i2chs1_fck",
	.ops		= &clkops_omap2430_i2chs_wait,
	.parent		= &func_96m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_I2CHS1_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * The enable_reg/enable_bit in this clock is only used for CM_AUTOIDLE
 * accesses derived from this data.
 */
static struct clk gpmc_fck = {
	.name		= "gpmc_fck",
	.ops		= &clkops_omap2_iclk_idle_only,
	.parent		= &core_l3_ck,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP24XX_AUTO_GPMC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk sdma_fck = {
	.name		= "sdma_fck",
	.ops		= &clkops_null, /* RMK: missing? */
	.parent		= &core_l3_ck,
	.clkdm_name	= "core_l3_clkdm",
	.recalc		= &followparent_recalc,
};

/*
 * The enable_reg/enable_bit in this clock is only used for CM_AUTOIDLE
 * accesses derived from this data.
 */
static struct clk sdma_ick = {
	.name		= "sdma_ick",
	.ops		= &clkops_omap2_iclk_idle_only,
	.parent		= &core_l3_ck,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP24XX_AUTO_SDMA_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk sdrc_ick = {
	.name		= "sdrc_ick",
	.ops		= &clkops_omap2_iclk_idle_only,
	.parent		= &core_l3_ck,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP2430_EN_SDRC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk des_ick = {
	.name		= "des_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_DES_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk sha_ick = {
	.name		= "sha_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_SHA_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk rng_ick = {
	.name		= "rng_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_RNG_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk aes_ick = {
	.name		= "aes_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_AES_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk pka_ick = {
	.name		= "pka_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_PKA_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk usb_fck = {
	.name		= "usb_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_48m_ck,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP24XX_EN_USB_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk usbhs_ick = {
	.name		= "usbhs_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &core_l3_ck,
	.clkdm_name	= "core_l3_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_USBHS_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchs1_ick = {
	.name		= "mmchs1_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHS1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchs1_fck = {
	.name		= "mmchs1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_96m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHS1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchs2_ick = {
	.name		= "mmchs2_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHS2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchs2_fck = {
	.name		= "mmchs2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_96m_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHS2_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpio5_ick = {
	.name		= "gpio5_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_GPIO5_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk gpio5_fck = {
	.name		= "gpio5_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_GPIO5_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mdm_intc_ick = {
	.name		= "mdm_intc_ick",
	.ops		= &clkops_omap2_iclk_dflt_wait,
	.parent		= &l4_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP2430_EN_MDM_INTC_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchsdb1_fck = {
	.name		= "mmchsdb1_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHSDB1_SHIFT,
	.recalc		= &followparent_recalc,
};

static struct clk mmchsdb2_fck = {
	.name		= "mmchsdb2_fck",
	.ops		= &clkops_omap2_dflt_wait,
	.parent		= &func_32k_ck,
	.clkdm_name	= "core_l4_clkdm",
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP2430_EN_MMCHSDB2_SHIFT,
	.recalc		= &followparent_recalc,
};

/*
 * This clock is a composite clock which does entire set changes then
 * forces a rebalance. It keys on the MPU speed, but it really could
 * be any key speed part of a set in the rate table.
 *
 * to really change a set, you need memory table sets which get changed
 * in sram, pre-notifiers & post notifiers, changing the top set, without
 * having low level display recalc's won't work... this is why dpm notifiers
 * work, isr's off, walk a list of clocks already _off_ and not messing with
 * the bus.
 *
 * This clock should have no parent. It embodies the entire upper level
 * active set. A parent will mess up some of the init also.
 */
static struct clk virt_prcm_set = {
	.name		= "virt_prcm_set",
	.ops		= &clkops_null,
	.parent		= &mpu_ck,	/* Indexed by mpu speed, no parent */
	.recalc		= &omap2_table_mpu_recalc,	/* sets are keyed on mpu rate */
	.set_rate	= &omap2_select_table_rate,
	.round_rate	= &omap2_round_to_table_rate,
};


/*
 * clkdev integration
 */

static struct omap_clk omap2430_clks[] = {
	/* external root sources */
	CLK(NULL,	"func_32k_ck",	&func_32k_ck,	CK_243X),
	CLK(NULL,	"secure_32k_ck", &secure_32k_ck, CK_243X),
	CLK(NULL,	"osc_ck",	&osc_ck,	CK_243X),
	CLK(NULL,	"sys_ck",	&sys_ck,	CK_243X),
	CLK(NULL,	"alt_ck",	&alt_ck,	CK_243X),
	CLK("omap-mcbsp.1",	"pad_fck",	&mcbsp_clks,	CK_243X),
	CLK("omap-mcbsp.2",	"pad_fck",	&mcbsp_clks,	CK_243X),
	CLK("omap-mcbsp.3",	"pad_fck",	&mcbsp_clks,	CK_243X),
	CLK("omap-mcbsp.4",	"pad_fck",	&mcbsp_clks,	CK_243X),
	CLK("omap-mcbsp.5",	"pad_fck",	&mcbsp_clks,	CK_243X),
	CLK(NULL,	"mcbsp_clks",	&mcbsp_clks,	CK_243X),
	/* internal analog sources */
	CLK(NULL,	"dpll_ck",	&dpll_ck,	CK_243X),
	CLK(NULL,	"apll96_ck",	&apll96_ck,	CK_243X),
	CLK(NULL,	"apll54_ck",	&apll54_ck,	CK_243X),
	/* internal prcm root sources */
	CLK(NULL,	"func_54m_ck",	&func_54m_ck,	CK_243X),
	CLK(NULL,	"core_ck",	&core_ck,	CK_243X),
	CLK("omap-mcbsp.1",	"prcm_fck",	&func_96m_ck,	CK_243X),
	CLK("omap-mcbsp.2",	"prcm_fck",	&func_96m_ck,	CK_243X),
	CLK("omap-mcbsp.3",	"prcm_fck",	&func_96m_ck,	CK_243X),
	CLK("omap-mcbsp.4",	"prcm_fck",	&func_96m_ck,	CK_243X),
	CLK("omap-mcbsp.5",	"prcm_fck",	&func_96m_ck,	CK_243X),
	CLK(NULL,	"func_96m_ck",	&func_96m_ck,	CK_243X),
	CLK(NULL,	"func_48m_ck",	&func_48m_ck,	CK_243X),
	CLK(NULL,	"func_12m_ck",	&func_12m_ck,	CK_243X),
	CLK(NULL,	"ck_wdt1_osc",	&wdt1_osc_ck,	CK_243X),
	CLK(NULL,	"sys_clkout_src", &sys_clkout_src, CK_243X),
	CLK(NULL,	"sys_clkout",	&sys_clkout,	CK_243X),
	CLK(NULL,	"emul_ck",	&emul_ck,	CK_243X),
	/* mpu domain clocks */
	CLK(NULL,	"mpu_ck",	&mpu_ck,	CK_243X),
	/* dsp domain clocks */
	CLK(NULL,	"dsp_fck",	&dsp_fck,	CK_243X),
	CLK(NULL,	"iva2_1_ick",	&iva2_1_ick,	CK_243X),
	/* GFX domain clocks */
	CLK(NULL,	"gfx_3d_fck",	&gfx_3d_fck,	CK_243X),
	CLK(NULL,	"gfx_2d_fck",	&gfx_2d_fck,	CK_243X),
	CLK(NULL,	"gfx_ick",	&gfx_ick,	CK_243X),
	/* Modem domain clocks */
	CLK(NULL,	"mdm_ick",	&mdm_ick,	CK_243X),
	CLK(NULL,	"mdm_osc_ck",	&mdm_osc_ck,	CK_243X),
	/* DSS domain clocks */
	CLK("omapdss_dss",	"ick",		&dss_ick,	CK_243X),
	CLK("omapdss_dss",	"fck",		&dss1_fck,	CK_243X),
	CLK("omapdss_dss",	"sys_clk",	&dss2_fck,	CK_243X),
	CLK("omapdss_dss",	"tv_clk",	&dss_54m_fck,	CK_243X),
	/* L3 domain clocks */
	CLK(NULL,	"core_l3_ck",	&core_l3_ck,	CK_243X),
	CLK(NULL,	"ssi_fck",	&ssi_ssr_sst_fck, CK_243X),
	CLK(NULL,	"usb_l4_ick",	&usb_l4_ick,	CK_243X),
	/* L4 domain clocks */
	CLK(NULL,	"l4_ck",	&l4_ck,		CK_243X),
	CLK(NULL,	"ssi_l4_ick",	&ssi_l4_ick,	CK_243X),
	CLK(NULL,	"wu_l4_ick",	&wu_l4_ick,	CK_243X),
	/* virtual meta-group clock */
	CLK(NULL,	"virt_prcm_set", &virt_prcm_set, CK_243X),
	/* general l4 interface ck, multi-parent functional clk */
	CLK(NULL,	"gpt1_ick",	&gpt1_ick,	CK_243X),
	CLK(NULL,	"gpt1_fck",	&gpt1_fck,	CK_243X),
	CLK(NULL,	"gpt2_ick",	&gpt2_ick,	CK_243X),
	CLK(NULL,	"gpt2_fck",	&gpt2_fck,	CK_243X),
	CLK(NULL,	"gpt3_ick",	&gpt3_ick,	CK_243X),
	CLK(NULL,	"gpt3_fck",	&gpt3_fck,	CK_243X),
	CLK(NULL,	"gpt4_ick",	&gpt4_ick,	CK_243X),
	CLK(NULL,	"gpt4_fck",	&gpt4_fck,	CK_243X),
	CLK(NULL,	"gpt5_ick",	&gpt5_ick,	CK_243X),
	CLK(NULL,	"gpt5_fck",	&gpt5_fck,	CK_243X),
	CLK(NULL,	"gpt6_ick",	&gpt6_ick,	CK_243X),
	CLK(NULL,	"gpt6_fck",	&gpt6_fck,	CK_243X),
	CLK(NULL,	"gpt7_ick",	&gpt7_ick,	CK_243X),
	CLK(NULL,	"gpt7_fck",	&gpt7_fck,	CK_243X),
	CLK(NULL,	"gpt8_ick",	&gpt8_ick,	CK_243X),
	CLK(NULL,	"gpt8_fck",	&gpt8_fck,	CK_243X),
	CLK(NULL,	"gpt9_ick",	&gpt9_ick,	CK_243X),
	CLK(NULL,	"gpt9_fck",	&gpt9_fck,	CK_243X),
	CLK(NULL,	"gpt10_ick",	&gpt10_ick,	CK_243X),
	CLK(NULL,	"gpt10_fck",	&gpt10_fck,	CK_243X),
	CLK(NULL,	"gpt11_ick",	&gpt11_ick,	CK_243X),
	CLK(NULL,	"gpt11_fck",	&gpt11_fck,	CK_243X),
	CLK(NULL,	"gpt12_ick",	&gpt12_ick,	CK_243X),
	CLK(NULL,	"gpt12_fck",	&gpt12_fck,	CK_243X),
	CLK("omap-mcbsp.1", "ick",	&mcbsp1_ick,	CK_243X),
	CLK("omap-mcbsp.1", "fck",	&mcbsp1_fck,	CK_243X),
	CLK("omap-mcbsp.2", "ick",	&mcbsp2_ick,	CK_243X),
	CLK("omap-mcbsp.2", "fck",	&mcbsp2_fck,	CK_243X),
	CLK("omap-mcbsp.3", "ick",	&mcbsp3_ick,	CK_243X),
	CLK("omap-mcbsp.3", "fck",	&mcbsp3_fck,	CK_243X),
	CLK("omap-mcbsp.4", "ick",	&mcbsp4_ick,	CK_243X),
	CLK("omap-mcbsp.4", "fck",	&mcbsp4_fck,	CK_243X),
	CLK("omap-mcbsp.5", "ick",	&mcbsp5_ick,	CK_243X),
	CLK("omap-mcbsp.5", "fck",	&mcbsp5_fck,	CK_243X),
	CLK("omap2_mcspi.1", "ick",	&mcspi1_ick,	CK_243X),
	CLK("omap2_mcspi.1", "fck",	&mcspi1_fck,	CK_243X),
	CLK("omap2_mcspi.2", "ick",	&mcspi2_ick,	CK_243X),
	CLK("omap2_mcspi.2", "fck",	&mcspi2_fck,	CK_243X),
	CLK("omap2_mcspi.3", "ick",	&mcspi3_ick,	CK_243X),
	CLK("omap2_mcspi.3", "fck",	&mcspi3_fck,	CK_243X),
	CLK(NULL,	"uart1_ick",	&uart1_ick,	CK_243X),
	CLK(NULL,	"uart1_fck",	&uart1_fck,	CK_243X),
	CLK(NULL,	"uart2_ick",	&uart2_ick,	CK_243X),
	CLK(NULL,	"uart2_fck",	&uart2_fck,	CK_243X),
	CLK(NULL,	"uart3_ick",	&uart3_ick,	CK_243X),
	CLK(NULL,	"uart3_fck",	&uart3_fck,	CK_243X),
	CLK(NULL,	"gpios_ick",	&gpios_ick,	CK_243X),
	CLK(NULL,	"gpios_fck",	&gpios_fck,	CK_243X),
	CLK("omap_wdt",	"ick",		&mpu_wdt_ick,	CK_243X),
	CLK("omap_wdt",	"fck",		&mpu_wdt_fck,	CK_243X),
	CLK(NULL,	"sync_32k_ick",	&sync_32k_ick,	CK_243X),
	CLK(NULL,	"wdt1_ick",	&wdt1_ick,	CK_243X),
	CLK(NULL,	"omapctrl_ick",	&omapctrl_ick,	CK_243X),
	CLK(NULL,	"icr_ick",	&icr_ick,	CK_243X),
	CLK("omap24xxcam", "fck",	&cam_fck,	CK_243X),
	CLK("omap24xxcam", "ick",	&cam_ick,	CK_243X),
	CLK(NULL,	"mailboxes_ick", &mailboxes_ick,	CK_243X),
	CLK(NULL,	"wdt4_ick",	&wdt4_ick,	CK_243X),
	CLK(NULL,	"wdt4_fck",	&wdt4_fck,	CK_243X),
	CLK(NULL,	"mspro_ick",	&mspro_ick,	CK_243X),
	CLK(NULL,	"mspro_fck",	&mspro_fck,	CK_243X),
	CLK(NULL,	"fac_ick",	&fac_ick,	CK_243X),
	CLK(NULL,	"fac_fck",	&fac_fck,	CK_243X),
	CLK("omap_hdq.0", "ick",	&hdq_ick,	CK_243X),
	CLK("omap_hdq.1", "fck",	&hdq_fck,	CK_243X),
	CLK("omap_i2c.1", "ick",	&i2c1_ick,	CK_243X),
	CLK("omap_i2c.1", "fck",	&i2chs1_fck,	CK_243X),
	CLK("omap_i2c.2", "ick",	&i2c2_ick,	CK_243X),
	CLK("omap_i2c.2", "fck",	&i2chs2_fck,	CK_243X),
	CLK(NULL,	"gpmc_fck",	&gpmc_fck,	CK_243X),
	CLK(NULL,	"sdma_fck",	&sdma_fck,	CK_243X),
	CLK(NULL,	"sdma_ick",	&sdma_ick,	CK_243X),
	CLK(NULL,	"sdrc_ick",	&sdrc_ick,	CK_243X),
	CLK(NULL,	"des_ick",	&des_ick,	CK_243X),
	CLK("omap-sham",	"ick",	&sha_ick,	CK_243X),
	CLK("omap_rng",	"ick",		&rng_ick,	CK_243X),
	CLK("omap-aes",	"ick",	&aes_ick,	CK_243X),
	CLK(NULL,	"pka_ick",	&pka_ick,	CK_243X),
	CLK(NULL,	"usb_fck",	&usb_fck,	CK_243X),
	CLK("musb-omap2430",	"ick",	&usbhs_ick,	CK_243X),
	CLK("omap_hsmmc.0", "ick",	&mmchs1_ick,	CK_243X),
	CLK("omap_hsmmc.0", "fck",	&mmchs1_fck,	CK_243X),
	CLK("omap_hsmmc.1", "ick",	&mmchs2_ick,	CK_243X),
	CLK("omap_hsmmc.1", "fck",	&mmchs2_fck,	CK_243X),
	CLK(NULL,	"gpio5_ick",	&gpio5_ick,	CK_243X),
	CLK(NULL,	"gpio5_fck",	&gpio5_fck,	CK_243X),
	CLK(NULL,	"mdm_intc_ick",	&mdm_intc_ick,	CK_243X),
	CLK("omap_hsmmc.0", "mmchsdb_fck",	&mmchsdb1_fck,	CK_243X),
	CLK("omap_hsmmc.1", "mmchsdb_fck",	&mmchsdb2_fck,	CK_243X),
};

/*
 * init code
 */

int __init omap2430_clk_init(void)
{
	const struct prcm_config *prcm;
	struct omap_clk *c;
	u32 clkrate;

	prcm_clksrc_ctrl = OMAP2430_PRCM_CLKSRC_CTRL;
	cm_idlest_pll = OMAP_CM_REGADDR(PLL_MOD, CM_IDLEST);
	cpu_mask = RATE_IN_243X;
	rate_table = omap2430_rate_table;

	clk_init(&omap2_clk_functions);

	for (c = omap2430_clks; c < omap2430_clks + ARRAY_SIZE(omap2430_clks);
	     c++)
		clk_preinit(c->lk.clk);

	osc_ck.rate = omap2_osc_clk_recalc(&osc_ck);
	propagate_rate(&osc_ck);
	sys_ck.rate = omap2xxx_sys_clk_recalc(&sys_ck);
	propagate_rate(&sys_ck);

	for (c = omap2430_clks; c < omap2430_clks + ARRAY_SIZE(omap2430_clks);
	     c++) {
		clkdev_add(&c->lk);
		clk_register(c->lk.clk);
		omap2_init_clk_clkdm(c->lk.clk);
	}

	/* Disable autoidle on all clocks; let the PM code enable it later */
	omap_clk_disable_autoidle_all();

	/* Check the MPU rate set by bootloader */
	clkrate = omap2xxx_clk_get_core_rate(&dpll_ck);
	for (prcm = rate_table; prcm->mpu_speed; prcm++) {
		if (!(prcm->flags & cpu_mask))
			continue;
		if (prcm->xtal_speed != sys_ck.rate)
			continue;
		if (prcm->dpll_speed <= clkrate)
			break;
	}
	curr_prcm_set = prcm;

	recalculate_root_clocks();

	pr_info("Clocking rate (Crystal/DPLL/MPU): %ld.%01ld/%ld/%ld MHz\n",
		(sys_ck.rate / 1000000), (sys_ck.rate / 100000) % 10,
		(dpll_ck.rate / 1000000), (mpu_ck.rate / 1000000)) ;

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_enable_init_clocks();

	/* Avoid sleeping sleeping during omap2_clk_prepare_for_reboot() */
	vclk = clk_get(NULL, "virt_prcm_set");
	sclk = clk_get(NULL, "sys_ck");
	dclk = clk_get(NULL, "dpll_ck");

	return 0;
}

