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

#include <linux/platform_data/gpio-omap.h>
#include <linux/platform_data/spi-omap2-mcspi.h>
#include "omap_hwmod.h"
#include "i2c.h"
#include "mmc.h"
#include "wd_timer.h"
#include "cm33xx.h"
#include "prm33xx.h"
#include "omap_hwmod_33xx_43xx_common_data.h"
#include "prcm43xx.h"

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
 * 'pru-icss' class
 * Programmable Real-Time Unit and Industrial Communication Subsystem
 */
static struct omap_hwmod_class am33xx_pruss_hwmod_class = {
	.name	= "pruss",
};

static struct omap_hwmod_rst_info am33xx_pruss_resets[] = {
	{ .name = "pruss", .rst_shift = 1 },
};

/* pru-icss */
/* Pseudo hwmod for reset control purpose only */
struct omap_hwmod am33xx_pruss_hwmod = {
	.name		= "pruss",
	.class		= &am33xx_pruss_hwmod_class,
	.clkdm_name	= "pruss_ocp_clkdm",
	.main_clk	= "pruss_ocp_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.rst_lines	= am33xx_pruss_resets,
	.rst_lines_cnt	= ARRAY_SIZE(am33xx_pruss_resets),
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
 * 'aes0' class
 */
static struct omap_hwmod_class_sysconfig am33xx_aes0_sysc = {
	.rev_offs	= 0x80,
	.sysc_offs	= 0x84,
	.syss_offs	= 0x88,
	.sysc_flags	= SYSS_HAS_RESET_STATUS,
};

static struct omap_hwmod_class am33xx_aes0_hwmod_class = {
	.name		= "aes0",
	.sysc		= &am33xx_aes0_sysc,
};

struct omap_hwmod am33xx_aes0_hwmod = {
	.name		= "aes",
	.class		= &am33xx_aes0_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.main_clk	= "aes0_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* sha0 HIB2 (the 'P' (public) device) */
static struct omap_hwmod_class_sysconfig am33xx_sha0_sysc = {
	.rev_offs	= 0x100,
	.sysc_offs	= 0x110,
	.syss_offs	= 0x114,
	.sysc_flags	= SYSS_HAS_RESET_STATUS,
};

static struct omap_hwmod_class am33xx_sha0_hwmod_class = {
	.name		= "sha0",
	.sysc		= &am33xx_sha0_sysc,
};

struct omap_hwmod am33xx_sha0_hwmod = {
	.name		= "sham",
	.class		= &am33xx_sha0_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.main_clk	= "l3_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
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

/*
 * 'cpgmac' class
 * cpsw/cpgmac sub system
 */
static struct omap_hwmod_class_sysconfig am33xx_cpgmac_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x8,
	.syss_offs	= 0x4,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | MSTANDBY_FORCE |
			   MSTANDBY_NO),
	.sysc_fields	= &omap_hwmod_sysc_type3,
};

static struct omap_hwmod_class am33xx_cpgmac0_hwmod_class = {
	.name		= "cpgmac0",
	.sysc		= &am33xx_cpgmac_sysc,
};

struct omap_hwmod am33xx_cpgmac0_hwmod = {
	.name		= "cpgmac0",
	.class		= &am33xx_cpgmac0_hwmod_class,
	.clkdm_name	= "cpsw_125mhz_clkdm",
	.flags		= (HWMOD_SWSUP_SIDLE | HWMOD_SWSUP_MSTANDBY),
	.main_clk	= "cpsw_125mhz_gclk",
	.mpu_rt_idx	= 1,
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/*
 * mdio class
 */
static struct omap_hwmod_class am33xx_mdio_hwmod_class = {
	.name		= "davinci_mdio",
};

struct omap_hwmod am33xx_mdio_hwmod = {
	.name		= "davinci_mdio",
	.class		= &am33xx_mdio_hwmod_class,
	.clkdm_name	= "cpsw_125mhz_clkdm",
	.main_clk	= "cpsw_125mhz_gclk",
};

/*
 * dcan class
 */
static struct omap_hwmod_class am33xx_dcan_hwmod_class = {
	.name = "d_can",
};

/* dcan0 */
struct omap_hwmod am33xx_dcan0_hwmod = {
	.name		= "d_can0",
	.class		= &am33xx_dcan_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "dcan0_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* dcan1 */
struct omap_hwmod am33xx_dcan1_hwmod = {
	.name		= "d_can1",
	.class		= &am33xx_dcan_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "dcan1_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* elm */
static struct omap_hwmod_class_sysconfig am33xx_elm_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class am33xx_elm_hwmod_class = {
	.name		= "elm",
	.sysc		= &am33xx_elm_sysc,
};

struct omap_hwmod am33xx_elm_hwmod = {
	.name		= "elm",
	.class		= &am33xx_elm_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* pwmss  */
static struct omap_hwmod_class_sysconfig am33xx_epwmss_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x4,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			SIDLE_SMART_WKUP | MSTANDBY_FORCE | MSTANDBY_NO |
			MSTANDBY_SMART | MSTANDBY_SMART_WKUP),
	.sysc_fields	= &omap_hwmod_sysc_type2,
};

struct omap_hwmod_class am33xx_epwmss_hwmod_class = {
	.name		= "epwmss",
	.sysc		= &am33xx_epwmss_sysc,
};

static struct omap_hwmod_class am33xx_ecap_hwmod_class = {
	.name		= "ecap",
};

static struct omap_hwmod_class am33xx_eqep_hwmod_class = {
	.name		= "eqep",
};

struct omap_hwmod_class am33xx_ehrpwm_hwmod_class = {
	.name		= "ehrpwm",
};

/* epwmss0 */
struct omap_hwmod am33xx_epwmss0_hwmod = {
	.name		= "epwmss0",
	.class		= &am33xx_epwmss_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* ecap0 */
struct omap_hwmod am33xx_ecap0_hwmod = {
	.name		= "ecap0",
	.class		= &am33xx_ecap_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
};

/* eqep0 */
struct omap_hwmod am33xx_eqep0_hwmod = {
	.name		= "eqep0",
	.class		= &am33xx_eqep_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
};

/* ehrpwm0 */
struct omap_hwmod am33xx_ehrpwm0_hwmod = {
	.name		= "ehrpwm0",
	.class		= &am33xx_ehrpwm_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
};

/* epwmss1 */
struct omap_hwmod am33xx_epwmss1_hwmod = {
	.name		= "epwmss1",
	.class		= &am33xx_epwmss_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* ecap1 */
struct omap_hwmod am33xx_ecap1_hwmod = {
	.name		= "ecap1",
	.class		= &am33xx_ecap_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
};

/* eqep1 */
struct omap_hwmod am33xx_eqep1_hwmod = {
	.name		= "eqep1",
	.class		= &am33xx_eqep_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
};

/* ehrpwm1 */
struct omap_hwmod am33xx_ehrpwm1_hwmod = {
	.name		= "ehrpwm1",
	.class		= &am33xx_ehrpwm_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
};

/* epwmss2 */
struct omap_hwmod am33xx_epwmss2_hwmod = {
	.name		= "epwmss2",
	.class		= &am33xx_epwmss_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* ecap2 */
struct omap_hwmod am33xx_ecap2_hwmod = {
	.name		= "ecap2",
	.class		= &am33xx_ecap_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
};

/* eqep2 */
struct omap_hwmod am33xx_eqep2_hwmod = {
	.name		= "eqep2",
	.class		= &am33xx_eqep_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
};

/* ehrpwm2 */
struct omap_hwmod am33xx_ehrpwm2_hwmod = {
	.name		= "ehrpwm2",
	.class		= &am33xx_ehrpwm_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
};

/*
 * 'gpio' class: for gpio 0,1,2,3
 */
static struct omap_hwmod_class_sysconfig am33xx_gpio_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0114,
	.sysc_flags	= (SYSC_HAS_AUTOIDLE | SYSC_HAS_ENAWAKEUP |
			  SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET |
			  SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			  SIDLE_SMART_WKUP),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class am33xx_gpio_hwmod_class = {
	.name		= "gpio",
	.sysc		= &am33xx_gpio_sysc,
	.rev		= 2,
};

struct omap_gpio_dev_attr gpio_dev_attr = {
	.bank_width	= 32,
	.dbck_flag	= true,
};

/* gpio1 */
static struct omap_hwmod_opt_clk gpio1_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio1_dbclk" },
};

struct omap_hwmod am33xx_gpio1_hwmod = {
	.name		= "gpio2",
	.class		= &am33xx_gpio_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.opt_clks	= gpio1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio1_opt_clks),
	.dev_attr	= &gpio_dev_attr,
};

/* gpio2 */
static struct omap_hwmod_opt_clk gpio2_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio2_dbclk" },
};

struct omap_hwmod am33xx_gpio2_hwmod = {
	.name		= "gpio3",
	.class		= &am33xx_gpio_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.opt_clks	= gpio2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio2_opt_clks),
	.dev_attr	= &gpio_dev_attr,
};

/* gpio3 */
static struct omap_hwmod_opt_clk gpio3_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio3_dbclk" },
};

struct omap_hwmod am33xx_gpio3_hwmod = {
	.name		= "gpio4",
	.class		= &am33xx_gpio_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.main_clk	= "l4ls_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.opt_clks	= gpio3_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio3_opt_clks),
	.dev_attr	= &gpio_dev_attr,
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
	.flags		= (HWMOD_INIT_NO_IDLE | HWMOD_INIT_NO_RESET),
	.main_clk	= "l3s_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* 'i2c' class */
static struct omap_hwmod_class_sysconfig am33xx_i2c_sysc = {
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0090,
	.sysc_flags	= (SYSC_HAS_AUTOIDLE | SYSC_HAS_CLOCKACTIVITY |
			  SYSC_HAS_ENAWAKEUP | SYSC_HAS_SIDLEMODE |
			  SYSC_HAS_SOFTRESET | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			  SIDLE_SMART_WKUP),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class i2c_class = {
	.name		= "i2c",
	.sysc		= &am33xx_i2c_sysc,
	.rev		= OMAP_I2C_IP_VERSION_2,
	.reset		= &omap_i2c_reset,
};

static struct omap_i2c_dev_attr i2c_dev_attr = {
	.flags = OMAP_I2C_FLAG_BUS_SHIFT_NONE,
};

/* i2c1 */
struct omap_hwmod am33xx_i2c1_hwmod = {
	.name		= "i2c1",
	.class		= &i2c_class,
	.clkdm_name	= "l4_wkup_clkdm",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.main_clk	= "dpll_per_m2_div4_wkupdm_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &i2c_dev_attr,
};

/* i2c1 */
struct omap_hwmod am33xx_i2c2_hwmod = {
	.name		= "i2c2",
	.class		= &i2c_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4 = {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &i2c_dev_attr,
};

/* i2c3 */
struct omap_hwmod am33xx_i2c3_hwmod = {
	.name		= "i2c3",
	.class		= &i2c_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &i2c_dev_attr,
};

/*
 * 'mailbox' class
 * mailbox module allowing communication between the on-chip processors using a
 * queued mailbox-interrupt mechanism.
 */
static struct omap_hwmod_class_sysconfig am33xx_mailbox_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.sysc_flags	= (SYSC_HAS_RESET_STATUS | SYSC_HAS_SIDLEMODE |
			  SYSC_HAS_SOFTRESET),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type2,
};

static struct omap_hwmod_class am33xx_mailbox_hwmod_class = {
	.name	= "mailbox",
	.sysc	= &am33xx_mailbox_sysc,
};

struct omap_hwmod am33xx_mailbox_hwmod = {
	.name		= "mailbox",
	.class		= &am33xx_mailbox_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
	.prcm = {
		.omap4 = {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/*
 * 'mcasp' class
 */
static struct omap_hwmod_class_sysconfig am33xx_mcasp_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x4,
	.sysc_flags	= SYSC_HAS_SIDLEMODE,
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type3,
};

static struct omap_hwmod_class am33xx_mcasp_hwmod_class = {
	.name		= "mcasp",
	.sysc		= &am33xx_mcasp_sysc,
};

/* mcasp0 */
struct omap_hwmod am33xx_mcasp0_hwmod = {
	.name		= "mcasp0",
	.class		= &am33xx_mcasp_hwmod_class,
	.clkdm_name	= "l3s_clkdm",
	.main_clk	= "mcasp0_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* mcasp1 */
struct omap_hwmod am33xx_mcasp1_hwmod = {
	.name		= "mcasp1",
	.class		= &am33xx_mcasp_hwmod_class,
	.clkdm_name	= "l3s_clkdm",
	.main_clk	= "mcasp1_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* 'mmc' class */
static struct omap_hwmod_class_sysconfig am33xx_mmc_sysc = {
	.rev_offs	= 0x1fc,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x14,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			  SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			  SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class am33xx_mmc_hwmod_class = {
	.name		= "mmc",
	.sysc		= &am33xx_mmc_sysc,
};

/* mmc0 */
static struct omap_mmc_dev_attr am33xx_mmc0_dev_attr = {
	.flags		= OMAP_HSMMC_SUPPORTS_DUAL_VOLT,
};

struct omap_hwmod am33xx_mmc0_hwmod = {
	.name		= "mmc1",
	.class		= &am33xx_mmc_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "mmc_clk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &am33xx_mmc0_dev_attr,
};

/* mmc1 */
static struct omap_mmc_dev_attr am33xx_mmc1_dev_attr = {
	.flags		= OMAP_HSMMC_SUPPORTS_DUAL_VOLT,
};

struct omap_hwmod am33xx_mmc1_hwmod = {
	.name		= "mmc2",
	.class		= &am33xx_mmc_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "mmc_clk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &am33xx_mmc1_dev_attr,
};

/* mmc2 */
static struct omap_mmc_dev_attr am33xx_mmc2_dev_attr = {
	.flags		= OMAP_HSMMC_SUPPORTS_DUAL_VOLT,
};
struct omap_hwmod am33xx_mmc2_hwmod = {
	.name		= "mmc3",
	.class		= &am33xx_mmc_hwmod_class,
	.clkdm_name	= "l3s_clkdm",
	.main_clk	= "mmc_clk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &am33xx_mmc2_dev_attr,
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

/* 'spi' class */
static struct omap_hwmod_class_sysconfig am33xx_mcspi_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0110,
	.syss_offs	= 0x0114,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			  SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			  SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class am33xx_spi_hwmod_class = {
	.name		= "mcspi",
	.sysc		= &am33xx_mcspi_sysc,
	.rev		= OMAP4_MCSPI_REV,
};

/* spi0 */
struct omap2_mcspi_dev_attr mcspi_attrib = {
	.num_chipselect	= 2,
};
struct omap_hwmod am33xx_spi0_hwmod = {
	.name		= "spi0",
	.class		= &am33xx_spi_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &mcspi_attrib,
};

/* spi1 */
struct omap_hwmod am33xx_spi1_hwmod = {
	.name		= "spi1",
	.class		= &am33xx_spi_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &mcspi_attrib,
};

/*
 * 'spinlock' class
 * spinlock provides hardware assistance for synchronizing the
 * processes running on multiple processors
 */

static struct omap_hwmod_class_sysconfig am33xx_spinlock_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_AUTOIDLE | SYSC_HAS_CLOCKACTIVITY |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class am33xx_spinlock_hwmod_class = {
	.name		= "spinlock",
	.sysc		= &am33xx_spinlock_sysc,
};

struct omap_hwmod am33xx_spinlock_hwmod = {
	.name		= "spinlock",
	.class		= &am33xx_spinlock_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "l4ls_gclk",
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
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET),
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

struct omap_hwmod am33xx_timer3_hwmod = {
	.name		= "timer3",
	.class		= &am33xx_timer_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "timer3_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

struct omap_hwmod am33xx_timer4_hwmod = {
	.name		= "timer4",
	.class		= &am33xx_timer_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "timer4_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

struct omap_hwmod am33xx_timer5_hwmod = {
	.name		= "timer5",
	.class		= &am33xx_timer_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "timer5_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

struct omap_hwmod am33xx_timer6_hwmod = {
	.name		= "timer6",
	.class		= &am33xx_timer_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "timer6_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

struct omap_hwmod am33xx_timer7_hwmod = {
	.name		= "timer7",
	.class		= &am33xx_timer_hwmod_class,
	.clkdm_name	= "l4ls_clkdm",
	.main_clk	= "timer7_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* tpcc */
static struct omap_hwmod_class am33xx_tpcc_hwmod_class = {
	.name		= "tpcc",
};

struct omap_hwmod am33xx_tpcc_hwmod = {
	.name		= "tpcc",
	.class		= &am33xx_tpcc_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.main_clk	= "l3_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_class_sysconfig am33xx_tptc_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x10,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET |
			  SYSC_HAS_MIDLEMODE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_SMART | MSTANDBY_FORCE),
	.sysc_fields	= &omap_hwmod_sysc_type2,
};

/* 'tptc' class */
static struct omap_hwmod_class am33xx_tptc_hwmod_class = {
	.name		= "tptc",
	.sysc		= &am33xx_tptc_sysc,
};

/* tptc0 */
struct omap_hwmod am33xx_tptc0_hwmod = {
	.name		= "tptc0",
	.class		= &am33xx_tptc_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.flags		= HWMOD_SWSUP_SIDLE | HWMOD_SWSUP_MSTANDBY,
	.main_clk	= "l3_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* tptc1 */
struct omap_hwmod am33xx_tptc1_hwmod = {
	.name		= "tptc1",
	.class		= &am33xx_tptc_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.flags		= (HWMOD_SWSUP_SIDLE | HWMOD_SWSUP_MSTANDBY),
	.main_clk	= "l3_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* tptc2 */
struct omap_hwmod am33xx_tptc2_hwmod = {
	.name		= "tptc2",
	.class		= &am33xx_tptc_hwmod_class,
	.clkdm_name	= "l3_clkdm",
	.flags		= (HWMOD_SWSUP_SIDLE | HWMOD_SWSUP_MSTANDBY),
	.main_clk	= "l3_gclk",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* 'uart' class */
static struct omap_hwmod_class_sysconfig uart_sysc = {
	.rev_offs	= 0x50,
	.sysc_offs	= 0x54,
	.syss_offs	= 0x58,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_ENAWAKEUP |
			  SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			  SIDLE_SMART_WKUP),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class uart_class = {
	.name		= "uart",
	.sysc		= &uart_sysc,
};

struct omap_hwmod am33xx_uart1_hwmod = {
	.name		= "uart1",
	.class		= &uart_class,
	.clkdm_name	= "l4_wkup_clkdm",
	.flags		= DEBUG_AM33XXUART1_FLAGS | HWMOD_SWSUP_SIDLE_ACT,
	.main_clk	= "dpll_per_m2_div4_wkupdm_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

struct omap_hwmod am33xx_uart2_hwmod = {
	.name		= "uart2",
	.class		= &uart_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_SWSUP_SIDLE_ACT,
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* uart3 */
struct omap_hwmod am33xx_uart3_hwmod = {
	.name		= "uart3",
	.class		= &uart_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_SWSUP_SIDLE_ACT,
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

struct omap_hwmod am33xx_uart4_hwmod = {
	.name		= "uart4",
	.class		= &uart_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_SWSUP_SIDLE_ACT,
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

struct omap_hwmod am33xx_uart5_hwmod = {
	.name		= "uart5",
	.class		= &uart_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_SWSUP_SIDLE_ACT,
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

struct omap_hwmod am33xx_uart6_hwmod = {
	.name		= "uart6",
	.class		= &uart_class,
	.clkdm_name	= "l4ls_clkdm",
	.flags		= HWMOD_SWSUP_SIDLE_ACT,
	.main_clk	= "dpll_per_m2_div4_ck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

/* 'wd_timer' class */
static struct omap_hwmod_class_sysconfig wdt_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x14,
	.sysc_flags	= (SYSC_HAS_EMUFREE | SYSC_HAS_SIDLEMODE |
			SYSC_HAS_SOFTRESET | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			SIDLE_SMART_WKUP),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class am33xx_wd_timer_hwmod_class = {
	.name		= "wd_timer",
	.sysc		= &wdt_sysc,
	.pre_shutdown	= &omap2_wd_timer_disable,
};

/*
 * XXX: device.c file uses hardcoded name for watchdog timer
 * driver "wd_timer2, so we are also using same name as of now...
 */
struct omap_hwmod am33xx_wd_timer1_hwmod = {
	.name		= "wd_timer2",
	.class		= &am33xx_wd_timer_hwmod_class,
	.clkdm_name	= "l4_wkup_clkdm",
	.flags		= HWMOD_SWSUP_SIDLE,
	.main_clk	= "wdt1_fck",
	.prcm		= {
		.omap4	= {
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static void omap_hwmod_am33xx_clkctrl(void)
{
	CLKCTRL(am33xx_uart2_hwmod, AM33XX_CM_PER_UART1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_uart3_hwmod, AM33XX_CM_PER_UART2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_uart4_hwmod, AM33XX_CM_PER_UART3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_uart5_hwmod, AM33XX_CM_PER_UART4_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_uart6_hwmod, AM33XX_CM_PER_UART5_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_dcan0_hwmod, AM33XX_CM_PER_DCAN0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_dcan1_hwmod, AM33XX_CM_PER_DCAN1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_elm_hwmod, AM33XX_CM_PER_ELM_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_epwmss0_hwmod, AM33XX_CM_PER_EPWMSS0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_epwmss1_hwmod, AM33XX_CM_PER_EPWMSS1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_epwmss2_hwmod, AM33XX_CM_PER_EPWMSS2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gpio1_hwmod, AM33XX_CM_PER_GPIO1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gpio2_hwmod, AM33XX_CM_PER_GPIO2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gpio3_hwmod, AM33XX_CM_PER_GPIO3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_i2c2_hwmod, AM33XX_CM_PER_I2C1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_i2c3_hwmod, AM33XX_CM_PER_I2C2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mailbox_hwmod, AM33XX_CM_PER_MAILBOX0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mcasp0_hwmod, AM33XX_CM_PER_MCASP0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mcasp1_hwmod, AM33XX_CM_PER_MCASP1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mmc0_hwmod, AM33XX_CM_PER_MMC0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mmc1_hwmod, AM33XX_CM_PER_MMC1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_spi0_hwmod, AM33XX_CM_PER_SPI0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_spi1_hwmod, AM33XX_CM_PER_SPI1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_spinlock_hwmod, AM33XX_CM_PER_SPINLOCK_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer2_hwmod, AM33XX_CM_PER_TIMER2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer3_hwmod, AM33XX_CM_PER_TIMER3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer4_hwmod, AM33XX_CM_PER_TIMER4_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer5_hwmod, AM33XX_CM_PER_TIMER5_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer6_hwmod, AM33XX_CM_PER_TIMER6_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer7_hwmod, AM33XX_CM_PER_TIMER7_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_smartreflex0_hwmod,
		AM33XX_CM_WKUP_SMARTREFLEX0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_smartreflex1_hwmod,
		AM33XX_CM_WKUP_SMARTREFLEX1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_uart1_hwmod, AM33XX_CM_WKUP_UART0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer1_hwmod, AM33XX_CM_WKUP_TIMER1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_i2c1_hwmod, AM33XX_CM_WKUP_I2C0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_wd_timer1_hwmod, AM33XX_CM_WKUP_WDT1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_rtc_hwmod, AM33XX_CM_RTC_RTC_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mmc2_hwmod, AM33XX_CM_PER_MMC2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gpmc_hwmod, AM33XX_CM_PER_GPMC_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_ls_hwmod, AM33XX_CM_PER_L4LS_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_wkup_hwmod, AM33XX_CM_WKUP_L4WKUP_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_main_hwmod, AM33XX_CM_PER_L3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_tpcc_hwmod, AM33XX_CM_PER_TPCC_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_tptc0_hwmod, AM33XX_CM_PER_TPTC0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_tptc1_hwmod, AM33XX_CM_PER_TPTC1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_tptc2_hwmod, AM33XX_CM_PER_TPTC2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gfx_hwmod, AM33XX_CM_GFX_GFX_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_cpgmac0_hwmod, AM33XX_CM_PER_CPGMAC0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_pruss_hwmod, AM33XX_CM_PER_PRUSS_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mpu_hwmod , AM33XX_CM_MPU_MPU_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_instr_hwmod , AM33XX_CM_PER_L3_INSTR_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_ocmcram_hwmod , AM33XX_CM_PER_OCMCRAM_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_sha0_hwmod , AM33XX_CM_PER_SHA0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_aes0_hwmod , AM33XX_CM_PER_AES0_CLKCTRL_OFFSET);
}

static void omap_hwmod_am33xx_rst(void)
{
	RSTCTRL(am33xx_pruss_hwmod, AM33XX_RM_PER_RSTCTRL_OFFSET);
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
	CLKCTRL(am33xx_uart2_hwmod, AM43XX_CM_PER_UART1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_uart3_hwmod, AM43XX_CM_PER_UART2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_uart4_hwmod, AM43XX_CM_PER_UART3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_uart5_hwmod, AM43XX_CM_PER_UART4_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_uart6_hwmod, AM43XX_CM_PER_UART5_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_dcan0_hwmod, AM43XX_CM_PER_DCAN0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_dcan1_hwmod, AM43XX_CM_PER_DCAN1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_elm_hwmod, AM43XX_CM_PER_ELM_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_epwmss0_hwmod, AM43XX_CM_PER_EPWMSS0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_epwmss1_hwmod, AM43XX_CM_PER_EPWMSS1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_epwmss2_hwmod, AM43XX_CM_PER_EPWMSS2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gpio1_hwmod, AM43XX_CM_PER_GPIO1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gpio2_hwmod, AM43XX_CM_PER_GPIO2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gpio3_hwmod, AM43XX_CM_PER_GPIO3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_i2c2_hwmod, AM43XX_CM_PER_I2C1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_i2c3_hwmod, AM43XX_CM_PER_I2C2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mailbox_hwmod, AM43XX_CM_PER_MAILBOX0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mcasp0_hwmod, AM43XX_CM_PER_MCASP0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mcasp1_hwmod, AM43XX_CM_PER_MCASP1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mmc0_hwmod, AM43XX_CM_PER_MMC0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mmc1_hwmod, AM43XX_CM_PER_MMC1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_spi0_hwmod, AM43XX_CM_PER_SPI0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_spi1_hwmod, AM43XX_CM_PER_SPI1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_spinlock_hwmod, AM43XX_CM_PER_SPINLOCK_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer2_hwmod, AM43XX_CM_PER_TIMER2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer3_hwmod, AM43XX_CM_PER_TIMER3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer4_hwmod, AM43XX_CM_PER_TIMER4_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer5_hwmod, AM43XX_CM_PER_TIMER5_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer6_hwmod, AM43XX_CM_PER_TIMER6_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer7_hwmod, AM43XX_CM_PER_TIMER7_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_smartreflex0_hwmod,
		AM43XX_CM_WKUP_SMARTREFLEX0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_smartreflex1_hwmod,
		AM43XX_CM_WKUP_SMARTREFLEX1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_uart1_hwmod, AM43XX_CM_WKUP_UART0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_timer1_hwmod, AM43XX_CM_WKUP_TIMER1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_i2c1_hwmod, AM43XX_CM_WKUP_I2C0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_wd_timer1_hwmod, AM43XX_CM_WKUP_WDT1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_rtc_hwmod, AM43XX_CM_RTC_RTC_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mmc2_hwmod, AM43XX_CM_PER_MMC2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gpmc_hwmod, AM43XX_CM_PER_GPMC_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_ls_hwmod, AM43XX_CM_PER_L4LS_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l4_wkup_hwmod, AM43XX_CM_WKUP_L4WKUP_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_main_hwmod, AM43XX_CM_PER_L3_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_tpcc_hwmod, AM43XX_CM_PER_TPCC_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_tptc0_hwmod, AM43XX_CM_PER_TPTC0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_tptc1_hwmod, AM43XX_CM_PER_TPTC1_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_tptc2_hwmod, AM43XX_CM_PER_TPTC2_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_gfx_hwmod, AM43XX_CM_GFX_GFX_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_cpgmac0_hwmod, AM43XX_CM_PER_CPGMAC0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_pruss_hwmod, AM43XX_CM_PER_PRUSS_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_mpu_hwmod , AM43XX_CM_MPU_MPU_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_l3_instr_hwmod , AM43XX_CM_PER_L3_INSTR_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_ocmcram_hwmod , AM43XX_CM_PER_OCMCRAM_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_sha0_hwmod , AM43XX_CM_PER_SHA0_CLKCTRL_OFFSET);
	CLKCTRL(am33xx_aes0_hwmod , AM43XX_CM_PER_AES0_CLKCTRL_OFFSET);
}

static void omap_hwmod_am43xx_rst(void)
{
	RSTCTRL(am33xx_pruss_hwmod, AM43XX_RM_PER_RSTCTRL_OFFSET);
	RSTCTRL(am33xx_gfx_hwmod, AM43XX_RM_GFX_RSTCTRL_OFFSET);
	RSTST(am33xx_gfx_hwmod, AM43XX_RM_GFX_RSTST_OFFSET);
}

void omap_hwmod_am43xx_reg(void)
{
	omap_hwmod_am43xx_clkctrl();
	omap_hwmod_am43xx_rst();
}
