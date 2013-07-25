/*
 * OMAP3 clock data
 *
 * Copyright (C) 2007-2012 Texas Instruments, Inc.
 * Copyright (C) 2007-2011 Nokia Corporation
 *
 * Written by Paul Walmsley
 * Updated to COMMON clk data format by Rajendra Nayak <rnayak@ti.com>
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
#include <linux/clk-private.h>
#include <linux/list.h>
#include <linux/io.h>

#include "soc.h"
#include "iomap.h"
#include "clock.h"
#include "clock3xxx.h"
#include "clock34xx.h"
#include "clock36xx.h"
#include "clock3517.h"
#include "cm3xxx.h"
#include "cm-regbits-34xx.h"
#include "prm3xxx.h"
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

DEFINE_CLK_FIXED_RATE(dummy_apb_pclk, CLK_IS_ROOT, 0x0, 0x0);

DEFINE_CLK_FIXED_RATE(mcbsp_clks, CLK_IS_ROOT, 0x0, 0x0);

DEFINE_CLK_FIXED_RATE(omap_32k_fck, CLK_IS_ROOT, 32768, 0x0);

DEFINE_CLK_FIXED_RATE(pclk_ck, CLK_IS_ROOT, 27000000, 0x0);

DEFINE_CLK_FIXED_RATE(rmii_ck, CLK_IS_ROOT, 50000000, 0x0);

DEFINE_CLK_FIXED_RATE(secure_32k_fck, CLK_IS_ROOT, 32768, 0x0);

DEFINE_CLK_FIXED_RATE(sys_altclk, CLK_IS_ROOT, 0x0, 0x0);

DEFINE_CLK_FIXED_RATE(virt_12m_ck, CLK_IS_ROOT, 12000000, 0x0);

DEFINE_CLK_FIXED_RATE(virt_13m_ck, CLK_IS_ROOT, 13000000, 0x0);

DEFINE_CLK_FIXED_RATE(virt_16_8m_ck, CLK_IS_ROOT, 16800000, 0x0);

DEFINE_CLK_FIXED_RATE(virt_19200000_ck, CLK_IS_ROOT, 19200000, 0x0);

DEFINE_CLK_FIXED_RATE(virt_26000000_ck, CLK_IS_ROOT, 26000000, 0x0);

DEFINE_CLK_FIXED_RATE(virt_38_4m_ck, CLK_IS_ROOT, 38400000, 0x0);

static const char *osc_sys_ck_parent_names[] = {
	"virt_12m_ck", "virt_13m_ck", "virt_19200000_ck", "virt_26000000_ck",
	"virt_38_4m_ck", "virt_16_8m_ck",
};

DEFINE_CLK_MUX(osc_sys_ck, osc_sys_ck_parent_names, NULL, 0x0,
	       OMAP3430_PRM_CLKSEL, OMAP3430_SYS_CLKIN_SEL_SHIFT,
	       OMAP3430_SYS_CLKIN_SEL_WIDTH, 0x0, NULL);

DEFINE_CLK_DIVIDER(sys_ck, "osc_sys_ck", &osc_sys_ck, 0x0,
		   OMAP3430_PRM_CLKSRC_CTRL, OMAP_SYSCLKDIV_SHIFT,
		   OMAP_SYSCLKDIV_WIDTH, CLK_DIVIDER_ONE_BASED, NULL);

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
	.max_multiplier	= OMAP3_MAX_DPLL_MULT,
	.min_divider	= 1,
	.max_divider	= OMAP3_MAX_DPLL_DIV,
};

static struct clk dpll3_ck;

static const char *dpll3_ck_parent_names[] = {
	"sys_ck",
};

static const struct clk_ops dpll3_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap3_dpll_recalc,
	.round_rate	= &omap2_dpll_round_rate,
};

static struct clk_hw_omap dpll3_ck_hw = {
	.hw = {
		.clk = &dpll3_ck,
	},
	.ops		= &clkhwops_omap3_dpll,
	.dpll_data	= &dpll3_dd,
	.clkdm_name	= "dpll3_clkdm",
};

DEFINE_STRUCT_CLK(dpll3_ck, dpll3_ck_parent_names, dpll3_ck_ops);

DEFINE_CLK_DIVIDER(dpll3_m2_ck, "dpll3_ck", &dpll3_ck, 0x0,
		   OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
		   OMAP3430_CORE_DPLL_CLKOUT_DIV_SHIFT,
		   OMAP3430_CORE_DPLL_CLKOUT_DIV_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk core_ck;

static const char *core_ck_parent_names[] = {
	"dpll3_m2_ck",
};

static const struct clk_ops core_ck_ops = {};

DEFINE_STRUCT_CLK_HW_OMAP(core_ck, NULL);
DEFINE_STRUCT_CLK(core_ck, core_ck_parent_names, core_ck_ops);

DEFINE_CLK_DIVIDER(l3_ick, "core_ck", &core_ck, 0x0,
		   OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
		   OMAP3430_CLKSEL_L3_SHIFT, OMAP3430_CLKSEL_L3_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

DEFINE_CLK_DIVIDER(l4_ick, "l3_ick", &l3_ick, 0x0,
		   OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
		   OMAP3430_CLKSEL_L4_SHIFT, OMAP3430_CLKSEL_L4_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk security_l4_ick2;

static const char *security_l4_ick2_parent_names[] = {
	"l4_ick",
};

DEFINE_STRUCT_CLK_HW_OMAP(security_l4_ick2, NULL);
DEFINE_STRUCT_CLK(security_l4_ick2, security_l4_ick2_parent_names, core_ck_ops);

static struct clk aes1_ick;

static const char *aes1_ick_parent_names[] = {
	"security_l4_ick2",
};

static const struct clk_ops aes1_ick_ops = {
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
};

static struct clk_hw_omap aes1_ick_hw = {
	.hw = {
		.clk = &aes1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP3430_EN_AES1_SHIFT,
};

DEFINE_STRUCT_CLK(aes1_ick, aes1_ick_parent_names, aes1_ick_ops);

static struct clk core_l4_ick;

static const struct clk_ops core_l4_ick_ops = {
	.init		= &omap2_init_clk_clkdm,
};

DEFINE_STRUCT_CLK_HW_OMAP(core_l4_ick, "core_l4_clkdm");
DEFINE_STRUCT_CLK(core_l4_ick, security_l4_ick2_parent_names, core_l4_ick_ops);

static struct clk aes2_ick;

static const char *aes2_ick_parent_names[] = {
	"core_l4_ick",
};

static const struct clk_ops aes2_ick_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
};

static struct clk_hw_omap aes2_ick_hw = {
	.hw = {
		.clk = &aes2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_AES2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(aes2_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk dpll1_fck;

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
	.max_multiplier	= OMAP3_MAX_DPLL_MULT,
	.min_divider	= 1,
	.max_divider	= OMAP3_MAX_DPLL_DIV,
};

static struct clk dpll1_ck;

static const struct clk_ops dpll1_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap3_noncore_dpll_enable,
	.disable	= &omap3_noncore_dpll_disable,
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap3_dpll_recalc,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.round_rate	= &omap2_dpll_round_rate,
};

static struct clk_hw_omap dpll1_ck_hw = {
	.hw = {
		.clk = &dpll1_ck,
	},
	.ops		= &clkhwops_omap3_dpll,
	.dpll_data	= &dpll1_dd,
	.clkdm_name	= "dpll1_clkdm",
};

DEFINE_STRUCT_CLK(dpll1_ck, dpll3_ck_parent_names, dpll1_ck_ops);

DEFINE_CLK_FIXED_FACTOR(dpll1_x2_ck, "dpll1_ck", &dpll1_ck, 0x0, 2, 1);

DEFINE_CLK_DIVIDER(dpll1_x2m2_ck, "dpll1_x2_ck", &dpll1_x2_ck, 0x0,
		   OMAP_CM_REGADDR(MPU_MOD, OMAP3430_CM_CLKSEL2_PLL),
		   OMAP3430_MPU_DPLL_CLKOUT_DIV_SHIFT,
		   OMAP3430_MPU_DPLL_CLKOUT_DIV_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk mpu_ck;

static const char *mpu_ck_parent_names[] = {
	"dpll1_x2m2_ck",
};

DEFINE_STRUCT_CLK_HW_OMAP(mpu_ck, "mpu_clkdm");
DEFINE_STRUCT_CLK(mpu_ck, mpu_ck_parent_names, core_l4_ick_ops);

DEFINE_CLK_DIVIDER(arm_fck, "mpu_ck", &mpu_ck, 0x0,
		   OMAP_CM_REGADDR(MPU_MOD, OMAP3430_CM_IDLEST_PLL),
		   OMAP3430_ST_MPU_CLK_SHIFT, OMAP3430_ST_MPU_CLK_WIDTH,
		   0x0, NULL);

static struct clk cam_ick;

static struct clk_hw_omap cam_ick_hw = {
	.hw = {
		.clk = &cam_ick,
	},
	.ops		= &clkhwops_iclk,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_CAM_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_CAM_SHIFT,
	.clkdm_name	= "cam_clkdm",
};

DEFINE_STRUCT_CLK(cam_ick, security_l4_ick2_parent_names, aes2_ick_ops);

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

static struct clk dpll4_ck;

static const struct clk_ops dpll4_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap3_noncore_dpll_enable,
	.disable	= &omap3_noncore_dpll_disable,
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap3_dpll_recalc,
	.set_rate	= &omap3_dpll4_set_rate,
	.round_rate	= &omap2_dpll_round_rate,
};

static struct clk_hw_omap dpll4_ck_hw = {
	.hw = {
		.clk = &dpll4_ck,
	},
	.dpll_data	= &dpll4_dd,
	.ops		= &clkhwops_omap3_dpll,
	.clkdm_name	= "dpll4_clkdm",
};

DEFINE_STRUCT_CLK(dpll4_ck, dpll3_ck_parent_names, dpll4_ck_ops);

DEFINE_CLK_DIVIDER(dpll4_m5_ck, "dpll4_ck", &dpll4_ck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_CAM_MOD, CM_CLKSEL),
		   OMAP3430_CLKSEL_CAM_SHIFT, OMAP3630_CLKSEL_CAM_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk dpll4_m5x2_ck;

static const char *dpll4_m5x2_ck_parent_names[] = {
	"dpll4_m5_ck",
};

static const struct clk_ops dpll4_m5x2_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
	.recalc_rate	= &omap3_clkoutx2_recalc,
};

static const struct clk_ops dpll4_m5x2_ck_3630_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap36xx_pwrdn_clk_enable_with_hsdiv_restore,
	.disable	= &omap2_dflt_clk_disable,
	.recalc_rate	= &omap3_clkoutx2_recalc,
};

static struct clk_hw_omap dpll4_m5x2_ck_hw = {
	.hw = {
		.clk = &dpll4_m5x2_ck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_CAM_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll4_clkdm",
};

DEFINE_STRUCT_CLK(dpll4_m5x2_ck, dpll4_m5x2_ck_parent_names, dpll4_m5x2_ck_ops);

static struct clk dpll4_m5x2_ck_3630 = {
	.name		= "dpll4_m5x2_ck",
	.hw		= &dpll4_m5x2_ck_hw.hw,
	.parent_names	= dpll4_m5x2_ck_parent_names,
	.num_parents	= ARRAY_SIZE(dpll4_m5x2_ck_parent_names),
	.ops		= &dpll4_m5x2_ck_3630_ops,
	.flags		= CLK_SET_RATE_PARENT,
};

static struct clk cam_mclk;

static const char *cam_mclk_parent_names[] = {
	"dpll4_m5x2_ck",
};

static struct clk_hw_omap cam_mclk_hw = {
	.hw = {
		.clk = &cam_mclk,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_CAM_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_CAM_SHIFT,
	.clkdm_name	= "cam_clkdm",
};

static struct clk cam_mclk = {
	.name		= "cam_mclk",
	.hw		= &cam_mclk_hw.hw,
	.parent_names	= cam_mclk_parent_names,
	.num_parents	= ARRAY_SIZE(cam_mclk_parent_names),
	.ops		= &aes2_ick_ops,
	.flags		= CLK_SET_RATE_PARENT,
};

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

DEFINE_CLK_DIVIDER(dpll4_m2_ck, "dpll4_ck", &dpll4_ck, 0x0,
		   OMAP_CM_REGADDR(PLL_MOD, OMAP3430_CM_CLKSEL3),
		   OMAP3430_DIV_96M_SHIFT, OMAP3630_DIV_96M_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk dpll4_m2x2_ck;

static const char *dpll4_m2x2_ck_parent_names[] = {
	"dpll4_m2_ck",
};

static struct clk_hw_omap dpll4_m2x2_ck_hw = {
	.hw = {
		.clk = &dpll4_m2x2_ck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_96M_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll4_clkdm",
};

DEFINE_STRUCT_CLK(dpll4_m2x2_ck, dpll4_m2x2_ck_parent_names, dpll4_m5x2_ck_ops);

static struct clk dpll4_m2x2_ck_3630 = {
	.name		= "dpll4_m2x2_ck",
	.hw		= &dpll4_m2x2_ck_hw.hw,
	.parent_names	= dpll4_m2x2_ck_parent_names,
	.num_parents	= ARRAY_SIZE(dpll4_m2x2_ck_parent_names),
	.ops		= &dpll4_m5x2_ck_3630_ops,
};

static struct clk omap_96m_alwon_fck;

static const char *omap_96m_alwon_fck_parent_names[] = {
	"dpll4_m2x2_ck",
};

DEFINE_STRUCT_CLK_HW_OMAP(omap_96m_alwon_fck, NULL);
DEFINE_STRUCT_CLK(omap_96m_alwon_fck, omap_96m_alwon_fck_parent_names,
		  core_ck_ops);

static struct clk cm_96m_fck;

static const char *cm_96m_fck_parent_names[] = {
	"omap_96m_alwon_fck",
};

DEFINE_STRUCT_CLK_HW_OMAP(cm_96m_fck, NULL);
DEFINE_STRUCT_CLK(cm_96m_fck, cm_96m_fck_parent_names, core_ck_ops);

static const struct clksel_rate clkout2_src_54m_rates[] = {
	{ .div = 1, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

DEFINE_CLK_DIVIDER(dpll4_m3_ck, "dpll4_ck", &dpll4_ck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_CLKSEL),
		   OMAP3430_CLKSEL_TV_SHIFT, OMAP3630_CLKSEL_TV_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk dpll4_m3x2_ck;

static const char *dpll4_m3x2_ck_parent_names[] = {
	"dpll4_m3_ck",
};

static struct clk_hw_omap dpll4_m3x2_ck_hw = {
	.hw = {
		.clk = &dpll4_m3x2_ck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_TV_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll4_clkdm",
};

DEFINE_STRUCT_CLK(dpll4_m3x2_ck, dpll4_m3x2_ck_parent_names, dpll4_m5x2_ck_ops);

static struct clk dpll4_m3x2_ck_3630 = {
	.name		= "dpll4_m3x2_ck",
	.hw		= &dpll4_m3x2_ck_hw.hw,
	.parent_names	= dpll4_m3x2_ck_parent_names,
	.num_parents	= ARRAY_SIZE(dpll4_m3x2_ck_parent_names),
	.ops		= &dpll4_m5x2_ck_3630_ops,
};

static const char *omap_54m_fck_parent_names[] = {
	"dpll4_m3x2_ck", "sys_altclk",
};

DEFINE_CLK_MUX(omap_54m_fck, omap_54m_fck_parent_names, NULL, 0x0,
	       OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1), OMAP3430_SOURCE_54M_SHIFT,
	       OMAP3430_SOURCE_54M_WIDTH, 0x0, NULL);

static const struct clksel clkout2_src_clksel[] = {
	{ .parent = &core_ck, .rates = clkout2_src_core_rates },
	{ .parent = &sys_ck, .rates = clkout2_src_sys_rates },
	{ .parent = &cm_96m_fck, .rates = clkout2_src_96m_rates },
	{ .parent = &omap_54m_fck, .rates = clkout2_src_54m_rates },
	{ .parent = NULL },
};

static const char *clkout2_src_ck_parent_names[] = {
	"core_ck", "sys_ck", "cm_96m_fck", "omap_54m_fck",
};

static const struct clk_ops clkout2_src_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
	.recalc_rate	= &omap2_clksel_recalc,
	.get_parent	= &omap2_clksel_find_parent_index,
	.set_parent	= &omap2_clksel_set_parent,
};

DEFINE_CLK_OMAP_MUX_GATE(clkout2_src_ck, "core_clkdm",
			 clkout2_src_clksel, OMAP3430_CM_CLKOUT_CTRL,
			 OMAP3430_CLKOUT2SOURCE_MASK,
			 OMAP3430_CM_CLKOUT_CTRL, OMAP3430_CLKOUT2_EN_SHIFT,
			 NULL, clkout2_src_ck_parent_names, clkout2_src_ck_ops);

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
	{ .parent = NULL },
};

static const char *omap_48m_fck_parent_names[] = {
	"cm_96m_fck", "sys_altclk",
};

static struct clk omap_48m_fck;

static const struct clk_ops omap_48m_fck_ops = {
	.recalc_rate	= &omap2_clksel_recalc,
	.get_parent	= &omap2_clksel_find_parent_index,
	.set_parent	= &omap2_clksel_set_parent,
};

static struct clk_hw_omap omap_48m_fck_hw = {
	.hw = {
		.clk = &omap_48m_fck,
	},
	.clksel		= omap_48m_clksel,
	.clksel_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_SOURCE_48M_MASK,
};

DEFINE_STRUCT_CLK(omap_48m_fck, omap_48m_fck_parent_names, omap_48m_fck_ops);

DEFINE_CLK_FIXED_FACTOR(omap_12m_fck, "omap_48m_fck", &omap_48m_fck, 0x0, 1, 4);

static struct clk core_12m_fck;

static const char *core_12m_fck_parent_names[] = {
	"omap_12m_fck",
};

DEFINE_STRUCT_CLK_HW_OMAP(core_12m_fck, "core_l4_clkdm");
DEFINE_STRUCT_CLK(core_12m_fck, core_12m_fck_parent_names, core_l4_ick_ops);

static struct clk core_48m_fck;

static const char *core_48m_fck_parent_names[] = {
	"omap_48m_fck",
};

DEFINE_STRUCT_CLK_HW_OMAP(core_48m_fck, "core_l4_clkdm");
DEFINE_STRUCT_CLK(core_48m_fck, core_48m_fck_parent_names, core_l4_ick_ops);

static const char *omap_96m_fck_parent_names[] = {
	"cm_96m_fck", "sys_ck",
};

DEFINE_CLK_MUX(omap_96m_fck, omap_96m_fck_parent_names, NULL, 0x0,
	       OMAP_CM_REGADDR(PLL_MOD, CM_CLKSEL1),
	       OMAP3430_SOURCE_96M_SHIFT, OMAP3430_SOURCE_96M_WIDTH, 0x0, NULL);

static struct clk core_96m_fck;

static const char *core_96m_fck_parent_names[] = {
	"omap_96m_fck",
};

DEFINE_STRUCT_CLK_HW_OMAP(core_96m_fck, "core_l4_clkdm");
DEFINE_STRUCT_CLK(core_96m_fck, core_96m_fck_parent_names, core_l4_ick_ops);

static struct clk core_l3_ick;

static const char *core_l3_ick_parent_names[] = {
	"l3_ick",
};

DEFINE_STRUCT_CLK_HW_OMAP(core_l3_ick, "core_l3_clkdm");
DEFINE_STRUCT_CLK(core_l3_ick, core_l3_ick_parent_names, core_l4_ick_ops);

DEFINE_CLK_FIXED_FACTOR(dpll3_m2x2_ck, "dpll3_m2_ck", &dpll3_m2_ck, 0x0, 2, 1);

static struct clk corex2_fck;

static const char *corex2_fck_parent_names[] = {
	"dpll3_m2x2_ck",
};

DEFINE_STRUCT_CLK_HW_OMAP(corex2_fck, NULL);
DEFINE_STRUCT_CLK(corex2_fck, corex2_fck_parent_names, core_ck_ops);

static struct clk cpefuse_fck;

static struct clk_hw_omap cpefuse_fck_hw = {
	.hw = {
		.clk = &cpefuse_fck,
	},
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP3430ES2_CM_FCLKEN3),
	.enable_bit	= OMAP3430ES2_EN_CPEFUSE_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(cpefuse_fck, dpll3_ck_parent_names, aes2_ick_ops);

static struct clk csi2_96m_fck;

static const char *csi2_96m_fck_parent_names[] = {
	"core_96m_fck",
};

static struct clk_hw_omap csi2_96m_fck_hw = {
	.hw = {
		.clk = &csi2_96m_fck,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_CAM_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_CSI2_SHIFT,
	.clkdm_name	= "cam_clkdm",
};

DEFINE_STRUCT_CLK(csi2_96m_fck, csi2_96m_fck_parent_names, aes2_ick_ops);

static struct clk d2d_26m_fck;

static struct clk_hw_omap d2d_26m_fck_hw = {
	.hw = {
		.clk = &d2d_26m_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430ES1_EN_D2D_SHIFT,
	.clkdm_name	= "d2d_clkdm",
};

DEFINE_STRUCT_CLK(d2d_26m_fck, dpll3_ck_parent_names, aes2_ick_ops);

static struct clk des1_ick;

static struct clk_hw_omap des1_ick_hw = {
	.hw = {
		.clk = &des1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP3430_EN_DES1_SHIFT,
};

DEFINE_STRUCT_CLK(des1_ick, aes1_ick_parent_names, aes1_ick_ops);

static struct clk des2_ick;

static struct clk_hw_omap des2_ick_hw = {
	.hw = {
		.clk = &des2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_DES2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(des2_ick, aes2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_DIVIDER(dpll1_fck, "core_ck", &core_ck, 0x0,
		   OMAP_CM_REGADDR(MPU_MOD, OMAP3430_CM_CLKSEL1_PLL),
		   OMAP3430_MPU_CLK_SRC_SHIFT, OMAP3430_MPU_CLK_SRC_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk dpll2_fck;

static struct dpll_data dpll2_dd = {
	.mult_div1_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_CLKSEL1_PLL),
	.mult_mask	= OMAP3430_IVA2_DPLL_MULT_MASK,
	.div1_mask	= OMAP3430_IVA2_DPLL_DIV_MASK,
	.clk_bypass	= &dpll2_fck,
	.clk_ref	= &sys_ck,
	.freqsel_mask	= OMAP3430_IVA2_DPLL_FREQSEL_MASK,
	.control_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_CLKEN_PLL),
	.enable_mask	= OMAP3430_EN_IVA2_DPLL_MASK,
	.modes		= ((1 << DPLL_LOW_POWER_STOP) | (1 << DPLL_LOCKED) |
			   (1 << DPLL_LOW_POWER_BYPASS)),
	.auto_recal_bit	= OMAP3430_EN_IVA2_DPLL_DRIFTGUARD_SHIFT,
	.recal_en_bit	= OMAP3430_PRM_IRQENABLE_MPU_IVA2_DPLL_RECAL_EN_SHIFT,
	.recal_st_bit	= OMAP3430_PRM_IRQSTATUS_MPU_IVA2_DPLL_ST_SHIFT,
	.autoidle_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_AUTOIDLE_PLL),
	.autoidle_mask	= OMAP3430_AUTO_IVA2_DPLL_MASK,
	.idlest_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_IDLEST_PLL),
	.idlest_mask	= OMAP3430_ST_IVA2_CLK_MASK,
	.max_multiplier	= OMAP3_MAX_DPLL_MULT,
	.min_divider	= 1,
	.max_divider	= OMAP3_MAX_DPLL_DIV,
};

static struct clk dpll2_ck;

static struct clk_hw_omap dpll2_ck_hw = {
	.hw = {
		.clk = &dpll2_ck,
	},
	.ops		= &clkhwops_omap3_dpll,
	.dpll_data	= &dpll2_dd,
	.clkdm_name	= "dpll2_clkdm",
};

DEFINE_STRUCT_CLK(dpll2_ck, dpll3_ck_parent_names, dpll1_ck_ops);

DEFINE_CLK_DIVIDER(dpll2_fck, "core_ck", &core_ck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_CLKSEL1_PLL),
		   OMAP3430_IVA2_CLK_SRC_SHIFT, OMAP3430_IVA2_CLK_SRC_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

DEFINE_CLK_DIVIDER(dpll2_m2_ck, "dpll2_ck", &dpll2_ck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, OMAP3430_CM_CLKSEL2_PLL),
		   OMAP3430_IVA2_DPLL_CLKOUT_DIV_SHIFT,
		   OMAP3430_IVA2_DPLL_CLKOUT_DIV_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

DEFINE_CLK_DIVIDER(dpll3_m3_ck, "dpll3_ck", &dpll3_ck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
		   OMAP3430_DIV_DPLL3_SHIFT, OMAP3430_DIV_DPLL3_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk dpll3_m3x2_ck;

static const char *dpll3_m3x2_ck_parent_names[] = {
	"dpll3_m3_ck",
};

static struct clk_hw_omap dpll3_m3x2_ck_hw = {
	.hw = {
		.clk = &dpll3_m3x2_ck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_EMU_CORE_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll3_clkdm",
};

DEFINE_STRUCT_CLK(dpll3_m3x2_ck, dpll3_m3x2_ck_parent_names, dpll4_m5x2_ck_ops);

static struct clk dpll3_m3x2_ck_3630 = {
	.name		= "dpll3_m3x2_ck",
	.hw		= &dpll3_m3x2_ck_hw.hw,
	.parent_names	= dpll3_m3x2_ck_parent_names,
	.num_parents	= ARRAY_SIZE(dpll3_m3x2_ck_parent_names),
	.ops		= &dpll4_m5x2_ck_3630_ops,
};

DEFINE_CLK_FIXED_FACTOR(dpll3_x2_ck, "dpll3_ck", &dpll3_ck, 0x0, 2, 1);

DEFINE_CLK_DIVIDER(dpll4_m4_ck, "dpll4_ck", &dpll4_ck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_CLKSEL),
		   OMAP3430_CLKSEL_DSS1_SHIFT, OMAP3630_CLKSEL_DSS1_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk dpll4_m4x2_ck;

static const char *dpll4_m4x2_ck_parent_names[] = {
	"dpll4_m4_ck",
};

static struct clk_hw_omap dpll4_m4x2_ck_hw = {
	.hw = {
		.clk = &dpll4_m4x2_ck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_DSS1_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll4_clkdm",
};

DEFINE_STRUCT_CLK(dpll4_m4x2_ck, dpll4_m4x2_ck_parent_names, dpll4_m5x2_ck_ops);

static struct clk dpll4_m4x2_ck_3630 = {
	.name		= "dpll4_m4x2_ck",
	.hw		= &dpll4_m4x2_ck_hw.hw,
	.parent_names	= dpll4_m4x2_ck_parent_names,
	.num_parents	= ARRAY_SIZE(dpll4_m4x2_ck_parent_names),
	.ops		= &dpll4_m5x2_ck_3630_ops,
};

DEFINE_CLK_DIVIDER(dpll4_m6_ck, "dpll4_ck", &dpll4_ck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
		   OMAP3430_DIV_DPLL4_SHIFT, OMAP3630_DIV_DPLL4_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk dpll4_m6x2_ck;

static const char *dpll4_m6x2_ck_parent_names[] = {
	"dpll4_m6_ck",
};

static struct clk_hw_omap dpll4_m6x2_ck_hw = {
	.hw = {
		.clk = &dpll4_m6x2_ck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(PLL_MOD, CM_CLKEN),
	.enable_bit	= OMAP3430_PWRDN_EMU_PERIPH_SHIFT,
	.flags		= INVERT_ENABLE,
	.clkdm_name	= "dpll4_clkdm",
};

DEFINE_STRUCT_CLK(dpll4_m6x2_ck, dpll4_m6x2_ck_parent_names, dpll4_m5x2_ck_ops);

static struct clk dpll4_m6x2_ck_3630 = {
	.name		= "dpll4_m6x2_ck",
	.hw		= &dpll4_m6x2_ck_hw.hw,
	.parent_names	= dpll4_m6x2_ck_parent_names,
	.num_parents	= ARRAY_SIZE(dpll4_m6x2_ck_parent_names),
	.ops		= &dpll4_m5x2_ck_3630_ops,
};

DEFINE_CLK_FIXED_FACTOR(dpll4_x2_ck, "dpll4_ck", &dpll4_ck, 0x0, 2, 1);

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
	.max_multiplier	= OMAP3_MAX_DPLL_MULT,
	.min_divider	= 1,
	.max_divider	= OMAP3_MAX_DPLL_DIV,
};

static struct clk dpll5_ck;

static struct clk_hw_omap dpll5_ck_hw = {
	.hw = {
		.clk = &dpll5_ck,
	},
	.ops		= &clkhwops_omap3_dpll,
	.dpll_data	= &dpll5_dd,
	.clkdm_name	= "dpll5_clkdm",
};

DEFINE_STRUCT_CLK(dpll5_ck, dpll3_ck_parent_names, dpll1_ck_ops);

DEFINE_CLK_DIVIDER(dpll5_m2_ck, "dpll5_ck", &dpll5_ck, 0x0,
		   OMAP_CM_REGADDR(PLL_MOD, OMAP3430ES2_CM_CLKSEL5),
		   OMAP3430ES2_DIV_120M_SHIFT, OMAP3430ES2_DIV_120M_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk dss1_alwon_fck_3430es1;

static const char *dss1_alwon_fck_3430es1_parent_names[] = {
	"dpll4_m4x2_ck",
};

static struct clk_hw_omap dss1_alwon_fck_3430es1_hw = {
	.hw = {
		.clk = &dss1_alwon_fck_3430es1,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_DSS1_SHIFT,
	.clkdm_name	= "dss_clkdm",
};

DEFINE_STRUCT_CLK(dss1_alwon_fck_3430es1, dss1_alwon_fck_3430es1_parent_names,
		  aes2_ick_ops);

static struct clk dss1_alwon_fck_3430es2;

static struct clk_hw_omap dss1_alwon_fck_3430es2_hw = {
	.hw = {
		.clk = &dss1_alwon_fck_3430es2,
	},
	.ops		= &clkhwops_omap3430es2_dss_usbhost_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_DSS1_SHIFT,
	.clkdm_name	= "dss_clkdm",
};

DEFINE_STRUCT_CLK(dss1_alwon_fck_3430es2, dss1_alwon_fck_3430es1_parent_names,
		  aes2_ick_ops);

static struct clk dss2_alwon_fck;

static struct clk_hw_omap dss2_alwon_fck_hw = {
	.hw = {
		.clk = &dss2_alwon_fck,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_DSS2_SHIFT,
	.clkdm_name	= "dss_clkdm",
};

DEFINE_STRUCT_CLK(dss2_alwon_fck, dpll3_ck_parent_names, aes2_ick_ops);

static struct clk dss_96m_fck;

static struct clk_hw_omap dss_96m_fck_hw = {
	.hw = {
		.clk = &dss_96m_fck,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_TV_SHIFT,
	.clkdm_name	= "dss_clkdm",
};

DEFINE_STRUCT_CLK(dss_96m_fck, core_96m_fck_parent_names, aes2_ick_ops);

static struct clk dss_ick_3430es1;

static struct clk_hw_omap dss_ick_3430es1_hw = {
	.hw = {
		.clk = &dss_ick_3430es1,
	},
	.ops		= &clkhwops_iclk,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_CM_ICLKEN_DSS_EN_DSS_SHIFT,
	.clkdm_name	= "dss_clkdm",
};

DEFINE_STRUCT_CLK(dss_ick_3430es1, security_l4_ick2_parent_names, aes2_ick_ops);

static struct clk dss_ick_3430es2;

static struct clk_hw_omap dss_ick_3430es2_hw = {
	.hw = {
		.clk = &dss_ick_3430es2,
	},
	.ops		= &clkhwops_omap3430es2_iclk_dss_usbhost_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_CM_ICLKEN_DSS_EN_DSS_SHIFT,
	.clkdm_name	= "dss_clkdm",
};

DEFINE_STRUCT_CLK(dss_ick_3430es2, security_l4_ick2_parent_names, aes2_ick_ops);

static struct clk dss_tv_fck;

static const char *dss_tv_fck_parent_names[] = {
	"omap_54m_fck",
};

static struct clk_hw_omap dss_tv_fck_hw = {
	.hw = {
		.clk = &dss_tv_fck,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_DSS_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_TV_SHIFT,
	.clkdm_name	= "dss_clkdm",
};

DEFINE_STRUCT_CLK(dss_tv_fck, dss_tv_fck_parent_names, aes2_ick_ops);

static struct clk emac_fck;

static const char *emac_fck_parent_names[] = {
	"rmii_ck",
};

static struct clk_hw_omap emac_fck_hw = {
	.hw = {
		.clk = &emac_fck,
	},
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_CPGMAC_FCLK_SHIFT,
};

DEFINE_STRUCT_CLK(emac_fck, emac_fck_parent_names, aes1_ick_ops);

static struct clk ipss_ick;

static const char *ipss_ick_parent_names[] = {
	"core_l3_ick",
};

static struct clk_hw_omap ipss_ick_hw = {
	.hw = {
		.clk = &ipss_ick,
	},
	.ops		= &clkhwops_am35xx_ipss_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= AM35XX_EN_IPSS_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(ipss_ick, ipss_ick_parent_names, aes2_ick_ops);

static struct clk emac_ick;

static const char *emac_ick_parent_names[] = {
	"ipss_ick",
};

static struct clk_hw_omap emac_ick_hw = {
	.hw = {
		.clk = &emac_ick,
	},
	.ops		= &clkhwops_am35xx_ipss_module_wait,
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_CPGMAC_VBUSP_CLK_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(emac_ick, emac_ick_parent_names, aes2_ick_ops);

static struct clk emu_core_alwon_ck;

static const char *emu_core_alwon_ck_parent_names[] = {
	"dpll3_m3x2_ck",
};

DEFINE_STRUCT_CLK_HW_OMAP(emu_core_alwon_ck, "dpll3_clkdm");
DEFINE_STRUCT_CLK(emu_core_alwon_ck, emu_core_alwon_ck_parent_names,
		  core_l4_ick_ops);

static struct clk emu_mpu_alwon_ck;

static const char *emu_mpu_alwon_ck_parent_names[] = {
	"mpu_ck",
};

DEFINE_STRUCT_CLK_HW_OMAP(emu_mpu_alwon_ck, NULL);
DEFINE_STRUCT_CLK(emu_mpu_alwon_ck, emu_mpu_alwon_ck_parent_names, core_ck_ops);

static struct clk emu_per_alwon_ck;

static const char *emu_per_alwon_ck_parent_names[] = {
	"dpll4_m6x2_ck",
};

DEFINE_STRUCT_CLK_HW_OMAP(emu_per_alwon_ck, "dpll4_clkdm");
DEFINE_STRUCT_CLK(emu_per_alwon_ck, emu_per_alwon_ck_parent_names,
		  core_l4_ick_ops);

static const char *emu_src_ck_parent_names[] = {
	"sys_ck", "emu_core_alwon_ck", "emu_per_alwon_ck", "emu_mpu_alwon_ck",
};

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

static const struct clk_ops emu_src_ck_ops = {
	.init		= &omap2_init_clk_clkdm,
	.recalc_rate	= &omap2_clksel_recalc,
	.get_parent	= &omap2_clksel_find_parent_index,
	.set_parent	= &omap2_clksel_set_parent,
	.enable		= &omap2_clkops_enable_clkdm,
	.disable	= &omap2_clkops_disable_clkdm,
};

static struct clk emu_src_ck;

static struct clk_hw_omap emu_src_ck_hw = {
	.hw = {
		.clk = &emu_src_ck,
	},
	.clksel		= emu_src_clksel,
	.clksel_reg	= OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
	.clksel_mask	= OMAP3430_MUX_CTRL_MASK,
	.clkdm_name	= "emu_clkdm",
};

DEFINE_STRUCT_CLK(emu_src_ck, emu_src_ck_parent_names, emu_src_ck_ops);

DEFINE_CLK_DIVIDER(atclk_fck, "emu_src_ck", &emu_src_ck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
		   OMAP3430_CLKSEL_ATCLK_SHIFT, OMAP3430_CLKSEL_ATCLK_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk fac_ick;

static struct clk_hw_omap fac_ick_hw = {
	.hw = {
		.clk = &fac_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430ES1_EN_FAC_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(fac_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk fshostusb_fck;

static const char *fshostusb_fck_parent_names[] = {
	"core_48m_fck",
};

static struct clk_hw_omap fshostusb_fck_hw = {
	.hw = {
		.clk = &fshostusb_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430ES1_EN_FSHOSTUSB_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(fshostusb_fck, fshostusb_fck_parent_names, aes2_ick_ops);

static struct clk gfx_l3_ck;

static struct clk_hw_omap gfx_l3_ck_hw = {
	.hw = {
		.clk = &gfx_l3_ck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_ICLKEN),
	.enable_bit	= OMAP_EN_GFX_SHIFT,
	.clkdm_name	= "gfx_3430es1_clkdm",
};

DEFINE_STRUCT_CLK(gfx_l3_ck, core_l3_ick_parent_names, aes1_ick_ops);

DEFINE_CLK_DIVIDER(gfx_l3_fck, "l3_ick", &l3_ick, 0x0,
		   OMAP_CM_REGADDR(GFX_MOD, CM_CLKSEL),
		   OMAP_CLKSEL_GFX_SHIFT, OMAP_CLKSEL_GFX_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk gfx_cg1_ck;

static const char *gfx_cg1_ck_parent_names[] = {
	"gfx_l3_fck",
};

static struct clk_hw_omap gfx_cg1_ck_hw = {
	.hw = {
		.clk = &gfx_cg1_ck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430ES1_EN_2D_SHIFT,
	.clkdm_name	= "gfx_3430es1_clkdm",
};

DEFINE_STRUCT_CLK(gfx_cg1_ck, gfx_cg1_ck_parent_names, aes2_ick_ops);

static struct clk gfx_cg2_ck;

static struct clk_hw_omap gfx_cg2_ck_hw = {
	.hw = {
		.clk = &gfx_cg2_ck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(GFX_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430ES1_EN_3D_SHIFT,
	.clkdm_name	= "gfx_3430es1_clkdm",
};

DEFINE_STRUCT_CLK(gfx_cg2_ck, gfx_cg1_ck_parent_names, aes2_ick_ops);

static struct clk gfx_l3_ick;

static const char *gfx_l3_ick_parent_names[] = {
	"gfx_l3_ck",
};

DEFINE_STRUCT_CLK_HW_OMAP(gfx_l3_ick, "gfx_3430es1_clkdm");
DEFINE_STRUCT_CLK(gfx_l3_ick, gfx_l3_ick_parent_names, core_l4_ick_ops);

static struct clk wkup_32k_fck;

static const char *wkup_32k_fck_parent_names[] = {
	"omap_32k_fck",
};

DEFINE_STRUCT_CLK_HW_OMAP(wkup_32k_fck, "wkup_clkdm");
DEFINE_STRUCT_CLK(wkup_32k_fck, wkup_32k_fck_parent_names, core_l4_ick_ops);

static struct clk gpio1_dbck;

static const char *gpio1_dbck_parent_names[] = {
	"wkup_32k_fck",
};

static struct clk_hw_omap gpio1_dbck_hw = {
	.hw = {
		.clk = &gpio1_dbck,
	},
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(gpio1_dbck, gpio1_dbck_parent_names, aes2_ick_ops);

static struct clk wkup_l4_ick;

DEFINE_STRUCT_CLK_HW_OMAP(wkup_l4_ick, "wkup_clkdm");
DEFINE_STRUCT_CLK(wkup_l4_ick, dpll3_ck_parent_names, core_l4_ick_ops);

static struct clk gpio1_ick;

static const char *gpio1_ick_parent_names[] = {
	"wkup_l4_ick",
};

static struct clk_hw_omap gpio1_ick_hw = {
	.hw = {
		.clk = &gpio1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(gpio1_ick, gpio1_ick_parent_names, aes2_ick_ops);

static struct clk per_32k_alwon_fck;

DEFINE_STRUCT_CLK_HW_OMAP(per_32k_alwon_fck, "per_clkdm");
DEFINE_STRUCT_CLK(per_32k_alwon_fck, wkup_32k_fck_parent_names,
		  core_l4_ick_ops);

static struct clk gpio2_dbck;

static const char *gpio2_dbck_parent_names[] = {
	"per_32k_alwon_fck",
};

static struct clk_hw_omap gpio2_dbck_hw = {
	.hw = {
		.clk = &gpio2_dbck,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO2_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpio2_dbck, gpio2_dbck_parent_names, aes2_ick_ops);

static struct clk per_l4_ick;

DEFINE_STRUCT_CLK_HW_OMAP(per_l4_ick, "per_clkdm");
DEFINE_STRUCT_CLK(per_l4_ick, security_l4_ick2_parent_names, core_l4_ick_ops);

static struct clk gpio2_ick;

static const char *gpio2_ick_parent_names[] = {
	"per_l4_ick",
};

static struct clk_hw_omap gpio2_ick_hw = {
	.hw = {
		.clk = &gpio2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO2_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpio2_ick, gpio2_ick_parent_names, aes2_ick_ops);

static struct clk gpio3_dbck;

static struct clk_hw_omap gpio3_dbck_hw = {
	.hw = {
		.clk = &gpio3_dbck,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO3_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpio3_dbck, gpio2_dbck_parent_names, aes2_ick_ops);

static struct clk gpio3_ick;

static struct clk_hw_omap gpio3_ick_hw = {
	.hw = {
		.clk = &gpio3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO3_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpio3_ick, gpio2_ick_parent_names, aes2_ick_ops);

static struct clk gpio4_dbck;

static struct clk_hw_omap gpio4_dbck_hw = {
	.hw = {
		.clk = &gpio4_dbck,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO4_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpio4_dbck, gpio2_dbck_parent_names, aes2_ick_ops);

static struct clk gpio4_ick;

static struct clk_hw_omap gpio4_ick_hw = {
	.hw = {
		.clk = &gpio4_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO4_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpio4_ick, gpio2_ick_parent_names, aes2_ick_ops);

static struct clk gpio5_dbck;

static struct clk_hw_omap gpio5_dbck_hw = {
	.hw = {
		.clk = &gpio5_dbck,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO5_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpio5_dbck, gpio2_dbck_parent_names, aes2_ick_ops);

static struct clk gpio5_ick;

static struct clk_hw_omap gpio5_ick_hw = {
	.hw = {
		.clk = &gpio5_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO5_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpio5_ick, gpio2_ick_parent_names, aes2_ick_ops);

static struct clk gpio6_dbck;

static struct clk_hw_omap gpio6_dbck_hw = {
	.hw = {
		.clk = &gpio6_dbck,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_GPIO6_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpio6_dbck, gpio2_dbck_parent_names, aes2_ick_ops);

static struct clk gpio6_ick;

static struct clk_hw_omap gpio6_ick_hw = {
	.hw = {
		.clk = &gpio6_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPIO6_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpio6_ick, gpio2_ick_parent_names, aes2_ick_ops);

static struct clk gpmc_fck;

static struct clk_hw_omap gpmc_fck_hw = {
	.hw = {
		.clk = &gpmc_fck,
	},
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(gpmc_fck, ipss_ick_parent_names, core_l4_ick_ops);

static const struct clksel omap343x_gpt_clksel[] = {
	{ .parent = &omap_32k_fck, .rates = gpt_32k_rates },
	{ .parent = &sys_ck, .rates = gpt_sys_rates },
	{ .parent = NULL },
};

static const char *gpt10_fck_parent_names[] = {
	"omap_32k_fck", "sys_ck",
};

DEFINE_CLK_OMAP_MUX_GATE(gpt10_fck, "core_l4_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT10_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP3430_EN_GPT10_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt10_ick;

static struct clk_hw_omap gpt10_ick_hw = {
	.hw = {
		.clk = &gpt10_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_GPT10_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt10_ick, aes2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt11_fck, "core_l4_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT11_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP3430_EN_GPT11_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt11_ick;

static struct clk_hw_omap gpt11_ick_hw = {
	.hw = {
		.clk = &gpt11_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_GPT11_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(gpt11_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk gpt12_fck;

static const char *gpt12_fck_parent_names[] = {
	"secure_32k_fck",
};

DEFINE_STRUCT_CLK_HW_OMAP(gpt12_fck, "wkup_clkdm");
DEFINE_STRUCT_CLK(gpt12_fck, gpt12_fck_parent_names, core_l4_ick_ops);

static struct clk gpt12_ick;

static struct clk_hw_omap gpt12_ick_hw = {
	.hw = {
		.clk = &gpt12_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT12_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(gpt12_ick, gpio1_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt1_fck, "wkup_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(WKUP_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT1_MASK,
			 OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
			 OMAP3430_EN_GPT1_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt1_ick;

static struct clk_hw_omap gpt1_ick_hw = {
	.hw = {
		.clk = &gpt1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(gpt1_ick, gpio1_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt2_fck, "per_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT2_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_GPT2_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt2_ick;

static struct clk_hw_omap gpt2_ick_hw = {
	.hw = {
		.clk = &gpt2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT2_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpt2_ick, gpio2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt3_fck, "per_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT3_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_GPT3_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt3_ick;

static struct clk_hw_omap gpt3_ick_hw = {
	.hw = {
		.clk = &gpt3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT3_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpt3_ick, gpio2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt4_fck, "per_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT4_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_GPT4_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt4_ick;

static struct clk_hw_omap gpt4_ick_hw = {
	.hw = {
		.clk = &gpt4_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT4_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpt4_ick, gpio2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt5_fck, "per_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT5_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_GPT5_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt5_ick;

static struct clk_hw_omap gpt5_ick_hw = {
	.hw = {
		.clk = &gpt5_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT5_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpt5_ick, gpio2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt6_fck, "per_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT6_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_GPT6_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt6_ick;

static struct clk_hw_omap gpt6_ick_hw = {
	.hw = {
		.clk = &gpt6_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT6_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpt6_ick, gpio2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt7_fck, "per_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT7_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_GPT7_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt7_ick;

static struct clk_hw_omap gpt7_ick_hw = {
	.hw = {
		.clk = &gpt7_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT7_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpt7_ick, gpio2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt8_fck, "per_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT8_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_GPT8_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt8_ick;

static struct clk_hw_omap gpt8_ick_hw = {
	.hw = {
		.clk = &gpt8_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT8_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpt8_ick, gpio2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(gpt9_fck, "per_clkdm", omap343x_gpt_clksel,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_GPT9_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_GPT9_SHIFT, &clkhwops_wait,
			 gpt10_fck_parent_names, clkout2_src_ck_ops);

static struct clk gpt9_ick;

static struct clk_hw_omap gpt9_ick_hw = {
	.hw = {
		.clk = &gpt9_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_GPT9_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(gpt9_ick, gpio2_ick_parent_names, aes2_ick_ops);

static struct clk hdq_fck;

static const char *hdq_fck_parent_names[] = {
	"core_12m_fck",
};

static struct clk_hw_omap hdq_fck_hw = {
	.hw = {
		.clk = &hdq_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_HDQ_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(hdq_fck, hdq_fck_parent_names, aes2_ick_ops);

static struct clk hdq_ick;

static struct clk_hw_omap hdq_ick_hw = {
	.hw = {
		.clk = &hdq_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_HDQ_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(hdq_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk hecc_ck;

static struct clk_hw_omap hecc_ck_hw = {
	.hw = {
		.clk = &hecc_ck,
	},
	.ops		= &clkhwops_am35xx_ipss_module_wait,
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_HECC_VBUSP_CLK_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(hecc_ck, dpll3_ck_parent_names, aes2_ick_ops);

static struct clk hsotgusb_fck_am35xx;

static struct clk_hw_omap hsotgusb_fck_am35xx_hw = {
	.hw = {
		.clk = &hsotgusb_fck_am35xx,
	},
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_USBOTG_FCLK_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(hsotgusb_fck_am35xx, dpll3_ck_parent_names, aes2_ick_ops);

static struct clk hsotgusb_ick_3430es1;

static struct clk_hw_omap hsotgusb_ick_3430es1_hw = {
	.hw = {
		.clk = &hsotgusb_ick_3430es1,
	},
	.ops		= &clkhwops_iclk,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_HSOTGUSB_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(hsotgusb_ick_3430es1, ipss_ick_parent_names, aes2_ick_ops);

static struct clk hsotgusb_ick_3430es2;

static struct clk_hw_omap hsotgusb_ick_3430es2_hw = {
	.hw = {
		.clk = &hsotgusb_ick_3430es2,
	},
	.ops		= &clkhwops_omap3430es2_iclk_hsotgusb_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_HSOTGUSB_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(hsotgusb_ick_3430es2, ipss_ick_parent_names, aes2_ick_ops);

static struct clk hsotgusb_ick_am35xx;

static struct clk_hw_omap hsotgusb_ick_am35xx_hw = {
	.hw = {
		.clk = &hsotgusb_ick_am35xx,
	},
	.ops		= &clkhwops_am35xx_ipss_module_wait,
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_USBOTG_VBUSP_CLK_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(hsotgusb_ick_am35xx, emac_ick_parent_names, aes2_ick_ops);

static struct clk i2c1_fck;

static struct clk_hw_omap i2c1_fck_hw = {
	.hw = {
		.clk = &i2c1_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_I2C1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(i2c1_fck, csi2_96m_fck_parent_names, aes2_ick_ops);

static struct clk i2c1_ick;

static struct clk_hw_omap i2c1_ick_hw = {
	.hw = {
		.clk = &i2c1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_I2C1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(i2c1_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk i2c2_fck;

static struct clk_hw_omap i2c2_fck_hw = {
	.hw = {
		.clk = &i2c2_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_I2C2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(i2c2_fck, csi2_96m_fck_parent_names, aes2_ick_ops);

static struct clk i2c2_ick;

static struct clk_hw_omap i2c2_ick_hw = {
	.hw = {
		.clk = &i2c2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_I2C2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(i2c2_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk i2c3_fck;

static struct clk_hw_omap i2c3_fck_hw = {
	.hw = {
		.clk = &i2c3_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_I2C3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(i2c3_fck, csi2_96m_fck_parent_names, aes2_ick_ops);

static struct clk i2c3_ick;

static struct clk_hw_omap i2c3_ick_hw = {
	.hw = {
		.clk = &i2c3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_I2C3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(i2c3_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk icr_ick;

static struct clk_hw_omap icr_ick_hw = {
	.hw = {
		.clk = &icr_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_ICR_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(icr_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk iva2_ck;

static const char *iva2_ck_parent_names[] = {
	"dpll2_m2_ck",
};

static struct clk_hw_omap iva2_ck_hw = {
	.hw = {
		.clk = &iva2_ck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_IVA2_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_CM_FCLKEN_IVA2_EN_IVA2_SHIFT,
	.clkdm_name	= "iva2_clkdm",
};

DEFINE_STRUCT_CLK(iva2_ck, iva2_ck_parent_names, aes2_ick_ops);

static struct clk mad2d_ick;

static struct clk_hw_omap mad2d_ick_hw = {
	.hw = {
		.clk = &mad2d_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP3430_EN_MAD2D_SHIFT,
	.clkdm_name	= "d2d_clkdm",
};

DEFINE_STRUCT_CLK(mad2d_ick, core_l3_ick_parent_names, aes2_ick_ops);

static struct clk mailboxes_ick;

static struct clk_hw_omap mailboxes_ick_hw = {
	.hw = {
		.clk = &mailboxes_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MAILBOXES_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mailboxes_ick, aes2_ick_parent_names, aes2_ick_ops);

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
	{ .parent = &mcbsp_clks, .rates = common_mcbsp_mcbsp_rates },
	{ .parent = NULL },
};

static const char *mcbsp1_fck_parent_names[] = {
	"core_96m_fck", "mcbsp_clks",
};

DEFINE_CLK_OMAP_MUX_GATE(mcbsp1_fck, "core_l4_clkdm", mcbsp_15_clksel,
			 OMAP343X_CTRL_REGADDR(OMAP2_CONTROL_DEVCONF0),
			 OMAP2_MCBSP1_CLKS_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP3430_EN_MCBSP1_SHIFT, &clkhwops_wait,
			 mcbsp1_fck_parent_names, clkout2_src_ck_ops);

static struct clk mcbsp1_ick;

static struct clk_hw_omap mcbsp1_ick_hw = {
	.hw = {
		.clk = &mcbsp1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCBSP1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcbsp1_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk per_96m_fck;

DEFINE_STRUCT_CLK_HW_OMAP(per_96m_fck, "per_clkdm");
DEFINE_STRUCT_CLK(per_96m_fck, cm_96m_fck_parent_names, core_l4_ick_ops);

static const struct clksel mcbsp_234_clksel[] = {
	{ .parent = &per_96m_fck, .rates = common_mcbsp_96m_rates },
	{ .parent = &mcbsp_clks, .rates = common_mcbsp_mcbsp_rates },
	{ .parent = NULL },
};

static const char *mcbsp2_fck_parent_names[] = {
	"per_96m_fck", "mcbsp_clks",
};

DEFINE_CLK_OMAP_MUX_GATE(mcbsp2_fck, "per_clkdm", mcbsp_234_clksel,
			 OMAP343X_CTRL_REGADDR(OMAP2_CONTROL_DEVCONF0),
			 OMAP2_MCBSP2_CLKS_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_MCBSP2_SHIFT, &clkhwops_wait,
			 mcbsp2_fck_parent_names, clkout2_src_ck_ops);

static struct clk mcbsp2_ick;

static struct clk_hw_omap mcbsp2_ick_hw = {
	.hw = {
		.clk = &mcbsp2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_MCBSP2_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(mcbsp2_ick, gpio2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(mcbsp3_fck, "per_clkdm", mcbsp_234_clksel,
			 OMAP343X_CTRL_REGADDR(OMAP343X_CONTROL_DEVCONF1),
			 OMAP2_MCBSP3_CLKS_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_MCBSP3_SHIFT, &clkhwops_wait,
			 mcbsp2_fck_parent_names, clkout2_src_ck_ops);

static struct clk mcbsp3_ick;

static struct clk_hw_omap mcbsp3_ick_hw = {
	.hw = {
		.clk = &mcbsp3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_MCBSP3_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(mcbsp3_ick, gpio2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(mcbsp4_fck, "per_clkdm", mcbsp_234_clksel,
			 OMAP343X_CTRL_REGADDR(OMAP343X_CONTROL_DEVCONF1),
			 OMAP2_MCBSP4_CLKS_MASK,
			 OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
			 OMAP3430_EN_MCBSP4_SHIFT, &clkhwops_wait,
			 mcbsp2_fck_parent_names, clkout2_src_ck_ops);

static struct clk mcbsp4_ick;

static struct clk_hw_omap mcbsp4_ick_hw = {
	.hw = {
		.clk = &mcbsp4_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_MCBSP4_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(mcbsp4_ick, gpio2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_OMAP_MUX_GATE(mcbsp5_fck, "core_l4_clkdm", mcbsp_15_clksel,
			 OMAP343X_CTRL_REGADDR(OMAP343X_CONTROL_DEVCONF1),
			 OMAP2_MCBSP5_CLKS_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP3430_EN_MCBSP5_SHIFT, &clkhwops_wait,
			 mcbsp1_fck_parent_names, clkout2_src_ck_ops);

static struct clk mcbsp5_ick;

static struct clk_hw_omap mcbsp5_ick_hw = {
	.hw = {
		.clk = &mcbsp5_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCBSP5_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcbsp5_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk mcspi1_fck;

static struct clk_hw_omap mcspi1_fck_hw = {
	.hw = {
		.clk = &mcspi1_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi1_fck, fshostusb_fck_parent_names, aes2_ick_ops);

static struct clk mcspi1_ick;

static struct clk_hw_omap mcspi1_ick_hw = {
	.hw = {
		.clk = &mcspi1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi1_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk mcspi2_fck;

static struct clk_hw_omap mcspi2_fck_hw = {
	.hw = {
		.clk = &mcspi2_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi2_fck, fshostusb_fck_parent_names, aes2_ick_ops);

static struct clk mcspi2_ick;

static struct clk_hw_omap mcspi2_ick_hw = {
	.hw = {
		.clk = &mcspi2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi2_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk mcspi3_fck;

static struct clk_hw_omap mcspi3_fck_hw = {
	.hw = {
		.clk = &mcspi3_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi3_fck, fshostusb_fck_parent_names, aes2_ick_ops);

static struct clk mcspi3_ick;

static struct clk_hw_omap mcspi3_ick_hw = {
	.hw = {
		.clk = &mcspi3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi3_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk mcspi4_fck;

static struct clk_hw_omap mcspi4_fck_hw = {
	.hw = {
		.clk = &mcspi4_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI4_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi4_fck, fshostusb_fck_parent_names, aes2_ick_ops);

static struct clk mcspi4_ick;

static struct clk_hw_omap mcspi4_ick_hw = {
	.hw = {
		.clk = &mcspi4_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MCSPI4_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mcspi4_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk mmchs1_fck;

static struct clk_hw_omap mmchs1_fck_hw = {
	.hw = {
		.clk = &mmchs1_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MMC1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mmchs1_fck, csi2_96m_fck_parent_names, aes2_ick_ops);

static struct clk mmchs1_ick;

static struct clk_hw_omap mmchs1_ick_hw = {
	.hw = {
		.clk = &mmchs1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MMC1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mmchs1_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk mmchs2_fck;

static struct clk_hw_omap mmchs2_fck_hw = {
	.hw = {
		.clk = &mmchs2_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MMC2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mmchs2_fck, csi2_96m_fck_parent_names, aes2_ick_ops);

static struct clk mmchs2_ick;

static struct clk_hw_omap mmchs2_ick_hw = {
	.hw = {
		.clk = &mmchs2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MMC2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mmchs2_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk mmchs3_fck;

static struct clk_hw_omap mmchs3_fck_hw = {
	.hw = {
		.clk = &mmchs3_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430ES2_EN_MMC3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mmchs3_fck, csi2_96m_fck_parent_names, aes2_ick_ops);

static struct clk mmchs3_ick;

static struct clk_hw_omap mmchs3_ick_hw = {
	.hw = {
		.clk = &mmchs3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430ES2_EN_MMC3_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mmchs3_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk modem_fck;

static struct clk_hw_omap modem_fck_hw = {
	.hw = {
		.clk = &modem_fck,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MODEM_SHIFT,
	.clkdm_name	= "d2d_clkdm",
};

DEFINE_STRUCT_CLK(modem_fck, dpll3_ck_parent_names, aes2_ick_ops);

static struct clk mspro_fck;

static struct clk_hw_omap mspro_fck_hw = {
	.hw = {
		.clk = &mspro_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_MSPRO_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mspro_fck, csi2_96m_fck_parent_names, aes2_ick_ops);

static struct clk mspro_ick;

static struct clk_hw_omap mspro_ick_hw = {
	.hw = {
		.clk = &mspro_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_MSPRO_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(mspro_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk omap_192m_alwon_fck;

DEFINE_STRUCT_CLK_HW_OMAP(omap_192m_alwon_fck, NULL);
DEFINE_STRUCT_CLK(omap_192m_alwon_fck, omap_96m_alwon_fck_parent_names,
		  core_ck_ops);

static struct clk omap_32ksync_ick;

static struct clk_hw_omap omap_32ksync_ick_hw = {
	.hw = {
		.clk = &omap_32ksync_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_32KSYNC_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(omap_32ksync_ick, gpio1_ick_parent_names, aes2_ick_ops);

static const struct clksel_rate omap_96m_alwon_fck_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_36XX },
	{ .div = 2, .val = 2, .flags = RATE_IN_36XX },
	{ .div = 0 }
};

static const struct clksel omap_96m_alwon_fck_clksel[] = {
	{ .parent = &omap_192m_alwon_fck, .rates = omap_96m_alwon_fck_rates },
	{ .parent = NULL }
};

static struct clk omap_96m_alwon_fck_3630;

static const char *omap_96m_alwon_fck_3630_parent_names[] = {
	"omap_192m_alwon_fck",
};

static const struct clk_ops omap_96m_alwon_fck_3630_ops = {
	.set_rate	= &omap2_clksel_set_rate,
	.recalc_rate	= &omap2_clksel_recalc,
	.round_rate	= &omap2_clksel_round_rate,
};

static struct clk_hw_omap omap_96m_alwon_fck_3630_hw = {
	.hw = {
		.clk = &omap_96m_alwon_fck_3630,
	},
	.clksel		= omap_96m_alwon_fck_clksel,
	.clksel_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
	.clksel_mask	= OMAP3630_CLKSEL_96M_MASK,
};

static struct clk omap_96m_alwon_fck_3630 = {
	.name	= "omap_96m_alwon_fck",
	.hw	= &omap_96m_alwon_fck_3630_hw.hw,
	.parent_names	= omap_96m_alwon_fck_3630_parent_names,
	.num_parents	= ARRAY_SIZE(omap_96m_alwon_fck_3630_parent_names),
	.ops	= &omap_96m_alwon_fck_3630_ops,
};

static struct clk omapctrl_ick;

static struct clk_hw_omap omapctrl_ick_hw = {
	.hw = {
		.clk = &omapctrl_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_OMAPCTRL_SHIFT,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(omapctrl_ick, aes2_ick_parent_names, aes2_ick_ops);

DEFINE_CLK_DIVIDER(pclk_fck, "emu_src_ck", &emu_src_ck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
		   OMAP3430_CLKSEL_PCLK_SHIFT, OMAP3430_CLKSEL_PCLK_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

DEFINE_CLK_DIVIDER(pclkx2_fck, "emu_src_ck", &emu_src_ck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
		   OMAP3430_CLKSEL_PCLKX2_SHIFT, OMAP3430_CLKSEL_PCLKX2_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk per_48m_fck;

DEFINE_STRUCT_CLK_HW_OMAP(per_48m_fck, "per_clkdm");
DEFINE_STRUCT_CLK(per_48m_fck, core_48m_fck_parent_names, core_l4_ick_ops);

static struct clk security_l3_ick;

DEFINE_STRUCT_CLK_HW_OMAP(security_l3_ick, NULL);
DEFINE_STRUCT_CLK(security_l3_ick, core_l3_ick_parent_names, core_ck_ops);

static struct clk pka_ick;

static const char *pka_ick_parent_names[] = {
	"security_l3_ick",
};

static struct clk_hw_omap pka_ick_hw = {
	.hw = {
		.clk = &pka_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP3430_EN_PKA_SHIFT,
};

DEFINE_STRUCT_CLK(pka_ick, pka_ick_parent_names, aes1_ick_ops);

DEFINE_CLK_DIVIDER(rm_ick, "l4_ick", &l4_ick, 0x0,
		   OMAP_CM_REGADDR(WKUP_MOD, CM_CLKSEL),
		   OMAP3430_CLKSEL_RM_SHIFT, OMAP3430_CLKSEL_RM_WIDTH,
		   CLK_DIVIDER_ONE_BASED, NULL);

static struct clk rng_ick;

static struct clk_hw_omap rng_ick_hw = {
	.hw = {
		.clk = &rng_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP3430_EN_RNG_SHIFT,
};

DEFINE_STRUCT_CLK(rng_ick, aes1_ick_parent_names, aes1_ick_ops);

static struct clk sad2d_ick;

static struct clk_hw_omap sad2d_ick_hw = {
	.hw = {
		.clk = &sad2d_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_SAD2D_SHIFT,
	.clkdm_name	= "d2d_clkdm",
};

DEFINE_STRUCT_CLK(sad2d_ick, core_l3_ick_parent_names, aes2_ick_ops);

static struct clk sdrc_ick;

static struct clk_hw_omap sdrc_ick_hw = {
	.hw = {
		.clk = &sdrc_ick,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_SDRC_SHIFT,
	.flags		= ENABLE_ON_INIT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(sdrc_ick, ipss_ick_parent_names, aes2_ick_ops);

static const struct clksel_rate sgx_core_rates[] = {
	{ .div = 2, .val = 5, .flags = RATE_IN_36XX },
	{ .div = 3, .val = 0, .flags = RATE_IN_3XXX },
	{ .div = 4, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 6, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate sgx_96m_rates[] = {
	{ .div = 1, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate sgx_192m_rates[] = {
	{ .div = 1, .val = 4, .flags = RATE_IN_36XX },
	{ .div = 0 }
};

static const struct clksel_rate sgx_corex2_rates[] = {
	{ .div = 3, .val = 6, .flags = RATE_IN_36XX },
	{ .div = 5, .val = 7, .flags = RATE_IN_36XX },
	{ .div = 0 }
};

static const struct clksel sgx_clksel[] = {
	{ .parent = &core_ck, .rates = sgx_core_rates },
	{ .parent = &cm_96m_fck, .rates = sgx_96m_rates },
	{ .parent = &omap_192m_alwon_fck, .rates = sgx_192m_rates },
	{ .parent = &corex2_fck, .rates = sgx_corex2_rates },
	{ .parent = NULL },
};

static const char *sgx_fck_parent_names[] = {
	"core_ck", "cm_96m_fck", "omap_192m_alwon_fck", "corex2_fck",
};

static struct clk sgx_fck;

static const struct clk_ops sgx_fck_ops = {
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

DEFINE_CLK_OMAP_MUX_GATE(sgx_fck, "sgx_clkdm", sgx_clksel,
			 OMAP_CM_REGADDR(OMAP3430ES2_SGX_MOD, CM_CLKSEL),
			 OMAP3430ES2_CLKSEL_SGX_MASK,
			 OMAP_CM_REGADDR(OMAP3430ES2_SGX_MOD, CM_FCLKEN),
			 OMAP3430ES2_CM_FCLKEN_SGX_EN_SGX_SHIFT,
			 &clkhwops_wait, sgx_fck_parent_names, sgx_fck_ops);

static struct clk sgx_ick;

static struct clk_hw_omap sgx_ick_hw = {
	.hw = {
		.clk = &sgx_ick,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430ES2_SGX_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430ES2_CM_ICLKEN_SGX_EN_SGX_SHIFT,
	.clkdm_name	= "sgx_clkdm",
};

DEFINE_STRUCT_CLK(sgx_ick, core_l3_ick_parent_names, aes2_ick_ops);

static struct clk sha11_ick;

static struct clk_hw_omap sha11_ick_hw = {
	.hw = {
		.clk = &sha11_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN2),
	.enable_bit	= OMAP3430_EN_SHA11_SHIFT,
};

DEFINE_STRUCT_CLK(sha11_ick, aes1_ick_parent_names, aes1_ick_ops);

static struct clk sha12_ick;

static struct clk_hw_omap sha12_ick_hw = {
	.hw = {
		.clk = &sha12_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_SHA12_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(sha12_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk sr1_fck;

static struct clk_hw_omap sr1_fck_hw = {
	.hw = {
		.clk = &sr1_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_SR1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(sr1_fck, dpll3_ck_parent_names, aes2_ick_ops);

static struct clk sr2_fck;

static struct clk_hw_omap sr2_fck_hw = {
	.hw = {
		.clk = &sr2_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_SR2_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(sr2_fck, dpll3_ck_parent_names, aes2_ick_ops);

static struct clk sr_l4_ick;

DEFINE_STRUCT_CLK_HW_OMAP(sr_l4_ick, "core_l4_clkdm");
DEFINE_STRUCT_CLK(sr_l4_ick, security_l4_ick2_parent_names, core_l4_ick_ops);

static struct clk ssi_l4_ick;

DEFINE_STRUCT_CLK_HW_OMAP(ssi_l4_ick, "core_l4_clkdm");
DEFINE_STRUCT_CLK(ssi_l4_ick, security_l4_ick2_parent_names, core_l4_ick_ops);

static struct clk ssi_ick_3430es1;

static const char *ssi_ick_3430es1_parent_names[] = {
	"ssi_l4_ick",
};

static struct clk_hw_omap ssi_ick_3430es1_hw = {
	.hw = {
		.clk = &ssi_ick_3430es1,
	},
	.ops		= &clkhwops_iclk,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_SSI_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(ssi_ick_3430es1, ssi_ick_3430es1_parent_names, aes2_ick_ops);

static struct clk ssi_ick_3430es2;

static struct clk_hw_omap ssi_ick_3430es2_hw = {
	.hw = {
		.clk = &ssi_ick_3430es2,
	},
	.ops		= &clkhwops_omap3430es2_iclk_ssi_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_SSI_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(ssi_ick_3430es2, ssi_ick_3430es1_parent_names, aes2_ick_ops);

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
	{ .parent = NULL },
};

static const char *ssi_ssr_fck_3430es1_parent_names[] = {
	"corex2_fck",
};

static const struct clk_ops ssi_ssr_fck_3430es1_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
	.recalc_rate	= &omap2_clksel_recalc,
	.set_rate	= &omap2_clksel_set_rate,
	.round_rate	= &omap2_clksel_round_rate,
};

DEFINE_CLK_OMAP_MUX_GATE(ssi_ssr_fck_3430es1, "core_l4_clkdm",
			 ssi_ssr_clksel, OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_SSI_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP3430_EN_SSI_SHIFT,
			 NULL, ssi_ssr_fck_3430es1_parent_names,
			 ssi_ssr_fck_3430es1_ops);

DEFINE_CLK_OMAP_MUX_GATE(ssi_ssr_fck_3430es2, "core_l4_clkdm",
			 ssi_ssr_clksel, OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
			 OMAP3430_CLKSEL_SSI_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
			 OMAP3430_EN_SSI_SHIFT,
			 NULL, ssi_ssr_fck_3430es1_parent_names,
			 ssi_ssr_fck_3430es1_ops);

DEFINE_CLK_FIXED_FACTOR(ssi_sst_fck_3430es1, "ssi_ssr_fck_3430es1",
			&ssi_ssr_fck_3430es1, 0x0, 1, 2);

DEFINE_CLK_FIXED_FACTOR(ssi_sst_fck_3430es2, "ssi_ssr_fck_3430es2",
			&ssi_ssr_fck_3430es2, 0x0, 1, 2);

static struct clk sys_clkout1;

static const char *sys_clkout1_parent_names[] = {
	"osc_sys_ck",
};

static struct clk_hw_omap sys_clkout1_hw = {
	.hw = {
		.clk = &sys_clkout1,
	},
	.enable_reg	= OMAP3430_PRM_CLKOUT_CTRL,
	.enable_bit	= OMAP3430_CLKOUT_EN_SHIFT,
};

DEFINE_STRUCT_CLK(sys_clkout1, sys_clkout1_parent_names, aes1_ick_ops);

DEFINE_CLK_DIVIDER(sys_clkout2, "clkout2_src_ck", &clkout2_src_ck, 0x0,
		   OMAP3430_CM_CLKOUT_CTRL, OMAP3430_CLKOUT2_DIV_SHIFT,
		   OMAP3430_CLKOUT2_DIV_WIDTH, CLK_DIVIDER_POWER_OF_TWO, NULL);

DEFINE_CLK_MUX(traceclk_src_fck, emu_src_ck_parent_names, NULL, 0x0,
	       OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
	       OMAP3430_TRACE_MUX_CTRL_SHIFT, OMAP3430_TRACE_MUX_CTRL_WIDTH,
	       0x0, NULL);

DEFINE_CLK_DIVIDER(traceclk_fck, "traceclk_src_fck", &traceclk_src_fck, 0x0,
		   OMAP_CM_REGADDR(OMAP3430_EMU_MOD, CM_CLKSEL1),
		   OMAP3430_CLKSEL_TRACECLK_SHIFT,
		   OMAP3430_CLKSEL_TRACECLK_WIDTH, CLK_DIVIDER_ONE_BASED, NULL);

static struct clk ts_fck;

static struct clk_hw_omap ts_fck_hw = {
	.hw = {
		.clk = &ts_fck,
	},
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP3430ES2_CM_FCLKEN3),
	.enable_bit	= OMAP3430ES2_EN_TS_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(ts_fck, wkup_32k_fck_parent_names, aes2_ick_ops);

static struct clk uart1_fck;

static struct clk_hw_omap uart1_fck_hw = {
	.hw = {
		.clk = &uart1_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_UART1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart1_fck, fshostusb_fck_parent_names, aes2_ick_ops);

static struct clk uart1_ick;

static struct clk_hw_omap uart1_ick_hw = {
	.hw = {
		.clk = &uart1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_UART1_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart1_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk uart2_fck;

static struct clk_hw_omap uart2_fck_hw = {
	.hw = {
		.clk = &uart2_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= OMAP3430_EN_UART2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart2_fck, fshostusb_fck_parent_names, aes2_ick_ops);

static struct clk uart2_ick;

static struct clk_hw_omap uart2_ick_hw = {
	.hw = {
		.clk = &uart2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= OMAP3430_EN_UART2_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart2_ick, aes2_ick_parent_names, aes2_ick_ops);

static struct clk uart3_fck;

static const char *uart3_fck_parent_names[] = {
	"per_48m_fck",
};

static struct clk_hw_omap uart3_fck_hw = {
	.hw = {
		.clk = &uart3_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_UART3_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(uart3_fck, uart3_fck_parent_names, aes2_ick_ops);

static struct clk uart3_ick;

static struct clk_hw_omap uart3_ick_hw = {
	.hw = {
		.clk = &uart3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_UART3_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(uart3_ick, gpio2_ick_parent_names, aes2_ick_ops);

static struct clk uart4_fck;

static struct clk_hw_omap uart4_fck_hw = {
	.hw = {
		.clk = &uart4_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3630_EN_UART4_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(uart4_fck, uart3_fck_parent_names, aes2_ick_ops);

static struct clk uart4_fck_am35xx;

static struct clk_hw_omap uart4_fck_am35xx_hw = {
	.hw = {
		.clk = &uart4_fck_am35xx,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_FCLKEN1),
	.enable_bit	= AM35XX_EN_UART4_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart4_fck_am35xx, fshostusb_fck_parent_names, aes2_ick_ops);

static struct clk uart4_ick;

static struct clk_hw_omap uart4_ick_hw = {
	.hw = {
		.clk = &uart4_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3630_EN_UART4_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(uart4_ick, gpio2_ick_parent_names, aes2_ick_ops);

static struct clk uart4_ick_am35xx;

static struct clk_hw_omap uart4_ick_am35xx_hw = {
	.hw = {
		.clk = &uart4_ick_am35xx,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
	.enable_bit	= AM35XX_EN_UART4_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(uart4_ick_am35xx, aes2_ick_parent_names, aes2_ick_ops);

static const struct clksel_rate div2_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_3XXX },
	{ .div = 2, .val = 2, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel usb_l4_clksel[] = {
	{ .parent = &l4_ick, .rates = div2_rates },
	{ .parent = NULL },
};

static const char *usb_l4_ick_parent_names[] = {
	"l4_ick",
};

DEFINE_CLK_OMAP_MUX_GATE(usb_l4_ick, "core_l4_clkdm", usb_l4_clksel,
			 OMAP_CM_REGADDR(CORE_MOD, CM_CLKSEL),
			 OMAP3430ES1_CLKSEL_FSHOSTUSB_MASK,
			 OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN1),
			 OMAP3430ES1_EN_FSHOSTUSB_SHIFT,
			 &clkhwops_iclk_wait, usb_l4_ick_parent_names,
			 ssi_ssr_fck_3430es1_ops);

static struct clk usbhost_120m_fck;

static const char *usbhost_120m_fck_parent_names[] = {
	"dpll5_m2_ck",
};

static struct clk_hw_omap usbhost_120m_fck_hw = {
	.hw = {
		.clk = &usbhost_120m_fck,
	},
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430ES2_USBHOST_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430ES2_EN_USBHOST2_SHIFT,
	.clkdm_name	= "usbhost_clkdm",
};

DEFINE_STRUCT_CLK(usbhost_120m_fck, usbhost_120m_fck_parent_names,
		  aes2_ick_ops);

static struct clk usbhost_48m_fck;

static struct clk_hw_omap usbhost_48m_fck_hw = {
	.hw = {
		.clk = &usbhost_48m_fck,
	},
	.ops		= &clkhwops_omap3430es2_dss_usbhost_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430ES2_USBHOST_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430ES2_EN_USBHOST1_SHIFT,
	.clkdm_name	= "usbhost_clkdm",
};

DEFINE_STRUCT_CLK(usbhost_48m_fck, core_48m_fck_parent_names, aes2_ick_ops);

static struct clk usbhost_ick;

static struct clk_hw_omap usbhost_ick_hw = {
	.hw = {
		.clk = &usbhost_ick,
	},
	.ops		= &clkhwops_omap3430es2_iclk_dss_usbhost_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430ES2_USBHOST_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430ES2_EN_USBHOST_SHIFT,
	.clkdm_name	= "usbhost_clkdm",
};

DEFINE_STRUCT_CLK(usbhost_ick, security_l4_ick2_parent_names, aes2_ick_ops);

static struct clk usbtll_fck;

static struct clk_hw_omap usbtll_fck_hw = {
	.hw = {
		.clk = &usbtll_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, OMAP3430ES2_CM_FCLKEN3),
	.enable_bit	= OMAP3430ES2_EN_USBTLL_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(usbtll_fck, usbhost_120m_fck_parent_names, aes2_ick_ops);

static struct clk usbtll_ick;

static struct clk_hw_omap usbtll_ick_hw = {
	.hw = {
		.clk = &usbtll_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(CORE_MOD, CM_ICLKEN3),
	.enable_bit	= OMAP3430ES2_EN_USBTLL_SHIFT,
	.clkdm_name	= "core_l4_clkdm",
};

DEFINE_STRUCT_CLK(usbtll_ick, aes2_ick_parent_names, aes2_ick_ops);

static const struct clksel_rate usim_96m_rates[] = {
	{ .div = 2, .val = 3, .flags = RATE_IN_3XXX },
	{ .div = 4, .val = 4, .flags = RATE_IN_3XXX },
	{ .div = 8, .val = 5, .flags = RATE_IN_3XXX },
	{ .div = 10, .val = 6, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel_rate usim_120m_rates[] = {
	{ .div = 4, .val = 7, .flags = RATE_IN_3XXX },
	{ .div = 8, .val = 8, .flags = RATE_IN_3XXX },
	{ .div = 16, .val = 9, .flags = RATE_IN_3XXX },
	{ .div = 20, .val = 10, .flags = RATE_IN_3XXX },
	{ .div = 0 }
};

static const struct clksel usim_clksel[] = {
	{ .parent = &omap_96m_fck, .rates = usim_96m_rates },
	{ .parent = &dpll5_m2_ck, .rates = usim_120m_rates },
	{ .parent = &sys_ck, .rates = div2_rates },
	{ .parent = NULL },
};

static const char *usim_fck_parent_names[] = {
	"omap_96m_fck", "dpll5_m2_ck", "sys_ck",
};

static struct clk usim_fck;

static const struct clk_ops usim_fck_ops = {
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
	.recalc_rate	= &omap2_clksel_recalc,
	.get_parent	= &omap2_clksel_find_parent_index,
	.set_parent	= &omap2_clksel_set_parent,
};

DEFINE_CLK_OMAP_MUX_GATE(usim_fck, NULL, usim_clksel,
			 OMAP_CM_REGADDR(WKUP_MOD, CM_CLKSEL),
			 OMAP3430ES2_CLKSEL_USIMOCP_MASK,
			 OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
			 OMAP3430ES2_EN_USIMOCP_SHIFT, &clkhwops_wait,
			 usim_fck_parent_names, usim_fck_ops);

static struct clk usim_ick;

static struct clk_hw_omap usim_ick_hw = {
	.hw = {
		.clk = &usim_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430ES2_EN_USIMOCP_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(usim_ick, gpio1_ick_parent_names, aes2_ick_ops);

static struct clk vpfe_fck;

static const char *vpfe_fck_parent_names[] = {
	"pclk_ck",
};

static struct clk_hw_omap vpfe_fck_hw = {
	.hw = {
		.clk = &vpfe_fck,
	},
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_VPFE_FCLK_SHIFT,
};

DEFINE_STRUCT_CLK(vpfe_fck, vpfe_fck_parent_names, aes1_ick_ops);

static struct clk vpfe_ick;

static struct clk_hw_omap vpfe_ick_hw = {
	.hw = {
		.clk = &vpfe_ick,
	},
	.ops		= &clkhwops_am35xx_ipss_module_wait,
	.enable_reg	= OMAP343X_CTRL_REGADDR(AM35XX_CONTROL_IPSS_CLK_CTRL),
	.enable_bit	= AM35XX_VPFE_VBUSP_CLK_SHIFT,
	.clkdm_name	= "core_l3_clkdm",
};

DEFINE_STRUCT_CLK(vpfe_ick, emac_ick_parent_names, aes2_ick_ops);

static struct clk wdt1_fck;

DEFINE_STRUCT_CLK_HW_OMAP(wdt1_fck, "wkup_clkdm");
DEFINE_STRUCT_CLK(wdt1_fck, gpt12_fck_parent_names, core_l4_ick_ops);

static struct clk wdt1_ick;

static struct clk_hw_omap wdt1_ick_hw = {
	.hw = {
		.clk = &wdt1_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_WDT1_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(wdt1_ick, gpio1_ick_parent_names, aes2_ick_ops);

static struct clk wdt2_fck;

static struct clk_hw_omap wdt2_fck_hw = {
	.hw = {
		.clk = &wdt2_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_WDT2_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(wdt2_fck, gpio1_dbck_parent_names, aes2_ick_ops);

static struct clk wdt2_ick;

static struct clk_hw_omap wdt2_ick_hw = {
	.hw = {
		.clk = &wdt2_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(WKUP_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_WDT2_SHIFT,
	.clkdm_name	= "wkup_clkdm",
};

DEFINE_STRUCT_CLK(wdt2_ick, gpio1_ick_parent_names, aes2_ick_ops);

static struct clk wdt3_fck;

static struct clk_hw_omap wdt3_fck_hw = {
	.hw = {
		.clk = &wdt3_fck,
	},
	.ops		= &clkhwops_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_FCLKEN),
	.enable_bit	= OMAP3430_EN_WDT3_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(wdt3_fck, gpio2_dbck_parent_names, aes2_ick_ops);

static struct clk wdt3_ick;

static struct clk_hw_omap wdt3_ick_hw = {
	.hw = {
		.clk = &wdt3_ick,
	},
	.ops		= &clkhwops_iclk_wait,
	.enable_reg	= OMAP_CM_REGADDR(OMAP3430_PER_MOD, CM_ICLKEN),
	.enable_bit	= OMAP3430_EN_WDT3_SHIFT,
	.clkdm_name	= "per_clkdm",
};

DEFINE_STRUCT_CLK(wdt3_ick, gpio2_ick_parent_names, aes2_ick_ops);

/*
 * clocks specific to omap3430es1
 */
static struct omap_clk omap3430es1_clks[] = {
	CLK(NULL,	"gfx_l3_ck",	&gfx_l3_ck),
	CLK(NULL,	"gfx_l3_fck",	&gfx_l3_fck),
	CLK(NULL,	"gfx_l3_ick",	&gfx_l3_ick),
	CLK(NULL,	"gfx_cg1_ck",	&gfx_cg1_ck),
	CLK(NULL,	"gfx_cg2_ck",	&gfx_cg2_ck),
	CLK(NULL,	"d2d_26m_fck",	&d2d_26m_fck),
	CLK(NULL,	"fshostusb_fck", &fshostusb_fck),
	CLK(NULL,	"ssi_ssr_fck",	&ssi_ssr_fck_3430es1),
	CLK(NULL,	"ssi_sst_fck",	&ssi_sst_fck_3430es1),
	CLK("musb-omap2430",	"ick",	&hsotgusb_ick_3430es1),
	CLK(NULL,	"hsotgusb_ick",	&hsotgusb_ick_3430es1),
	CLK(NULL,	"fac_ick",	&fac_ick),
	CLK(NULL,	"ssi_ick",	&ssi_ick_3430es1),
	CLK(NULL,	"usb_l4_ick",	&usb_l4_ick),
	CLK(NULL,	"dss1_alwon_fck",	&dss1_alwon_fck_3430es1),
	CLK("omapdss_dss",	"ick",		&dss_ick_3430es1),
	CLK(NULL,	"dss_ick",		&dss_ick_3430es1),
};

/*
 * clocks specific to am35xx
 */
static struct omap_clk am35xx_clks[] = {
	CLK(NULL,	"ipss_ick",	&ipss_ick),
	CLK(NULL,	"rmii_ck",	&rmii_ck),
	CLK(NULL,	"pclk_ck",	&pclk_ck),
	CLK(NULL,	"emac_ick",	&emac_ick),
	CLK(NULL,	"emac_fck",	&emac_fck),
	CLK("davinci_emac.0",	NULL,	&emac_ick),
	CLK("davinci_mdio.0",	NULL,	&emac_fck),
	CLK("vpfe-capture",	"master",	&vpfe_ick),
	CLK("vpfe-capture",	"slave",	&vpfe_fck),
	CLK(NULL,	"hsotgusb_ick",		&hsotgusb_ick_am35xx),
	CLK(NULL,	"hsotgusb_fck",		&hsotgusb_fck_am35xx),
	CLK(NULL,	"hecc_ck",	&hecc_ck),
	CLK(NULL,	"uart4_ick",	&uart4_ick_am35xx),
	CLK(NULL,	"uart4_fck",	&uart4_fck_am35xx),
};

/*
 * clocks specific to omap36xx
 */
static struct omap_clk omap36xx_clks[] = {
	CLK(NULL,	"omap_192m_alwon_fck", &omap_192m_alwon_fck),
	CLK(NULL,	"uart4_fck",	&uart4_fck),
};

/*
 * clocks common to omap36xx omap34xx
 */
static struct omap_clk omap34xx_omap36xx_clks[] = {
	CLK(NULL,	"aes1_ick",	&aes1_ick),
	CLK("omap_rng",	"ick",		&rng_ick),
	CLK(NULL,	"sha11_ick",	&sha11_ick),
	CLK(NULL,	"des1_ick",	&des1_ick),
	CLK(NULL,	"cam_mclk",	&cam_mclk),
	CLK(NULL,	"cam_ick",	&cam_ick),
	CLK(NULL,	"csi2_96m_fck",	&csi2_96m_fck),
	CLK(NULL,	"security_l3_ick", &security_l3_ick),
	CLK(NULL,	"pka_ick",	&pka_ick),
	CLK(NULL,	"icr_ick",	&icr_ick),
	CLK("omap-aes",	"ick",	&aes2_ick),
	CLK("omap-sham",	"ick",	&sha12_ick),
	CLK(NULL,	"des2_ick",	&des2_ick),
	CLK(NULL,	"mspro_ick",	&mspro_ick),
	CLK(NULL,	"mailboxes_ick", &mailboxes_ick),
	CLK(NULL,	"ssi_l4_ick",	&ssi_l4_ick),
	CLK(NULL,	"sr1_fck",	&sr1_fck),
	CLK(NULL,	"sr2_fck",	&sr2_fck),
	CLK(NULL,	"sr_l4_ick",	&sr_l4_ick),
	CLK(NULL,	"security_l4_ick2", &security_l4_ick2),
	CLK(NULL,	"wkup_l4_ick",	&wkup_l4_ick),
	CLK(NULL,	"dpll2_fck",	&dpll2_fck),
	CLK(NULL,	"iva2_ck",	&iva2_ck),
	CLK(NULL,	"modem_fck",	&modem_fck),
	CLK(NULL,	"sad2d_ick",	&sad2d_ick),
	CLK(NULL,	"mad2d_ick",	&mad2d_ick),
	CLK(NULL,	"mspro_fck",	&mspro_fck),
	CLK(NULL,	"dpll2_ck",	&dpll2_ck),
	CLK(NULL,	"dpll2_m2_ck",	&dpll2_m2_ck),
};

/*
 * clocks common to omap36xx and omap3430es2plus
 */
static struct omap_clk omap36xx_omap3430es2plus_clks[] = {
	CLK(NULL,	"ssi_ssr_fck",	&ssi_ssr_fck_3430es2),
	CLK(NULL,	"ssi_sst_fck",	&ssi_sst_fck_3430es2),
	CLK("musb-omap2430",	"ick",	&hsotgusb_ick_3430es2),
	CLK(NULL,	"hsotgusb_ick",	&hsotgusb_ick_3430es2),
	CLK(NULL,	"ssi_ick",	&ssi_ick_3430es2),
	CLK(NULL,	"usim_fck",	&usim_fck),
	CLK(NULL,	"usim_ick",	&usim_ick),
};

/*
 * clocks common to am35xx omap36xx and omap3430es2plus
 */
static struct omap_clk omap36xx_am35xx_omap3430es2plus_clks[] = {
	CLK(NULL,	"virt_16_8m_ck", &virt_16_8m_ck),
	CLK(NULL,	"dpll5_ck",	&dpll5_ck),
	CLK(NULL,	"dpll5_m2_ck",	&dpll5_m2_ck),
	CLK(NULL,	"sgx_fck",	&sgx_fck),
	CLK(NULL,	"sgx_ick",	&sgx_ick),
	CLK(NULL,	"cpefuse_fck",	&cpefuse_fck),
	CLK(NULL,	"ts_fck",	&ts_fck),
	CLK(NULL,	"usbtll_fck",	&usbtll_fck),
	CLK(NULL,	"usbtll_ick",	&usbtll_ick),
	CLK("omap_hsmmc.2",	"ick",	&mmchs3_ick),
	CLK(NULL,	"mmchs3_ick",	&mmchs3_ick),
	CLK(NULL,	"mmchs3_fck",	&mmchs3_fck),
	CLK(NULL,	"dss1_alwon_fck",	&dss1_alwon_fck_3430es2),
	CLK("omapdss_dss",	"ick",		&dss_ick_3430es2),
	CLK(NULL,	"dss_ick",		&dss_ick_3430es2),
	CLK(NULL,	"usbhost_120m_fck", &usbhost_120m_fck),
	CLK(NULL,	"usbhost_48m_fck", &usbhost_48m_fck),
	CLK(NULL,	"usbhost_ick",	&usbhost_ick),
};

/*
 * common clocks
 */
static struct omap_clk omap3xxx_clks[] = {
	CLK(NULL,	"apb_pclk",	&dummy_apb_pclk),
	CLK(NULL,	"omap_32k_fck",	&omap_32k_fck),
	CLK(NULL,	"virt_12m_ck",	&virt_12m_ck),
	CLK(NULL,	"virt_13m_ck",	&virt_13m_ck),
	CLK(NULL,	"virt_19200000_ck", &virt_19200000_ck),
	CLK(NULL,	"virt_26000000_ck", &virt_26000000_ck),
	CLK(NULL,	"virt_38_4m_ck", &virt_38_4m_ck),
	CLK(NULL,	"osc_sys_ck",	&osc_sys_ck),
	CLK("twl",	"fck",		&osc_sys_ck),
	CLK(NULL,	"sys_ck",	&sys_ck),
	CLK(NULL,	"omap_96m_alwon_fck", &omap_96m_alwon_fck),
	CLK("etb",	"emu_core_alwon_ck", &emu_core_alwon_ck),
	CLK(NULL,	"sys_altclk",	&sys_altclk),
	CLK(NULL,	"mcbsp_clks",	&mcbsp_clks),
	CLK(NULL,	"sys_clkout1",	&sys_clkout1),
	CLK(NULL,	"dpll1_ck",	&dpll1_ck),
	CLK(NULL,	"dpll1_x2_ck",	&dpll1_x2_ck),
	CLK(NULL,	"dpll1_x2m2_ck", &dpll1_x2m2_ck),
	CLK(NULL,	"dpll3_ck",	&dpll3_ck),
	CLK(NULL,	"core_ck",	&core_ck),
	CLK(NULL,	"dpll3_x2_ck",	&dpll3_x2_ck),
	CLK(NULL,	"dpll3_m2_ck",	&dpll3_m2_ck),
	CLK(NULL,	"dpll3_m2x2_ck", &dpll3_m2x2_ck),
	CLK(NULL,	"dpll3_m3_ck",	&dpll3_m3_ck),
	CLK(NULL,	"dpll3_m3x2_ck", &dpll3_m3x2_ck),
	CLK(NULL,	"dpll4_ck",	&dpll4_ck),
	CLK(NULL,	"dpll4_x2_ck",	&dpll4_x2_ck),
	CLK(NULL,	"omap_96m_fck",	&omap_96m_fck),
	CLK(NULL,	"cm_96m_fck",	&cm_96m_fck),
	CLK(NULL,	"omap_54m_fck",	&omap_54m_fck),
	CLK(NULL,	"omap_48m_fck",	&omap_48m_fck),
	CLK(NULL,	"omap_12m_fck",	&omap_12m_fck),
	CLK(NULL,	"dpll4_m2_ck",	&dpll4_m2_ck),
	CLK(NULL,	"dpll4_m2x2_ck", &dpll4_m2x2_ck),
	CLK(NULL,	"dpll4_m3_ck",	&dpll4_m3_ck),
	CLK(NULL,	"dpll4_m3x2_ck", &dpll4_m3x2_ck),
	CLK(NULL,	"dpll4_m4_ck",	&dpll4_m4_ck),
	CLK(NULL,	"dpll4_m4x2_ck", &dpll4_m4x2_ck),
	CLK(NULL,	"dpll4_m5_ck",	&dpll4_m5_ck),
	CLK(NULL,	"dpll4_m5x2_ck", &dpll4_m5x2_ck),
	CLK(NULL,	"dpll4_m6_ck",	&dpll4_m6_ck),
	CLK(NULL,	"dpll4_m6x2_ck", &dpll4_m6x2_ck),
	CLK("etb",	"emu_per_alwon_ck", &emu_per_alwon_ck),
	CLK(NULL,	"clkout2_src_ck", &clkout2_src_ck),
	CLK(NULL,	"sys_clkout2",	&sys_clkout2),
	CLK(NULL,	"corex2_fck",	&corex2_fck),
	CLK(NULL,	"dpll1_fck",	&dpll1_fck),
	CLK(NULL,	"mpu_ck",	&mpu_ck),
	CLK(NULL,	"arm_fck",	&arm_fck),
	CLK("etb",	"emu_mpu_alwon_ck", &emu_mpu_alwon_ck),
	CLK(NULL,	"l3_ick",	&l3_ick),
	CLK(NULL,	"l4_ick",	&l4_ick),
	CLK(NULL,	"rm_ick",	&rm_ick),
	CLK(NULL,	"gpt10_fck",	&gpt10_fck),
	CLK(NULL,	"gpt11_fck",	&gpt11_fck),
	CLK(NULL,	"core_96m_fck",	&core_96m_fck),
	CLK(NULL,	"mmchs2_fck",	&mmchs2_fck),
	CLK(NULL,	"mmchs1_fck",	&mmchs1_fck),
	CLK(NULL,	"i2c3_fck",	&i2c3_fck),
	CLK(NULL,	"i2c2_fck",	&i2c2_fck),
	CLK(NULL,	"i2c1_fck",	&i2c1_fck),
	CLK(NULL,	"mcbsp5_fck",	&mcbsp5_fck),
	CLK(NULL,	"mcbsp1_fck",	&mcbsp1_fck),
	CLK(NULL,	"core_48m_fck",	&core_48m_fck),
	CLK(NULL,	"mcspi4_fck",	&mcspi4_fck),
	CLK(NULL,	"mcspi3_fck",	&mcspi3_fck),
	CLK(NULL,	"mcspi2_fck",	&mcspi2_fck),
	CLK(NULL,	"mcspi1_fck",	&mcspi1_fck),
	CLK(NULL,	"uart2_fck",	&uart2_fck),
	CLK(NULL,	"uart1_fck",	&uart1_fck),
	CLK(NULL,	"core_12m_fck",	&core_12m_fck),
	CLK("omap_hdq.0",	"fck",	&hdq_fck),
	CLK(NULL,	"hdq_fck",	&hdq_fck),
	CLK(NULL,	"core_l3_ick",	&core_l3_ick),
	CLK(NULL,	"sdrc_ick",	&sdrc_ick),
	CLK(NULL,	"gpmc_fck",	&gpmc_fck),
	CLK(NULL,	"core_l4_ick",	&core_l4_ick),
	CLK("omap_hsmmc.1",	"ick",	&mmchs2_ick),
	CLK("omap_hsmmc.0",	"ick",	&mmchs1_ick),
	CLK(NULL,	"mmchs2_ick",	&mmchs2_ick),
	CLK(NULL,	"mmchs1_ick",	&mmchs1_ick),
	CLK("omap_hdq.0", "ick",	&hdq_ick),
	CLK(NULL,	"hdq_ick",	&hdq_ick),
	CLK("omap2_mcspi.4", "ick",	&mcspi4_ick),
	CLK("omap2_mcspi.3", "ick",	&mcspi3_ick),
	CLK("omap2_mcspi.2", "ick",	&mcspi2_ick),
	CLK("omap2_mcspi.1", "ick",	&mcspi1_ick),
	CLK(NULL,	"mcspi4_ick",	&mcspi4_ick),
	CLK(NULL,	"mcspi3_ick",	&mcspi3_ick),
	CLK(NULL,	"mcspi2_ick",	&mcspi2_ick),
	CLK(NULL,	"mcspi1_ick",	&mcspi1_ick),
	CLK("omap_i2c.3", "ick",	&i2c3_ick),
	CLK("omap_i2c.2", "ick",	&i2c2_ick),
	CLK("omap_i2c.1", "ick",	&i2c1_ick),
	CLK(NULL,	"i2c3_ick",	&i2c3_ick),
	CLK(NULL,	"i2c2_ick",	&i2c2_ick),
	CLK(NULL,	"i2c1_ick",	&i2c1_ick),
	CLK(NULL,	"uart2_ick",	&uart2_ick),
	CLK(NULL,	"uart1_ick",	&uart1_ick),
	CLK(NULL,	"gpt11_ick",	&gpt11_ick),
	CLK(NULL,	"gpt10_ick",	&gpt10_ick),
	CLK("omap-mcbsp.5", "ick",	&mcbsp5_ick),
	CLK("omap-mcbsp.1", "ick",	&mcbsp1_ick),
	CLK(NULL,	"mcbsp5_ick",	&mcbsp5_ick),
	CLK(NULL,	"mcbsp1_ick",	&mcbsp1_ick),
	CLK(NULL,	"omapctrl_ick",	&omapctrl_ick),
	CLK(NULL,	"dss_tv_fck",	&dss_tv_fck),
	CLK(NULL,	"dss_96m_fck",	&dss_96m_fck),
	CLK(NULL,	"dss2_alwon_fck",	&dss2_alwon_fck),
	CLK(NULL,	"utmi_p1_gfclk",	&dummy_ck),
	CLK(NULL,	"utmi_p2_gfclk",	&dummy_ck),
	CLK(NULL,	"xclk60mhsp1_ck",	&dummy_ck),
	CLK(NULL,	"xclk60mhsp2_ck",	&dummy_ck),
	CLK(NULL,	"init_60m_fclk",	&dummy_ck),
	CLK(NULL,	"gpt1_fck",	&gpt1_fck),
	CLK(NULL,	"aes2_ick",	&aes2_ick),
	CLK(NULL,	"wkup_32k_fck",	&wkup_32k_fck),
	CLK(NULL,	"gpio1_dbck",	&gpio1_dbck),
	CLK(NULL,	"sha12_ick",	&sha12_ick),
	CLK(NULL,	"wdt2_fck",		&wdt2_fck),
	CLK("omap_wdt",	"ick",		&wdt2_ick),
	CLK(NULL,	"wdt2_ick",	&wdt2_ick),
	CLK(NULL,	"wdt1_ick",	&wdt1_ick),
	CLK(NULL,	"gpio1_ick",	&gpio1_ick),
	CLK(NULL,	"omap_32ksync_ick", &omap_32ksync_ick),
	CLK(NULL,	"gpt12_ick",	&gpt12_ick),
	CLK(NULL,	"gpt1_ick",	&gpt1_ick),
	CLK(NULL,	"per_96m_fck",	&per_96m_fck),
	CLK(NULL,	"per_48m_fck",	&per_48m_fck),
	CLK(NULL,	"uart3_fck",	&uart3_fck),
	CLK(NULL,	"gpt2_fck",	&gpt2_fck),
	CLK(NULL,	"gpt3_fck",	&gpt3_fck),
	CLK(NULL,	"gpt4_fck",	&gpt4_fck),
	CLK(NULL,	"gpt5_fck",	&gpt5_fck),
	CLK(NULL,	"gpt6_fck",	&gpt6_fck),
	CLK(NULL,	"gpt7_fck",	&gpt7_fck),
	CLK(NULL,	"gpt8_fck",	&gpt8_fck),
	CLK(NULL,	"gpt9_fck",	&gpt9_fck),
	CLK(NULL,	"per_32k_alwon_fck", &per_32k_alwon_fck),
	CLK(NULL,	"gpio6_dbck",	&gpio6_dbck),
	CLK(NULL,	"gpio5_dbck",	&gpio5_dbck),
	CLK(NULL,	"gpio4_dbck",	&gpio4_dbck),
	CLK(NULL,	"gpio3_dbck",	&gpio3_dbck),
	CLK(NULL,	"gpio2_dbck",	&gpio2_dbck),
	CLK(NULL,	"wdt3_fck",	&wdt3_fck),
	CLK(NULL,	"per_l4_ick",	&per_l4_ick),
	CLK(NULL,	"gpio6_ick",	&gpio6_ick),
	CLK(NULL,	"gpio5_ick",	&gpio5_ick),
	CLK(NULL,	"gpio4_ick",	&gpio4_ick),
	CLK(NULL,	"gpio3_ick",	&gpio3_ick),
	CLK(NULL,	"gpio2_ick",	&gpio2_ick),
	CLK(NULL,	"wdt3_ick",	&wdt3_ick),
	CLK(NULL,	"uart3_ick",	&uart3_ick),
	CLK(NULL,	"uart4_ick",	&uart4_ick),
	CLK(NULL,	"gpt9_ick",	&gpt9_ick),
	CLK(NULL,	"gpt8_ick",	&gpt8_ick),
	CLK(NULL,	"gpt7_ick",	&gpt7_ick),
	CLK(NULL,	"gpt6_ick",	&gpt6_ick),
	CLK(NULL,	"gpt5_ick",	&gpt5_ick),
	CLK(NULL,	"gpt4_ick",	&gpt4_ick),
	CLK(NULL,	"gpt3_ick",	&gpt3_ick),
	CLK(NULL,	"gpt2_ick",	&gpt2_ick),
	CLK("omap-mcbsp.2", "ick",	&mcbsp2_ick),
	CLK("omap-mcbsp.3", "ick",	&mcbsp3_ick),
	CLK("omap-mcbsp.4", "ick",	&mcbsp4_ick),
	CLK(NULL,	"mcbsp4_ick",	&mcbsp2_ick),
	CLK(NULL,	"mcbsp3_ick",	&mcbsp3_ick),
	CLK(NULL,	"mcbsp2_ick",	&mcbsp4_ick),
	CLK(NULL,	"mcbsp2_fck",	&mcbsp2_fck),
	CLK(NULL,	"mcbsp3_fck",	&mcbsp3_fck),
	CLK(NULL,	"mcbsp4_fck",	&mcbsp4_fck),
	CLK("etb",	"emu_src_ck",	&emu_src_ck),
	CLK(NULL,	"emu_src_ck",	&emu_src_ck),
	CLK(NULL,	"pclk_fck",	&pclk_fck),
	CLK(NULL,	"pclkx2_fck",	&pclkx2_fck),
	CLK(NULL,	"atclk_fck",	&atclk_fck),
	CLK(NULL,	"traceclk_src_fck", &traceclk_src_fck),
	CLK(NULL,	"traceclk_fck",	&traceclk_fck),
	CLK(NULL,	"secure_32k_fck", &secure_32k_fck),
	CLK(NULL,	"gpt12_fck",	&gpt12_fck),
	CLK(NULL,	"wdt1_fck",	&wdt1_fck),
	CLK(NULL,	"timer_32k_ck",	&omap_32k_fck),
	CLK(NULL,	"timer_sys_ck",	&sys_ck),
	CLK(NULL,	"cpufreq_ck",	&dpll1_ck),
};

static const char *enable_init_clks[] = {
	"sdrc_ick",
	"gpmc_fck",
	"omapctrl_ick",
};

int __init omap3xxx_clk_init(void)
{
	if (omap3_has_192mhz_clk())
		omap_96m_alwon_fck = omap_96m_alwon_fck_3630;

	if (cpu_is_omap3630()) {
		dpll3_m3x2_ck = dpll3_m3x2_ck_3630;
		dpll4_m2x2_ck = dpll4_m2x2_ck_3630;
		dpll4_m3x2_ck = dpll4_m3x2_ck_3630;
		dpll4_m4x2_ck = dpll4_m4x2_ck_3630;
		dpll4_m5x2_ck = dpll4_m5x2_ck_3630;
		dpll4_m6x2_ck = dpll4_m6x2_ck_3630;
	}

	/*
	 * XXX This type of dynamic rewriting of the clock tree is
	 * deprecated and should be revised soon.
	 */
	if (cpu_is_omap3630())
		dpll4_dd = dpll4_dd_3630;
	else
		dpll4_dd = dpll4_dd_34xx;


	/*
	 * 3505 must be tested before 3517, since 3517 returns true
	 * for both AM3517 chips and AM3517 family chips, which
	 * includes 3505.  Unfortunately there's no obvious family
	 * test for 3517/3505 :-(
	 */
	if (soc_is_am35xx()) {
		cpu_mask = RATE_IN_34XX;
		omap_clocks_register(am35xx_clks, ARRAY_SIZE(am35xx_clks));
		omap_clocks_register(omap36xx_am35xx_omap3430es2plus_clks,
				     ARRAY_SIZE(omap36xx_am35xx_omap3430es2plus_clks));
		omap_clocks_register(omap3xxx_clks, ARRAY_SIZE(omap3xxx_clks));
	} else if (cpu_is_omap3630()) {
		cpu_mask = (RATE_IN_34XX | RATE_IN_36XX);
		omap_clocks_register(omap36xx_clks, ARRAY_SIZE(omap36xx_clks));
		omap_clocks_register(omap36xx_omap3430es2plus_clks,
				     ARRAY_SIZE(omap36xx_omap3430es2plus_clks));
		omap_clocks_register(omap34xx_omap36xx_clks,
				     ARRAY_SIZE(omap34xx_omap36xx_clks));
		omap_clocks_register(omap36xx_am35xx_omap3430es2plus_clks,
				     ARRAY_SIZE(omap36xx_am35xx_omap3430es2plus_clks));
		omap_clocks_register(omap3xxx_clks, ARRAY_SIZE(omap3xxx_clks));
	} else if (soc_is_am33xx()) {
		cpu_mask = RATE_IN_AM33XX;
	} else if (cpu_is_ti814x()) {
		cpu_mask = RATE_IN_TI814X;
	} else if (cpu_is_omap34xx()) {
		if (omap_rev() == OMAP3430_REV_ES1_0) {
			cpu_mask = RATE_IN_3430ES1;
			omap_clocks_register(omap3430es1_clks,
					     ARRAY_SIZE(omap3430es1_clks));
			omap_clocks_register(omap34xx_omap36xx_clks,
					     ARRAY_SIZE(omap34xx_omap36xx_clks));
			omap_clocks_register(omap3xxx_clks,
					     ARRAY_SIZE(omap3xxx_clks));
		} else {
			/*
			 * Assume that anything that we haven't matched yet
			 * has 3430ES2-type clocks.
			 */
			cpu_mask = RATE_IN_3430ES2PLUS;
			omap_clocks_register(omap34xx_omap36xx_clks,
					     ARRAY_SIZE(omap34xx_omap36xx_clks));
			omap_clocks_register(omap36xx_omap3430es2plus_clks,
					     ARRAY_SIZE(omap36xx_omap3430es2plus_clks));
			omap_clocks_register(omap36xx_am35xx_omap3430es2plus_clks,
					     ARRAY_SIZE(omap36xx_am35xx_omap3430es2plus_clks));
			omap_clocks_register(omap3xxx_clks,
					     ARRAY_SIZE(omap3xxx_clks));
		}
	} else {
		WARN(1, "clock: could not identify OMAP3 variant\n");
	}

		omap2_clk_disable_autoidle_all();

	omap2_clk_enable_init_clocks(enable_init_clks,
				     ARRAY_SIZE(enable_init_clks));

	pr_info("Clocking rate (Crystal/Core/MPU): %ld.%01ld/%ld/%ld MHz\n",
		(clk_get_rate(&osc_sys_ck) / 1000000),
		(clk_get_rate(&osc_sys_ck) / 100000) % 10,
		(clk_get_rate(&core_ck) / 1000000),
		(clk_get_rate(&arm_fck) / 1000000));

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
