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
#include <linux/clk-private.h>
#include <linux/clkdev.h>
#include <linux/io.h>

#include "am33xx.h"
#include "soc.h"
#include "iomap.h"
#include "clock.h"
#include "control.h"
#include "cm.h"
#include "cm33xx.h"
#include "cm-regbits-33xx.h"
#include "prm.h"

/* Modulemode control */
#define AM33XX_MODULEMODE_HWCTRL_SHIFT		0
#define AM33XX_MODULEMODE_SWCTRL_SHIFT		1

/*LIST_HEAD(clocks);*/

/* Root clocks */

/* RTC 32k */
DEFINE_CLK_FIXED_RATE(clk_32768_ck, CLK_IS_ROOT, 32768, 0x0);

/* On-Chip 32KHz RC OSC */
DEFINE_CLK_FIXED_RATE(clk_rc32k_ck, CLK_IS_ROOT, 32000, 0x0);

/* Crystal input clks */
DEFINE_CLK_FIXED_RATE(virt_19200000_ck, CLK_IS_ROOT, 19200000, 0x0);

DEFINE_CLK_FIXED_RATE(virt_24000000_ck, CLK_IS_ROOT, 24000000, 0x0);

DEFINE_CLK_FIXED_RATE(virt_25000000_ck, CLK_IS_ROOT, 25000000, 0x0);

DEFINE_CLK_FIXED_RATE(virt_26000000_ck, CLK_IS_ROOT, 26000000, 0x0);

/* Oscillator clock */
/* 19.2, 24, 25 or 26 MHz */
static const char *sys_clkin_ck_parents[] = {
	"virt_19200000_ck", "virt_24000000_ck", "virt_25000000_ck",
	"virt_26000000_ck",
};

/*
 * sys_clk in: input to the dpll and also used as funtional clock for,
 *   adc_tsc, smartreflex0-1, timer1-7, mcasp0-1, dcan0-1, cefuse
 *
 */
DEFINE_CLK_MUX(sys_clkin_ck, sys_clkin_ck_parents, NULL, 0x0,
	       AM33XX_CTRL_REGADDR(AM33XX_CONTROL_STATUS),
	       AM33XX_CONTROL_STATUS_SYSBOOT1_SHIFT,
	       AM33XX_CONTROL_STATUS_SYSBOOT1_WIDTH,
	       0, NULL);

/* External clock - 12 MHz */
DEFINE_CLK_FIXED_RATE(tclkin_ck, CLK_IS_ROOT, 12000000, 0x0);

/* Module clocks and DPLL outputs */

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
	.max_multiplier	= 2047,
	.max_divider	= 128,
	.min_divider	= 1,
};

/* CLKDCOLDO output */
static const char *dpll_core_ck_parents[] = {
	"sys_clkin_ck",
};

static struct clk dpll_core_ck;

static const struct clk_ops dpll_core_ck_ops = {
	.recalc_rate	= &omap3_dpll_recalc,
	.get_parent	= &omap2_init_dpll_parent,
};

static struct clk_hw_omap dpll_core_ck_hw = {
	.hw	= {
		.clk	= &dpll_core_ck,
	},
	.dpll_data	= &dpll_core_dd,
	.ops		= &clkhwops_omap3_dpll,
};

DEFINE_STRUCT_CLK(dpll_core_ck, dpll_core_ck_parents, dpll_core_ck_ops);

static const char *dpll_core_x2_ck_parents[] = {
	"dpll_core_ck",
};

static struct clk dpll_core_x2_ck;

static const struct clk_ops dpll_x2_ck_ops = {
	.recalc_rate	= &omap3_clkoutx2_recalc,
};

static struct clk_hw_omap dpll_core_x2_ck_hw = {
	.hw	= {
		.clk	= &dpll_core_x2_ck,
	},
	.flags		= CLOCK_CLKOUTX2,
};

DEFINE_STRUCT_CLK(dpll_core_x2_ck, dpll_core_x2_ck_parents, dpll_x2_ck_ops);

DEFINE_CLK_DIVIDER(dpll_core_m4_ck, "dpll_core_x2_ck", &dpll_core_x2_ck,
		   0x0, AM33XX_CM_DIV_M4_DPLL_CORE,
		   AM33XX_HSDIVIDER_CLKOUT1_DIV_SHIFT,
		   AM33XX_HSDIVIDER_CLKOUT1_DIV_WIDTH, CLK_DIVIDER_ONE_BASED,
		   NULL);

DEFINE_CLK_DIVIDER(dpll_core_m5_ck, "dpll_core_x2_ck", &dpll_core_x2_ck,
		   0x0, AM33XX_CM_DIV_M5_DPLL_CORE,
		   AM33XX_HSDIVIDER_CLKOUT2_DIV_SHIFT,
		   AM33XX_HSDIVIDER_CLKOUT2_DIV_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

DEFINE_CLK_DIVIDER(dpll_core_m6_ck, "dpll_core_x2_ck", &dpll_core_x2_ck,
		   0x0, AM33XX_CM_DIV_M6_DPLL_CORE,
		   AM33XX_HSDIVIDER_CLKOUT3_DIV_SHIFT,
		   AM33XX_HSDIVIDER_CLKOUT3_DIV_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);


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
	.max_multiplier	= 2047,
	.max_divider	= 128,
	.min_divider	= 1,
};

/* CLKOUT: fdpll/M2 */
static struct clk dpll_mpu_ck;

static const struct clk_ops dpll_mpu_ck_ops = {
	.enable		= &omap3_noncore_dpll_enable,
	.disable	= &omap3_noncore_dpll_disable,
	.recalc_rate	= &omap3_dpll_recalc,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.get_parent	= &omap2_init_dpll_parent,
};

static struct clk_hw_omap dpll_mpu_ck_hw = {
	.hw = {
		.clk	= &dpll_mpu_ck,
	},
	.dpll_data	= &dpll_mpu_dd,
	.ops		= &clkhwops_omap3_dpll,
};

DEFINE_STRUCT_CLK(dpll_mpu_ck, dpll_core_ck_parents, dpll_mpu_ck_ops);

/*
 * TODO: Add clksel here (sys_clkin, CORE_CLKOUTM6, PER_CLKOUTM2
 * and ALT_CLK1/2)
 */
DEFINE_CLK_DIVIDER(dpll_mpu_m2_ck, "dpll_mpu_ck", &dpll_mpu_ck,
		   0x0, AM33XX_CM_DIV_M2_DPLL_MPU, AM33XX_DPLL_CLKOUT_DIV_SHIFT,
		   AM33XX_DPLL_CLKOUT_DIV_WIDTH, CLK_DIVIDER_ONE_BASED, NULL);

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
	.max_multiplier	= 2047,
	.max_divider	= 128,
	.min_divider	= 1,
};

/* CLKOUT: fdpll/M2 */
static struct clk dpll_ddr_ck;

static const struct clk_ops dpll_ddr_ck_ops = {
	.recalc_rate	= &omap3_dpll_recalc,
	.get_parent	= &omap2_init_dpll_parent,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
};

static struct clk_hw_omap dpll_ddr_ck_hw = {
	.hw = {
		.clk	= &dpll_ddr_ck,
	},
	.dpll_data	= &dpll_ddr_dd,
	.ops		= &clkhwops_omap3_dpll,
};

DEFINE_STRUCT_CLK(dpll_ddr_ck, dpll_core_ck_parents, dpll_ddr_ck_ops);

/*
 * TODO: Add clksel here (sys_clkin, CORE_CLKOUTM6, PER_CLKOUTM2
 * and ALT_CLK1/2)
 */
DEFINE_CLK_DIVIDER(dpll_ddr_m2_ck, "dpll_ddr_ck", &dpll_ddr_ck,
		   0x0, AM33XX_CM_DIV_M2_DPLL_DDR,
		   AM33XX_DPLL_CLKOUT_DIV_SHIFT, AM33XX_DPLL_CLKOUT_DIV_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

/* emif_fck functional clock */
DEFINE_CLK_FIXED_FACTOR(dpll_ddr_m2_div2_ck, "dpll_ddr_m2_ck", &dpll_ddr_m2_ck,
			0x0, 1, 2);

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
	.max_multiplier	= 2047,
	.max_divider	= 128,
	.min_divider	= 1,
};

/* CLKOUT: fdpll/M2 */
static struct clk dpll_disp_ck;

static struct clk_hw_omap dpll_disp_ck_hw = {
	.hw = {
		.clk	= &dpll_disp_ck,
	},
	.dpll_data	= &dpll_disp_dd,
	.ops		= &clkhwops_omap3_dpll,
};

DEFINE_STRUCT_CLK(dpll_disp_ck, dpll_core_ck_parents, dpll_ddr_ck_ops);

/*
 * TODO: Add clksel here (sys_clkin, CORE_CLKOUTM6, PER_CLKOUTM2
 * and ALT_CLK1/2)
 */
DEFINE_CLK_DIVIDER(dpll_disp_m2_ck, "dpll_disp_ck", &dpll_disp_ck, 0x0,
		   AM33XX_CM_DIV_M2_DPLL_DISP, AM33XX_DPLL_CLKOUT_DIV_SHIFT,
		   AM33XX_DPLL_CLKOUT_DIV_WIDTH, CLK_DIVIDER_ONE_BASED, NULL);

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
	.max_multiplier	= 2047,
	.max_divider	= 128,
	.min_divider	= 1,
	.flags		= DPLL_J_TYPE,
};

/* CLKDCOLDO */
static struct clk dpll_per_ck;

static struct clk_hw_omap dpll_per_ck_hw = {
	.hw	= {
		.clk	= &dpll_per_ck,
	},
	.dpll_data	= &dpll_per_dd,
	.ops		= &clkhwops_omap3_dpll,
};

DEFINE_STRUCT_CLK(dpll_per_ck, dpll_core_ck_parents, dpll_ddr_ck_ops);

/* CLKOUT: fdpll/M2 */
DEFINE_CLK_DIVIDER(dpll_per_m2_ck, "dpll_per_ck", &dpll_per_ck, 0x0,
		   AM33XX_CM_DIV_M2_DPLL_PER, AM33XX_DPLL_CLKOUT_DIV_SHIFT,
		   AM33XX_DPLL_CLKOUT_DIV_WIDTH, CLK_DIVIDER_ONE_BASED,
		   NULL);

DEFINE_CLK_FIXED_FACTOR(dpll_per_m2_div4_wkupdm_ck, "dpll_per_m2_ck",
			&dpll_per_m2_ck, 0x0, 1, 4);

DEFINE_CLK_FIXED_FACTOR(dpll_per_m2_div4_ck, "dpll_per_m2_ck",
			&dpll_per_m2_ck, 0x0, 1, 4);

DEFINE_CLK_FIXED_FACTOR(dpll_core_m4_div2_ck, "dpll_core_m4_ck",
			&dpll_core_m4_ck, 0x0, 1, 2);

DEFINE_CLK_FIXED_FACTOR(l4_rtc_gclk, "dpll_core_m4_ck", &dpll_core_m4_ck, 0x0,
			1, 2);

DEFINE_CLK_FIXED_FACTOR(clk_24mhz, "dpll_per_m2_ck", &dpll_per_m2_ck, 0x0, 1,
			8);

/*
 * Below clock nodes describes clockdomains derived out
 * of core clock.
 */
static const struct clk_ops clk_ops_null = {
};

static const char *l3_gclk_parents[] = {
	"dpll_core_m4_ck"
};

static struct clk l3_gclk;
DEFINE_STRUCT_CLK_HW_OMAP(l3_gclk, NULL);
DEFINE_STRUCT_CLK(l3_gclk, l3_gclk_parents, clk_ops_null);

static struct clk l4hs_gclk;
DEFINE_STRUCT_CLK_HW_OMAP(l4hs_gclk, NULL);
DEFINE_STRUCT_CLK(l4hs_gclk, l3_gclk_parents, clk_ops_null);

static const char *l3s_gclk_parents[] = {
	"dpll_core_m4_div2_ck"
};

static struct clk l3s_gclk;
DEFINE_STRUCT_CLK_HW_OMAP(l3s_gclk, NULL);
DEFINE_STRUCT_CLK(l3s_gclk, l3s_gclk_parents, clk_ops_null);

static struct clk l4fw_gclk;
DEFINE_STRUCT_CLK_HW_OMAP(l4fw_gclk, NULL);
DEFINE_STRUCT_CLK(l4fw_gclk, l3s_gclk_parents, clk_ops_null);

static struct clk l4ls_gclk;
DEFINE_STRUCT_CLK_HW_OMAP(l4ls_gclk, NULL);
DEFINE_STRUCT_CLK(l4ls_gclk, l3s_gclk_parents, clk_ops_null);

static struct clk sysclk_div_ck;
DEFINE_STRUCT_CLK_HW_OMAP(sysclk_div_ck, NULL);
DEFINE_STRUCT_CLK(sysclk_div_ck, l3_gclk_parents, clk_ops_null);

/*
 * In order to match the clock domain with hwmod clockdomain entry,
 * separate clock nodes is required for the modules which are
 * directly getting their funtioncal clock from sys_clkin.
 */
static struct clk adc_tsc_fck;
DEFINE_STRUCT_CLK_HW_OMAP(adc_tsc_fck, NULL);
DEFINE_STRUCT_CLK(adc_tsc_fck, dpll_core_ck_parents, clk_ops_null);

static struct clk dcan0_fck;
DEFINE_STRUCT_CLK_HW_OMAP(dcan0_fck, NULL);
DEFINE_STRUCT_CLK(dcan0_fck, dpll_core_ck_parents, clk_ops_null);

static struct clk dcan1_fck;
DEFINE_STRUCT_CLK_HW_OMAP(dcan1_fck, NULL);
DEFINE_STRUCT_CLK(dcan1_fck, dpll_core_ck_parents, clk_ops_null);

static struct clk mcasp0_fck;
DEFINE_STRUCT_CLK_HW_OMAP(mcasp0_fck, NULL);
DEFINE_STRUCT_CLK(mcasp0_fck, dpll_core_ck_parents, clk_ops_null);

static struct clk mcasp1_fck;
DEFINE_STRUCT_CLK_HW_OMAP(mcasp1_fck, NULL);
DEFINE_STRUCT_CLK(mcasp1_fck, dpll_core_ck_parents, clk_ops_null);

static struct clk smartreflex0_fck;
DEFINE_STRUCT_CLK_HW_OMAP(smartreflex0_fck, NULL);
DEFINE_STRUCT_CLK(smartreflex0_fck, dpll_core_ck_parents, clk_ops_null);

static struct clk smartreflex1_fck;
DEFINE_STRUCT_CLK_HW_OMAP(smartreflex1_fck, NULL);
DEFINE_STRUCT_CLK(smartreflex1_fck, dpll_core_ck_parents, clk_ops_null);

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
DEFINE_CLK_GATE(debugss_ick, "dpll_core_m4_ck", &dpll_core_m4_ck, 0x0,
		AM33XX_CM_WKUP_DEBUGSS_CLKCTRL, AM33XX_MODULEMODE_SWCTRL_SHIFT,
		0x0, NULL);

DEFINE_CLK_GATE(mmu_fck, "dpll_core_m4_ck", &dpll_core_m4_ck, 0x0,
		AM33XX_CM_GFX_MMUDATA_CLKCTRL, AM33XX_MODULEMODE_SWCTRL_SHIFT,
		0x0, NULL);

DEFINE_CLK_GATE(cefuse_fck, "sys_clkin_ck", &sys_clkin_ck, 0x0,
		AM33XX_CM_CEFUSE_CEFUSE_CLKCTRL, AM33XX_MODULEMODE_SWCTRL_SHIFT,
		0x0, NULL);

/*
 * clkdiv32 is generated from fixed division of 732.4219
 */
DEFINE_CLK_FIXED_FACTOR(clkdiv32k_ck, "clk_24mhz", &clk_24mhz, 0x0, 1, 732);

DEFINE_CLK_GATE(clkdiv32k_ick, "clkdiv32k_ck", &clkdiv32k_ck, 0x0,
		AM33XX_CM_PER_CLKDIV32K_CLKCTRL, AM33XX_MODULEMODE_SWCTRL_SHIFT,
		0x0, NULL);

/* "usbotg_fck" is an additional clock and not really a modulemode */
DEFINE_CLK_GATE(usbotg_fck, "dpll_per_ck", &dpll_per_ck, 0x0,
		AM33XX_CM_CLKDCOLDO_DPLL_PER, AM33XX_ST_DPLL_CLKDCOLDO_SHIFT,
		0x0, NULL);

DEFINE_CLK_GATE(ieee5000_fck, "dpll_core_m4_div2_ck", &dpll_core_m4_div2_ck,
		0x0, AM33XX_CM_PER_IEEE5000_CLKCTRL,
		AM33XX_MODULEMODE_SWCTRL_SHIFT, 0x0, NULL);

/* Timers */
static const struct clksel timer1_clkmux_sel[] = {
	{ .parent = &sys_clkin_ck, .rates = div_1_0_rates },
	{ .parent = &clkdiv32k_ick, .rates = div_1_1_rates },
	{ .parent = &tclkin_ck, .rates = div_1_2_rates },
	{ .parent = &clk_rc32k_ck, .rates = div_1_3_rates },
	{ .parent = &clk_32768_ck, .rates = div_1_4_rates },
	{ .parent = NULL },
};

static const char *timer1_ck_parents[] = {
	"sys_clkin_ck", "clkdiv32k_ick", "tclkin_ck", "clk_rc32k_ck",
	"clk_32768_ck",
};

static struct clk timer1_fck;

static const struct clk_ops timer1_fck_ops = {
	.recalc_rate	= &omap2_clksel_recalc,
	.get_parent	= &omap2_clksel_find_parent_index,
	.set_parent	= &omap2_clksel_set_parent,
	.init		= &omap2_init_clk_clkdm,
};

static struct clk_hw_omap timer1_fck_hw = {
	.hw	= {
		.clk	= &timer1_fck,
	},
	.clkdm_name	= "l4ls_clkdm",
	.clksel		= timer1_clkmux_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER1MS_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_2_MASK,
};

DEFINE_STRUCT_CLK(timer1_fck, timer1_ck_parents, timer1_fck_ops);

static const struct clksel timer2_to_7_clk_sel[] = {
	{ .parent = &tclkin_ck, .rates = div_1_0_rates },
	{ .parent = &sys_clkin_ck, .rates = div_1_1_rates },
	{ .parent = &clkdiv32k_ick, .rates = div_1_2_rates },
	{ .parent = NULL },
};

static const char *timer2_to_7_ck_parents[] = {
	"tclkin_ck", "sys_clkin_ck", "clkdiv32k_ick",
};

static struct clk timer2_fck;

static struct clk_hw_omap timer2_fck_hw = {
	.hw	= {
		.clk	= &timer2_fck,
	},
	.clkdm_name	= "l4ls_clkdm",
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER2_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
};

DEFINE_STRUCT_CLK(timer2_fck, timer2_to_7_ck_parents, timer1_fck_ops);

static struct clk timer3_fck;

static struct clk_hw_omap timer3_fck_hw = {
	.hw	= {
		.clk	= &timer3_fck,
	},
	.clkdm_name	= "l4ls_clkdm",
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER3_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
};

DEFINE_STRUCT_CLK(timer3_fck, timer2_to_7_ck_parents, timer1_fck_ops);

static struct clk timer4_fck;

static struct clk_hw_omap timer4_fck_hw = {
	.hw	= {
		.clk	= &timer4_fck,
	},
	.clkdm_name	= "l4ls_clkdm",
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER4_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
};

DEFINE_STRUCT_CLK(timer4_fck, timer2_to_7_ck_parents, timer1_fck_ops);

static struct clk timer5_fck;

static struct clk_hw_omap timer5_fck_hw = {
	.hw	= {
		.clk	= &timer5_fck,
	},
	.clkdm_name	= "l4ls_clkdm",
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER5_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
};

DEFINE_STRUCT_CLK(timer5_fck, timer2_to_7_ck_parents, timer1_fck_ops);

static struct clk timer6_fck;

static struct clk_hw_omap timer6_fck_hw = {
	.hw	= {
		.clk	= &timer6_fck,
	},
	.clkdm_name	= "l4ls_clkdm",
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER6_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
};

DEFINE_STRUCT_CLK(timer6_fck, timer2_to_7_ck_parents, timer1_fck_ops);

static struct clk timer7_fck;

static struct clk_hw_omap timer7_fck_hw = {
	.hw	= {
		.clk	= &timer7_fck,
	},
	.clkdm_name	= "l4ls_clkdm",
	.clksel		= timer2_to_7_clk_sel,
	.clksel_reg	= AM33XX_CLKSEL_TIMER7_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
};

DEFINE_STRUCT_CLK(timer7_fck, timer2_to_7_ck_parents, timer1_fck_ops);

DEFINE_CLK_FIXED_FACTOR(cpsw_125mhz_gclk,
			"dpll_core_m5_ck",
			&dpll_core_m5_ck,
			0x0,
			1, 2);

static const struct clk_ops cpsw_fck_ops = {
	.recalc_rate	= &omap2_clksel_recalc,
	.get_parent	= &omap2_clksel_find_parent_index,
	.set_parent	= &omap2_clksel_set_parent,
};

static const struct clksel cpsw_cpts_rft_clkmux_sel[] = {
	{ .parent = &dpll_core_m5_ck, .rates = div_1_0_rates },
	{ .parent = &dpll_core_m4_ck, .rates = div_1_1_rates },
	{ .parent = NULL },
};

static const char *cpsw_cpts_rft_ck_parents[] = {
	"dpll_core_m5_ck", "dpll_core_m4_ck",
};

static struct clk cpsw_cpts_rft_clk;

static struct clk_hw_omap cpsw_cpts_rft_clk_hw = {
	.hw	= {
		.clk	= &cpsw_cpts_rft_clk,
	},
	.clkdm_name	= "cpsw_125mhz_clkdm",
	.clksel		= cpsw_cpts_rft_clkmux_sel,
	.clksel_reg	= AM33XX_CM_CPTS_RFT_CLKSEL,
	.clksel_mask	= AM33XX_CLKSEL_0_0_MASK,
};

DEFINE_STRUCT_CLK(cpsw_cpts_rft_clk, cpsw_cpts_rft_ck_parents, cpsw_fck_ops);


/* gpio */
static const char *gpio0_ck_parents[] = {
	"clk_rc32k_ck", "clk_32768_ck", "clkdiv32k_ick",
};

static const struct clksel gpio0_dbclk_mux_sel[] = {
	{ .parent = &clk_rc32k_ck, .rates = div_1_0_rates },
	{ .parent = &clk_32768_ck, .rates = div_1_1_rates },
	{ .parent = &clkdiv32k_ick, .rates = div_1_2_rates },
	{ .parent = NULL },
};

static const struct clk_ops gpio_fck_ops = {
	.recalc_rate	= &omap2_clksel_recalc,
	.get_parent	= &omap2_clksel_find_parent_index,
	.set_parent	= &omap2_clksel_set_parent,
	.init		= &omap2_init_clk_clkdm,
};

static struct clk gpio0_dbclk_mux_ck;

static struct clk_hw_omap gpio0_dbclk_mux_ck_hw = {
	.hw	= {
		.clk	= &gpio0_dbclk_mux_ck,
	},
	.clkdm_name	= "l4_wkup_clkdm",
	.clksel		= gpio0_dbclk_mux_sel,
	.clksel_reg	= AM33XX_CLKSEL_GPIO0_DBCLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
};

DEFINE_STRUCT_CLK(gpio0_dbclk_mux_ck, gpio0_ck_parents, gpio_fck_ops);

DEFINE_CLK_GATE(gpio0_dbclk, "gpio0_dbclk_mux_ck", &gpio0_dbclk_mux_ck, 0x0,
		AM33XX_CM_WKUP_GPIO0_CLKCTRL,
		AM33XX_OPTFCLKEN_GPIO0_GDBCLK_SHIFT, 0x0, NULL);

DEFINE_CLK_GATE(gpio1_dbclk, "clkdiv32k_ick", &clkdiv32k_ick, 0x0,
		AM33XX_CM_PER_GPIO1_CLKCTRL,
		AM33XX_OPTFCLKEN_GPIO_1_GDBCLK_SHIFT, 0x0, NULL);

DEFINE_CLK_GATE(gpio2_dbclk, "clkdiv32k_ick", &clkdiv32k_ick, 0x0,
		AM33XX_CM_PER_GPIO2_CLKCTRL,
		AM33XX_OPTFCLKEN_GPIO_2_GDBCLK_SHIFT, 0x0, NULL);

DEFINE_CLK_GATE(gpio3_dbclk, "clkdiv32k_ick", &clkdiv32k_ick, 0x0,
		AM33XX_CM_PER_GPIO3_CLKCTRL,
		AM33XX_OPTFCLKEN_GPIO_3_GDBCLK_SHIFT, 0x0, NULL);


static const char *pruss_ck_parents[] = {
	"l3_gclk", "dpll_disp_m2_ck",
};

static const struct clksel pruss_ocp_clk_mux_sel[] = {
	{ .parent = &l3_gclk, .rates = div_1_0_rates },
	{ .parent = &dpll_disp_m2_ck, .rates = div_1_1_rates },
	{ .parent = NULL },
};

static struct clk pruss_ocp_gclk;

static struct clk_hw_omap pruss_ocp_gclk_hw = {
	.hw	= {
		.clk	= &pruss_ocp_gclk,
	},
	.clkdm_name	= "pruss_ocp_clkdm",
	.clksel		= pruss_ocp_clk_mux_sel,
	.clksel_reg	= AM33XX_CLKSEL_PRUSS_OCP_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_0_MASK,
};

DEFINE_STRUCT_CLK(pruss_ocp_gclk, pruss_ck_parents, gpio_fck_ops);

static const char *lcd_ck_parents[] = {
	"dpll_disp_m2_ck", "dpll_core_m5_ck", "dpll_per_m2_ck",
};

static const struct clksel lcd_clk_mux_sel[] = {
	{ .parent = &dpll_disp_m2_ck, .rates = div_1_0_rates },
	{ .parent = &dpll_core_m5_ck, .rates = div_1_1_rates },
	{ .parent = &dpll_per_m2_ck, .rates = div_1_2_rates },
	{ .parent = NULL },
};

static struct clk lcd_gclk;

static struct clk_hw_omap lcd_gclk_hw = {
	.hw	= {
		.clk	= &lcd_gclk,
	},
	.clkdm_name	= "lcdc_clkdm",
	.clksel		= lcd_clk_mux_sel,
	.clksel_reg	= AM33XX_CLKSEL_LCDC_PIXEL_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
};

DEFINE_STRUCT_CLK(lcd_gclk, lcd_ck_parents, gpio_fck_ops);

DEFINE_CLK_FIXED_FACTOR(mmc_clk, "dpll_per_m2_ck", &dpll_per_m2_ck, 0x0, 1, 2);

static const char *gfx_ck_parents[] = {
	"dpll_core_m4_ck", "dpll_per_m2_ck",
};

static const struct clksel gfx_clksel_sel[] = {
	{ .parent = &dpll_core_m4_ck, .rates = div_1_0_rates },
	{ .parent = &dpll_per_m2_ck, .rates = div_1_1_rates },
	{ .parent = NULL },
};

static struct clk gfx_fclk_clksel_ck;

static struct clk_hw_omap gfx_fclk_clksel_ck_hw = {
	.hw	= {
		.clk	= &gfx_fclk_clksel_ck,
	},
	.clksel		= gfx_clksel_sel,
	.clksel_reg	= AM33XX_CLKSEL_GFX_FCLK,
	.clksel_mask	= AM33XX_CLKSEL_GFX_FCLK_MASK,
};

DEFINE_STRUCT_CLK(gfx_fclk_clksel_ck, gfx_ck_parents, gpio_fck_ops);

static const struct clk_div_table div_1_0_2_1_rates[] = {
	{ .div = 1, .val = 0, },
	{ .div = 2, .val = 1, },
	{ .div = 0 },
};

DEFINE_CLK_DIVIDER_TABLE(gfx_fck_div_ck, "gfx_fclk_clksel_ck",
			 &gfx_fclk_clksel_ck, 0x0, AM33XX_CLKSEL_GFX_FCLK,
			 AM33XX_CLKSEL_0_0_SHIFT, AM33XX_CLKSEL_0_0_WIDTH,
			 0x0, div_1_0_2_1_rates, NULL);

static const char *sysclkout_ck_parents[] = {
	"clk_32768_ck", "l3_gclk", "dpll_ddr_m2_ck", "dpll_per_m2_ck",
	"lcd_gclk",
};

static const struct clksel sysclkout_pre_sel[] = {
	{ .parent = &clk_32768_ck, .rates = div_1_0_rates },
	{ .parent = &l3_gclk, .rates = div_1_1_rates },
	{ .parent = &dpll_ddr_m2_ck, .rates = div_1_2_rates },
	{ .parent = &dpll_per_m2_ck, .rates = div_1_3_rates },
	{ .parent = &lcd_gclk, .rates = div_1_4_rates },
	{ .parent = NULL },
};

static struct clk sysclkout_pre_ck;

static struct clk_hw_omap sysclkout_pre_ck_hw = {
	.hw	= {
		.clk	= &sysclkout_pre_ck,
	},
	.clksel		= sysclkout_pre_sel,
	.clksel_reg	= AM33XX_CM_CLKOUT_CTRL,
	.clksel_mask	= AM33XX_CLKOUT2SOURCE_MASK,
};

DEFINE_STRUCT_CLK(sysclkout_pre_ck, sysclkout_ck_parents, gpio_fck_ops);

/* Divide by 8 clock rates with default clock is 1/1*/
static const struct clk_div_table div8_rates[] = {
	{ .div = 1, .val = 0, },
	{ .div = 2, .val = 1, },
	{ .div = 3, .val = 2, },
	{ .div = 4, .val = 3, },
	{ .div = 5, .val = 4, },
	{ .div = 6, .val = 5, },
	{ .div = 7, .val = 6, },
	{ .div = 8, .val = 7, },
	{ .div = 0 },
};

DEFINE_CLK_DIVIDER_TABLE(clkout2_div_ck, "sysclkout_pre_ck", &sysclkout_pre_ck,
			 0x0, AM33XX_CM_CLKOUT_CTRL, AM33XX_CLKOUT2DIV_SHIFT,
			 AM33XX_CLKOUT2DIV_WIDTH, 0x0, div8_rates, NULL);

DEFINE_CLK_GATE(clkout2_ck, "clkout2_div_ck", &clkout2_div_ck, 0x0,
		AM33XX_CM_CLKOUT_CTRL, AM33XX_CLKOUT2EN_SHIFT, 0x0, NULL);

static const char *wdt_ck_parents[] = {
	"clk_rc32k_ck", "clkdiv32k_ick",
};

static const struct clksel wdt_clkmux_sel[] = {
	{ .parent = &clk_rc32k_ck, .rates = div_1_0_rates },
	{ .parent = &clkdiv32k_ick, .rates = div_1_1_rates },
	{ .parent = NULL },
};

static struct clk wdt1_fck;

static struct clk_hw_omap wdt1_fck_hw = {
	.hw	= {
		.clk	= &wdt1_fck,
	},
	.clkdm_name	= "l4_wkup_clkdm",
	.clksel		= wdt_clkmux_sel,
	.clksel_reg	= AM33XX_CLKSEL_WDT1_CLK,
	.clksel_mask	= AM33XX_CLKSEL_0_1_MASK,
};

DEFINE_STRUCT_CLK(wdt1_fck, wdt_ck_parents, gpio_fck_ops);

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
	CLK("cpu0",	NULL,			&dpll_mpu_ck,	CK_AM33XX),
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
	CLK(NULL,	"clkdiv32k_ck",		&clkdiv32k_ck,	CK_AM33XX),
	CLK(NULL,	"clkdiv32k_ick",	&clkdiv32k_ick,	CK_AM33XX),
	CLK(NULL,	"dcan0_fck",		&dcan0_fck,	CK_AM33XX),
	CLK("481cc000.d_can",	NULL,		&dcan0_fck,	CK_AM33XX),
	CLK(NULL,	"dcan1_fck",		&dcan1_fck,	CK_AM33XX),
	CLK("481d0000.d_can",	NULL,		&dcan1_fck,	CK_AM33XX),
	CLK(NULL,	"debugss_ick",		&debugss_ick,	CK_AM33XX),
	CLK(NULL,	"pruss_ocp_gclk",	&pruss_ocp_gclk,	CK_AM33XX),
	CLK(NULL,	"mcasp0_fck",		&mcasp0_fck,	CK_AM33XX),
	CLK(NULL,	"mcasp1_fck",		&mcasp1_fck,	CK_AM33XX),
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
	CLK(NULL,	"clkout2_div_ck",	&clkout2_div_ck,	CK_AM33XX),
	CLK(NULL,	"timer_32k_ck",		&clkdiv32k_ick,	CK_AM33XX),
	CLK(NULL,	"timer_sys_ck",		&sys_clkin_ck,	CK_AM33XX),
};


static const char *enable_init_clks[] = {
	"dpll_ddr_m2_ck",
	"dpll_mpu_m2_ck",
	"l3_gclk",
	"l4hs_gclk",
	"l4fw_gclk",
	"l4ls_gclk",
};

int __init am33xx_clk_init(void)
{
	struct omap_clk *c;
	u32 cpu_clkflg;

	if (soc_is_am33xx()) {
		cpu_mask = RATE_IN_AM33XX;
		cpu_clkflg = CK_AM33XX;
	}

	for (c = am33xx_clks; c < am33xx_clks + ARRAY_SIZE(am33xx_clks); c++) {
		if (c->cpu & cpu_clkflg) {
			clkdev_add(&c->lk);
			if (!__clk_init(NULL, c->lk.clk))
				omap2_init_clk_hw_omap_clocks(c->lk.clk);
		}
	}

	omap2_clk_disable_autoidle_all();

	omap2_clk_enable_init_clocks(enable_init_clks,
				     ARRAY_SIZE(enable_init_clks));

	/* TRM ERRATA: Timer 3 & 6 default parent (TCLKIN) may not be always
	 *    physically present, in such a case HWMOD enabling of
	 *    clock would be failure with default parent. And timer
	 *    probe thinks clock is already enabled, this leads to
	 *    crash upon accessing timer 3 & 6 registers in probe.
	 *    Fix by setting parent of both these timers to master
	 *    oscillator clock.
	 */

	clk_set_parent(&timer3_fck, &sys_clkin_ck);
	clk_set_parent(&timer6_fck, &sys_clkin_ck);

	return 0;
}
