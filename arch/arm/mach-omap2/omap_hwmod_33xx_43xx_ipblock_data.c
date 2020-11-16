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

static void omap_hwmod_am33xx_clkctrl(void)
{
	CLKCTRL(am33xx_smartreflex0_hwmod,
		AM33XX_CM_WKUP_SMARTREFLEX0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_smartreflex1_hwmod,
		AM33XX_CM_WKUP_SMARTREFLEX1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_ls_hwmod, AM33XX_CM_PER_L4LS_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_wkup_hwmod, AM33XX_CM_WKUP_L4WKUP_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_main_hwmod, AM33XX_CM_PER_L3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mpu_hwmod , AM33XX_CM_MPU_MPU_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_instr_hwmod , AM33XX_CM_PER_L3_INSTR_CLKCTRL_OFFSET);
}

void omap_hwmod_am33xx_reg(void)
{
	omap_hwmod_am33xx_clkctrl();
}

static void omap_hwmod_am43xx_clkctrl(void)
{
	CLKCTRL(am33xx_smartreflex0_hwmod,
		AM43XX_CM_WKUP_SMARTREFLEX0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_smartreflex1_hwmod,
		AM43XX_CM_WKUP_SMARTREFLEX1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_ls_hwmod, AM43XX_CM_PER_L4LS_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_wkup_hwmod, AM43XX_CM_WKUP_L4WKUP_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_main_hwmod, AM43XX_CM_PER_L3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mpu_hwmod , AM43XX_CM_MPU_MPU_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_instr_hwmod , AM43XX_CM_PER_L3_INSTR_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_ocmcram_hwmod , AM43XX_CM_PER_OCMCRAM_CLKCTRL_OFFSET);
}

void omap_hwmod_am43xx_reg(void)
{
	omap_hwmod_am43xx_clkctrl();
}
