/*
 * OMAP3 Legacy clock data
 *
 * Copyright (C) 2014 Texas Instruments, Inc
 *     Tero Kristo (t-kristo@ti.com)
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
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>

#include "clock.h"

static struct ti_clk_fixed virt_12m_ck_data = {
	.frequency = 12000000,
};

static struct ti_clk virt_12m_ck = {
	.name = "virt_12m_ck",
	.type = TI_CLK_FIXED,
	.data = &virt_12m_ck_data,
};

static struct ti_clk_fixed virt_13m_ck_data = {
	.frequency = 13000000,
};

static struct ti_clk virt_13m_ck = {
	.name = "virt_13m_ck",
	.type = TI_CLK_FIXED,
	.data = &virt_13m_ck_data,
};

static struct ti_clk_fixed virt_19200000_ck_data = {
	.frequency = 19200000,
};

static struct ti_clk virt_19200000_ck = {
	.name = "virt_19200000_ck",
	.type = TI_CLK_FIXED,
	.data = &virt_19200000_ck_data,
};

static struct ti_clk_fixed virt_26000000_ck_data = {
	.frequency = 26000000,
};

static struct ti_clk virt_26000000_ck = {
	.name = "virt_26000000_ck",
	.type = TI_CLK_FIXED,
	.data = &virt_26000000_ck_data,
};

static struct ti_clk_fixed virt_38_4m_ck_data = {
	.frequency = 38400000,
};

static struct ti_clk virt_38_4m_ck = {
	.name = "virt_38_4m_ck",
	.type = TI_CLK_FIXED,
	.data = &virt_38_4m_ck_data,
};

static struct ti_clk_fixed virt_16_8m_ck_data = {
	.frequency = 16800000,
};

static struct ti_clk virt_16_8m_ck = {
	.name = "virt_16_8m_ck",
	.type = TI_CLK_FIXED,
	.data = &virt_16_8m_ck_data,
};

static const char *osc_sys_ck_parents[] = {
	"virt_12m_ck",
	"virt_13m_ck",
	"virt_19200000_ck",
	"virt_26000000_ck",
	"virt_38_4m_ck",
	"virt_16_8m_ck",
};

static struct ti_clk_mux osc_sys_ck_data = {
	.num_parents = ARRAY_SIZE(osc_sys_ck_parents),
	.reg = 0xd40,
	.module = TI_CLKM_PRM,
	.parents = osc_sys_ck_parents,
};

static struct ti_clk osc_sys_ck = {
	.name = "osc_sys_ck",
	.type = TI_CLK_MUX,
	.data = &osc_sys_ck_data,
};

static struct ti_clk_divider sys_ck_data = {
	.parent = "osc_sys_ck",
	.bit_shift = 6,
	.max_div = 3,
	.reg = 0x1270,
	.module = TI_CLKM_PRM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk sys_ck = {
	.name = "sys_ck",
	.type = TI_CLK_DIVIDER,
	.data = &sys_ck_data,
};

static const char *dpll3_ck_parents[] = {
	"sys_ck",
	"sys_ck",
};

static struct ti_clk_dpll dpll3_ck_data = {
	.num_parents = ARRAY_SIZE(dpll3_ck_parents),
	.control_reg = 0xd00,
	.idlest_reg = 0xd20,
	.mult_div1_reg = 0xd40,
	.autoidle_reg = 0xd30,
	.module = TI_CLKM_CM,
	.parents = dpll3_ck_parents,
	.flags = CLKF_CORE,
	.freqsel_mask = 0xf0,
	.div1_mask = 0x7f00,
	.idlest_mask = 0x1,
	.auto_recal_bit = 0x3,
	.max_divider = 0x80,
	.min_divider = 0x1,
	.recal_en_bit = 0x5,
	.max_multiplier = 0x7ff,
	.enable_mask = 0x7,
	.mult_mask = 0x7ff0000,
	.recal_st_bit = 0x5,
	.autoidle_mask = 0x7,
};

static struct ti_clk dpll3_ck = {
	.name = "dpll3_ck",
	.clkdm_name = "dpll3_clkdm",
	.type = TI_CLK_DPLL,
	.data = &dpll3_ck_data,
};

static struct ti_clk_divider dpll3_m2_ck_data = {
	.parent = "dpll3_ck",
	.bit_shift = 27,
	.max_div = 31,
	.reg = 0xd40,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll3_m2_ck = {
	.name = "dpll3_m2_ck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll3_m2_ck_data,
};

static struct ti_clk_fixed_factor core_ck_data = {
	.parent = "dpll3_m2_ck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk core_ck = {
	.name = "core_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &core_ck_data,
};

static struct ti_clk_divider l3_ick_data = {
	.parent = "core_ck",
	.max_div = 3,
	.reg = 0xa40,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk l3_ick = {
	.name = "l3_ick",
	.type = TI_CLK_DIVIDER,
	.data = &l3_ick_data,
};

static struct ti_clk_fixed_factor security_l3_ick_data = {
	.parent = "l3_ick",
	.div = 1,
	.mult = 1,
};

static struct ti_clk security_l3_ick = {
	.name = "security_l3_ick",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &security_l3_ick_data,
};

static struct ti_clk_fixed_factor wkup_l4_ick_data = {
	.parent = "sys_ck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk wkup_l4_ick = {
	.name = "wkup_l4_ick",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &wkup_l4_ick_data,
};

static struct ti_clk_gate usim_ick_data = {
	.parent = "wkup_l4_ick",
	.bit_shift = 9,
	.reg = 0xc10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk usim_ick = {
	.name = "usim_ick",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &usim_ick_data,
};

static struct ti_clk_gate dss2_alwon_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 1,
	.reg = 0xe00,
	.module = TI_CLKM_CM,
};

static struct ti_clk dss2_alwon_fck = {
	.name = "dss2_alwon_fck",
	.clkdm_name = "dss_clkdm",
	.type = TI_CLK_GATE,
	.data = &dss2_alwon_fck_data,
};

static struct ti_clk_divider l4_ick_data = {
	.parent = "l3_ick",
	.bit_shift = 2,
	.max_div = 3,
	.reg = 0xa40,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk l4_ick = {
	.name = "l4_ick",
	.type = TI_CLK_DIVIDER,
	.data = &l4_ick_data,
};

static struct ti_clk_fixed_factor core_l4_ick_data = {
	.parent = "l4_ick",
	.div = 1,
	.mult = 1,
};

static struct ti_clk core_l4_ick = {
	.name = "core_l4_ick",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &core_l4_ick_data,
};

static struct ti_clk_gate mmchs2_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 25,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mmchs2_ick = {
	.name = "mmchs2_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mmchs2_ick_data,
};

static const char *dpll4_ck_parents[] = {
	"sys_ck",
	"sys_ck",
};

static struct ti_clk_dpll dpll4_ck_data = {
	.num_parents = ARRAY_SIZE(dpll4_ck_parents),
	.control_reg = 0xd00,
	.idlest_reg = 0xd20,
	.mult_div1_reg = 0xd44,
	.autoidle_reg = 0xd30,
	.module = TI_CLKM_CM,
	.parents = dpll4_ck_parents,
	.flags = CLKF_PER,
	.freqsel_mask = 0xf00000,
	.modes = 0x82,
	.div1_mask = 0x7f,
	.idlest_mask = 0x2,
	.auto_recal_bit = 0x13,
	.max_divider = 0x80,
	.min_divider = 0x1,
	.recal_en_bit = 0x6,
	.max_multiplier = 0x7ff,
	.enable_mask = 0x70000,
	.mult_mask = 0x7ff00,
	.recal_st_bit = 0x6,
	.autoidle_mask = 0x38,
};

static struct ti_clk dpll4_ck = {
	.name = "dpll4_ck",
	.clkdm_name = "dpll4_clkdm",
	.type = TI_CLK_DPLL,
	.data = &dpll4_ck_data,
};

static struct ti_clk_divider dpll4_m2_ck_data = {
	.parent = "dpll4_ck",
	.max_div = 63,
	.reg = 0xd48,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll4_m2_ck = {
	.name = "dpll4_m2_ck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll4_m2_ck_data,
};

static struct ti_clk_fixed_factor dpll4_m2x2_mul_ck_data = {
	.parent = "dpll4_m2_ck",
	.div = 1,
	.mult = 2,
};

static struct ti_clk dpll4_m2x2_mul_ck = {
	.name = "dpll4_m2x2_mul_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll4_m2x2_mul_ck_data,
};

static struct ti_clk_gate dpll4_m2x2_ck_data = {
	.parent = "dpll4_m2x2_mul_ck",
	.bit_shift = 0x1b,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll4_m2x2_ck = {
	.name = "dpll4_m2x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll4_m2x2_ck_data,
};

static struct ti_clk_fixed_factor omap_96m_alwon_fck_data = {
	.parent = "dpll4_m2x2_ck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk omap_96m_alwon_fck = {
	.name = "omap_96m_alwon_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &omap_96m_alwon_fck_data,
};

static struct ti_clk_fixed_factor cm_96m_fck_data = {
	.parent = "omap_96m_alwon_fck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk cm_96m_fck = {
	.name = "cm_96m_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &cm_96m_fck_data,
};

static const char *omap_96m_fck_parents[] = {
	"cm_96m_fck",
	"sys_ck",
};

static struct ti_clk_mux omap_96m_fck_data = {
	.bit_shift = 6,
	.num_parents = ARRAY_SIZE(omap_96m_fck_parents),
	.reg = 0xd40,
	.module = TI_CLKM_CM,
	.parents = omap_96m_fck_parents,
};

static struct ti_clk omap_96m_fck = {
	.name = "omap_96m_fck",
	.type = TI_CLK_MUX,
	.data = &omap_96m_fck_data,
};

static struct ti_clk_fixed_factor core_96m_fck_data = {
	.parent = "omap_96m_fck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk core_96m_fck = {
	.name = "core_96m_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &core_96m_fck_data,
};

static struct ti_clk_gate mspro_fck_data = {
	.parent = "core_96m_fck",
	.bit_shift = 23,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk mspro_fck = {
	.name = "mspro_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mspro_fck_data,
};

static struct ti_clk_gate dss_ick_3430es2_data = {
	.parent = "l4_ick",
	.bit_shift = 0,
	.reg = 0xe10,
	.module = TI_CLKM_CM,
	.flags = CLKF_DSS | CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk dss_ick_3430es2 = {
	.name = "dss_ick",
	.clkdm_name = "dss_clkdm",
	.type = TI_CLK_GATE,
	.data = &dss_ick_3430es2_data,
};

static struct ti_clk_gate uart4_ick_am35xx_data = {
	.parent = "core_l4_ick",
	.bit_shift = 23,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk uart4_ick_am35xx = {
	.name = "uart4_ick_am35xx",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &uart4_ick_am35xx_data,
};

static struct ti_clk_fixed_factor security_l4_ick2_data = {
	.parent = "l4_ick",
	.div = 1,
	.mult = 1,
};

static struct ti_clk security_l4_ick2 = {
	.name = "security_l4_ick2",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &security_l4_ick2_data,
};

static struct ti_clk_gate aes1_ick_data = {
	.parent = "security_l4_ick2",
	.bit_shift = 3,
	.reg = 0xa14,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk aes1_ick = {
	.name = "aes1_ick",
	.type = TI_CLK_GATE,
	.data = &aes1_ick_data,
};

static const char *dpll5_ck_parents[] = {
	"sys_ck",
	"sys_ck",
};

static struct ti_clk_dpll dpll5_ck_data = {
	.num_parents = ARRAY_SIZE(dpll5_ck_parents),
	.control_reg = 0xd04,
	.idlest_reg = 0xd24,
	.mult_div1_reg = 0xd4c,
	.autoidle_reg = 0xd34,
	.module = TI_CLKM_CM,
	.parents = dpll5_ck_parents,
	.freqsel_mask = 0xf0,
	.modes = 0x82,
	.div1_mask = 0x7f,
	.idlest_mask = 0x1,
	.auto_recal_bit = 0x3,
	.max_divider = 0x80,
	.min_divider = 0x1,
	.recal_en_bit = 0x19,
	.max_multiplier = 0x7ff,
	.enable_mask = 0x7,
	.mult_mask = 0x7ff00,
	.recal_st_bit = 0x19,
	.autoidle_mask = 0x7,
};

static struct ti_clk dpll5_ck = {
	.name = "dpll5_ck",
	.clkdm_name = "dpll5_clkdm",
	.type = TI_CLK_DPLL,
	.data = &dpll5_ck_data,
};

static struct ti_clk_divider dpll5_m2_ck_data = {
	.parent = "dpll5_ck",
	.max_div = 31,
	.reg = 0xd50,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll5_m2_ck = {
	.name = "dpll5_m2_ck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll5_m2_ck_data,
};

static struct ti_clk_gate usbhost_120m_fck_data = {
	.parent = "dpll5_m2_ck",
	.bit_shift = 1,
	.reg = 0x1400,
	.module = TI_CLKM_CM,
};

static struct ti_clk usbhost_120m_fck = {
	.name = "usbhost_120m_fck",
	.clkdm_name = "usbhost_clkdm",
	.type = TI_CLK_GATE,
	.data = &usbhost_120m_fck_data,
};

static struct ti_clk_fixed_factor cm_96m_d2_fck_data = {
	.parent = "cm_96m_fck",
	.div = 2,
	.mult = 1,
};

static struct ti_clk cm_96m_d2_fck = {
	.name = "cm_96m_d2_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &cm_96m_d2_fck_data,
};

static struct ti_clk_fixed sys_altclk_data = {
	.frequency = 0x0,
};

static struct ti_clk sys_altclk = {
	.name = "sys_altclk",
	.type = TI_CLK_FIXED,
	.data = &sys_altclk_data,
};

static const char *omap_48m_fck_parents[] = {
	"cm_96m_d2_fck",
	"sys_altclk",
};

static struct ti_clk_mux omap_48m_fck_data = {
	.bit_shift = 3,
	.num_parents = ARRAY_SIZE(omap_48m_fck_parents),
	.reg = 0xd40,
	.module = TI_CLKM_CM,
	.parents = omap_48m_fck_parents,
};

static struct ti_clk omap_48m_fck = {
	.name = "omap_48m_fck",
	.type = TI_CLK_MUX,
	.data = &omap_48m_fck_data,
};

static struct ti_clk_fixed_factor core_48m_fck_data = {
	.parent = "omap_48m_fck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk core_48m_fck = {
	.name = "core_48m_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &core_48m_fck_data,
};

static struct ti_clk_fixed mcbsp_clks_data = {
	.frequency = 0x0,
};

static struct ti_clk mcbsp_clks = {
	.name = "mcbsp_clks",
	.type = TI_CLK_FIXED,
	.data = &mcbsp_clks_data,
};

static struct ti_clk_gate mcbsp2_gate_fck_data = {
	.parent = "mcbsp_clks",
	.bit_shift = 0,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk_fixed_factor per_96m_fck_data = {
	.parent = "omap_96m_alwon_fck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk per_96m_fck = {
	.name = "per_96m_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &per_96m_fck_data,
};

static const char *mcbsp2_mux_fck_parents[] = {
	"per_96m_fck",
	"mcbsp_clks",
};

static struct ti_clk_mux mcbsp2_mux_fck_data = {
	.bit_shift = 6,
	.num_parents = ARRAY_SIZE(mcbsp2_mux_fck_parents),
	.reg = 0x274,
	.module = TI_CLKM_SCRM,
	.parents = mcbsp2_mux_fck_parents,
};

static struct ti_clk_composite mcbsp2_fck_data = {
	.mux = &mcbsp2_mux_fck_data,
	.gate = &mcbsp2_gate_fck_data,
};

static struct ti_clk mcbsp2_fck = {
	.name = "mcbsp2_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &mcbsp2_fck_data,
};

static struct ti_clk_fixed_factor dpll3_m2x2_ck_data = {
	.parent = "dpll3_m2_ck",
	.div = 1,
	.mult = 2,
};

static struct ti_clk dpll3_m2x2_ck = {
	.name = "dpll3_m2x2_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll3_m2x2_ck_data,
};

static struct ti_clk_fixed_factor corex2_fck_data = {
	.parent = "dpll3_m2x2_ck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk corex2_fck = {
	.name = "corex2_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &corex2_fck_data,
};

static struct ti_clk_gate ssi_ssr_gate_fck_3430es1_data = {
	.parent = "corex2_fck",
	.bit_shift = 0,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_NO_WAIT,
};

static int ssi_ssr_div_fck_3430es1_divs[] = {
	0,
	1,
	2,
	3,
	4,
	0,
	6,
	0,
	8,
};

static struct ti_clk_divider ssi_ssr_div_fck_3430es1_data = {
	.num_dividers = ARRAY_SIZE(ssi_ssr_div_fck_3430es1_divs),
	.parent = "corex2_fck",
	.bit_shift = 8,
	.dividers = ssi_ssr_div_fck_3430es1_divs,
	.reg = 0xa40,
	.module = TI_CLKM_CM,
};

static struct ti_clk_composite ssi_ssr_fck_3430es1_data = {
	.gate = &ssi_ssr_gate_fck_3430es1_data,
	.divider = &ssi_ssr_div_fck_3430es1_data,
};

static struct ti_clk ssi_ssr_fck_3430es1 = {
	.name = "ssi_ssr_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &ssi_ssr_fck_3430es1_data,
};

static struct ti_clk_fixed_factor ssi_sst_fck_3430es1_data = {
	.parent = "ssi_ssr_fck",
	.div = 2,
	.mult = 1,
};

static struct ti_clk ssi_sst_fck_3430es1 = {
	.name = "ssi_sst_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &ssi_sst_fck_3430es1_data,
};

static struct ti_clk_fixed omap_32k_fck_data = {
	.frequency = 32768,
};

static struct ti_clk omap_32k_fck = {
	.name = "omap_32k_fck",
	.type = TI_CLK_FIXED,
	.data = &omap_32k_fck_data,
};

static struct ti_clk_fixed_factor per_32k_alwon_fck_data = {
	.parent = "omap_32k_fck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk per_32k_alwon_fck = {
	.name = "per_32k_alwon_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &per_32k_alwon_fck_data,
};

static struct ti_clk_gate gpio5_dbck_data = {
	.parent = "per_32k_alwon_fck",
	.bit_shift = 16,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk gpio5_dbck = {
	.name = "gpio5_dbck",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio5_dbck_data,
};

static struct ti_clk_gate gpt1_ick_data = {
	.parent = "wkup_l4_ick",
	.bit_shift = 0,
	.reg = 0xc10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt1_ick = {
	.name = "gpt1_ick",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt1_ick_data,
};

static struct ti_clk_gate mcspi3_fck_data = {
	.parent = "core_48m_fck",
	.bit_shift = 20,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk mcspi3_fck = {
	.name = "mcspi3_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcspi3_fck_data,
};

static struct ti_clk_gate gpt2_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 3,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static const char *gpt2_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt2_mux_fck_data = {
	.num_parents = ARRAY_SIZE(gpt2_mux_fck_parents),
	.reg = 0x1040,
	.module = TI_CLKM_CM,
	.parents = gpt2_mux_fck_parents,
};

static struct ti_clk_composite gpt2_fck_data = {
	.mux = &gpt2_mux_fck_data,
	.gate = &gpt2_gate_fck_data,
};

static struct ti_clk gpt2_fck = {
	.name = "gpt2_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt2_fck_data,
};

static struct ti_clk_gate gpt10_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 11,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt10_ick = {
	.name = "gpt10_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt10_ick_data,
};

static struct ti_clk_gate uart2_fck_data = {
	.parent = "core_48m_fck",
	.bit_shift = 14,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk uart2_fck = {
	.name = "uart2_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &uart2_fck_data,
};

static struct ti_clk_fixed_factor sr_l4_ick_data = {
	.parent = "l4_ick",
	.div = 1,
	.mult = 1,
};

static struct ti_clk sr_l4_ick = {
	.name = "sr_l4_ick",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &sr_l4_ick_data,
};

static struct ti_clk_fixed_factor omap_96m_d8_fck_data = {
	.parent = "omap_96m_fck",
	.div = 8,
	.mult = 1,
};

static struct ti_clk omap_96m_d8_fck = {
	.name = "omap_96m_d8_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &omap_96m_d8_fck_data,
};

static struct ti_clk_divider dpll4_m5_ck_data = {
	.parent = "dpll4_ck",
	.max_div = 63,
	.reg = 0xf40,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll4_m5_ck = {
	.name = "dpll4_m5_ck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll4_m5_ck_data,
};

static struct ti_clk_fixed_factor dpll4_m5x2_mul_ck_data = {
	.parent = "dpll4_m5_ck",
	.div = 1,
	.mult = 2,
	.flags = CLKF_SET_RATE_PARENT,
};

static struct ti_clk dpll4_m5x2_mul_ck = {
	.name = "dpll4_m5x2_mul_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll4_m5x2_mul_ck_data,
};

static struct ti_clk_gate dpll4_m5x2_ck_data = {
	.parent = "dpll4_m5x2_mul_ck",
	.bit_shift = 0x1e,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll4_m5x2_ck = {
	.name = "dpll4_m5x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll4_m5x2_ck_data,
};

static struct ti_clk_gate cam_mclk_data = {
	.parent = "dpll4_m5x2_ck",
	.bit_shift = 0,
	.reg = 0xf00,
	.module = TI_CLKM_CM,
	.flags = CLKF_SET_RATE_PARENT,
};

static struct ti_clk cam_mclk = {
	.name = "cam_mclk",
	.type = TI_CLK_GATE,
	.data = &cam_mclk_data,
};

static struct ti_clk_gate mcbsp3_gate_fck_data = {
	.parent = "mcbsp_clks",
	.bit_shift = 1,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static const char *mcbsp3_mux_fck_parents[] = {
	"per_96m_fck",
	"mcbsp_clks",
};

static struct ti_clk_mux mcbsp3_mux_fck_data = {
	.num_parents = ARRAY_SIZE(mcbsp3_mux_fck_parents),
	.reg = 0x2d8,
	.module = TI_CLKM_SCRM,
	.parents = mcbsp3_mux_fck_parents,
};

static struct ti_clk_composite mcbsp3_fck_data = {
	.mux = &mcbsp3_mux_fck_data,
	.gate = &mcbsp3_gate_fck_data,
};

static struct ti_clk mcbsp3_fck = {
	.name = "mcbsp3_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &mcbsp3_fck_data,
};

static struct ti_clk_gate csi2_96m_fck_data = {
	.parent = "core_96m_fck",
	.bit_shift = 1,
	.reg = 0xf00,
	.module = TI_CLKM_CM,
};

static struct ti_clk csi2_96m_fck = {
	.name = "csi2_96m_fck",
	.clkdm_name = "cam_clkdm",
	.type = TI_CLK_GATE,
	.data = &csi2_96m_fck_data,
};

static struct ti_clk_gate gpt9_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 10,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static const char *gpt9_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt9_mux_fck_data = {
	.bit_shift = 7,
	.num_parents = ARRAY_SIZE(gpt9_mux_fck_parents),
	.reg = 0x1040,
	.module = TI_CLKM_CM,
	.parents = gpt9_mux_fck_parents,
};

static struct ti_clk_composite gpt9_fck_data = {
	.mux = &gpt9_mux_fck_data,
	.gate = &gpt9_gate_fck_data,
};

static struct ti_clk gpt9_fck = {
	.name = "gpt9_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt9_fck_data,
};

static struct ti_clk_divider dpll3_m3_ck_data = {
	.parent = "dpll3_ck",
	.bit_shift = 16,
	.max_div = 31,
	.reg = 0x1140,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll3_m3_ck = {
	.name = "dpll3_m3_ck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll3_m3_ck_data,
};

static struct ti_clk_fixed_factor dpll3_m3x2_mul_ck_data = {
	.parent = "dpll3_m3_ck",
	.div = 1,
	.mult = 2,
};

static struct ti_clk dpll3_m3x2_mul_ck = {
	.name = "dpll3_m3x2_mul_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll3_m3x2_mul_ck_data,
};

static struct ti_clk_gate sr2_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 7,
	.reg = 0xc00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk sr2_fck = {
	.name = "sr2_fck",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &sr2_fck_data,
};

static struct ti_clk_fixed pclk_ck_data = {
	.frequency = 27000000,
};

static struct ti_clk pclk_ck = {
	.name = "pclk_ck",
	.type = TI_CLK_FIXED,
	.data = &pclk_ck_data,
};

static struct ti_clk_gate wdt2_ick_data = {
	.parent = "wkup_l4_ick",
	.bit_shift = 5,
	.reg = 0xc10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk wdt2_ick = {
	.name = "wdt2_ick",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &wdt2_ick_data,
};

static struct ti_clk_fixed_factor core_l3_ick_data = {
	.parent = "l3_ick",
	.div = 1,
	.mult = 1,
};

static struct ti_clk core_l3_ick = {
	.name = "core_l3_ick",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &core_l3_ick_data,
};

static struct ti_clk_gate mcspi4_fck_data = {
	.parent = "core_48m_fck",
	.bit_shift = 21,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk mcspi4_fck = {
	.name = "mcspi4_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcspi4_fck_data,
};

static struct ti_clk_fixed_factor per_48m_fck_data = {
	.parent = "omap_48m_fck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk per_48m_fck = {
	.name = "per_48m_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &per_48m_fck_data,
};

static struct ti_clk_gate uart4_fck_data = {
	.parent = "per_48m_fck",
	.bit_shift = 18,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk uart4_fck = {
	.name = "uart4_fck",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &uart4_fck_data,
};

static struct ti_clk_fixed_factor omap_96m_d10_fck_data = {
	.parent = "omap_96m_fck",
	.div = 10,
	.mult = 1,
};

static struct ti_clk omap_96m_d10_fck = {
	.name = "omap_96m_d10_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &omap_96m_d10_fck_data,
};

static struct ti_clk_gate usim_gate_fck_data = {
	.parent = "omap_96m_fck",
	.bit_shift = 9,
	.reg = 0xc00,
	.module = TI_CLKM_CM,
};

static struct ti_clk_fixed_factor per_l4_ick_data = {
	.parent = "l4_ick",
	.div = 1,
	.mult = 1,
};

static struct ti_clk per_l4_ick = {
	.name = "per_l4_ick",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &per_l4_ick_data,
};

static struct ti_clk_gate gpt5_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 6,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt5_ick = {
	.name = "gpt5_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt5_ick_data,
};

static struct ti_clk_gate mcspi2_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 19,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mcspi2_ick = {
	.name = "mcspi2_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcspi2_ick_data,
};

static struct ti_clk_fixed_factor ssi_l4_ick_data = {
	.parent = "l4_ick",
	.div = 1,
	.mult = 1,
};

static struct ti_clk ssi_l4_ick = {
	.name = "ssi_l4_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &ssi_l4_ick_data,
};

static struct ti_clk_gate ssi_ick_3430es1_data = {
	.parent = "ssi_l4_ick",
	.bit_shift = 0,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_NO_WAIT | CLKF_INTERFACE,
};

static struct ti_clk ssi_ick_3430es1 = {
	.name = "ssi_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &ssi_ick_3430es1_data,
};

static struct ti_clk_gate i2c2_fck_data = {
	.parent = "core_96m_fck",
	.bit_shift = 16,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk i2c2_fck = {
	.name = "i2c2_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &i2c2_fck_data,
};

static struct ti_clk_divider dpll1_fck_data = {
	.parent = "core_ck",
	.bit_shift = 19,
	.max_div = 7,
	.reg = 0x940,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll1_fck = {
	.name = "dpll1_fck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll1_fck_data,
};

static const char *dpll1_ck_parents[] = {
	"sys_ck",
	"dpll1_fck",
};

static struct ti_clk_dpll dpll1_ck_data = {
	.num_parents = ARRAY_SIZE(dpll1_ck_parents),
	.control_reg = 0x904,
	.idlest_reg = 0x924,
	.mult_div1_reg = 0x940,
	.autoidle_reg = 0x934,
	.module = TI_CLKM_CM,
	.parents = dpll1_ck_parents,
	.freqsel_mask = 0xf0,
	.modes = 0xa0,
	.div1_mask = 0x7f,
	.idlest_mask = 0x1,
	.auto_recal_bit = 0x3,
	.max_divider = 0x80,
	.min_divider = 0x1,
	.recal_en_bit = 0x7,
	.max_multiplier = 0x7ff,
	.enable_mask = 0x7,
	.mult_mask = 0x7ff00,
	.recal_st_bit = 0x7,
	.autoidle_mask = 0x7,
};

static struct ti_clk dpll1_ck = {
	.name = "dpll1_ck",
	.clkdm_name = "dpll1_clkdm",
	.type = TI_CLK_DPLL,
	.data = &dpll1_ck_data,
};

static struct ti_clk_fixed secure_32k_fck_data = {
	.frequency = 32768,
};

static struct ti_clk secure_32k_fck = {
	.name = "secure_32k_fck",
	.type = TI_CLK_FIXED,
	.data = &secure_32k_fck_data,
};

static struct ti_clk_gate gpio5_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 16,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpio5_ick = {
	.name = "gpio5_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio5_ick_data,
};

static struct ti_clk_divider dpll4_m4_ck_data = {
	.parent = "dpll4_ck",
	.max_div = 32,
	.reg = 0xe40,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll4_m4_ck = {
	.name = "dpll4_m4_ck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll4_m4_ck_data,
};

static struct ti_clk_fixed_factor dpll4_m4x2_mul_ck_data = {
	.parent = "dpll4_m4_ck",
	.div = 1,
	.mult = 2,
	.flags = CLKF_SET_RATE_PARENT,
};

static struct ti_clk dpll4_m4x2_mul_ck = {
	.name = "dpll4_m4x2_mul_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll4_m4x2_mul_ck_data,
};

static struct ti_clk_gate dpll4_m4x2_ck_data = {
	.parent = "dpll4_m4x2_mul_ck",
	.bit_shift = 0x1d,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_SET_RATE_PARENT | CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll4_m4x2_ck = {
	.name = "dpll4_m4x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll4_m4x2_ck_data,
};

static struct ti_clk_gate dss1_alwon_fck_3430es2_data = {
	.parent = "dpll4_m4x2_ck",
	.bit_shift = 0,
	.reg = 0xe00,
	.module = TI_CLKM_CM,
	.flags = CLKF_DSS | CLKF_SET_RATE_PARENT,
};

static struct ti_clk dss1_alwon_fck_3430es2 = {
	.name = "dss1_alwon_fck",
	.clkdm_name = "dss_clkdm",
	.type = TI_CLK_GATE,
	.data = &dss1_alwon_fck_3430es2_data,
};

static struct ti_clk_gate uart3_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 11,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk uart3_ick = {
	.name = "uart3_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &uart3_ick_data,
};

static struct ti_clk_divider dpll4_m3_ck_data = {
	.parent = "dpll4_ck",
	.bit_shift = 8,
	.max_div = 32,
	.reg = 0xe40,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll4_m3_ck = {
	.name = "dpll4_m3_ck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll4_m3_ck_data,
};

static struct ti_clk_gate mcbsp3_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 1,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mcbsp3_ick = {
	.name = "mcbsp3_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcbsp3_ick_data,
};

static struct ti_clk_gate gpio3_dbck_data = {
	.parent = "per_32k_alwon_fck",
	.bit_shift = 14,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk gpio3_dbck = {
	.name = "gpio3_dbck",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio3_dbck_data,
};

static struct ti_clk_gate fac_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 8,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk fac_ick = {
	.name = "fac_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &fac_ick_data,
};

static struct ti_clk_gate clkout2_src_gate_ck_data = {
	.parent = "core_ck",
	.bit_shift = 7,
	.reg = 0xd70,
	.module = TI_CLKM_CM,
	.flags = CLKF_NO_WAIT,
};

static struct ti_clk_fixed_factor dpll4_m3x2_mul_ck_data = {
	.parent = "dpll4_m3_ck",
	.div = 1,
	.mult = 2,
};

static struct ti_clk dpll4_m3x2_mul_ck = {
	.name = "dpll4_m3x2_mul_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll4_m3x2_mul_ck_data,
};

static struct ti_clk_gate dpll4_m3x2_ck_data = {
	.parent = "dpll4_m3x2_mul_ck",
	.bit_shift = 0x1c,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll4_m3x2_ck = {
	.name = "dpll4_m3x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll4_m3x2_ck_data,
};

static const char *omap_54m_fck_parents[] = {
	"dpll4_m3x2_ck",
	"sys_altclk",
};

static struct ti_clk_mux omap_54m_fck_data = {
	.bit_shift = 5,
	.num_parents = ARRAY_SIZE(omap_54m_fck_parents),
	.reg = 0xd40,
	.module = TI_CLKM_CM,
	.parents = omap_54m_fck_parents,
};

static struct ti_clk omap_54m_fck = {
	.name = "omap_54m_fck",
	.type = TI_CLK_MUX,
	.data = &omap_54m_fck_data,
};

static const char *clkout2_src_mux_ck_parents[] = {
	"core_ck",
	"sys_ck",
	"cm_96m_fck",
	"omap_54m_fck",
};

static struct ti_clk_mux clkout2_src_mux_ck_data = {
	.num_parents = ARRAY_SIZE(clkout2_src_mux_ck_parents),
	.reg = 0xd70,
	.module = TI_CLKM_CM,
	.parents = clkout2_src_mux_ck_parents,
};

static struct ti_clk_composite clkout2_src_ck_data = {
	.mux = &clkout2_src_mux_ck_data,
	.gate = &clkout2_src_gate_ck_data,
};

static struct ti_clk clkout2_src_ck = {
	.name = "clkout2_src_ck",
	.type = TI_CLK_COMPOSITE,
	.data = &clkout2_src_ck_data,
};

static struct ti_clk_gate i2c1_fck_data = {
	.parent = "core_96m_fck",
	.bit_shift = 15,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk i2c1_fck = {
	.name = "i2c1_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &i2c1_fck_data,
};

static struct ti_clk_gate wdt3_fck_data = {
	.parent = "per_32k_alwon_fck",
	.bit_shift = 12,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk wdt3_fck = {
	.name = "wdt3_fck",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &wdt3_fck_data,
};

static struct ti_clk_gate gpt7_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 8,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static const char *gpt7_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt7_mux_fck_data = {
	.bit_shift = 5,
	.num_parents = ARRAY_SIZE(gpt7_mux_fck_parents),
	.reg = 0x1040,
	.module = TI_CLKM_CM,
	.parents = gpt7_mux_fck_parents,
};

static struct ti_clk_composite gpt7_fck_data = {
	.mux = &gpt7_mux_fck_data,
	.gate = &gpt7_gate_fck_data,
};

static struct ti_clk gpt7_fck = {
	.name = "gpt7_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt7_fck_data,
};

static struct ti_clk_gate usb_l4_gate_ick_data = {
	.parent = "l4_ick",
	.bit_shift = 5,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_INTERFACE,
};

static struct ti_clk_divider usb_l4_div_ick_data = {
	.parent = "l4_ick",
	.bit_shift = 4,
	.max_div = 1,
	.reg = 0xa40,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk_composite usb_l4_ick_data = {
	.gate = &usb_l4_gate_ick_data,
	.divider = &usb_l4_div_ick_data,
};

static struct ti_clk usb_l4_ick = {
	.name = "usb_l4_ick",
	.type = TI_CLK_COMPOSITE,
	.data = &usb_l4_ick_data,
};

static struct ti_clk_gate uart4_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 18,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk uart4_ick = {
	.name = "uart4_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &uart4_ick_data,
};

static struct ti_clk_fixed dummy_ck_data = {
	.frequency = 0,
};

static struct ti_clk dummy_ck = {
	.name = "dummy_ck",
	.type = TI_CLK_FIXED,
	.data = &dummy_ck_data,
};

static const char *gpt3_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt3_mux_fck_data = {
	.bit_shift = 1,
	.num_parents = ARRAY_SIZE(gpt3_mux_fck_parents),
	.reg = 0x1040,
	.module = TI_CLKM_CM,
	.parents = gpt3_mux_fck_parents,
};

static struct ti_clk_gate gpt9_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 10,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt9_ick = {
	.name = "gpt9_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt9_ick_data,
};

static struct ti_clk_gate gpt10_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 11,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
};

static struct ti_clk_gate dss_ick_3430es1_data = {
	.parent = "l4_ick",
	.bit_shift = 0,
	.reg = 0xe10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_NO_WAIT | CLKF_INTERFACE,
};

static struct ti_clk dss_ick_3430es1 = {
	.name = "dss_ick",
	.clkdm_name = "dss_clkdm",
	.type = TI_CLK_GATE,
	.data = &dss_ick_3430es1_data,
};

static struct ti_clk_gate gpt11_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 12,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt11_ick = {
	.name = "gpt11_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt11_ick_data,
};

static struct ti_clk_divider dpll2_fck_data = {
	.parent = "core_ck",
	.bit_shift = 19,
	.max_div = 7,
	.reg = 0x40,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll2_fck = {
	.name = "dpll2_fck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll2_fck_data,
};

static struct ti_clk_gate uart1_fck_data = {
	.parent = "core_48m_fck",
	.bit_shift = 13,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk uart1_fck = {
	.name = "uart1_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &uart1_fck_data,
};

static struct ti_clk_gate hsotgusb_ick_3430es1_data = {
	.parent = "core_l3_ick",
	.bit_shift = 4,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_NO_WAIT | CLKF_INTERFACE,
};

static struct ti_clk hsotgusb_ick_3430es1 = {
	.name = "hsotgusb_ick_3430es1",
	.clkdm_name = "core_l3_clkdm",
	.type = TI_CLK_GATE,
	.data = &hsotgusb_ick_3430es1_data,
};

static struct ti_clk_gate gpio2_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 13,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpio2_ick = {
	.name = "gpio2_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio2_ick_data,
};

static struct ti_clk_gate mmchs1_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 24,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mmchs1_ick = {
	.name = "mmchs1_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mmchs1_ick_data,
};

static struct ti_clk_gate modem_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 31,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk modem_fck = {
	.name = "modem_fck",
	.clkdm_name = "d2d_clkdm",
	.type = TI_CLK_GATE,
	.data = &modem_fck_data,
};

static struct ti_clk_gate mcbsp4_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 2,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mcbsp4_ick = {
	.name = "mcbsp4_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcbsp4_ick_data,
};

static struct ti_clk_gate gpio1_ick_data = {
	.parent = "wkup_l4_ick",
	.bit_shift = 3,
	.reg = 0xc10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpio1_ick = {
	.name = "gpio1_ick",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio1_ick_data,
};

static const char *gpt6_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt6_mux_fck_data = {
	.bit_shift = 4,
	.num_parents = ARRAY_SIZE(gpt6_mux_fck_parents),
	.reg = 0x1040,
	.module = TI_CLKM_CM,
	.parents = gpt6_mux_fck_parents,
};

static struct ti_clk_fixed_factor dpll1_x2_ck_data = {
	.parent = "dpll1_ck",
	.div = 1,
	.mult = 2,
};

static struct ti_clk dpll1_x2_ck = {
	.name = "dpll1_x2_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll1_x2_ck_data,
};

static struct ti_clk_divider dpll1_x2m2_ck_data = {
	.parent = "dpll1_x2_ck",
	.max_div = 31,
	.reg = 0x944,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll1_x2m2_ck = {
	.name = "dpll1_x2m2_ck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll1_x2m2_ck_data,
};

static struct ti_clk_fixed_factor mpu_ck_data = {
	.parent = "dpll1_x2m2_ck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk mpu_ck = {
	.name = "mpu_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &mpu_ck_data,
};

static struct ti_clk_divider arm_fck_data = {
	.parent = "mpu_ck",
	.max_div = 2,
	.reg = 0x924,
	.module = TI_CLKM_CM,
};

static struct ti_clk arm_fck = {
	.name = "arm_fck",
	.type = TI_CLK_DIVIDER,
	.data = &arm_fck_data,
};

static struct ti_clk_fixed_factor core_d3_ck_data = {
	.parent = "core_ck",
	.div = 3,
	.mult = 1,
};

static struct ti_clk core_d3_ck = {
	.name = "core_d3_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &core_d3_ck_data,
};

static struct ti_clk_gate gpt11_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 12,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
};

static const char *gpt11_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt11_mux_fck_data = {
	.bit_shift = 7,
	.num_parents = ARRAY_SIZE(gpt11_mux_fck_parents),
	.reg = 0xa40,
	.module = TI_CLKM_CM,
	.parents = gpt11_mux_fck_parents,
};

static struct ti_clk_composite gpt11_fck_data = {
	.mux = &gpt11_mux_fck_data,
	.gate = &gpt11_gate_fck_data,
};

static struct ti_clk gpt11_fck = {
	.name = "gpt11_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt11_fck_data,
};

static struct ti_clk_fixed_factor core_d6_ck_data = {
	.parent = "core_ck",
	.div = 6,
	.mult = 1,
};

static struct ti_clk core_d6_ck = {
	.name = "core_d6_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &core_d6_ck_data,
};

static struct ti_clk_gate uart4_fck_am35xx_data = {
	.parent = "core_48m_fck",
	.bit_shift = 23,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk uart4_fck_am35xx = {
	.name = "uart4_fck_am35xx",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &uart4_fck_am35xx_data,
};

static struct ti_clk_gate dpll3_m3x2_ck_data = {
	.parent = "dpll3_m3x2_mul_ck",
	.bit_shift = 0xc,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll3_m3x2_ck = {
	.name = "dpll3_m3x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll3_m3x2_ck_data,
};

static struct ti_clk_fixed_factor emu_core_alwon_ck_data = {
	.parent = "dpll3_m3x2_ck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk emu_core_alwon_ck = {
	.name = "emu_core_alwon_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &emu_core_alwon_ck_data,
};

static struct ti_clk_divider dpll4_m6_ck_data = {
	.parent = "dpll4_ck",
	.bit_shift = 24,
	.max_div = 63,
	.reg = 0x1140,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll4_m6_ck = {
	.name = "dpll4_m6_ck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll4_m6_ck_data,
};

static struct ti_clk_fixed_factor dpll4_m6x2_mul_ck_data = {
	.parent = "dpll4_m6_ck",
	.div = 1,
	.mult = 2,
};

static struct ti_clk dpll4_m6x2_mul_ck = {
	.name = "dpll4_m6x2_mul_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll4_m6x2_mul_ck_data,
};

static struct ti_clk_gate dpll4_m6x2_ck_data = {
	.parent = "dpll4_m6x2_mul_ck",
	.bit_shift = 0x1f,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll4_m6x2_ck = {
	.name = "dpll4_m6x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll4_m6x2_ck_data,
};

static struct ti_clk_fixed_factor emu_per_alwon_ck_data = {
	.parent = "dpll4_m6x2_ck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk emu_per_alwon_ck = {
	.name = "emu_per_alwon_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &emu_per_alwon_ck_data,
};

static struct ti_clk_fixed_factor emu_mpu_alwon_ck_data = {
	.parent = "mpu_ck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk emu_mpu_alwon_ck = {
	.name = "emu_mpu_alwon_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &emu_mpu_alwon_ck_data,
};

static const char *emu_src_mux_ck_parents[] = {
	"sys_ck",
	"emu_core_alwon_ck",
	"emu_per_alwon_ck",
	"emu_mpu_alwon_ck",
};

static struct ti_clk_mux emu_src_mux_ck_data = {
	.num_parents = ARRAY_SIZE(emu_src_mux_ck_parents),
	.reg = 0x1140,
	.module = TI_CLKM_CM,
	.parents = emu_src_mux_ck_parents,
};

static struct ti_clk emu_src_mux_ck = {
	.name = "emu_src_mux_ck",
	.type = TI_CLK_MUX,
	.data = &emu_src_mux_ck_data,
};

static struct ti_clk_gate emu_src_ck_data = {
	.parent = "emu_src_mux_ck",
	.flags = CLKF_CLKDM,
};

static struct ti_clk emu_src_ck = {
	.name = "emu_src_ck",
	.clkdm_name = "emu_clkdm",
	.type = TI_CLK_GATE,
	.data = &emu_src_ck_data,
};

static struct ti_clk_divider atclk_fck_data = {
	.parent = "emu_src_ck",
	.bit_shift = 4,
	.max_div = 3,
	.reg = 0x1140,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk atclk_fck = {
	.name = "atclk_fck",
	.type = TI_CLK_DIVIDER,
	.data = &atclk_fck_data,
};

static struct ti_clk_gate ipss_ick_data = {
	.parent = "core_l3_ick",
	.bit_shift = 4,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_AM35XX | CLKF_INTERFACE,
};

static struct ti_clk ipss_ick = {
	.name = "ipss_ick",
	.clkdm_name = "core_l3_clkdm",
	.type = TI_CLK_GATE,
	.data = &ipss_ick_data,
};

static struct ti_clk_gate emac_ick_data = {
	.parent = "ipss_ick",
	.bit_shift = 1,
	.reg = 0x59c,
	.module = TI_CLKM_SCRM,
	.flags = CLKF_AM35XX,
};

static struct ti_clk emac_ick = {
	.name = "emac_ick",
	.clkdm_name = "core_l3_clkdm",
	.type = TI_CLK_GATE,
	.data = &emac_ick_data,
};

static struct ti_clk_gate vpfe_ick_data = {
	.parent = "ipss_ick",
	.bit_shift = 2,
	.reg = 0x59c,
	.module = TI_CLKM_SCRM,
	.flags = CLKF_AM35XX,
};

static struct ti_clk vpfe_ick = {
	.name = "vpfe_ick",
	.clkdm_name = "core_l3_clkdm",
	.type = TI_CLK_GATE,
	.data = &vpfe_ick_data,
};

static const char *dpll2_ck_parents[] = {
	"sys_ck",
	"dpll2_fck",
};

static struct ti_clk_dpll dpll2_ck_data = {
	.num_parents = ARRAY_SIZE(dpll2_ck_parents),
	.control_reg = 0x4,
	.idlest_reg = 0x24,
	.mult_div1_reg = 0x40,
	.autoidle_reg = 0x34,
	.module = TI_CLKM_CM,
	.parents = dpll2_ck_parents,
	.freqsel_mask = 0xf0,
	.modes = 0xa2,
	.div1_mask = 0x7f,
	.idlest_mask = 0x1,
	.auto_recal_bit = 0x3,
	.max_divider = 0x80,
	.min_divider = 0x1,
	.recal_en_bit = 0x8,
	.max_multiplier = 0x7ff,
	.enable_mask = 0x7,
	.mult_mask = 0x7ff00,
	.recal_st_bit = 0x8,
	.autoidle_mask = 0x7,
};

static struct ti_clk dpll2_ck = {
	.name = "dpll2_ck",
	.clkdm_name = "dpll2_clkdm",
	.type = TI_CLK_DPLL,
	.data = &dpll2_ck_data,
};

static struct ti_clk_divider dpll2_m2_ck_data = {
	.parent = "dpll2_ck",
	.max_div = 31,
	.reg = 0x44,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk dpll2_m2_ck = {
	.name = "dpll2_m2_ck",
	.type = TI_CLK_DIVIDER,
	.data = &dpll2_m2_ck_data,
};

static const char *mcbsp4_mux_fck_parents[] = {
	"per_96m_fck",
	"mcbsp_clks",
};

static struct ti_clk_mux mcbsp4_mux_fck_data = {
	.bit_shift = 2,
	.num_parents = ARRAY_SIZE(mcbsp4_mux_fck_parents),
	.reg = 0x2d8,
	.module = TI_CLKM_SCRM,
	.parents = mcbsp4_mux_fck_parents,
};

static const char *mcbsp1_mux_fck_parents[] = {
	"core_96m_fck",
	"mcbsp_clks",
};

static struct ti_clk_mux mcbsp1_mux_fck_data = {
	.bit_shift = 2,
	.num_parents = ARRAY_SIZE(mcbsp1_mux_fck_parents),
	.reg = 0x274,
	.module = TI_CLKM_SCRM,
	.parents = mcbsp1_mux_fck_parents,
};

static struct ti_clk_gate gpt8_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 9,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk_gate gpt8_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 9,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt8_ick = {
	.name = "gpt8_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt8_ick_data,
};

static const char *gpt10_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt10_mux_fck_data = {
	.bit_shift = 6,
	.num_parents = ARRAY_SIZE(gpt10_mux_fck_parents),
	.reg = 0xa40,
	.module = TI_CLKM_CM,
	.parents = gpt10_mux_fck_parents,
};

static struct ti_clk_gate mmchs3_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 30,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mmchs3_ick = {
	.name = "mmchs3_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mmchs3_ick_data,
};

static struct ti_clk_gate gpio3_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 14,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpio3_ick = {
	.name = "gpio3_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio3_ick_data,
};

static const char *traceclk_src_fck_parents[] = {
	"sys_ck",
	"emu_core_alwon_ck",
	"emu_per_alwon_ck",
	"emu_mpu_alwon_ck",
};

static struct ti_clk_mux traceclk_src_fck_data = {
	.bit_shift = 2,
	.num_parents = ARRAY_SIZE(traceclk_src_fck_parents),
	.reg = 0x1140,
	.module = TI_CLKM_CM,
	.parents = traceclk_src_fck_parents,
};

static struct ti_clk traceclk_src_fck = {
	.name = "traceclk_src_fck",
	.type = TI_CLK_MUX,
	.data = &traceclk_src_fck_data,
};

static struct ti_clk_divider traceclk_fck_data = {
	.parent = "traceclk_src_fck",
	.bit_shift = 11,
	.max_div = 7,
	.reg = 0x1140,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk traceclk_fck = {
	.name = "traceclk_fck",
	.type = TI_CLK_DIVIDER,
	.data = &traceclk_fck_data,
};

static struct ti_clk_gate mcbsp5_gate_fck_data = {
	.parent = "mcbsp_clks",
	.bit_shift = 10,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
};

static struct ti_clk_gate sad2d_ick_data = {
	.parent = "l3_ick",
	.bit_shift = 3,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk sad2d_ick = {
	.name = "sad2d_ick",
	.clkdm_name = "d2d_clkdm",
	.type = TI_CLK_GATE,
	.data = &sad2d_ick_data,
};

static const char *gpt1_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt1_mux_fck_data = {
	.num_parents = ARRAY_SIZE(gpt1_mux_fck_parents),
	.reg = 0xc40,
	.module = TI_CLKM_CM,
	.parents = gpt1_mux_fck_parents,
};

static struct ti_clk_gate hecc_ck_data = {
	.parent = "sys_ck",
	.bit_shift = 3,
	.reg = 0x59c,
	.module = TI_CLKM_SCRM,
	.flags = CLKF_AM35XX,
};

static struct ti_clk hecc_ck = {
	.name = "hecc_ck",
	.clkdm_name = "core_l3_clkdm",
	.type = TI_CLK_GATE,
	.data = &hecc_ck_data,
};

static struct ti_clk_gate gpt1_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 0,
	.reg = 0xc00,
	.module = TI_CLKM_CM,
};

static struct ti_clk_composite gpt1_fck_data = {
	.mux = &gpt1_mux_fck_data,
	.gate = &gpt1_gate_fck_data,
};

static struct ti_clk gpt1_fck = {
	.name = "gpt1_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt1_fck_data,
};

static struct ti_clk_gate dpll4_m2x2_ck_omap36xx_data = {
	.parent = "dpll4_m2x2_mul_ck",
	.bit_shift = 0x1b,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_HSDIV | CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll4_m2x2_ck_omap36xx = {
	.name = "dpll4_m2x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll4_m2x2_ck_omap36xx_data,
	.patch = &dpll4_m2x2_ck,
};

static struct ti_clk_divider gfx_l3_fck_data = {
	.parent = "l3_ick",
	.max_div = 7,
	.reg = 0xb40,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk gfx_l3_fck = {
	.name = "gfx_l3_fck",
	.type = TI_CLK_DIVIDER,
	.data = &gfx_l3_fck_data,
};

static struct ti_clk_gate gfx_cg1_ck_data = {
	.parent = "gfx_l3_fck",
	.bit_shift = 1,
	.reg = 0xb00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk gfx_cg1_ck = {
	.name = "gfx_cg1_ck",
	.clkdm_name = "gfx_3430es1_clkdm",
	.type = TI_CLK_GATE,
	.data = &gfx_cg1_ck_data,
};

static struct ti_clk_gate mailboxes_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 7,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mailboxes_ick = {
	.name = "mailboxes_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mailboxes_ick_data,
};

static struct ti_clk_gate sha11_ick_data = {
	.parent = "security_l4_ick2",
	.bit_shift = 1,
	.reg = 0xa14,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk sha11_ick = {
	.name = "sha11_ick",
	.type = TI_CLK_GATE,
	.data = &sha11_ick_data,
};

static struct ti_clk_gate hsotgusb_ick_am35xx_data = {
	.parent = "ipss_ick",
	.bit_shift = 0,
	.reg = 0x59c,
	.module = TI_CLKM_SCRM,
	.flags = CLKF_AM35XX,
};

static struct ti_clk hsotgusb_ick_am35xx = {
	.name = "hsotgusb_ick_am35xx",
	.clkdm_name = "core_l3_clkdm",
	.type = TI_CLK_GATE,
	.data = &hsotgusb_ick_am35xx_data,
};

static struct ti_clk_gate mmchs3_fck_data = {
	.parent = "core_96m_fck",
	.bit_shift = 30,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk mmchs3_fck = {
	.name = "mmchs3_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mmchs3_fck_data,
};

static struct ti_clk_divider pclk_fck_data = {
	.parent = "emu_src_ck",
	.bit_shift = 8,
	.max_div = 7,
	.reg = 0x1140,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk pclk_fck = {
	.name = "pclk_fck",
	.type = TI_CLK_DIVIDER,
	.data = &pclk_fck_data,
};

static const char *dpll4_ck_omap36xx_parents[] = {
	"sys_ck",
	"sys_ck",
};

static struct ti_clk_dpll dpll4_ck_omap36xx_data = {
	.num_parents = ARRAY_SIZE(dpll4_ck_omap36xx_parents),
	.control_reg = 0xd00,
	.idlest_reg = 0xd20,
	.mult_div1_reg = 0xd44,
	.autoidle_reg = 0xd30,
	.module = TI_CLKM_CM,
	.parents = dpll4_ck_omap36xx_parents,
	.modes = 0x82,
	.div1_mask = 0x7f,
	.idlest_mask = 0x2,
	.auto_recal_bit = 0x13,
	.max_divider = 0x80,
	.min_divider = 0x1,
	.recal_en_bit = 0x6,
	.max_multiplier = 0xfff,
	.enable_mask = 0x70000,
	.mult_mask = 0xfff00,
	.recal_st_bit = 0x6,
	.autoidle_mask = 0x38,
	.sddiv_mask = 0xff000000,
	.dco_mask = 0xe00000,
	.flags = CLKF_PER | CLKF_J_TYPE,
};

static struct ti_clk dpll4_ck_omap36xx = {
	.name = "dpll4_ck",
	.type = TI_CLK_DPLL,
	.data = &dpll4_ck_omap36xx_data,
	.patch = &dpll4_ck,
};

static struct ti_clk_gate uart3_fck_data = {
	.parent = "per_48m_fck",
	.bit_shift = 11,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk uart3_fck = {
	.name = "uart3_fck",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &uart3_fck_data,
};

static struct ti_clk_fixed_factor wkup_32k_fck_data = {
	.parent = "omap_32k_fck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk wkup_32k_fck = {
	.name = "wkup_32k_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &wkup_32k_fck_data,
};

static struct ti_clk_gate sys_clkout1_data = {
	.parent = "osc_sys_ck",
	.bit_shift = 7,
	.reg = 0xd70,
	.module = TI_CLKM_PRM,
};

static struct ti_clk sys_clkout1 = {
	.name = "sys_clkout1",
	.type = TI_CLK_GATE,
	.data = &sys_clkout1_data,
};

static struct ti_clk_fixed_factor gpmc_fck_data = {
	.parent = "core_l3_ick",
	.div = 1,
	.mult = 1,
};

static struct ti_clk gpmc_fck = {
	.name = "gpmc_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &gpmc_fck_data,
};

static struct ti_clk_fixed_factor dpll5_m2_d20_ck_data = {
	.parent = "dpll5_m2_ck",
	.div = 20,
	.mult = 1,
};

static struct ti_clk dpll5_m2_d20_ck = {
	.name = "dpll5_m2_d20_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll5_m2_d20_ck_data,
};

static struct ti_clk_gate dpll4_m5x2_ck_omap36xx_data = {
	.parent = "dpll4_m5x2_mul_ck",
	.bit_shift = 0x1e,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_HSDIV | CLKF_SET_RATE_PARENT | CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll4_m5x2_ck_omap36xx = {
	.name = "dpll4_m5x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll4_m5x2_ck_omap36xx_data,
	.patch = &dpll4_m5x2_ck,
};

static struct ti_clk_gate ssi_ssr_gate_fck_3430es2_data = {
	.parent = "corex2_fck",
	.bit_shift = 0,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_NO_WAIT,
};

static struct ti_clk_gate uart1_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 13,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk uart1_ick = {
	.name = "uart1_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &uart1_ick_data,
};

static struct ti_clk_gate iva2_ck_data = {
	.parent = "dpll2_m2_ck",
	.bit_shift = 0,
	.reg = 0x0,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk iva2_ck = {
	.name = "iva2_ck",
	.clkdm_name = "iva2_clkdm",
	.type = TI_CLK_GATE,
	.data = &iva2_ck_data,
};

static struct ti_clk_gate pka_ick_data = {
	.parent = "security_l3_ick",
	.bit_shift = 4,
	.reg = 0xa14,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk pka_ick = {
	.name = "pka_ick",
	.type = TI_CLK_GATE,
	.data = &pka_ick_data,
};

static struct ti_clk_gate gpt12_ick_data = {
	.parent = "wkup_l4_ick",
	.bit_shift = 1,
	.reg = 0xc10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt12_ick = {
	.name = "gpt12_ick",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt12_ick_data,
};

static const char *mcbsp5_mux_fck_parents[] = {
	"core_96m_fck",
	"mcbsp_clks",
};

static struct ti_clk_mux mcbsp5_mux_fck_data = {
	.bit_shift = 4,
	.num_parents = ARRAY_SIZE(mcbsp5_mux_fck_parents),
	.reg = 0x2d8,
	.module = TI_CLKM_SCRM,
	.parents = mcbsp5_mux_fck_parents,
};

static struct ti_clk_composite mcbsp5_fck_data = {
	.mux = &mcbsp5_mux_fck_data,
	.gate = &mcbsp5_gate_fck_data,
};

static struct ti_clk mcbsp5_fck = {
	.name = "mcbsp5_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &mcbsp5_fck_data,
};

static struct ti_clk_gate usbhost_48m_fck_data = {
	.parent = "omap_48m_fck",
	.bit_shift = 0,
	.reg = 0x1400,
	.module = TI_CLKM_CM,
	.flags = CLKF_DSS,
};

static struct ti_clk usbhost_48m_fck = {
	.name = "usbhost_48m_fck",
	.clkdm_name = "usbhost_clkdm",
	.type = TI_CLK_GATE,
	.data = &usbhost_48m_fck_data,
};

static struct ti_clk_gate des1_ick_data = {
	.parent = "security_l4_ick2",
	.bit_shift = 0,
	.reg = 0xa14,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk des1_ick = {
	.name = "des1_ick",
	.type = TI_CLK_GATE,
	.data = &des1_ick_data,
};

static struct ti_clk_gate sgx_gate_fck_data = {
	.parent = "core_ck",
	.bit_shift = 1,
	.reg = 0xb00,
	.module = TI_CLKM_CM,
};

static struct ti_clk_fixed_factor core_d4_ck_data = {
	.parent = "core_ck",
	.div = 4,
	.mult = 1,
};

static struct ti_clk core_d4_ck = {
	.name = "core_d4_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &core_d4_ck_data,
};

static struct ti_clk_fixed_factor omap_192m_alwon_fck_data = {
	.parent = "dpll4_m2x2_ck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk omap_192m_alwon_fck = {
	.name = "omap_192m_alwon_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &omap_192m_alwon_fck_data,
};

static struct ti_clk_fixed_factor core_d2_ck_data = {
	.parent = "core_ck",
	.div = 2,
	.mult = 1,
};

static struct ti_clk core_d2_ck = {
	.name = "core_d2_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &core_d2_ck_data,
};

static struct ti_clk_fixed_factor corex2_d3_fck_data = {
	.parent = "corex2_fck",
	.div = 3,
	.mult = 1,
};

static struct ti_clk corex2_d3_fck = {
	.name = "corex2_d3_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &corex2_d3_fck_data,
};

static struct ti_clk_fixed_factor corex2_d5_fck_data = {
	.parent = "corex2_fck",
	.div = 5,
	.mult = 1,
};

static struct ti_clk corex2_d5_fck = {
	.name = "corex2_d5_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &corex2_d5_fck_data,
};

static const char *sgx_mux_fck_parents[] = {
	"core_d3_ck",
	"core_d4_ck",
	"core_d6_ck",
	"cm_96m_fck",
	"omap_192m_alwon_fck",
	"core_d2_ck",
	"corex2_d3_fck",
	"corex2_d5_fck",
};

static struct ti_clk_mux sgx_mux_fck_data = {
	.num_parents = ARRAY_SIZE(sgx_mux_fck_parents),
	.reg = 0xb40,
	.module = TI_CLKM_CM,
	.parents = sgx_mux_fck_parents,
};

static struct ti_clk_composite sgx_fck_data = {
	.mux = &sgx_mux_fck_data,
	.gate = &sgx_gate_fck_data,
};

static struct ti_clk sgx_fck = {
	.name = "sgx_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &sgx_fck_data,
};

static struct ti_clk_gate mcspi1_fck_data = {
	.parent = "core_48m_fck",
	.bit_shift = 18,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk mcspi1_fck = {
	.name = "mcspi1_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcspi1_fck_data,
};

static struct ti_clk_gate mmchs2_fck_data = {
	.parent = "core_96m_fck",
	.bit_shift = 25,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk mmchs2_fck = {
	.name = "mmchs2_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mmchs2_fck_data,
};

static struct ti_clk_gate mcspi2_fck_data = {
	.parent = "core_48m_fck",
	.bit_shift = 19,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk mcspi2_fck = {
	.name = "mcspi2_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcspi2_fck_data,
};

static struct ti_clk_gate vpfe_fck_data = {
	.parent = "pclk_ck",
	.bit_shift = 10,
	.reg = 0x59c,
	.module = TI_CLKM_SCRM,
};

static struct ti_clk vpfe_fck = {
	.name = "vpfe_fck",
	.type = TI_CLK_GATE,
	.data = &vpfe_fck_data,
};

static struct ti_clk_gate gpt4_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 5,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk_gate mcbsp1_gate_fck_data = {
	.parent = "mcbsp_clks",
	.bit_shift = 9,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
};

static struct ti_clk_gate gpt5_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 6,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static const char *gpt5_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt5_mux_fck_data = {
	.bit_shift = 3,
	.num_parents = ARRAY_SIZE(gpt5_mux_fck_parents),
	.reg = 0x1040,
	.module = TI_CLKM_CM,
	.parents = gpt5_mux_fck_parents,
};

static struct ti_clk_composite gpt5_fck_data = {
	.mux = &gpt5_mux_fck_data,
	.gate = &gpt5_gate_fck_data,
};

static struct ti_clk gpt5_fck = {
	.name = "gpt5_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt5_fck_data,
};

static struct ti_clk_gate ts_fck_data = {
	.parent = "omap_32k_fck",
	.bit_shift = 1,
	.reg = 0xa08,
	.module = TI_CLKM_CM,
};

static struct ti_clk ts_fck = {
	.name = "ts_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &ts_fck_data,
};

static struct ti_clk_fixed_factor wdt1_fck_data = {
	.parent = "secure_32k_fck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk wdt1_fck = {
	.name = "wdt1_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &wdt1_fck_data,
};

static struct ti_clk_gate dpll4_m6x2_ck_omap36xx_data = {
	.parent = "dpll4_m6x2_mul_ck",
	.bit_shift = 0x1f,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_HSDIV | CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll4_m6x2_ck_omap36xx = {
	.name = "dpll4_m6x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll4_m6x2_ck_omap36xx_data,
	.patch = &dpll4_m6x2_ck,
};

static const char *gpt4_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt4_mux_fck_data = {
	.bit_shift = 2,
	.num_parents = ARRAY_SIZE(gpt4_mux_fck_parents),
	.reg = 0x1040,
	.module = TI_CLKM_CM,
	.parents = gpt4_mux_fck_parents,
};

static struct ti_clk_gate usbhost_ick_data = {
	.parent = "l4_ick",
	.bit_shift = 0,
	.reg = 0x1410,
	.module = TI_CLKM_CM,
	.flags = CLKF_DSS | CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk usbhost_ick = {
	.name = "usbhost_ick",
	.clkdm_name = "usbhost_clkdm",
	.type = TI_CLK_GATE,
	.data = &usbhost_ick_data,
};

static struct ti_clk_gate mcbsp2_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 0,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mcbsp2_ick = {
	.name = "mcbsp2_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcbsp2_ick_data,
};

static struct ti_clk_gate omapctrl_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 6,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk omapctrl_ick = {
	.name = "omapctrl_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &omapctrl_ick_data,
};

static struct ti_clk_fixed_factor omap_96m_d4_fck_data = {
	.parent = "omap_96m_fck",
	.div = 4,
	.mult = 1,
};

static struct ti_clk omap_96m_d4_fck = {
	.name = "omap_96m_d4_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &omap_96m_d4_fck_data,
};

static struct ti_clk_gate gpt6_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 7,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt6_ick = {
	.name = "gpt6_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt6_ick_data,
};

static struct ti_clk_gate dpll3_m3x2_ck_omap36xx_data = {
	.parent = "dpll3_m3x2_mul_ck",
	.bit_shift = 0xc,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_HSDIV | CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll3_m3x2_ck_omap36xx = {
	.name = "dpll3_m3x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll3_m3x2_ck_omap36xx_data,
	.patch = &dpll3_m3x2_ck,
};

static struct ti_clk_gate i2c3_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 17,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk i2c3_ick = {
	.name = "i2c3_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &i2c3_ick_data,
};

static struct ti_clk_gate gpio6_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 17,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpio6_ick = {
	.name = "gpio6_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio6_ick_data,
};

static struct ti_clk_gate mspro_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 23,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mspro_ick = {
	.name = "mspro_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mspro_ick_data,
};

static struct ti_clk_composite mcbsp1_fck_data = {
	.mux = &mcbsp1_mux_fck_data,
	.gate = &mcbsp1_gate_fck_data,
};

static struct ti_clk mcbsp1_fck = {
	.name = "mcbsp1_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &mcbsp1_fck_data,
};

static struct ti_clk_gate gpt3_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 4,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk_fixed rmii_ck_data = {
	.frequency = 50000000,
};

static struct ti_clk rmii_ck = {
	.name = "rmii_ck",
	.type = TI_CLK_FIXED,
	.data = &rmii_ck_data,
};

static struct ti_clk_gate gpt6_gate_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 7,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk_composite gpt6_fck_data = {
	.mux = &gpt6_mux_fck_data,
	.gate = &gpt6_gate_fck_data,
};

static struct ti_clk gpt6_fck = {
	.name = "gpt6_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt6_fck_data,
};

static struct ti_clk_fixed_factor dpll5_m2_d4_ck_data = {
	.parent = "dpll5_m2_ck",
	.div = 4,
	.mult = 1,
};

static struct ti_clk dpll5_m2_d4_ck = {
	.name = "dpll5_m2_d4_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll5_m2_d4_ck_data,
};

static struct ti_clk_fixed_factor sys_d2_ck_data = {
	.parent = "sys_ck",
	.div = 2,
	.mult = 1,
};

static struct ti_clk sys_d2_ck = {
	.name = "sys_d2_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &sys_d2_ck_data,
};

static struct ti_clk_fixed_factor omap_96m_d2_fck_data = {
	.parent = "omap_96m_fck",
	.div = 2,
	.mult = 1,
};

static struct ti_clk omap_96m_d2_fck = {
	.name = "omap_96m_d2_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &omap_96m_d2_fck_data,
};

static struct ti_clk_fixed_factor dpll5_m2_d8_ck_data = {
	.parent = "dpll5_m2_ck",
	.div = 8,
	.mult = 1,
};

static struct ti_clk dpll5_m2_d8_ck = {
	.name = "dpll5_m2_d8_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll5_m2_d8_ck_data,
};

static struct ti_clk_fixed_factor dpll5_m2_d16_ck_data = {
	.parent = "dpll5_m2_ck",
	.div = 16,
	.mult = 1,
};

static struct ti_clk dpll5_m2_d16_ck = {
	.name = "dpll5_m2_d16_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll5_m2_d16_ck_data,
};

static const char *usim_mux_fck_parents[] = {
	"sys_ck",
	"sys_d2_ck",
	"omap_96m_d2_fck",
	"omap_96m_d4_fck",
	"omap_96m_d8_fck",
	"omap_96m_d10_fck",
	"dpll5_m2_d4_ck",
	"dpll5_m2_d8_ck",
	"dpll5_m2_d16_ck",
	"dpll5_m2_d20_ck",
};

static struct ti_clk_mux usim_mux_fck_data = {
	.bit_shift = 3,
	.num_parents = ARRAY_SIZE(usim_mux_fck_parents),
	.reg = 0xc40,
	.module = TI_CLKM_CM,
	.parents = usim_mux_fck_parents,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk_composite usim_fck_data = {
	.mux = &usim_mux_fck_data,
	.gate = &usim_gate_fck_data,
};

static struct ti_clk usim_fck = {
	.name = "usim_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &usim_fck_data,
};

static int ssi_ssr_div_fck_3430es2_divs[] = {
	0,
	1,
	2,
	3,
	4,
	0,
	6,
	0,
	8,
};

static struct ti_clk_divider ssi_ssr_div_fck_3430es2_data = {
	.num_dividers = ARRAY_SIZE(ssi_ssr_div_fck_3430es2_divs),
	.parent = "corex2_fck",
	.bit_shift = 8,
	.dividers = ssi_ssr_div_fck_3430es2_divs,
	.reg = 0xa40,
	.module = TI_CLKM_CM,
};

static struct ti_clk_composite ssi_ssr_fck_3430es2_data = {
	.gate = &ssi_ssr_gate_fck_3430es2_data,
	.divider = &ssi_ssr_div_fck_3430es2_data,
};

static struct ti_clk ssi_ssr_fck_3430es2 = {
	.name = "ssi_ssr_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &ssi_ssr_fck_3430es2_data,
};

static struct ti_clk_gate dss1_alwon_fck_3430es1_data = {
	.parent = "dpll4_m4x2_ck",
	.bit_shift = 0,
	.reg = 0xe00,
	.module = TI_CLKM_CM,
	.flags = CLKF_SET_RATE_PARENT,
};

static struct ti_clk dss1_alwon_fck_3430es1 = {
	.name = "dss1_alwon_fck",
	.clkdm_name = "dss_clkdm",
	.type = TI_CLK_GATE,
	.data = &dss1_alwon_fck_3430es1_data,
};

static struct ti_clk_gate gpt3_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 4,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt3_ick = {
	.name = "gpt3_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt3_ick_data,
};

static struct ti_clk_fixed_factor omap_12m_fck_data = {
	.parent = "omap_48m_fck",
	.div = 4,
	.mult = 1,
};

static struct ti_clk omap_12m_fck = {
	.name = "omap_12m_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &omap_12m_fck_data,
};

static struct ti_clk_fixed_factor core_12m_fck_data = {
	.parent = "omap_12m_fck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk core_12m_fck = {
	.name = "core_12m_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &core_12m_fck_data,
};

static struct ti_clk_gate hdq_fck_data = {
	.parent = "core_12m_fck",
	.bit_shift = 22,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk hdq_fck = {
	.name = "hdq_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &hdq_fck_data,
};

static struct ti_clk_gate usbtll_fck_data = {
	.parent = "dpll5_m2_ck",
	.bit_shift = 2,
	.reg = 0xa08,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk usbtll_fck = {
	.name = "usbtll_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &usbtll_fck_data,
};

static struct ti_clk_gate hsotgusb_fck_am35xx_data = {
	.parent = "sys_ck",
	.bit_shift = 8,
	.reg = 0x59c,
	.module = TI_CLKM_SCRM,
};

static struct ti_clk hsotgusb_fck_am35xx = {
	.name = "hsotgusb_fck_am35xx",
	.clkdm_name = "core_l3_clkdm",
	.type = TI_CLK_GATE,
	.data = &hsotgusb_fck_am35xx_data,
};

static struct ti_clk_gate hsotgusb_ick_3430es2_data = {
	.parent = "core_l3_ick",
	.bit_shift = 4,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_HSOTGUSB | CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk hsotgusb_ick_3430es2 = {
	.name = "hsotgusb_ick_3430es2",
	.clkdm_name = "core_l3_clkdm",
	.type = TI_CLK_GATE,
	.data = &hsotgusb_ick_3430es2_data,
};

static struct ti_clk_gate gfx_l3_ck_data = {
	.parent = "l3_ick",
	.bit_shift = 0,
	.reg = 0xb10,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk gfx_l3_ck = {
	.name = "gfx_l3_ck",
	.clkdm_name = "gfx_3430es1_clkdm",
	.type = TI_CLK_GATE,
	.data = &gfx_l3_ck_data,
};

static struct ti_clk_fixed_factor gfx_l3_ick_data = {
	.parent = "gfx_l3_ck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk gfx_l3_ick = {
	.name = "gfx_l3_ick",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &gfx_l3_ick_data,
};

static struct ti_clk_gate mcbsp1_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 9,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mcbsp1_ick = {
	.name = "mcbsp1_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcbsp1_ick_data,
};

static struct ti_clk_fixed_factor gpt12_fck_data = {
	.parent = "secure_32k_fck",
	.div = 1,
	.mult = 1,
};

static struct ti_clk gpt12_fck = {
	.name = "gpt12_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &gpt12_fck_data,
};

static struct ti_clk_gate gfx_cg2_ck_data = {
	.parent = "gfx_l3_fck",
	.bit_shift = 2,
	.reg = 0xb00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk gfx_cg2_ck = {
	.name = "gfx_cg2_ck",
	.clkdm_name = "gfx_3430es1_clkdm",
	.type = TI_CLK_GATE,
	.data = &gfx_cg2_ck_data,
};

static struct ti_clk_gate i2c2_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 16,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk i2c2_ick = {
	.name = "i2c2_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &i2c2_ick_data,
};

static struct ti_clk_gate gpio4_dbck_data = {
	.parent = "per_32k_alwon_fck",
	.bit_shift = 15,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk gpio4_dbck = {
	.name = "gpio4_dbck",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio4_dbck_data,
};

static struct ti_clk_gate i2c3_fck_data = {
	.parent = "core_96m_fck",
	.bit_shift = 17,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk i2c3_fck = {
	.name = "i2c3_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &i2c3_fck_data,
};

static struct ti_clk_composite gpt3_fck_data = {
	.mux = &gpt3_mux_fck_data,
	.gate = &gpt3_gate_fck_data,
};

static struct ti_clk gpt3_fck = {
	.name = "gpt3_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt3_fck_data,
};

static struct ti_clk_gate i2c1_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 15,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk i2c1_ick = {
	.name = "i2c1_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &i2c1_ick_data,
};

static struct ti_clk_gate omap_32ksync_ick_data = {
	.parent = "wkup_l4_ick",
	.bit_shift = 2,
	.reg = 0xc10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk omap_32ksync_ick = {
	.name = "omap_32ksync_ick",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &omap_32ksync_ick_data,
};

static struct ti_clk_gate aes2_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 28,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk aes2_ick = {
	.name = "aes2_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &aes2_ick_data,
};

static const char *gpt8_mux_fck_parents[] = {
	"omap_32k_fck",
	"sys_ck",
};

static struct ti_clk_mux gpt8_mux_fck_data = {
	.bit_shift = 6,
	.num_parents = ARRAY_SIZE(gpt8_mux_fck_parents),
	.reg = 0x1040,
	.module = TI_CLKM_CM,
	.parents = gpt8_mux_fck_parents,
};

static struct ti_clk_composite gpt8_fck_data = {
	.mux = &gpt8_mux_fck_data,
	.gate = &gpt8_gate_fck_data,
};

static struct ti_clk gpt8_fck = {
	.name = "gpt8_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt8_fck_data,
};

static struct ti_clk_gate mcbsp4_gate_fck_data = {
	.parent = "mcbsp_clks",
	.bit_shift = 2,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk_composite mcbsp4_fck_data = {
	.mux = &mcbsp4_mux_fck_data,
	.gate = &mcbsp4_gate_fck_data,
};

static struct ti_clk mcbsp4_fck = {
	.name = "mcbsp4_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &mcbsp4_fck_data,
};

static struct ti_clk_gate gpio2_dbck_data = {
	.parent = "per_32k_alwon_fck",
	.bit_shift = 13,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk gpio2_dbck = {
	.name = "gpio2_dbck",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio2_dbck_data,
};

static struct ti_clk_gate usbtll_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 2,
	.reg = 0xa18,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk usbtll_ick = {
	.name = "usbtll_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &usbtll_ick_data,
};

static struct ti_clk_gate mcspi4_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 21,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mcspi4_ick = {
	.name = "mcspi4_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcspi4_ick_data,
};

static struct ti_clk_gate dss_96m_fck_data = {
	.parent = "omap_96m_fck",
	.bit_shift = 2,
	.reg = 0xe00,
	.module = TI_CLKM_CM,
};

static struct ti_clk dss_96m_fck = {
	.name = "dss_96m_fck",
	.clkdm_name = "dss_clkdm",
	.type = TI_CLK_GATE,
	.data = &dss_96m_fck_data,
};

static struct ti_clk_divider rm_ick_data = {
	.parent = "l4_ick",
	.bit_shift = 1,
	.max_div = 3,
	.reg = 0xc40,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk rm_ick = {
	.name = "rm_ick",
	.type = TI_CLK_DIVIDER,
	.data = &rm_ick_data,
};

static struct ti_clk_gate hdq_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 22,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk hdq_ick = {
	.name = "hdq_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &hdq_ick_data,
};

static struct ti_clk_fixed_factor dpll3_x2_ck_data = {
	.parent = "dpll3_ck",
	.div = 1,
	.mult = 2,
};

static struct ti_clk dpll3_x2_ck = {
	.name = "dpll3_x2_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll3_x2_ck_data,
};

static struct ti_clk_gate mad2d_ick_data = {
	.parent = "l3_ick",
	.bit_shift = 3,
	.reg = 0xa18,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mad2d_ick = {
	.name = "mad2d_ick",
	.clkdm_name = "d2d_clkdm",
	.type = TI_CLK_GATE,
	.data = &mad2d_ick_data,
};

static struct ti_clk_gate fshostusb_fck_data = {
	.parent = "core_48m_fck",
	.bit_shift = 5,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk fshostusb_fck = {
	.name = "fshostusb_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &fshostusb_fck_data,
};

static struct ti_clk_gate sr1_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 6,
	.reg = 0xc00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk sr1_fck = {
	.name = "sr1_fck",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &sr1_fck_data,
};

static struct ti_clk_gate des2_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 26,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk des2_ick = {
	.name = "des2_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &des2_ick_data,
};

static struct ti_clk_gate sdrc_ick_data = {
	.parent = "core_l3_ick",
	.bit_shift = 1,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk sdrc_ick = {
	.name = "sdrc_ick",
	.clkdm_name = "core_l3_clkdm",
	.type = TI_CLK_GATE,
	.data = &sdrc_ick_data,
};

static struct ti_clk_composite gpt4_fck_data = {
	.mux = &gpt4_mux_fck_data,
	.gate = &gpt4_gate_fck_data,
};

static struct ti_clk gpt4_fck = {
	.name = "gpt4_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt4_fck_data,
};

static struct ti_clk_gate dpll4_m3x2_ck_omap36xx_data = {
	.parent = "dpll4_m3x2_mul_ck",
	.bit_shift = 0x1c,
	.reg = 0xd00,
	.module = TI_CLKM_CM,
	.flags = CLKF_HSDIV | CLKF_SET_BIT_TO_DISABLE,
};

static struct ti_clk dpll4_m3x2_ck_omap36xx = {
	.name = "dpll4_m3x2_ck",
	.type = TI_CLK_GATE,
	.data = &dpll4_m3x2_ck_omap36xx_data,
	.patch = &dpll4_m3x2_ck,
};

static struct ti_clk_gate cpefuse_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 0,
	.reg = 0xa08,
	.module = TI_CLKM_CM,
};

static struct ti_clk cpefuse_fck = {
	.name = "cpefuse_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &cpefuse_fck_data,
};

static struct ti_clk_gate mcspi3_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 20,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mcspi3_ick = {
	.name = "mcspi3_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcspi3_ick_data,
};

static struct ti_clk_fixed_factor ssi_sst_fck_3430es2_data = {
	.parent = "ssi_ssr_fck",
	.div = 2,
	.mult = 1,
};

static struct ti_clk ssi_sst_fck_3430es2 = {
	.name = "ssi_sst_fck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &ssi_sst_fck_3430es2_data,
};

static struct ti_clk_gate gpio1_dbck_data = {
	.parent = "wkup_32k_fck",
	.bit_shift = 3,
	.reg = 0xc00,
	.module = TI_CLKM_CM,
};

static struct ti_clk gpio1_dbck = {
	.name = "gpio1_dbck",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio1_dbck_data,
};

static struct ti_clk_gate gpt4_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 5,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt4_ick = {
	.name = "gpt4_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt4_ick_data,
};

static struct ti_clk_gate gpt2_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 3,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt2_ick = {
	.name = "gpt2_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt2_ick_data,
};

static struct ti_clk_gate mmchs1_fck_data = {
	.parent = "core_96m_fck",
	.bit_shift = 24,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk mmchs1_fck = {
	.name = "mmchs1_fck",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mmchs1_fck_data,
};

static struct ti_clk_fixed dummy_apb_pclk_data = {
	.frequency = 0x0,
};

static struct ti_clk dummy_apb_pclk = {
	.name = "dummy_apb_pclk",
	.type = TI_CLK_FIXED,
	.data = &dummy_apb_pclk_data,
};

static struct ti_clk_gate gpio6_dbck_data = {
	.parent = "per_32k_alwon_fck",
	.bit_shift = 17,
	.reg = 0x1000,
	.module = TI_CLKM_CM,
};

static struct ti_clk gpio6_dbck = {
	.name = "gpio6_dbck",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio6_dbck_data,
};

static struct ti_clk_gate uart2_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 14,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk uart2_ick = {
	.name = "uart2_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &uart2_ick_data,
};

static struct ti_clk_fixed_factor dpll4_x2_ck_data = {
	.parent = "dpll4_ck",
	.div = 1,
	.mult = 2,
};

static struct ti_clk dpll4_x2_ck = {
	.name = "dpll4_x2_ck",
	.type = TI_CLK_FIXED_FACTOR,
	.data = &dpll4_x2_ck_data,
};

static struct ti_clk_gate gpt7_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 8,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpt7_ick = {
	.name = "gpt7_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpt7_ick_data,
};

static struct ti_clk_gate dss_tv_fck_data = {
	.parent = "omap_54m_fck",
	.bit_shift = 2,
	.reg = 0xe00,
	.module = TI_CLKM_CM,
};

static struct ti_clk dss_tv_fck = {
	.name = "dss_tv_fck",
	.clkdm_name = "dss_clkdm",
	.type = TI_CLK_GATE,
	.data = &dss_tv_fck_data,
};

static struct ti_clk_gate mcbsp5_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 10,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mcbsp5_ick = {
	.name = "mcbsp5_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcbsp5_ick_data,
};

static struct ti_clk_gate mcspi1_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 18,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk mcspi1_ick = {
	.name = "mcspi1_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &mcspi1_ick_data,
};

static struct ti_clk_gate d2d_26m_fck_data = {
	.parent = "sys_ck",
	.bit_shift = 3,
	.reg = 0xa00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk d2d_26m_fck = {
	.name = "d2d_26m_fck",
	.clkdm_name = "d2d_clkdm",
	.type = TI_CLK_GATE,
	.data = &d2d_26m_fck_data,
};

static struct ti_clk_gate wdt3_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 12,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk wdt3_ick = {
	.name = "wdt3_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &wdt3_ick_data,
};

static struct ti_clk_divider pclkx2_fck_data = {
	.parent = "emu_src_ck",
	.bit_shift = 6,
	.max_div = 3,
	.reg = 0x1140,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_STARTS_AT_ONE,
};

static struct ti_clk pclkx2_fck = {
	.name = "pclkx2_fck",
	.type = TI_CLK_DIVIDER,
	.data = &pclkx2_fck_data,
};

static struct ti_clk_gate sha12_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 27,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk sha12_ick = {
	.name = "sha12_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &sha12_ick_data,
};

static struct ti_clk_gate emac_fck_data = {
	.parent = "rmii_ck",
	.bit_shift = 9,
	.reg = 0x59c,
	.module = TI_CLKM_SCRM,
};

static struct ti_clk emac_fck = {
	.name = "emac_fck",
	.type = TI_CLK_GATE,
	.data = &emac_fck_data,
};

static struct ti_clk_composite gpt10_fck_data = {
	.mux = &gpt10_mux_fck_data,
	.gate = &gpt10_gate_fck_data,
};

static struct ti_clk gpt10_fck = {
	.name = "gpt10_fck",
	.type = TI_CLK_COMPOSITE,
	.data = &gpt10_fck_data,
};

static struct ti_clk_gate wdt2_fck_data = {
	.parent = "wkup_32k_fck",
	.bit_shift = 5,
	.reg = 0xc00,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk wdt2_fck = {
	.name = "wdt2_fck",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &wdt2_fck_data,
};

static struct ti_clk_gate cam_ick_data = {
	.parent = "l4_ick",
	.bit_shift = 0,
	.reg = 0xf10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_NO_WAIT | CLKF_INTERFACE,
};

static struct ti_clk cam_ick = {
	.name = "cam_ick",
	.clkdm_name = "cam_clkdm",
	.type = TI_CLK_GATE,
	.data = &cam_ick_data,
};

static struct ti_clk_gate ssi_ick_3430es2_data = {
	.parent = "ssi_l4_ick",
	.bit_shift = 0,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_SSI | CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk ssi_ick_3430es2 = {
	.name = "ssi_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &ssi_ick_3430es2_data,
};

static struct ti_clk_gate gpio4_ick_data = {
	.parent = "per_l4_ick",
	.bit_shift = 15,
	.reg = 0x1010,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk gpio4_ick = {
	.name = "gpio4_ick",
	.clkdm_name = "per_clkdm",
	.type = TI_CLK_GATE,
	.data = &gpio4_ick_data,
};

static struct ti_clk_gate wdt1_ick_data = {
	.parent = "wkup_l4_ick",
	.bit_shift = 4,
	.reg = 0xc10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk wdt1_ick = {
	.name = "wdt1_ick",
	.clkdm_name = "wkup_clkdm",
	.type = TI_CLK_GATE,
	.data = &wdt1_ick_data,
};

static struct ti_clk_gate rng_ick_data = {
	.parent = "security_l4_ick2",
	.bit_shift = 2,
	.reg = 0xa14,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk rng_ick = {
	.name = "rng_ick",
	.type = TI_CLK_GATE,
	.data = &rng_ick_data,
};

static struct ti_clk_gate icr_ick_data = {
	.parent = "core_l4_ick",
	.bit_shift = 29,
	.reg = 0xa10,
	.module = TI_CLKM_CM,
	.flags = CLKF_OMAP3 | CLKF_INTERFACE,
};

static struct ti_clk icr_ick = {
	.name = "icr_ick",
	.clkdm_name = "core_l4_clkdm",
	.type = TI_CLK_GATE,
	.data = &icr_ick_data,
};

static struct ti_clk_gate sgx_ick_data = {
	.parent = "l3_ick",
	.bit_shift = 0,
	.reg = 0xb10,
	.module = TI_CLKM_CM,
	.flags = CLKF_WAIT,
};

static struct ti_clk sgx_ick = {
	.name = "sgx_ick",
	.clkdm_name = "sgx_clkdm",
	.type = TI_CLK_GATE,
	.data = &sgx_ick_data,
};

static struct ti_clk_divider sys_clkout2_data = {
	.parent = "clkout2_src_ck",
	.bit_shift = 3,
	.max_div = 64,
	.reg = 0xd70,
	.module = TI_CLKM_CM,
	.flags = CLKF_INDEX_POWER_OF_TWO,
};

static struct ti_clk sys_clkout2 = {
	.name = "sys_clkout2",
	.type = TI_CLK_DIVIDER,
	.data = &sys_clkout2_data,
};

static struct ti_clk_alias omap34xx_omap36xx_clks[] = {
	CLK(NULL, "security_l4_ick2", &security_l4_ick2),
	CLK(NULL, "aes1_ick", &aes1_ick),
	CLK("omap_rng", "ick", &rng_ick),
	CLK("omap3-rom-rng", "ick", &rng_ick),
	CLK(NULL, "sha11_ick", &sha11_ick),
	CLK(NULL, "des1_ick", &des1_ick),
	CLK(NULL, "cam_mclk", &cam_mclk),
	CLK(NULL, "cam_ick", &cam_ick),
	CLK(NULL, "csi2_96m_fck", &csi2_96m_fck),
	CLK(NULL, "security_l3_ick", &security_l3_ick),
	CLK(NULL, "pka_ick", &pka_ick),
	CLK(NULL, "icr_ick", &icr_ick),
	CLK(NULL, "des2_ick", &des2_ick),
	CLK(NULL, "mspro_ick", &mspro_ick),
	CLK(NULL, "mailboxes_ick", &mailboxes_ick),
	CLK(NULL, "ssi_l4_ick", &ssi_l4_ick),
	CLK(NULL, "sr1_fck", &sr1_fck),
	CLK(NULL, "sr2_fck", &sr2_fck),
	CLK(NULL, "sr_l4_ick", &sr_l4_ick),
	CLK(NULL, "dpll2_fck", &dpll2_fck),
	CLK(NULL, "dpll2_ck", &dpll2_ck),
	CLK(NULL, "dpll2_m2_ck", &dpll2_m2_ck),
	CLK(NULL, "iva2_ck", &iva2_ck),
	CLK(NULL, "modem_fck", &modem_fck),
	CLK(NULL, "sad2d_ick", &sad2d_ick),
	CLK(NULL, "mad2d_ick", &mad2d_ick),
	CLK(NULL, "mspro_fck", &mspro_fck),
	{ NULL },
};

static struct ti_clk_alias omap36xx_omap3430es2plus_clks[] = {
	CLK(NULL, "ssi_ssr_fck", &ssi_ssr_fck_3430es2),
	CLK(NULL, "ssi_sst_fck", &ssi_sst_fck_3430es2),
	CLK("musb-omap2430", "ick", &hsotgusb_ick_3430es2),
	CLK(NULL, "hsotgusb_ick", &hsotgusb_ick_3430es2),
	CLK(NULL, "ssi_ick", &ssi_ick_3430es2),
	CLK(NULL, "sys_d2_ck", &sys_d2_ck),
	CLK(NULL, "omap_96m_d2_fck", &omap_96m_d2_fck),
	CLK(NULL, "omap_96m_d4_fck", &omap_96m_d4_fck),
	CLK(NULL, "omap_96m_d8_fck", &omap_96m_d8_fck),
	CLK(NULL, "omap_96m_d10_fck", &omap_96m_d10_fck),
	CLK(NULL, "dpll5_m2_d4_ck", &dpll5_m2_d4_ck),
	CLK(NULL, "dpll5_m2_d8_ck", &dpll5_m2_d8_ck),
	CLK(NULL, "dpll5_m2_d16_ck", &dpll5_m2_d16_ck),
	CLK(NULL, "dpll5_m2_d20_ck", &dpll5_m2_d20_ck),
	CLK(NULL, "usim_fck", &usim_fck),
	CLK(NULL, "usim_ick", &usim_ick),
	{ NULL },
};

static struct ti_clk_alias omap3xxx_clks[] = {
	CLK(NULL, "apb_pclk", &dummy_apb_pclk),
	CLK(NULL, "omap_32k_fck", &omap_32k_fck),
	CLK(NULL, "virt_12m_ck", &virt_12m_ck),
	CLK(NULL, "virt_13m_ck", &virt_13m_ck),
	CLK(NULL, "virt_19200000_ck", &virt_19200000_ck),
	CLK(NULL, "virt_26000000_ck", &virt_26000000_ck),
	CLK(NULL, "virt_38_4m_ck", &virt_38_4m_ck),
	CLK(NULL, "virt_16_8m_ck", &virt_16_8m_ck),
	CLK(NULL, "osc_sys_ck", &osc_sys_ck),
	CLK("twl", "fck", &osc_sys_ck),
	CLK(NULL, "sys_ck", &sys_ck),
	CLK(NULL, "timer_sys_ck", &sys_ck),
	CLK(NULL, "dpll4_ck", &dpll4_ck),
	CLK(NULL, "dpll4_m2_ck", &dpll4_m2_ck),
	CLK(NULL, "dpll4_m2x2_mul_ck", &dpll4_m2x2_mul_ck),
	CLK(NULL, "dpll4_m2x2_ck", &dpll4_m2x2_ck),
	CLK(NULL, "omap_96m_alwon_fck", &omap_96m_alwon_fck),
	CLK(NULL, "dpll3_ck", &dpll3_ck),
	CLK(NULL, "dpll3_m3_ck", &dpll3_m3_ck),
	CLK(NULL, "dpll3_m3x2_mul_ck", &dpll3_m3x2_mul_ck),
	CLK(NULL, "dpll3_m3x2_ck", &dpll3_m3x2_ck),
	CLK("etb", "emu_core_alwon_ck", &emu_core_alwon_ck),
	CLK(NULL, "sys_altclk", &sys_altclk),
	CLK(NULL, "mcbsp_clks", &mcbsp_clks),
	CLK(NULL, "sys_clkout1", &sys_clkout1),
	CLK(NULL, "dpll3_m2_ck", &dpll3_m2_ck),
	CLK(NULL, "core_ck", &core_ck),
	CLK(NULL, "dpll1_fck", &dpll1_fck),
	CLK(NULL, "dpll1_ck", &dpll1_ck),
	CLK(NULL, "cpufreq_ck", &dpll1_ck),
	CLK(NULL, "dpll1_x2_ck", &dpll1_x2_ck),
	CLK(NULL, "dpll1_x2m2_ck", &dpll1_x2m2_ck),
	CLK(NULL, "dpll3_x2_ck", &dpll3_x2_ck),
	CLK(NULL, "dpll3_m2x2_ck", &dpll3_m2x2_ck),
	CLK(NULL, "dpll4_x2_ck", &dpll4_x2_ck),
	CLK(NULL, "cm_96m_fck", &cm_96m_fck),
	CLK(NULL, "omap_96m_fck", &omap_96m_fck),
	CLK(NULL, "dpll4_m3_ck", &dpll4_m3_ck),
	CLK(NULL, "dpll4_m3x2_mul_ck", &dpll4_m3x2_mul_ck),
	CLK(NULL, "dpll4_m3x2_ck", &dpll4_m3x2_ck),
	CLK(NULL, "omap_54m_fck", &omap_54m_fck),
	CLK(NULL, "cm_96m_d2_fck", &cm_96m_d2_fck),
	CLK(NULL, "omap_48m_fck", &omap_48m_fck),
	CLK(NULL, "omap_12m_fck", &omap_12m_fck),
	CLK(NULL, "dpll4_m4_ck", &dpll4_m4_ck),
	CLK(NULL, "dpll4_m4x2_mul_ck", &dpll4_m4x2_mul_ck),
	CLK(NULL, "dpll4_m4x2_ck", &dpll4_m4x2_ck),
	CLK(NULL, "dpll4_m5_ck", &dpll4_m5_ck),
	CLK(NULL, "dpll4_m5x2_mul_ck", &dpll4_m5x2_mul_ck),
	CLK(NULL, "dpll4_m5x2_ck", &dpll4_m5x2_ck),
	CLK(NULL, "dpll4_m6_ck", &dpll4_m6_ck),
	CLK(NULL, "dpll4_m6x2_mul_ck", &dpll4_m6x2_mul_ck),
	CLK(NULL, "dpll4_m6x2_ck", &dpll4_m6x2_ck),
	CLK("etb", "emu_per_alwon_ck", &emu_per_alwon_ck),
	CLK(NULL, "clkout2_src_ck", &clkout2_src_ck),
	CLK(NULL, "sys_clkout2", &sys_clkout2),
	CLK(NULL, "corex2_fck", &corex2_fck),
	CLK(NULL, "mpu_ck", &mpu_ck),
	CLK(NULL, "arm_fck", &arm_fck),
	CLK("etb", "emu_mpu_alwon_ck", &emu_mpu_alwon_ck),
	CLK(NULL, "l3_ick", &l3_ick),
	CLK(NULL, "l4_ick", &l4_ick),
	CLK(NULL, "rm_ick", &rm_ick),
	CLK(NULL, "timer_32k_ck", &omap_32k_fck),
	CLK(NULL, "gpt10_fck", &gpt10_fck),
	CLK(NULL, "gpt11_fck", &gpt11_fck),
	CLK(NULL, "core_96m_fck", &core_96m_fck),
	CLK(NULL, "mmchs2_fck", &mmchs2_fck),
	CLK(NULL, "mmchs1_fck", &mmchs1_fck),
	CLK(NULL, "i2c3_fck", &i2c3_fck),
	CLK(NULL, "i2c2_fck", &i2c2_fck),
	CLK(NULL, "i2c1_fck", &i2c1_fck),
	CLK(NULL, "mcbsp5_fck", &mcbsp5_fck),
	CLK(NULL, "mcbsp1_fck", &mcbsp1_fck),
	CLK(NULL, "core_48m_fck", &core_48m_fck),
	CLK(NULL, "mcspi4_fck", &mcspi4_fck),
	CLK(NULL, "mcspi3_fck", &mcspi3_fck),
	CLK(NULL, "mcspi2_fck", &mcspi2_fck),
	CLK(NULL, "mcspi1_fck", &mcspi1_fck),
	CLK(NULL, "uart2_fck", &uart2_fck),
	CLK(NULL, "uart1_fck", &uart1_fck),
	CLK(NULL, "core_12m_fck", &core_12m_fck),
	CLK("omap_hdq.0", "fck", &hdq_fck),
	CLK(NULL, "hdq_fck", &hdq_fck),
	CLK(NULL, "core_l3_ick", &core_l3_ick),
	CLK(NULL, "sdrc_ick", &sdrc_ick),
	CLK(NULL, "gpmc_fck", &gpmc_fck),
	CLK(NULL, "core_l4_ick", &core_l4_ick),
	CLK("omap_hsmmc.1", "ick", &mmchs2_ick),
	CLK("omap_hsmmc.0", "ick", &mmchs1_ick),
	CLK(NULL, "mmchs2_ick", &mmchs2_ick),
	CLK(NULL, "mmchs1_ick", &mmchs1_ick),
	CLK("omap_hdq.0", "ick", &hdq_ick),
	CLK(NULL, "hdq_ick", &hdq_ick),
	CLK("omap2_mcspi.4", "ick", &mcspi4_ick),
	CLK("omap2_mcspi.3", "ick", &mcspi3_ick),
	CLK("omap2_mcspi.2", "ick", &mcspi2_ick),
	CLK("omap2_mcspi.1", "ick", &mcspi1_ick),
	CLK(NULL, "mcspi4_ick", &mcspi4_ick),
	CLK(NULL, "mcspi3_ick", &mcspi3_ick),
	CLK(NULL, "mcspi2_ick", &mcspi2_ick),
	CLK(NULL, "mcspi1_ick", &mcspi1_ick),
	CLK("omap_i2c.3", "ick", &i2c3_ick),
	CLK("omap_i2c.2", "ick", &i2c2_ick),
	CLK("omap_i2c.1", "ick", &i2c1_ick),
	CLK(NULL, "i2c3_ick", &i2c3_ick),
	CLK(NULL, "i2c2_ick", &i2c2_ick),
	CLK(NULL, "i2c1_ick", &i2c1_ick),
	CLK(NULL, "uart2_ick", &uart2_ick),
	CLK(NULL, "uart1_ick", &uart1_ick),
	CLK(NULL, "gpt11_ick", &gpt11_ick),
	CLK(NULL, "gpt10_ick", &gpt10_ick),
	CLK("omap-mcbsp.5", "ick", &mcbsp5_ick),
	CLK("omap-mcbsp.1", "ick", &mcbsp1_ick),
	CLK(NULL, "mcbsp5_ick", &mcbsp5_ick),
	CLK(NULL, "mcbsp1_ick", &mcbsp1_ick),
	CLK(NULL, "omapctrl_ick", &omapctrl_ick),
	CLK(NULL, "dss_tv_fck", &dss_tv_fck),
	CLK(NULL, "dss_96m_fck", &dss_96m_fck),
	CLK(NULL, "dss2_alwon_fck", &dss2_alwon_fck),
	CLK(NULL, "init_60m_fclk", &dummy_ck),
	CLK(NULL, "gpt1_fck", &gpt1_fck),
	CLK(NULL, "aes2_ick", &aes2_ick),
	CLK(NULL, "wkup_32k_fck", &wkup_32k_fck),
	CLK(NULL, "gpio1_dbck", &gpio1_dbck),
	CLK(NULL, "sha12_ick", &sha12_ick),
	CLK(NULL, "wdt2_fck", &wdt2_fck),
	CLK(NULL, "wkup_l4_ick", &wkup_l4_ick),
	CLK("omap_wdt", "ick", &wdt2_ick),
	CLK(NULL, "wdt2_ick", &wdt2_ick),
	CLK(NULL, "wdt1_ick", &wdt1_ick),
	CLK(NULL, "gpio1_ick", &gpio1_ick),
	CLK(NULL, "omap_32ksync_ick", &omap_32ksync_ick),
	CLK(NULL, "gpt12_ick", &gpt12_ick),
	CLK(NULL, "gpt1_ick", &gpt1_ick),
	CLK(NULL, "per_96m_fck", &per_96m_fck),
	CLK(NULL, "per_48m_fck", &per_48m_fck),
	CLK(NULL, "uart3_fck", &uart3_fck),
	CLK(NULL, "gpt2_fck", &gpt2_fck),
	CLK(NULL, "gpt3_fck", &gpt3_fck),
	CLK(NULL, "gpt4_fck", &gpt4_fck),
	CLK(NULL, "gpt5_fck", &gpt5_fck),
	CLK(NULL, "gpt6_fck", &gpt6_fck),
	CLK(NULL, "gpt7_fck", &gpt7_fck),
	CLK(NULL, "gpt8_fck", &gpt8_fck),
	CLK(NULL, "gpt9_fck", &gpt9_fck),
	CLK(NULL, "per_32k_alwon_fck", &per_32k_alwon_fck),
	CLK(NULL, "gpio6_dbck", &gpio6_dbck),
	CLK(NULL, "gpio5_dbck", &gpio5_dbck),
	CLK(NULL, "gpio4_dbck", &gpio4_dbck),
	CLK(NULL, "gpio3_dbck", &gpio3_dbck),
	CLK(NULL, "gpio2_dbck", &gpio2_dbck),
	CLK(NULL, "wdt3_fck", &wdt3_fck),
	CLK(NULL, "per_l4_ick", &per_l4_ick),
	CLK(NULL, "gpio6_ick", &gpio6_ick),
	CLK(NULL, "gpio5_ick", &gpio5_ick),
	CLK(NULL, "gpio4_ick", &gpio4_ick),
	CLK(NULL, "gpio3_ick", &gpio3_ick),
	CLK(NULL, "gpio2_ick", &gpio2_ick),
	CLK(NULL, "wdt3_ick", &wdt3_ick),
	CLK(NULL, "uart3_ick", &uart3_ick),
	CLK(NULL, "uart4_ick", &uart4_ick),
	CLK(NULL, "gpt9_ick", &gpt9_ick),
	CLK(NULL, "gpt8_ick", &gpt8_ick),
	CLK(NULL, "gpt7_ick", &gpt7_ick),
	CLK(NULL, "gpt6_ick", &gpt6_ick),
	CLK(NULL, "gpt5_ick", &gpt5_ick),
	CLK(NULL, "gpt4_ick", &gpt4_ick),
	CLK(NULL, "gpt3_ick", &gpt3_ick),
	CLK(NULL, "gpt2_ick", &gpt2_ick),
	CLK("omap-mcbsp.2", "ick", &mcbsp2_ick),
	CLK("omap-mcbsp.3", "ick", &mcbsp3_ick),
	CLK("omap-mcbsp.4", "ick", &mcbsp4_ick),
	CLK(NULL, "mcbsp4_ick", &mcbsp2_ick),
	CLK(NULL, "mcbsp3_ick", &mcbsp3_ick),
	CLK(NULL, "mcbsp2_ick", &mcbsp4_ick),
	CLK(NULL, "mcbsp2_fck", &mcbsp2_fck),
	CLK(NULL, "mcbsp3_fck", &mcbsp3_fck),
	CLK(NULL, "mcbsp4_fck", &mcbsp4_fck),
	CLK(NULL, "emu_src_mux_ck", &emu_src_mux_ck),
	CLK("etb", "emu_src_ck", &emu_src_ck),
	CLK(NULL, "emu_src_mux_ck", &emu_src_mux_ck),
	CLK(NULL, "emu_src_ck", &emu_src_ck),
	CLK(NULL, "pclk_fck", &pclk_fck),
	CLK(NULL, "pclkx2_fck", &pclkx2_fck),
	CLK(NULL, "atclk_fck", &atclk_fck),
	CLK(NULL, "traceclk_src_fck", &traceclk_src_fck),
	CLK(NULL, "traceclk_fck", &traceclk_fck),
	CLK(NULL, "secure_32k_fck", &secure_32k_fck),
	CLK(NULL, "gpt12_fck", &gpt12_fck),
	CLK(NULL, "wdt1_fck", &wdt1_fck),
	{ NULL },
};

static struct ti_clk_alias omap36xx_am35xx_omap3430es2plus_clks[] = {
	CLK(NULL, "dpll5_ck", &dpll5_ck),
	CLK(NULL, "dpll5_m2_ck", &dpll5_m2_ck),
	CLK(NULL, "core_d3_ck", &core_d3_ck),
	CLK(NULL, "core_d4_ck", &core_d4_ck),
	CLK(NULL, "core_d6_ck", &core_d6_ck),
	CLK(NULL, "omap_192m_alwon_fck", &omap_192m_alwon_fck),
	CLK(NULL, "core_d2_ck", &core_d2_ck),
	CLK(NULL, "corex2_d3_fck", &corex2_d3_fck),
	CLK(NULL, "corex2_d5_fck", &corex2_d5_fck),
	CLK(NULL, "sgx_fck", &sgx_fck),
	CLK(NULL, "sgx_ick", &sgx_ick),
	CLK(NULL, "cpefuse_fck", &cpefuse_fck),
	CLK(NULL, "ts_fck", &ts_fck),
	CLK(NULL, "usbtll_fck", &usbtll_fck),
	CLK(NULL, "usbtll_ick", &usbtll_ick),
	CLK("omap_hsmmc.2", "ick", &mmchs3_ick),
	CLK(NULL, "mmchs3_ick", &mmchs3_ick),
	CLK(NULL, "mmchs3_fck", &mmchs3_fck),
	CLK(NULL, "dss1_alwon_fck", &dss1_alwon_fck_3430es2),
	CLK("omapdss_dss", "ick", &dss_ick_3430es2),
	CLK(NULL, "dss_ick", &dss_ick_3430es2),
	CLK(NULL, "usbhost_120m_fck", &usbhost_120m_fck),
	CLK(NULL, "usbhost_48m_fck", &usbhost_48m_fck),
	CLK(NULL, "usbhost_ick", &usbhost_ick),
	{ NULL },
};

static struct ti_clk_alias omap3430es1_clks[] = {
	CLK(NULL, "gfx_l3_ck", &gfx_l3_ck),
	CLK(NULL, "gfx_l3_fck", &gfx_l3_fck),
	CLK(NULL, "gfx_l3_ick", &gfx_l3_ick),
	CLK(NULL, "gfx_cg1_ck", &gfx_cg1_ck),
	CLK(NULL, "gfx_cg2_ck", &gfx_cg2_ck),
	CLK(NULL, "d2d_26m_fck", &d2d_26m_fck),
	CLK(NULL, "fshostusb_fck", &fshostusb_fck),
	CLK(NULL, "ssi_ssr_fck", &ssi_ssr_fck_3430es1),
	CLK(NULL, "ssi_sst_fck", &ssi_sst_fck_3430es1),
	CLK("musb-omap2430", "ick", &hsotgusb_ick_3430es1),
	CLK(NULL, "hsotgusb_ick", &hsotgusb_ick_3430es1),
	CLK(NULL, "fac_ick", &fac_ick),
	CLK(NULL, "ssi_ick", &ssi_ick_3430es1),
	CLK(NULL, "usb_l4_ick", &usb_l4_ick),
	CLK(NULL, "dss1_alwon_fck", &dss1_alwon_fck_3430es1),
	CLK("omapdss_dss", "ick", &dss_ick_3430es1),
	CLK(NULL, "dss_ick", &dss_ick_3430es1),
	{ NULL },
};

static struct ti_clk_alias omap36xx_clks[] = {
	CLK(NULL, "uart4_fck", &uart4_fck),
	{ NULL },
};

static struct ti_clk_alias am35xx_clks[] = {
	CLK(NULL, "ipss_ick", &ipss_ick),
	CLK(NULL, "rmii_ck", &rmii_ck),
	CLK(NULL, "pclk_ck", &pclk_ck),
	CLK(NULL, "emac_ick", &emac_ick),
	CLK(NULL, "emac_fck", &emac_fck),
	CLK("davinci_emac.0", NULL, &emac_ick),
	CLK("davinci_mdio.0", NULL, &emac_fck),
	CLK("vpfe-capture", "master", &vpfe_ick),
	CLK("vpfe-capture", "slave", &vpfe_fck),
	CLK(NULL, "hsotgusb_ick", &hsotgusb_ick_am35xx),
	CLK(NULL, "hsotgusb_fck", &hsotgusb_fck_am35xx),
	CLK(NULL, "hecc_ck", &hecc_ck),
	CLK(NULL, "uart4_ick", &uart4_ick_am35xx),
	CLK(NULL, "uart4_fck", &uart4_fck_am35xx),
	{ NULL },
};

static struct ti_clk *omap36xx_clk_patches[] = {
	&dpll4_m3x2_ck_omap36xx,
	&dpll3_m3x2_ck_omap36xx,
	&dpll4_m6x2_ck_omap36xx,
	&dpll4_m2x2_ck_omap36xx,
	&dpll4_m5x2_ck_omap36xx,
	&dpll4_ck_omap36xx,
	NULL,
};

static const char *enable_init_clks[] = {
	"sdrc_ick",
	"gpmc_fck",
	"omapctrl_ick",
};

static void __init omap3_clk_legacy_common_init(void)
{
	omap2_clk_disable_autoidle_all();

	omap2_clk_enable_init_clocks(enable_init_clks,
				     ARRAY_SIZE(enable_init_clks));

	pr_info("Clocking rate (Crystal/Core/MPU): %ld.%01ld/%ld/%ld MHz\n",
		(clk_get_rate(osc_sys_ck.clk) / 1000000),
		(clk_get_rate(osc_sys_ck.clk) / 100000) % 10,
		(clk_get_rate(core_ck.clk) / 1000000),
		(clk_get_rate(arm_fck.clk) / 1000000));
}

int __init omap3430es1_clk_legacy_init(void)
{
	int r;

	r = ti_clk_register_legacy_clks(omap3430es1_clks);
	r |= ti_clk_register_legacy_clks(omap34xx_omap36xx_clks);
	r |= ti_clk_register_legacy_clks(omap3xxx_clks);

	omap3_clk_legacy_common_init();

	return r;
}

int __init omap3430_clk_legacy_init(void)
{
	int r;

	r = ti_clk_register_legacy_clks(omap34xx_omap36xx_clks);
	r |= ti_clk_register_legacy_clks(omap36xx_omap3430es2plus_clks);
	r |= ti_clk_register_legacy_clks(omap36xx_am35xx_omap3430es2plus_clks);
	r |= ti_clk_register_legacy_clks(omap3xxx_clks);

	omap3_clk_legacy_common_init();
	omap3_clk_lock_dpll5();

	return r;
}

int __init omap36xx_clk_legacy_init(void)
{
	int r;

	ti_clk_patch_legacy_clks(omap36xx_clk_patches);
	r = ti_clk_register_legacy_clks(omap36xx_clks);
	r |= ti_clk_register_legacy_clks(omap36xx_omap3430es2plus_clks);
	r |= ti_clk_register_legacy_clks(omap34xx_omap36xx_clks);
	r |= ti_clk_register_legacy_clks(omap36xx_am35xx_omap3430es2plus_clks);
	r |= ti_clk_register_legacy_clks(omap3xxx_clks);

	omap3_clk_legacy_common_init();
	omap3_clk_lock_dpll5();

	return r;
}

int __init am35xx_clk_legacy_init(void)
{
	int r;

	r = ti_clk_register_legacy_clks(am35xx_clks);
	r |= ti_clk_register_legacy_clks(omap36xx_am35xx_omap3430es2plus_clks);
	r |= ti_clk_register_legacy_clks(omap3xxx_clks);

	omap3_clk_legacy_common_init();
	omap3_clk_lock_dpll5();

	return r;
}
