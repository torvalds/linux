/*
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 *
 * Hwmod common for AM335x and AM43x
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

#include <linux/types.h>

#include "omap_hwmod.h"
#include "cm33xx.h"
#include "prm33xx.h"
#include "omap_hwmod_33xx_43xx_common_data.h"
#include "prcm43xx.h"
#include "common.h"

#define CLKCTRL(oh, clkctrl) ((oh).prcm.omap4.clkctrl_offs = (clkctrl))
#define RSTCTRL(oh, rstctrl) ((oh).prcm.omap4.rstctrl_offs = (rstctrl))
#define RSTST(oh, rstst) ((oh).prcm.omap4.rstst_offs = (rstst))
#define PRCM_FLAGS(oh, flag) ((oh).prcm.omap4.flags = (flag))

/*
 * 'l3' class
 * instance(s): l3_main, l3_s, l3_instr
 */
static struct omap_hwmod_class am33xx_l3_hwmod_class = {
	.name		= "l3",
};

struct omap_hwmod am33xx_l3_main_hwmod = {
	.name		= "l3_main",
	.class		= &am33xx_l3_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.flags		= HWMOD_INIT_NO_IDLE,
	.main_clk	= "l3_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* l3_s */
struct omap_hwmod am33xx_l3_s_hwmod = {
	.name		= "l3_s",
	.class		= &am33xx_l3_hwmod_class,
	.clkdm_name	= "l3s_clkdm",
};

/* l3_instr */
struct omap_hwmod am33xx_l3_instr_hwmod = {
	.name		= "l3_instr",
	.class		= &am33xx_l3_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.flags		= HWMOD_INIT_NO_IDLE,
	.main_clk	= "l3_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/*
 * 'l4' class
 * instance(s): l4_ls, l4_hs, l4_wkup, l4_fw
 */
struct omap_hwmod_class am33xx_l4_hwmod_class = {
	.name		= "l4",
};

/* l4_ls */
struct omap_hwmod am33xx_l4_ls_hwmod = {
	.name		= "l4_ls",
	.class		= &am33xx_l4_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_INIT_NO_IDLE,
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* l4_wkup */
struct omap_hwmod am33xx_l4_wkup_hwmod = {
	.name		= "l4_wkup",
	.class		= &am33xx_l4_hwmod_class,
	.clkdm_name	= "l4_wkup_clkdm",
	.flags		= HWMOD_INIT_NO_IDLE,
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/*
 * 'mpu' class
 */
static struct omap_hwmod_class am33xx_mpu_hwmod_class = {
	.name	= "mpu",
};

struct omap_hwmod am33xx_mpu_hwmod = {
	.name		= "mpu",
	.class		= &am33xx_mpu_hwmod_class,
	.clkdm_name	= "mpu_clkdm",
	.flags		= HWMOD_INIT_NO_IDLE,
	.main_clk	= "dpll_mpu_m2_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/*
 * 'wakeup m3' class
 * Wakeup controller sub-system under wakeup domain
 */
struct omap_hwmod_class am33xx_wkup_m3_hwmod_class = {
	.name		= "wkup_m3",
};

/* gfx */
/* Pseudo hwmod for reset control purpose only */
static struct omap_hwmod_class am33xx_gfx_hwmod_class = {
	.name	= "gfx",
};

static struct omap_hwmod_rst_info am33xx_gfx_resets[] = {
	{ .name = "gfx", .rst_shift = 0, .st_shift = 0},
};

struct omap_hwmod am33xx_gfx_hwmod = {
	.name		= "gfx",
	.class		= &am33xx_gfx_hwmod_class,
	.clkdm_name	= "gfx_l3_clkdm",
	.main_clk	= "gfx_fck_div_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.rst_lines	= am33xx_gfx_resets,
	.rst_lines_cnt	= ARRAY_SIZE(am33xx_gfx_resets),
};

/*
 * 'prcm' class
 * power and reset manager (whole prcm infrastructure)
 */
static struct omap_hwmod_class am33xx_prcm_hwmod_class = {
	.name	= "prcm",
};

/* prcm */
struct omap_hwmod am33xx_prcm_hwmod = {
	.name		= "prcm",
	.class		= &am33xx_prcm_hwmod_class,
	.clkdm_name	= "l4_wkup_clkdm",
};

/*
 * 'emif' class
 * instance(s): emif
 */
static struct omap_hwmod_class_sysconfig am33xx_emif_sysc = {
	.rev_offs	= 0x0000,
};

struct omap_hwmod_class am33xx_emif_hwmod_class = {
	.name		= "emif",
	.sysc		= &am33xx_emif_sysc,
};



/* ocmcram */
static struct omap_hwmod_class am33xx_ocmcram_hwmod_class = {
	.name = "ocmcram",
};

struct omap_hwmod am33xx_ocmcram_hwmod = {
	.name		= "ocmcram",
	.class		= &am33xx_ocmcram_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.flags		= HWMOD_INIT_NO_IDLE,
	.main_clk	= "l3_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* 'smartreflex' class */
static struct omap_hwmod_class am33xx_smartreflex_hwmod_class = {
	.name		= "smartreflex",
};

/* smartreflex0 */
struct omap_hwmod am33xx_smartreflex0_hwmod = {
	.name		= "smartreflex0",
	.class		= &am33xx_smartreflex_hwmod_class,
	.clkdm_name	= "l4_wkup_clkdm",
	.main_clk	= "smartreflex0_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* smartreflex1 */
struct omap_hwmod am33xx_smartreflex1_hwmod = {
	.name		= "smartreflex1",
	.class		= &am33xx_smartreflex_hwmod_class,
	.clkdm_name	= "l4_wkup_clkdm",
	.main_clk	= "smartreflex1_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/*
 * 'control' module class
 */
struct omap_hwmod_class am33xx_control_hwmod_class = {
	.name		= "control",
};


/* gpmc */
static struct omap_hwmod_class_sysconfig gpmc_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x14,
	.sysc_flags	= (SYSC_HAS_AUTOIDLE | SYSC_HAS_SIDLEMODE |
			SYSC_HAS_SOFTRESET | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class am33xx_gpmc_hwmod_class = {
	.name		= "gpmc",
	.sysc		= &gpmc_sysc,
};

struct omap_hwmod am33xx_gpmc_hwmod = {
	.name		= "gpmc",
	.class		= &am33xx_gpmc_hwmod_class,
	.clkdm_name	= "l3s_clkdm",
	/* Skip reset for CONFIG_OMAP_GPMC_DEBUG for bootloader timings */
	.flags		= DEBUG_OMAP_GPMC_HWMOD_FLAGS,
	.main_clk	= "l3s_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};


/*
 * 'rtc' class
 * rtc subsystem
 */
static struct omap_hwmod_class_sysconfig am33xx_rtc_sysc = {
	.rev_offs	= 0x0074,
	.sysc_offs	= 0x0078,
	.sysc_flags	= SYSC_HAS_SIDLEMODE,
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO |
			  SIDLE_SMART | SIDLE_SMART_WKUP),
	.sysc_fields	= &omap_hwmod_sysc_type3,
};

static struct omap_hwmod_class am33xx_rtc_hwmod_class = {
	.name		= "rtc",
	.sysc		= &am33xx_rtc_sysc,
	.unlock		= &omap_hwmod_rtc_unlock,
	.lock		= &omap_hwmod_rtc_lock,
};

struct omap_hwmod am33xx_rtc_hwmod = {
	.name		= "rtc",
	.class		= &am33xx_rtc_hwmod_class,
	.clkdm_name	= "l4_rtc_clkdm",
	.main_clk	= "clk_32768_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* 'timer 2-7' class */
static struct omap_hwmod_class_sysconfig am33xx_timer_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET |
			  SYSC_HAS_RESET_STATUS,
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			  SIDLE_SMART_WKUP),
	.sysc_fields	= &omap_hwmod_sysc_type2,
};

struct omap_hwmod_class am33xx_timer_hwmod_class = {
	.name		= "timer",
	.sysc		= &am33xx_timer_sysc,
};

/* timer1 1ms */
static struct omap_hwmod_class_sysconfig am33xx_timer1ms_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class am33xx_timer1ms_hwmod_class = {
	.name		= "timer",
	.sysc		= &am33xx_timer1ms_sysc,
};

struct omap_hwmod am33xx_timer1_hwmod = {
	.name		= "timer1",
	.class		= &am33xx_timer1ms_hwmod_class,
	.clkdm_name	= "l4_wkup_clkdm",
	.main_clk	= "timer1_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

struct omap_hwmod am33xx_timer2_hwmod = {
	.name		= "timer2",
	.class		= &am33xx_timer_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "timer2_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static void omap_hwmod_am33xx_clkctrl(void)
{
	CLKCTRL(am33xx_timer2_hwmod, AM33XX_CM_PER_TIMER2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_smartreflex0_hwmod,
		AM33XX_CM_WKUP_SMARTREFLEX0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_smartreflex1_hwmod,
		AM33XX_CM_WKUP_SMARTREFLEX1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer1_hwmod, AM33XX_CM_WKUP_TIMER1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_rtc_hwmod, AM33XX_CM_RTC_RTC_CLKCTRL_OFFSET);
	PRCM_FLAGS(am33xx_rtc_hwmod, HWMOD_OMAP4_ZERO_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gpmc_hwmod, AM33XX_CM_PER_GPMC_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_ls_hwmod, AM33XX_CM_PER_L4LS_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_wkup_hwmod, AM33XX_CM_WKUP_L4WKUP_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_main_hwmod, AM33XX_CM_PER_L3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gfx_hwmod, AM33XX_CM_GFX_GFX_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mpu_hwmod , AM33XX_CM_MPU_MPU_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_instr_hwmod , AM33XX_CM_PER_L3_INSTR_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_ocmcram_hwmod , AM33XX_CM_PER_OCMCRAM_CLKCTRL_OFFSET);
}

static void omap_hwmod_am33xx_rst(void)
{
	RSTCTRL(am33xx_gfx_hwmod, AM33XX_RM_GFX_RSTCTRL_OFFSET);
	RSTST(am33xx_gfx_hwmod, AM33XX_RM_GFX_RSTST_OFFSET);
}

void omap_hwmod_am33xx_reg(void)
{
	omap_hwmod_am33xx_clkctrl();
	omap_hwmod_am33xx_rst();
}

static void omap_hwmod_am43xx_clkctrl(void)
{
	CLKCTRL(am33xx_timer2_hwmod, AM43XX_CM_PER_TIMER2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_smartreflex0_hwmod,
		AM43XX_CM_WKUP_SMARTREFLEX0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_smartreflex1_hwmod,
		AM43XX_CM_WKUP_SMARTREFLEX1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer1_hwmod, AM43XX_CM_WKUP_TIMER1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_rtc_hwmod, AM43XX_CM_RTC_RTC_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gpmc_hwmod, AM43XX_CM_PER_GPMC_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_ls_hwmod, AM43XX_CM_PER_L4LS_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_wkup_hwmod, AM43XX_CM_WKUP_L4WKUP_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_main_hwmod, AM43XX_CM_PER_L3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gfx_hwmod, AM43XX_CM_GFX_GFX_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mpu_hwmod , AM43XX_CM_MPU_MPU_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_instr_hwmod , AM43XX_CM_PER_L3_INSTR_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_ocmcram_hwmod , AM43XX_CM_PER_OCMCRAM_CLKCTRL_OFFSET);
}

static void omap_hwmod_am43xx_rst(void)
{
	RSTCTRL(am33xx_gfx_hwmod, AM43XX_RM_GFX_RSTCTRL_OFFSET);
	RSTST(am33xx_gfx_hwmod, AM43XX_RM_GFX_RSTST_OFFSET);
}

void omap_hwmod_am43xx_reg(void)
{
	omap_hwmod_am43xx_clkctrl();
	omap_hwmod_am43xx_rst();
}
