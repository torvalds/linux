/*
 * AM33XX Clock data
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 * Vaibhav Hiremath <hvaibhav@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <plat/clkdev_omap.h>

#include "am33xx.h"
#include "iomap.h"
#include "control.h"
#include "clock.h"
#include "cm.h"
#include "cm33xx.h"
#include "cm-regbits-33xx.h"
#include "prm.h"

/* Maximum DPLL multiplier, divider values for AM33XX */
#define AM33XX_MAX_DPLL_MULT		2047
#define AM33XX_MAX_DPLL_DIV		128

/* Modulemode control */
#define AM33XX_MODULEMODE_HWCTRL	0
#define AM33XX_MODULEMODE_SWCTRL	1

/* TRM ERRATA: Timer 3 & 6 default parent (TCLKIN) may not be always
 *    physically present, in such a case HWMOD enabling of
 *    clock would be failure with default parent. And timer
 *    probe thinks clock is already enabled, this leads to
 *    crash upon accessing timer 3 & 6 registers in probe.
 *    Fix by setting parent of both these timers to master
 *    oscillator clock.
 */
static inline void am33xx_init_timer_parent(struct clk *clk)
{
	omap2_clksel_set_parent(clk, clk->parent);
}

/* Root clocks */

/* RTC 32k */
static struct clk clk_32768_ck = {
	.name		= "clk_32768_ck",
	.clkdm_name	= "l4_rtc_clkdm",
	.rate		= 32768,
	.ops		= &clkops_null,
};

/* On-Chip 32KHz RC OSC */
static struct clk clk_rc32k_ck = {
	.name		= "clk_rc32k_ck",
	.rate		= 32000,
	.ops		= &clkops_null,
};

/* Crystal input clks */
static struct clk virt_24000000_ck = {
	.name		= "virt_24000000_ck",
	.rate		= 24000000,
	.ops		= &clkops_null,
};

static struct clk virt_25000000_ck = {
	.name		= "virt_25000000_ck",
	.rate		= 25000000,
	.ops		= &clkops_null,
};

/* Oscillator clock */
/* 19.2, 24, 25 or 26 MHz */
static const struct clksel sys_clkin_sel[] = {
	{ .parent = &virt_19200000_ck, .rates = div_1_0_rates },
	{ .parent = &virt_24000000_ck, .rates = div_1_1_rates },
	{ .parent = &virt_25000000_ck, .rates = div_1_2_rates },
	{ .parent = &virt_26000000_ck, .rates = div_1_3_rates },
	{ .parent = NULL },
};

/* External clock - 12 MHz */
static struct clk tclkin_ck = {
	.name		= "tclkin_ck",
	.rate		= 12000000,
	.ops		= &clkops_null,
};

/*
 * sys_clk in: input to the dpll and also used as funtional clock for,
 *   adc_tsc, smartreflex0-1, timer1-7, mcasp0-1, dcan0-1, cefuse
 *
 */
static struct clk sys_clkin_ck = {
	.name		= "sys_clkin_ck",
	.parent		= &virt_24000000_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel_reg	= AM33XX_CTRL_REGADDR(AM33XX_CONTROL_STATUS),
	.clksel_mask	= AM33XX_CONTROL_STATUS_SYSBOOT1_MASK,
	.clksel		= sys_clkin_sel,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

/* DPLL_CORE */
static struct dpll_data dpll_core_dd = {
	.mult_div1_reg	= AM33XX_CM_CLKSEL_DPLL_CORE,
	.clk_bypass	= &sys_clkin_ck,
	.clk_ref	= &sys_clkin_ck,
	.control_reg	= AM33XX_CM_CLKMODE_DPLL_CORE,
	.modes		= (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	.idlest_reg	= AM33XX_CM_IDLEST_DPLL_CORE,
	.mult_mask	= AM33XX_DPLL_MULT_MASK,
	.div1_mask	= AM33XX_DPLL_DIV_MASK,
	.enable_mask	= AM33XX_DPLL_EN_MASK,
	.idlest_mask	= AM33XX_ST_DPLL_CLK_MASK,
	.max_multiplier	= AM33XX_MAX_DPLL_MULT,
	.max_divider	= AM33XX_MAX_DPLL_DIV,
	.min_divider	= 1,
};

/* CLKDCOLDO output */
static struct clk dpll_core_ck = {
	.name		= "dpll_core_ck",
	.parent		= &sys_clkin_ck,
	.dpll_data	= &dpll_core_dd,
	.init		= &omap2_init_dpll_parent,
	.ops		= &clkops_omap3_core_dpll_ops,
	.recalc		= &omap3_dpll_recalc,
};

static struct clk dpll_core_x2_ck = {
	.name		= "dpll_core_x2_ck",
	.parent		= &dpll_core_ck,
	.flags		= CLOCK_CLKOUTX2,
	.ops		= &clkops_null,
	.recalc		= &omap3_clkoutx2_recalc,
};


static const struct clksel dpll_core_m4_div[] = {
	{ .parent = &dpll_core_x2_ck, .rates = div31_1to31_rates },
	{ .parent = NULL },
};

static struct clk dpll_core_m4_ck = {
	.name		= "dpll_core_m4_ck",
	.parent		= &dpll_core_x2_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= dpll_core_m4_div,
	.clksel_reg	= AM33XX_CM_DIV_M4_DPLL_CORE,
	.clksel_mask	= AM33XX_HSDIVIDER_CLKOUT1_DIV_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate,
};

static const struct clksel dpll_core_m5_div[] = {
	{ .parent = &dpll_core_x2_ck, .rates = div31_1to31_rates },
	{ .parent = NULL },
};

static struct clk dpll_core_m5_ck = {
	.name		= "dpll_core_m5_ck",
	.parent		= &dpll_core_x2_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= dpll_core_m5_div,
	.clksel_reg	= AM33XX_CM_DIV_M5_DPLL_CORE,
	.clksel_mask	= AM33XX_HSDIVIDER_CLKOUT2_DIV_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate,
};

static const struct clksel dpll_core_m6_div[] = {
	{ .parent = &dpll_core_x2_ck, .rates = div31_1to31_rates },
	{ .parent = NULL },
};

static struct clk dpll_core_m6_ck = {
	.name		= "dpll_core_m6_ck",
	.parent		= &dpll_core_x2_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= dpll_core_m6_div,
	.clksel_reg	= AM33XX_CM_DIV_M6_DPLL_CORE,
	.clksel_mask	= AM33XX_HSDIVIDER_CLKOUT3_DIV_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate,
};

/* DPLL_MPU */
static struct dpll_data dpll_mpu_dd = {
	.mult_div1_reg	= AM33XX_CM_CLKSEL_DPLL_MPU,
	.clk_bypass	= &sys_clkin_ck,
	.clk_ref	= &sys_clkin_ck,
	.control_reg	= AM33XX_CM_CLKMODE_DPLL_MPU,
	.modes		= (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	.idlest_reg	= AM33XX_CM_IDLEST_DPLL_MPU,
	.mult_mask	= AM33XX_DPLL_MULT_MASK,
	.div1_mask	= AM33XX_DPLL_DIV_MASK,
	.enable_mask	= AM33XX_DPLL_EN_MASK,
	.idlest_mask	= AM33XX_ST_DPLL_CLK_MASK,
	.max_multiplier	= AM33XX_MAX_DPLL_MULT,
	.max_divider	= AM33XX_MAX_DPLL_DIV,
	.min_divider	= 1,
};

/* CLKOUT: fdpll/M2 */
static struct clk dpll_mpu_ck = {
	.name		= "dpll_mpu_ck",
	.parent		= &sys_clkin_ck,
	.dpll_data	= &dpll_mpu_dd,
	.init		= &omap2_init_dpll_parent,
	.ops		= &clkops_omap3_noncore_dpll_ops,
	.recalc		= &omap3_dpll_recalc,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
};

/*
 * TODO: Add clksel here (sys_clkin, CORE_CLKOUTM6, PER_CLKOUTM2
 * and ALT_CLK1/2)
 */
static const struct clksel dpll_mpu_m2_div[] = {
	{ .parent = &dpll_mpu_ck, .rates = div31_1to31_rates },
	{ .parent = NULL },
};

static struct clk dpll_mpu_m2_ck = {
	.name		= "dpll_mpu_m2_ck",
	.clkdm_name	= "mpu_clkdm",
	.parent		= &dpll_mpu_ck,
	.clksel		= dpll_mpu_m2_div,
	.clksel_reg	= AM33XX_CM_DIV_M2_DPLL_MPU,
	.clksel_mask	= AM33XX_DPLL_CLKOUT_DIV_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate,
};

/* DPLL_DDR */
static struct dpll_data dpll_ddr_dd = {
	.mult_div1_reg	= AM33XX_CM_CLKSEL_DPLL_DDR,
	.clk_bypass	= &sys_clkin_ck,
	.clk_ref	= &sys_clkin_ck,
	.control_reg	= AM33XX_CM_CLKMODE_DPLL_DDR,
	.modes		= (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	.idlest_reg	= AM33XX_CM_IDLEST_DPLL_DDR,
	.mult_mask	= AM33XX_DPLL_MULT_MASK,
	.div1_mask	= AM33XX_DPLL_DIV_MASK,
	.enable_mask	= AM33XX_DPLL_EN_MASK,
	.idlest_mask	= AM33XX_ST_DPLL_CLK_MASK,
	.max_multiplier	= AM33XX_MAX_DPLL_MULT,
	.max_divider	= AM33XX_MAX_DPLL_DIV,
	.min_divider	= 1,
};

/* CLKOUT: fdpll/M2 */
static struct clk dpll_ddr_ck = {
	.name		= "dpll_ddr_ck",
	.parent		= &sys_clkin_ck,
	.dpll_data	= &dpll_ddr_dd,
	.init		= &omap2_init_dpll_parent,
	.ops		= &clkops_null,
	.recalc		= &omap3_dpll_recalc,
};

/*
 * TODO: Add clksel here (sys_clkin, CORE_CLKOUTM6, PER_CLKOUTM2
 * and ALT_CLK1/2)
 */
static const struct clksel dpll_ddr_m2_div[] = {
	{ .parent = &dpll_ddr_ck, .rates = div31_1to31_rates },
	{ .parent = NULL },
};

static struct clk dpll_ddr_m2_ck = {
	.name		= "dpll_ddr_m2_ck",
	.parent		= &dpll_ddr_ck,
	.clksel		= dpll_ddr_m2_div,
	.clksel_reg	= AM33XX_CM_DIV_M2_DPLL_DDR,
	.clksel_mask	= AM33XX_DPLL_CLKOUT_DIV_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate,
};

/* emif_fck functional clock */
static struct clk dpll_ddr_m2_div2_ck = {
	.name		= "dpll_ddr_m2_div2_ck",
	.clkdm_name	= "l3_clkdm",
	.parent		= &dpll_ddr_m2_ck,
	.ops		= &clkops_null,
	.fixed_div	= 2,
	.recalc		= &omap_fixed_divisor_recalc,
};

/* DPLL_DISP */
static struct dpll_data dpll_disp_dd = {
	.mult_div1_reg	= AM33XX_CM_CLKSEL_DPLL_DISP,
	.clk_bypass	= &sys_clkin_ck,
	.clk_ref	= &sys_clkin_ck,
	.control_reg	= AM33XX_CM_CLKMODE_DPLL_DISP,
	.modes		= (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	.idlest_reg	= AM33XX_CM_IDLEST_DPLL_DISP,
	.mult_mask	= AM33XX_DPLL_MULT_MASK,
	.div1_mask	= AM33XX_DPLL_DIV_MASK,
	.enable_mask	= AM33XX_DPLL_EN_MASK,
	.idlest_mask	= AM33XX_ST_DPLL_CLK_MASK,
	.max_multiplier	= AM33XX_MAX_DPLL_MULT,
	.max_divider	= AM33XX_MAX_DPLL_DIV,
	.min_divider	= 1,
};

/* CLKOUT: fdpll/M2 */
static struct clk dpll_disp_ck = {
	.name		= "dpll_disp_ck",
	.parent		= &sys_clkin_ck,
	.dpll_data	= &dpll_disp_dd,
	.init		= &omap2_init_dpll_parent,
	.ops		= &clkops_null,
	.recalc		= &omap3_dpll_recalc,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
};

/*
 * TODO: Add clksel here (sys_clkin, CORE_CLKOUTM6, PER_CLKOUTM2
 * and ALT_CLK1/2)
 */
static const struct clksel dpll_disp_m2_div[] = {
	{ .parent = &dpll_disp_ck, .rates = div31_1to31_rates },
	{ .parent = NULL },
};

static struct clk dpll_disp_m2_ck = {
	.name		= "dpll_disp_m2_ck",
	.parent		= &dpll_disp_ck,
	.clksel		= dpll_disp_m2_div,
	.clksel_reg	= AM33XX_CM_DIV_M2_DPLL_DISP,
	.clksel_mask	= AM33XX_DPLL_CLKOUT_DIV_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate,
};

/* DPLL_PER */
static struct dpll_data dpll_per_dd = {
	.mult_div1_reg	= AM33XX_CM_CLKSEL_DPLL_PERIPH,
	.clk_bypass	= &sys_clkin_ck,
	.clk_ref	= &sys_clkin_ck,
	.control_reg	= AM33XX_CM_CLKMODE_DPLL_PER,
	.modes		= (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	.idlest_reg	= AM33XX_CM_IDLEST_DPLL_PER,
	.mult_mask	= AM33XX_DPLL_MULT_PERIPH_MASK,
	.div1_mask	= AM33XX_DPLL_PER_DIV_MASK,
	.enable_mask	= AM33XX_DPLL_EN_MASK,
	.idlest_mask	= AM33XX_ST_DPLL_CLK_MASK,
	.max_multiplier	= AM33XX_MAX_DPLL_MULT,
	.max_divider	= AM33XX_MAX_DPLL_DIV,
	.min_divider	= 1,
	.flags		= DPLL_J_TYPE,
};

/* CLKDCOLDO */
static struct clk dpll_per_ck = {
	.name		= "dpll_per_ck",
	.parent		= &sys_clkin_ck,
	.dpll_data	= &dpll_per_dd,
	.init		= &omap2_init_dpll_parent,
	.ops		= &clkops_null,
	.recalc		= &omap3_dpll_recalc,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
};

/* CLKOUT: fdpll/M2 */
static const struct clksel dpll_per_m2_div[] = {
	{ .parent = &dpll_per_ck, .rates = div31_1to31_rates },
	{ .parent = NULL },
};

static struct clk dpll_per_m2_ck = {
	.name		= "dpll_per_m2_ck",
	.parent		= &dpll_per_ck,
	.clksel		= dpll_per_m2_div,
	.clksel_reg	= AM33XX_CM_DIV_M2_DPLL_PER,
	.clksel_mask	= AM33XX_DPLL_CLKOUT_DIV_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate,
};

static struct clk dpll_per_m2_div4_wkupdm_ck = {
	.name		= "dpll_per_m2_div4_wkupdm_ck",
	.clkdm_name	= "l4_wkup_clkdm",
	.parent		= &dpll_per_m2_ck,
	.fixed_div	= 4,
	.ops		= &clkops_null,
	.recalc		= &omap_fixed_divisor_recalc,
};

static struct clk dpll_per_m2_div4_ck = {
	.name		= "dpll_per_m2_div4_ck",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &dpll_per_m2_ck,
	.fixed_div	= 4,
	.ops		= &clkops_null,
	.recalc		= &omap_fixed_divisor_recalc,
};

static struct clk l3_gclk = {
	.name		= "l3_gclk",
	.clkdm_name	= "l3_clkdm",
	.parent		= &dpll_core_m4_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk dpll_core_m4_div2_ck = {
	.name		= "dpll_core_m4_div2_ck",
	.clkdm_name	= "l4_wkup_clkdm",
	.parent		= &dpll_core_m4_ck,
	.ops		= &clkops_null,
	.fixed_div	= 2,
	.recalc		= &omap_fixed_divisor_recalc,
};

static struct clk l4_rtc_gclk = {
	.name		= "l4_rtc_gclk",
	.parent		= &dpll_core_m4_ck,
	.ops		= &clkops_null,
	.fixed_div	= 2,
	.recalc		= &omap_fixed_divisor_recalc,
};

static struct clk clk_24mhz = {
	.name		= "clk_24mhz",
	.parent		= &dpll_per_m2_ck,
	.fixed_div	= 8,
	.ops		= &clkops_null,
	.recalc		= &omap_fixed_divisor_recalc,
};

/*
 * Below clock nodes describes clockdomains derived out
 * of core clock.
 */
static struct clk l4hs_gclk = {
	.name		= "l4hs_gclk",
	.clkdm_name	= "l4hs_clkdm",
	.parent		= &dpll_core_m4_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk l3s_gclk = {
	.name		= "l3s_gclk",
	.clkdm_name	= "l3s_clkdm",
	.parent		= &dpll_core_m4_div2_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk l4fw_gclk = {
	.name		= "l4fw_gclk",
	.clkdm_name	= "l4fw_clkdm",
	.parent		= &dpll_core_m4_div2_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk l4ls_gclk = {
	.name		= "l4ls_gclk",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &dpll_core_m4_div2_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk sysclk_div_ck = {
	.name		= "sysclk_div_ck",
	.parent		= &dpll_core_m4_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

/*
 * In order to match the clock domain with hwmod clockdomain entry,
 * separate clock nodes is required for the modules which are
 * directly getting their funtioncal clock from sys_clkin.
 */
static struct clk adc_tsc_fck = {
	.name		= "adc_tsc_fck",
	.clkdm_name	= "l4_wkup_clkdm",
	.parent		= &sys_clkin_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk dcan0_fck = {
	.name		= "dcan0_fck",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &sys_clkin_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk dcan1_fck = {
	.name		= "dcan1_fck",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &sys_clkin_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk mcasp0_fck = {
	.name		= "mcasp0_fck",
	.clkdm_name	= "l3s_clkdm",
	.parent		= &sys_clkin_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk mcasp1_fck = {
	.name		= "mcasp1_fck",
	.clkdm_name	= "l3s_clkdm",
	.parent		= &sys_clkin_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk smartreflex0_fck = {
	.name		= "smartreflex0_fck",
	.clkdm_name	= "l4_wkup_clkdm",
	.parent		= &sys_clkin_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk smartreflex1_fck = {
	.name		= "smartreflex1_fck",
	.clkdm_name	= "l4_wkup_clkdm",
	.parent		= &sys_clkin_ck,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

/*
 * Modules clock nodes
 *
 * The following clock leaf nodes are added for the moment because:
 *
 *  - hwmod data is not present for these modules, either hwmod
 *    control is not required or its not populated.
 *  - Driver code is not yet migrated to use hwmod/runtime pm
 *  - Modules outside kernel access (to disable them by default)
 *
 *     - debugss
 *     - mmu (gfx domain)
 *     - cefuse
 *     - usbotg_fck (its additional clock and not really a modulemode)
 *     - ieee5000
 */
static struct clk debugss_ick = {
	.name		= "debugss_ick",
	.clkdm_name	= "l3_aon_clkdm",
	.parent		= &dpll_core_m4_ck,
	.ops		= &clkops_omap2_dflt,
	.enable_reg	= AM33XX_CM_WKUP_DEBUGSS_CLKCTRL,
	.enable_bit	= AM33XX_MODULEMODE_SWCTRL,
	.recalc		= &followparent_recalc,
};

static struct clk mmu_fck = {
	.name		= "mmu_fck",
	.clkdm_name	= "gfx_l3_clkdm",
	.parent		= &dpll_core_m4_ck,
	.ops		= &clkops_omap2_dflt,
	.enable_reg	= AM33XX_CM_GFX_MMUDATA_CLKCTRL,
	.enable_bit	= AM33XX_MODULEMODE_SWCTRL,
	.recalc		= &followparent_recalc,
};

static struct clk cefuse_fck = {
	.name		= "cefuse_fck",
	.clkdm_name	= "l4_cefuse_clkdm",
	.parent		= &sys_clkin_ck,
	.enable_reg	= AM33XX_CM_CEFUSE_CEFUSE_CLKCTRL,
	.enable_bit	= AM33XX_MODULEMODE_SWCTRL,
	.ops		= &clkops_omap2_dflt,
	.recalc		= &followparent_recalc,
};

/*
 * clkdiv32 is generated from fixed division of 732.4219
 */
static struct clk clkdiv32k_ick = {
	.name		= "clkdiv32k_ick",
	.clkdm_name	= "clk_24mhz_clkdm",
	.rate		= 32768,
	.parent		= &clk_24mhz,
	.enable_reg	= AM33XX_CM_PER_CLKDIV32K_CLKCTRL,
	.enable_bit	= AM33XX_MODULEMODE_SWCTRL,
	.ops		= &clkops_omap2_dflt,
};

static struct clk usbotg_fck = {
	.name		= "usbotg_fck",
	.clkdm_name	= "l3s_clkdm",
	.parent		= &dpll_per_ck,
	.enable_reg	= AM33XX_CM_CLKDCOLDO_DPLL_PER,
	.enable_bit	= AM33XX_ST_DPLL_CLKDCOLDO_SHIFT,
	.ops		= &clkops_omap2_dflt,
	.recalc		= &followparent_recalc,
};

static struct clk ieee5000_fck = {
	.name		= "ieee5000_fck",
	.clkdm_name	= "l3s_clkdm",
	.parent		= &dpll_core_m4_div2_ck,
	.enable_reg	= AM33XX_CM_PER_IEEE5000_CLKCTRL,
	.enable_bit	= AM33XX_MODULEMODE_SWCTRL,
	.ops		= &clkops_omap2_dflt,
	.recalc		= &followparent_recalc,
};

/* Timers */
static const struct clksel timer1_clkmux_sel[] = {
	{ .parent = &sys_clkin_ck, .rates = div_1_0_rates },
	{ .parent = &clkdiv32k_ick, .rates = div_1_1_rates },
	{ .parent = &tclkin_ck, .rates = div_1_2_rates },
	{ .parent = &clk_rc32k_ck, .rates = div_1_3_rates },
	{ .parent = &clk_32768_ck, .rates = div_1_4_rates },
	{ .parent = NULL },
};

static struct clk timer1_fck = {
	.name		= "timer1_fck",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &sys_clkin_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= timer1_clkmux_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER1MS_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_2_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel timer2_to_7_clk_sel[] = {
	{ .parent = &tclkin_ck, .rates = div_1_0_rates },
	{ .parent = &sys_clkin_ck, .rates = div_1_1_rates },
	{ .parent = &clkdiv32k_ick, .rates = div_1_2_rates },
	{ .parent = NULL },
};

static struct clk timer2_fck = {
	.name		= "timer2_fck",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &sys_clkin_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER2_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk timer3_fck = {
	.name		= "timer3_fck",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &sys_clkin_ck,
	.init		= &am33xx_init_timer_parent,
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER3_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk timer4_fck = {
	.name		= "timer4_fck",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &sys_clkin_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER4_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk timer5_fck = {
	.name		= "timer5_fck",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &sys_clkin_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER5_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk timer6_fck = {
	.name		= "timer6_fck",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &sys_clkin_ck,
	.init		= &am33xx_init_timer_parent,
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER6_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk timer7_fck = {
	.name		= "timer7_fck",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &sys_clkin_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER7_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk cpsw_125mhz_gclk = {
	.name		= "cpsw_125mhz_gclk",
	.clkdm_name	= "cpsw_125mhz_clkdm",
	.parent		= &dpll_core_m5_ck,
	.ops		= &clkops_null,
	.fixed_div	= 2,
	.recalc		= &omap_fixed_divisor_recalc,
};

static const struct clksel cpsw_cpts_rft_clkmux_sel[] = {
	{ .parent = &dpll_core_m5_ck, .rates = div_1_0_rates },
	{ .parent = &dpll_core_m4_ck, .rates = div_1_1_rates },
	{ .parent = NULL },
};

static struct clk cpsw_cpts_rft_clk = {
	.name		= "cpsw_cpts_rft_clk",
	.clkdm_name	= "cpsw_125mhz_clkdm",
	.parent		= &dpll_core_m5_ck,
	.clksel		= cpsw_cpts_rft_clkmux_sel,
	.clksel_reg	= AM33XX_CM_CPTS_RFT_CLKSEL,
	.clksel_mask	= AM33XX_CLKSEL_0_0_MASK,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

/* gpio */
static const struct clksel gpio0_dbclk_mux_sel[] = {
	{ .parent = &clk_rc32k_ck, .rates = div_1_0_rates },
	{ .parent = &clk_32768_ck, .rates = div_1_1_rates },
	{ .parent = &clkdiv32k_ick, .rates = div_1_2_rates },
	{ .parent = NULL },
};

static struct clk gpio0_dbclk_mux_ck = {
	.name		= "gpio0_dbclk_mux_ck",
	.clkdm_name	= "l4_wkup_clkdm",
	.parent		= &clk_rc32k_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= gpio0_dbclk_mux_sel,
	.clksel_reg	= AM33XX_CLKSEL_GPIO0_DBCLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

static struct clk gpio0_dbclk = {
	.name		= "gpio0_dbclk",
	.clkdm_name	= "l4_wkup_clkdm",
	.parent		= &gpio0_dbclk_mux_ck,
	.enable_reg	= AM33XX_CM_WKUP_GPIO0_CLKCTRL,
	.enable_bit	= AM33XX_OPTFCLKEN_GPIO0_GDBCLK_SHIFT,
	.ops		= &clkops_omap2_dflt,
	.recalc		= &followparent_recalc,
};

static struct clk gpio1_dbclk = {
	.name		= "gpio1_dbclk",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &clkdiv32k_ick,
	.enable_reg	= AM33XX_CM_PER_GPIO1_CLKCTRL,
	.enable_bit	= AM33XX_OPTFCLKEN_GPIO_1_GDBCLK_SHIFT,
	.ops		= &clkops_omap2_dflt,
	.recalc		= &followparent_recalc,
};

static struct clk gpio2_dbclk = {
	.name		= "gpio2_dbclk",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &clkdiv32k_ick,
	.enable_reg	= AM33XX_CM_PER_GPIO2_CLKCTRL,
	.enable_bit	= AM33XX_OPTFCLKEN_GPIO_2_GDBCLK_SHIFT,
	.ops		= &clkops_omap2_dflt,
	.recalc		= &followparent_recalc,
};

static struct clk gpio3_dbclk = {
	.name		= "gpio3_dbclk",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &clkdiv32k_ick,
	.enable_reg	= AM33XX_CM_PER_GPIO3_CLKCTRL,
	.enable_bit	= AM33XX_OPTFCLKEN_GPIO_3_GDBCLK_SHIFT,
	.ops		= &clkops_omap2_dflt,
	.recalc		= &followparent_recalc,
};

static const struct clksel pruss_ocp_clk_mux_sel[] = {
	{ .parent = &l3_gclk, .rates = div_1_0_rates },
	{ .parent = &dpll_disp_m2_ck, .rates = div_1_1_rates },
	{ .parent = NULL },
};

static struct clk pruss_ocp_gclk = {
	.name		= "pruss_ocp_gclk",
	.clkdm_name	= "pruss_ocp_clkdm",
	.parent		= &l3_gclk,
	.init		= &omap2_init_clksel_parent,
	.clksel		= pruss_ocp_clk_mux_sel,
	.clksel_reg	= AM33XX_CLKSEL_PRUSS_OCP_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_0_MASK,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static const struct clksel lcd_clk_mux_sel[] = {
	{ .parent = &dpll_disp_m2_ck, .rates = div_1_0_rates },
	{ .parent = &dpll_core_m5_ck, .rates = div_1_1_rates },
	{ .parent = &dpll_per_m2_ck, .rates = div_1_2_rates },
	{ .parent = NULL },
};

static struct clk lcd_gclk = {
	.name		= "lcd_gclk",
	.clkdm_name	= "lcdc_clkdm",
	.parent		= &dpll_disp_m2_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= lcd_clk_mux_sel,
	.clksel_reg	= AM33XX_CLKSEL_LCDC_PIXEL_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static struct clk mmc_clk = {
	.name		= "mmc_clk",
	.clkdm_name	= "l4ls_clkdm",
	.parent		= &dpll_per_m2_ck,
	.ops		= &clkops_null,
	.fixed_div	= 2,
	.recalc		= &omap_fixed_divisor_recalc,
};

static struct clk mmc2_fck = {
	.name		= "mmc2_fck",
	.clkdm_name	= "l3s_clkdm",
	.parent		= &mmc_clk,
	.ops		= &clkops_null,
	.recalc		= &followparent_recalc,
};

static const struct clksel gfx_clksel_sel[] = {
	{ .parent = &dpll_core_m4_ck, .rates = div_1_0_rates },
	{ .parent = &dpll_per_m2_ck, .rates = div_1_1_rates },
	{ .parent = NULL },
};

static struct clk gfx_fclk_clksel_ck = {
	.name		= "gfx_fclk_clksel_ck",
	.parent		= &dpll_core_m4_ck,
	.clksel		= gfx_clksel_sel,
	.ops		= &clkops_null,
	.clksel_reg	= AM33XX_CLKSEL_GFX_FCLK,
	.clksel_mask	= AM33XX_CLKSEL_GFX_FCLK_MASK,
	.recalc		= &omap2_clksel_recalc,
};

static const struct clksel_rate div_1_0_2_1_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_AM33XX },
	{ .div = 2, .val = 1, .flags = RATE_IN_AM33XX },
	{ .div = 0 },
};

static const struct clksel gfx_div_sel[] = {
	{ .parent = &gfx_fclk_clksel_ck, .rates = div_1_0_2_1_rates },
	{ .parent = NULL },
};

static struct clk gfx_fck_div_ck = {
	.name		= "gfx_fck_div_ck",
	.clkdm_name	= "gfx_l3_clkdm",
	.parent		= &gfx_fclk_clksel_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= gfx_div_sel,
	.clksel_reg	= AM33XX_CLKSEL_GFX_FCLK,
	.clksel_mask	= AM33XX_CLKSEL_0_0_MASK,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate,
	.ops		= &clkops_null,
};

static const struct clksel sysclkout_pre_sel[] = {
	{ .parent = &clk_32768_ck, .rates = div_1_0_rates },
	{ .parent = &l3_gclk, .rates = div_1_1_rates },
	{ .parent = &dpll_ddr_m2_ck, .rates = div_1_2_rates },
	{ .parent = &dpll_per_m2_ck, .rates = div_1_3_rates },
	{ .parent = &lcd_gclk, .rates = div_1_4_rates },
	{ .parent = NULL },
};

static struct clk sysclkout_pre_ck = {
	.name		= "sysclkout_pre_ck",
	.parent		= &clk_32768_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= sysclkout_pre_sel,
	.clksel_reg	= AM33XX_CM_CLKOUT_CTRL,
	.clksel_mask	= AM33XX_CLKOUT2SOURCE_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

/* Divide by 8 clock rates with default clock is 1/1*/
static const struct clksel_rate div8_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_AM33XX },
	{ .div = 2, .val = 1, .flags = RATE_IN_AM33XX },
	{ .div = 3, .val = 2, .flags = RATE_IN_AM33XX },
	{ .div = 4, .val = 3, .flags = RATE_IN_AM33XX },
	{ .div = 5, .val = 4, .flags = RATE_IN_AM33XX },
	{ .div = 6, .val = 5, .flags = RATE_IN_AM33XX },
	{ .div = 7, .val = 6, .flags = RATE_IN_AM33XX },
	{ .div = 8, .val = 7, .flags = RATE_IN_AM33XX },
	{ .div = 0 },
};

static const struct clksel clkout2_div[] = {
	{ .parent = &sysclkout_pre_ck, .rates = div8_rates },
	{ .parent = NULL },
};

static struct clk clkout2_ck = {
	.name		= "clkout2_ck",
	.parent		= &sysclkout_pre_ck,
	.ops		= &clkops_omap2_dflt,
	.clksel		= clkout2_div,
	.clksel_reg	= AM33XX_CM_CLKOUT_CTRL,
	.clksel_mask	= AM33XX_CLKOUT2DIV_MASK,
	.enable_reg	= AM33XX_CM_CLKOUT_CTRL,
	.enable_bit	= AM33XX_CLKOUT2EN_SHIFT,
	.recalc		= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
	.set_rate	= &omap2_clksel_set_rate,
};

static const struct clksel wdt_clkmux_sel[] = {
	{ .parent = &clk_rc32k_ck, .rates = div_1_0_rates },
	{ .parent = &clkdiv32k_ick, .rates = div_1_1_rates },
	{ .parent = NULL },
};

static struct clk wdt1_fck = {
	.name		= "wdt1_fck",
	.clkdm_name	= "l4_wkup_clkdm",
	.parent		= &clk_rc32k_ck,
	.init		= &omap2_init_clksel_parent,
	.clksel		= wdt_clkmux_sel,
	.clksel_reg	= AM33XX_CLKSEL_WDT1_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
	.ops		= &clkops_null,
	.recalc		= &omap2_clksel_recalc,
};

/*
 * clkdev
 */
static struct omap_clk am33xx_clks[] = {
	CLK(NULL,	"clk_32768_ck",		&clk_32768_ck,	CK_AM33XX),
	CLK(NULL,	"clk_rc32k_ck",		&clk_rc32k_ck,	CK_AM33XX),
	CLK(NULL,	"virt_19200000_ck",	&virt_19200000_ck,	CK_AM33XX),
	CLK(NULL,	"virt_24000000_ck",	&virt_24000000_ck,	CK_AM33XX),
	CLK(NULL,	"virt_25000000_ck",	&virt_25000000_ck,	CK_AM33XX),
	CLK(NULL,	"virt_26000000_ck",	&virt_26000000_ck,	CK_AM33XX),
	CLK(NULL,	"sys_clkin_ck",		&sys_clkin_ck,	CK_AM33XX),
	CLK(NULL,	"tclkin_ck",		&tclkin_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_core_ck",		&dpll_core_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_core_x2_ck",	&dpll_core_x2_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_core_m4_ck",	&dpll_core_m4_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_core_m5_ck",	&dpll_core_m5_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_core_m6_ck",	&dpll_core_m6_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_mpu_ck",		&dpll_mpu_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_mpu_m2_ck",	&dpll_mpu_m2_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_ddr_ck",		&dpll_ddr_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_ddr_m2_ck",	&dpll_ddr_m2_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_ddr_m2_div2_ck",	&dpll_ddr_m2_div2_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_disp_ck",		&dpll_disp_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_disp_m2_ck",	&dpll_disp_m2_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_per_ck",		&dpll_per_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_per_m2_ck",	&dpll_per_m2_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_per_m2_div4_wkupdm_ck",	&dpll_per_m2_div4_wkupdm_ck,	CK_AM33XX),
	CLK(NULL,	"dpll_per_m2_div4_ck",	&dpll_per_m2_div4_ck,	CK_AM33XX),
	CLK(NULL,	"adc_tsc_fck",		&adc_tsc_fck,	CK_AM33XX),
	CLK(NULL,	"cefuse_fck",		&cefuse_fck,	CK_AM33XX),
	CLK(NULL,	"clkdiv32k_ick",	&clkdiv32k_ick,	CK_AM33XX),
	CLK(NULL,	"dcan0_fck",		&dcan0_fck,	CK_AM33XX),
	CLK("481cc000.d_can",	NULL,		&dcan0_fck,	CK_AM33XX),
	CLK(NULL,	"dcan1_fck",		&dcan1_fck,	CK_AM33XX),
	CLK("481d0000.d_can",	NULL,		&dcan1_fck,	CK_AM33XX),
	CLK(NULL,	"debugss_ick",		&debugss_ick,	CK_AM33XX),
	CLK(NULL,	"pruss_ocp_gclk",	&pruss_ocp_gclk,	CK_AM33XX),
	CLK("davinci-mcasp.0",  NULL,           &mcasp0_fck,    CK_AM33XX),
	CLK("davinci-mcasp.1",  NULL,           &mcasp1_fck,    CK_AM33XX),
	CLK("NULL",	"mmc2_fck",		&mmc2_fck,	CK_AM33XX),
	CLK(NULL,	"mmu_fck",		&mmu_fck,	CK_AM33XX),
	CLK(NULL,	"smartreflex0_fck",	&smartreflex0_fck,	CK_AM33XX),
	CLK(NULL,	"smartreflex1_fck",	&smartreflex1_fck,	CK_AM33XX),
	CLK(NULL,	"timer1_fck",		&timer1_fck,	CK_AM33XX),
	CLK(NULL,	"timer2_fck",		&timer2_fck,	CK_AM33XX),
	CLK(NULL,	"timer3_fck",		&timer3_fck,	CK_AM33XX),
	CLK(NULL,	"timer4_fck",		&timer4_fck,	CK_AM33XX),
	CLK(NULL,	"timer5_fck",		&timer5_fck,	CK_AM33XX),
	CLK(NULL,	"timer6_fck",		&timer6_fck,	CK_AM33XX),
	CLK(NULL,	"timer7_fck",		&timer7_fck,	CK_AM33XX),
	CLK(NULL,	"usbotg_fck",		&usbotg_fck,	CK_AM33XX),
	CLK(NULL,	"ieee5000_fck",		&ieee5000_fck,	CK_AM33XX),
	CLK(NULL,	"wdt1_fck",		&wdt1_fck,	CK_AM33XX),
	CLK(NULL,	"l4_rtc_gclk",		&l4_rtc_gclk,	CK_AM33XX),
	CLK(NULL,	"l3_gclk",		&l3_gclk,	CK_AM33XX),
	CLK(NULL,	"dpll_core_m4_div2_ck",	&dpll_core_m4_div2_ck,	CK_AM33XX),
	CLK(NULL,	"l4hs_gclk",		&l4hs_gclk,	CK_AM33XX),
	CLK(NULL,	"l3s_gclk",		&l3s_gclk,	CK_AM33XX),
	CLK(NULL,	"l4fw_gclk",		&l4fw_gclk,	CK_AM33XX),
	CLK(NULL,	"l4ls_gclk",		&l4ls_gclk,	CK_AM33XX),
	CLK(NULL,	"clk_24mhz",		&clk_24mhz,	CK_AM33XX),
	CLK(NULL,	"sysclk_div_ck",	&sysclk_div_ck,	CK_AM33XX),
	CLK(NULL,	"cpsw_125mhz_gclk",	&cpsw_125mhz_gclk,	CK_AM33XX),
	CLK(NULL,	"cpsw_cpts_rft_clk",	&cpsw_cpts_rft_clk,	CK_AM33XX),
	CLK(NULL,	"gpio0_dbclk_mux_ck",	&gpio0_dbclk_mux_ck,	CK_AM33XX),
	CLK(NULL,	"gpio0_dbclk",		&gpio0_dbclk,	CK_AM33XX),
	CLK(NULL,	"gpio1_dbclk",		&gpio1_dbclk,	CK_AM33XX),
	CLK(NULL,	"gpio2_dbclk",		&gpio2_dbclk,	CK_AM33XX),
	CLK(NULL,	"gpio3_dbclk",		&gpio3_dbclk,	CK_AM33XX),
	CLK(NULL,	"lcd_gclk",		&lcd_gclk,	CK_AM33XX),
	CLK(NULL,	"mmc_clk",		&mmc_clk,	CK_AM33XX),
	CLK(NULL,	"gfx_fclk_clksel_ck",	&gfx_fclk_clksel_ck,	CK_AM33XX),
	CLK(NULL,	"gfx_fck_div_ck",	&gfx_fck_div_ck,	CK_AM33XX),
	CLK(NULL,	"sysclkout_pre_ck",	&sysclkout_pre_ck,	CK_AM33XX),
	CLK(NULL,	"clkout2_ck",		&clkout2_ck,	CK_AM33XX),
};

int __init am33xx_clk_init(void)
{
	struct omap_clk *c;
	u32 cpu_clkflg;

	if (soc_is_am33xx()) {
		cpu_mask = RATE_IN_AM33XX;
		cpu_clkflg = CK_AM33XX;
	}

	clk_init(&omap2_clk_functions);

	for (c = am33xx_clks; c < am33xx_clks + ARRAY_SIZE(am33xx_clks); c++)
		clk_preinit(c->lk.clk);

	for (c = am33xx_clks; c < am33xx_clks + ARRAY_SIZE(am33xx_clks); c++) {
		if (c->cpu & cpu_clkflg) {
			clkdev_add(&c->lk);
			clk_register(c->lk.clk);
			omap2_init_clk_clkdm(c->lk.clk);
		}
	}

	recalculate_root_clocks();

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_enable_init_clocks();

	return 0;
}
