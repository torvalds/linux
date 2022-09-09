// SPDX-License-Identifier: GPL-2.0-only
/*
 * omap_hwmod_2430_data.c - hardware modules present on the OMAP2430 chips
 *
 * Copyright (C) 2009-2011 Nokia Corporation
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * XXX handle crossbar/shared link difference for L3?
 * XXX these should be marked initdata for multi-OMAP kernels
 */

#include <linux/platform_data/i2c-omap.h>
#include <linux/platform_data/hsmmc-omap.h>

#include "omap_hwmod.h"
#include "l3_2xxx.h"

#include "soc.h"
#include "omap_hwmod_common_data.h"
#include "prm-regbits-24xx.h"
#include "cm-regbits-24xx.h"
#include "i2c.h"
#include "wd_timer.h"

/*
 * OMAP2430 hardware module integration data
 *
 * All of the data in this section should be autogeneratable from the
 * TI hardware database or other technical documentation.  Data that
 * is driver-specific or driver-kernel integration-specific belongs
 * elsewhere.
 */

/*
 * IP blocks
 */

/* IVA2 (IVA2) */
static struct omap_hwmod_rst_info omap2430_iva_resets[] = {
	{ .name = "logic", .rst_shift = 0 },
	{ .name = "mmu", .rst_shift = 1 },
};

static struct omap_hwmod omap2430_iva_hwmod = {
	.name		= "iva",
	.class		= &iva_hwmod_class,
	.clkdm_name	= "dsp_clkdm",
	.rst_lines	= omap2430_iva_resets,
	.rst_lines_cnt	= ARRAY_SIZE(omap2430_iva_resets),
	.main_clk	= "dsp_fck",
};

/* I2C common */
static struct omap_hwmod_class_sysconfig i2c_sysc = {
	.rev_offs	= 0x00,
	.sysc_offs	= 0x20,
	.syss_offs	= 0x10,
	.sysc_flags	= (SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class i2c_class = {
	.name		= "i2c",
	.sysc		= &i2c_sysc,
	.reset		= &omap_i2c_reset,
};

/* I2C1 */
static struct omap_hwmod omap2430_i2c1_hwmod = {
	.name		= "i2c1",
	.flags		= HWMOD_16BIT_REG,
	.main_clk	= "i2chs1_fck",
	.prcm		= {
		.omap2 = {
			/*
			 * NOTE: The CM_FCLKEN* and CM_ICLKEN* for
			 * I2CHS IP's do not follow the usual pattern.
			 * prcm_reg_id alone cannot be used to program
			 * the iclk and fclk. Needs to be handled using
			 * additional flags when clk handling is moved
			 * to hwmod framework.
			 */
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_I2CHS1_SHIFT,
		},
	},
	.class		= &i2c_class,
};

/* I2C2 */
static struct omap_hwmod omap2430_i2c2_hwmod = {
	.name		= "i2c2",
	.flags		= HWMOD_16BIT_REG,
	.main_clk	= "i2chs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_I2CHS2_SHIFT,
		},
	},
	.class		= &i2c_class,
};

/* gpio5 */
static struct omap_hwmod omap2430_gpio5_hwmod = {
	.name		= "gpio5",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.main_clk	= "gpio5_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_GPIO5_SHIFT,
		},
	},
	.class		= &omap2xxx_gpio_hwmod_class,
};

/* mailbox */
static struct omap_hwmod omap2430_mailbox_hwmod = {
	.name		= "mailbox",
	.class		= &omap2xxx_mailbox_hwmod_class,
	.main_clk	= "mailboxes_ick",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MAILBOXES_SHIFT,
		},
	},
};

/* mcspi3 */
static struct omap_hwmod omap2430_mcspi3_hwmod = {
	.name		= "mcspi3",
	.main_clk	= "mcspi3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCSPI3_SHIFT,
		},
	},
	.class		= &omap2xxx_mcspi_class,
};

/* usbhsotg */
static struct omap_hwmod_class_sysconfig omap2430_usbhsotg_sysc = {
	.rev_offs	= 0x0400,
	.sysc_offs	= 0x0404,
	.syss_offs	= 0x0408,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE|
			  SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			  SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			  MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class usbotg_class = {
	.name = "usbotg",
	.sysc = &omap2430_usbhsotg_sysc,
};

/* usb_otg_hs */
static struct omap_hwmod omap2430_usbhsotg_hwmod = {
	.name		= "usb_otg_hs",
	.main_clk	= "usbhs_ick",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_USBHS_SHIFT,
		},
	},
	.class		= &usbotg_class,
	/*
	 * Erratum ID: i479  idle_req / idle_ack mechanism potentially
	 * broken when autoidle is enabled
	 * workaround is to disable the autoidle bit at module level.
	 */
	.flags		= HWMOD_NO_OCP_AUTOIDLE | HWMOD_SWSUP_SIDLE
				| HWMOD_SWSUP_MSTANDBY,
};

/*
 * 'mcbsp' class
 * multi channel buffered serial port controller
 */

static struct omap_hwmod_class_sysconfig omap2430_mcbsp_sysc = {
	.rev_offs	= 0x007C,
	.sysc_offs	= 0x008C,
	.sysc_flags	= (SYSC_HAS_SOFTRESET),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_mcbsp_hwmod_class = {
	.name = "mcbsp",
	.sysc = &omap2430_mcbsp_sysc,
};

static struct omap_hwmod_opt_clk mcbsp_opt_clks[] = {
	{ .role = "pad_fck", .clk = "mcbsp_clks" },
	{ .role = "prcm_fck", .clk = "func_96m_ck" },
};

/* mcbsp1 */
static struct omap_hwmod omap2430_mcbsp1_hwmod = {
	.name		= "mcbsp1",
	.class		= &omap2430_mcbsp_hwmod_class,
	.main_clk	= "mcbsp1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCBSP1_SHIFT,
		},
	},
	.opt_clks	= mcbsp_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp_opt_clks),
};

/* mcbsp2 */
static struct omap_hwmod omap2430_mcbsp2_hwmod = {
	.name		= "mcbsp2",
	.class		= &omap2430_mcbsp_hwmod_class,
	.main_clk	= "mcbsp2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCBSP2_SHIFT,
		},
	},
	.opt_clks	= mcbsp_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp_opt_clks),
};

/* mcbsp3 */
static struct omap_hwmod omap2430_mcbsp3_hwmod = {
	.name		= "mcbsp3",
	.class		= &omap2430_mcbsp_hwmod_class,
	.main_clk	= "mcbsp3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP3_SHIFT,
		},
	},
	.opt_clks	= mcbsp_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp_opt_clks),
};

/* mcbsp4 */
static struct omap_hwmod omap2430_mcbsp4_hwmod = {
	.name		= "mcbsp4",
	.class		= &omap2430_mcbsp_hwmod_class,
	.main_clk	= "mcbsp4_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP4_SHIFT,
		},
	},
	.opt_clks	= mcbsp_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp_opt_clks),
};

/* mcbsp5 */
static struct omap_hwmod omap2430_mcbsp5_hwmod = {
	.name		= "mcbsp5",
	.class		= &omap2430_mcbsp_hwmod_class,
	.main_clk	= "mcbsp5_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP5_SHIFT,
		},
	},
	.opt_clks	= mcbsp_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp_opt_clks),
};

/* MMC/SD/SDIO common */
static struct omap_hwmod_class_sysconfig omap2430_mmc_sysc = {
	.rev_offs	= 0x1fc,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x14,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_mmc_class = {
	.name = "mmc",
	.sysc = &omap2430_mmc_sysc,
};

/* MMC/SD/SDIO1 */
static struct omap_hwmod_opt_clk omap2430_mmc1_opt_clks[] = {
	{ .role = "dbck", .clk = "mmchsdb1_fck" },
};

static struct omap_hsmmc_dev_attr mmc1_dev_attr = {
	.flags = OMAP_HSMMC_SUPPORTS_DUAL_VOLT,
};

static struct omap_hwmod omap2430_mmc1_hwmod = {
	.name		= "mmc1",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.opt_clks	= omap2430_mmc1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap2430_mmc1_opt_clks),
	.main_clk	= "mmchs1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MMCHS1_SHIFT,
		},
	},
	.dev_attr	= &mmc1_dev_attr,
	.class		= &omap2430_mmc_class,
};

/* MMC/SD/SDIO2 */
static struct omap_hwmod_opt_clk omap2430_mmc2_opt_clks[] = {
	{ .role = "dbck", .clk = "mmchsdb2_fck" },
};

static struct omap_hwmod omap2430_mmc2_hwmod = {
	.name		= "mmc2",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.opt_clks	= omap2430_mmc2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap2430_mmc2_opt_clks),
	.main_clk	= "mmchs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MMCHS2_SHIFT,
		},
	},
	.class		= &omap2430_mmc_class,
};

/* HDQ1W/1-wire */
static struct omap_hwmod omap2430_hdq1w_hwmod = {
	.name		= "hdq1w",
	.main_clk	= "hdq_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_HDQ_SHIFT,
		},
	},
	.class		= &omap2_hdq1w_class,
};

/*
 * interfaces
 */

/* L3 -> L4_CORE interface */
/* l3_core -> usbhsotg  interface */
static struct omap_hwmod_ocp_if omap2430_usbhsotg__l3 = {
	.master		= &omap2430_usbhsotg_hwmod,
	.slave		= &omap2xxx_l3_main_hwmod,
	.clk		= "core_l3_ck",
	.user		= OCP_USER_MPU,
};

/* L4 CORE -> I2C1 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__i2c1 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_i2c1_hwmod,
	.clk		= "i2c1_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C2 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__i2c2 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_i2c2_hwmod,
	.clk		= "i2c2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/*  l4_core ->usbhsotg  interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__usbhsotg = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_usbhsotg_hwmod,
	.clk		= "usb_l4_ick",
	.user		= OCP_USER_MPU,
};

/* L4 CORE -> MMC1 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mmc1 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mmc1_hwmod,
	.clk		= "mmchs1_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> MMC2 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mmc2 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mmc2_hwmod,
	.clk		= "mmchs2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi3 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcspi3 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcspi3_hwmod,
	.clk		= "mcspi3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* IVA2 <- L3 interface */
static struct omap_hwmod_ocp_if omap2430_l3__iva = {
	.master		= &omap2xxx_l3_main_hwmod,
	.slave		= &omap2430_iva_hwmod,
	.clk		= "core_l3_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> wd_timer2 */
static struct omap_hwmod_ocp_if omap2430_l4_wkup__wd_timer2 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_wd_timer2_hwmod,
	.clk		= "mpu_wdt_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio1 */
static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio1 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_gpio1_hwmod,
	.clk		= "gpios_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio2 */
static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio2 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_gpio2_hwmod,
	.clk		= "gpios_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio3 */
static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio3 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_gpio3_hwmod,
	.clk		= "gpios_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio4 */
static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio4 = {
	.master		= &omap2xxx_l4_wkup_hwmod,
	.slave		= &omap2xxx_gpio4_hwmod,
	.clk		= "gpios_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> gpio5 */
static struct omap_hwmod_ocp_if omap2430_l4_core__gpio5 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_gpio5_hwmod,
	.clk		= "gpio5_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> mailbox */
static struct omap_hwmod_ocp_if omap2430_l4_core__mailbox = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mailbox_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> mcbsp1 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp1 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcbsp1_hwmod,
	.clk		= "mcbsp1_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> mcbsp2 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp2 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcbsp2_hwmod,
	.clk		= "mcbsp2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> mcbsp3 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp3 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcbsp3_hwmod,
	.clk		= "mcbsp3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> mcbsp4 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp4 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcbsp4_hwmod,
	.clk		= "mcbsp4_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> mcbsp5 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp5 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_mcbsp5_hwmod,
	.clk		= "mcbsp5_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> hdq1w */
static struct omap_hwmod_ocp_if omap2430_l4_core__hdq1w = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2430_hdq1w_hwmod,
	.clk		= "hdq_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4 | OCPIF_SWSUP_IDLE,
};

static struct omap_hwmod_ocp_if omap2430_l3__gpmc = {
	.master		= &omap2xxx_l3_main_hwmod,
	.slave		= &omap2xxx_gpmc_hwmod,
	.clk		= "core_l3_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if *omap2430_hwmod_ocp_ifs[] __initdata = {
	&omap2xxx_l3_main__l4_core,
	&omap2xxx_mpu__l3_main,
	&omap2xxx_dss__l3,
	&omap2430_usbhsotg__l3,
	&omap2430_l4_core__i2c1,
	&omap2430_l4_core__i2c2,
	&omap2xxx_l4_core__l4_wkup,
	&omap2_l4_core__uart1,
	&omap2_l4_core__uart2,
	&omap2_l4_core__uart3,
	&omap2430_l4_core__usbhsotg,
	&omap2430_l4_core__mmc1,
	&omap2430_l4_core__mmc2,
	&omap2xxx_l4_core__mcspi1,
	&omap2xxx_l4_core__mcspi2,
	&omap2430_l4_core__mcspi3,
	&omap2430_l3__iva,
	&omap2xxx_l4_core__timer3,
	&omap2xxx_l4_core__timer4,
	&omap2xxx_l4_core__timer5,
	&omap2xxx_l4_core__timer6,
	&omap2xxx_l4_core__timer7,
	&omap2xxx_l4_core__timer8,
	&omap2xxx_l4_core__timer9,
	&omap2xxx_l4_core__timer10,
	&omap2xxx_l4_core__timer11,
	&omap2xxx_l4_core__timer12,
	&omap2430_l4_wkup__wd_timer2,
	&omap2xxx_l4_core__dss,
	&omap2xxx_l4_core__dss_dispc,
	&omap2xxx_l4_core__dss_rfbi,
	&omap2xxx_l4_core__dss_venc,
	&omap2430_l4_wkup__gpio1,
	&omap2430_l4_wkup__gpio2,
	&omap2430_l4_wkup__gpio3,
	&omap2430_l4_wkup__gpio4,
	&omap2430_l4_core__gpio5,
	&omap2430_l4_core__mailbox,
	&omap2430_l4_core__mcbsp1,
	&omap2430_l4_core__mcbsp2,
	&omap2430_l4_core__mcbsp3,
	&omap2430_l4_core__mcbsp4,
	&omap2430_l4_core__mcbsp5,
	&omap2430_l4_core__hdq1w,
	&omap2xxx_l4_core__rng,
	&omap2xxx_l4_core__sham,
	&omap2xxx_l4_core__aes,
	&omap2430_l3__gpmc,
	NULL,
};

int __init omap2430_hwmod_init(void)
{
	omap_hwmod_init();
	return omap_hwmod_register_links(omap2430_hwmod_ocp_ifs);
}
