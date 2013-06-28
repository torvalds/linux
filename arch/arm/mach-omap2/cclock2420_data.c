/*
 * OMAP2420 clock data
 *
 * Copyright (C) 2005-2012 Texas Instruments, Inc.
 * Copyright (C) 2004-2011 Nokia Corporation
 *
 * Contacts:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Paul Walmsley
 * Updated to COMMON clk format by Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-private.h>
#include <linux/list.h>

#include "soc.h"
#include "iomap.h"
#include "clock.h"
#include "clock2xxx.h"
#include "opp2xxx.h"
#include "cm2xxx.h"
#include "prm2xxx.h"
#include "prm-regbits-24xx.h"
#include "cm-regbits-24xx.h"
#include "sdrc.h"
#include "control.h"

#define OMAP_CM_REGADDR                 OMAP2420_CM_REGADDR

/*
 * 2420 clock tree.
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

DEFINE_CLK_FIXED_RATE(alt_ck, CLK_IS_ROOT, 54000000, 0x0);

DEFINE_CLK_FIXED_RATE(func_32k_ck, CLK_IS_ROOT, 32768, 0x0);

DEFINE_CLK_FIXED_RATE(mcbsp_clks, CLK_IS_ROOT, 0x0, 0x0);

static struct clk osc_ck;

static const struct clk_ops osc_ck_ops = {
	.recalc_rate	= &omap2_osc_clk_recalc,
};

static struct clk_hw_omap osc_ck_hw = {
	.hw = {
		.clk = &osc_ck,
	},
};

static struct clk osc_ck = {
	.name	= "osc_ck",
	.ops	= &osc_ck_ops,
	.hw	= &osc_ck_hw.hw,
	.flags	= CLK_IS_ROOT,
};

DEFINE_CLK_FIXED_RATE(secure_32k_ck, CLK_IS_ROOT, 32768, 0x0);

static struct clk sys_ck;

static const char *sys_ck_parent_names[] = {
	"osc_ck",
};

static const struct clk_ops sys_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.recalc_rate	= &omap2xxx_sys_clk_recalc,
};

DEFINE_STRUCT_CLK_HW_OMAP(sys_ck, "wkup_clkdm");
DEFINE_STRUCT_CLK(sys_ck, sys_ck_parent_names, sys_ck_ops);

static struct dpll_data dpll_dd = {
	.mult_div1_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.mult_mask	= OMAP24XX_DPLL_MULT_MASK,
	.div1_mask	= OMAP24XX_DPLL_DIV_MASK,
	.clk_bypass	= &sys_ck,
	.clk_ref	= &sys_ck,
	.control_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_mask	= OMAP24XX_EN_DPLL_MASK,
	.max_multiplier	= 1023,
	.min_divider	= 1,
	.max_divider	= 16,
};

static struct clk dpll_ck;

static const char *dpll_ck_parent_names[] = {
	"sys_ck",
};

static const struct clk_ops dpll_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap2_dpllcore_recalc,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap2_reprogram_dpllcore,
};

static struct clk_hw_omap dpll_ck_hw = {
	.hw = {
		.clk = &dpll_ck,
	},
	.ops		= &clkhwops_omap2xxx_dpll,
	.dpll_data	= &dpll_dd,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(dpll_ck, dpll_ck_parent_names, dpll_ck_ops);

static struct clk core_ck;

static const char *core_ck_parent_names[] = {
	"dpll_ck",
};

static const struct clk_ops core_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
};

DEFINE_STRUCT_CLK_HW_OMAP(core_ck, "wkup_clkdm");
DEFINE_STRUCT_CLK(core_ck, core_ck_parent_names, core_ck_ops);

DEFINE_CLK_DIVIDER(core_l3_ck, "core_ck", &core_ck, 0x0,
		   OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
		   OMAP24XX_CLKSEL_L3_SHIFT, OMAP24XX_CLKSEL_L3_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

DEFINE_CLK_DIVIDER(l4_ck, "core_l3_ck", &core_l3_ck, 0x0,
		   OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
		   OMAP24XX_CLKSEL_L4_SHIFT, OMAP24XX_CLKSEL_L4_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk aes_ick;

static const char *aes_ick_parent_names[] = {
	"l4_ck",
};

static const struct clk_ops aes_ick_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
};

static struct clk_hw_omap aes_ick_hw = {
	.hw = {
		.clk = &aes_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_AES_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(aes_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk apll54_ck;

static const struct clk_ops apll54_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_clk_apll54_enable,
	.disable	= &omap2_clk_apll54_disable,
	.recalc_rate	= &omap2_clk_apll54_recalc,
};

static struct clk_hw_omap apll54_ck_hw = {
	.hw = {
		.clk = &apll54_ck,
	},
	.ops		= &clkhwops_apll54,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP24XX_EN_54M_PLL_SHIFT,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(apll54_ck, dpll_ck_parent_names, apll54_ck_ops);

static struct clk apll96_ck;

static const struct clk_ops apll96_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_clk_apll96_enable,
	.disable	= &omap2_clk_apll96_disable,
	.recalc_rate	= &omap2_clk_apll96_recalc,
};

static struct clk_hw_omap apll96_ck_hw = {
	.hw = {
		.clk = &apll96_ck,
	},
	.ops		= &clkhwops_apll96,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP24XX_EN_96M_PLL_SHIFT,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(apll96_ck, dpll_ck_parent_names, apll96_ck_ops);

static struct clk func_96m_ck;

static const char *func_96m_ck_parent_names[] = {
	"apll96_ck",
};

DEFINE_STRUCT_CLK_HW_OMAP(func_96m_ck, "wkup_clkdm");
DEFINE_STRUCT_CLK(func_96m_ck, func_96m_ck_parent_names, core_ck_ops);

static struct clk cam_fck;

static const char *cam_fck_parent_names[] = {
	"func_96m_ck",
};

static struct clk_hw_omap cam_fck_hw = {
	.hw = {
		.clk = &cam_fck,
	},
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_CAM_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(cam_fck, cam_fck_parent_names, aes_ick_ops);

static struct clk cam_ick;

static struct clk_hw_omap cam_ick_hw = {
	.hw = {
		.clk = &cam_ick,
	},
	.ops		= &clkhwops_iclk,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_CAM_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(cam_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk des_ick;

static struct clk_hw_omap des_ick_hw = {
	.hw = {
		.clk = &des_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_DES_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(des_ick, aes_ick_parent_names, aes_ick_ops);

static const struct clksel_rate dsp_fck_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 3, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 6, .val = 6, .flags = RATE_IN_242X },
	{ .div = 8, .val = 8, .flags = RATE_IN_242X },
	{ .div = 12, .val = 12, .flags = RATE_IN_242X },
	{ .div = 0 }
};

static const struct clksel dsp_fck_clksel[] = {
	{ .parent = &core_ck, .rates = dsp_fck_core_rates },
	{ .parent = NULL },
};

static const char *dsp_fck_parent_names[] = {
	"core_ck",
};

static const struct clk_ops dsp_fck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
	.recalc_rate	= &omap2_clksel_recalc,
	.set_rate	= &omap2_clksel_set_rate,
	.round_rate	= &omap2_clksel_round_rate,
};

DEFINE_CLK_OMAP_MUX_GATE(dsp_fck, "dsp_clkdm", dsp_fck_clksel,
			 OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_CLKSEL),
			 OMAP24XX_CLKSEL_DSP_MASK,
			 OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_FCLKEN),
			 OMAP24XX_CM_FCLKEN_DSP_EN_DSP_SHIFT, &clkhwops_wait,
			 dsp_fck_parent_names, dsp_fck_ops);

static const struct clksel dsp_ick_clksel[] = {
	{ .parent = &dsp_fck, .rates = dsp_ick_rates },
	{ .parent = NULL },
};

static const char *dsp_ick_parent_names[] = {
	"dsp_fck",
};

DEFINE_CLK_OMAP_MUX_GATE(dsp_ick, "dsp_clkdm", dsp_ick_clksel,
			 OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_CLKSEL),
			 OMAP24XX_CLKSEL_DSP_IF_MASK,
			 OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_ICLKEN),
			 OMAP2420_EN_DSP_IPI_SHIFT, &clkhwops_iclk_wait,
			 dsp_ick_parent_names, dsp_fck_ops);

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
	{ .parent = &sys_ck, .rates = dss1_fck_sys_rates },
	{ .parent = &core_ck, .rates = dss1_fck_core_rates },
	{ .parent = NULL },
};

static const char *dss1_fck_parent_names[] = {
	"sys_ck", "core_ck",
};

static struct clk dss1_fck;

static const struct clk_ops dss1_fck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
	.recalc_rate	= &omap2_clksel_recalc,
	.get_parent	= &omap2_clksel_find_parent_index,
	.set_parent	= &omap2_clksel_set_parent,
};

DEFINE_CLK_OMAP_MUX_GATE(dss1_fck, "dss_clkdm", dss1_fck_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
			 OMAP24XX_CLKSEL_DSS1_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_DSS1_SHIFT, NULL,
			 dss1_fck_parent_names, dss1_fck_ops);

static const struct clksel_rate dss2_fck_sys_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel_rate dss2_fck_48m_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel_rate func_48m_apll96_rates[] = {
	{ .div = 2, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel_rate func_48m_alt_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel func_48m_clksel[] = {
	{ .parent = &apll96_ck, .rates = func_48m_apll96_rates },
	{ .parent = &alt_ck, .rates = func_48m_alt_rates },
	{ .parent = NULL },
};

static const char *func_48m_ck_parent_names[] = {
	"apll96_ck", "alt_ck",
};

static struct clk func_48m_ck;

static const struct clk_ops func_48m_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.recalc_rate	= &omap2_clksel_recalc,
	.set_rate	= &omap2_clksel_set_rate,
	.round_rate	= &omap2_clksel_round_rate,
	.get_parent	= &omap2_clksel_find_parent_index,
	.set_parent	= &omap2_clksel_set_parent,
};

static struct clk_hw_omap func_48m_ck_hw = {
	.hw = {
		.clk = &func_48m_ck,
	},
	.clksel		= func_48m_clksel,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP24XX_48M_SOURCE_MASK,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(func_48m_ck, func_48m_ck_parent_names, func_48m_ck_ops);

static const struct clksel dss2_fck_clksel[] = {
	{ .parent = &sys_ck, .rates = dss2_fck_sys_rates },
	{ .parent = &func_48m_ck, .rates = dss2_fck_48m_rates },
	{ .parent = NULL },
};

static const char *dss2_fck_parent_names[] = {
	"sys_ck", "func_48m_ck",
};

DEFINE_CLK_OMAP_MUX_GATE(dss2_fck, "dss_clkdm", dss2_fck_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
			 OMAP24XX_CLKSEL_DSS2_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_DSS2_SHIFT, NULL,
			 dss2_fck_parent_names, dss1_fck_ops);

static const char *func_54m_ck_parent_names[] = {
	"apll54_ck", "alt_ck",
};

DEFINE_CLK_MUX(func_54m_ck, func_54m_ck_parent_names, NULL, 0x0,
	       OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	       OMAP24XX_54M_SOURCE_SHIFT, OMAP24XX_54M_SOURCE_WIDTH,
	       0x0, NULL);

static struct clk dss_54m_fck;

static const char *dss_54m_fck_parent_names[] = {
	"func_54m_ck",
};

static struct clk_hw_omap dss_54m_fck_hw = {
	.hw = {
		.clk = &dss_54m_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_TV_SHIFT,
	.clkdm_name	= "dss_clkdm",
};

DEFINE_STRUCT_CLK(dss_54m_fck, dss_54m_fck_parent_names, aes_ick_ops);

static struct clk dss_ick;

static struct clk_hw_omap dss_ick_hw = {
	.hw = {
		.clk = &dss_ick,
	},
	.ops		= &clkhwops_iclk,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_DSS1_SHIFT,
	.clkdm_name	= "dss_clkdm",
};

DEFINE_STRUCT_CLK(dss_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk eac_fck;

static struct clk_hw_omap eac_fck_hw = {
	.hw = {
		.clk = &eac_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_EAC_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(eac_fck, cam_fck_parent_names, aes_ick_ops);

static struct clk eac_ick;

static struct clk_hw_omap eac_ick_hw = {
	.hw = {
		.clk = &eac_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_EAC_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(eac_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk emul_ck;

static struct clk_hw_omap emul_ck_hw = {
	.hw = {
		.clk = &emul_ck,
	},
	.enable_reg	= OMAP2420_PRCM_CLKEMUL_CTRL,
	.enable_bit	= OMAP24XX_EMULATION_EN_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(emul_ck, dss_54m_fck_parent_names, aes_ick_ops);

DEFINE_CLK_FIXED_FACTOR(func_12m_ck, "func_48m_ck", &func_48m_ck, 0x0, 1, 4);

static struct clk fac_fck;

static const char *fac_fck_parent_names[] = {
	"func_12m_ck",
};

static struct clk_hw_omap fac_fck_hw = {
	.hw = {
		.clk = &fac_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_FAC_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(fac_fck, fac_fck_parent_names, aes_ick_ops);

static struct clk fac_ick;

static struct clk_hw_omap fac_ick_hw = {
	.hw = {
		.clk = &fac_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_FAC_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(fac_ick, aes_ick_parent_names, aes_ick_ops);

static const struct clksel gfx_fck_clksel[] = {
	{ .parent = &core_l3_ck, .rates = gfx_l3_rates },
	{ .parent = NULL },
};

static const char *gfx_2d_fck_parent_names[] = {
	"core_l3_ck",
};

DEFINE_CLK_OMAP_MUX_GATE(gfx_2d_fck, "gfx_clkdm", gfx_fck_clksel,
			 OMAP_CM_REGADDR(GFX_MOD, CM_CLKSEL),
			 OMAP_CLKSEL_GFX_MASK,
			 OMAP_CM_REGADDR(GFX_MOD, CM_FCLKEN),
			 OMAP24XX_EN_2D_SHIFT, &clkhwops_wait,
			 gfx_2d_fck_parent_names, dsp_fck_ops);

DEFINE_CLK_OMAP_MUX_GATE(gfx_3d_fck, "gfx_clkdm", gfx_fck_clksel,
			 OMAP_CM_REGADDR(GFX_MOD, CM_CLKSEL),
			 OMAP_CLKSEL_GFX_MASK,
			 OMAP_CM_REGADDR(GFX_MOD, CM_FCLKEN),
			 OMAP24XX_EN_3D_SHIFT, &clkhwops_wait,
			 gfx_2d_fck_parent_names, dsp_fck_ops);

static struct clk gfx_ick;

static const char *gfx_ick_parent_names[] = {
	"core_l3_ck",
};

static struct clk_hw_omap gfx_ick_hw = {
	.hw = {
		.clk = &gfx_ick,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_ICLKEN),
	.enable_bit	= OMAP_EN_GFX_SHIFT,
	.clkdm_name	= "gfx_clkdm",
};

DEFINE_STRUCT_CLK(gfx_ick, gfx_ick_parent_names, aes_ick_ops);

static struct clk gpios_fck;

static const char *gpios_fck_parent_names[] = {
	"func_32k_ck",
};

static struct clk_hw_omap gpios_fck_hw = {
	.hw = {
		.clk = &gpios_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_GPIOS_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(gpios_fck, gpios_fck_parent_names, aes_ick_ops);

static struct clk gpios_ick;

static const char *gpios_ick_parent_names[] = {
	"sys_ck",
};

static struct clk_hw_omap gpios_ick_hw = {
	.hw = {
		.clk = &gpios_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_GPIOS_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(gpios_ick, gpios_ick_parent_names, aes_ick_ops);

static struct clk gpmc_fck;

static struct clk_hw_omap gpmc_fck_hw = {
	.hw = {
		.clk = &gpmc_fck,
	},
	.ops		= &clkhwops_iclk,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP24XX_AUTO_GPMC_SHIFT,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(gpmc_fck, gfx_ick_parent_names, core_ck_ops);

static const struct clksel_rate gpt_alt_rates[] = {
	{ .div = 1, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel omap24xx_gpt_clksel[] = {
	{ .parent = &func_32k_ck, .rates = gpt_32k_rates },
	{ .parent = &sys_ck, .rates = gpt_sys_rates },
	{ .parent = &alt_ck, .rates = gpt_alt_rates },
	{ .parent = NULL },
};

static const char *gpt10_fck_parent_names[] = {
	"func_32k_ck", "sys_ck", "alt_ck",
};

DEFINE_CLK_OMAP_MUX_GATE(gpt10_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT10_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT10_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt10_ick;

static struct clk_hw_omap gpt10_ick_hw = {
	.hw = {
		.clk = &gpt10_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT10_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt10_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt11_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT11_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT11_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt11_ick;

static struct clk_hw_omap gpt11_ick_hw = {
	.hw = {
		.clk = &gpt11_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT11_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt11_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt12_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT12_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT12_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt12_ick;

static struct clk_hw_omap gpt12_ick_hw = {
	.hw = {
		.clk = &gpt12_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT12_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt12_ick, aes_ick_parent_names, aes_ick_ops);

static const struct clk_ops gpt1_fck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
	.recalc_rate	= &omap2_clksel_recalc,
	.set_rate	= &omap2_clksel_set_rate,
	.round_rate	= &omap2_clksel_round_rate,
	.get_parent	= &omap2_clksel_find_parent_index,
	.set_parent	= &omap2_clksel_set_parent,
};

DEFINE_CLK_OMAP_MUX_GATE(gpt1_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(WKUP_MOD, CM_CLKSEL1),
			 OMAP24XX_CLKSEL_GPT1_MASK,
			 OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
			 OMAP24XX_EN_GPT1_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, gpt1_fck_ops);

static struct clk gpt1_ick;

static struct clk_hw_omap gpt1_ick_hw = {
	.hw = {
		.clk = &gpt1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_GPT1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(gpt1_ick, gpios_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt2_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT2_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT2_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt2_ick;

static struct clk_hw_omap gpt2_ick_hw = {
	.hw = {
		.clk = &gpt2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt2_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt3_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT3_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT3_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt3_ick;

static struct clk_hw_omap gpt3_ick_hw = {
	.hw = {
		.clk = &gpt3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt3_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt4_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT4_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT4_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt4_ick;

static struct clk_hw_omap gpt4_ick_hw = {
	.hw = {
		.clk = &gpt4_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT4_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt4_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt5_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT5_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT5_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt5_ick;

static struct clk_hw_omap gpt5_ick_hw = {
	.hw = {
		.clk = &gpt5_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT5_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt5_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt6_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT6_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT6_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt6_ick;

static struct clk_hw_omap gpt6_ick_hw = {
	.hw = {
		.clk = &gpt6_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT6_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt6_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt7_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT7_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT7_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt7_ick;

static struct clk_hw_omap gpt7_ick_hw = {
	.hw = {
		.clk = &gpt7_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT7_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt7_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt8_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT8_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT8_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt8_ick;

static struct clk_hw_omap gpt8_ick_hw = {
	.hw = {
		.clk = &gpt8_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT8_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt8_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt9_fck, "core_l4_clkdm", omap24xx_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL2),
			 OMAP24XX_CLKSEL_GPT9_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_GPT9_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, dss1_fck_ops);

static struct clk gpt9_ick;

static struct clk_hw_omap gpt9_ick_hw = {
	.hw = {
		.clk = &gpt9_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_GPT9_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt9_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk hdq_fck;

static struct clk_hw_omap hdq_fck_hw = {
	.hw = {
		.clk = &hdq_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_HDQ_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(hdq_fck, fac_fck_parent_names, aes_ick_ops);

static struct clk hdq_ick;

static struct clk_hw_omap hdq_ick_hw = {
	.hw = {
		.clk = &hdq_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_HDQ_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(hdq_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk i2c1_fck;

static struct clk_hw_omap i2c1_fck_hw = {
	.hw = {
		.clk = &i2c1_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_I2C1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(i2c1_fck, fac_fck_parent_names, aes_ick_ops);

static struct clk i2c1_ick;

static struct clk_hw_omap i2c1_ick_hw = {
	.hw = {
		.clk = &i2c1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_I2C1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(i2c1_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk i2c2_fck;

static struct clk_hw_omap i2c2_fck_hw = {
	.hw = {
		.clk = &i2c2_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_I2C2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(i2c2_fck, fac_fck_parent_names, aes_ick_ops);

static struct clk i2c2_ick;

static struct clk_hw_omap i2c2_ick_hw = {
	.hw = {
		.clk = &i2c2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_I2C2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(i2c2_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(iva1_ifck, "iva1_clkdm", dsp_fck_clksel,
			 OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_CLKSEL),
			 OMAP2420_CLKSEL_IVA_MASK,
			 OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_FCLKEN),
			 OMAP2420_EN_IVA_COP_SHIFT, &clkhwops_wait,
			 dsp_fck_parent_names, dsp_fck_ops);

static struct clk iva1_mpu_int_ifck;

static const char *iva1_mpu_int_ifck_parent_names[] = {
	"iva1_ifck",
};

static const struct clk_ops iva1_mpu_int_ifck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
	.recalc_rate	= &omap_fixed_divisor_recalc,
};

static struct clk_hw_omap iva1_mpu_int_ifck_hw = {
	.hw = {
		.clk = &iva1_mpu_int_ifck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP24XX_DSP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP2420_EN_IVA_MPU_SHIFT,
	.clkdm_name	= "iva1_clkdm",
	.fixed_div	= 2,
};

DEFINE_STRUCT_CLK(iva1_mpu_int_ifck, iva1_mpu_int_ifck_parent_names,
		  iva1_mpu_int_ifck_ops);

static struct clk mailboxes_ick;

static struct clk_hw_omap mailboxes_ick_hw = {
	.hw = {
		.clk = &mailboxes_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MAILBOXES_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mailboxes_ick, aes_ick_parent_names, aes_ick_ops);

static const struct clksel_rate common_mcbsp_96m_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel_rate common_mcbsp_mcbsp_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 0 }
};

static const struct clksel mcbsp_fck_clksel[] = {
	{ .parent = &func_96m_ck, .rates = common_mcbsp_96m_rates },
	{ .parent = &mcbsp_clks, .rates = common_mcbsp_mcbsp_rates },
	{ .parent = NULL },
};

static const char *mcbsp1_fck_parent_names[] = {
	"func_96m_ck", "mcbsp_clks",
};

DEFINE_CLK_OMAP_MUX_GATE(mcbsp1_fck, "core_l4_clkdm", mcbsp_fck_clksel,
			 OMAP242X_CTRL_REGADDR(OMAP2_CONTROL_DEVCONF0),
			 OMAP2_MCBSP1_CLKS_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_MCBSP1_SHIFT, &clkhwops_wait,
			 mcbsp1_fck_parent_names, dss1_fck_ops);

static struct clk mcbsp1_ick;

static struct clk_hw_omap mcbsp1_ick_hw = {
	.hw = {
		.clk = &mcbsp1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCBSP1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcbsp1_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(mcbsp2_fck, "core_l4_clkdm", mcbsp_fck_clksel,
			 OMAP242X_CTRL_REGADDR(OMAP2_CONTROL_DEVCONF0),
			 OMAP2_MCBSP2_CLKS_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP24XX_EN_MCBSP2_SHIFT, &clkhwops_wait,
			 mcbsp1_fck_parent_names, dss1_fck_ops);

static struct clk mcbsp2_ick;

static struct clk_hw_omap mcbsp2_ick_hw = {
	.hw = {
		.clk = &mcbsp2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCBSP2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcbsp2_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk mcspi1_fck;

static const char *mcspi1_fck_parent_names[] = {
	"func_48m_ck",
};

static struct clk_hw_omap mcspi1_fck_hw = {
	.hw = {
		.clk = &mcspi1_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi1_fck, mcspi1_fck_parent_names, aes_ick_ops);

static struct clk mcspi1_ick;

static struct clk_hw_omap mcspi1_ick_hw = {
	.hw = {
		.clk = &mcspi1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi1_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk mcspi2_fck;

static struct clk_hw_omap mcspi2_fck_hw = {
	.hw = {
		.clk = &mcspi2_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi2_fck, mcspi1_fck_parent_names, aes_ick_ops);

static struct clk mcspi2_ick;

static struct clk_hw_omap mcspi2_ick_hw = {
	.hw = {
		.clk = &mcspi2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MCSPI2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi2_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk mmc_fck;

static struct clk_hw_omap mmc_fck_hw = {
	.hw = {
		.clk = &mmc_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_MMC_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mmc_fck, cam_fck_parent_names, aes_ick_ops);

static struct clk mmc_ick;

static struct clk_hw_omap mmc_ick_hw = {
	.hw = {
		.clk = &mmc_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_MMC_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mmc_ick, aes_ick_parent_names, aes_ick_ops);

DEFINE_CLK_DIVIDER(mpu_ck, "core_ck", &core_ck, 0x0,
		   OMAP_CM_REGADDR(MPU_MOD, CM_CLKSEL),
		   OMAP24XX_CLKSEL_MPU_SHIFT, OMAP24XX_CLKSEL_MPU_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk mpu_wdt_fck;

static struct clk_hw_omap mpu_wdt_fck_hw = {
	.hw = {
		.clk = &mpu_wdt_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP24XX_EN_MPU_WDT_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(mpu_wdt_fck, gpios_fck_parent_names, aes_ick_ops);

static struct clk mpu_wdt_ick;

static struct clk_hw_omap mpu_wdt_ick_hw = {
	.hw = {
		.clk = &mpu_wdt_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_MPU_WDT_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(mpu_wdt_ick, gpios_ick_parent_names, aes_ick_ops);

static struct clk mspro_fck;

static struct clk_hw_omap mspro_fck_hw = {
	.hw = {
		.clk = &mspro_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_MSPRO_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mspro_fck, cam_fck_parent_names, aes_ick_ops);

static struct clk mspro_ick;

static struct clk_hw_omap mspro_ick_hw = {
	.hw = {
		.clk = &mspro_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_MSPRO_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mspro_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk omapctrl_ick;

static struct clk_hw_omap omapctrl_ick_hw = {
	.hw = {
		.clk = &omapctrl_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_OMAPCTRL_SHIFT,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(omapctrl_ick, gpios_ick_parent_names, aes_ick_ops);

static struct clk pka_ick;

static struct clk_hw_omap pka_ick_hw = {
	.hw = {
		.clk = &pka_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_PKA_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(pka_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk rng_ick;

static struct clk_hw_omap rng_ick_hw = {
	.hw = {
		.clk = &rng_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_RNG_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(rng_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk sdma_fck;

DEFINE_STRUCT_CLK_HW_OMAP(sdma_fck, "core_l3_clkdm");
DEFINE_STRUCT_CLK(sdma_fck, gfx_ick_parent_names, core_ck_ops);

static struct clk sdma_ick;

static struct clk_hw_omap sdma_ick_hw = {
	.hw = {
		.clk = &sdma_ick,
	},
	.ops		= &clkhwops_iclk,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP24XX_AUTO_SDMA_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(sdma_ick, gfx_ick_parent_names, core_ck_ops);

static struct clk sdrc_ick;

static struct clk_hw_omap sdrc_ick_hw = {
	.hw = {
		.clk = &sdrc_ick,
	},
	.ops		= &clkhwops_iclk,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP24XX_AUTO_SDRC_SHIFT,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(sdrc_ick, gfx_ick_parent_names, core_ck_ops);

static struct clk sha_ick;

static struct clk_hw_omap sha_ick_hw = {
	.hw = {
		.clk = &sha_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_ICLKEN4),
	.enable_bit	= OMAP24XX_EN_SHA_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(sha_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk ssi_l4_ick;

static struct clk_hw_omap ssi_l4_ick_hw = {
	.hw = {
		.clk = &ssi_l4_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP24XX_EN_SSI_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(ssi_l4_ick, aes_ick_parent_names, aes_ick_ops);

static const struct clksel_rate ssi_ssr_sst_fck_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX },
	{ .div = 3, .val = 3, .flags = RATE_IN_24XX },
	{ .div = 4, .val = 4, .flags = RATE_IN_24XX },
	{ .div = 6, .val = 6, .flags = RATE_IN_242X },
	{ .div = 8, .val = 8, .flags = RATE_IN_242X },
	{ .div = 0 }
};

static const struct clksel ssi_ssr_sst_fck_clksel[] = {
	{ .parent = &core_ck, .rates = ssi_ssr_sst_fck_core_rates },
	{ .parent = NULL },
};

static const char *ssi_ssr_sst_fck_parent_names[] = {
	"core_ck",
};

DEFINE_CLK_OMAP_MUX_GATE(ssi_ssr_sst_fck, "core_l3_clkdm",
			 ssi_ssr_sst_fck_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
			 OMAP24XX_CLKSEL_SSI_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
			 OMAP24XX_EN_SSI_SHIFT, &clkhwops_wait,
			 ssi_ssr_sst_fck_parent_names, dsp_fck_ops);

static struct clk sync_32k_ick;

static struct clk_hw_omap sync_32k_ick_hw = {
	.hw = {
		.clk = &sync_32k_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_32KSYNC_SHIFT,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(sync_32k_ick, gpios_ick_parent_names, aes_ick_ops);

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
	{ .parent = &core_ck, .rates = common_clkout_src_core_rates },
	{ .parent = &sys_ck, .rates = common_clkout_src_sys_rates },
	{ .parent = &func_96m_ck, .rates = common_clkout_src_96m_rates },
	{ .parent = &func_54m_ck, .rates = common_clkout_src_54m_rates },
	{ .parent = NULL },
};

static const char *sys_clkout_src_parent_names[] = {
	"core_ck", "sys_ck", "func_96m_ck", "func_54m_ck",
};

DEFINE_CLK_OMAP_MUX_GATE(sys_clkout_src, "wkup_clkdm", common_clkout_src_clksel,
			 OMAP2420_PRCM_CLKOUT_CTRL, OMAP24XX_CLKOUT_SOURCE_MASK,
			 OMAP2420_PRCM_CLKOUT_CTRL, OMAP24XX_CLKOUT_EN_SHIFT,
			 NULL, sys_clkout_src_parent_names, gpt1_fck_ops);

DEFINE_CLK_DIVIDER(sys_clkout, "sys_clkout_src", &sys_clkout_src, 0x0,
		   OMAP2420_PRCM_CLKOUT_CTRL, OMAP24XX_CLKOUT_DIV_SHIFT,
		   OMAP24XX_CLKOUT_DIV_WIDTH, CLK_DIVIDER_POWER_OF_TWO, NULL);

DEFINE_CLK_OMAP_MUX_GATE(sys_clkout2_src, "wkup_clkdm",
			 common_clkout_src_clksel, OMAP2420_PRCM_CLKOUT_CTRL,
			 OMAP2420_CLKOUT2_SOURCE_MASK,
			 OMAP2420_PRCM_CLKOUT_CTRL, OMAP2420_CLKOUT2_EN_SHIFT,
			 NULL, sys_clkout_src_parent_names, gpt1_fck_ops);

DEFINE_CLK_DIVIDER(sys_clkout2, "sys_clkout2_src", &sys_clkout2_src, 0x0,
		   OMAP2420_PRCM_CLKOUT_CTRL, OMAP2420_CLKOUT2_DIV_SHIFT,
		   OMAP2420_CLKOUT2_DIV_WIDTH, CLK_DIVIDER_POWER_OF_TWO, NULL);

static struct clk uart1_fck;

static struct clk_hw_omap uart1_fck_hw = {
	.hw = {
		.clk = &uart1_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_UART1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart1_fck, mcspi1_fck_parent_names, aes_ick_ops);

static struct clk uart1_ick;

static struct clk_hw_omap uart1_ick_hw = {
	.hw = {
		.clk = &uart1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_UART1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart1_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk uart2_fck;

static struct clk_hw_omap uart2_fck_hw = {
	.hw = {
		.clk = &uart2_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_UART2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart2_fck, mcspi1_fck_parent_names, aes_ick_ops);

static struct clk uart2_ick;

static struct clk_hw_omap uart2_ick_hw = {
	.hw = {
		.clk = &uart2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_UART2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart2_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk uart3_fck;

static struct clk_hw_omap uart3_fck_hw = {
	.hw = {
		.clk = &uart3_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP24XX_EN_UART3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart3_fck, mcspi1_fck_parent_names, aes_ick_ops);

static struct clk uart3_ick;

static struct clk_hw_omap uart3_ick_hw = {
	.hw = {
		.clk = &uart3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP24XX_EN_UART3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart3_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk usb_fck;

static struct clk_hw_omap usb_fck_hw = {
	.hw = {
		.clk = &usb_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP24XX_CM_FCLKEN2),
	.enable_bit	= OMAP24XX_EN_USB_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(usb_fck, mcspi1_fck_parent_names, aes_ick_ops);

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

static const char *usb_l4_ick_parent_names[] = {
	"core_l3_ck",
};

DEFINE_CLK_OMAP_MUX_GATE(usb_l4_ick, "core_l4_clkdm", usb_l4_ick_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
			 OMAP24XX_CLKSEL_USB_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
			 OMAP24XX_EN_USB_SHIFT, &clkhwops_iclk_wait,
			 usb_l4_ick_parent_names, dsp_fck_ops);

static struct clk virt_prcm_set;

static const char *virt_prcm_set_parent_names[] = {
	"mpu_ck",
};

static const struct clk_ops virt_prcm_set_ops = {
	.recalc_rate	= &omap2_table_mpu_recalc,
	.set_rate	= &omap2_select_table_rate,
	.round_rate	= &omap2_round_to_table_rate,
};

DEFINE_STRUCT_CLK_HW_OMAP(virt_prcm_set, NULL);
DEFINE_STRUCT_CLK(virt_prcm_set, virt_prcm_set_parent_names, virt_prcm_set_ops);

static const struct clksel_rate vlynq_fck_96m_rates[] = {
	{ .div = 1, .val = 0, .flags = RATE_IN_242X },
	{ .div = 0 }
};

static const struct clksel_rate vlynq_fck_core_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_242X },
	{ .div = 2, .val = 2, .flags = RATE_IN_242X },
	{ .div = 3, .val = 3, .flags = RATE_IN_242X },
	{ .div = 4, .val = 4, .flags = RATE_IN_242X },
	{ .div = 6, .val = 6, .flags = RATE_IN_242X },
	{ .div = 8, .val = 8, .flags = RATE_IN_242X },
	{ .div = 9, .val = 9, .flags = RATE_IN_242X },
	{ .div = 12, .val = 12, .flags = RATE_IN_242X },
	{ .div = 16, .val = 16, .flags = RATE_IN_242X },
	{ .div = 18, .val = 18, .flags = RATE_IN_242X },
	{ .div = 0 }
};

static const struct clksel vlynq_fck_clksel[] = {
	{ .parent = &func_96m_ck, .rates = vlynq_fck_96m_rates },
	{ .parent = &core_ck, .rates = vlynq_fck_core_rates },
	{ .parent = NULL },
};

static const char *vlynq_fck_parent_names[] = {
	"func_96m_ck", "core_ck",
};

DEFINE_CLK_OMAP_MUX_GATE(vlynq_fck, "core_l3_clkdm", vlynq_fck_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL1),
			 OMAP2420_CLKSEL_VLYNQ_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP2420_EN_VLYNQ_SHIFT, &clkhwops_wait,
			 vlynq_fck_parent_names, dss1_fck_ops);

static struct clk vlynq_ick;

static struct clk_hw_omap vlynq_ick_hw = {
	.hw = {
		.clk = &vlynq_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_VLYNQ_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(vlynq_ick, gfx_ick_parent_names, aes_ick_ops);

static struct clk wdt1_ick;

static struct clk_hw_omap wdt1_ick_hw = {
	.hw = {
		.clk = &wdt1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP24XX_EN_WDT1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(wdt1_ick, gpios_ick_parent_names, aes_ick_ops);

static struct clk wdt3_fck;

static struct clk_hw_omap wdt3_fck_hw = {
	.hw = {
		.clk = &wdt3_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP2420_EN_WDT3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(wdt3_fck, gpios_fck_parent_names, aes_ick_ops);

static struct clk wdt3_ick;

static struct clk_hw_omap wdt3_ick_hw = {
	.hw = {
		.clk = &wdt3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP2420_EN_WDT3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(wdt3_ick, aes_ick_parent_names, aes_ick_ops);

static struct clk wdt4_fck;

static struct clk_hw_omap wdt4_fck_hw = {
	.hw = {
		.clk = &wdt4_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP24XX_EN_WDT4_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(wdt4_fck, gpios_fck_parent_names, aes_ick_ops);

static struct clk wdt4_ick;

static struct clk_hw_omap wdt4_ick_hw = {
	.hw = {
		.clk = &wdt4_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP24XX_EN_WDT4_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(wdt4_ick, aes_ick_parent_names, aes_ick_ops);

/*
 * clkdev integration
 */

static struct omap_clk omap2420_clks[] = {
	/* external root sources */
	CLK(NULL,	"func_32k_ck",	&func_32k_ck),
	CLK(NULL,	"secure_32k_ck", &secure_32k_ck),
	CLK(NULL,	"osc_ck",	&osc_ck),
	CLK(NULL,	"sys_ck",	&sys_ck),
	CLK(NULL,	"alt_ck",	&alt_ck),
	CLK(NULL,	"mcbsp_clks",	&mcbsp_clks),
	/* internal analog sources */
	CLK(NULL,	"dpll_ck",	&dpll_ck),
	CLK(NULL,	"apll96_ck",	&apll96_ck),
	CLK(NULL,	"apll54_ck",	&apll54_ck),
	/* internal prcm root sources */
	CLK(NULL,	"func_54m_ck",	&func_54m_ck),
	CLK(NULL,	"core_ck",	&core_ck),
	CLK(NULL,	"func_96m_ck",	&func_96m_ck),
	CLK(NULL,	"func_48m_ck",	&func_48m_ck),
	CLK(NULL,	"func_12m_ck",	&func_12m_ck),
	CLK(NULL,	"sys_clkout_src", &sys_clkout_src),
	CLK(NULL,	"sys_clkout",	&sys_clkout),
	CLK(NULL,	"sys_clkout2_src", &sys_clkout2_src),
	CLK(NULL,	"sys_clkout2",	&sys_clkout2),
	CLK(NULL,	"emul_ck",	&emul_ck),
	/* mpu domain clocks */
	CLK(NULL,	"mpu_ck",	&mpu_ck),
	/* dsp domain clocks */
	CLK(NULL,	"dsp_fck",	&dsp_fck),
	CLK(NULL,	"dsp_ick",	&dsp_ick),
	CLK(NULL,	"iva1_ifck",	&iva1_ifck),
	CLK(NULL,	"iva1_mpu_int_ifck", &iva1_mpu_int_ifck),
	/* GFX domain clocks */
	CLK(NULL,	"gfx_3d_fck",	&gfx_3d_fck),
	CLK(NULL,	"gfx_2d_fck",	&gfx_2d_fck),
	CLK(NULL,	"gfx_ick",	&gfx_ick),
	/* DSS domain clocks */
	CLK("omapdss_dss",	"ick",		&dss_ick),
	CLK(NULL,	"dss_ick",		&dss_ick),
	CLK(NULL,	"dss1_fck",		&dss1_fck),
	CLK(NULL,	"dss2_fck",	&dss2_fck),
	CLK(NULL,	"dss_54m_fck",	&dss_54m_fck),
	/* L3 domain clocks */
	CLK(NULL,	"core_l3_ck",	&core_l3_ck),
	CLK(NULL,	"ssi_fck",	&ssi_ssr_sst_fck),
	CLK(NULL,	"usb_l4_ick",	&usb_l4_ick),
	/* L4 domain clocks */
	CLK(NULL,	"l4_ck",	&l4_ck),
	CLK(NULL,	"ssi_l4_ick",	&ssi_l4_ick),
	/* virtual meta-group clock */
	CLK(NULL,	"virt_prcm_set", &virt_prcm_set),
	/* general l4 interface ck, multi-parent functional clk */
	CLK(NULL,	"gpt1_ick",	&gpt1_ick),
	CLK(NULL,	"gpt1_fck",	&gpt1_fck),
	CLK(NULL,	"gpt2_ick",	&gpt2_ick),
	CLK(NULL,	"gpt2_fck",	&gpt2_fck),
	CLK(NULL,	"gpt3_ick",	&gpt3_ick),
	CLK(NULL,	"gpt3_fck",	&gpt3_fck),
	CLK(NULL,	"gpt4_ick",	&gpt4_ick),
	CLK(NULL,	"gpt4_fck",	&gpt4_fck),
	CLK(NULL,	"gpt5_ick",	&gpt5_ick),
	CLK(NULL,	"gpt5_fck",	&gpt5_fck),
	CLK(NULL,	"gpt6_ick",	&gpt6_ick),
	CLK(NULL,	"gpt6_fck",	&gpt6_fck),
	CLK(NULL,	"gpt7_ick",	&gpt7_ick),
	CLK(NULL,	"gpt7_fck",	&gpt7_fck),
	CLK(NULL,	"gpt8_ick",	&gpt8_ick),
	CLK(NULL,	"gpt8_fck",	&gpt8_fck),
	CLK(NULL,	"gpt9_ick",	&gpt9_ick),
	CLK(NULL,	"gpt9_fck",	&gpt9_fck),
	CLK(NULL,	"gpt10_ick",	&gpt10_ick),
	CLK(NULL,	"gpt10_fck",	&gpt10_fck),
	CLK(NULL,	"gpt11_ick",	&gpt11_ick),
	CLK(NULL,	"gpt11_fck",	&gpt11_fck),
	CLK(NULL,	"gpt12_ick",	&gpt12_ick),
	CLK(NULL,	"gpt12_fck",	&gpt12_fck),
	CLK("omap-mcbsp.1", "ick",	&mcbsp1_ick),
	CLK(NULL,	"mcbsp1_ick",	&mcbsp1_ick),
	CLK(NULL,	"mcbsp1_fck",	&mcbsp1_fck),
	CLK("omap-mcbsp.2", "ick",	&mcbsp2_ick),
	CLK(NULL,	"mcbsp2_ick",	&mcbsp2_ick),
	CLK(NULL,	"mcbsp2_fck",	&mcbsp2_fck),
	CLK("omap2_mcspi.1", "ick",	&mcspi1_ick),
	CLK(NULL,	"mcspi1_ick",	&mcspi1_ick),
	CLK(NULL,	"mcspi1_fck",	&mcspi1_fck),
	CLK("omap2_mcspi.2", "ick",	&mcspi2_ick),
	CLK(NULL,	"mcspi2_ick",	&mcspi2_ick),
	CLK(NULL,	"mcspi2_fck",	&mcspi2_fck),
	CLK(NULL,	"uart1_ick",	&uart1_ick),
	CLK(NULL,	"uart1_fck",	&uart1_fck),
	CLK(NULL,	"uart2_ick",	&uart2_ick),
	CLK(NULL,	"uart2_fck",	&uart2_fck),
	CLK(NULL,	"uart3_ick",	&uart3_ick),
	CLK(NULL,	"uart3_fck",	&uart3_fck),
	CLK(NULL,	"gpios_ick",	&gpios_ick),
	CLK(NULL,	"gpios_fck",	&gpios_fck),
	CLK("omap_wdt",	"ick",		&mpu_wdt_ick),
	CLK(NULL,	"mpu_wdt_ick",		&mpu_wdt_ick),
	CLK(NULL,	"mpu_wdt_fck",	&mpu_wdt_fck),
	CLK(NULL,	"sync_32k_ick",	&sync_32k_ick),
	CLK(NULL,	"wdt1_ick",	&wdt1_ick),
	CLK(NULL,	"omapctrl_ick",	&omapctrl_ick),
	CLK("omap24xxcam", "fck",	&cam_fck),
	CLK(NULL,	"cam_fck",	&cam_fck),
	CLK("omap24xxcam", "ick",	&cam_ick),
	CLK(NULL,	"cam_ick",	&cam_ick),
	CLK(NULL,	"mailboxes_ick", &mailboxes_ick),
	CLK(NULL,	"wdt4_ick",	&wdt4_ick),
	CLK(NULL,	"wdt4_fck",	&wdt4_fck),
	CLK(NULL,	"wdt3_ick",	&wdt3_ick),
	CLK(NULL,	"wdt3_fck",	&wdt3_fck),
	CLK(NULL,	"mspro_ick",	&mspro_ick),
	CLK(NULL,	"mspro_fck",	&mspro_fck),
	CLK("mmci-omap.0", "ick",	&mmc_ick),
	CLK(NULL,	"mmc_ick",	&mmc_ick),
	CLK("mmci-omap.0", "fck",	&mmc_fck),
	CLK(NULL,	"mmc_fck",	&mmc_fck),
	CLK(NULL,	"fac_ick",	&fac_ick),
	CLK(NULL,	"fac_fck",	&fac_fck),
	CLK(NULL,	"eac_ick",	&eac_ick),
	CLK(NULL,	"eac_fck",	&eac_fck),
	CLK("omap_hdq.0", "ick",	&hdq_ick),
	CLK(NULL,	"hdq_ick",	&hdq_ick),
	CLK("omap_hdq.0", "fck",	&hdq_fck),
	CLK(NULL,	"hdq_fck",	&hdq_fck),
	CLK("omap_i2c.1", "ick",	&i2c1_ick),
	CLK(NULL,	"i2c1_ick",	&i2c1_ick),
	CLK(NULL,	"i2c1_fck",	&i2c1_fck),
	CLK("omap_i2c.2", "ick",	&i2c2_ick),
	CLK(NULL,	"i2c2_ick",	&i2c2_ick),
	CLK(NULL,	"i2c2_fck",	&i2c2_fck),
	CLK(NULL,	"gpmc_fck",	&gpmc_fck),
	CLK(NULL,	"sdma_fck",	&sdma_fck),
	CLK(NULL,	"sdma_ick",	&sdma_ick),
	CLK(NULL,	"sdrc_ick",	&sdrc_ick),
	CLK(NULL,	"vlynq_ick",	&vlynq_ick),
	CLK(NULL,	"vlynq_fck",	&vlynq_fck),
	CLK(NULL,	"des_ick",	&des_ick),
	CLK("omap-sham",	"ick",	&sha_ick),
	CLK(NULL,	"sha_ick",	&sha_ick),
	CLK("omap_rng",	"ick",		&rng_ick),
	CLK(NULL,	"rng_ick",		&rng_ick),
	CLK("omap-aes",	"ick",	&aes_ick),
	CLK(NULL,	"aes_ick",	&aes_ick),
	CLK(NULL,	"pka_ick",	&pka_ick),
	CLK(NULL,	"usb_fck",	&usb_fck),
	CLK("musb-hdrc",	"fck",	&osc_ck),
	CLK(NULL,	"timer_32k_ck",	&func_32k_ck),
	CLK(NULL,	"timer_sys_ck",	&sys_ck),
	CLK(NULL,	"timer_ext_ck",	&alt_ck),
	CLK(NULL,	"cpufreq_ck",	&virt_prcm_set),
};


static const char *enable_init_clks[] = {
	"apll96_ck",
	"apll54_ck",
	"sync_32k_ick",
	"omapctrl_ick",
	"gpmc_fck",
	"sdrc_ick",
};

/*
 * init code
 */

int __init omap2420_clk_init(void)
{
	prcm_clksrc_ctrl = OMAP2420_PRCM_CLKSRC_CTRL;
	cpu_mask = RATE_IN_242X;
	rate_table = omap2420_rate_table;

	omap2xxx_clkt_dpllcore_init(&dpll_ck_hw.hw);

	omap2xxx_clkt_vps_check_bootloader_rates();

	omap_clocks_register(omap2420_clks, ARRAY_SIZE(omap2420_clks));

	omap2xxx_clkt_vps_late_init();

	omap2_clk_disable_autoidle_all();

	omap2_clk_enable_init_clocks(enable_init_clks,
				     ARRAY_SIZE(enable_init_clks));

	pr_info("Clocking rate (Crystal/DPLL/MPU): %ld.%01ld/%ld/%ld MHz\n",
		(clk_get_rate(&sys_ck) / 1000000),
		(clk_get_rate(&sys_ck) / 100000) % 10,
		(clk_get_rate(&dpll_ck) / 1000000),
		(clk_get_rate(&mpu_ck) / 1000000));

	return 0;
}
